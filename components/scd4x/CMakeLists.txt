idf_component_register(
    SRCS
        "scd4x.c"
        "port/sensirion_i2c_hal.c"
        "vendor/embedded-i2c-scd4x/scd4x_i2c.c"
        "vendor/embedded-i2c-scd4x/sensirion_common.c"
        "vendor/embedded-i2c-scd4x/sensirion_i2c.c"
    INCLUDE_DIRS
        "include"
        "vendor/embedded-i2c-scd4x"
    REQUIRES
        i2c_bus driver freertos
)
