#include "building_clock_generator.h"
#include "building_clock_validation.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BATCH_SIZE 2048U
#define SIGNATURE_CAPACITY 256U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static bool rect_in_world(building_clock_rect_t rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0U && rect.height > 0U && (int32_t)rect.x + rect.width <= BUILDING_CLOCK_WORLD_WIDTH &&
           (int32_t)rect.y + rect.height <= BUILDING_CLOCK_WORLD_HEIGHT;
}

static bool point_in_world(building_clock_point_t point) {
    return point.x >= 0 && point.x < BUILDING_CLOCK_WORLD_WIDTH && point.y >= 0 && point.y < BUILDING_CLOCK_WORLD_HEIGHT;
}

static bool rects_overlap(building_clock_rect_t first, building_clock_rect_t second) {
    return first.x < (int32_t)second.x + second.width && second.x < (int32_t)first.x + first.width && first.y < (int32_t)second.y + second.height && second.y < (int32_t)first.y + first.height;
}

static bool horizontal_hits_rect(const building_clock_platform_t* platform, building_clock_rect_t rect) {
    return platform->surface_y >= rect.y && platform->surface_y < (int32_t)rect.y + rect.height && platform->x_start < (int32_t)rect.x + rect.width && platform->x_end >= rect.x;
}

static bool vertical_hits_rect(const building_clock_ladder_t* ladder, building_clock_rect_t rect) {
    return ladder->x >= rect.x && ladder->x < (int32_t)rect.x + rect.width && ladder->top_y < (int32_t)rect.y + rect.height && ladder->bottom_y >= rect.y;
}

static uint64_t signature_add(uint64_t signature, uint32_t value) {
    signature ^= value;
    return signature * UINT64_C(1099511628211);
}

static uint64_t geometry_signature(const building_clock_world_t* world) {
    uint64_t signature = UINT64_C(1469598103934665603);
    signature = signature_add(signature, (uint32_t)world->topology_family);
    signature = signature_add(signature, world->platform_count);
    for (uint8_t index = 0; index < world->platform_count; index++) {
        signature = signature_add(signature, (uint16_t)world->platforms[index].x_start);
        signature = signature_add(signature, (uint16_t)world->platforms[index].x_end);
        signature = signature_add(signature, (uint16_t)world->platforms[index].surface_y);
    }
    signature = signature_add(signature, (uint16_t)world->cranes[0].base_position.x);
    signature = signature_add(signature, world->lift_count);
    signature = signature_add(signature, (uint16_t)world->depots[0].area.x);
    return signature;
}

static void test_deterministic_candidate(void) {
    building_clock_world_t first;
    building_clock_world_t second;
    building_clock_generate_candidate(&first, UINT64_C(0x123456789abcdef0), BUILDING_CLOCK_GENERATOR_VERSION, 7U);
    building_clock_generate_candidate(&second, UINT64_C(0x123456789abcdef0), BUILDING_CLOCK_GENERATOR_VERSION, 7U);
    expect_true(memcmp(&first, &second, sizeof(first)) == 0, "same root, version, and attempt produce byte-equivalent worlds");

    expect_true(first.replay.root_seed == UINT64_C(0x123456789abcdef0), "candidate records root seed");
    expect_true(first.replay.generator_version == BUILDING_CLOCK_GENERATOR_VERSION, "candidate records generator version");
    expect_true(first.replay.generation_attempt == 7U, "candidate records generation attempt");
    expect_true(first.navigation.node_count == 0U && first.navigation.edge_count == 0U, "candidate navigation remains empty");
}

static void test_attempt_derivation(void) {
    building_clock_world_t first;
    building_clock_world_t second;
    const uint64_t root_seed = UINT64_C(0x0f1e2d3c4b5a6978);
    building_clock_generate_candidate(&first, root_seed, BUILDING_CLOCK_GENERATOR_VERSION, 2U);
    building_clock_generate_candidate(&second, root_seed, BUILDING_CLOCK_GENERATOR_VERSION, 3U);

    expect_true(first.replay.candidate_seed != second.replay.candidate_seed, "different attempts have different candidate seeds");
    expect_true(memcmp(&first, &second, sizeof(first)) != 0, "different attempts produce different candidate worlds");
    expect_true(first.replay.candidate_seed == building_clock_candidate_seed(root_seed, BUILDING_CLOCK_GENERATOR_VERSION, 2U), "recorded candidate seed matches derivation API");
}

static void check_counts(const building_clock_world_t* world) {
    expect_true(world->platform_count <= BUILDING_CLOCK_MAX_PLATFORM_SPANS, "platform count within capacity");
    expect_true(world->ladder_count <= BUILDING_CLOCK_MAX_LADDERS, "ladder count within capacity");
    expect_true(world->major_support_count <= BUILDING_CLOCK_MAX_MAJOR_SUPPORTS, "support count within capacity");
    expect_true(world->crane_count <= BUILDING_CLOCK_MAX_CRANES, "crane count within capacity");
    expect_true(world->lift_count <= BUILDING_CLOCK_MAX_LIFTS, "lift count within capacity");
    expect_true(world->cart_route_count <= BUILDING_CLOCK_MAX_CART_ROUTES, "cart route count within capacity");
    expect_true(world->depot_count <= BUILDING_CLOCK_MAX_MATERIAL_DEPOTS, "depot count within capacity");
    expect_true(world->debris_bay_count <= BUILDING_CLOCK_MAX_DEBRIS_BAYS, "debris bay count within capacity");
    expect_true(world->staging_area_count <= BUILDING_CLOCK_MAX_STAGING_AREAS, "staging count within capacity");
    expect_true(world->worker_spawn_count <= BUILDING_CLOCK_MAX_WORKER_SPAWNS, "spawn count within capacity");
    expect_true(world->temp_attachment_set_count <= BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS, "temporary attachment count within capacity");

    for (uint8_t index = 0; index < world->cart_route_count; index++) {
        expect_true(world->cart_routes[index].stop_count <= BUILDING_CLOCK_MAX_CART_ROUTE_STOPS, "cart stop count within capacity");
    }
    for (uint8_t index = 0; index < world->lift_count; index++) {
        expect_true(world->lifts[index].stop_count <= BUILDING_CLOCK_MAX_LIFT_STOPS, "lift stop count within capacity");
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        expect_true(world->digit_work_areas[digit].access_count <= BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS, "digit access count within capacity");
        expect_true(world->digit_work_areas[digit].safe_retreat_node_count <= BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES, "safe retreat count within capacity");
    }
    for (uint8_t index = 0; index < world->temp_attachment_set_count; index++) {
        expect_true(world->temp_attachment_sets[index].anchor_count <= BUILDING_CLOCK_MAX_ATTACHMENT_ANCHORS, "attachment anchor count within capacity");
        expect_true(world->temp_attachment_sets[index].connection_count <= BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS, "attachment connection count within capacity");
    }
}

static void check_permanent_geometry(const building_clock_world_t* world) {
    expect_true(rect_in_world(world->date_band), "date band inside world");
    expect_true(rect_in_world(world->colon_envelope), "colon envelope inside world");

    for (uint8_t index = 0; index < world->platform_count; index++) {
        const building_clock_platform_t* platform = &world->platforms[index];
        expect_true(platform->x_start >= 0 && platform->x_end < BUILDING_CLOCK_WORLD_WIDTH && platform->x_start <= platform->x_end && platform->surface_y >= 0 &&
                        platform->surface_y < BUILDING_CLOCK_WORLD_HEIGHT,
                    "platform inside world");
        expect_true(!horizontal_hits_rect(platform, world->date_band), "platform avoids date band");
        expect_true(!horizontal_hits_rect(platform, world->colon_envelope), "platform avoids colon envelope");
        for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            expect_true(!horizontal_hits_rect(platform, world->digit_work_areas[digit].envelope), "platform avoids digit envelope");
        }
    }

    for (uint8_t index = 0; index < world->ladder_count; index++) {
        const building_clock_ladder_t* ladder = &world->ladders[index];
        expect_true(ladder->x >= 0 && ladder->x < BUILDING_CLOCK_WORLD_WIDTH && ladder->top_y >= 0 && ladder->bottom_y < BUILDING_CLOCK_WORLD_HEIGHT, "ladder inside world");
        expect_true(!vertical_hits_rect(ladder, world->date_band), "ladder avoids date band");
        expect_true(!vertical_hits_rect(ladder, world->colon_envelope), "ladder avoids colon envelope");
        for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            expect_true(!vertical_hits_rect(ladder, world->digit_work_areas[digit].envelope), "ladder avoids digit envelope");
        }
    }

    for (uint8_t index = 0; index < world->major_support_count; index++) {
        expect_true(rect_in_world(world->major_supports[index].bounds), "support inside world");
        expect_true(!rects_overlap(world->major_supports[index].bounds, world->date_band), "support avoids date band");
        expect_true(!rects_overlap(world->major_supports[index].bounds, world->colon_envelope), "support avoids colon envelope");
        for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
            expect_true(!rects_overlap(world->major_supports[index].bounds, world->digit_work_areas[digit].envelope), "support avoids digit envelope");
        }
    }

    for (uint8_t index = 0; index < world->depot_count; index++) {
        expect_true(rect_in_world(world->depots[index].area), "depot inside world");
    }
    for (uint8_t index = 0; index < world->debris_bay_count; index++) {
        expect_true(rect_in_world(world->debris_bays[index].area), "debris bay inside world");
    }
    for (uint8_t index = 0; index < world->staging_area_count; index++) {
        expect_true(rect_in_world(world->staging_areas[index].area), "staging area inside world");
    }
    for (uint8_t index = 0; index < world->worker_spawn_count; index++) {
        expect_true(point_in_world(world->worker_spawns[index].position), "worker spawn inside world");
    }

    for (uint8_t index = 0; index < world->crane_count; index++) {
        expect_true(point_in_world(world->cranes[index].base_position), "crane base inside world");
        expect_true(point_in_world(world->cranes[index].home_hook_position), "crane hook home inside world");
        expect_true(rect_in_world(world->cranes[index].working_area), "crane working area inside world");
    }
    for (uint8_t index = 0; index < world->lift_count; index++) {
        for (uint8_t stop = 0; stop < world->lifts[index].stop_count; stop++) {
            expect_true(point_in_world(world->lifts[index].stops[stop].position), "lift stop inside world");
        }
    }
    for (uint8_t index = 0; index < world->cart_route_count; index++) {
        for (uint8_t stop = 0; stop < world->cart_routes[index].stop_count; stop++) {
            expect_true(point_in_world(world->cart_routes[index].stops[stop].position), "cart stop inside world");
        }
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        const building_clock_digit_work_area_t* area = &world->digit_work_areas[digit];
        expect_true(rect_in_world(area->envelope), "digit envelope inside world");
        expect_true(rect_in_world(area->work_halo), "digit work halo inside world");
        expect_true(rect_in_world(area->hazard_area), "digit hazard area inside world");
        for (uint8_t access = 0; access < area->access_count; access++) {
            expect_true(point_in_world(area->access[access].position), "digit access inside world");
        }
    }
    for (uint8_t index = 0; index < world->temp_attachment_set_count; index++) {
        const building_clock_temp_attachment_set_t* attachment = &world->temp_attachment_sets[index];
        expect_true(rect_in_world(attachment->allowed_area), "temporary attachment area inside world");
        for (uint8_t anchor = 0; anchor < attachment->anchor_count; anchor++) {
            expect_true(point_in_world(attachment->anchors[anchor]), "temporary attachment anchor inside world");
        }
    }
}

static void test_rejection_reason_reproducibility(void) {
    building_clock_world_t candidate;
    building_clock_generate_candidate(&candidate, UINT64_C(0x55aa55aa55aa55aa), BUILDING_CLOCK_GENERATOR_VERSION, 0U);
    candidate.platforms[0].x_start = 60;
    candidate.platforms[0].x_end = 100;
    candidate.platforms[0].surface_y = 120;

    const building_clock_prevalidation_result_t first = building_clock_prevalidate_candidate(&candidate);
    const building_clock_prevalidation_result_t second = building_clock_prevalidate_candidate(&candidate);
    expect_true(first.reason == BUILDING_CLOCK_PREVALIDATION_PLATFORM_PROTECTED_REGION, "invalid candidate reports protected-platform reason");
    expect_true(first.reason == second.reason && first.item_index == second.item_index, "preliminary rejection reason is reproducible");
    expect_true(strcmp(building_clock_prevalidation_reason_name(first.reason), "platform crosses protected region") == 0, "preliminary rejection reason has stable text");
}

static void test_generation_batch(void) {
    bool topology_seen[BUILDING_CLOCK_TOPOLOGY_COUNT] = {false};
    bool crane_left_seen = false;
    bool crane_right_seen = false;
    bool lift_seen = false;
    bool no_lift_seen = false;
    int16_t minimum_lower_y = INT16_MAX;
    int16_t maximum_lower_y = INT16_MIN;
    uint64_t signatures[SIGNATURE_CAPACITY] = {0};
    uint16_t signature_count = 0U;
    uint32_t passed = 0U;
    uint32_t rejected = 0U;
    uint32_t reasons[BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE + 1U] = {0};

    for (uint32_t index = 0; index < BATCH_SIZE; index++) {
        const uint64_t root_seed = UINT64_C(0x6a09e667f3bcc909) + UINT64_C(0x9e3779b97f4a7c15) * index;
        const uint16_t attempt = (uint16_t)(index % 11U);
        building_clock_world_t world;
        building_clock_generate_candidate(&world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION, attempt);

        check_counts(&world);
        check_permanent_geometry(&world);
        expect_true(world.navigation.node_count == 0U && world.navigation.edge_count == 0U, "batch candidate navigation remains empty");

        if (world.topology_family < BUILDING_CLOCK_TOPOLOGY_COUNT) {
            topology_seen[world.topology_family] = true;
        }
        crane_left_seen |= world.crane_count > 0U && world.cranes[0].base_position.x < BUILDING_CLOCK_WORLD_WIDTH / 2;
        crane_right_seen |= world.crane_count > 0U && world.cranes[0].base_position.x > BUILDING_CLOCK_WORLD_WIDTH / 2;
        lift_seen |= world.lift_count > 0U;
        no_lift_seen |= world.lift_count == 0U;

        for (uint8_t platform = 0; platform < world.platform_count; platform++) {
            if (world.platforms[platform].surface_y >= 335) {
                if (world.platforms[platform].surface_y < minimum_lower_y) {
                    minimum_lower_y = world.platforms[platform].surface_y;
                }
                if (world.platforms[platform].surface_y > maximum_lower_y) {
                    maximum_lower_y = world.platforms[platform].surface_y;
                }
            }
        }

        const uint64_t signature = geometry_signature(&world);
        bool known_signature = false;
        for (uint16_t known = 0; known < signature_count; known++) {
            if (signatures[known] == signature) {
                known_signature = true;
                break;
            }
        }
        if (!known_signature && signature_count < SIGNATURE_CAPACITY) {
            signatures[signature_count++] = signature;
        }

        const building_clock_world_t world_before_validation = world;
        const building_clock_prevalidation_result_t validation = building_clock_prevalidate_candidate(&world);
        expect_true(memcmp(&world, &world_before_validation, sizeof(world)) == 0, "preliminary validation does not mutate candidate");
        if (validation.reason == BUILDING_CLOCK_PREVALIDATION_OK) {
            passed++;
        } else {
            rejected++;
            if ((uint32_t)validation.reason <= BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE) {
                reasons[validation.reason]++;
            }

            building_clock_world_t repeated;
            building_clock_generate_candidate(&repeated, root_seed, BUILDING_CLOCK_GENERATOR_VERSION, attempt);
            const building_clock_prevalidation_result_t repeated_validation = building_clock_prevalidate_candidate(&repeated);
            expect_true(validation.reason == repeated_validation.reason && validation.item_index == repeated_validation.item_index, "natural preliminary rejection is reproducible");
        }
    }

    unsigned int topology_count = 0U;
    for (uint8_t family = 0; family < BUILDING_CLOCK_TOPOLOGY_COUNT; family++) {
        topology_count += topology_seen[family] ? 1U : 0U;
    }
    expect_true(topology_count == BUILDING_CLOCK_TOPOLOGY_COUNT, "batch exercises every topology family");
    expect_true(crane_left_seen && crane_right_seen, "batch varies crane side");
    expect_true(lift_seen && no_lift_seen, "batch varies optional lift presence");
    expect_true(minimum_lower_y < maximum_lower_y, "batch varies platform levels");
    expect_true(signature_count >= 64U, "batch contains many distinct geometry signatures");

    printf("Generator batch: %u candidates, %" PRIu32 " preliminary pass, %" PRIu32 " preliminary reject, %u topology families, %u distinct signatures\n", BATCH_SIZE, passed, rejected, topology_count,
           signature_count);
    for (uint32_t reason = 1U; reason <= BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE; reason++) {
        if (reasons[reason] != 0U) {
            printf("  reject %-38s %" PRIu32 "\n", building_clock_prevalidation_reason_name((building_clock_prevalidation_reason_t)reason), reasons[reason]);
        }
    }
}

int main(void) {
    test_deterministic_candidate();
    test_attempt_derivation();
    test_rejection_reason_reproducibility();
    test_generation_batch();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock generator test(s) failed\n", failures);
        return 1;
    }

    puts("Building Clock generator tests passed");
    return 0;
}
