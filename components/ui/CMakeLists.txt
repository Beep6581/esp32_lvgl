set(ui_sources)
set(ui_requires board display lvgl__lvgl espressif__esp_lvgl_port air_quality)

if(CONFIG_APP_MODE_AIR_QUALITY)
    list(APPEND ui_sources "ui.c")
elseif(CONFIG_APP_MODE_SCREEN_DIAGNOSTICS)
    list(APPEND ui_sources "screen_diagnostics.c")
endif()

idf_component_register(
    SRCS ${ui_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${ui_requires}
)
