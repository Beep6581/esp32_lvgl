#include "building_clock_model.h"

#include <stdint.h>

_Static_assert(BUILDING_CLOCK_DIGIT_COUNT <= 8, "digit masks store one bit per digit");
_Static_assert(BUILDING_CLOCK_MAX_DIGIT_SECTIONS <= 16, "section dependency masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_PLATFORM_SPANS <= 32, "platform relationship masks are 32 bits");
_Static_assert(BUILDING_CLOCK_MAX_WORKERS <= 16, "worker occupancy masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_LOADS <= 16, "load occupancy masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_NAV_NODES < BUILDING_CLOCK_INVALID_ID, "navigation node identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_NAV_EDGES < UINT16_MAX, "navigation edge identifiers are 16 bits");
