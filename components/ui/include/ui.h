#pragma once

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(lv_display_t* disp);
void ui_touch_set_point(uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
