/* Version: 2026-10-19. Allocate LVGL's private pool in PSRAM. */
#ifndef PROJECT_LVGL_PSRAM_POOL_H
#define PROJECT_LVGL_PSRAM_POOL_H

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"

static void* project_lvgl_alloc_pool(size_t size) {
    /* Retain the pool across LVGL deinit/init, like its original static array. */
    static void* pool;
    if (pool == NULL) {
        pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pool == NULL) {
            esp_rom_printf("LVGL PSRAM pool allocation failed: %u bytes\n", (unsigned)size);
            abort();
        }
        esp_rom_printf("LVGL private heap: %u bytes in PSRAM at %p\n", (unsigned)size, pool);
    }
    return pool;
}

#define LV_MEM_POOL_ALLOC(size) project_lvgl_alloc_pool(size)

#endif
