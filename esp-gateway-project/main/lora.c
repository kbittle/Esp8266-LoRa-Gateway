#include "lora.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "lora_driver";

void lora_init(void) {
    // Configure NSS and Reset GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LORA_NSS_GPIO) | (1ULL << LORA_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Deselect NSS (Active Low)
    gpio_set_level(LORA_NSS_GPIO, 1);

    // Hardware Reset Ra-01 Module
    gpio_set_level(LORA_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Initialize ESP8266 Hardware SPI (HSPI)
    spi_config_t spi_conf = {
        .interface = {
            .cs_en = 0, // Manual CS control via GPIO 5
        },
        .clk_div = SPI_2MHz_DIV,
    };
    
    // Init HSPI bus on GPIO 12 (MISO), GPIO 13 (MOSI), GPIO 14 (SCK)
    spi_init(HSPI_HOST, &spi_conf);

    ESP_LOGI(TAG, "Ra-01 LoRa Hardware initialized on HSPI (GPIO 12, 13, 14)");
}

uint8_t lora_read_reg(uint8_t reg) {
    uint8_t reg_addr = reg & 0x7F; // Read flag: bit 7 = 0
    uint8_t rx_data = 0;

    // 1. Send Register Address
    spi_trans_t trans = {0};
    trans.mosi = &reg_addr;
    trans.bits.mosi = 8;

    gpio_set_level(LORA_NSS_GPIO, 0); // Assert CS
    spi_trans(HSPI_HOST, &trans);

    // 2. Read Register Value
    memset(&trans, 0, sizeof(trans));
    trans.miso = &rx_data;
    trans.bits.miso = 8;
    spi_trans(HSPI_HOST, &trans);

    gpio_set_level(LORA_NSS_GPIO, 1); // De-assert CS

    return rx_data;
}

void lora_write_reg(uint8_t reg, uint8_t value) {
    uint8_t reg_addr = reg | 0x80; // Write flag: bit 7 = 1
    uint8_t tx_data = value;

    spi_trans_t trans = {0};
    trans.mosi = &reg_addr;
    trans.bits.mosi = 8;

    gpio_set_level(LORA_NSS_GPIO, 0); // Assert CS
    spi_trans(HSPI_HOST, &trans);

    memset(&trans, 0, sizeof(trans));
    trans.mosi = &tx_data;
    trans.bits.mosi = 8;
    spi_trans(HSPI_HOST, &trans);

    gpio_set_level(LORA_NSS_GPIO, 1); // De-assert CS
}
