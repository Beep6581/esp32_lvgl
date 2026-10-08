#include "building_clock_world_selection.h"

#include <stddef.h>
#include <string.h>

#include "building_clock_generator.h"

_Static_assert(BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS > 0U, "world selection requires at least one normal attempt");
_Static_assert(BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS <= UINT16_MAX, "normal attempt count must fit uint16_t");
_Static_assert(BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT <= UINT16_MAX, "fallback attempt must fit uint16_t");

static building_clock_world_selection_result_t initial_result(uint64_t root_seed, uint32_t generator_version) {
    building_clock_world_selection_result_t result = {
        .status = BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE,
        .requested_root_seed = root_seed,
        .requested_generator_version = generator_version,
        .accepted_attempt = BUILDING_CLOCK_WORLD_NO_ACCEPTED_ATTEMPT,
        .last_failed_stage = BUILDING_CLOCK_WORLD_ATTEMPT_NOT_RUN,
    };
    return result;
}

static building_clock_world_attempt_record_t* begin_attempt(building_clock_world_selection_result_t* result, uint64_t root_seed, uint32_t generator_version, uint16_t attempt, bool is_fallback) {
    if (result->record_count >= BUILDING_CLOCK_WORLD_ATTEMPT_RECORD_CAPACITY) {
        return NULL;
    }

    building_clock_world_attempt_record_t* record = &result->records[result->record_count];
    result->record_count++;
    result->pipeline_attempt_count++;
    *record = (building_clock_world_attempt_record_t){
        .root_seed = root_seed,
        .generator_version = generator_version,
        .attempt = attempt,
        .is_fallback = is_fallback,
        .stage = BUILDING_CLOCK_WORLD_ATTEMPT_NOT_RUN,
        .failure_item_index = BUILDING_CLOCK_VALIDATION_NO_ITEM,
    };
    return record;
}

static bool run_attempt(building_clock_world_t* world, building_clock_world_selection_result_t* result, uint64_t root_seed, uint32_t generator_version, uint16_t attempt, bool is_fallback) {
    building_clock_world_attempt_record_t* record = begin_attempt(result, root_seed, generator_version, attempt, is_fallback);
    if (record == NULL) {
        return false;
    }

    building_clock_generate_candidate(world, root_seed, generator_version, attempt);

    const building_clock_prevalidation_result_t preliminary = building_clock_prevalidate_candidate(world);
    record->prevalidation_reason = preliminary.reason;
    if (preliminary.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
        record->stage = BUILDING_CLOCK_WORLD_ATTEMPT_PREVALIDATION_FAILED;
        record->failure_item_index = preliminary.item_index;
        result->last_failed_stage = record->stage;
        result->last_prevalidation_reason = preliminary.reason;
        return false;
    }

    const building_clock_nav_derivation_result_t navigation = building_clock_derive_navigation(world);
    record->navigation_reason = navigation.reason;
    if (navigation.reason != BUILDING_CLOCK_NAV_DERIVATION_OK) {
        record->stage = BUILDING_CLOCK_WORLD_ATTEMPT_NAVIGATION_FAILED;
        record->failure_item_index = navigation.item_index;
        result->last_failed_stage = record->stage;
        result->last_navigation_reason = navigation.reason;
        return false;
    }

    const building_clock_complete_validation_result_t complete = building_clock_validate_complete(world);
    record->complete_validation_reason = complete.reason;
    if (complete.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
        record->stage = BUILDING_CLOCK_WORLD_ATTEMPT_COMPLETE_VALIDATION_FAILED;
        record->failure_item_index = complete.item_index;
        result->last_failed_stage = record->stage;
        result->last_complete_validation_reason = complete.reason;
        return false;
    }

    record->stage = BUILDING_CLOCK_WORLD_ATTEMPT_ACCEPTED;
    result->accepted_attempt = attempt;
    return true;
}

building_clock_world_selection_result_t building_clock_select_world(building_clock_world_t* world, uint64_t root_seed, uint32_t generator_version) {
    building_clock_world_selection_result_t result = initial_result(root_seed, generator_version);
    if (world == NULL) {
        result.status = BUILDING_CLOCK_WORLD_SELECTION_INVALID_ARGUMENT;
        return result;
    }

    for (uint16_t attempt = 0U; attempt < BUILDING_CLOCK_WORLD_MAX_CANDIDATE_ATTEMPTS; attempt++) {
        result.normal_attempt_count++;
        if (run_attempt(world, &result, root_seed, generator_version, attempt, false)) {
            result.status = BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED;
            return result;
        }
    }

    result.fallback_used = true;
    if (run_attempt(world, &result, BUILDING_CLOCK_WORLD_FALLBACK_ROOT_SEED, BUILDING_CLOCK_WORLD_FALLBACK_GENERATOR_VERSION, BUILDING_CLOCK_WORLD_FALLBACK_ATTEMPT, true)) {
        result.status = BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK;
        return result;
    }

    memset(world, 0, sizeof(*world));
    result.status = BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE;
    result.accepted_attempt = BUILDING_CLOCK_WORLD_NO_ACCEPTED_ATTEMPT;
    return result;
}

const char* building_clock_world_selection_status_name(building_clock_world_selection_status_t status) {
    switch (status) {
    case BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED:
        return "accepted";
    case BUILDING_CLOCK_WORLD_SELECTION_ACCEPTED_FALLBACK:
        return "accepted fallback";
    case BUILDING_CLOCK_WORLD_SELECTION_HARD_FAILURE:
        return "hard failure";
    case BUILDING_CLOCK_WORLD_SELECTION_INVALID_ARGUMENT:
        return "invalid argument";
    default:
        return "unknown";
    }
}
