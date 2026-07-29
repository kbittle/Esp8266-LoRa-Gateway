#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Core combined logging function. Formats string, adds to RAM buffer, and calls ESP_LOG.
 */
void logger_log(esp_log_level_t level, const char *tag, const char *format, ...) __attribute__((format(printf, 3, 4)));

/**
 * @brief Macro wrappers for seamless replacement of ESP_LOGx calls.
 */
#define LOGI(tag, format, ...) logger_log(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define LOGE(tag, format, ...) logger_log(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define LOGW(tag, format, ...) logger_log(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define LOGD(tag, format, ...) logger_log(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define LOGV(tag, format, ...) logger_log(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // LOGGER_H
