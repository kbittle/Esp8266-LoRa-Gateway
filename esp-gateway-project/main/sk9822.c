#include "sk9822.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void sk9822_write_byte(uint8_t b) {
    for (int i = 0; i < 8; i++) {
        // Output MSB first
        gpio_set_level(LED_DATA_GPIO, (b & 0x80) ? 1 : 0);
        
        // Clock High -> Low pulse
        gpio_set_level(LED_CLK_GPIO, 1);
        gpio_set_level(LED_CLK_GPIO, 0);
        
        b <<= 1;
    }
}

void sk9822_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_DATA_GPIO) | (1ULL << LED_CLK_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(LED_DATA_GPIO, 0);
    gpio_set_level(LED_CLK_GPIO, 0);
}

void sk9822_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    if (brightness > 31) brightness = 31;

    // 1. Start Frame: 32 zero bits
    for (int i = 0; i < 4; i++) sk9822_write_byte(0x00);

    // 2. LED Frame: [3 bits '111'] + [5 bits brightness] + [Blue] + [Green] + [Red]
    sk9822_write_byte(0xE0 | brightness);
    sk9822_write_byte(b);
    sk9822_write_byte(g);
    sk9822_write_byte(r);

    // 3. End Frame: 32 one bits
    for (int i = 0; i < 4; i++) sk9822_write_byte(0xFF);
}
