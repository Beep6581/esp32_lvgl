cmake_minimum_required(VERSION 3.16)
project(building_clock_viewer C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

set(BUILDING_CLOCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(
    building_clock_viewer
    "building_clock_viewer.c"
    "../../display/fonts/glcdfont_5x7.c"
    "${BUILDING_CLOCK_DIR}/building_clock_complete_validation.c"
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
target_link_libraries(building_clock_viewer PRIVATE PkgConfig::SDL2)
