#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi.h"
#include "ads1115.h"
#include "tcp_server.h"

static const char *TAG = "MAIN";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init();
    ads1115_init();

    float test_v = ads1115_read_voltage();
    ESP_LOGI(TAG, "Initial voltage: %.3f V", test_v);

    tcp_server_start();
}
