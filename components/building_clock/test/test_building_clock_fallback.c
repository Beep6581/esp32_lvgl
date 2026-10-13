#include "building_clock_generator.h"
#include "building_clock_random.h"
#include "building_clock_world_selection.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FALLBACK_TRIGGER_ROOT UINT64_C(0x37a3c00e69d91259)

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static bool run_complete_pipeline(building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version, uint16_t attempt) {
    building_clock_generate_candidate(world, root_seed, generator_version, attempt);
    if (building_clock_prevalidate_candidate(world).reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        return false;
    }
    if (building_clock_derive_navigation(world).reason != BUILDING_CLOCK_NAV_DERIVATION_OK) {
        return false;
    }
    return building_clock_validate_complete(world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_OK;
}

int main(void) {
    building_clock_world_t world;
    building_clock_world_t repeated;
    building_clock_world_t direct_fallback;
    const building_clock_world_selection_result_t result = building_clock_select_world(&world, FALLBACK_TRIGGER_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);
    const building_clock_world_selection_result_t repeated_result = building_clock_select_world(&repeated, FALLBACK_TRIGGER_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);

    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "failed normal candidate selects fallback");
    expect_true(result.fallback_used && result.normal_attempt_count == 1U && result.pipeline_attempt_count == 2U, "fallback follows the configured normal attempt");
    expect_true(result.record_count == 2U && !result.records[0].is_fallback && result.records[0].stage != BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED, "failed normal attempt is retained");
    expect_true(result.records[1].is_fallback && result.records[1].stage == BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED, "fallback acceptance is recorded explicitly");
    expect_true(repeated_result.status == result.status && memcmp(&world, &repeated, sizeof(world)) == 0, "fallback world is byte-reproducible");
    expect_true(run_complete_pipeline(&direct_fallback, BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED, BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION, BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT),
                "fallback candidate passes the unchanged complete pipeline");
    expect_true(memcmp(&world, &direct_fallback, sizeof(world)) == 0, "selected fallback equals direct grammar output");
    expect_true(world.replay.root_seed == BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED && world.replay.generator_version == BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION &&
                    world.replay.generation_attempt == BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT &&
                    world.replay.candidate_seed ==
                        building_clock_candidate_seed(BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED, BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION, BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT),
                "fallback replay metadata identifies the actual candidate");

    if (failures != 0U) {
        fprintf(stderr, "%u fallback test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock fallback test passed");
    return 0;
}
