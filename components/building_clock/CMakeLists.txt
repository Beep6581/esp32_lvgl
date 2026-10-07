idf_component_register(
    SRCS
        "building_clock_model.c"
        "building_clock_complete_validation.c"
        "building_clock_generator.c"
        "building_clock_navigation.c"
        "building_clock_random.c"
        "building_clock_seed.c"
        "building_clock_validation.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_hw_support
)
