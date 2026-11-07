idf_component_register(SRCS "main.c" "ronin_ble.c" "duml.c" INCLUDE_DIRS "." EMBED_TXTFILES "index.html" REQUIRES bt nvs_flash esp_wifi esp_event esp_netif esp_http_server esp_timer)
