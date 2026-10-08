idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_common
        esp_event
        esp_http_client
        esp_netif
        esp_wifi
        freertos
        mbedtls
        nvs_flash
        wpa_supplicant
)
