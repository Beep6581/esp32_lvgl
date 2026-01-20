#pragma once
#include <stdint.h>

#define GLCDFONT_5X7_GLYPH_COUNT 255
#define GLCDFONT_5X7_GLYPH_BYTES 5

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t glcdfont_5x7[GLCDFONT_5X7_GLYPH_COUNT * GLCDFONT_5X7_GLYPH_BYTES];

#ifdef __cplusplus
}
#endif
