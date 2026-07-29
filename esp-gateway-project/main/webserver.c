#include "webserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "logger.h"

/* Compatibility definition for ESP8266_RTOS_SDK */
#ifndef HTTPD_RESP_USE_STRLEN
#define HTTPD_RESP_USE_STRLEN -1
#endif

static const char *TAG = "webserver";

/* -------------------------------------------------------------------------- */
/*                            In-RAM Gateway State                            */
/* -------------------------------------------------------------------------- */

lora_config_t_fix g_lora_cfg = {
    .frequency = 915,
    .power = 14,
    .sf = 12
};

mqtt_config_t g_mqtt_cfg = {
    .broker = "192.168.1.100",
    .port = 1883,
    .client_id = "lora_gateway_01",
    .topic_prefix = "lora/gateway",
    .connected = false
};

wifi_config_state_t g_wifi_cfg = {
    .sta_ssid = "",
    .sta_password = "",
    .sta_connected = false,
    .ip_addr = "0.0.0.0"
};

gateway_stats_t g_stats = {
    .lora_rx_count = 0,
    .lora_tx_count = 0,
    .mqtt_pub_count = 0
};

/* In-RAM Log Buffer */
#define LOG_MAX_ENTRIES 10
#define LOG_ENTRY_LEN 128
static char g_logs[LOG_MAX_ENTRIES][LOG_ENTRY_LEN] = {0};
static int g_log_head = 0;

void app_log_add(const char *msg) {
    if (!msg) return;
    
    // Safely cast 64-bit microsecond timer to 32-bit second uptime
    uint32_t uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    snprintf(g_logs[g_log_head], LOG_ENTRY_LEN, "[%lu] %s", 
             (unsigned long)uptime_sec, msg);

    g_log_head = (g_log_head + 1) % LOG_MAX_ENTRIES;
}

/* -------------------------------------------------------------------------- */
/*                               HTML Static Chunks                           */
/* -------------------------------------------------------------------------- */

static const char page_head[] =
"<!DOCTYPE html><html><head>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"  * { box-sizing: border-box; font-family: 'Segoe UI', sans-serif; }"
"  body { margin: 0; padding: 20px; background: #1a1a2e; color: #e0e0e0; }"
"  .container { max-width: 900px; margin: 0 auto; }"
"  h1 { text-align: center; color: #00fff5; margin-bottom: 20px; }"
"  .card { background: #16213e; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }"
"  .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
"  .stat-box { background: #0f3460; padding: 15px; border-radius: 8px; text-align: center; }"
"  .stat-box h3 { margin: 0; font-size: 14px; color: #00fff5; }"
"  .stat-box p { margin: 5px 0 0; font-size: 20px; font-weight: bold; }"
"  .tabs { display: flex; margin-bottom: 15px; border-bottom: 2px solid #0f3460; }"
"  .tab-btn { padding: 10px 20px; background: none; border: none; color: #888; cursor: pointer; font-size: 16px; font-weight: bold; }"
"  .tab-btn.active { color: #00fff5; border-bottom: 3px solid #00fff5; }"
"  .tab-content { display: none; }"
"  .tab-content.active { display: block; }"
"  label { display: block; margin-top: 10px; color: #00fff5; font-size: 14px; }"
"  input, select { width: 100%; padding: 8px; margin-top: 5px; border-radius: 5px; border: 1px solid #0f3460; background: #0f3460; color: #fff; }"
"  button.submit-btn { background: #00fff5; color: #16213e; border: none; padding: 10px 15px; margin-top: 15px; border-radius: 5px; cursor: pointer; font-weight: bold; width: 100%; }"
"  .logs { background: #000; color: #00ff00; padding: 10px; font-family: monospace; border-radius: 5px; height: 150px; overflow-y: auto; white-space: pre-wrap; }"
"</style>"
"<script>"
"function openTab(evt, tabName) {"
"  var i, tc, tb;"
"  tc = document.getElementsByClassName('tab-content');"
"  for (i = 0; i < tc.length; i++) tc[i].style.display = 'none';"
"  tb = document.getElementsByClassName('tab-btn');"
"  for (i = 0; i < tb.length; i++) tb[i].className = tb[i].className.replace(' active', '');"
"  document.getElementById(tabName).style.display = 'block';"
"  evt.currentTarget.className += ' active';"
"}"
"</script>"
"</head><body>"
"<div class=\"container\">"
"  <h1>LoRa to MQTT Gateway</h1>";

static const char tabs_header[] =
"  <div class=\"tabs\">"
"    <button class=\"tab-btn active\" onclick=\"openTab(event, 'Lora')\">LoRa Config</button>"
"    <button class=\"tab-btn\" onclick=\"openTab(event, 'Mqtt')\">MQTT Config</button>"
"    <button class=\"tab-btn\" onclick=\"openTab(event, 'Wifi')\">Wi-Fi Config</button>"
"  </div>"
"  <div class=\"card\">";

static const char logs_header[] =
"  </div>"
"  <div class=\"card\">"
"    <h3>System Logs</h3>"
"    <div class=\"logs\">";

static const char page_footer[] =
"</div>"
"  </div>"
"</div>"
"</body></html>";

/* -------------------------------------------------------------------------- */
/*                       Safe HTTP Chunked Generator                          */
/* -------------------------------------------------------------------------- */

static esp_err_t root_get_handler(httpd_req_t *req) {
    static char buf[256];

    httpd_resp_set_type(req, "text/html");

    // 1. Send Head & CSS
    httpd_resp_send_chunk(req, page_head, HTTPD_RESP_USE_STRLEN);

    // 2. Send System Health & Statistics Cards
    httpd_resp_send_chunk(req, "<div class=\"card\"><div class=\"grid\">", HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>System Uptime</h3><p>%llds</p></div>",
             (long long)(esp_timer_get_time() / 1000000));
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>Free Heap</h3><p>%u KB</p></div>",
             (unsigned int)(esp_get_free_heap_size() / 1024));
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>Wi-Fi Mode</h3><p>%s</p></div>",
             g_wifi_cfg.sta_connected ? "Station Connected" : "SoftAP Only");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>MQTT Status</h3><p>%s</p></div>",
             g_mqtt_cfg.connected ? "Connected" : "Disconnected");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>LoRa Rx / Tx</h3><p>%u / %u</p></div>",
             (unsigned int)g_stats.lora_rx_count, (unsigned int)g_stats.lora_tx_count);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<div class=\"stat-box\"><h3>MQTT Published</h3><p>%u</p></div>",
             (unsigned int)g_stats.mqtt_pub_count);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "</div></div>", HTTPD_RESP_USE_STRLEN);

    // 3. Send Tab Buttons
    httpd_resp_send_chunk(req, tabs_header, HTTPD_RESP_USE_STRLEN);

    // 4. Send LoRa Tab
    httpd_resp_send_chunk(req, "<div id=\"Lora\" class=\"tab-content active\"><form action=\"/save_lora\" method=\"POST\">", HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Frequency (MHz)</label><input type=\"number\" name=\"frequency\" value=\"%d\">", g_lora_cfg.frequency);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Tx Power (dBm)</label><input type=\"number\" name=\"power\" value=\"%d\">", g_lora_cfg.power);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "<label>Spreading Factor</label><select name=\"sf\">", HTTPD_RESP_USE_STRLEN);

    for (int sf = 7; sf <= 12; sf++) {
        snprintf(buf, sizeof(buf), "<option value=\"%d\" %s>SF%d</option>",
                 sf, (g_lora_cfg.sf == sf) ? "selected" : "", sf);
        httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, "</select><button type=\"submit\" class=\"submit-btn\">Save LoRa Settings</button></form></div>", HTTPD_RESP_USE_STRLEN);

    // 5. Send MQTT Tab
    httpd_resp_send_chunk(req, "<div id=\"Mqtt\" class=\"tab-content\"><form action=\"/save_mqtt\" method=\"POST\">", HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Broker Address</label><input type=\"text\" name=\"broker\" value=\"%s\">",
             g_mqtt_cfg.broker[0] ? g_mqtt_cfg.broker : "");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Port</label><input type=\"number\" name=\"port\" value=\"%d\">", g_mqtt_cfg.port);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Client ID</label><input type=\"text\" name=\"client_id\" value=\"%s\">",
             g_mqtt_cfg.client_id[0] ? g_mqtt_cfg.client_id : "");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Topic Prefix</label><input type=\"text\" name=\"topic_prefix\" value=\"%s\">",
             g_mqtt_cfg.topic_prefix[0] ? g_mqtt_cfg.topic_prefix : "");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "<button type=\"submit\" class=\"submit-btn\">Save MQTT Settings</button></form></div>", HTTPD_RESP_USE_STRLEN);

    // 6. Send Wi-Fi Tab
    httpd_resp_send_chunk(req, "<div id=\"Wifi\" class=\"tab-content\"><form action=\"/save_wifi\" method=\"POST\">", HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Station SSID</label><input type=\"text\" name=\"ssid\" value=\"%s\">",
             g_wifi_cfg.sta_ssid[0] ? g_wifi_cfg.sta_ssid : "");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    snprintf(buf, sizeof(buf), "<label>Station Password</label><input type=\"password\" name=\"password\" value=\"%s\">",
             g_wifi_cfg.sta_password[0] ? g_wifi_cfg.sta_password : "");
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "<button type=\"submit\" class=\"submit-btn\">Connect Wi-Fi</button></form></div>", HTTPD_RESP_USE_STRLEN);

    // 7. Send Log Section Header
    httpd_resp_send_chunk(req, logs_header, HTTPD_RESP_USE_STRLEN);

    // 8. Stream In-RAM Logs
    bool empty = true;
    int idx = g_log_head;
    for (int i = 0; i < LOG_MAX_ENTRIES; i++) {
        if (g_logs[idx][0] != '\0') {
            httpd_resp_send_chunk(req, g_logs[idx], HTTPD_RESP_USE_STRLEN);
            httpd_resp_send_chunk(req, "\n", 1);
            empty = false;
        }
        idx = (idx + 1) % LOG_MAX_ENTRIES;
    }
    if (empty) {
        httpd_resp_send_chunk(req, "System initialized.", HTTPD_RESP_USE_STRLEN);
    }

    // 9. Send Footer & End Response
    httpd_resp_send_chunk(req, page_footer, HTTPD_RESP_USE_STRLEN);
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

static esp_err_t save_lora_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    if (strstr(buf, "frequency=")) sscanf(strstr(buf, "frequency="), "frequency=%d", &g_lora_cfg.frequency);
    if (strstr(buf, "power="))     sscanf(strstr(buf, "power="), "power=%d", &g_lora_cfg.power);
    if (strstr(buf, "sf="))        sscanf(strstr(buf, "sf="), "sf=%d", &g_lora_cfg.sf);

    LOGI(TAG, "LoRa settings updated");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_mqtt_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    parse_form_str(buf, "broker", g_mqtt_cfg.broker, sizeof(g_mqtt_cfg.broker));
    parse_form_str(buf, "client_id", g_mqtt_cfg.client_id, sizeof(g_mqtt_cfg.client_id));
    parse_form_str(buf, "topic_prefix", g_mqtt_cfg.topic_prefix, sizeof(g_mqtt_cfg.topic_prefix));

    char port_str[8] = {0};
    parse_form_str(buf, "port", port_str, sizeof(port_str));
    if (port_str[0] != '\0') g_mqtt_cfg.port = atoi(port_str);

    LOGI(TAG, "MQTT settings updated");

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    char buf[256] = {0};
    if (httpd_req_recv(req, buf, sizeof(buf) - 1) <= 0) return ESP_FAIL;

    parse_form_str(buf, "ssid", g_wifi_cfg.sta_ssid, sizeof(g_wifi_cfg.sta_ssid));
    parse_form_str(buf, "password", g_wifi_cfg.sta_password, sizeof(g_wifi_cfg.sta_password));

    wifi_config_t sta_config = {0};
    strncpy((char*)sta_config.sta.ssid, g_wifi_cfg.sta_ssid, sizeof(sta_config.sta.ssid));
    strncpy((char*)sta_config.sta.password, g_wifi_cfg.sta_password, sizeof(sta_config.sta.password));

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
