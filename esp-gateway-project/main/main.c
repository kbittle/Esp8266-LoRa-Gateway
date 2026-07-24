#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Include modular drivers
#include "sk9822.h"
#include "lora.h"

static const char *TAG = "gateway_main";

// FreeRTOS Task: Cycle LED colors
void led_status_task(void *pvParameters) {
    sk9822_init();
    ESP_LOGI(TAG, "SK9822 LED driver initialized.");

    uint8_t step = 0;
    while (1) {
        if (step == 0)      sk9822_set_color(30, 0, 0, 10);   // Red
        else if (step == 1) sk9822_set_color(0, 30, 0, 10);   // Green
        else                sk9822_set_color(0, 0, 30, 10);   // Blue

        step = (step + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    // Initialize Non-Volatile Storage
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting ESP8266 LoRa Gateway...");

    // Start LED background task
    xTaskCreate(&led_status_task, "led_task", 1024, NULL, 5, NULL);

    // Initialize LoRa Hardware
    lora_init();

    // Verify Ra-01 communication
    uint8_t version = lora_read_reg(REG_VERSION);
    if (version == 0x12) {
        ESP_LOGI(TAG, "Ra-01 / SX1278 detected! Chip Version: 0x%02X", version);
    } else {
        ESP_LOGE(TAG, "Ra-01 comm error. Read Version: 0x%02X (Expected: 0x12)", version);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
