#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Application module drivers
#include "sk9822.h"
#include "lora.h"

static const char *TAG = "gateway_main";

void led_status_task(void *pvParameters) {
    sk9822_init();
    uint8_t step = 0;
    while (1) {
        if (step == 0)      sk9822_set_color(30, 0, 0, 10);
        else if (step == 1) sk9822_set_color(0, 30, 0, 10);
        else                sk9822_set_color(0, 0, 30, 10);

        step = (step + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Starting Gateway...");

    xTaskCreate(&led_status_task, "led_task", 1024, NULL, 5, NULL);

    // Initialize LoRa Hardware & Config
    lora_init();

    // Verify Ra-01SH status via lora driver helper
    if (lora_check_connection()) {
        ESP_LOGI(TAG, "LoRa Gateway initialization complete.");
    } else {
        ESP_LOGE(TAG, "LoRa Gateway initialization failed.");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
