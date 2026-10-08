#include "building_clock_generator.h"
#include "building_clock_random.h"
#include "building_clock_world_selection.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BATCH_SIZE 4096U

#define ATTEMPT_ZERO_ROOT UINT64_C(0x123456789abcdef0)
#define RETRY_ROOT UINT64_C(0x243f6a8885a308d3)
#define FALLBACK_ROOT UINT64_C(0xa07b7c116ab173f1)

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

static void check_replay_metadata(const building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version, uint16_t attempt) {
    expect_true(world->replay.root_seed == root_seed, "accepted world records its actual root seed");
    expect_true(world->replay.generator_version == generator_version, "accepted world records its actual generator version");
    expect_true(world->replay.generation_attempt == attempt, "accepted world records its accepted attempt");
    expect_true(world->replay.world_layout_seed == building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORLD_LAYOUT), "accepted world records world-layout seed");
    expect_true(world->replay.candidate_seed == building_clock_candidate_seed(root_seed, generator_version, attempt), "accepted world records candidate seed");
    expect_true(world->replay.construction_scheduling_seed == building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_CONSTRUCTION_SCHEDULING), "accepted world records construction seed");
    expect_true(world->replay.worker_behavior_seed == building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORKER_BEHAVIOR), "accepted world records worker seed");
    expect_true(world->replay.cosmetic_seed == building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_COSMETIC), "accepted world records cosmetic seed");
}

static void test_attempt_zero_acceptance(void) {
    building_clock_world_t world;
    const building_clock_world_selection_result_t result = building_clock_select_world(&world, ATTEMPT_ZERO_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);

    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED, "known root accepts normally");
    expect_true(result.accepted_attempt == 0U, "known root accepts on attempt zero");
    expect_true(result.normal_attempt_count == 1U && result.pipeline_attempt_count == 1U, "attempt-zero success runs one pipeline");
    expect_true(result.record_count == 1U && result.records[0].stage == BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED, "attempt-zero success is recorded");
    expect_true(!result.fallback_used, "attempt-zero success does not use fallback");
    check_replay_metadata(&world, ATTEMPT_ZERO_ROOT, BUILDING_CLOCK_GENERATOR_VERSION, 0U);
}

static void test_retry_and_reproduction(void) {
    building_clock_world_t first;
    building_clock_world_t second;
    building_clock_world_t direct;
    const building_clock_world_selection_result_t first_result = building_clock_select_world(&first, RETRY_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);
    const building_clock_world_selection_result_t second_result = building_clock_select_world(&second, RETRY_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);

    expect_true(first_result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED, "retry fixture accepts normally");
    expect_true(first_result.accepted_attempt == 1U, "retry fixture accepts on attempt one");
    expect_true(first_result.normal_attempt_count == 2U && first_result.pipeline_attempt_count == 2U, "retry fixture runs two pipelines");
    expect_true(first_result.record_count == 2U && first_result.records[0].stage != BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED && first_result.records[1].stage == BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED,
                "failed and accepted retry attempts are recorded separately");
    expect_true(first_result.records[0].root_seed == RETRY_ROOT && first_result.records[0].attempt == 0U && first_result.records[1].root_seed == RETRY_ROOT && first_result.records[1].attempt == 1U,
                "attempt records preserve deterministic inputs");
    expect_true(second_result.status == first_result.status && second_result.accepted_attempt == first_result.accepted_attempt &&
                    second_result.normal_attempt_count == first_result.normal_attempt_count && second_result.pipeline_attempt_count == first_result.pipeline_attempt_count,
                "repeated selection has identical result summary");
    expect_true(memcmp(&first, &second, sizeof(first)) == 0, "repeated selection produces a byte-equivalent world");

    expect_true(run_complete_pipeline(&direct, RETRY_ROOT, BUILDING_CLOCK_GENERATOR_VERSION, 1U), "accepted retry attempt independently passes the pipeline");
    expect_true(memcmp(&first, &direct, sizeof(first)) == 0, "failed attempt does not perturb the independently derived later attempt");
    check_replay_metadata(&first, RETRY_ROOT, BUILDING_CLOCK_GENERATOR_VERSION, 1U);
}

static void test_fallback(void) {
    building_clock_world_t world;
    building_clock_world_t repeated;
    building_clock_world_t direct_fallback;
    const building_clock_world_selection_result_t result = building_clock_select_world(&world, FALLBACK_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);
    const building_clock_world_selection_result_t repeated_result = building_clock_select_world(&repeated, FALLBACK_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);

    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK, "fallback fixture uses fallback");
    expect_true(result.fallback_used, "fallback use is reported");
    expect_true(result.normal_attempt_count == BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS, "fallback follows all configured normal attempts");
    expect_true(result.pipeline_attempt_count == BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS + 1U, "fallback runs through the normal pipeline");
    expect_true(result.record_count == BUILDING_CLOCK_WORLD_ATTEMPT_RECORD_CAPACITY, "fallback records normal and fallback attempts");
    for (uint16_t attempt = 0U; attempt < BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS; attempt++) {
        expect_true(result.records[attempt].stage != BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED && !result.records[attempt].is_fallback, "normal attempt fails before fallback");
    }
    const building_clock_world_attempt_record_t* fallback_record = &result.records[result.record_count - 1U];
    expect_true(fallback_record->is_fallback && fallback_record->stage == BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED, "fallback acceptance is recorded explicitly");
    expect_true(fallback_record->root_seed == BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED && fallback_record->generator_version == BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION &&
                    fallback_record->attempt == BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT,
                "fallback record identifies the documented deterministic candidate");
    expect_true(repeated_result.status == result.status && repeated_result.accepted_attempt == result.accepted_attempt, "fallback choice is deterministic");
    expect_true(memcmp(&world, &repeated, sizeof(world)) == 0, "fallback world is byte-reproducible");

    expect_true(run_complete_pipeline(&direct_fallback, BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED, BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION, BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT),
                "fallback candidate passes the complete normal pipeline");
    expect_true(memcmp(&world, &direct_fallback, sizeof(world)) == 0, "selected fallback equals direct grammar output");
    check_replay_metadata(&world, BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED, BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION, BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT);
}

static void test_invalid_argument(void) {
    const building_clock_world_selection_result_t result = building_clock_select_world(NULL, ATTEMPT_ZERO_ROOT, BUILDING_CLOCK_GENERATOR_VERSION);
    expect_true(result.status == BUILDING_CLOCK_WORLD_SELECTION_INVALID_ARGUMENT, "null output world returns invalid argument");
    expect_true(result.pipeline_attempt_count == 0U && result.record_count == 0U, "invalid argument runs no candidate pipeline");
}

static void test_acceptance_batch(void) {
    uint32_t accepted_attempts[BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS] = {0};
    uint32_t fallback_count = 0U;
    uint32_t hard_failures = 0U;
    uint16_t maximum_pipeline_attempts = 0U;
    uint8_t maximum_nodes = 0U;
    uint16_t maximum_edges = 0U;

    for (uint32_t index = 0U; index < BATCH_SIZE; index++) {
        const uint64_t root_seed = UINT64_C(0xbb67ae8584caa73b) + UINT64_C(0x9e3779b97f4a7c15) * index;
        building_clock_world_t world;
        const building_clock_world_selection_result_t result = building_clock_select_world(&world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION);

        if (result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED) {
            expect_true(result.accepted_attempt < BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS, "normal accepted attempt is in range");
            if (result.accepted_attempt < BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS) {
                accepted_attempts[result.accepted_attempt]++;
            }
        } else if (result.status == BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK) {
            fallback_count++;
        } else {
            hard_failures++;
            continue;
        }

        expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_OK, "batch selector returns a completely valid world");
        if (result.pipeline_attempt_count > maximum_pipeline_attempts) {
            maximum_pipeline_attempts = result.pipeline_attempt_count;
        }
        if (world.navigation.node_count > maximum_nodes) {
            maximum_nodes = world.navigation.node_count;
        }
        if (world.navigation.edge_count > maximum_edges) {
            maximum_edges = world.navigation.edge_count;
        }
    }

    expect_true(accepted_attempts[0] > 0U, "batch includes attempt-zero acceptance");
    expect_true(accepted_attempts[1] + accepted_attempts[2] + accepted_attempts[3] > 0U, "batch includes successful retries");
    expect_true(fallback_count > 0U, "batch exercises fallback selection");
    expect_true(hard_failures == 0U, "batch has no hard failures");

    printf("World selection batch: %u roots\n", BATCH_SIZE);
    for (uint16_t attempt = 0U; attempt < BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS; attempt++) {
        printf("  accepted attempt %u: %" PRIu32 "\n", attempt, accepted_attempts[attempt]);
    }
    printf("  fallback: %" PRIu32 "\n", fallback_count);
    printf("  hard failures: %" PRIu32 "\n", hard_failures);
    printf("  maximum pipeline attempts before success: %u\n", maximum_pipeline_attempts);
    printf("  maximum accepted navigation: %u nodes, %u edges\n", maximum_nodes, maximum_edges);
}

int main(void) {
    test_attempt_zero_acceptance();
    test_retry_and_reproduction();
    test_fallback();
    test_invalid_argument();
    test_acceptance_batch();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock world-selection test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock world-selection tests passed");
    return 0;
}
