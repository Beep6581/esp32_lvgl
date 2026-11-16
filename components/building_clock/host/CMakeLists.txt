cmake_minimum_required(VERSION 3.16)
project(building_clock_viewer C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)

set(BUILDING_CLOCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(
    building_clock_viewer
    "building_clock_viewer.c"
    "../../display/fonts/glcdfont_5x7.c"
    "${BUILDING_CLOCK_DIR}/building_clock_complete_validation.c"
    "${BUILDING_CLOCK_DIR}/building_clock_geometry.c"
    "${BUILDING_CLOCK_DIR}/building_clock_generator.c"
    "${BUILDING_CLOCK_DIR}/building_clock_navigation.c"
    "${BUILDING_CLOCK_DIR}/building_clock_random.c"
    "${BUILDING_CLOCK_DIR}/building_clock_runtime_navigation.c"
    "${BUILDING_CLOCK_DIR}/building_clock_scene.c"
    "${BUILDING_CLOCK_DIR}/building_clock_simulation.c"
    "${BUILDING_CLOCK_DIR}/building_clock_validation.c"
    "${BUILDING_CLOCK_DIR}/building_clock_world_selection.c"
)

target_include_directories(
    building_clock_viewer
    PRIVATE
        "${BUILDING_CLOCK_DIR}/include"
        "../../display/include"
)

target_compile_features(building_clock_viewer PRIVATE c_std_11)
target_compile_options(building_clock_viewer PRIVATE -Wall -Wextra -Werror)
target_link_libraries(building_clock_viewer PRIVATE PkgConfig::SDL2 PkgConfig::PNG)

enable_testing()
add_test(NAME building_clock_character_animation COMMAND building_clock_viewer --animation-check)
set_tests_properties(building_clock_character_animation PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

# Diagnostic draw-only benchmark; normal viewer builds do not need this target.
add_executable(building_clock_render_benchmark EXCLUDE_FROM_ALL
    "benchmark_building_clock_render.c"
    "${BUILDING_CLOCK_DIR}/building_clock_geometry.c"
)
target_include_directories(building_clock_render_benchmark PRIVATE "${BUILDING_CLOCK_DIR}/include" "../../display/include")
target_compile_features(building_clock_render_benchmark PRIVATE c_std_11)
target_compile_options(building_clock_render_benchmark PRIVATE -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
target_link_options(building_clock_render_benchmark PRIVATE -Wl,--gc-sections)
target_link_libraries(building_clock_render_benchmark PRIVATE PkgConfig::SDL2 PkgConfig::PNG)
