#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "building_clock_model.h"
#include "building_clock_navigation.h"
#include "building_clock_validation.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS
#define BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS 4U
#endif

#ifndef BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED
#define BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED UINT64_C(0xbb67ae8584caa73b)
#endif

#ifndef BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION
#define BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION 1U
#endif

#ifndef BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT
#define BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT 0U
#endif

#define BUILDING_CLOCK_WORLD_ATTEMPT_RECORD_CAPACITY (BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS + 1U)
#define BUILDING_CLOCK_WORLD_NO_ACCEPTED_ATTEMPT UINT16_MAX

typedef enum {
    BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED = 0,
    BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK,
    BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE,
    BUILDING_CLOCK_WORLD_SELECTION_INVALID_ARGUMENT,
} building_clock_world_selection_status_t;

typedef enum {
    BUILDING_CLOCK_WORLD_ATTEMPT_NOT_RUN = 0,
    BUILDING_CLOCK_WORLD_ATTEMPT_PREVALIDATION_FAILED,
    BUILDING_CLOCK_WORLD_ATTEMPT_NAVIGATION_FAILED,
    BUILDING_CLOCK_WORLD_ATTEMPT_COMPLETE_VALIDATION_FAILED,
    BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED,
} building_clock_world_attempt_stage_t;

typedef struct {
    uint64_t root_seed;
    uint32_t generator_version;
    uint16_t attempt;
    bool is_fallback;
    building_clock_world_attempt_stage_t stage;
    building_clock_prevalidation_reason_t prevalidation_reason;
    building_clock_nav_derivation_reason_t navigation_reason;
    building_clock_complete_validation_reason_t complete_validation_reason;
    uint16_t failure_item_index;
} building_clock_world_attempt_record_t;

typedef struct {
    building_clock_world_selection_status_t status;
    uint64_t requested_root_seed;
    uint32_t requested_generator_version;
    uint16_t accepted_attempt;
    uint16_t normal_attempt_count;
    uint16_t pipeline_attempt_count;
    uint16_t record_count;
    bool fallback_used;
    building_clock_world_attempt_stage_t last_failed_stage;
    building_clock_prevalidation_reason_t last_prevalidation_reason;
    building_clock_nav_derivation_reason_t last_navigation_reason;
    building_clock_complete_validation_reason_t last_complete_validation_reason;
    building_clock_world_attempt_record_t records[BUILDING_CLOCK_WORLD_ATTEMPT_RECORD_CAPACITY];
} building_clock_world_selection_result_t;

/*
 * Runs deterministic candidates through the complete generation pipeline.
 * On success, world contains the accepted immutable world and derived graph.
 * On hard failure, world is cleared so invalid candidate data cannot escape.
 */
building_clock_world_selection_result_t building_clock_select_world(building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version);
const char* building_clock_world_selection_status_name(building_clock_world_selection_status_t status);

#ifdef __cplusplus
}
#endif
