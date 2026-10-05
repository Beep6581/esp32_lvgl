set(clock_sources)
if(CONFIG_APP_MODE_CLOCK)
    list(APPEND clock_sources
        "clock.c"
        "clock_layout.c"
        "particle_engine.c"
    )
endif()

idf_component_register(
    SRCS ${clock_sources}
    INCLUDE_DIRS "include"
    REQUIRES
        board
        heap
        esp_timer
        lvgl__lvgl
        espressif__esp_lvgl_port
)
