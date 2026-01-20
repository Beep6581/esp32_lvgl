#include "glyph_5x7.h"
#include "glcdfont_5x7.h"

uint8_t glyph_5x7_row(char c, int row) {
    // 5x7 rows: 0..6
    if (row < 0 || row > 6) {
        return 0;
    }

    uint8_t uc = (uint8_t)c;

    // Use '?' if out of range (font table is 0..254)
    if (uc >= GLCDFONT_5X7_GLYPH_COUNT) {
        uc = (uint8_t)'?';
    }

    const uint8_t* cols = &glcdfont_5x7[uc * GLCDFONT_5X7_GLYPH_BYTES];

    // glcdfont stores 5 columns; each column byte holds vertical pixels.
    // Convert column data into a 5-bit row mask (bits 4..0).
    uint8_t out = 0;
    for (int x = 0; x < 5; x++) {
        uint8_t col = cols[x];
        if ((col >> row) & 0x01) {
            out |= (uint8_t)(1u << (4 - x));
        }
    }
    return out;
}
