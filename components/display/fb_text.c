#include "fb_text.h"
#include "glyph_5x7.h"
#include <string.h>

static inline void fb_put_px_u16(uint16_t* fb, int w, int h, int x, int y, uint16_t c) {
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h)
        return;
    fb[y * w + x] = c;
}

static void font_metrics(fb_font_t font, int scale, int* glyph_w, int* glyph_h, int* spacing) {
    // Monospace fonts only
    switch (font) {
    case FB_FONT_5X7:
    default:
        *glyph_w = 5 * scale;
        *glyph_h = 7 * scale;
        *spacing = 1 * scale;
        break;
    }
}

static uint8_t font_row_bits(fb_font_t font, char c, int row) {
    switch (font) {
    case FB_FONT_5X7:
    default:
        return glyph_5x7_row(c, row); // 5-bit row, bits [4:0]
    }
}

static int font_rows(fb_font_t font) {
    switch (font) {
    case FB_FONT_5X7:
    default:
        return 7;
    }
}

static void compute_start_xy(int w, int h, int text_w, int text_h, fb_align_t align, int* x0, int* y0) {
    // Default: centered
    int x = (w - text_w) / 2;
    int y = (h - text_h) / 2;

    switch (align) {
    case FB_ALIGN_LEFT:
        x = 0;
        break;
    case FB_ALIGN_RIGHT:
        x = w - text_w;
        break;
    case FB_ALIGN_TOP:
        y = 0;
        break;
    case FB_ALIGN_BOTTOM:
        y = h - text_h;
        break;
    case FB_ALIGN_CENTER:
    default:
        break;
    }

    // Clamp so it never starts outside the framebuffer
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    *x0 = x;
    *y0 = y;
}

void fb_draw_text_2fb(uint16_t* fb1, uint16_t* fb2, int w, int h, const char* s, uint16_t color, int scale, fb_font_t font, fb_align_t align) {
    fb_draw_text(fb1, w, h, s, color, scale, font, align);
    fb_draw_text(fb2, w, h, s, color, scale, font, align);
}

void fb_draw_text(uint16_t* fb, int w, int h, const char* s, uint16_t color, int scale, fb_font_t font, fb_align_t align) {
    if (!fb || !s)
        return;
    if (scale < 1)
        scale = 1;

    int glyph_w, glyph_h, spacing;
    font_metrics(font, scale, &glyph_w, &glyph_h, &spacing);

    const int len = (int)strlen(s);
    if (len <= 0)
        return;

    const int text_w = len * glyph_w + (len - 1) * spacing;
    const int text_h = glyph_h;

    int x0, y0;
    compute_start_xy(w, h, text_w, text_h, align, &x0, &y0);

    const int rows = font_rows(font);

    int x = x0;
    for (int i = 0; i < len; i++) {
        const char ch = s[i];

        // Draw one glyph at (x, y0)
        for (int row = 0; row < rows; row++) {
            const uint8_t bits = font_row_bits(font, ch, row); // 5-bit pattern

            for (int col = 0; col < 5; col++) { // 5 columns for 5x7
                if (bits & (1u << (4 - col))) {
                    // scale x scale block
                    const int px = x + col * scale;
                    const int py = y0 + row * scale;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            fb_put_px_u16(fb, w, h, px + dx, py + dy, color);
                        }
                    }
                }
            }
        }

        x += glyph_w + spacing;
    }
}
