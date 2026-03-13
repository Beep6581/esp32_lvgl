idf_component_register(
    SRCS "stcc4.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c_bus driver freertos
)
