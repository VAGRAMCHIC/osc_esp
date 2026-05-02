#include "ads1115.h"
#include "config.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "ADS1115";

void ads1115_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLOCK_SPEED,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized");
}

static int16_t ads1115_read_raw(void) {
    uint8_t write_buf[3] = {
        ADS1115_CONFIG_REG,                // регистр конфигурации
        (ADS1115_CONFIG >> 8) & 0xFF,
        ADS1115_CONFIG & 0xFF
    };
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, ADS1115_ADDR, write_buf, 3, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write config failed");
        return 0;
    }

    vTaskDelay(pdMS_TO_TICKS(2)); // ждём преобразования (~1.16ms при 860 SPS)

    uint8_t reg = ADS1115_CONVERSION_REG;
    uint8_t read_buf[2];
    err = i2c_master_write_read_device(I2C_MASTER_NUM, ADS1115_ADDR, &reg, 1, read_buf, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Read conversion failed");
        return 0;
    }
    uint16_t raw = (read_buf[0] << 8) | read_buf[1];
    return (int16_t)raw;
}

float ads1115_read_voltage(void) {
    int16_t raw = ads1115_read_raw();
    return (float)raw * 0.000125f; // 0.125 mV per LSB for ±4.096V range
}
