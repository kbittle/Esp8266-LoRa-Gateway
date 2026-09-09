#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lora_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_SOLID,
    LED_MODE_RAINBOW
} led_mode_t;

typedef struct {
    led_mode_t mode;
    uint8_t brightness;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_config_t;

typedef struct {
    char broker[64];
    uint16_t port;
    char client_id[32];
    char topic_prefix[32];
    bool connected;
} mqtt_config_t;

typedef struct {
    char sta_ssid[32];
    char sta_password[64];
    bool sta_connected;
    char ip_addr[16];
} wifi_config_state_t;

typedef struct {
    uint32_t lora_rx_count;
    uint32_t lora_tx_count;
    uint32_t lora_crc_err_count;
    uint32_t lora_tx_timeout_count;
    uint32_t lora_init_fail_count;
    uint32_t mqtt_pub_count;
} gateway_stats_t;

// -----------------------------------------------------------------------------
// Configuration API Declarations
// -----------------------------------------------------------------------------
void config_init(void);

// Thread-safe Getters & Setters
void config_get_led(led_config_t *out_cfg);
void config_set_led(const led_config_t *in_cfg);

void config_get_lora(lora_config_t *out_cfg);
void config_set_lora(const lora_config_t *in_cfg);

void config_get_mqtt(mqtt_config_t *out_cfg);
void config_set_mqtt(const mqtt_config_t *in_cfg);

void config_get_wifi(wifi_config_state_t *out_cfg);
void config_set_wifi(const wifi_config_state_t *in_cfg);

void config_get_stats(gateway_stats_t *out_stats);
void config_inc_rx(void);
void config_inc_tx(void);
void config_inc_crc_err(void);
void config_inc_tx_timeout(void);
void config_inc_init_fail(void);
void config_inc_mqtt_pub(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIGURATION_H