cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_lvgl)

# Ensure LVGL uses lv_conf.h (independent of include path quirks)
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
if(lvgl_lib)
    target_compile_definitions(${lvgl_lib} PUBLIC LV_CONF_INCLUDE_SIMPLE)
    target_include_directories(${lvgl_lib} PUBLIC
        ${CMAKE_SOURCE_DIR}/components/lvgl_conf/include
    )
endif()

# --------
# Fix compile_commands to not generate clang errors.
# Warning race condition: must run after compile_commands.json is generated.

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(FIX_SCRIPT       ${CMAKE_SOURCE_DIR}/fix_clangd_flags.py)
set(RAW_CC_JSON      ${CMAKE_BINARY_DIR}/compile_commands.json)
set(CLEAN_CC_JSON    ${CMAKE_BINARY_DIR}/compile_commands.clangd.json)

add_custom_command(
  OUTPUT  ${CLEAN_CC_JSON}
  COMMAND ${Python3_EXECUTABLE}
          ${FIX_SCRIPT}
          ${RAW_CC_JSON}
          ${CLEAN_CC_JSON}
  DEPENDS ${RAW_CC_JSON}    # rerun when the raw file changes
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Stripping Xtensa flags for clangd to ${CLEAN_CC_JSON}..."
)

add_custom_target(
  fix_clangd ALL
  DEPENDS ${CLEAN_CC_JSON}
)

add_custom_command(
  TARGET fix_clangd
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove ${RAW_CC_JSON}
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CLEAN_CC_JSON} ${RAW_CC_JSON}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Symlinking ${LINKED_CC_JSON} -> ${CLEAN_CC_JSON}"
)
