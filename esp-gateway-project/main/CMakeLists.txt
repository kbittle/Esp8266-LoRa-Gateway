idf_component_register(
    SRCS "main.c" "lora.c" "sk9822.c" "webserver.c"
    INCLUDE_DIRS "." 
                 "../SWSD003/sx126x/sx126x_driver/src" # Core SX1262 driver headers
                 "../SWSD003/common/inc"               # Common SWSD003 headers
    REQUIRES esp_http_server esp_wifi nvs_flash
    EMBED_TXTFILES "index.html"
)
