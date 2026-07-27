#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Wi-Fi Access Point (SoftAP).
 */
void wifi_init_softap(void);

/**
 * @brief Starts the HTTP web server and registers URI handlers.
 * 
 * @return httpd_handle_t Handle to the web server, or NULL if startup failed.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stops the HTTP web server.
 * 
 * @param server Handle to the running server.
 */
void stop_webserver(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H
