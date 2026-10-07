#include "building_clock_random.h"

#define PCG32_MULTIPLIER UINT64_C(6364136223846793005)
#define PCG32_STATE_SALT UINT64_C(0x243f6a8885a308d3)
#define PCG32_SEQUENCE_SALT UINT64_C(0x13198a2e03707344)
#define SPLITMIX64_INCREMENT UINT64_C(0x9e3779b97f4a7c15)

static uint64_t mix_uint64(uint64_t value) {
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

uint64_t building_clock_random_derive_seed(uint64_t root_seed, building_clock_random_stream_id_t stream_id) {
    return mix_uint64(root_seed + SPLITMIX64_INCREMENT * (uint64_t)stream_id);
}

uint32_t building_clock_random_next(building_clock_random_t* random) {
    const uint64_t old_state = random->state;
    random->state = old_state * PCG32_MULTIPLIER + random->increment;

    const uint32_t xorshifted = (uint32_t)(((old_state >> 18U) ^ old_state) >> 27U);
    const uint32_t rotation = (uint32_t)(old_state >> 59U);
    return (xorshifted >> rotation) | (xorshifted << ((32U - rotation) & 31U));
}

void building_clock_random_init(building_clock_random_t* random, uint64_t seed) {
    const uint64_t initial_state = mix_uint64(seed + PCG32_STATE_SALT);
    const uint64_t sequence = mix_uint64(seed + PCG32_SEQUENCE_SALT);

    random->state = 0U;
    random->increment = (sequence << 1U) | 1U;
    (void)building_clock_random_next(random);
    random->state += initial_state;
    (void)building_clock_random_next(random);
}

void building_clock_random_init_streams(building_clock_random_streams_t* streams, uint64_t root_seed) {
    building_clock_random_init(&streams->world_layout, building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORLD_LAYOUT));
    building_clock_random_init(&streams->construction_scheduling, building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_CONSTRUCTION_SCHEDULING));
    building_clock_random_init(&streams->worker_behavior, building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_WORKER_BEHAVIOR));
    building_clock_random_init(&streams->cosmetic, building_clock_random_derive_seed(root_seed, BUILDING_CLOCK_RANDOM_COSMETIC));
}

uint32_t building_clock_random_bounded(building_clock_random_t* random, uint32_t exclusive_upper_bound) {
    if (exclusive_upper_bound == 0U) {
        return 0U;
    }

    const uint32_t threshold = (uint32_t)(0U - exclusive_upper_bound) % exclusive_upper_bound;
    uint32_t value;
    do {
        value = building_clock_random_next(random);
    } while (value < threshold);

    return value % exclusive_upper_bound;
}

uint32_t building_clock_random_range(building_clock_random_t* random, uint32_t minimum_inclusive, uint32_t maximum_exclusive) {
    if (maximum_exclusive <= minimum_inclusive) {
        return minimum_inclusive;
    }

    return minimum_inclusive + building_clock_random_bounded(random, maximum_exclusive - minimum_inclusive);
}
