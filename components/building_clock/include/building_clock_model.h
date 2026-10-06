#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic design-space dimensions. These are not framebuffer dimensions. */
#define BUILDING_CLOCK_WORLD_WIDTH 480
#define BUILDING_CLOCK_WORLD_HEIGHT 480
#define BUILDING_CLOCK_DIGIT_COUNT 4

/* Generated-world safety limits from the design specification. */
#define BUILDING_CLOCK_MAX_PLATFORM_SPANS 18
#define BUILDING_CLOCK_MAX_LADDERS 10
#define BUILDING_CLOCK_MAX_MAJOR_SUPPORTS 4
#define BUILDING_CLOCK_MAX_CRANES 1
#define BUILDING_CLOCK_MAX_LIFTS 1
#define BUILDING_CLOCK_MAX_CART_ROUTES 2
#define BUILDING_CLOCK_MAX_MATERIAL_DEPOTS 1
#define BUILDING_CLOCK_MAX_DEBRIS_BAYS 4
#define BUILDING_CLOCK_MAX_STAGING_AREAS 8
#define BUILDING_CLOCK_MAX_WORKER_SPAWNS 10
#define BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS 8
#define BUILDING_CLOCK_MAX_NAV_NODES 96
#define BUILDING_CLOCK_MAX_NAV_EDGES 160

/* Small per-world limits needed to bound nested generated records. */
#define BUILDING_CLOCK_MAX_CART_ROUTE_STOPS 8
#define BUILDING_CLOCK_MAX_LIFT_STOPS 6
#define BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS 8
#define BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES 4
#define BUILDING_CLOCK_MAX_ATTACHMENT_ANCHORS 4
#define BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS 4

/* Mutable simulation safety limits. */
#define BUILDING_CLOCK_MAX_DIGIT_SECTIONS 10
#define BUILDING_CLOCK_MAX_WORKERS 9
#define BUILDING_CLOCK_MAX_JOBS 48
#define BUILDING_CLOCK_MAX_TEMP_STRUCTURES BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS
#define BUILDING_CLOCK_MAX_FALLING_SECTIONS 20
#define BUILDING_CLOCK_MAX_DEBRIS_PIECES 32
#define BUILDING_CLOCK_MAX_LOADS 8
#define BUILDING_CLOCK_MAX_CARTS 2
#define BUILDING_CLOCK_MAX_HAZARDS BUILDING_CLOCK_DIGIT_COUNT
#define BUILDING_CLOCK_MAX_WORKER_ROUTE_NODES BUILDING_CLOCK_MAX_NAV_NODES

#define BUILDING_CLOCK_INVALID_ID UINT8_MAX
#define BUILDING_CLOCK_INVALID_EDGE_ID UINT16_MAX

typedef struct {
    int16_t x;
    int16_t y;
} building_clock_point_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} building_clock_rect_t;

typedef struct {
    int16_t x;
    int16_t y;
} building_clock_vector_t;

typedef enum {
    BUILDING_CLOCK_SUPPORT_TOWER = 0,
    BUILDING_CLOCK_SUPPORT_FRAME,
    BUILDING_CLOCK_SUPPORT_BRACE,
} building_clock_support_kind_t;

typedef struct {
    int16_t x_start;
    int16_t x_end;
    int16_t surface_y;
    uint32_t support_mask;
} building_clock_platform_t;

typedef struct {
    int16_t x;
    int16_t top_y;
    int16_t bottom_y;
    uint8_t top_platform_id;
    uint8_t bottom_platform_id;
} building_clock_ladder_t;

typedef struct {
    building_clock_support_kind_t kind;
    building_clock_rect_t bounds;
    uint32_t platform_mask;
} building_clock_major_support_t;

typedef enum {
    BUILDING_CLOCK_NAV_SOURCE_PLATFORM = 0,
    BUILDING_CLOCK_NAV_SOURCE_LADDER,
    BUILDING_CLOCK_NAV_SOURCE_CART_ROUTE,
    BUILDING_CLOCK_NAV_SOURCE_LIFT,
    BUILDING_CLOCK_NAV_SOURCE_DIGIT_ACCESS,
    BUILDING_CLOCK_NAV_SOURCE_STAGING_AREA,
    BUILDING_CLOCK_NAV_SOURCE_DEPOT,
    BUILDING_CLOCK_NAV_SOURCE_DEBRIS_BAY,
    BUILDING_CLOCK_NAV_SOURCE_MACHINERY,
    BUILDING_CLOCK_NAV_SOURCE_SPAWN,
    BUILDING_CLOCK_NAV_SOURCE_TEMP_ATTACHMENT,
} building_clock_nav_source_kind_t;

typedef enum {
    BUILDING_CLOCK_NAV_NODE_PLATFORM_ENDPOINT = 0,
    BUILDING_CLOCK_NAV_NODE_PLATFORM_JUNCTION,
    BUILDING_CLOCK_NAV_NODE_LADDER_TOP,
    BUILDING_CLOCK_NAV_NODE_LADDER_BOTTOM,
    BUILDING_CLOCK_NAV_NODE_CART_STOP,
    BUILDING_CLOCK_NAV_NODE_LIFT_STOP,
    BUILDING_CLOCK_NAV_NODE_DIGIT_WORK,
    BUILDING_CLOCK_NAV_NODE_STAGING,
    BUILDING_CLOCK_NAV_NODE_DEPOT,
    BUILDING_CLOCK_NAV_NODE_DEBRIS,
    BUILDING_CLOCK_NAV_NODE_MACHINERY_CONTROL,
    BUILDING_CLOCK_NAV_NODE_SPAWN,
    BUILDING_CLOCK_NAV_NODE_SAFE_WAITING,
    BUILDING_CLOCK_NAV_NODE_TEMP_ATTACHMENT,
} building_clock_nav_node_kind_t;

typedef struct {
    building_clock_nav_node_kind_t kind;
    building_clock_point_t position;
    building_clock_nav_source_kind_t source_kind;
    uint8_t source_id;
    uint8_t source_point_id;
} building_clock_nav_node_t;

typedef enum {
    BUILDING_CLOCK_NAV_EDGE_WALK = 0,
    BUILDING_CLOCK_NAV_EDGE_CLIMB,
    BUILDING_CLOCK_NAV_EDGE_RIDE_CART,
    BUILDING_CLOCK_NAV_EDGE_RIDE_LIFT,
    BUILDING_CLOCK_NAV_EDGE_TEMP_CONNECTION,
} building_clock_nav_edge_kind_t;

typedef enum {
    BUILDING_CLOCK_NAV_EDGE_BIDIRECTIONAL = 1U << 0,
    BUILDING_CLOCK_NAV_EDGE_ALLOW_LARGE_LOAD = 1U << 1,
    BUILDING_CLOCK_NAV_EDGE_REQUIRES_TEMP_STRUCTURE = 1U << 2,
} building_clock_nav_edge_flag_t;

typedef struct {
    uint8_t from_node_id;
    uint8_t to_node_id;
    building_clock_nav_edge_kind_t kind;
    uint16_t traversal_cost;
    uint8_t capacity;
    uint8_t flags;
    uint8_t temp_attachment_set_id;
} building_clock_nav_edge_t;

typedef struct {
    uint8_t node_count;
    building_clock_nav_node_t nodes[BUILDING_CLOCK_MAX_NAV_NODES];
    uint16_t edge_count;
    building_clock_nav_edge_t edges[BUILDING_CLOCK_MAX_NAV_EDGES];
} building_clock_nav_graph_t;

typedef struct {
    building_clock_rect_t area;
    uint8_t access_node_id;
} building_clock_depot_t;

typedef struct {
    building_clock_rect_t area;
    uint8_t access_node_id;
    uint8_t assigned_digit_mask;
} building_clock_debris_bay_t;

typedef struct {
    building_clock_rect_t area;
    uint8_t access_node_id;
    uint8_t assigned_digit_mask;
} building_clock_staging_area_t;

typedef struct {
    building_clock_point_t position;
    uint8_t nav_node_id;
} building_clock_worker_spawn_t;

typedef enum {
    BUILDING_CLOCK_DIGIT_ACCESS_LOWER = 0,
    BUILDING_CLOCK_DIGIT_ACCESS_LEFT,
    BUILDING_CLOCK_DIGIT_ACCESS_RIGHT,
    BUILDING_CLOCK_DIGIT_ACCESS_UPPER,
    BUILDING_CLOCK_DIGIT_ACCESS_MACHINERY,
    BUILDING_CLOCK_DIGIT_ACCESS_STAGING,
} building_clock_digit_access_kind_t;

typedef struct {
    building_clock_digit_access_kind_t kind;
    building_clock_point_t position;
    uint8_t nav_node_id;
} building_clock_digit_access_t;

typedef struct {
    building_clock_rect_t envelope;
    building_clock_rect_t work_halo;
    building_clock_rect_t hazard_area;
    uint8_t debris_bay_id;
    uint8_t access_count;
    building_clock_digit_access_t access[BUILDING_CLOCK_MAX_DIGIT_ACCESS_POINTS];
    uint8_t safe_retreat_node_count;
    uint8_t safe_retreat_node_ids[BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES];
} building_clock_digit_work_area_t;

typedef enum {
    BUILDING_CLOCK_TEMP_STRUCTURE_SCAFFOLD = 0,
    BUILDING_CLOCK_TEMP_STRUCTURE_PLATFORM,
    BUILDING_CLOCK_TEMP_STRUCTURE_LADDER,
} building_clock_temp_structure_kind_t;

typedef struct {
    uint8_t from_node_id;
    uint8_t to_node_id;
    building_clock_nav_edge_kind_t edge_kind;
    uint16_t traversal_cost;
    uint8_t capacity;
    bool allow_large_load;
} building_clock_temp_connection_t;

typedef struct {
    building_clock_temp_structure_kind_t kind;
    uint8_t assigned_digit_mask;
    building_clock_rect_t allowed_area;
    uint8_t anchor_count;
    building_clock_point_t anchors[BUILDING_CLOCK_MAX_ATTACHMENT_ANCHORS];
    uint8_t connection_count;
    building_clock_temp_connection_t connections[BUILDING_CLOCK_MAX_ATTACHMENT_CONNECTIONS];
} building_clock_temp_attachment_set_t;

typedef struct {
    building_clock_point_t position;
    uint8_t nav_node_id;
    uint8_t staging_area_id;
} building_clock_route_stop_t;

typedef struct {
    uint8_t stop_count;
    building_clock_route_stop_t stops[BUILDING_CLOCK_MAX_CART_ROUTE_STOPS];
} building_clock_cart_route_t;

typedef struct {
    building_clock_point_t base_position;
    building_clock_point_t home_hook_position;
    building_clock_rect_t working_area;
    uint8_t operator_node_id;
} building_clock_crane_layout_t;

typedef struct {
    building_clock_point_t position;
    uint8_t nav_node_id;
} building_clock_lift_stop_t;

typedef struct {
    int16_t x;
    uint8_t operator_node_id;
    uint8_t stop_count;
    building_clock_lift_stop_t stops[BUILDING_CLOCK_MAX_LIFT_STOPS];
} building_clock_lift_layout_t;

typedef struct {
    uint64_t root_seed;
    uint64_t world_seed;
    uint64_t scheduling_seed;
    uint64_t behavior_seed;
    uint64_t cosmetic_seed;
    uint32_t generator_version;
    uint16_t accepted_attempt;
} building_clock_replay_metadata_t;

/* Filled by generation, then treated as immutable by validation and runtime. */
typedef struct {
    building_clock_replay_metadata_t replay;
    building_clock_rect_t colon_envelope;
    building_clock_rect_t date_band;

    uint8_t platform_count;
    building_clock_platform_t platforms[BUILDING_CLOCK_MAX_PLATFORM_SPANS];
    uint8_t ladder_count;
    building_clock_ladder_t ladders[BUILDING_CLOCK_MAX_LADDERS];
    uint8_t major_support_count;
    building_clock_major_support_t major_supports[BUILDING_CLOCK_MAX_MAJOR_SUPPORTS];

    uint8_t crane_count;
    building_clock_crane_layout_t cranes[BUILDING_CLOCK_MAX_CRANES];
    uint8_t lift_count;
    building_clock_lift_layout_t lifts[BUILDING_CLOCK_MAX_LIFTS];
    uint8_t cart_route_count;
    building_clock_cart_route_t cart_routes[BUILDING_CLOCK_MAX_CART_ROUTES];

    uint8_t depot_count;
    building_clock_depot_t depots[BUILDING_CLOCK_MAX_MATERIAL_DEPOTS];
    uint8_t debris_bay_count;
    building_clock_debris_bay_t debris_bays[BUILDING_CLOCK_MAX_DEBRIS_BAYS];
    uint8_t staging_area_count;
    building_clock_staging_area_t staging_areas[BUILDING_CLOCK_MAX_STAGING_AREAS];
    uint8_t worker_spawn_count;
    building_clock_worker_spawn_t worker_spawns[BUILDING_CLOCK_MAX_WORKER_SPAWNS];

    building_clock_digit_work_area_t digit_work_areas[BUILDING_CLOCK_DIGIT_COUNT];
    uint8_t temp_attachment_set_count;
    building_clock_temp_attachment_set_t temp_attachment_sets[BUILDING_CLOCK_MAX_TEMP_ATTACHMENT_SETS];

    /* Derived from the geometry above after generation and validation. */
    building_clock_nav_graph_t navigation;
} building_clock_world_t;

typedef struct {
    uint64_t state;
} building_clock_random_stream_t;

typedef struct {
    building_clock_random_stream_t scheduling;
    building_clock_random_stream_t behavior;
    building_clock_random_stream_t cosmetic;
} building_clock_runtime_random_streams_t;

typedef enum {
    BUILDING_CLOCK_DIGIT_STABLE = 0,
    BUILDING_CLOCK_DIGIT_PREPARING,
    BUILDING_CLOCK_DIGIT_EVACUATING,
    BUILDING_CLOCK_DIGIT_COLLAPSING,
    BUILDING_CLOCK_DIGIT_SETTLING,
    BUILDING_CLOCK_DIGIT_CLEARING,
    BUILDING_CLOCK_DIGIT_FOUNDATION,
    BUILDING_CLOCK_DIGIT_STRUCTURE,
    BUILDING_CLOCK_DIGIT_RECOGNIZABLE,
    BUILDING_CLOCK_DIGIT_FINISHING,
    BUILDING_CLOCK_DIGIT_COMPLETE,
} building_clock_digit_lifecycle_t;

typedef enum {
    BUILDING_CLOCK_SECTION_FOUNDATION = 0,
    BUILDING_CLOCK_SECTION_TOP_BEAM,
    BUILDING_CLOCK_SECTION_MIDDLE_BEAM,
    BUILDING_CLOCK_SECTION_UPPER_LEFT_PIER,
    BUILDING_CLOCK_SECTION_UPPER_RIGHT_PIER,
    BUILDING_CLOCK_SECTION_LOWER_LEFT_PIER,
    BUILDING_CLOCK_SECTION_LOWER_RIGHT_PIER,
    BUILDING_CLOCK_SECTION_CORNER_OR_DIAGONAL,
    BUILDING_CLOCK_SECTION_FINISHING_OR_INFILL,
} building_clock_section_kind_t;

typedef enum {
    BUILDING_CLOCK_SECTION_ABSENT = 0,
    BUILDING_CLOCK_SECTION_STAGED,
    BUILDING_CLOCK_SECTION_IN_TRANSIT,
    BUILDING_CLOCK_SECTION_PLACED,
    BUILDING_CLOCK_SECTION_SECURED,
    BUILDING_CLOCK_SECTION_FAILED,
    BUILDING_CLOCK_SECTION_FALLING,
    BUILDING_CLOCK_SECTION_DEBRIS,
} building_clock_section_status_t;

typedef struct {
    building_clock_section_kind_t kind;
    building_clock_section_status_t status;
    uint16_t dependency_mask;
    uint8_t assigned_job_id;
    uint16_t progress_per_mille;
} building_clock_section_state_t;

typedef struct {
    uint8_t numeral;
    uint8_t section_count;
    building_clock_section_state_t sections[BUILDING_CLOCK_MAX_DIGIT_SECTIONS];
} building_clock_digit_structure_t;

typedef struct {
    uint8_t current_digit;
    uint8_t target_digit;
    building_clock_digit_lifecycle_t lifecycle;
    building_clock_digit_structure_t outgoing;
    building_clock_digit_structure_t incoming;
    uint32_t lifecycle_elapsed_ms;
} building_clock_digit_state_t;

typedef enum {
    BUILDING_CLOCK_WORKER_ROLE_GENERAL = 0,
    BUILDING_CLOCK_WORKER_ROLE_BUILDER,
    BUILDING_CLOCK_WORKER_ROLE_RIGGER,
    BUILDING_CLOCK_WORKER_ROLE_OPERATOR,
    BUILDING_CLOCK_WORKER_ROLE_CLEARER,
    BUILDING_CLOCK_WORKER_ROLE_INSPECTOR,
} building_clock_worker_role_t;

typedef enum {
    BUILDING_CLOCK_WORKER_CAN_BUILD = 1U << 0,
    BUILDING_CLOCK_WORKER_CAN_RIG = 1U << 1,
    BUILDING_CLOCK_WORKER_CAN_OPERATE = 1U << 2,
    BUILDING_CLOCK_WORKER_CAN_CLEAR = 1U << 3,
    BUILDING_CLOCK_WORKER_CAN_INSPECT = 1U << 4,
    BUILDING_CLOCK_WORKER_CAN_CARRY_LARGE = 1U << 5,
} building_clock_worker_capability_t;

typedef enum {
    BUILDING_CLOCK_WORKER_IDLE = 0,
    BUILDING_CLOCK_WORKER_CHOOSE_JOB,
    BUILDING_CLOCK_WORKER_WALK,
    BUILDING_CLOCK_WORKER_CLIMB,
    BUILDING_CLOCK_WORKER_RIDE,
    BUILDING_CLOCK_WORKER_CARRY,
    BUILDING_CLOCK_WORKER_PUSH,
    BUILDING_CLOCK_WORKER_WAIT,
    BUILDING_CLOCK_WORKER_YIELD_OR_TURN,
    BUILDING_CLOCK_WORKER_INSPECT,
    BUILDING_CLOCK_WORKER_HAMMER,
    BUILDING_CLOCK_WORKER_OPERATE_MACHINERY,
    BUILDING_CLOCK_WORKER_CLEAR_DEBRIS,
    BUILDING_CLOCK_WORKER_AVOID_DANGER,
    BUILDING_CLOCK_WORKER_FINISH_JOB,
    BUILDING_CLOCK_WORKER_CELEBRATE,
    BUILDING_CLOCK_WORKER_SMOKE,
} building_clock_worker_state_kind_t;

typedef enum {
    BUILDING_CLOCK_FACING_LEFT = 0,
    BUILDING_CLOCK_FACING_RIGHT,
} building_clock_facing_t;

typedef struct {
    building_clock_worker_role_t preferred_role;
    uint16_t capabilities;
    building_clock_worker_state_kind_t state;
    building_clock_point_t position;
    building_clock_facing_t facing;
    uint8_t current_job_id;
    uint8_t carried_load_id;
    uint8_t current_node_id;
    uint8_t destination_node_id;
    uint16_t reserved_edge_id;
    uint8_t route_node_count;
    uint8_t route_node_index;
    uint8_t route_node_ids[BUILDING_CLOCK_MAX_WORKER_ROUTE_NODES];
    uint16_t edge_progress_per_mille;
    uint8_t action_phase;
    uint32_t state_elapsed_ms;
    uint32_t behavior_timer_ms;
} building_clock_worker_t;

typedef enum {
    BUILDING_CLOCK_JOB_STAGE_MATERIAL = 0,
    BUILDING_CLOCK_JOB_CARRY_MATERIAL,
    BUILDING_CLOCK_JOB_PUSH_CART,
    BUILDING_CLOCK_JOB_OPERATE_CRANE,
    BUILDING_CLOCK_JOB_OPERATE_LIFT,
    BUILDING_CLOCK_JOB_INSTALL_TEMP_LADDER,
    BUILDING_CLOCK_JOB_INSTALL_TEMP_PLATFORM,
    BUILDING_CLOCK_JOB_REMOVE_TEMP_STRUCTURE,
    BUILDING_CLOCK_JOB_REMOVE_SUPPORT,
    BUILDING_CLOCK_JOB_CLEAR_DEBRIS,
    BUILDING_CLOCK_JOB_PLACE_SECTION,
    BUILDING_CLOCK_JOB_SECURE_SECTION,
    BUILDING_CLOCK_JOB_INSPECT,
    BUILDING_CLOCK_JOB_PATROL,
    BUILDING_CLOCK_JOB_CELEBRATE,
} building_clock_job_kind_t;

typedef enum {
    BUILDING_CLOCK_JOB_PENDING = 0,
    BUILDING_CLOCK_JOB_RESERVED,
    BUILDING_CLOCK_JOB_ACTIVE,
    BUILDING_CLOCK_JOB_BLOCKED,
    BUILDING_CLOCK_JOB_COMPLETE,
    BUILDING_CLOCK_JOB_CANCELLED,
} building_clock_job_status_t;

typedef enum {
    BUILDING_CLOCK_MATERIAL_LOOSE_MASONRY = 0,
    BUILDING_CLOCK_MATERIAL_STRUCTURAL_SECTION,
    BUILDING_CLOCK_MATERIAL_SCAFFOLD,
    BUILDING_CLOCK_MATERIAL_TOOL,
    BUILDING_CLOCK_MATERIAL_KIND_COUNT,
    BUILDING_CLOCK_MATERIAL_NONE = UINT8_MAX,
} building_clock_material_kind_t;

typedef enum {
    BUILDING_CLOCK_MACHINERY_NONE = 0,
    BUILDING_CLOCK_MACHINERY_CART,
    BUILDING_CLOCK_MACHINERY_CRANE,
    BUILDING_CLOCK_MACHINERY_LIFT,
} building_clock_machinery_kind_t;

typedef struct {
    building_clock_job_kind_t kind;
    building_clock_job_status_t status;
    uint8_t priority;
    uint8_t target_node_id;
    uint8_t digit_id;
    uint8_t section_id;
    building_clock_material_kind_t required_material;
    building_clock_machinery_kind_t machinery_kind;
    uint8_t machinery_id;
    uint16_t required_worker_capabilities;
    uint32_t earliest_start_ms;
    uint32_t desired_completion_ms;
    uint8_t reserved_worker_id;
} building_clock_job_t;

typedef enum {
    BUILDING_CLOCK_TEMP_STRUCTURE_PLANNED = 0,
    BUILDING_CLOCK_TEMP_STRUCTURE_BUILDING,
    BUILDING_CLOCK_TEMP_STRUCTURE_ACTIVE,
    BUILDING_CLOCK_TEMP_STRUCTURE_REMOVING,
} building_clock_temp_structure_status_t;

typedef struct {
    building_clock_temp_structure_kind_t kind;
    building_clock_temp_structure_status_t status;
    uint8_t attachment_set_id;
    uint8_t assigned_job_id;
    uint16_t worker_occupancy_mask;
    uint16_t progress_per_mille;
} building_clock_temp_structure_t;

typedef struct {
    uint16_t quantity[BUILDING_CLOCK_MATERIAL_KIND_COUNT];
} building_clock_material_inventory_t;

typedef enum {
    BUILDING_CLOCK_LOAD_AT_DEPOT = 0,
    BUILDING_CLOCK_LOAD_STAGED,
    BUILDING_CLOCK_LOAD_CARRIED,
    BUILDING_CLOCK_LOAD_ON_CART,
    BUILDING_CLOCK_LOAD_SUSPENDED,
    BUILDING_CLOCK_LOAD_PLACED,
    BUILDING_CLOCK_LOAD_CONSUMED,
} building_clock_load_status_t;

typedef struct {
    building_clock_material_kind_t material_kind;
    building_clock_load_status_t status;
    uint16_t quantity;
    building_clock_point_t position;
    uint8_t digit_id;
    uint8_t section_id;
    uint8_t location_id;
    uint8_t carrier_worker_id;
    building_clock_machinery_kind_t machinery_kind;
    uint8_t machinery_id;
} building_clock_load_t;

typedef enum {
    BUILDING_CLOCK_DEBRIS_FALLING = 0,
    BUILDING_CLOCK_DEBRIS_SETTLED,
    BUILDING_CLOCK_DEBRIS_RECOVERABLE,
    BUILDING_CLOCK_DEBRIS_RESERVED,
    BUILDING_CLOCK_DEBRIS_CARRIED,
    BUILDING_CLOCK_DEBRIS_CLEARED,
} building_clock_debris_status_t;

typedef struct {
    building_clock_debris_status_t status;
    building_clock_material_kind_t material_kind;
    building_clock_point_t position;
    building_clock_vector_t velocity;
    uint8_t source_digit_id;
    uint8_t debris_bay_id;
    uint8_t reserved_worker_id;
} building_clock_debris_t;

typedef struct {
    building_clock_section_kind_t section_kind;
    building_clock_point_t position;
    building_clock_vector_t velocity;
    int16_t angle_degrees;
    int16_t angular_velocity_degrees_per_second;
    uint8_t source_digit_id;
    uint8_t target_debris_bay_id;
} building_clock_falling_section_t;

typedef enum {
    BUILDING_CLOCK_CART_PARKED = 0,
    BUILDING_CLOCK_CART_LOADING,
    BUILDING_CLOCK_CART_MOVING,
    BUILDING_CLOCK_CART_UNLOADING,
    BUILDING_CLOCK_CART_WAITING,
} building_clock_cart_status_t;

typedef struct {
    building_clock_cart_status_t status;
    uint8_t route_id;
    uint8_t current_stop_id;
    uint8_t target_stop_id;
    uint16_t route_progress_per_mille;
    uint16_t load_mask;
    uint8_t operator_worker_id;
} building_clock_cart_t;

typedef enum {
    BUILDING_CLOCK_CRANE_IDLE = 0,
    BUILDING_CLOCK_CRANE_SLEWING,
    BUILDING_CLOCK_CRANE_LOWERING,
    BUILDING_CLOCK_CRANE_WAITING_FOR_ATTACHMENT,
    BUILDING_CLOCK_CRANE_LIFTING,
    BUILDING_CLOCK_CRANE_CARRYING,
    BUILDING_CLOCK_CRANE_LOWERING_AT_DESTINATION,
    BUILDING_CLOCK_CRANE_RETURNING,
} building_clock_crane_status_t;

typedef struct {
    building_clock_crane_status_t status;
    building_clock_point_t hook_position;
    building_clock_point_t target_position;
    uint8_t suspended_load_id;
    uint8_t operator_worker_id;
    uint16_t movement_progress_per_mille;
} building_clock_crane_t;

typedef enum {
    BUILDING_CLOCK_LIFT_IDLE = 0,
    BUILDING_CLOCK_LIFT_CALLED,
    BUILDING_CLOCK_LIFT_MOVING,
    BUILDING_CLOCK_LIFT_LOADING,
    BUILDING_CLOCK_LIFT_UNLOADING,
} building_clock_lift_status_t;

typedef struct {
    building_clock_lift_status_t status;
    uint8_t current_stop_id;
    uint8_t target_stop_id;
    uint16_t worker_occupancy_mask;
    uint16_t load_mask;
    uint16_t movement_progress_per_mille;
} building_clock_lift_t;

typedef enum {
    BUILDING_CLOCK_HAZARD_DIGIT_COLLAPSE = 0,
    BUILDING_CLOCK_HAZARD_FALLING_LOAD,
    BUILDING_CLOCK_HAZARD_MACHINERY,
} building_clock_hazard_kind_t;

typedef struct {
    building_clock_hazard_kind_t kind;
    building_clock_rect_t area;
    uint8_t source_digit_id;
    uint8_t safe_node_count;
    uint8_t safe_node_ids[BUILDING_CLOCK_MAX_SAFE_RETREAT_NODES];
} building_clock_hazard_t;

typedef enum {
    BUILDING_CLOCK_COLON_OFF = 0,
    BUILDING_CLOCK_COLON_ON,
    BUILDING_CLOCK_COLON_PULSING,
} building_clock_colon_status_t;

typedef struct {
    building_clock_colon_status_t status;
    uint16_t phase_per_mille;
} building_clock_colon_state_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
} building_clock_date_t;

/* Mutable runtime state. It owns no generated-world geometry. */
typedef struct {
    uint64_t elapsed_ms;
    building_clock_runtime_random_streams_t random_streams;
    building_clock_digit_state_t digits[BUILDING_CLOCK_DIGIT_COUNT];
    building_clock_colon_state_t colon;
    building_clock_date_t date;

    uint8_t worker_count;
    building_clock_worker_t workers[BUILDING_CLOCK_MAX_WORKERS];
    uint8_t job_count;
    building_clock_job_t jobs[BUILDING_CLOCK_MAX_JOBS];
    uint8_t temp_structure_count;
    building_clock_temp_structure_t temp_structures[BUILDING_CLOCK_MAX_TEMP_STRUCTURES];

    building_clock_material_inventory_t depot_inventory[BUILDING_CLOCK_MAX_MATERIAL_DEPOTS];
    building_clock_material_inventory_t staging_inventory[BUILDING_CLOCK_MAX_STAGING_AREAS];
    uint8_t load_count;
    building_clock_load_t loads[BUILDING_CLOCK_MAX_LOADS];
    uint8_t debris_count;
    building_clock_debris_t debris[BUILDING_CLOCK_MAX_DEBRIS_PIECES];
    uint8_t falling_section_count;
    building_clock_falling_section_t falling_sections[BUILDING_CLOCK_MAX_FALLING_SECTIONS];

    uint8_t cart_count;
    building_clock_cart_t carts[BUILDING_CLOCK_MAX_CARTS];
    uint8_t crane_count;
    building_clock_crane_t cranes[BUILDING_CLOCK_MAX_CRANES];
    uint8_t lift_count;
    building_clock_lift_t lifts[BUILDING_CLOCK_MAX_LIFTS];
    uint8_t hazard_count;
    building_clock_hazard_t hazards[BUILDING_CLOCK_MAX_HAZARDS];
} building_clock_simulation_state_t;

#ifdef __cplusplus
}
#endif
