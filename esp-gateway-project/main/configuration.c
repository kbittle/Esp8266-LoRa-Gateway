#include "configuration.h"
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *NVS_NAMESPACE = "gateway_cfg";

static SemaphoreHandle_t g_config_mutex = NULL;

static esp_err_t config_nvs_load_wifi(void);
static esp_err_t config_nvs_save_wifi(void);
static esp_err_t config_nvs_load_mqtt(void);
static esp_err_t config_nvs_save_mqtt(void);

// In-RAM System State Definitions
static led_config_t g_led_cfg = {
    .mode = LED_MODE_RAINBOW,
    .brightness = 10,
    .r = 30,
    .g = 0,
    .b = 0
};

static lora_config_t g_lora_cfg = LORA_CONFIG_DEFAULT();

static mqtt_config_t g_mqtt_cfg = {
    .broker       = "192.168.1.100",
    .port         = 1883,
    .client_id    = "lora_gateway_01",
    .username     = "",
    .password     = "",
    .gateway_id   = "01",
    .topic_prefix = "lora/gateway",
    .connected    = false
};

static wifi_config_state_t g_wifi_cfg = {
    .sta_ssid      = "",
    .sta_password  = "",
    .sta_connected = false,
    .ip_addr       = "0.0.0.0"
};

static gateway_stats_t g_stats = {0};

void config_init(void) {
    if (g_config_mutex == NULL) {
        g_config_mutex = xSemaphoreCreateMutex();
    }
    config_nvs_load_wifi();
    config_nvs_load_mqtt();
}

void config_get_led(led_config_t *out_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        *out_cfg = g_led_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_set_led(const led_config_t *in_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_led_cfg = *in_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_get_lora(lora_config_t *out_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        *out_cfg = g_lora_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_set_lora(const lora_config_t *in_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_lora_cfg = *in_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_get_mqtt(mqtt_config_t *out_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        *out_cfg = g_mqtt_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

static esp_err_t config_nvs_load_wifi(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t ssid_len = sizeof(g_wifi_cfg.sta_ssid);
    size_t pass_len = sizeof(g_wifi_cfg.sta_password);
    err = nvs_get_str(handle, "sta_ssid", g_wifi_cfg.sta_ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "sta_pass", g_wifi_cfg.sta_password, &pass_len);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t config_nvs_save_wifi(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "sta_ssid", g_wifi_cfg.sta_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "sta_pass", g_wifi_cfg.sta_password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void config_get_wifi(wifi_config_state_t *out_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        *out_cfg = g_wifi_cfg;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_set_wifi(const wifi_config_state_t *in_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_wifi_cfg = *in_cfg;
        xSemaphoreGive(g_config_mutex);
    }
    config_nvs_save_wifi();
}

void config_set_mqtt(const mqtt_config_t *in_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_mqtt_cfg = *in_cfg;
        xSemaphoreGive(g_config_mutex);
    }
    config_nvs_save_mqtt();
}

static esp_err_t config_nvs_load_mqtt(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t broker_len = sizeof(g_mqtt_cfg.broker);
    size_t client_id_len = sizeof(g_mqtt_cfg.client_id);
    size_t username_len = sizeof(g_mqtt_cfg.username);
    size_t password_len = sizeof(g_mqtt_cfg.password);
    size_t gateway_id_len = sizeof(g_mqtt_cfg.gateway_id);
    size_t topic_prefix_len = sizeof(g_mqtt_cfg.topic_prefix);

    err = nvs_get_str(handle, "mqtt_broker", g_mqtt_cfg.broker, &broker_len);
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "mqtt_client_id", g_mqtt_cfg.client_id, &client_id_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "mqtt_username", g_mqtt_cfg.username, &username_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "mqtt_password", g_mqtt_cfg.password, &password_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "mqtt_gateway_id", g_mqtt_cfg.gateway_id, &gateway_id_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_str(handle, "mqtt_topic_prefix", g_mqtt_cfg.topic_prefix, &topic_prefix_len);
    }
    if (err == ESP_OK) {
        err = nvs_get_u16(handle, "mqtt_port", &g_mqtt_cfg.port);
    }
    nvs_close(handle);
    return err;
}

static esp_err_t config_nvs_save_mqtt(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, "mqtt_broker", g_mqtt_cfg.broker);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "mqtt_client_id", g_mqtt_cfg.client_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "mqtt_username", g_mqtt_cfg.username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "mqtt_password", g_mqtt_cfg.password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "mqtt_gateway_id", g_mqtt_cfg.gateway_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "mqtt_topic_prefix", g_mqtt_cfg.topic_prefix);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, "mqtt_port", g_mqtt_cfg.port);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

void config_get_stats(gateway_stats_t *out_stats) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        *out_stats = g_stats;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_rx(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.lora_rx_count++;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_tx(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.lora_tx_count++;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_crc_err(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.lora_crc_err_count++;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_tx_timeout(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.lora_tx_timeout_count++;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_init_fail(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.lora_init_fail_count++;
        xSemaphoreGive(g_config_mutex);
    }
}

void config_inc_mqtt_pub(void) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_stats.mqtt_pub_count++;
        xSemaphoreGive(g_config_mutex);
    }
}