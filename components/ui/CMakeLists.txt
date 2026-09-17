idf_component_register(
    SRCS
        "ui.c"
        "screen_diagnostics.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        display
        lvgl__lvgl
        espressif__esp_lvgl_port
        air_quality
)
