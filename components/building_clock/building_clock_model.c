#include "building_clock_model.h"

#include <stdint.h>

_Static_assert(BUILDING_CLOCK_DIGIT_COUNT <= 8, "digit masks store one bit per digit");
_Static_assert(BUILDING_CLOCK_MAX_DIGIT_SECTIONS <= 16, "section dependency masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_PLATFORM_SPANS <= 32, "major-support platform masks are 32 bits");
_Static_assert(BUILDING_CLOCK_MAX_MAJOR_SUPPORTS <= 32, "platform support masks are 32 bits");
_Static_assert(BUILDING_CLOCK_MAX_WORKERS <= 16, "worker occupancy masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_LOADS <= 16, "load occupancy masks are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_NAV_NODES < BUILDING_CLOCK_INVALID_ID, "navigation node identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_NAV_EDGES < UINT16_MAX, "navigation edge identifiers are 16 bits");
_Static_assert(BUILDING_CLOCK_MAX_WORKERS < BUILDING_CLOCK_INVALID_ID, "worker identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_JOBS < BUILDING_CLOCK_INVALID_ID, "job identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_TEMP_STRUCTURES < BUILDING_CLOCK_INVALID_ID, "temporary-structure identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_LOADS < BUILDING_CLOCK_INVALID_ID, "load identifiers are 8 bits");
_Static_assert(BUILDING_CLOCK_MAX_DEBRIS_PIECES < BUILDING_CLOCK_INVALID_ID, "debris identifiers are 8 bits");
