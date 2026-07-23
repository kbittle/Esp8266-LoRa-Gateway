#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "gateway_main";

// --- PIN DEFINITIONS ---
// SK9822 RGB LED (Bit-Bang)
#define LED_DATA_GPIO  GPIO_NUM_2
#define LED_CLK_GPIO   GPIO_NUM_16

// Ra-01 LoRa Module (SPI)
#define LORA_RST_GPIO  GPIO_NUM_4
#define LORA_NSS_GPIO  GPIO_NUM_5

// Hardware SPI pins for ESP8266 (HSPI bus)
// GPIO 12 = MISO, GPIO 13 = MOSI, GPIO 14 = SCK

// ============================================================================
// SK9822 LED DRIVER (Bit-Bang)
// ============================================================================

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

// Sets a single SK9822 LED's color and brightness
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

void init_sk9822(void) {
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

void led_status_task(void *pvParameters) {
    init_sk9822();
    ESP_LOGI(TAG, "SK9822 LED driver initialized on GPIO 2 (DATA) & GPIO 16 (CLK)");

    uint8_t step = 0;
    while (1) {
        if (step == 0)      sk9822_set_color(30, 0, 0, 10);   // Red
        else if (step == 1) sk9822_set_color(0, 30, 0, 10);   // Green
        else                sk9822_set_color(0, 0, 30, 10);   // Blue

        step = (step + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ============================================================================
// RA-01 LORA MODULE DRIVER (HSPI)
// ============================================================================

void init_lora_hardware(void) {
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
        .clk_div = SPI_2MHz_DIV, // Correct 2 MHz divider constant
    };
    
    // Init HSPI bus on GPIO 12 (MISO), GPIO 13 (MOSI), GPIO 14 (SCK)
    spi_init(HSPI_HOST, &spi_conf);

    ESP_LOGI(TAG, "Ra-01 LoRa Hardware initialized on HSPI (GPIO 12, 13, 14)");
}

// Read register from SX1278 (Ra-01)
uint8_t lora_read_reg(uint8_t reg) {
    uint8_t reg_addr = reg & 0x7F; // Read flag: bit 7 = 0
    uint8_t rx_data = 0;

    // 1. Send Register Address
    spi_trans_t trans = {0};
    trans.mosi = &reg_addr;
    trans.bits.mosi = 8;

    gpio_set_level(LORA_NSS_GPIO, 0); // Assert Chip Select
    spi_trans(HSPI_HOST, &trans);

    // 2. Read Register Value
    memset(&trans, 0, sizeof(trans));
    trans.miso = &rx_data;
    trans.bits.miso = 8;
    spi_trans(HSPI_HOST, &trans);

    gpio_set_level(LORA_NSS_GPIO, 1); // De-assert Chip Select

    return rx_data;
}

// ============================================================================
// APPLICATION ENTRY POINT
// ============================================================================

void app_main(void) {
    // Initialize Non-Volatile Storage (Required for Wi-Fi stack)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting ESP8266 LoRa Gateway Initialization...");

    // Start LED Task
    xTaskCreate(&led_status_task, "led_task", 1024, NULL, 5, NULL);

    // Initialize LoRa hardware
    init_lora_hardware();

    // Verify Ra-01 communication by reading Version Register (0x42 should return 0x12)
    uint8_t version = lora_read_reg(0x42);
    if (version == 0x12) {
        ESP_LOGI(TAG, "Ra-01 / SX1278 detected successfully! Chip Version: 0x%02X", version);
    } else {
        ESP_LOGE(TAG, "Ra-01 communication error. Read Version: 0x%02X (Expected: 0x12)", version);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}