#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Application module drivers
#include "sk9822.h"
#include "lora.h"
#include "webserver.h"

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

    // Start Wi-Fi AP & Webserver
    wifi_init_softap();
    start_webserver();

    xTaskCreate(&led_status_task, "led_task", 1024, NULL, 5, NULL);

    ESP_LOGI(TAG, "Starting Gateway (Ra-01SH / SX1262)...");

    // Initialize LoRa with default struct parameters: 
    // 915 MHz, SF12, BW 125 kHz, CR 4/5, +14 dBm power
    lora_config_t lora_cfg = LORA_CONFIG_DEFAULT();

    // Custom modifications can be done directly on the struct:
    // lora_cfg.power_dbm = 22; // Boost power to +22 dBm

    if (lora_init(&lora_cfg) && lora_check_connection()) {
        ESP_LOGI(TAG, "LoRa Gateway initialization complete.");
    } else {
        ESP_LOGE(TAG, "LoRa Gateway initialization failed.");
    }

    // --- Transmit Packet Example ---
    const char *msg = "Hello World!";
    lora_send_packet((const uint8_t*)msg, strlen(msg), 3000);

    // --- Receive Packet Example ---
    uint8_t rx_buf[255] = {0};
    uint8_t rx_len = 0;
    
    // Listen for 5 seconds
    if (lora_receive_packet(rx_buf, sizeof(rx_buf), &rx_len, 5000)) {
        rx_buf[rx_len] = '\0'; // Null-terminate if text string
        ESP_LOGI(TAG, "Payload: %s", (char*)rx_buf);
    } else {
        ESP_LOGI(TAG, "No packet received within 5s window.");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
