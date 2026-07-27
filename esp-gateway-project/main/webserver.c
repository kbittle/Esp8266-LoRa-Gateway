#include "webserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "tcpip_adapter.h"

static const char *TAG = "webserver";

/* -------------------------------------------------------------------------- */
/*                          Gateway Configuration State                       */
/* -------------------------------------------------------------------------- */

typedef struct {
    int frequency; /* MHz */
    int power;     /* dBm */
    int sf;        /* Spreading factor */
} gateway_config_t;

#define FREQ_MIN 137
#define FREQ_MAX 1020
#define POWER_MIN 2
#define POWER_MAX 20
#define SF_MIN 7
#define SF_MAX 12

static gateway_config_t current_config = {
    .frequency = 915,
    .power = 14,
    .sf = 12
};

/* -------------------------------------------------------------------------- */
/*                               HTML Templates                               */
/* -------------------------------------------------------------------------- */

/* Shared look and feel for both pages. Kept as its own literal so it isn't
 * duplicated (and so it isn't part of any snprintf format string). */
static const char page_style[] =
"  <style>"
"    * { box-sizing: border-box; }"
"    body {"
"      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;"
"      margin: 0; min-height: 100vh; display: flex; align-items: center; justify-content: center;"
"      background: linear-gradient(135deg, #4facfe 0%, #7367f0 55%, #6a3df0 100%);"
"      padding: 24px;"
"    }"
"    .card {"
"      background: #ffffff; padding: 32px 28px; border-radius: 18px; max-width: 400px; width: 100%;"
"      box-shadow: 0 20px 40px rgba(30, 20, 90, 0.25);"
"    }"
"    h2 {"
"      color: #2b2450; text-align: center; margin: 0 0 22px; font-size: 22px; letter-spacing: 0.3px;"
"    }"
"    label {"
"      display: flex; align-items: center; gap: 6px; margin-top: 16px; font-weight: 600; font-size: 14px; color: #3d3763;"
"    }"
"    input, select {"
"      width: 100%; padding: 10px 12px; margin-top: 6px; border-radius: 8px;"
"      border: 1.5px solid #dcd9f0; font-size: 15px; background: #faf9ff; transition: border-color .15s, box-shadow .15s;"
"    }"
"    input:focus, select:focus {"
"      outline: none; border-color: #7367f0; box-shadow: 0 0 0 3px rgba(115, 103, 240, 0.18);"
"    }"
"    small { display: block; margin-top: 4px; color: #918dab; font-size: 12px; }"
"    button {"
"      background: linear-gradient(135deg, #7367f0, #4facfe); color: white; border: none; padding: 12px;"
"      margin-top: 26px; width: 100%; border-radius: 8px; cursor: pointer; font-size: 16px; font-weight: 600;"
"      letter-spacing: 0.3px; transition: transform .12s, box-shadow .12s;"
"    }"
"    button:hover { transform: translateY(-1px); box-shadow: 0 8px 18px rgba(115, 103, 240, 0.35); }"
"    button:active { transform: translateY(0); }"
"    .banner {"
"      background: #e6f8ee; color: #1c8a53; border: 1px solid #b9ecd0; padding: 10px 12px; border-radius: 8px;"
"      margin-bottom: 18px; text-align: center; font-size: 14px; font-weight: 600;"
"    }"
"    .error-card h2 { color: #c0392b; }"
"    .error-card p { color: #5b5578; text-align: center; font-size: 14px; line-height: 1.5; }"
"    .error-card a {"
"      display: block; text-align: center; margin-top: 20px; color: #7367f0; font-weight: 600; text-decoration: none;"
"    }"
"    .error-card a:hover { text-decoration: underline; }"
"  </style>";

/* %s = banner/success message, then %d = frequency, %d = power, then 6x %s
 * for the SF <option> "selected" markers */
static const char config_page_template[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"  <link rel=\"icon\" href=\"data:,\">"
"  <title>ESP8266 Gateway Configuration</title>"
"%s"
"</head>"
"<body>"
"  <div class=\"card\">"
"    <h2>&#128225; Gateway Configuration</h2>"
"    %s"
"    <form action=\"/save\" method=\"POST\">"
"      <label for=\"frequency\">&#128246; LoRa Frequency (MHz)</label>"
"      <input type=\"number\" id=\"frequency\" name=\"frequency\" value=\"%d\" min=\"%d\" max=\"%d\" required>"
"      <small>Allowed range: %d&ndash;%d MHz</small>"
"      <label for=\"power\">&#9889; Tx Power (dBm)</label>"
"      <input type=\"number\" id=\"power\" name=\"power\" value=\"%d\" min=\"%d\" max=\"%d\" required>"
"      <small>Allowed range: %d&ndash;%d dBm</small>"
"      <label for=\"sf\">&#128257; Spreading Factor</label>"
"      <select id=\"sf\" name=\"sf\">"
"        <option value=\"7\" %s>SF7</option>"
"        <option value=\"8\" %s>SF8</option>"
"        <option value=\"9\" %s>SF9</option>"
"        <option value=\"10\" %s>SF10</option>"
"        <option value=\"11\" %s>SF11</option>"
"        <option value=\"12\" %s>SF12</option>"
"      </select>"
"      <button type=\"submit\">Save Settings</button>"
"    </form>"
"  </div>"
"</body>"
"</html>";

/* %s = style block, %s = error message */
static const char error_page_template[] =
"<!DOCTYPE html>"
"<html>"
"<head>"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"  <link rel=\"icon\" href=\"data:,\">"
"  <title>Configuration Error</title>"
"%s"
"</head>"
"<body>"
"  <div class=\"card error-card\">"
"    <h2>&#9888; Invalid Configuration</h2>"
"    <p>%s</p>"
"    <a href=\"/\">&larr; Go Back</a>"
"  </div>"
"</body>"
"</html>";

/* -------------------------------------------------------------------------- */
/*                               Helper Functions                             */
/* -------------------------------------------------------------------------- */

/* Extracts an integer value for "key=" out of a x-www-form-urlencoded body.
 * Returns 0 on success and writes the value to *out, -1 if the key is missing
 * or has no parseable integer after it. */
static int parse_form_int(const char *body, const char *key, int *out) {
    char needle[24];
    snprintf(needle, sizeof(needle), "%s=", key);

    const char *pos = strstr(body, needle);
    if (pos == NULL) {
        return -1;
    }
    pos += strlen(needle);

    if (*pos == '\0' || (*pos != '-' && (*pos < '0' || *pos > '9'))) {
        return -1;
    }

    *out = atoi(pos);
    return 0;
}

/* Builds the config page into a freshly malloc'd buffer reflecting the
 * currently active configuration. Caller must free() the result. */
static char *build_config_page(const gateway_config_t *cfg, const char *banner) {
    size_t buf_size = sizeof(config_page_template) + sizeof(page_style)
        + strlen(banner) + 256;
    char *page = malloc(buf_size);
    if (page == NULL) {
        return NULL;
    }

    snprintf(page, buf_size, config_page_template,
        page_style,
        banner,
        cfg->frequency, FREQ_MIN, FREQ_MAX, FREQ_MIN, FREQ_MAX,
        cfg->power, POWER_MIN, POWER_MAX, POWER_MIN, POWER_MAX,
        cfg->sf == 7  ? "selected" : "",
        cfg->sf == 8  ? "selected" : "",
        cfg->sf == 9  ? "selected" : "",
        cfg->sf == 10 ? "selected" : "",
        cfg->sf == 11 ? "selected" : "",
        cfg->sf == 12 ? "selected" : "");

    return page;
}

/* Builds the error page into a freshly malloc'd buffer sized to fit the
 * given message (avoids the fixed-buffer format-truncation warning that
 * comes from hand-sizing a stack buffer for a variable-length message).
 * Caller must free() the result. */
static char *build_error_page(const char *message) {
    size_t buf_size = sizeof(error_page_template) + sizeof(page_style)
        + strlen(message) + 64;
    char *page = malloc(buf_size);
    if (page == NULL) {
        return NULL;
    }

    snprintf(page, buf_size, error_page_template, page_style, message);
    return page;
}

/* -------------------------------------------------------------------------- */
/*                            HTTP URI Handlers                               */
/* -------------------------------------------------------------------------- */

static esp_err_t root_get_handler(httpd_req_t *req) {
    char *page = build_config_page(&current_config, "");
    if (page == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, strlen(page));
    free(page);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[100];
    int remaining = req->content_len;

    if (remaining <= 0 || remaining >= (int)sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = 0;
    while (received < remaining) {
        int ret = httpd_req_recv(req, buf + received, remaining - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    ESP_LOGI(TAG, "Received POST data: %s", buf);

    int frequency, power, sf;
    if (parse_form_int(buf, "frequency", &frequency) != 0 ||
        parse_form_int(buf, "power", &power) != 0 ||
        parse_form_int(buf, "sf", &sf) != 0) {
        char *page = build_error_page("Missing or malformed form fields.");
        if (page == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page, strlen(page));
        free(page);
        return ESP_FAIL;
    }

    if (frequency < FREQ_MIN || frequency > FREQ_MAX ||
        power < POWER_MIN || power > POWER_MAX ||
        sf < SF_MIN || sf > SF_MAX) {
        char err[160];
        snprintf(err, sizeof(err),
            "Values out of range. Frequency %d-%d MHz, power %d-%d dBm, SF %d-%d.",
            FREQ_MIN, FREQ_MAX, POWER_MIN, POWER_MAX, SF_MIN, SF_MAX);
        char *page = build_error_page(err);
        if (page == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, page, strlen(page));
        free(page);
        return ESP_FAIL;
    }

    /* Values are valid: commit them.
     * NOTE: this only updates the in-RAM config. If you want settings to
     * survive a reboot, persist current_config to NVS here. */
    current_config.frequency = frequency;
    current_config.power = power;
    current_config.sf = sf;

    ESP_LOGI(TAG, "Config updated: freq=%d power=%d sf=%d",
        current_config.frequency, current_config.power, current_config.sf);

    char *page = build_config_page(&current_config,
        "<div class=\"banner\">Configuration saved successfully!</div>");
    if (page == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, strlen(page));
    free(page);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*                              Public Functions                              */
/* -------------------------------------------------------------------------- */

void wifi_init_softap(void) {
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ESP8266-Gateway-Config",
            .ssid_len = strlen("ESP8266-Gateway-Config"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi SoftAP initialized. SSID: ESP8266-Gateway-Config | Password: 12345678");
}

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t save_uri = {
            .uri      = "/save",
            .method   = HTTP_POST,
            .handler  = save_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_uri);

        ESP_LOGI(TAG, "Webserver started successfully.");
        return server;
    }

    ESP_LOGE(TAG, "Error starting webserver!");
    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "Webserver stopped.");
    }
}
