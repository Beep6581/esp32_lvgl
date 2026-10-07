#include "building_clock_generator.h"
#include "building_clock_navigation.h"
#include "building_clock_validation.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BATCH_SIZE 4096U

static unsigned int failures;

static void expect_true(bool condition, const char* description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static bool points_equal(building_clock_point_t first, building_clock_point_t second) {
    return first.x == second.x && first.y == second.y;
}

static bool point_on_platform(const building_clock_platform_t* platform, building_clock_point_t point) {
    return point.y == platform->surface_y && point.x >= platform->x_start && point.x <= platform->x_end;
}

static bool generate_prevalid_world(building_clock_world_t* world, uint64_t first_seed) {
    for (uint16_t attempt = 0; attempt < 256U; attempt++) {
        building_clock_generate_candidate(world, first_seed + attempt, BUILDING_CLOCK_GENERATOR_VERSION, attempt);
        if (building_clock_prevalidate_candidate(world).reason == BUILDING_CLOCK_PREVALIDATION_OK) {
            return true;
        }
    }
    return false;
}

static void check_graph_references(const building_clock_world_t* world) {
    expect_true(world->navigation.node_count <= BUILDING_CLOCK_MAX_NAV_NODES, "navigation node count stays within capacity");
    expect_true(world->navigation.edge_count <= BUILDING_CLOCK_MAX_NAV_EDGES, "navigation edge count stays within capacity");
    for (uint16_t edge = 0; edge < world->navigation.edge_count; edge++) {
        expect_true(world->navigation.edges[edge].from_node_id < world->navigation.node_count, "edge source node is valid");
        expect_true(world->navigation.edges[edge].to_node_id < world->navigation.node_count, "edge destination node is valid");
    }
}

static void check_edges_match_geometry(const building_clock_world_t* world) {
    for (uint16_t edge_id = 0; edge_id < world->navigation.edge_count; edge_id++) {
        const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
        const building_clock_point_t from = world->navigation.nodes[edge->from_node_id].position;
        const building_clock_point_t to = world->navigation.nodes[edge->to_node_id].position;

        switch (edge->kind) {
        case BUILDING_CLOCK_NAV_EDGE_WALK:
            expect_true(edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_PLATFORM && edge->source_id < world->platform_count, "walk edge identifies a platform");
            if (edge->source_id < world->platform_count) {
                expect_true(point_on_platform(&world->platforms[edge->source_id], from) && point_on_platform(&world->platforms[edge->source_id], to), "walk edge follows its platform surface");
            }
            break;
        case BUILDING_CLOCK_NAV_EDGE_CLIMB:
            expect_true(edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_LADDER && edge->source_id < world->ladder_count, "climb edge identifies a ladder");
            if (edge->source_id < world->ladder_count) {
                const building_clock_ladder_t* ladder = &world->ladders[edge->source_id];
                const building_clock_point_t top = {.x = ladder->x, .y = ladder->top_y};
                const building_clock_point_t bottom = {.x = ladder->x, .y = ladder->bottom_y};
                expect_true((points_equal(from, top) && points_equal(to, bottom)) || (points_equal(from, bottom) && points_equal(to, top)), "climb edge joins the actual ladder endpoints");
            }
            break;
        case BUILDING_CLOCK_NAV_EDGE_RIDE_CART:
            expect_true(edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE && edge->source_id < world->cart_route_count, "cart edge identifies a generated route");
            if (edge->source_id < world->cart_route_count) {
                const building_clock_nav_node_t* from_node = &world->navigation.nodes[edge->from_node_id];
                const building_clock_nav_node_t* to_node = &world->navigation.nodes[edge->to_node_id];
                expect_true(from_node->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE && to_node->source_kind == BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE && from_node->source_id == edge->source_id &&
                                to_node->source_id == edge->source_id && (from_node->source_point_id + 1U == to_node->source_point_id || to_node->source_point_id + 1U == from_node->source_point_id),
                            "cart edge joins adjacent stops on its generated route");
            }
            break;
        case BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT:
            expect_true(edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT && edge->source_id < world->lift_count, "lift edge identifies a generated lift");
            if (edge->source_id < world->lift_count) {
                const building_clock_nav_node_t* from_node = &world->navigation.nodes[edge->from_node_id];
                const building_clock_nav_node_t* to_node = &world->navigation.nodes[edge->to_node_id];
                expect_true(from_node->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT && to_node->source_kind == BUILDING_CLOCK_NAV_SOURCE_LIFT && from_node->source_id == edge->source_id &&
                                to_node->source_id == edge->source_id && (from_node->source_point_id + 1U == to_node->source_point_id || to_node->source_point_id + 1U == from_node->source_point_id),
                            "lift edge joins adjacent generated lift stops");
            }
            break;
        case BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION:
            expect_true(edge->source_kind == BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT && edge->source_id < world->temp_attachment_set_count && edge->temp_attachment_set_id == edge->source_id,
                        "temporary edge identifies its attachment set");
            break;
        default:
            expect_true(false, "edge kind is recognized");
            break;
        }
    }
}

static void test_deterministic_derivation(void) {
    building_clock_world_t first;
    building_clock_world_t second;
    expect_true(generate_prevalid_world(&first, UINT64_C(0x123456789abcdef0)), "find deterministic prevalidated candidate");
    second = first;

    const building_clock_nav_derivation_result_t first_result = building_clock_derive_navigation(&first);
    const building_clock_nav_derivation_result_t second_result = building_clock_derive_navigation(&second);
    expect_true(first_result.reason == BUILDING_CLOCK_NAV_DERIVATION_OK && second_result.reason == BUILDING_CLOCK_NAV_DERIVATION_OK, "deterministic candidates derive successfully");
    expect_true(memcmp(&first, &second, sizeof(first)) == 0, "identical candidates produce byte-equivalent derived worlds");
    expect_true(memcmp(&first.navigation, &second.navigation, sizeof(first.navigation)) == 0, "identical candidates produce byte-equivalent navigation graphs");
    check_graph_references(&first);
    check_edges_match_geometry(&first);

    const building_clock_world_t before_validation = first;
    const building_clock_complete_validation_result_t validation = building_clock_validate_complete(&first);
    expect_true(validation.reason == BUILDING_CLOCK_COMPLETE_VALIDATION_OK, "derived candidate passes complete validation");
    expect_true(memcmp(&first, &before_validation, sizeof(first)) == 0, "complete validation does not mutate the world");
}

static uint8_t add_isolated_copy(building_clock_world_t* world, uint8_t original_node_id) {
    if (original_node_id >= world->navigation.node_count || world->navigation.node_count >= BUILDING_CLOCK_MAX_NAV_NODES) {
        return BUILDING_CLOCK_INVALID_ID;
    }
    const uint8_t node_id = world->navigation.node_count++;
    world->navigation.nodes[node_id] = world->navigation.nodes[original_node_id];
    return node_id;
}

static bool prepare_complete_world(building_clock_world_t* world, uint64_t seed) {
    for (uint16_t attempt = 0; attempt < 256U; attempt++) {
        building_clock_generate_candidate(world, seed + attempt, BUILDING_CLOCK_GENERATOR_VERSION, attempt);
        if (building_clock_prevalidate_candidate(world).reason == BUILDING_CLOCK_PREVALIDATION_OK && building_clock_derive_navigation(world).reason == BUILDING_CLOCK_NAV_DERIVATION_OK &&
            building_clock_validate_complete(world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
            return true;
        }
    }
    return false;
}

static void test_unreachable_semantic_features(void) {
    building_clock_world_t base;
    expect_true(prepare_complete_world(&base, UINT64_C(0xa5a5a5a500000001)), "find completely valid candidate for reachability tests");

    building_clock_world_t world = base;
    world.worker_spawns[0].nav_node_id = add_isolated_copy(&world, world.worker_spawns[0].nav_node_id);
    expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_SPAWN_UNREACHABLE, "isolated worker spawn has explicit failure reason");

    world = base;
    world.depots[0].access_node_id = add_isolated_copy(&world, world.depots[0].access_node_id);
    expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_DEPOT_UNREACHABLE, "isolated depot has explicit failure reason");

    world = base;
    world.staging_areas[0].access_node_id = add_isolated_copy(&world, world.staging_areas[0].access_node_id);
    expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_STAGING_UNREACHABLE, "isolated staging area has explicit failure reason");

    world = base;
    world.debris_bays[0].access_node_id = add_isolated_copy(&world, world.debris_bays[0].access_node_id);
    expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_UNREACHABLE, "isolated debris bay has explicit failure reason");

    world = base;
    uint8_t lower_access = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t access = 0; access < world.digit_work_areas[0].access_count; access++) {
        if (world.digit_work_areas[0].access[access].kind == BUILDING_CLOCK_DIGIT_ACCESS_LOWER) {
            lower_access = access;
            break;
        }
    }
    expect_true(lower_access != BUILDING_CLOCK_INVALID_ID, "digit has lower access for reachability test");
    if (lower_access != BUILDING_CLOCK_INVALID_ID) {
        building_clock_digit_access_t* access = &world.digit_work_areas[0].access[lower_access];
        access->nav_node_id = add_isolated_copy(&world, access->nav_node_id);
        expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_DIGIT_ACCESS_UNREACHABLE, "isolated lower digit access has explicit failure reason");
    }
}

static void test_disconnected_candidate_and_hazard(void) {
    building_clock_world_t world;
    bool found = false;
    for (uint16_t attempt = 0; attempt < 256U && !found; attempt++) {
        building_clock_generate_candidate(&world, UINT64_C(0x0ddc0ffe00000000) + attempt, BUILDING_CLOCK_GENERATOR_VERSION, attempt);
        world.ladder_count = 0U;
        world.crane_count = 0U;
        world.lift_count = 0U;
        world.temp_attachment_set_count = 0U;
        found = building_clock_prevalidate_candidate(&world).reason == BUILDING_CLOCK_PREVALIDATION_OK;
    }
    expect_true(found, "construct a prevalidated disconnected candidate");
    if (found) {
        expect_true(building_clock_derive_navigation(&world).reason == BUILDING_CLOCK_NAV_DERIVATION_OK, "derive graph for disconnected candidate");
        expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_UPPER_CONSTRUCTION_ACCESS,
                    "complete validation rejects disconnected upper construction access");
    }

    expect_true(prepare_complete_world(&world, UINT64_C(0x0ddc0ffe10000000)), "find completely valid candidate for hazard test");
    world.digit_work_areas[0].safe_retreat_node_count = 0U;
    expect_true(building_clock_validate_complete(&world).reason == BUILDING_CLOCK_COMPLETE_VALIDATION_HAZARD_RETREAT, "missing safe retreat has explicit failure reason");
}

static void test_derivation_capacity_failure(void) {
    building_clock_world_t world;
    expect_true(generate_prevalid_world(&world, UINT64_C(0xfeedface00000000)), "find prevalidated candidate for capacity test");

    const building_clock_platform_t first_platform = world.platforms[0];
    const uint8_t original_platform_count = world.platform_count;
    world.platform_count = BUILDING_CLOCK_MAX_PLATFORM_SPANS;
    for (uint8_t platform = original_platform_count; platform < world.platform_count; platform++) {
        world.platforms[platform] = first_platform;
    }
    for (uint8_t digit = 0; digit < BUILDING_CLOCK_DIGIT_COUNT; digit++) {
        building_clock_digit_work_area_t* area = &world.digit_work_areas[digit];
        const building_clock_digit_access_t first_access = area->access[0];
        area->access_count = BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS;
        for (uint8_t access = 0; access < area->access_count; access++) {
            area->access[access] = first_access;
            area->access[access].nav_node_id = BUILDING_CLOCK_INVALID_ID;
        }
    }
    for (uint8_t attachment = 0; attachment < world.temp_attachment_set_count; attachment++) {
        building_clock_temp_attachment_set_t* set = &world.temp_attachment_sets[attachment];
        set->anchor_count = BUILDING_CLOCK_MAX_ATTACHMENT_ANCHORS;
        set->anchors[2] = set->anchors[0];
        set->anchors[3] = set->anchors[1];
    }

    const building_clock_nav_derivation_result_t derivation = building_clock_derive_navigation(&world);
    expect_true(derivation.reason == BUILDING_CLOCK_NAV_DERIVATION_NODE_CAPACITY, "node-capacity pressure returns explicit derivation failure");
    expect_true(world.navigation.node_count == 0U && world.navigation.edge_count == 0U, "capacity failure leaves no truncated navigation graph");
}

static void print_reason_counts(const char* heading, const uint32_t* reasons, uint32_t last_reason, const char* (*reason_name)(uint32_t)) {
    bool any_reason = false;
    printf("%s\n", heading);
    for (uint32_t reason = 1U; reason <= last_reason; reason++) {
        if (reasons[reason] == 0U) {
            continue;
        }
        any_reason = true;
        printf("  %-44s %" PRIu32 "\n", reason_name(reason), reasons[reason]);
    }
    if (!any_reason) {
        puts("  none");
    }
}

static const char* prevalidation_name(uint32_t reason) {
    return building_clock_prevalidation_reason_name((building_clock_prevalidation_reason_t)reason);
}

static const char* derivation_name(uint32_t reason) {
    return building_clock_nav_derivation_reason_name((building_clock_nav_derivation_reason_t)reason);
}

static const char* complete_validation_name(uint32_t reason) {
    return building_clock_complete_validation_reason_name((building_clock_complete_validation_reason_t)reason);
}

static void test_full_pipeline_batch(void) {
    uint32_t preliminary_failures[BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE + 1U] = {0};
    uint32_t derivation_failures[BUILDING_CLOCK_NAV_DERIVATION_TEMP_CONNECTION_CAPACITY + 1U] = {0};
    uint32_t complete_failures[BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT + 1U] = {0};
    uint32_t passing_topologies[BUILDING_CLOCK_TOPOLOGY_COUNT] = {0};
    uint32_t preliminary_pass = 0U;
    uint32_t derivation_pass = 0U;
    uint32_t complete_pass = 0U;
    uint8_t minimum_nodes = UINT8_MAX;
    uint8_t maximum_nodes = 0U;
    uint16_t minimum_edges = UINT16_MAX;
    uint16_t maximum_edges = 0U;
    uint64_t total_nodes = 0U;
    uint64_t total_edges = 0U;

    for (uint32_t index = 0; index < BATCH_SIZE; index++) {
        building_clock_world_t world;
        const uint64_t root_seed = UINT64_C(0xbb67ae8584caa73b) + UINT64_C(0x9e3779b97f4a7c15) * index;
        const uint16_t attempt = (uint16_t)(index % 13U);
        building_clock_generate_candidate(&world, root_seed, BUILDING_CLOCK_GENERATOR_VERSION, attempt);

        const building_clock_prevalidation_result_t preliminary = building_clock_prevalidate_candidate(&world);
        if (preliminary.reason != BUILDING_CLOCK_PREVALIDATION_OK) {
            preliminary_failures[preliminary.reason]++;
            continue;
        }
        preliminary_pass++;

        const building_clock_nav_derivation_result_t derivation = building_clock_derive_navigation(&world);
        if (derivation.reason != BUILDING_CLOCK_NAV_DERIVATION_OK) {
            derivation_failures[derivation.reason]++;
            continue;
        }
        derivation_pass++;
        check_graph_references(&world);
        if (world.navigation.node_count < minimum_nodes) {
            minimum_nodes = world.navigation.node_count;
        }
        if (world.navigation.node_count > maximum_nodes) {
            maximum_nodes = world.navigation.node_count;
        }
        if (world.navigation.edge_count < minimum_edges) {
            minimum_edges = world.navigation.edge_count;
        }
        if (world.navigation.edge_count > maximum_edges) {
            maximum_edges = world.navigation.edge_count;
        }
        total_nodes += world.navigation.node_count;
        total_edges += world.navigation.edge_count;

        const building_clock_world_t before_validation = world;
        const building_clock_complete_validation_result_t complete = building_clock_validate_complete(&world);
        expect_true(memcmp(&world, &before_validation, sizeof(world)) == 0, "batch complete validation remains read-only");
        if (complete.reason != BUILDING_CLOCK_COMPLETE_VALIDATION_OK) {
            complete_failures[complete.reason]++;
            continue;
        }

        complete_pass++;
        passing_topologies[world.topology_family]++;
    }

    expect_true(complete_pass > 0U, "full pipeline batch has completely valid candidates");
    printf("Full pipeline batch: %u total, %" PRIu32 " preliminary pass, %" PRIu32 " derivation pass, %" PRIu32 " complete pass\n", BATCH_SIZE, preliminary_pass, derivation_pass, complete_pass);
    print_reason_counts("Preliminary failures:", preliminary_failures, BUILDING_CLOCK_PREVALIDATION_NEGATIVE_SPACE, prevalidation_name);
    print_reason_counts("Navigation derivation failures:", derivation_failures, BUILDING_CLOCK_NAV_DERIVATION_TEMP_CONNECTION_CAPACITY, derivation_name);
    print_reason_counts("Complete validation failures:", complete_failures, BUILDING_CLOCK_COMPLETE_VALIDATION_DEBRIS_FOUNDATION_CONFLICT, complete_validation_name);
    if (complete_pass > 0U) {
        printf("Passing topology distribution:");
        for (uint8_t topology = 0; topology < BUILDING_CLOCK_TOPOLOGY_COUNT; topology++) {
            printf(" %u=%" PRIu32, topology, passing_topologies[topology]);
        }
    }
    if (derivation_pass > 0U) {
        printf("\nDerived navigation nodes: %u-%u, average %.2f\n", minimum_nodes, maximum_nodes, (double)total_nodes / derivation_pass);
        printf("Derived navigation edges: %u-%u, average %.2f\n", minimum_edges, maximum_edges, (double)total_edges / derivation_pass);
    }
}

int main(void) {
    test_deterministic_derivation();
    test_unreachable_semantic_features();
    test_disconnected_candidate_and_hazard();
    test_derivation_capacity_failure();
    test_full_pipeline_batch();

    if (failures != 0U) {
        fprintf(stderr, "%u Building Clock navigation test(s) failed\n", failures);
        return 1;
    }
    puts("Building Clock navigation tests passed");
    return 0;
}
