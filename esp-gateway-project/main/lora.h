#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi.h"

// Hardware Pin Definitions for Ra-01SH (ESP8266/ESP32)
#define LORA_RST_GPIO   GPIO_NUM_4
#define LORA_NSS_GPIO   GPIO_NUM_5
#define LORA_BUSY_GPIO  GPIO_NUM_16
#define LORA_DIO1_GPIO  GPIO_NUM_15

// High-level driver functions
void lora_init(void);
bool lora_check_connection(void);

#endif // LORA_H
