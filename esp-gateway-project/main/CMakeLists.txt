idf_component_register(
    SRCS "main.c" "lora.c" "sk9822.c" "webserver.c" "mqtt_manager.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "$(IDF_PATH)/components/mqtt/esp-mqtt/include"
    REQUIRES esp_http_server esp_wifi nvs_flash mqtt
    EMBED_TXTFILES "index.html"
)
