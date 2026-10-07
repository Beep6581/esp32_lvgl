/* Version: 2026-10-07 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t wifi_qr_ui_show(lv_display_t* display, const char* dpp_uri);
