#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUILDING_CLOCK_SEED_AUTOMATIC = 0,
    BUILDING_CLOCK_SEED_FIXED,
} building_clock_seed_mode_t;

typedef struct {
    building_clock_seed_mode_t mode;
    uint64_t fixed_root_seed;
} building_clock_seed_config_t;

/* Resolve once at boot. Fixed mode performs no hardware entropy reads. */
uint64_t building_clock_seed_resolve_root(const building_clock_seed_config_t* config);

#ifdef __cplusplus
}
#endif
