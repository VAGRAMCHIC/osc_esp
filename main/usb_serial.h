#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stdint.h>
#include <stddef.h>

// Инициализация UART для USB (CP2102/CH340)
void usb_serial_init(void);

// Отправить бинарный буфер
void usb_serial_send(const uint8_t* data, size_t len);

#endif
