#pragma once

#include "building_clock_simulation.h"

/* Simulation helpers for local excursions on real broad platforms. Important
 * work interrupts these before routing; workers walk to an existing nearby
 * graph anchor. Calls use a valid simulation/world and stable worker slot. */
void building_clock_upkeep_init(building_clock_simulation_t* simulation, const building_clock_world_t* world);
void building_clock_upkeep_tick(building_clock_simulation_t* simulation, uint32_t elapsed_ms);
bool building_clock_upkeep_start(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id);
bool building_clock_upkeep_advance(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id, uint32_t elapsed_ms);
void building_clock_upkeep_interrupt(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_id);
