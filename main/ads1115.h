#ifndef ADS1115_H
#define ADS1115_H

#include <stdint.h>

void ads1115_init(void);
float ads1115_read_voltage(void);

#endif
