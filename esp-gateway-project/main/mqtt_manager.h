#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include "configuration.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MQTT client with configuration
 * @return true on success, false on failure
 */
bool mqtt_client_init(void);

/**
 * @brief MQTT client task function (for xTaskCreate in app_main)
 */
void mqtt_task(void *pvParameters);

/**
 * @brief Stop MQTT client
 */
void mqtt_client_stop(void);

/**
 * @brief Check if MQTT client is connected
 * @return true if connected, false otherwise
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Publish LoRa packet to MQTT topic
 * @param payload LoRa packet data
 * @param length payload length
 * @param rssi RSSI value
 * @param snr SNR value
 * @return true on success, false on failure
 */
bool mqtt_client_publish_lora_packet(const uint8_t *payload, uint8_t length, int16_t rssi, int8_t snr);

/**
 * @brief Publish gateway status to MQTT
 * @return true on success, false on failure
 */
bool mqtt_client_publish_status(void);

/**
 * @brief Publish config response to MQTT
 * @param section Config section (lora, mqtt, wifi, led, or "all")
 * @return true on success, false on failure
 */
bool mqtt_client_publish_config(const char *section);

#ifdef __cplusplus
}
#endif

#endif // MQTT_MANAGER_H