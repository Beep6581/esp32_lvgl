/* Version: 2026-10-07 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

esp_err_t system_ui_init(lv_display_t* display, bool open_initially);
