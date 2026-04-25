#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *LOG_TAG = "OSCILLOSCOPE";

// Wi-Fi настройки (замените на свои)
#define WIFI_SSID     "MikroTik-8E25DC"
#define WIFI_PASS     "qwertypoweroverus12"

// TCP сервер
#define TCP_PORT       9999
#define SAMPLE_RATE_HZ 1000
#define ADC_PIN        ADC_CHANNEL_6  // GPIO34
#define AMPLITUDE_MAX  3.3

static int sock = -1;
static bool wifi_connected = false;

// Дескрипторы ADC
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

// Прототипы функций
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_init_sta(void);
static void tcp_server_task(void *pvParameters);
static void adc_sampling_task(void *pvParameters);
static void send_greeting(int client_sock);
static bool adc_calibration_init(void);
static void adc_calibration_deinit(void);
static float read_voltage(void);

// --------------------------------------------------------------
// ADC функции
// --------------------------------------------------------------
static float read_voltage(void)
{
    if (adc_handle == NULL || adc_cali_handle == NULL) return 0.0f;
    
    int adc_raw = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, ADC_PIN, &adc_raw);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return 0.0f;
    }
    
    int voltage_mv = 0;
    ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
        return 0.0f;
    }
    
    return voltage_mv / 1000.0f;
}

static bool adc_calibration_init(void)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(LOG_TAG, "curve fitting calibration scheme supported");
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,        // Исправлено: DB_12 вместо DB_11
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(LOG_TAG, "line fitting calibration scheme supported");
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif

    adc_cali_handle = handle;
    if (calibrated) {
        ESP_LOGI(LOG_TAG, "ADC calibration initialized successfully");
    } else {
        ESP_LOGE(LOG_TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
    }
    return calibrated;
}

static void adc_calibration_deinit(void)
{
    if (adc_cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(adc_cali_handle);
#endif
        ESP_LOGI(LOG_TAG, "ADC calibration deinitialized");
    }
}

// --------------------------------------------------------------
// Wi-Fi события
// --------------------------------------------------------------
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        ESP_LOGI(LOG_TAG, "Wi-Fi disconnected, trying to reconnect...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(LOG_TAG, "Wi-Fi connected, IP address obtained");
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Регистрация обработчика событий Wi-Fi и IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(LOG_TAG, "Wi-Fi initialization started");
}

// --------------------------------------------------------------
// TCP сервер
// --------------------------------------------------------------
static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) {
        ESP_LOGE(LOG_TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(LOG_TAG, "Socket bind failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(listen_sock, 1) < 0) {
        ESP_LOGE(LOG_TAG, "Socket listen failed");
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(LOG_TAG, "TCP server listening on port %d", TCP_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        sock = accept(listen_sock, (struct sockaddr *)&client_addr, &client_len);
        if (sock < 0) {
            ESP_LOGE(LOG_TAG, "Accept failed");
            continue;
        }
        ESP_LOGI(LOG_TAG, "Client connected from %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        send_greeting(sock);
        xTaskCreate(adc_sampling_task, "adc_sampling", 4096, (void*)(intptr_t)sock, 5, NULL);

        // Ожидаем, пока клиент отключится
        while (sock != -1) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        ESP_LOGI(LOG_TAG, "Client disconnected");
    }
}

static void send_greeting(int client_sock)
{
    char greeting[256];
    snprintf(greeting, sizeof(greeting),
             "{\"format\":\"binary\",\"sample_rate_hz\":%d,\"amplitude_volts\":%.1f}\n",
             SAMPLE_RATE_HZ, AMPLITUDE_MAX);
    send(client_sock, greeting, strlen(greeting), 0);
    ESP_LOGI(LOG_TAG, "Greeting sent: %s", greeting);
}

static void adc_sampling_task(void *pvParameters)
{
    int client_sock = (int)(intptr_t)pvParameters;
    const uint8_t BINARY_PACKET_SIZE = 8;
    uint8_t buffer[BINARY_PACKET_SIZE];
    int packet_count = 0;
    const int delay_ms = 10; // 100 Гц (10 мс)

    while (1) {
        if (!wifi_connected) {
            ESP_LOGE(LOG_TAG, "Wi-Fi lost");
            break;
        }
        
        float timestamp = (float)(esp_timer_get_time() / 1000000.0); // секунды с запуска
        float voltage = read_voltage();

        memcpy(buffer, &timestamp, 4);
        memcpy(buffer + 4, &voltage, 4);

        int sent = send(client_sock, buffer, BINARY_PACKET_SIZE, 0);
        packet_count++;
        if (packet_count % 50 == 0) {
            ESP_LOGI(LOG_TAG, "Sent %d packets, last voltage=%.2f", packet_count, voltage);
        }
        if (sent < 0) {
            ESP_LOGE(LOG_TAG, "Send error, closing connection");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    close(client_sock);
    sock = -1;
    vTaskDelete(NULL);
}


// --------------------------------------------------------------
// Главная функция
// --------------------------------------------------------------
void app_main(void)
{
    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация Wi-Fi
    wifi_init_sta();

    // --- Инициализация ADC с новым API ---
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));
    
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,        // Исправлено: DB_12
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_PIN, &channel_config));
    
    if (!adc_calibration_init()) {
        ESP_LOGE(LOG_TAG, "ADC calibration init failed, continuing without calibration");
    }

    // Запуск TCP сервера
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}
