cmake_minimum_required(VERSION 3.16)
project(building_clock_random_tests C)

enable_testing()

add_executable(
    building_clock_random_tests
    "test_building_clock_random.c"
    "../building_clock_random.c"
)

target_include_directories(
    building_clock_random_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_random_tests PRIVATE c_std_11)
target_compile_options(building_clock_random_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME building_clock_random_tests COMMAND building_clock_random_tests)

add_executable(
    building_clock_generator_tests
    "test_building_clock_generator.c"
    "../building_clock_generator.c"
    "../building_clock_random.c"
    "../building_clock_validation.c"
)

target_include_directories(
    building_clock_generator_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_generator_tests PRIVATE c_std_11)
target_compile_options(building_clock_generator_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME building_clock_generator_tests COMMAND building_clock_generator_tests)
