#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

#define TAG "OSC"
#define WIFI_SSID     "MikroTik-8E25DC"
#define WIFI_PASS     "qwertypoweroverus12"
#define TCP_PORT      9999
#define ADC_PIN       ADC_CHANNEL_6   // GPIO34
#define SAMPLE_RATE_HZ 500            // 500 отсчётов в секунду
#define SAMPLE_INTERVAL_US (1000000 / SAMPLE_RATE_HZ)  // микросекунды

static int client_sock = -1;
static bool wifi_ok = false;
static adc_oneshot_unit_handle_t adc = NULL;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_ok = false;
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifi_ok = true;
        ESP_LOGI(TAG, "Got IP");
    }
}

static void wifi_init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static float read_voltage() {
    int raw = 0;
    if (adc_oneshot_read(adc, ADC_PIN, &raw) != ESP_OK) return -1;
    return raw * 3.3f / 4095.0f;
}

static void tcp_task(void *arg) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_sock, 1);
    ESP_LOGI(TAG, "TCP server started, port %d", TCP_PORT);

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        client_sock = accept(listen_sock, (struct sockaddr*)&client, &len);
        if (client_sock < 0) continue;
        ESP_LOGI(TAG, "Client connected");

        // Отправка JSON-приветствия с указанием бинарного формата
        const char *greeting = "{\"format\":\"binary\",\"sample_rate_hz\":500,\"amplitude_volts\":3.3}\n";
        send(client_sock, greeting, strlen(greeting), 0);

        // Основной цикл отправки бинарных данных
        int64_t next_time = esp_timer_get_time();
        while (client_sock >= 0 && wifi_ok) {
            float voltage = read_voltage();
            if (voltage < 0) break;

            // timestamp в секундах с момента запуска (можно использовать микросекунды / 1e6)
            float timestamp = (float)(esp_timer_get_time() / 1000000.0);

            uint8_t buffer[8];
            memcpy(buffer, &timestamp, 4);
            memcpy(buffer + 4, &voltage, 4);

            if (send(client_sock, buffer, 8, 0) < 0) {
                break;
            }

            // Точная задержка до следующего отсчёта
            next_time += SAMPLE_INTERVAL_US;
            int64_t current = esp_timer_get_time();
            int64_t delay = next_time - current;
            if (delay > 0) {
                usleep(delay);
            } else {
                // Если отстаём, просто пересчитываем следующее время
                next_time = current;
            }
        }
        close(client_sock);
        client_sock = -1;
        ESP_LOGI(TAG, "Client disconnected");
    }
}

void app_main() {
    nvs_flash_init();
    wifi_init();

    // Инициализация ADC
    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&adc_cfg, &adc);
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(adc, ADC_PIN, &chan_cfg);

    xTaskCreate(tcp_task, "tcp", 8192, NULL, 5, NULL);
}
