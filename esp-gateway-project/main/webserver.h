#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "configuration.h"

#ifdef __cplusplus
extern "C" {
#endif

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
