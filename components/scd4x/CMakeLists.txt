idf_component_register(
    SRCS
        "scd4x.c"
        "port/sensirion_i2c_hal.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        i2c_bus driver freertos
)
