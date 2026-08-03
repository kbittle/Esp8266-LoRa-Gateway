#include "configuration.h"
#include <string.h>

static SemaphoreHandle_t g_config_mutex = NULL;

// In-RAM System State Definitions
static lora_config_t g_lora_cfg = LORA_CONFIG_DEFAULT();

static mqtt_config_t g_mqtt_cfg = {
    .broker       = "192.168.1.100",
    .port         = 1883,
    .client_id    = "lora_gateway_01",
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

void config_set_mqtt(const mqtt_config_t *in_cfg) {
    if (xSemaphoreTake(g_config_mutex, portMAX_DELAY) == pdTRUE) {
        g_mqtt_cfg = *in_cfg;
        xSemaphoreGive(g_config_mutex);
    }
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
