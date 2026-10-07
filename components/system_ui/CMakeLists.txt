idf_component_register(
    SRCS "system_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        espressif__esp_lvgl_port
        lvgl__lvgl
        wifi_manager
)
