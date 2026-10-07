idf_component_register(
    SRCS "wifi_qr_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES
        lvgl__lvgl
        espressif__esp_lvgl_port
)
