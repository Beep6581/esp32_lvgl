#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These numeric identifiers are part of deterministic replay behavior. */
typedef enum {
    BUILDING_CLOCK_RANDOM_WORLD_LAYOUT = 1,
    BUILDING_CLOCK_RANDOM_CONSTRUCTION_SCHEDULING = 2,
    BUILDING_CLOCK_RANDOM_WORKER_BEHAVIOR = 3,
    BUILDING_CLOCK_RANDOM_COSMETIC = 4,
} building_clock_random_stream_id_t;

/* PCG32 state. Call building_clock_random_init() before use. */
typedef struct {
    uint64_t state;
    uint64_t increment;
} building_clock_random_t;

typedef struct {
    building_clock_random_t world_layout;
    building_clock_random_t construction_scheduling;
    building_clock_random_t worker_behavior;
    building_clock_random_t cosmetic;
} building_clock_random_streams_t;

uint64_t building_clock_random_derive_seed(uint64_t root_seed, building_clock_random_stream_id_t stream_id);
void building_clock_random_init(building_clock_random_t* random, uint64_t seed);
void building_clock_random_init_streams(building_clock_random_streams_t* streams, uint64_t root_seed);
uint32_t building_clock_random_next(building_clock_random_t* random);

/* Uses rejection sampling for [0, exclusive_upper_bound). A zero bound returns zero. */
uint32_t building_clock_random_bounded(building_clock_random_t* random, uint32_t exclusive_upper_bound);

/*
 * Returns a value in [minimum_inclusive, maximum_exclusive). An empty or
 * reversed range returns minimum_inclusive without consuming a random value.
 */
uint32_t building_clock_random_range(building_clock_random_t* random, uint32_t minimum_inclusive, uint32_t maximum_exclusive);

#ifdef __cplusplus
}
#endif
