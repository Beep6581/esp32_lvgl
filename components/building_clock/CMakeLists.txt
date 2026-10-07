idf_component_register(
    SRCS
        "building_clock_model.c"
        "building_clock_random.c"
        "building_clock_seed.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support
)
