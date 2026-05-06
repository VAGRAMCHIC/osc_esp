#include "tcp_server.h"
#include "config.h"
#include "wifi.h"
#include "ads1115.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "usb_serial.h"
#include "http_server.h"
#include "lwip/sockets.h"
#include <string.h>

static const char *TAG = "TCP";

static void tcp_server_task(void *arg) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        vTaskDelete(NULL);
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    }
    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_sock, 1);
    ESP_LOGI(TAG, "TCP server listening on port %d", TCP_PORT);

    int client_sock = -1;
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }
        ESP_LOGI(TAG, "Client connected");

        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 "{\"format\":\"binary\",\"sample_rate_hz\":%d,\"amplitude_volts\":4.096}\n",
                 SAMPLE_RATE_HZ);
        send(client_sock, greeting, strlen(greeting), 0);

        int64_t next_time = esp_timer_get_time();
        while (wifi_is_connected() && client_sock >= 0) {
            float voltage_raw = ads1115_read_voltage();
            float voltage = voltage_raw * get_probe_multiplier();
            float timestamp = (float)(esp_timer_get_time() / 1000000.0);

            uint8_t buffer[8];
            memcpy(buffer, &timestamp, 4);
            memcpy(buffer + 4, &voltage, 4);

            if (send(client_sock, buffer, 8, 0) < 0) {
                break;
            }
            usb_serial_send(buffer, 8);
            next_time += SAMPLE_INTERVAL_US;
            int64_t now = esp_timer_get_time();
            int64_t delay = next_time - now;
            if (delay > 0) {
                usleep(delay);
            } else {
                next_time = now;
            }
        }
        close(client_sock);
        client_sock = -1;
        ESP_LOGI(TAG, "Client disconnected");
    }
}

void tcp_server_start(void) {
    xTaskCreate(tcp_server_task, "tcp_server", 8192, NULL, 5, NULL);
}
