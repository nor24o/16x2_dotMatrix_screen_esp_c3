idf_component_register(SRCS "main.c" "matrix.c" "settings.c" "webserver.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_http_server nvs_flash esp_timer driver esp_hw_support freertos log)
