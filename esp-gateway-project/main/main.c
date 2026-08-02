#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_system.h" // Ensures heap API access

// Application module drivers
#include "sk9822.h"
#include "lora.h"
#include "webserver.h"
#include "logger.h"

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

void network_init_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(500));

    LOGI(TAG, "Starting webserver");

    // Initialize SoftAP & Webserver inside dedicated task context
    wifi_init_apsta();
    start_webserver();

    // Delete task once initialization completes
    vTaskDelete(NULL);
}

void lora_gateway_task(void *pvParameters) {
    LOGI(TAG, "Starting LoRa Gateway (Ra-01SH / SX1262)");

    lora_config_t lora_cfg = LORA_CONFIG_DEFAULT();

    if (lora_init(&lora_cfg) && lora_check_connection()) {
        LOGI(TAG, "LoRa Gateway initialization complete.");
    } else {
        LOGI(TAG, "LoRa Gateway initialization failed.");
    }

    // --- Transmit Packet Example ---
    const char *msg = "Hello World!";
    lora_send_packet((const uint8_t*)msg, strlen(msg), 3000);

    // ALWAYS yield between operations to keep watchdog happy
    vTaskDelay(pdMS_TO_TICKS(100));
    
    LOGI(TAG, "LoRa Gateway RX Mode");

    while (1) {
        // --- Receive Packet Example ---
        uint8_t rx_buf[255] = {0};
        uint8_t rx_len = 0;
        
        if (lora_receive_packet(rx_buf, sizeof(rx_buf), &rx_len, 3000)) {
            rx_buf[rx_len] = '\0';
            LOGI(TAG, "Payload: %s", (char*)rx_buf);
        }

        // Delay between loop iterations
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    LOGI(TAG, "Free heap before network init: %d bytes", (unsigned int)esp_get_free_heap_size());

    xTaskCreate(&network_init_task, "net_init", 2560, NULL, 5, NULL);
    xTaskCreate(&led_status_task, "led_task", 1024, NULL, 4, NULL);
    xTaskCreate(&lora_gateway_task, "lora_task", 3072, NULL, 3, NULL);

    LOGI(TAG, "Free heap after tasks created: %d bytes", (unsigned int)esp_get_free_heap_size());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
