#pragma once

#include <stdint.h>

#include "building_clock_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUILDING_CLOCK_GENERATOR_VERSION 1U

uint64_t building_clock_candidate_seed(uint64_t root_seed, uint32_t generator_version, uint16_t attempt);

/* Produces geometry only. The navigation graph is left empty. */
void building_clock_generate_candidate(building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version, uint16_t attempt);

#ifdef __cplusplus
}
#endif
