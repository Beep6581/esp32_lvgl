/* Version: 2026-10-07 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

typedef void (*system_ui_close_callback_t)(void);

esp_err_t system_ui_init(lv_display_t* display, bool open_initially, system_ui_close_callback_t close_callback);
