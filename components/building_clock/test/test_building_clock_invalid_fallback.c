#include "building_clock_generator.h"
#include "building_clock_world_selection.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define INVALID_FALLBACK_ROOT UINT64_C(0x37a3c00e69d91259)

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static bool world_is_cleared(const building_clock_world_t* world) {
    const unsigned char* bytes = (const unsigned char*)world;
    for (size_t index = 0U; index < sizeof(*world); index++) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

int main(void) {
    building_clock_world_t world;
    const building_clock_world_selection_result_t result = building_clock_select_world(&world, INVALID_FALLBACK_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);

    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE, "invalid fallback returns a hard failure");
    expect_true(result.fallback_used, "failed fallback attempt is reported");
    expect_true(result.accepted_attempt == BUILDING_CLOCK_WORLD_NO_ACCEPTED_ATTEMPT, "hard failure has no accepted attempt");
    expect_true(result.normal_attempt_count == 1U && result.pipeline_attempt_count == 2U, "normal and fallback candidates both ran");
    expect_true(result.record_count == 2U && result.records[0].stage != BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED && result.records[1].stage != BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED,
                "both failed attempts retain diagnostic records");
    expect_true(result.records[1].is_fallback, "second failed attempt is identified as fallback");
    expect_true(world_is_cleared(&world), "hard failure does not expose invalid world data");

    if (failures != 0U) {
        fprintf(stderr, "%u invalid-fallback test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock invalid-fallback test passed");
    return 0;
}
