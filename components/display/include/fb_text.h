#pragma once
#include <stdint.h>

typedef enum {
    FB_FONT_5X7 = 0,
} fb_font_t;

typedef enum {
    FB_ALIGN_CENTER = 0,
    FB_ALIGN_LEFT,
    FB_ALIGN_RIGHT,
    FB_ALIGN_TOP,
    FB_ALIGN_BOTTOM,
} fb_align_t;

#ifdef __cplusplus
extern "C" {
#endif

// Draw a null-terminated string centered on screen.
void fb_draw_text(uint16_t* fb, int w, int h, const char* s, uint16_t color, int scale, fb_font_t font, fb_align_t align);
void fb_draw_text_2fb(uint16_t* fb1, uint16_t* fb2, int w, int h, const char* s, uint16_t color, int scale, fb_font_t font, fb_align_t align);

#ifdef __cplusplus
}
#endif
