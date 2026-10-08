idf_component_register(
    SRCS "src/main.c"
    REQUIRES display ui clock air_quality i2c_bus touch wifi_manager system_ui esp_system freertos
)
