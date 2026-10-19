/* Version: 2026-10-19. Report LVGL assertions instead of silently spinning. */
#ifndef PROJECT_LVGL_ASSERT_HANDLER_H
#define PROJECT_LVGL_ASSERT_HANDLER_H

#include <stdlib.h>
#include "esp_rom_sys.h"

/* ROM output remains available with LVGL logging disabled and needs no heap. */
#define LV_ASSERT_HANDLER do { \
    esp_rom_printf("LVGL assertion failed: %s:%d (%s)\n", __FILE__, __LINE__, __func__); \
    abort(); \
} while (0);

#endif
