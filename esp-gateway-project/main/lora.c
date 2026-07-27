#include "lora.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// SWSD003 Library Headers
#include "sx126x.h"
#include "sx126x_hal.h"

static const char *TAG = "ra01sh_driver";

static void lora_wait_busy(void) {
    while (gpio_get_level(LORA_BUSY_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// -----------------------------------------------------------------------------
// SWSD003 HAL Implementations
// -----------------------------------------------------------------------------
sx126x_hal_status_t sx126x_hal_write(const void* context, const uint8_t* command, 
                                     const uint16_t command_length, const uint8_t* data, 
                                     const uint16_t data_length) {
    lora_wait_busy();
    gpio_set_level(LORA_NSS_GPIO, 0);

    spi_trans_t trans = {0};
    trans.mosi = (void*)command;
    trans.bits.mosi = command_length * 8;
    spi_trans(HSPI_HOST, &trans);

    if (data_length > 0 && data != NULL) {
        memset(&trans, 0, sizeof(trans));
        trans.mosi = (void*)data;
        trans.bits.mosi = data_length * 8;
        spi_trans(HSPI_HOST, &trans);
    }

    gpio_set_level(LORA_NSS_GPIO, 1);
    lora_wait_busy();
    return SX126X_HAL_STATUS_OK;
}

sx126x_hal_status_t sx126x_hal_read(const void* context, const uint8_t* command, 
                                    const uint16_t command_length, uint8_t* data, 
                                    const uint16_t data_length) {
    lora_wait_busy();
    gpio_set_level(LORA_NSS_GPIO, 0);

    spi_trans_t trans = {0};
    trans.mosi = (void*)command;
    trans.bits.mosi = command_length * 8;
    spi_trans(HSPI_HOST, &trans);

    if (data_length > 0 && data != NULL) {
        memset(&trans, 0, sizeof(trans));
        trans.miso = data;
        trans.bits.miso = data_length * 8;
        spi_trans(HSPI_HOST, &trans);
    }

    gpio_set_level(LORA_NSS_GPIO, 1);
    lora_wait_busy();
    return SX126X_HAL_STATUS_OK;
}

// -----------------------------------------------------------------------------
// High-Level Driver API
// -----------------------------------------------------------------------------
void lora_init(void) {
    // 1. Configure NSS & Reset GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LORA_NSS_GPIO) | (1ULL << LORA_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // 2. Configure BUSY GPIO
    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << LORA_BUSY_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&busy_conf);

    gpio_set_level(LORA_NSS_GPIO, 1);

    // 3. Hardware Reset Sequence
    gpio_set_level(LORA_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LORA_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 4. Initialize Hardware SPI
    spi_config_t spi_conf = {
        .interface = { .cs_en = 0 },
        .clk_div = SPI_2MHz_DIV,
    };
    spi_init(HSPI_HOST, &spi_conf);

    lora_wait_busy();

    // 5. Apply Ra-01SH Specific Configuration via SWSD003
    sx126x_set_reg_mode(NULL, SX126X_REG_MODE_LDO);   // Ra-01SH uses LDO mode
    sx126x_set_dio2_as_rf_sw_ctrl(NULL, true);         // Ra-01SH uses DIO2 for internal RF switch

    ESP_LOGI(TAG, "Ra-01SH (SX1262) hardware & SWSD003 initialized.");
}

bool lora_check_connection(void) {
    sx126x_chip_status_t status;
    
    if (sx126x_get_status(NULL, &status) == SX126X_STATUS_OK) {
        ESP_LOGI(TAG, "Ra-01SH online! Mode: 0x%02X, Cmd Status: 0x%02X", 
                 status.chip_mode, status.cmd_status);
        return true;
    }
    
    ESP_LOGE(TAG, "Communication failure with Ra-01SH module.");
    return false;
}
