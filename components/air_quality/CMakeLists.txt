idf_component_register(
    SRCS "air_quality.c"
    INCLUDE_DIRS "include"
    REQUIRES
      scd4x sht20 xy_md0x i2c_bus freertos
)
