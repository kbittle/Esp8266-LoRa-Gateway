#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* System & Gateway State Structures */
typedef struct {
    int frequency; /* MHz */
    int power;     /* dBm */
    int sf;        /* Spreading factor */
} lora_config_t_fix;

typedef struct {
    char broker[64];
    int port;
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
    uint32_t mqtt_pub_count;
} gateway_stats_t;

/* Global State Accessors (In-RAM) */
extern lora_config_t_fix g_lora_cfg;
extern mqtt_config_t g_mqtt_cfg;
extern wifi_config_state_t g_wifi_cfg;
extern gateway_stats_t g_stats;

/**
 * @brief Append a log entry to the in-RAM log ring buffer.
 */
void app_log_add(const char *msg);

/**
 * @brief Initializes Wi-Fi in AP + Station Mode (APSTA).
 */
void wifi_init_apsta(void);

/**
 * @brief Starts the HTTP web server and registers handlers.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stops the HTTP web server.
 */
void stop_webserver(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
