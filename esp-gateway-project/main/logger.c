#include "logger.h"
#include "webserver.h"
#include <stdarg.h>
#include <stdio.h>

void logger_log(esp_log_level_t level, const char *tag, const char *format, ...) {
    char buf[128];

    // 1. Format the variadic arguments into a temporary buffer
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // 2. Forward formatted string to webserver log ring buffer
    app_log_add(buf);

    // 3. Forward to ESP standard logger based on log level
    switch (level) {
        case ESP_LOG_ERROR:
            ESP_LOGE(tag, "%s", buf);
            break;
        case ESP_LOG_WARN:
            ESP_LOGW(tag, "%s", buf);
            break;
        case ESP_LOG_INFO:
            ESP_LOGI(tag, "%s", buf);
            break;
        case ESP_LOG_DEBUG:
            ESP_LOGD(tag, "%s", buf);
            break;
        case ESP_LOG_VERBOSE:
            ESP_LOGV(tag, "%s", buf);
            break;
        default:
            ESP_LOGI(tag, "%s", buf);
            break;
    }
}
