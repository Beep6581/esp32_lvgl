#pragma once

#include "building_clock_simulation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUILDING_CLOCK_SCENE_OK = 0,
    BUILDING_CLOCK_SCENE_INVALID_ARGUMENT,
    BUILDING_CLOCK_SCENE_INVALID_WORLD,
    BUILDING_CLOCK_SCENE_INVALID_STATE,
} building_clock_scene_status_t;

/*
 * The immutable world remains owned by the caller. Mutable semantic state is
 * copied so a renderer can inspect one coherent scene without owning or
 * changing simulation data.
 */
typedef struct {
    const building_clock_world_t* world;
    building_clock_simulation_state_t state;
    building_clock_navigation_runtime_t navigation;
    building_clock_simulation_metrics_t metrics;
} building_clock_scene_snapshot_t;

building_clock_scene_status_t building_clock_scene_capture(const building_clock_world_t* world, const building_clock_simulation_t* simulation, building_clock_scene_snapshot_t* snapshot);
const char* building_clock_scene_status_name(building_clock_scene_status_t status);

#ifdef __cplusplus
}
#endif
