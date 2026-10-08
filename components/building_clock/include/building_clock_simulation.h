#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "building_clock_model.h"
#include "building_clock_random.h"
#include "building_clock_runtime_navigation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUILDING_CLOCK_SIMULATION_MAX_STEP_MS 1000U
#define BUILDING_CLOCK_SIMULATION_QUANTUM_MS 50U

typedef enum {
    BUILDING_CLOCK_SIMULATION_OK = 0,
    BUILDING_CLOCK_SIMULATION_DELTA_CLAMPED,
    BUILDING_CLOCK_SIMULATION_INVALID_ARGUMENT,
    BUILDING_CLOCK_SIMULATION_INVALID_WORLD,
    BUILDING_CLOCK_SIMULATION_INVALID_STATE,
    BUILDING_CLOCK_SIMULATION_CAPACITY_EXCEEDED,
    BUILDING_CLOCK_SIMULATION_UNSUPPORTED_JOB,
} building_clock_simulation_status_t;

typedef struct {
    building_clock_job_kind_t kind;
    uint8_t priority;
    uint8_t target_node_id;
    uint16_t required_worker_capabilities;
    uint32_t action_duration_ms;
    uint64_t earliest_start_elapsed_ms;
    uint64_t desired_completion_elapsed_ms;
} building_clock_simple_job_spec_t;

typedef struct {
    uint32_t jobs_started;
    uint32_t jobs_completed;
    uint32_t jobs_cancelled;
    uint32_t jobs_recycled;
    uint32_t route_failures;
    uint32_t wait_events;
    uint32_t reservation_conflicts;
    uint32_t edge_reservations_acquired;
    uint32_t node_reservations_acquired;
    uint8_t peak_active_workers;
} building_clock_simulation_metrics_t;

typedef struct {
    building_clock_simulation_state_t state;
    building_clock_navigation_runtime_t navigation;
    building_clock_random_t worker_random;
    building_clock_simulation_metrics_t metrics;
} building_clock_simulation_t;

typedef struct {
    building_clock_simulation_status_t status;
    uint32_t requested_ms;
    uint32_t processed_ms;
} building_clock_simulation_step_result_t;

building_clock_simulation_status_t building_clock_simulation_init(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t worker_count);
building_clock_simulation_status_t building_clock_simulation_add_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, const building_clock_simple_job_spec_t* spec,
                                                                     uint8_t* job_id);
building_clock_simulation_status_t building_clock_simulation_cancel_job(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint8_t job_id);
building_clock_simulation_status_t building_clock_simulation_recycle_job(building_clock_simulation_t* simulation, uint8_t job_id);
building_clock_simulation_step_result_t building_clock_simulation_step(building_clock_simulation_t* simulation, const building_clock_world_t* world, uint32_t elapsed_ms);
const char* building_clock_simulation_status_name(building_clock_simulation_status_t status);

#ifdef __cplusplus
}
#endif
