idf_component_register(
    SRCS "sht20.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c_bus driver freertos
)
