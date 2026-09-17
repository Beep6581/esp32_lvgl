#pragma once

#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t* display_init(void);
const esp_lcd_rgb_timing_t* display_get_rgb_timing(void);

#ifdef __cplusplus
}
#endif
