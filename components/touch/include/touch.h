#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*touch_point_callback_t)(uint16_t x, uint16_t y);

void touch_set_point_callback(touch_point_callback_t callback);
esp_err_t touch_start(void);

#ifdef __cplusplus
}
#endif
