#include "building_clock_random.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_ROOT_SEED UINT64_C(0x0123456789abcdef)
#define KNOWN_SEQUENCE_LENGTH 6U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static void expect_u32(uint32_t actual, uint32_t expected, const char* description) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got 0x%08" PRIx32 ", expected 0x%08" PRIx32 "\n", description, actual, expected);
        failures++;
    }
}

static void expect_u64(uint64_t actual, uint64_t expected, const char* description) {
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s: got 0x%016" PRIx64 ", expected 0x%016" PRIx64 "\n", description, actual, expected);
        failures++;
    }
}

static void test_known_seed_derivation(void) {
    static const uint64_t expected[] = {
        UINT64_C(0x157a3807a48faa9d),
        UINT64_C(0xd573529b34a1d093),
        UINT64_C(0x2f90b72e996dccbe),
        UINT64_C(0xa2d419334c4667ec),
    };

    for (uint32_t stream_id = BUILDING_CLOCK_RANDOM_WORLD_LAYOUT; stream_id <= BUILDING_CLOCK_RANDOM_COSMETIC; stream_id++) {
        expect_u64(building_clock_random_derive_seed(TEST_ROOT_SEED, (building_clock_random_stream_id_t)stream_id), expected[stream_id - 1U], "derived seed vector");
    }
}

static void test_known_raw_sequence(void) {
    static const uint32_t expected[KNOWN_SEQUENCE_LENGTH] = {
        UINT32_C(0xb5f1fb99), UINT32_C(0xcb93bee2), UINT32_C(0x490c61ea), UINT32_C(0x58fd3b1f), UINT32_C(0xe40d89c9), UINT32_C(0x3ee64674),
    };
    building_clock_random_t random;
    building_clock_random_init(&random, TEST_ROOT_SEED);

    for (uint32_t index = 0; index < KNOWN_SEQUENCE_LENGTH; index++) {
        expect_u32(building_clock_random_next(&random), expected[index], "raw PCG32 vector");
    }
}

static void test_identical_seed(void) {
    building_clock_random_t first;
    building_clock_random_t second;
    building_clock_random_init(&first, UINT64_C(0xfedcba9876543210));
    building_clock_random_init(&second, UINT64_C(0xfedcba9876543210));

    for (unsigned int index = 0; index < 100U; index++) {
        expect_u32(building_clock_random_next(&first), building_clock_random_next(&second), "identical seed sequence");
    }
}

static void test_stream_independence(void) {
    building_clock_random_streams_t streams;
    building_clock_random_streams_t reference;
    building_clock_random_init_streams(&streams, TEST_ROOT_SEED);
    building_clock_random_init_streams(&reference, TEST_ROOT_SEED);

    for (unsigned int index = 0; index < 100U; index++) {
        (void)building_clock_random_next(&streams.world_layout);
    }

    for (unsigned int index = 0; index < 20U; index++) {
        expect_u32(building_clock_random_next(&streams.worker_behavior), building_clock_random_next(&reference.worker_behavior), "independent worker stream");
    }

    const uint32_t layout_value = building_clock_random_next(&reference.world_layout);
    const uint32_t schedule_value = building_clock_random_next(&reference.construction_scheduling);
    const uint32_t worker_value = building_clock_random_next(&reference.worker_behavior);
    const uint32_t cosmetic_value = building_clock_random_next(&reference.cosmetic);
    expect_true(layout_value != schedule_value && layout_value != worker_value && layout_value != cosmetic_value && schedule_value != worker_value && schedule_value != cosmetic_value &&
                    worker_value != cosmetic_value,
                "derived streams begin with distinct values");
}

static void test_bounded_values(void) {
    building_clock_random_t random;
    building_clock_random_init(&random, UINT64_C(0x55aa55aa55aa55aa));

    for (unsigned int index = 0; index < 10000U; index++) {
        expect_true(building_clock_random_bounded(&random, 37U) < 37U, "bounded value is below upper bound");
    }

    for (unsigned int index = 0; index < 10U; index++) {
        expect_u32(building_clock_random_bounded(&random, 1U), 0U, "bound one returns zero");
    }
    expect_u32(building_clock_random_bounded(&random, 0U), 0U, "invalid zero bound returns zero");
}

static void test_range_values(void) {
    building_clock_random_t random;
    building_clock_random_init(&random, UINT64_C(0xa5a5a5a5a5a5a5a5));

    for (unsigned int index = 0; index < 10000U; index++) {
        const uint32_t value = building_clock_random_range(&random, 10U, 23U);
        expect_true(value >= 10U && value < 23U, "range value is inside [minimum, maximum)");
    }

    expect_u32(building_clock_random_range(&random, 7U, 8U), 7U, "single-value range returns its minimum");
    expect_u32(building_clock_random_range(&random, 9U, 9U), 9U, "empty range returns its minimum");
    expect_u32(building_clock_random_range(&random, 12U, 4U), 12U, "reversed range returns its minimum");
}

int main(void) {
    test_known_seed_derivation();
    test_known_raw_sequence();
    test_identical_seed();
    test_stream_independence();
    test_bounded_values();
    test_range_values();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock random test(s) failed\n", failures);
        return 1;
    }

    puts("Building Clock random tests passed");
    return 0;
}
