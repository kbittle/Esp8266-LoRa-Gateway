#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi.h"

// Pin Definitions
#define LORA_RST_GPIO  GPIO_NUM_4
#define LORA_NSS_GPIO  GPIO_NUM_5

// SX1278 Registers
#define REG_VERSION    0x42

void lora_init(void);
uint8_t lora_read_reg(uint8_t reg);
void lora_write_reg(uint8_t reg, uint8_t value);

#endif // LORA_H
