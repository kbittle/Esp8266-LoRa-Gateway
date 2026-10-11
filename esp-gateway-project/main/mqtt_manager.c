#include "mqtt_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "configuration.h"
#include "logger.h"

static const char *TAG = "mqtt_manager";

static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static EventGroupHandle_t g_mqtt_event_group = NULL;
static TaskHandle_t g_mqtt_task_handle = NULL;
static bool g_mqtt_connected = false;
static mqtt_config_t g_current_cfg = {0};
static SemaphoreHandle_t g_mqtt_mutex = NULL;

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_STOP_BIT      BIT1

static char g_base_topic[64] = "lora/gateway/01";

static inline void mqtt_lock(void) {
    if (g_mqtt_mutex) xSemaphoreTake(g_mqtt_mutex, portMAX_DELAY);
}

static inline void mqtt_unlock(void) {
    if (g_mqtt_mutex) xSemaphoreGive(g_mqtt_mutex);
}

static void build_base_topic(const mqtt_config_t *cfg) {
    const char *prefix = cfg->topic_prefix[0] ? cfg->topic_prefix : "lora/gateway";
    const char *gw_id = cfg->gateway_id[0] ? cfg->gateway_id : "01";
    snprintf(g_base_topic, sizeof(g_base_topic), "%s/%s", prefix, gw_id);
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            LOGI(TAG, "MQTT Connected");
            g_mqtt_connected = true;
            mqtt_config_t cfg;
            config_get_mqtt(&cfg);
            cfg.connected = true;
            config_set_mqtt(&cfg);
            xEventGroupSetBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);

            // Subscribe to config/set and commands
            char sub_topic[128];
            snprintf(sub_topic, sizeof(sub_topic), "%s/config/set", g_base_topic);
            esp_mqtt_client_subscribe(g_mqtt_client, sub_topic, 1);
            snprintf(sub_topic, sizeof(sub_topic), "%s/cmd/+", g_base_topic);
            esp_mqtt_client_subscribe(g_mqtt_client, sub_topic, 1);
            LOGI(TAG, "Subscribed to %s/config/set and %s/cmd/+", g_base_topic, g_base_topic);

            mqtt_client_publish_status();
            break;
        case MQTT_EVENT_DISCONNECTED:
            LOGI(TAG, "MQTT Disconnected");
            g_mqtt_connected = false;
            mqtt_config_t cfg_disc;
            config_get_mqtt(&cfg_disc);
            cfg_disc.connected = false;
            config_set_mqtt(&cfg_disc);
            xEventGroupClearBits(g_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            LOGI(TAG, "MQTT Subscribed, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            LOGI(TAG, "MQTT Unsubscribed, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            config_inc_mqtt_pub();
            LOGD(TAG, "MQTT Published, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            LOGD(TAG, "MQTT Data received on topic: %.*s", event->topic_len, event->topic);
            // Handle config/set and commands
            if (event->topic_len > 0 && event->data_len > 0) {
                char topic[128];
                char data[256];
                snprintf(topic, sizeof(topic), "%.*s", event->topic_len, event->topic);
                snprintf(data, sizeof(data), "%.*s", event->data_len, (char*)event->data);

                // Config set
                if (strstr(topic, "/config/set") != NULL) {
                    LOGI(TAG, "Config set received: %s", data);
                    // TODO: Parse JSON and update config
                }
                // Commands
                else if (strstr(topic, "/cmd/") != NULL) {
                    if (strstr(topic, "/cmd/reboot") != NULL) {
                        LOGI(TAG, "Reboot command received");
                        esp_restart();
                    } else if (strstr(topic, "/cmd/factory_reset") != NULL) {
                        LOGI(TAG, "Factory reset command received");
                        // TODO: Implement factory reset
                    }
                }
            }
            break;
        case MQTT_EVENT_ERROR:
            LOGE(TAG, "MQTT Error: %s", strerror(event->error_handle->esp_tls_stack_err));
            break;
        default:
            break;
    }
    return ESP_OK;
}

void mqtt_task(void *pvParameters) {
    mqtt_config_t cfg;
    config_get_mqtt(&cfg);
    g_current_cfg = cfg;
    build_base_topic(&cfg);

    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.event_handle = mqtt_event_handler;
    mqtt_cfg.host = cfg.broker[0] ? cfg.broker : "192.168.1.100";
    mqtt_cfg.port = cfg.port > 0 ? cfg.port : 1883;
    mqtt_cfg.client_id = cfg.client_id[0] ? cfg.client_id : "lora_gateway_01";
    mqtt_cfg.username = cfg.username[0] ? cfg.username : NULL;
    mqtt_cfg.password = cfg.password[0] ? cfg.password : NULL;
    mqtt_cfg.keepalive = 60;
    mqtt_cfg.disable_clean_session = false;

    // Set LWT for connection status
    char lwt_topic[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status/connection", g_base_topic);
    mqtt_cfg.lwt_topic = lwt_topic;
    mqtt_cfg.lwt_msg = "offline";
    mqtt_cfg.lwt_qos = 1;
    mqtt_cfg.lwt_retain = 1;

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt_client == NULL) {
        LOGE(TAG, "Failed to initialize MQTT client");
        vTaskDelete(NULL);
        return;
    }

    // Wait for WiFi station to be connected before starting MQTT
    LOGI(TAG, "Waiting for WiFi connection before starting MQTT...");
    while (1) {
        wifi_config_state_t wifi_cfg;
        config_get_wifi(&wifi_cfg);
        if (wifi_cfg.sta_connected) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt_client));

    LOGI(TAG, "MQTT client started, connecting to %s:%d", mqtt_cfg.host, mqtt_cfg.port);

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(g_mqtt_event_group,
                                                MQTT_STOP_BIT,
                                                pdTRUE,
                                                pdFALSE,
                                                pdMS_TO_TICKS(5000));

        if (bits & MQTT_STOP_BIT) {
            break;
        }

        if (!g_mqtt_connected) {
            wifi_config_state_t wifi_cfg;
            config_get_wifi(&wifi_cfg);
            if (!wifi_cfg.sta_connected) {
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }

            mqtt_config_t new_cfg;
            config_get_mqtt(&new_cfg);
            if (strcmp(new_cfg.broker, g_current_cfg.broker) != 0 ||
                new_cfg.port != g_current_cfg.port ||
                strcmp(new_cfg.client_id, g_current_cfg.client_id) != 0 ||
                strcmp(new_cfg.username, g_current_cfg.username) != 0 ||
                strcmp(new_cfg.password, g_current_cfg.password) != 0 ||
                strcmp(new_cfg.gateway_id, g_current_cfg.gateway_id) != 0) {
                LOGI(TAG, "MQTT config changed, reconnecting...");
                esp_mqtt_client_stop(g_mqtt_client);
                esp_mqtt_client_destroy(g_mqtt_client);
                g_mqtt_client = NULL;

                mqtt_cfg.host = new_cfg.broker[0] ? new_cfg.broker : "192.168.1.100";
                mqtt_cfg.port = new_cfg.port > 0 ? new_cfg.port : 1883;
                mqtt_cfg.client_id = new_cfg.client_id[0] ? new_cfg.client_id : "lora_gateway_01";
                mqtt_cfg.username = new_cfg.username[0] ? new_cfg.username : NULL;
                mqtt_cfg.password = new_cfg.password[0] ? new_cfg.password : NULL;
                g_current_cfg = new_cfg;
                build_base_topic(&new_cfg);

                snprintf(lwt_topic, sizeof(lwt_topic), "%s/status/connection", g_base_topic);
                mqtt_cfg.lwt_topic = lwt_topic;

                g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
                if (g_mqtt_client) {
                    ESP_ERROR_CHECK(esp_mqtt_client_start(g_mqtt_client));
                }
            }
        }
    }

    if (g_mqtt_client) {
        // Publish offline before stopping
        char lwt_topic[128];
        snprintf(lwt_topic, sizeof(lwt_topic), "%s/status/connection", g_base_topic);
        esp_mqtt_client_publish(g_mqtt_client, lwt_topic, "offline", 0, 1, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_mqtt_client_stop(g_mqtt_client);
        esp_mqtt_client_destroy(g_mqtt_client);
        g_mqtt_client = NULL;
    }

    g_mqtt_connected = false;
    mqtt_config_t cfg_final;
    config_get_mqtt(&cfg_final);
    cfg_final.connected = false;
    config_set_mqtt(&cfg_final);

    vTaskDelete(NULL);
}

bool mqtt_client_init(void) {
    if (g_mqtt_event_group == NULL) {
        g_mqtt_event_group = xEventGroupCreate();
    }
    if (g_mqtt_mutex == NULL) {
        g_mqtt_mutex = xSemaphoreCreateMutex();
    }
    return g_mqtt_event_group != NULL && g_mqtt_mutex != NULL;
}

void mqtt_client_start(void) {
    LOGI(TAG, "MQTT client start requested");
}

void mqtt_client_stop(void) {
    if (g_mqtt_event_group) {
        xEventGroupSetBits(g_mqtt_event_group, MQTT_STOP_BIT);
    }
    if (g_mqtt_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    LOGI(TAG, "MQTT client stop requested");
}

bool mqtt_client_is_connected(void) {
    return g_mqtt_connected && g_mqtt_client != NULL;
}

bool mqtt_client_publish_lora_packet(const uint8_t *payload, uint8_t length, int16_t rssi, int8_t snr) {
    mqtt_lock();
    if (!mqtt_client_is_connected() || g_mqtt_client == NULL) {
        mqtt_unlock();
        return false;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/rx", g_base_topic);

    char *json = malloc(256 + length * 2);
    if (!json) return false;

    int offset = snprintf(json, 256 + length * 2,
        "{\"payload\":\"");
    for (int i = 0; i < length; i++) {
        offset += snprintf(json + offset, 256 + length * 2 - offset, "%02X", payload[i]);
    }
    offset += snprintf(json + offset, 256 + length * 2 - offset,
        "\",\"rssi\":%d,\"snr\":%d,\"len\":%u,\"ts\":%llu}", rssi, snr, length,
        (unsigned long long)(esp_timer_get_time() / 1000000));

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, json, 0, 1, false);
    free(json);

    if (msg_id < 0) {
        LOGE(TAG, "Failed to publish LoRa packet");
        mqtt_unlock();
        return false;
    }
    mqtt_unlock();
    return true;
}

bool mqtt_client_publish_status(void) {
    mqtt_lock();
    if (!mqtt_client_is_connected() || g_mqtt_client == NULL) {
        mqtt_unlock();
        return false;
    }

    mqtt_config_t cfg;
    config_get_mqtt(&cfg);
    wifi_config_state_t wifi_cfg;
    config_get_wifi(&wifi_cfg);
    gateway_stats_t stats;
    config_get_stats(&stats);
    lora_config_t lora_cfg;
    config_get_lora(&lora_cfg);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/status", g_base_topic);

    char json[1024];
    int ret = snprintf(json, sizeof(json),
        "{\"uptime\":%lu,\"heap\":%u,\"wifi_connected\":%s,\"ip\":\"%s\","
        "\"lora_freq\":%lu,\"lora_power\":%d,\"lora_sf\":%d,"
        "\"rx_count\":%u,\"tx_count\":%u,\"crc_err\":%u,\"tx_timeout\":%u,\"init_fail\":%u,"
        "\"mqtt_pub\":%u}",
        (unsigned long)(esp_timer_get_time() / 1000000ULL),
        (unsigned int)(esp_get_free_heap_size() / 1024),
        wifi_cfg.sta_connected ? "true" : "false",
        (wifi_cfg.ip_addr && wifi_cfg.ip_addr[0]) ? wifi_cfg.ip_addr : "0.0.0.0",
        (unsigned long)(lora_cfg.frequency_hz / 1000000UL),
        lora_cfg.power_dbm,
        lora_cfg.sf,
        stats.lora_rx_count,
        stats.lora_tx_count,
        stats.lora_crc_err_count,
        stats.lora_tx_timeout_count,
        stats.lora_init_fail_count,
        stats.mqtt_pub_count
    );
    if (ret >= sizeof(json)) ret = sizeof(json) - 1;
    json[ret] = '\0';

    LOGD(TAG, "Publishing status to %s: %s", topic, json);

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, json, 0, 1, true);
    if (msg_id < 0) {
        LOGE(TAG, "Failed to publish status");
        mqtt_unlock();
        return false;
    }
    mqtt_unlock();
    return true;
}

bool mqtt_client_publish_config(const char *section) {
    mqtt_lock();
    if (!mqtt_client_is_connected() || g_mqtt_client == NULL) {
        mqtt_unlock();
        return false;
    }

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/config/get/response", g_base_topic);

    // Build config JSON based on section
    char json[1024];
    json[0] = '\0';

    if (section == NULL || strcmp(section, "all") == 0) {
        // Publish all configs
        lora_config_t lora_cfg;
        config_get_lora(&lora_cfg);
        mqtt_config_t mqtt_cfg;
        config_get_mqtt(&mqtt_cfg);
        wifi_config_state_t wifi_cfg;
        config_get_wifi(&wifi_cfg);
        led_config_t led_cfg;
        config_get_led(&led_cfg);

        snprintf(json, sizeof(json),
            "{"
            "\"lora\":{\"freq_mhz\":%lu,\"power\":%d,\"sf\":%d},"
            "\"mqtt\":{\"broker\":\"%s\",\"port\":%u,\"client_id\":\"%s\",\"gateway_id\":\"%s\",\"topic_prefix\":\"%s\"},"
            "\"wifi\":{\"ssid\":\"%s\",\"connected\":%s,\"ip\":\"%s\"},"
            "\"led\":{\"mode\":%d,\"brightness\":%u,\"r\":%u,\"g\":%u,\"b\":%u}"
            "}",
            (unsigned long)(lora_cfg.frequency_hz / 1000000),
            lora_cfg.power_dbm, lora_cfg.sf,
            mqtt_cfg.broker, mqtt_cfg.port, mqtt_cfg.client_id, mqtt_cfg.gateway_id, mqtt_cfg.topic_prefix,
            wifi_cfg.sta_ssid, wifi_cfg.sta_connected ? "true" : "false", wifi_cfg.ip_addr,
            led_cfg.mode, led_cfg.brightness, led_cfg.r, led_cfg.g, led_cfg.b
        );
    } else if (strcmp(section, "lora") == 0) {
        lora_config_t lora_cfg;
        config_get_lora(&lora_cfg);
        snprintf(json, sizeof(json),
            "{\"lora\":{\"freq_mhz\":%lu,\"power\":%d,\"sf\":%d}}",
            (unsigned long)(lora_cfg.frequency_hz / 1000000), lora_cfg.power_dbm, lora_cfg.sf);
    } else if (strcmp(section, "mqtt") == 0) {
        mqtt_config_t mqtt_cfg;
        config_get_mqtt(&mqtt_cfg);
        snprintf(json, sizeof(json),
            "{\"mqtt\":{\"broker\":\"%s\",\"port\":%u,\"client_id\":\"%s\",\"gateway_id\":\"%s\",\"topic_prefix\":\"%s\"}}",
            mqtt_cfg.broker, mqtt_cfg.port, mqtt_cfg.client_id, mqtt_cfg.gateway_id, mqtt_cfg.topic_prefix);
    } else if (strcmp(section, "wifi") == 0) {
        wifi_config_state_t wifi_cfg;
        config_get_wifi(&wifi_cfg);
        snprintf(json, sizeof(json),
            "{\"wifi\":{\"ssid\":\"%s\",\"connected\":%s,\"ip\":\"%s\"}}",
            wifi_cfg.sta_ssid, wifi_cfg.sta_connected ? "true" : "false", wifi_cfg.ip_addr);
    } else if (strcmp(section, "led") == 0) {
        led_config_t led_cfg;
        config_get_led(&led_cfg);
        snprintf(json, sizeof(json),
            "{\"led\":{\"mode\":%d,\"brightness\":%u,\"r\":%u,\"g\":%u,\"b\":%u}}",
            led_cfg.mode, led_cfg.brightness, led_cfg.r, led_cfg.g, led_cfg.b);
    } else {
        return false;
    }

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, json, 0, 1, false);
    if (msg_id < 0) {
        LOGE(TAG, "Failed to publish config");
        mqtt_unlock();
        return false;
    }
    mqtt_unlock();
    return true;
}