#include "building_clock_seed.h"

#include <stddef.h>

#include "esp_random.h"

uint64_t building_clock_seed_resolve_root(const building_clock_seed_config_t* config) {
    if (config != NULL && config->mode == BUILDING_CLOCK_SEED_FIXED) {
        return config->fixed_root_seed;
    }

    uint64_t root_seed;
    esp_fill_random(&root_seed, sizeof(root_seed));
    return root_seed;
}
