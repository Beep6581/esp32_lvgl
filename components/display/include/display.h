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
    DISPLAY_TIMING_MIN_HBP,
    DISPLAY_TIMING_MIN_HBP_54HZ,
    DISPLAY_TIMING_MIN_HSYNC,
    DISPLAY_TIMING_MID_HBP,
    DISPLAY_TIMING_MID_BAL,
    DISPLAY_TIMING_MID_HSYNC,
    DISPLAY_TIMING_MAX_HBP,
    DISPLAY_TIMING_MAX_BAL,
    DISPLAY_TIMING_MAX_HSYNC,
    DISPLAY_TIMING_COUNT,
} display_timing_mode_t;

typedef struct {
    esp_lcd_rgb_timing_t timing;
    const char* name;
} display_rgb_timing_t;

lv_display_t* display_init(void);
esp_lcd_panel_handle_t display_init_direct(void);
esp_err_t display_prepare(void);
esp_err_t display_backlight_on(void);
const display_rgb_timing_t* display_get_rgb_timing(void);
esp_err_t display_set_timing_mode_and_restart(display_timing_mode_t mode);

#ifdef __cplusplus
}
#endif
