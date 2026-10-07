#pragma once

#include <stdint.h>

#include "building_clock_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUILDING_CLOCK_NAV_DERIVATION_NO_ITEM UINT16_MAX

typedef enum {
    BUILDING_CLOCK_NAV_DERIVATION_OK = 0,
    BUILDING_CLOCK_NAV_DERIVATION_NULL_WORLD,
    BUILDING_CLOCK_NAV_DERIVATION_NODE_CAPACITY,
    BUILDING_CLOCK_NAV_DERIVATION_EDGE_CAPACITY,
    BUILDING_CLOCK_NAV_DERIVATION_NO_WALKABLE_SURFACE,
    BUILDING_CLOCK_NAV_DERIVATION_MISSING_SOURCE_NODE,
    BUILDING_CLOCK_NAV_DERIVATION_TEMP_CONNECTION_CAPACITY,
} building_clock_nav_derivation_reason_t;

typedef struct {
    building_clock_nav_derivation_reason_t reason;
    building_clock_nav_source_kind_t source_kind;
    uint16_t item_index;
} building_clock_nav_derivation_result_t;

/*
 * Populates only navigation and derived node-reference fields. The input must
 * already have passed building_clock_prevalidate_candidate().
 */
building_clock_nav_derivation_result_t building_clock_derive_navigation(building_clock_world_t* world);
const char* building_clock_nav_derivation_reason_name(building_clock_nav_derivation_reason_t reason);

#ifdef __cplusplus
}
#endif
