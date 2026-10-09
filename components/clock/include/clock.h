/* Version: 2026-10-05 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*clock_settings_callback_t)(void);

esp_err_t clock_start(esp_lcd_panel_handle_t panel, clock_settings_callback_t settings_callback);

#ifdef __cplusplus
}
#endif
