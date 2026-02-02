idf_component_register(
    SRCS "ntc_adc.c"
    INCLUDE_DIRS "include"
    REQUIRES
       esp_adc
       driver
)
