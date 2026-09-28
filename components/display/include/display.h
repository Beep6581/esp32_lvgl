#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_TIMING_WT = 0,
    DISPLAY_TIMING_BS,
    DISPLAY_TIMING_TMIN,
    DISPLAY_TIMING_TMID,
    DISPLAY_TIMING_TMAX,
    DISPLAY_TIMING_COUNT,
} display_timing_mode_t;

lv_display_t* display_init(void);
const esp_lcd_rgb_timing_t* display_get_rgb_timing(void);
esp_err_t display_set_timing_mode_and_restart(display_timing_mode_t mode);

#ifdef __cplusplus
}
#endif
