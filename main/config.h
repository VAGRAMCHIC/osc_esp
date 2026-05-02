#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi
#define WIFI_SSID     "MikroTik-8E25DC"
#define WIFI_PASS     "qwertypoweroverus12"

// TCP
#define TCP_PORT      9999

// ADS1115 I2C
#define ADS1115_ADDR            0x48
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define I2C_CLOCK_SPEED         100000

// ADS1115 регистры
#define ADS1115_CONVERSION_REG  0x00
#define ADS1115_CONFIG_REG      0x01

// Параметры оцифровки
#define SAMPLE_RATE_HZ          500
#define SAMPLE_INTERVAL_US      (1000000 / SAMPLE_RATE_HZ)

// Конфигурация ADS1115: single-shot, AIN0-GND, ±4.096V, 860 SPS
#define ADS1115_CONFIG          0xC383

#endif
