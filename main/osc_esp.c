#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_timer.h"
#include "driver/i2c.h"

#define TAG "OSC"

// Wi-Fi
#define WIFI_SSID     "MikroTik-8E25DC"
#define WIFI_PASS     "qwertypoweroverus12"

// TCP
#define TCP_PORT      9999

// ADS1115
#define ADS1115_ADDR               0x48
#define ADS1115_CONVERSION_REG     0x00
#define ADS1115_CONFIG_REG         0x01

// Параметры оцифровки
#define SAMPLE_RATE_HZ             500
#define SAMPLE_INTERVAL_US         (1000000 / SAMPLE_RATE_HZ)

// Конфигурация ADS1115: AIN0-GND, ±4.096V, single-shot, 860 SPS
// Старший байт: 0xC3 | Младший байт: 0x83
#define ADS1115_CONFIG_SINGLE_SHOT 0xC383

static int client_sock = -1;
static bool wifi_ok = false;

// ---------- I2C инициализация (старый драйвер) ----------
static void i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized (legacy driver)");
}

// ---------- Чтение одного значения с ADS1115 (старый драйвер) ----------
static int16_t ads1115_read_raw(void) {
    uint16_t config = ADS1115_CONFIG_SINGLE_SHOT;
    uint8_t write_buf[3] = {
        ADS1115_CONFIG_REG,
        (config >> 8) & 0xFF,
        config & 0xFF
    };
    // 1. Записать конфигурацию
    esp_err_t err = i2c_master_write_to_device(I2C_NUM_0, ADS1115_ADDR, write_buf, 3, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config");
        return 0;
    }

    // 2. Ожидание преобразования (фиксированная задержка 2мс, 860 SPS -> ~1.16мс)
    vTaskDelay(pdMS_TO_TICKS(2));

    // 3. Чтение результата
    uint8_t reg = ADS1115_CONVERSION_REG;
    uint8_t read_buf[2];
    err = i2c_master_write_read_device(I2C_NUM_0, ADS1115_ADDR, &reg, 1, read_buf, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read conversion");
        return 0;
    }

    uint16_t raw_unsigned = (read_buf[0] << 8) | read_buf[1];
    return (int16_t)raw_unsigned;
}

static float ads1115_read_voltage(void) {
    int16_t raw = ads1115_read_raw();
    return (float)raw * 0.000125f; // Для GAIN=±4.096V: 1 LSB = 0.125 мВ
}

// ---------- Wi-Fi события (без изменений) ----------
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

static void wifi_init(void) {
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

// ---------- TCP сервер (передача бинарных данных, без изменений) ----------
static void tcp_task(void *arg) {
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(NULL);
    }
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_sock, 1);
    ESP_LOGI(TAG, "TCP server started on port %d", TCP_PORT);

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        client_sock = accept(listen_sock, (struct sockaddr*)&client, &len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Accept failed");
            continue;
        }
        ESP_LOGI(TAG, "Client connected");

        // Отправка JSON-приветствия
        char greeting[128];
        snprintf(greeting, sizeof(greeting),
                 "{\"format\":\"binary\",\"sample_rate_hz\":%d,\"amplitude_volts\":4.096}\n",
                 SAMPLE_RATE_HZ);
        send(client_sock, greeting, strlen(greeting), 0);

        int64_t next_time = esp_timer_get_time();
        while (client_sock >= 0 && wifi_ok) {
            float voltage = ads1115_read_voltage();
            float timestamp = (float)(esp_timer_get_time() / 1000000.0);

            uint8_t buffer[8];
            memcpy(buffer, &timestamp, 4);
            memcpy(buffer + 4, &voltage, 4);

            if (send(client_sock, buffer, 8, 0) < 0) {
                break;
            }

            next_time += SAMPLE_INTERVAL_US;
            int64_t current = esp_timer_get_time();
            int64_t delay = next_time - current;
            if (delay > 0) {
                usleep(delay);
            } else {
                next_time = current;
            }
        }
        close(client_sock);
        client_sock = -1;
        ESP_LOGI(TAG, "Client disconnected");
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    i2c_init(); // Теперь используется старый драйвер

    // Тестовое чтение
    float test_voltage = ads1115_read_voltage();
    ESP_LOGI(TAG, "Initial voltage: %.3f V", test_voltage);

    xTaskCreate(tcp_task, "tcp", 8192, NULL, 5, NULL);
}
