#include "webserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "logger.h"
#include "configuration.h"

#ifndef HTTPD_RESP_USE_STRLEN
#define HTTPD_RESP_USE_STRLEN -1
#endif

static const char *TAG = "webserver";

/* Standard ESP-IDF embedded file symbols */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/* In-RAM Log Buffer */
#define LOG_MAX_ENTRIES 50
#define LOG_ENTRY_LEN 128
static char g_logs[LOG_MAX_ENTRIES][LOG_ENTRY_LEN] = {0};
static int g_log_head = 0;

void app_log_add(const char *msg) {
    if (!msg) return;
    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    snprintf(g_logs[g_log_head], LOG_ENTRY_LEN, "[%lu] %s", (unsigned long)uptime_sec, msg);
    g_log_head = (g_log_head + 1) % LOG_MAX_ENTRIES;
}

/* Helper to replace placeholders dynamically in embedded HTML */
static void send_template_chunk(httpd_req_t *req, const char **cursor, const char *end, const char *token, const char *value) {
    const char *pos = strstr(*cursor, token);
    if (pos && pos < end) {
        if (pos > *cursor) {
            httpd_resp_send_chunk(req, *cursor, pos - *cursor);
        }
        httpd_resp_send_chunk(req, value, HTTPD_RESP_USE_STRLEN);
        *cursor = pos + strlen(token);
    }
}

/* -------------------------------------------------------------------------- */
/*                               GET Handler                                 */
/* -------------------------------------------------------------------------- */

static esp_err_t root_get_handler(httpd_req_t *req) {
    led_config_t led_cfg;
    lora_config_t lora_cfg;
    mqtt_config_t mqtt_cfg;
    wifi_config_state_t wifi_cfg;
    gateway_stats_t stats;

    config_get_led(&led_cfg);
    config_get_lora(&lora_cfg);
    config_get_mqtt(&mqtt_cfg);
    config_get_wifi(&wifi_cfg);
    config_get_stats(&stats);

    httpd_resp_set_type(req, "text/html");

    const char *cursor = (const char *)index_html_start;
    const char *end = (const char *)index_html_end;
    char val_buf[512];

    // Uptime
    snprintf(val_buf, sizeof(val_buf), "%lld", (long long)(esp_timer_get_time() / 1000000));
    send_template_chunk(req, &cursor, end, "%UPTIME%", val_buf);

    // Free Heap
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)(esp_get_free_heap_size() / 1024));
    send_template_chunk(req, &cursor, end, "%HEAP%", val_buf);

    // LED Mode Dropdown Options
    snprintf(val_buf, sizeof(val_buf),
             "<option value=\"0\" %s>Off</option>"
             "<option value=\"1\" %s>Solid</option>"
             "<option value=\"2\" %s>Rainbow</option>",
             (led_cfg.mode == LED_MODE_OFF) ? "selected" : "",
             (led_cfg.mode == LED_MODE_SOLID) ? "selected" : "",
             (led_cfg.mode == LED_MODE_RAINBOW) ? "selected" : "");
    send_template_chunk(req, &cursor, end, "%LED_MODE_OPTIONS%", val_buf);

    // Convert LED RGB back to HTML Hex Color string
    snprintf(val_buf, sizeof(val_buf), "#%02X%02X%02X", led_cfg.r, led_cfg.g, led_cfg.b);
    send_template_chunk(req, &cursor, end, "%LED_HEX%", val_buf);

    snprintf(val_buf, sizeof(val_buf), "%u", led_cfg.brightness);
    send_template_chunk(req, &cursor, end, "%BRIGHTNESS%", val_buf);

    // Wifi Mode
    send_template_chunk(req, &cursor, end, "%WIFI_MODE%", wifi_cfg.sta_connected ? "Station Connected" : "SoftAP Only");

    // MQTT Status
    send_template_chunk(req, &cursor, end, "%MQTT_STATUS%", mqtt_cfg.connected ? "Connected" : "Disconnected");

    // LoRa Rx/Tx
    snprintf(val_buf, sizeof(val_buf), "%u / %u", (unsigned int)stats.lora_rx_count, (unsigned int)stats.lora_tx_count);
    send_template_chunk(req, &cursor, end, "%LORA_RX_TX%", val_buf);

    // LoRa CRC
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_crc_err_count);
    send_template_chunk(req, &cursor, end, "%LORA_CRC%", val_buf);

    // LoRa Timeout
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_tx_timeout_count);
    send_template_chunk(req, &cursor, end, "%LORA_TIMEOUT%", val_buf);

    // LoRa Init Fail
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_init_fail_count);
    send_template_chunk(req, &cursor, end, "%LORA_INIT_FAIL%", val_buf);

    // MQTT Published
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.mqtt_pub_count);
    send_template_chunk(req, &cursor, end, "%MQTT_PUB%", val_buf);

    // LoRa Freq
    snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)(lora_cfg.frequency_hz / 1000000));
    send_template_chunk(req, &cursor, end, "%FREQ%", val_buf);

    // LoRa Power
    snprintf(val_buf, sizeof(val_buf), "%d", lora_cfg.power_dbm);
    send_template_chunk(req, &cursor, end, "%POWER%", val_buf);

    // SF Options
    char sf_buf[512] = {0};
    for (int sf = 7; sf <= 12; sf++) {
        char opt[64];
        snprintf(opt, sizeof(opt), "<option value=\"%d\" %s>SF%d</option>",
                 sf, (lora_cfg.sf == sf) ? "selected" : "", sf);
        strcat(sf_buf, opt);
    }
    send_template_chunk(req, &cursor, end, "%SF_OPTIONS%", sf_buf);

    // MQTT Broker, Port, Client ID, Prefix
    send_template_chunk(req, &cursor, end, "%BROKER%", mqtt_cfg.broker[0] ? mqtt_cfg.broker : "");
    snprintf(val_buf, sizeof(val_buf), "%d", mqtt_cfg.port);
    send_template_chunk(req, &cursor, end, "%PORT%", val_buf);
    send_template_chunk(req, &cursor, end, "%CLIENT_ID%", mqtt_cfg.client_id[0] ? mqtt_cfg.client_id : "");
    send_template_chunk(req, &cursor, end, "%TOPIC_PREFIX%", mqtt_cfg.topic_prefix[0] ? mqtt_cfg.topic_prefix : "");

    // Wi-Fi SSID & Password
    send_template_chunk(req, &cursor, end, "%SSID%", wifi_cfg.sta_ssid[0] ? wifi_cfg.sta_ssid : "");
    send_template_chunk(req, &cursor, end, "%PASSWORD%", wifi_cfg.sta_password[0] ? wifi_cfg.sta_password : "");

    // Logs
    char log_buf[LOG_MAX_ENTRIES * LOG_ENTRY_LEN] = {0};
    bool empty = true;
    int idx = g_log_head;
    for (int i = 0; i < LOG_MAX_ENTRIES; i++) {
        if (g_logs[idx][0] != '\0') {
            strcat(log_buf, g_logs[idx]);
            strcat(log_buf, "\n");
            empty = false;
        }
        idx = (idx + 1) % LOG_MAX_ENTRIES;
    }
    send_template_chunk(req, &cursor, end, "%LOGS%", empty ? "System initialized." : log_buf);

    // Flush remaining HTML tail
    if (cursor < end) {
        httpd_resp_send_chunk(req, cursor, end - cursor);
    }

    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*                               Form Parsers                                 */
/* -------------------------------------------------------------------------- */

static void parse_form_str(const char *body, const char *key, char *out, size_t max_len) {
    if (!body || !key || !out) return;
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *pos = strstr(body, needle);
    if (pos) {
        pos += strlen(needle);
        size_t i = 0;
        while (*pos != '\0' && *pos != '&' && i < max_len - 1) {
            out[i++] = *pos++;
        }
        out[i] = '\0';
    }
}

static void parse_form_hex_color(const char *body, uint8_t *r, uint8_t *g, uint8_t *b) {
    char color_str[16] = {0};
    parse_form_str(body, "color", color_str, sizeof(color_str));
    
    // Hex input comes URL-encoded as %23RRGGBB or #RRGGBB
    const char *hex_ptr = NULL;
    if (strncmp(color_str, "%23", 3) == 0) hex_ptr = color_str + 3;
    else if (color_str[0] == '#') hex_ptr = color_str + 1;
    else hex_ptr = color_str;

    if (strlen(hex_ptr) >= 6) {
        unsigned int red = 0, green = 0, blue = 0;
        if (sscanf(hex_ptr, "%02x%02x%02x", &red, &green, &blue) == 3) {
            *r = (uint8_t)red;
            *g = (uint8_t)green;
            *b = (uint8_t)blue;
        }
    }
}

static esp_err_t save_led_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    led_config_t cfg;
    config_get_led(&cfg);

    char val[16] = {0};
    parse_form_str(buf, "mode", val, sizeof(val));
    if (val[0] != '\0') {
        if (strcmp(val, "0") == 0 || strcmp(val, "off") == 0) {
            cfg.mode = LED_MODE_OFF;
        } else if (strcmp(val, "1") == 0 || strcmp(val, "solid") == 0) {
            cfg.mode = LED_MODE_SOLID;
        } else if (strcmp(val, "2") == 0 || strcmp(val, "rainbow") == 0) {
            cfg.mode = LED_MODE_RAINBOW;
        }
    }

    parse_form_hex_color(buf, &cfg.r, &cfg.g, &cfg.b);

    parse_form_str(buf, "brightness", val, sizeof(val));
    if (val[0] != '\0') cfg.brightness = (uint8_t)atoi(val);

    config_set_led(&cfg);

    LOGI(TAG, "LED settings updated via Webserver (mode: %d, RGB: %d,%d,%d)", cfg.mode, cfg.r, cfg.g, cfg.b);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_lora_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    lora_config_t cfg;
    config_get_lora(&cfg);

    int mhz = 0;
    int power = 0;
    int sf = 0;

    if (strstr(buf, "frequency=")) sscanf(strstr(buf, "frequency="), "frequency=%d", &mhz);
    if (strstr(buf, "power="))     sscanf(strstr(buf, "power="), "power=%d", &power);
    if (strstr(buf, "sf="))        sscanf(strstr(buf, "sf="), "sf=%d", &sf);

    if (mhz > 0) cfg.frequency_hz = (uint32_t)mhz * 1000000;
    if (power != 0) cfg.power_dbm = (int8_t)power;
    if (sf >= 6 && sf <= 12) cfg.sf = (lora_sf_t)sf;

    config_set_lora(&cfg);

    LOGI(TAG, "LoRa settings updated");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_mqtt_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    mqtt_config_t cfg;
    config_get_mqtt(&cfg);

    parse_form_str(buf, "broker", cfg.broker, sizeof(cfg.broker));
    parse_form_str(buf, "client_id", cfg.client_id, sizeof(cfg.client_id));
    parse_form_str(buf, "topic_prefix", cfg.topic_prefix, sizeof(cfg.topic_prefix));

    char port_str[8] = {0};
    parse_form_str(buf, "port", port_str, sizeof(port_str));
    if (port_str[0] != '\0') cfg.port = (uint16_t)atoi(port_str);

    config_set_mqtt(&cfg);

    LOGI(TAG, "MQTT settings updated");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    wifi_config_state_t cfg;
    config_get_wifi(&cfg);

    parse_form_str(buf, "ssid", cfg.sta_ssid, sizeof(cfg.sta_ssid));
    parse_form_str(buf, "password", cfg.sta_password, sizeof(cfg.sta_password));

    config_set_wifi(&cfg);

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, cfg.sta_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, cfg.sta_password, sizeof(sta_config.sta.password));

    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();

    LOGI(TAG, "Wi-Fi station connecting...");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*                               Initialization                               */
/* -------------------------------------------------------------------------- */

void wifi_init_apsta(void) {
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP8266-Gateway-Config",
            .ssid_len = strlen("ESP8266-Gateway-Config"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    LOGI(TAG, "Wi-Fi APSTA initialized.");
}

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t led_uri = { .uri = "/save_led", .method = HTTP_POST, .handler = save_led_handler };
        httpd_register_uri_handler(server, &led_uri);

        httpd_uri_t lora_uri = { .uri = "/save_lora", .method = HTTP_POST, .handler = save_lora_handler };
        httpd_register_uri_handler(server, &lora_uri);

        httpd_uri_t mqtt_uri = { .uri = "/save_mqtt", .method = HTTP_POST, .handler = save_mqtt_handler };
        httpd_register_uri_handler(server, &mqtt_uri);

        httpd_uri_t wifi_uri = { .uri = "/save_wifi", .method = HTTP_POST, .handler = save_wifi_handler };
        httpd_register_uri_handler(server, &wifi_uri);

        LOGI(TAG, "Webserver started.");
        return server;
    }
    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
    }
}