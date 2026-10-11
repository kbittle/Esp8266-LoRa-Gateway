#include "webserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "tcpip_adapter.h"
#include "logger.h"
#include "configuration.h"

#ifndef HTTPD_RESP_USE_STRLEN
#define HTTPD_RESP_USE_STRLEN -1
#endif

static const char *TAG = "webserver";

/* WiFi Event Handler */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                LOGI(TAG, "Station started, connecting...");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
                LOGI(TAG, "Station connected to AP: %s", event->ssid);
                break;
            }
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
                LOGI(TAG, "Station disconnected from AP, reason: %d, retrying...", event->reason);
                wifi_config_state_t cfg;
                config_get_wifi(&cfg);
                cfg.sta_connected = false;
                snprintf(cfg.ip_addr, sizeof(cfg.ip_addr), "0.0.0.0");
                config_set_wifi(&cfg);
                esp_wifi_connect();
                break;
            }
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                LOGI(TAG, "Station connected to SoftAP, AID: %d", event->aid);
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                LOGI(TAG, "Station disconnected from SoftAP, AID: %d", event->aid);
                break;
            }
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            wifi_config_state_t cfg;
            config_get_wifi(&cfg);
            cfg.sta_connected = true;
            snprintf(cfg.ip_addr, sizeof(cfg.ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
            config_set_wifi(&cfg);
            app_log_add("WiFi connected, got IP");
        }
    }
}

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

/* Helper to replace placeholders dynamically in embedded HTML using RAM buffering */
static void send_template_chunk(httpd_req_t *req, const char **cursor, const char *end, const char *token, const char *value) {
    const char *pos = strstr(*cursor, token);
    if (pos && pos < end) {
        size_t len = pos - *cursor;
        
        // Safely send the chunk prior to the token using a RAM buffer
        while (len > 0) {
            char chunk_buf[64];
            size_t to_send = (len > sizeof(chunk_buf)) ? sizeof(chunk_buf) : len;
            memcpy(chunk_buf, *cursor, to_send);
            
            httpd_resp_send_chunk(req, chunk_buf, to_send);
            *cursor += to_send;
            len -= to_send;
        }

        // Send the replacement dynamic string value
        if (value && strlen(value) > 0) {
            httpd_resp_send_chunk(req, value, HTTPD_RESP_USE_STRLEN);
        }
        *cursor = pos + strlen(token);
    }
}

/* Helper to send HTTP 303 Redirect cleanly */
static esp_err_t send_redirect(httpd_req_t *req, const char *location) {
    const char *resp = "Redirecting...";
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, resp, strlen(resp));
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
    char val_buf[256];

    // --- System Status Card (Exact HTML Order) ---
    // 1. %GATEWAY_ID% (in title)
    send_template_chunk(req, &cursor, end, "%GATEWAY_ID%", mqtt_cfg.gateway_id[0] ? mqtt_cfg.gateway_id : "01");

    // 2. %UPTIME%
    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)uptime_sec);
    send_template_chunk(req, &cursor, end, "%UPTIME%", val_buf);

    // 3. %HEAP%
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)(esp_get_free_heap_size() / 1024));
    send_template_chunk(req, &cursor, end, "%HEAP%", val_buf);

    // 4. %WIFI_STATUS_BOX%
    if (wifi_cfg.sta_connected) {
        send_template_chunk(req, &cursor, end, "%WIFI_STATUS_BOX%",
            "<span style=\"color:#00ff00;\">Connected</span>");
    } else {
        send_template_chunk(req, &cursor, end, "%WIFI_STATUS_BOX%",
            "<span style=\"color:#ff4444;\">Disconnected</span>");
    }

    // 5. %MQTT_STATUS_BOX%
    if (mqtt_cfg.connected) {
        send_template_chunk(req, &cursor, end, "%MQTT_STATUS_BOX%",
            "<span style=\"color:#00ff00;\">Connected</span>");
    } else {
        send_template_chunk(req, &cursor, end, "%MQTT_STATUS_BOX%",
            "<span style=\"color:#ff4444;\">Disconnected</span>");
    }

    // 6. %MQTT_PUB%
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.mqtt_pub_count);
    send_template_chunk(req, &cursor, end, "%MQTT_PUB%", val_buf);

    // --- RGB LED Control Card ---
    // 7. %LED_MODE_OPTIONS%
    snprintf(val_buf, sizeof(val_buf),
             "<option value=\"0\" %s>Off</option>"
             "<option value=\"1\" %s>Solid</option>"
             "<option value=\"2\" %s>Rainbow</option>",
             (led_cfg.mode == LED_MODE_OFF) ? "selected" : "",
             (led_cfg.mode == LED_MODE_SOLID) ? "selected" : "",
             (led_cfg.mode == LED_MODE_RAINBOW) ? "selected" : "");
    send_template_chunk(req, &cursor, end, "%LED_MODE_OPTIONS%", val_buf);

    // 8. %LED_HEX%
    snprintf(val_buf, sizeof(val_buf), "#%02X%02X%02X", led_cfg.r, led_cfg.g, led_cfg.b);
    send_template_chunk(req, &cursor, end, "%LED_HEX%", val_buf);

    // 9. %BRIGHTNESS%
    snprintf(val_buf, sizeof(val_buf), "%u", led_cfg.brightness);
    send_template_chunk(req, &cursor, end, "%BRIGHTNESS%", val_buf);

    // --- LoRa Diagnostics Tab ---
    // 10. %FREQ%
    snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)(lora_cfg.frequency_hz / 1000000));
    send_template_chunk(req, &cursor, end, "%FREQ%", val_buf);

    // 11. %POWER%
    snprintf(val_buf, sizeof(val_buf), "%d", lora_cfg.power_dbm);
    send_template_chunk(req, &cursor, end, "%POWER%", val_buf);

    // 12. %LORA_RX_TX%
    snprintf(val_buf, sizeof(val_buf), "%u / %u", (unsigned int)stats.lora_rx_count, (unsigned int)stats.lora_tx_count);
    send_template_chunk(req, &cursor, end, "%LORA_RX_TX%", val_buf);

    // 13. %LORA_CRC%
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_crc_err_count);
    send_template_chunk(req, &cursor, end, "%LORA_CRC%", val_buf);

    // 14. %LORA_TIMEOUT%
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_tx_timeout_count);
    send_template_chunk(req, &cursor, end, "%LORA_TIMEOUT%", val_buf);

    // 15. %LORA_INIT_FAIL%
    snprintf(val_buf, sizeof(val_buf), "%u", (unsigned int)stats.lora_init_fail_count);
    send_template_chunk(req, &cursor, end, "%LORA_INIT_FAIL%", val_buf);

    // --- LoRa Config Tab ---
    // 16. %FREQ% (Second occurrence in form)
    snprintf(val_buf, sizeof(val_buf), "%lu", (unsigned long)(lora_cfg.frequency_hz / 1000000));
    send_template_chunk(req, &cursor, end, "%FREQ%", val_buf);

    // 17. %POWER% (Second occurrence in form)
    snprintf(val_buf, sizeof(val_buf), "%d", lora_cfg.power_dbm);
    send_template_chunk(req, &cursor, end, "%POWER%", val_buf);

    // 18. %SF_OPTIONS%
    char *sf_buf = malloc(512);
    if (sf_buf) {
        sf_buf[0] = '\0';
        for (int sf = 7; sf <= 12; sf++) {
            char opt[64];
            snprintf(opt, sizeof(opt), "<option value=\"%d\" %s>SF%d</option>",
                     sf, (lora_cfg.sf == sf) ? "selected" : "", sf);
            strcat(sf_buf, opt);
        }
        send_template_chunk(req, &cursor, end, "%SF_OPTIONS%", sf_buf);
        free(sf_buf);
    }

    // 19. %BW_OPTIONS%
    char *bw_buf = malloc(512);
    if (bw_buf) {
        bw_buf[0] = '\0';
        struct { int val; const char *label; } bw_list[] = {
            {0, "7.8 kHz"}, {1, "10.4 kHz"}, {2, "15.6 kHz"}, {3, "20.8 kHz"},
            {4, "31.25 kHz"}, {5, "41.7 kHz"}, {6, "62.5 kHz"},
            {7, "125 kHz"}, {8, "250 kHz"}, {9, "500 kHz"}
        };
        for (int i = 0; i < 10; i++) {
            char opt[64];
            snprintf(opt, sizeof(opt), "<option value=\"%d\" %s>%s</option>",
                     bw_list[i].val, (lora_cfg.bw == bw_list[i].val) ? "selected" : "", bw_list[i].label);
            strcat(bw_buf, opt);
        }
        send_template_chunk(req, &cursor, end, "%BW_OPTIONS%", bw_buf);
        free(bw_buf);
    }

    // 20. %CR_OPTIONS%
    char *cr_buf = malloc(512);
    if (cr_buf) {
        cr_buf[0] = '\0';
        struct { int val; const char *label; } cr_list[] = {
            {1, "4/5"}, {2, "4/6"}, {3, "4/7"}, {4, "4/8"}
        };
        for (int i = 0; i < 4; i++) {
            char opt[64];
            snprintf(opt, sizeof(opt), "<option value=\"%d\" %s>%s</option>",
                     cr_list[i].val, (lora_cfg.cr == cr_list[i].val) ? "selected" : "", cr_list[i].label);
            strcat(cr_buf, opt);
        }
        send_template_chunk(req, &cursor, end, "%CR_OPTIONS%", cr_buf);
        free(cr_buf);
    }

    // 21. %PREAMBLE%
    snprintf(val_buf, sizeof(val_buf), "%u", lora_cfg.preamble_length);
    send_template_chunk(req, &cursor, end, "%PREAMBLE%", val_buf);

    // 22. %CRC_OPTIONS%
    char *crc_buf = malloc(256);
    if (crc_buf) {
        snprintf(crc_buf, 256,
                 "<option value=\"1\" %s>Enabled</option>"
                 "<option value=\"0\" %s>Disabled</option>",
                 (lora_cfg.crc_on) ? "selected" : "",
                 (!lora_cfg.crc_on) ? "selected" : "");
        send_template_chunk(req, &cursor, end, "%CRC_OPTIONS%", crc_buf);
        free(crc_buf);
    }

    // --- MQTT Config Tab ---
    // 19. %BROKER%
    send_template_chunk(req, &cursor, end, "%BROKER%", mqtt_cfg.broker[0] ? mqtt_cfg.broker : "");

    // 20. %PORT%
    snprintf(val_buf, sizeof(val_buf), "%d", mqtt_cfg.port);
    send_template_chunk(req, &cursor, end, "%PORT%", val_buf);

    // 21. %CLIENT_ID%
    send_template_chunk(req, &cursor, end, "%CLIENT_ID%", mqtt_cfg.client_id[0] ? mqtt_cfg.client_id : "");

    // 22. %GATEWAY_ID% (Second occurrence in form)
    send_template_chunk(req, &cursor, end, "%GATEWAY_ID%", mqtt_cfg.gateway_id[0] ? mqtt_cfg.gateway_id : "01");

    // 23. %USERNAME%
    send_template_chunk(req, &cursor, end, "%USERNAME%", mqtt_cfg.username[0] ? mqtt_cfg.username : "");

    // 24. %PASSWORD%
    send_template_chunk(req, &cursor, end, "%PASSWORD%", mqtt_cfg.password[0] ? mqtt_cfg.password : "");

    // 25. %TOPIC_PREFIX%
    send_template_chunk(req, &cursor, end, "%TOPIC_PREFIX%", mqtt_cfg.topic_prefix[0] ? mqtt_cfg.topic_prefix : "");

    // 26. %MQTT_BUTTON%
    if (mqtt_cfg.connected) {
        send_template_chunk(req, &cursor, end, "%MQTT_BUTTON%",
            "<button type=\"submit\" class=\"submit-btn\" style=\"background:#e74c3c;\" formaction=\"/disconnect_mqtt\">Disconnect MQTT</button>");
    } else {
        send_template_chunk(req, &cursor, end, "%MQTT_BUTTON%",
            "<button type=\"submit\" class=\"submit-btn\">Save MQTT Settings</button>");
    }

    // --- Wi-Fi Config Tab ---
    // 27. %SSID%
    send_template_chunk(req, &cursor, end, "%SSID%", wifi_cfg.sta_ssid[0] ? wifi_cfg.sta_ssid : "");

    // 28. %PASSWORD%
    send_template_chunk(req, &cursor, end, "%PASSWORD%", wifi_cfg.sta_password[0] ? wifi_cfg.sta_password : "");

    // 29. %WIFI_BUTTON%
    if (wifi_cfg.sta_connected) {
        send_template_chunk(req, &cursor, end, "%WIFI_BUTTON%",
            "<button type=\"submit\" class=\"submit-btn\" style=\"background:#e74c3c;\" formaction=\"/disconnect_wifi\">Disconnect Wi-Fi</button>");
    } else {
        send_template_chunk(req, &cursor, end, "%WIFI_BUTTON%",
            "<button type=\"submit\" class=\"submit-btn\">Connect Wi-Fi</button>");
    }

    // --- System Logs ---
    // 30. %LOGS%
    size_t log_alloc_size = LOG_MAX_ENTRIES * LOG_ENTRY_LEN;
    char *log_buf = malloc(log_alloc_size);
    if (log_buf) {
        log_buf[0] = '\0';
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
        free(log_buf);
    } else {
        send_template_chunk(req, &cursor, end, "%LOGS%", "System initialized.");
    }

    // Flush remaining HTML tail
    while (cursor < end) {
        char chunk_buf[64];
        size_t len = end - cursor;
        size_t to_send = (len > sizeof(chunk_buf)) ? sizeof(chunk_buf) : len;
        memcpy(chunk_buf, cursor, to_send);
        httpd_resp_send_chunk(req, chunk_buf, to_send);
        cursor += to_send;
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*                               Form Parsers                                 */
/* -------------------------------------------------------------------------- */

static void url_decode(const char *src, char *dst, size_t max_len) {
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < max_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], '\0'};
            unsigned int val;
            if (sscanf(hex, "%2x", &val) == 1) {
                dst[j++] = (char)val;
                i += 3;
            } else {
                dst[j++] = src[i++];
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

static void parse_form_str(const char *body, const char *key, char *out, size_t max_len) {
    if (!body || !key || !out) return;
    char needle[32];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *pos = strstr(body, needle);
    if (pos) {
        pos += strlen(needle);
        size_t i = 0;
        char encoded[128] = {0};
        while (*pos != '\0' && *pos != '&' && i < sizeof(encoded) - 1) {
            encoded[i++] = *pos++;
        }
        encoded[i] = '\0';
        url_decode(encoded, out, max_len);
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

    return send_redirect(req, "/");
}

static esp_err_t save_lora_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    lora_config_t cfg;
    config_get_lora(&cfg);

    int mhz = 0;
    int power = 0;
    int sf = 0;
    int bw = 0;
    int cr = 0;
    int preamble = 0;
    int crc = 0;

    if (strstr(buf, "frequency=")) sscanf(strstr(buf, "frequency="), "frequency=%d", &mhz);
    if (strstr(buf, "power="))     sscanf(strstr(buf, "power="), "power=%d", &power);
    if (strstr(buf, "sf="))        sscanf(strstr(buf, "sf="), "sf=%d", &sf);
    if (strstr(buf, "bw="))        sscanf(strstr(buf, "bw="), "bw=%d", &bw);
    if (strstr(buf, "cr="))        sscanf(strstr(buf, "cr="), "cr=%d", &cr);
    if (strstr(buf, "preamble="))  sscanf(strstr(buf, "preamble="), "preamble=%d", &preamble);
    if (strstr(buf, "crc="))       sscanf(strstr(buf, "crc="), "crc=%d", &crc);

    if (mhz > 0) cfg.frequency_hz = (uint32_t)mhz * 1000000;
    if (power != 0) cfg.power_dbm = (int8_t)power;
    if (sf >= 6 && sf <= 12) cfg.sf = (lora_sf_t)sf;
    if (bw >= 0 && bw <= 9) cfg.bw = (lora_bw_t)bw;
    if (cr >= 1 && cr <= 4) cfg.cr = (lora_cr_t)cr;
    if (preamble > 0) cfg.preamble_length = (uint16_t)preamble;
    cfg.crc_on = (crc == 1);

    config_set_lora(&cfg);

    LOGI(TAG, "LoRa settings updated");

    return send_redirect(req, "/");
}

static esp_err_t save_mqtt_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    mqtt_config_t cfg;
    config_get_mqtt(&cfg);

    parse_form_str(buf, "broker", cfg.broker, sizeof(cfg.broker));
    parse_form_str(buf, "client_id", cfg.client_id, sizeof(cfg.client_id));
    parse_form_str(buf, "gateway_id", cfg.gateway_id, sizeof(cfg.gateway_id));
    parse_form_str(buf, "username", cfg.username, sizeof(cfg.username));
    parse_form_str(buf, "password", cfg.password, sizeof(cfg.password));
    parse_form_str(buf, "topic_prefix", cfg.topic_prefix, sizeof(cfg.topic_prefix));

    char port_str[8] = {0};
    parse_form_str(buf, "port", port_str, sizeof(port_str));
    if (port_str[0] != '\0') cfg.port = (uint16_t)atoi(port_str);

    config_set_mqtt(&cfg);

    LOGI(TAG, "MQTT settings updated");

    return send_redirect(req, "/");
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    wifi_config_state_t cfg;
    config_get_wifi(&cfg);

    parse_form_str(buf, "ssid", cfg.sta_ssid, sizeof(cfg.sta_ssid));
    parse_form_str(buf, "password", cfg.sta_password, sizeof(cfg.sta_password));

    config_set_wifi(&cfg);

    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(wifi_config_t));
    snprintf((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", cfg.sta_ssid);
    snprintf((char*)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", cfg.sta_password);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;

    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_connect();

    LOGI(TAG, "Wi-Fi station connecting to: %s", cfg.sta_ssid);

    return send_redirect(req, "/");
}

static esp_err_t disconnect_wifi_handler(httpd_req_t *req) {
    esp_wifi_disconnect();

    wifi_config_state_t cfg;
    config_get_wifi(&cfg);
    cfg.sta_connected = false;
    snprintf(cfg.ip_addr, sizeof(cfg.ip_addr), "0.0.0.0");
    config_set_wifi(&cfg);

    LOGI(TAG, "Wi-Fi station disconnected by user");

    return send_redirect(req, "/");
}

static esp_err_t disconnect_mqtt_handler(httpd_req_t *req) {
    mqtt_config_t cfg;
    config_get_mqtt(&cfg);
    cfg.connected = false;
    config_set_mqtt(&cfg);

    LOGI(TAG, "MQTT disconnected by user");

    return send_redirect(req, "/");
}

/* -------------------------------------------------------------------------- */
/*                               Initialization                               */
/* -------------------------------------------------------------------------- */

void wifi_init_apsta(void) {
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(tcpip_adapter_set_default_wifi_handlers());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(wifi_config_t));
    snprintf((char*)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "ESP8266-Gateway-Config");
    ap_config.ap.ssid_len = strlen((char*)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    snprintf((char*)ap_config.ap.password, sizeof(ap_config.ap.password), "12345678");
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config_state_t sta_cfg;
    config_get_wifi(&sta_cfg);
    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(wifi_config_t));
    if (sta_cfg.sta_ssid[0] != '\0') {
        snprintf((char*)sta_config.sta.ssid, sizeof(sta_config.sta.ssid), "%s", sta_cfg.sta_ssid);
        snprintf((char*)sta_config.sta.password, sizeof(sta_config.sta.password), "%s", sta_cfg.sta_password);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    LOGI(TAG, "Wi-Fi APSTA initialized. AP SSID: %s", ap_config.ap.ssid);
    if (sta_cfg.sta_ssid[0] != '\0') {
        LOGI(TAG, "Station will connect to: %s", sta_cfg.sta_ssid);
    }
}

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;

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

        httpd_uri_t disconnect_wifi_uri = { .uri = "/disconnect_wifi", .method = HTTP_POST, .handler = disconnect_wifi_handler };
        httpd_register_uri_handler(server, &disconnect_wifi_uri);

        httpd_uri_t disconnect_mqtt_uri = { .uri = "/disconnect_mqtt", .method = HTTP_POST, .handler = disconnect_mqtt_handler };
        httpd_register_uri_handler(server, &disconnect_mqtt_uri);

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