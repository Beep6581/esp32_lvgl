#include "building_clock_runtime_navigation.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(BUILDING_CLOCK_MAX_WORKERS <= 16U, "reservation owner masks must hold every worker ID");
_Static_assert(BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS <= 16U, "temporary attachment mask must hold every attachment ID");
_Static_assert(BUILDING_CLOCK_MAX_NAV_NODES <= UINT8_MAX, "route node IDs must fit uint8_t");
_Static_assert(BUILDING_CLOCK_MAX_ROUTE_EDGES <= UINT8_MAX, "route edge count must fit uint8_t");
_Static_assert(BUILDING_CLOCK_MAX_NAV_NODES* BUILDING_CLOCK_MAX_NAV_EDGES <= UINT16_MAX, "examined edge count must fit uint16_t");

typedef struct {
    uint32_t distance[BUILDING_CLOCK_MAX_NAV_NODES];
    uint16_t hop_count[BUILDING_CLOCK_MAX_NAV_NODES];
    uint16_t predecessor_edge[BUILDING_CLOCK_MAX_NAV_NODES];
    uint8_t predecessor_node[BUILDING_CLOCK_MAX_NAV_NODES];
    bool settled[BUILDING_CLOCK_MAX_NAV_NODES];
} route_workspace_t;

static bool graph_counts_valid(const building_clock_nav_graph_t* graph) {
    return graph->node_count <= BUILDING_CLOCK_MAX_NAV_NODES && graph->edge_count <= BUILDING_CLOCK_MAX_NAV_EDGES;
}

static bool graph_is_routable(const building_clock_nav_graph_t* graph) {
    if (!graph_counts_valid(graph)) {
        return false;
    }
    for (uint16_t edge_id = 0U; edge_id < graph->edge_count; edge_id++) {
        const building_clock_nav_edge_t* edge = &graph->edges[edge_id];
        if (edge->from_node_id >= graph->node_count || edge->to_node_id >= graph->node_count || edge->from_node_id == edge->to_node_id || edge->traversal_cost == 0U || edge->capacity == 0U) {
            return false;
        }
    }
    return true;
}

static bool edge_is_usable(const building_clock_nav_edge_t* edge, const building_clock_navigation_runtime_t* runtime, const building_clock_route_request_t* request) {
    if (request->carries_large_load && (edge->flags & BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD) == 0U) {
        return false;
    }
    if ((edge->kind == BUILDING_CLOCK_NAV_EDGE_RIDE_CART || edge->kind == BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT) && !request->allow_machinery_edges) {
        return false;
    }
    if ((edge->flags & BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE) != 0U) {
        if (!request->allow_temporary_connections || edge->temp_attachment_set_id >= BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS) {
            return false;
        }
        const uint16_t attachment_bit = (uint16_t)(1U << edge->temp_attachment_set_id);
        if ((runtime->active_temp_attachment_mask & attachment_bit) == 0U) {
            return false;
        }
    }
    return true;
}

static bool edge_destination(const building_clock_nav_edge_t* edge, uint8_t current_node_id, uint8_t* destination_node_id) {
    if (edge->from_node_id == current_node_id) {
        *destination_node_id = edge->to_node_id;
        return true;
    }
    if (edge->to_node_id == current_node_id && (edge->flags & BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL) != 0U) {
        *destination_node_id = edge->from_node_id;
        return true;
    }
    return false;
}

static uint8_t select_next_node(const building_clock_nav_graph_t* graph, const route_workspace_t* workspace) {
    uint8_t selected = BUILDING_CLOCK_INVALID_ID;
    for (uint8_t node_id = 0U; node_id < graph->node_count; node_id++) {
        if (workspace->settled[node_id] || workspace->distance[node_id] == UINT32_MAX) {
            continue;
        }
        if (selected == BUILDING_CLOCK_INVALID_ID || workspace->distance[node_id] < workspace->distance[selected] ||
            (workspace->distance[node_id] == workspace->distance[selected] && workspace->hop_count[node_id] < workspace->hop_count[selected]) ||
            (workspace->distance[node_id] == workspace->distance[selected] && workspace->hop_count[node_id] == workspace->hop_count[selected] && node_id < selected)) {
            selected = node_id;
        }
    }
    return selected;
}

static bool candidate_is_better(const route_workspace_t* workspace, uint8_t destination, uint32_t candidate_distance, uint16_t candidate_hops, uint8_t predecessor, uint16_t edge_id) {
    if (candidate_distance != workspace->distance[destination]) {
        return candidate_distance < workspace->distance[destination];
    }
    if (candidate_hops != workspace->hop_count[destination]) {
        return candidate_hops < workspace->hop_count[destination];
    }
    if (predecessor != workspace->predecessor_node[destination]) {
        return predecessor < workspace->predecessor_node[destination];
    }
    return edge_id < workspace->predecessor_edge[destination];
}

static building_clock_route_status_t build_route(const building_clock_route_request_t* request, const route_workspace_t* workspace, building_clock_route_t* route) {
    const uint16_t edge_count = workspace->hop_count[request->destination_node_id];
    if (edge_count >= BUILDING_CLOCK_MAX_ROUTE_NODES) {
        return BUILDING_CLOCK_ROUTE_CAPACITY_EXCEEDED;
    }

    route->total_cost = workspace->distance[request->destination_node_id];
    route->edge_count = (uint8_t)edge_count;
    route->node_count = (uint8_t)(edge_count + 1U);

    uint8_t current = request->destination_node_id;
    for (uint16_t node_position = route->node_count; node_position > 0U; node_position--) {
        route->node_ids[node_position - 1U] = current;
        if (node_position > 1U) {
            const uint16_t edge_id = workspace->predecessor_edge[current];
            const uint8_t predecessor = workspace->predecessor_node[current];
            if (edge_id == BUILDING_CLOCK_INVALID_EDGE_ID || predecessor == BUILDING_CLOCK_INVALID_ID) {
                return BUILDING_CLOCK_ROUTE_CAPACITY_EXCEEDED;
            }
            route->edge_ids[node_position - 2U] = edge_id;
            current = predecessor;
        }
    }
    return current == request->start_node_id ? BUILDING_CLOCK_ROUTE_OK : BUILDING_CLOCK_ROUTE_CAPACITY_EXCEEDED;
}

void building_clock_navigation_runtime_init(building_clock_navigation_runtime_t* runtime) {
    if (runtime != NULL) {
        memset(runtime, 0, sizeof(*runtime));
    }
}

void building_clock_navigation_reset_reservations(building_clock_navigation_runtime_t* runtime) {
    if (runtime == NULL) {
        return;
    }
    memset(runtime->node_owner_masks, 0, sizeof(runtime->node_owner_masks));
    memset(runtime->edge_owner_masks, 0, sizeof(runtime->edge_owner_masks));
}

bool building_clock_navigation_set_temp_active(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t attachment_set_id, bool active) {
    if (world == NULL || runtime == NULL || attachment_set_id >= world->temp_attachment_set_count || attachment_set_id >= BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS) {
        return false;
    }
    const uint16_t attachment_bit = (uint16_t)(1U << attachment_set_id);
    if (active) {
        runtime->active_temp_attachment_mask |= attachment_bit;
    } else {
        runtime->active_temp_attachment_mask &= (uint16_t)~attachment_bit;
    }
    return true;
}

bool building_clock_navigation_temp_is_active(const building_clock_navigation_runtime_t* runtime, uint8_t attachment_set_id) {
    if (runtime == NULL || attachment_set_id >= BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS) {
        return false;
    }
    return (runtime->active_temp_attachment_mask & (uint16_t)(1U << attachment_set_id)) != 0U;
}

building_clock_route_status_t building_clock_find_route(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, const building_clock_route_request_t* request,
                                                        building_clock_route_t* route) {
    if (route != NULL) {
        memset(route, 0, sizeof(*route));
    }
    if (world == NULL || runtime == NULL || request == NULL || route == NULL) {
        return BUILDING_CLOCK_ROUTE_INVALID_ARGUMENT;
    }
    const building_clock_nav_graph_t* graph = &world->navigation;
    if (!graph_is_routable(graph)) {
        return BUILDING_CLOCK_ROUTE_INVALID_GRAPH;
    }
    if (request->start_node_id >= graph->node_count || request->destination_node_id >= graph->node_count) {
        return BUILDING_CLOCK_ROUTE_INVALID_NODE;
    }

    route_workspace_t workspace;
    for (uint8_t node_id = 0U; node_id < BUILDING_CLOCK_MAX_NAV_NODES; node_id++) {
        workspace.distance[node_id] = UINT32_MAX;
        workspace.hop_count[node_id] = UINT16_MAX;
        workspace.predecessor_edge[node_id] = BUILDING_CLOCK_INVALID_EDGE_ID;
        workspace.predecessor_node[node_id] = BUILDING_CLOCK_INVALID_ID;
        workspace.settled[node_id] = false;
    }
    workspace.distance[request->start_node_id] = 0U;
    workspace.hop_count[request->start_node_id] = 0U;

    for (;;) {
        const uint8_t current = select_next_node(graph, &workspace);
        if (current == BUILDING_CLOCK_INVALID_ID) {
            break;
        }
        workspace.settled[current] = true;
        route->settled_node_count++;
        if (current == request->destination_node_id) {
            const building_clock_route_status_t status = build_route(request, &workspace, route);
            if (status != BUILDING_CLOCK_ROUTE_OK) {
                route->total_cost = 0U;
                route->node_count = 0U;
                route->edge_count = 0U;
            }
            return status;
        }

        for (uint16_t edge_id = 0U; edge_id < graph->edge_count; edge_id++) {
            route->examined_edge_count++;
            const building_clock_nav_edge_t* edge = &graph->edges[edge_id];
            if (!edge_is_usable(edge, runtime, request)) {
                continue;
            }
            uint8_t destination;
            if (!edge_destination(edge, current, &destination) || destination >= graph->node_count || workspace.settled[destination] || edge->traversal_cost == 0U ||
                workspace.distance[current] > UINT32_MAX - edge->traversal_cost) {
                continue;
            }
            const uint32_t candidate_distance = workspace.distance[current] + edge->traversal_cost;
            const uint16_t candidate_hops = (uint16_t)(workspace.hop_count[current] + 1U);
            if (candidate_is_better(&workspace, destination, candidate_distance, candidate_hops, current, edge_id)) {
                workspace.distance[destination] = candidate_distance;
                workspace.hop_count[destination] = candidate_hops;
                workspace.predecessor_node[destination] = current;
                workspace.predecessor_edge[destination] = edge_id;
            }
        }
    }
    return BUILDING_CLOCK_ROUTE_UNREACHABLE;
}

const char* building_clock_route_status_name(building_clock_route_status_t status) {
    switch (status) {
    case BUILDING_CLOCK_ROUTE_OK:
        return "ok";
    case BUILDING_CLOCK_ROUTE_INVALID_ARGUMENT:
        return "invalid argument";
    case BUILDING_CLOCK_ROUTE_INVALID_GRAPH:
        return "invalid graph";
    case BUILDING_CLOCK_ROUTE_INVALID_NODE:
        return "invalid node";
    case BUILDING_CLOCK_ROUTE_UNREACHABLE:
        return "unreachable";
    case BUILDING_CLOCK_ROUTE_CAPACITY_EXCEEDED:
        return "route capacity exceeded";
    default:
        return "unknown route status";
    }
}

uint8_t building_clock_navigation_node_capacity(const building_clock_world_t* world, uint8_t node_id) {
    if (world == NULL || !graph_counts_valid(&world->navigation) || node_id >= world->navigation.node_count) {
        return 0U;
    }
    uint8_t capacity = BUILDING_CLOCK_MAX_WORKERS;
    bool has_incident_edge = false;
    for (uint16_t edge_id = 0U; edge_id < world->navigation.edge_count; edge_id++) {
        const building_clock_nav_edge_t* edge = &world->navigation.edges[edge_id];
        if (edge->from_node_id == node_id || edge->to_node_id == node_id) {
            has_incident_edge = true;
            if (edge->capacity < capacity) {
                capacity = edge->capacity;
            }
        }
    }
    return has_incident_edge ? capacity : 1U;
}

static uint8_t owner_count(uint16_t owner_mask) {
    uint8_t count = 0U;
    while (owner_mask != 0U) {
        count = (uint8_t)(count + (owner_mask & 1U));
        owner_mask >>= 1U;
    }
    return count;
}

static building_clock_reservation_status_t reserve_mask(uint16_t* owner_mask, uint8_t capacity, uint8_t worker_id) {
    if (worker_id >= BUILDING_CLOCK_MAX_WORKERS) {
        return BUILDING_CLOCK_RESERVATION_INVALID_OWNER;
    }
    const uint16_t owner_bit = (uint16_t)(1U << worker_id);
    if ((*owner_mask & owner_bit) != 0U) {
        return BUILDING_CLOCK_RESERVATION_ALREADY_HELD;
    }
    if (capacity == 0U || owner_count(*owner_mask) >= capacity) {
        return BUILDING_CLOCK_RESERVATION_UNAVAILABLE;
    }
    *owner_mask |= owner_bit;
    return BUILDING_CLOCK_RESERVATION_ACQUIRED;
}

static building_clock_reservation_status_t release_mask(uint16_t* owner_mask, uint8_t worker_id) {
    if (worker_id >= BUILDING_CLOCK_MAX_WORKERS) {
        return BUILDING_CLOCK_RESERVATION_INVALID_OWNER;
    }
    const uint16_t owner_bit = (uint16_t)(1U << worker_id);
    if ((*owner_mask & owner_bit) == 0U) {
        return BUILDING_CLOCK_RESERVATION_NOT_HELD;
    }
    *owner_mask &= (uint16_t)~owner_bit;
    return BUILDING_CLOCK_RESERVATION_RELEASED;
}

building_clock_reservation_status_t building_clock_navigation_reserve_node(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t node_id, uint8_t worker_id) {
    if (world == NULL || runtime == NULL) {
        return BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT;
    }
    const uint8_t capacity = building_clock_navigation_node_capacity(world, node_id);
    if (capacity == 0U) {
        return BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE;
    }
    return reserve_mask(&runtime->node_owner_masks[node_id], capacity, worker_id);
}

building_clock_reservation_status_t building_clock_navigation_release_node(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint8_t node_id, uint8_t worker_id) {
    if (world == NULL || runtime == NULL) {
        return BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT;
    }
    if (building_clock_navigation_node_capacity(world, node_id) == 0U) {
        return BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE;
    }
    return release_mask(&runtime->node_owner_masks[node_id], worker_id);
}

building_clock_reservation_status_t building_clock_navigation_reserve_edge(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint16_t edge_id, uint8_t worker_id) {
    if (world == NULL || runtime == NULL) {
        return BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT;
    }
    if (!graph_counts_valid(&world->navigation) || edge_id >= world->navigation.edge_count || world->navigation.edges[edge_id].capacity == 0U) {
        return BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE;
    }
    return reserve_mask(&runtime->edge_owner_masks[edge_id], world->navigation.edges[edge_id].capacity, worker_id);
}

building_clock_reservation_status_t building_clock_navigation_release_edge(const building_clock_world_t* world, building_clock_navigation_runtime_t* runtime, uint16_t edge_id, uint8_t worker_id) {
    if (world == NULL || runtime == NULL) {
        return BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT;
    }
    if (!graph_counts_valid(&world->navigation) || edge_id >= world->navigation.edge_count || world->navigation.edges[edge_id].capacity == 0U) {
        return BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE;
    }
    return release_mask(&runtime->edge_owner_masks[edge_id], worker_id);
}

void building_clock_navigation_release_owner(building_clock_navigation_runtime_t* runtime, uint8_t worker_id) {
    if (runtime == NULL || worker_id >= BUILDING_CLOCK_MAX_WORKERS) {
        return;
    }
    const uint16_t owner_bit = (uint16_t)(1U << worker_id);
    for (uint8_t node_id = 0U; node_id < BUILDING_CLOCK_MAX_NAV_NODES; node_id++) {
        runtime->node_owner_masks[node_id] &= (uint16_t)~owner_bit;
    }
    for (uint16_t edge_id = 0U; edge_id < BUILDING_CLOCK_MAX_NAV_EDGES; edge_id++) {
        runtime->edge_owner_masks[edge_id] &= (uint16_t)~owner_bit;
    }
}

uint16_t building_clock_navigation_node_owner_mask(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, uint8_t node_id) {
    if (world == NULL || runtime == NULL || !graph_counts_valid(&world->navigation) || node_id >= world->navigation.node_count) {
        return 0U;
    }
    return runtime->node_owner_masks[node_id];
}

uint16_t building_clock_navigation_edge_owner_mask(const building_clock_world_t* world, const building_clock_navigation_runtime_t* runtime, uint16_t edge_id) {
    if (world == NULL || runtime == NULL || !graph_counts_valid(&world->navigation) || edge_id >= world->navigation.edge_count) {
        return 0U;
    }
    return runtime->edge_owner_masks[edge_id];
}

const char* building_clock_reservation_status_name(building_clock_reservation_status_t status) {
    switch (status) {
    case BUILDING_CLOCK_RESERVATION_ACQUIRED:
        return "acquired";
    case BUILDING_CLOCK_RESERVATION_ALREADY_HELD:
        return "already held";
    case BUILDING_CLOCK_RESERVATION_UNAVAILABLE:
        return "unavailable";
    case BUILDING_CLOCK_RESERVATION_INVALID_ARGUMENT:
        return "invalid argument";
    case BUILDING_CLOCK_RESERVATION_INVALID_RESOURCE:
        return "invalid resource";
    case BUILDING_CLOCK_RESERVATION_INVALID_OWNER:
        return "invalid owner";
    case BUILDING_CLOCK_RESERVATION_NOT_HELD:
        return "not held";
    case BUILDING_CLOCK_RESERVATION_RELEASED:
        return "released";
    default:
        return "unknown reservation status";
    }
}
