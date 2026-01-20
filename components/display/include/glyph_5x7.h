#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns a 5-bit row (bits 4..0) for a 5x7 glyph row [0..6].
// Unknown chars return 0.
uint8_t glyph_5x7_row(char c, int row);

#ifdef __cplusplus
}
#endif
