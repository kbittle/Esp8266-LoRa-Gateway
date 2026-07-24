#ifndef SK9822_H
#define SK9822_H

#include <stdint.h>
#include "driver/gpio.h"

// Pin Definitions
#define LED_DATA_GPIO  GPIO_NUM_2
#define LED_CLK_GPIO   GPIO_NUM_16

void sk9822_init(void);
void sk9822_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

#endif // SK9822_H
