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

add_executable(
    building_clock_navigation_tests
    "test_building_clock_navigation.c"
    "../building_clock_complete_validation.c"
    "../building_clock_generator.c"
    "../building_clock_navigation.c"
    "../building_clock_random.c"
    "../building_clock_validation.c"
)

target_include_directories(
    building_clock_navigation_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_navigation_tests PRIVATE c_std_11)
target_compile_options(building_clock_navigation_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME building_clock_navigation_tests COMMAND building_clock_navigation_tests)

set(
    BUILDING_CLOCK_WORLD_SELECTION_SOURCES
    "../building_clock_complete_validation.c"
    "../building_clock_generator.c"
    "../building_clock_navigation.c"
    "../building_clock_random.c"
    "../building_clock_validation.c"
    "../building_clock_world_selection.c"
)

add_executable(
    building_clock_world_selection_tests
    "test_building_clock_world_selection.c"
    ${BUILDING_CLOCK_WORLD_SELECTION_SOURCES}
)

target_include_directories(
    building_clock_world_selection_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_world_selection_tests PRIVATE c_std_11)
target_compile_options(building_clock_world_selection_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME building_clock_world_selection_tests COMMAND building_clock_world_selection_tests)

add_executable(
    building_clock_invalid_fallback_tests
    "test_building_clock_invalid_fallback.c"
    ${BUILDING_CLOCK_WORLD_SELECTION_SOURCES}
)

target_include_directories(
    building_clock_invalid_fallback_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_invalid_fallback_tests PRIVATE c_std_11)
target_compile_options(building_clock_invalid_fallback_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(
    building_clock_invalid_fallback_tests
    PRIVATE
        BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS=1U
        BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED=0x6a09e667f3bcc909ULL
        BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION=1U
        BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT=0U
)

add_test(NAME building_clock_invalid_fallback_tests COMMAND building_clock_invalid_fallback_tests)

add_executable(
    building_clock_runtime_navigation_tests
    "test_building_clock_runtime_navigation.c"
    ${BUILDING_CLOCK_WORLD_SELECTION_SOURCES}
    "../building_clock_runtime_navigation.c"
)

target_include_directories(
    building_clock_runtime_navigation_tests
    PRIVATE "../include"
)

target_compile_features(building_clock_runtime_navigation_tests PRIVATE c_std_11)
target_compile_options(building_clock_runtime_navigation_tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME building_clock_runtime_navigation_tests COMMAND building_clock_runtime_navigation_tests)
