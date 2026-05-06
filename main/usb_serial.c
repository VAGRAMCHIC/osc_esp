#include "usb_serial.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "USB";

// Параметры UART2 (пины можно менять)
#define USB_UART_NUM      UART_NUM_2
#define USB_TX_PIN        17      // TX на ESP32 -> подключается к RX USB-UART
#define USB_RX_PIN        16      // RX на ESP32 <- подключается к TX USB-UART (не используется, но надо задать)
#define USB_BAUD_RATE     115200

void usb_serial_init(void) {
    uart_config_t uart_config = {
        .baud_rate = USB_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(USB_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(USB_UART_NUM, USB_TX_PIN, USB_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(USB_UART_NUM, 1024, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "USB UART initialized on pins TX=%d, RX=%d, baud=%d", USB_TX_PIN, USB_RX_PIN, USB_BAUD_RATE);
}

void usb_serial_send(const uint8_t* data, size_t len) {
    uart_write_bytes(USB_UART_NUM, (const char*)data, len);
} 
