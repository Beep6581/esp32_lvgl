/* Version: 2026-10-05 */

#include "particle_engine.h"

#include <limits.h>

#include "esp_heap_caps.h"

#define FLAME_PARTICLE_COUNT 1500U
#define PARTICLE_POSITION_FRACTION_BITS 8
#define PARTICLE_POSITION_SCALE (1 << PARTICLE_POSITION_FRACTION_BITS)
#define MINUTE_CHANGE_DISINTEGRATE_MS 1000U
#define MINUTE_CHANGE_TOTAL_MS 3000U
#define COLON_PARTICLE_EVERY_N 12U
#define FLAME_BODY_CELL_SIZE_PX 4U
#define FLAME_FINE_DETAIL_SIZE_PX 1U
#define FLAME_DETAIL_SIZE_PX 2U
#define FLAME_ACCENT_SIZE_PX 8U
#define FLAME_FINE_DETAIL_EVERY_N 3U
#define RISING_SPARK_COUNT 16U
#define FLAME_DETAIL_SPRITE_COUNT 28U
#define FLAME_ACCENT_SPRITE_COUNT 6U
#define FLAME_ACCENT_PARTICLE_STRIDE 17U
#define FLAME_DETAIL_PARTICLE_START 131U
#define FLAME_DETAIL_PARTICLE_STRIDE 29U
#define FLAME_SCENE_SPRITE_CAPACITY (RISING_SPARK_COUNT + FLAME_DETAIL_SPRITE_COUNT + FLAME_ACCENT_SPRITE_COUNT)
#define RISING_SPARK_EMIT_INTERVAL_MS 125U

typedef enum {
    PARTICLE_MODE_FLAME,
    PARTICLE_MODE_DISINTEGRATE,
    PARTICLE_MODE_ATTRACT,
} particle_mode_t;

typedef struct {
    int32_t x;
    int32_t y;
    int16_t velocity_x;
    int16_t velocity_y;
    int16_t target_x;
    int16_t target_y;
    uint16_t age_ms;
    uint16_t life_ms;
    uint8_t phase;
    uint8_t phase_step;
    bool colon;
} particle_t;

typedef struct {
    int32_t x;
    int32_t y;
    int16_t velocity_x;
    int16_t velocity_y;
    uint16_t age_ms;
    uint16_t life_ms;
    uint8_t size;
    bool active;
} flame_spark_t;

struct particle_engine {
    particle_t particles[FLAME_PARTICLE_COUNT];
    flame_spark_t sparks[RISING_SPARK_COUNT];
    clock_scene_sprite_t scene_sprites[FLAME_SCENE_SPRITE_CAPACITY];
    clock_mask_t mask;
    uint8_t* heat;
    uint16_t palette[UINT8_MAX + 1U];
    uint32_t random_state;
    uint32_t transition_elapsed_ms;
    uint32_t spark_emit_elapsed_ms;
    uint16_t width;
    uint16_t height;
    uint16_t heat_width;
    uint16_t heat_height;
    size_t scene_sprite_count;
    uint8_t colon_flare;
    particle_mode_t mode;
    bool mask_ready;
};

static uint16_t flame_color(uint8_t level);
static void compose_scene(particle_engine_t* engine);

static uint32_t particle_random(particle_engine_t* engine) {
    uint32_t value = engine->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    engine->random_state = value;
    return value;
}

static int32_t random_range(particle_engine_t* engine, int32_t minimum, int32_t maximum) {
    const uint32_t span = (uint32_t)(maximum - minimum + 1);
    return minimum + (int32_t)(particle_random(engine) % span);
}

static bool mask_bounds_valid(const clock_mask_t* mask, bool colon) {
    if (mask == NULL || mask->alpha == NULL || mask->width == 0 || mask->height == 0) {
        return false;
    }
    if (colon) {
        return mask->colon_x1 <= mask->colon_x2 && mask->colon_y1 <= mask->colon_y2;
    }
    return mask->x1 <= mask->x2 && mask->y1 <= mask->y2;
}

static void sample_mask(particle_engine_t* engine, bool colon, int16_t* x, int16_t* y) {
    const clock_mask_t* mask = &engine->mask;
    if (!mask_bounds_valid(mask, colon)) {
        *x = engine->width / 2;
        *y = engine->height / 2;
        return;
    }

    const uint16_t x1 = colon ? mask->colon_x1 : mask->x1;
    const uint16_t y1 = colon ? mask->colon_y1 : mask->y1;
    const uint16_t x2 = colon ? mask->colon_x2 : mask->x2;
    const uint16_t y2 = colon ? mask->colon_y2 : mask->y2;

    for (uint32_t attempt = 0; attempt < 128U; attempt++) {
        const uint16_t candidate_x = (uint16_t)random_range(engine, x1, x2);
        const uint16_t candidate_y = (uint16_t)random_range(engine, y1, y2);
        const uint8_t alpha = mask->alpha[candidate_y * mask->stride + candidate_x];
        if (alpha != 0 && (particle_random(engine) & 0xffU) <= alpha) {
            *x = (int16_t)candidate_x;
            *y = (int16_t)candidate_y;
            return;
        }
    }

    *x = (int16_t)((x1 + x2) / 2U);
    *y = (int16_t)((y1 + y2) / 2U);
}

static void sample_mask_upper_edge(particle_engine_t* engine, int16_t* x, int16_t* y) {
    const clock_mask_t* mask = &engine->mask;
    for (uint32_t attempt = 0; attempt < 256U; attempt++) {
        const uint16_t candidate_x = (uint16_t)random_range(engine, mask->x1, mask->x2);
        const uint16_t candidate_y = (uint16_t)random_range(engine, mask->y1, mask->y2);
        const uint8_t alpha = mask->alpha[candidate_y * mask->stride + candidate_x];
        const int32_t above_y = (int32_t)candidate_y - 2 * FLAME_BODY_CELL_SIZE_PX;
        const bool open_above = above_y < 0 || mask->alpha[(uint32_t)above_y * mask->stride + candidate_x] == 0U;

        if (alpha != 0U && open_above && (particle_random(engine) & 0xffU) <= alpha) {
            *x = (int16_t)candidate_x;
            *y = (int16_t)candidate_y;
            return;
        }
    }

    sample_mask(engine, false, x, y);
}

static void set_particle_target(particle_engine_t* engine, particle_t* particle, size_t index) {
    particle->colon = (index % COLON_PARTICLE_EVERY_N) == 0U;
    sample_mask(engine, particle->colon, &particle->target_x, &particle->target_y);
}

static void set_flame_velocity(particle_engine_t* engine, particle_t* particle) {
    particle->velocity_x = (int16_t)(random_range(engine, -8, 8) * PARTICLE_POSITION_SCALE);
    particle->velocity_y = (int16_t)(-random_range(engine, 14, 42) * PARTICLE_POSITION_SCALE);
    particle->phase = (uint8_t)particle_random(engine);
    particle->phase_step = (uint8_t)random_range(engine, 2, 6);
}

static void reset_flame_particle(particle_engine_t* engine, particle_t* particle, size_t index, bool spread_age) {
    set_particle_target(engine, particle, index);
    particle->life_ms = (uint16_t)random_range(engine, 750, 1450);
    particle->age_ms = spread_age ? (uint16_t)(particle_random(engine) % particle->life_ms) : 0;
    set_flame_velocity(engine, particle);

    particle->x = particle->target_x * PARTICLE_POSITION_SCALE + random_range(engine, -2, 2) * PARTICLE_POSITION_SCALE;
    particle->y = particle->target_y * PARTICLE_POSITION_SCALE;
    if (spread_age) {
        particle->x += (int32_t)particle->velocity_x * particle->age_ms / 1000;
        particle->y += (int32_t)particle->velocity_y * particle->age_ms / 1000;
    }
}

particle_engine_t* particle_engine_create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0 || width > INT16_MAX || height > INT16_MAX) {
        return NULL;
    }

    particle_engine_t* engine = heap_caps_calloc(1, sizeof(*engine), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (engine == NULL) {
        return NULL;
    }
    engine->width = width;
    engine->height = height;
    engine->heat_width = (width + FLAME_BODY_CELL_SIZE_PX - 1U) / FLAME_BODY_CELL_SIZE_PX;
    engine->heat_height = (height + FLAME_BODY_CELL_SIZE_PX - 1U) / FLAME_BODY_CELL_SIZE_PX;
    const size_t cell_count = (size_t)engine->heat_width * engine->heat_height;
    engine->heat = heap_caps_calloc(cell_count, sizeof(*engine->heat), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (engine->heat == NULL) {
        heap_caps_free(engine);
        return NULL;
    }
    for (uint16_t level = 0; level <= UINT8_MAX; level++) {
        engine->palette[level] = flame_color((uint8_t)level);
    }
    engine->random_state = 0x6d2b79f5U;
    return engine;
}

void particle_engine_destroy(particle_engine_t* engine) {
    if (engine == NULL) {
        return;
    }
    heap_caps_free(engine->heat);
    heap_caps_free(engine);
}

void particle_engine_set_mask(particle_engine_t* engine, const clock_mask_t* mask, bool transition) {
    if (engine == NULL || !mask_bounds_valid(mask, false)) {
        return;
    }

    engine->mask = *mask;
    engine->mask_ready = true;
    engine->colon_flare = 255;

    if (!transition) {
        engine->mode = PARTICLE_MODE_FLAME;
        engine->transition_elapsed_ms = 0;
        for (size_t index = 0; index < FLAME_PARTICLE_COUNT; index++) {
            reset_flame_particle(engine, &engine->particles[index], index, true);
        }
        return;
    }

    engine->mode = PARTICLE_MODE_DISINTEGRATE;
    engine->transition_elapsed_ms = 0;
    for (size_t index = 0; index < FLAME_PARTICLE_COUNT; index++) {
        particle_t* particle = &engine->particles[index];
        set_particle_target(engine, particle, index);
        particle->velocity_x = (int16_t)(random_range(engine, -75, 75) * PARTICLE_POSITION_SCALE);
        particle->velocity_y = (int16_t)(random_range(engine, -35, 12) * PARTICLE_POSITION_SCALE);
    }
}

void particle_engine_pulse_colon(particle_engine_t* engine) {
    if (engine != NULL) {
        engine->colon_flare = 255;
    }
}

static void update_flame(particle_engine_t* engine, particle_t* particle, size_t index, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    particle->age_ms = (uint16_t)(particle->age_ms + elapsed_ms);
    if (particle->age_ms >= particle->life_ms || particle->y < -8 * PARTICLE_POSITION_SCALE) {
        reset_flame_particle(engine, particle, index, false);
        return;
    }

    particle->phase = (uint8_t)(particle->phase + particle->phase_step);
    const int16_t turbulence = particle->phase < 128U ? (int16_t)particle->phase - 64 : 191 - (int16_t)particle->phase;
    const int32_t target_x = particle->target_x * PARTICLE_POSITION_SCALE;
    particle->velocity_x += (int16_t)(((target_x - particle->x) * (int32_t)elapsed_ms) / 8000);
    particle->velocity_x += (int16_t)(turbulence * (int32_t)elapsed_ms / 12);
    particle->velocity_x = (int16_t)((int32_t)particle->velocity_x * 245 / 256);

    particle->x += (int32_t)particle->velocity_x * elapsed / 1000;
    particle->y += (int32_t)particle->velocity_y * elapsed / 1000;
}

static void update_disintegrate(particle_engine_t* engine, particle_t* particle, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    particle->velocity_y += (int16_t)(150 * PARTICLE_POSITION_SCALE * elapsed / 1000);
    particle->x += (int32_t)particle->velocity_x * elapsed / 1000;
    particle->y += (int32_t)particle->velocity_y * elapsed / 1000;

    if (particle->x < -16 * PARTICLE_POSITION_SCALE || particle->x > (engine->width + 16) * PARTICLE_POSITION_SCALE) {
        particle->velocity_x = (int16_t)-particle->velocity_x;
    }
}

static void update_attract(particle_engine_t* engine, particle_t* particle, uint32_t elapsed_ms) {
    const int32_t target_x = particle->target_x * PARTICLE_POSITION_SCALE;
    const int32_t target_y = particle->target_y * PARTICLE_POSITION_SCALE;
    particle->phase = (uint8_t)(particle->phase + particle->phase_step);
    const int16_t turbulence = particle->phase < 128U ? (int16_t)particle->phase - 64 : 191 - (int16_t)particle->phase;

    particle->x += (target_x - particle->x) * (int32_t)elapsed_ms / 330;
    particle->y += (target_y - particle->y) * (int32_t)elapsed_ms / 330;
    particle->x += turbulence * (int32_t)elapsed_ms / 10;
    (void)engine;
}

static uint8_t random_spark_size(particle_engine_t* engine) {
    const uint32_t roll = particle_random(engine) % 8U;
    if (roll < 2U) {
        return FLAME_FINE_DETAIL_SIZE_PX;
    }
    if (roll == 7U) {
        return FLAME_BODY_CELL_SIZE_PX;
    }
    return FLAME_DETAIL_SIZE_PX;
}

static void spark_emit(particle_engine_t* engine) {
    const size_t first = particle_random(engine) % RISING_SPARK_COUNT;
    for (size_t offset = 0; offset < RISING_SPARK_COUNT; offset++) {
        flame_spark_t* spark = &engine->sparks[(first + offset) % RISING_SPARK_COUNT];
        if (spark->active) {
            continue;
        }

        int16_t x;
        int16_t y;
        sample_mask_upper_edge(engine, &x, &y);
        const uint8_t size = random_spark_size(engine);
        *spark = (flame_spark_t){
            .x = x * PARTICLE_POSITION_SCALE,
            .y = y * PARTICLE_POSITION_SCALE,
            .velocity_x = (int16_t)(random_range(engine, -8, 8) * PARTICLE_POSITION_SCALE),
            .velocity_y = (int16_t)(-random_range(engine, 75, 105) * PARTICLE_POSITION_SCALE),
            .life_ms = (uint16_t)random_range(engine, 900, 1400),
            .size = size,
            .active = true,
        };
        return;
    }
}

static void update_sparks(particle_engine_t* engine, uint32_t elapsed_ms) {
    const int32_t elapsed = (int32_t)elapsed_ms;
    for (size_t index = 0; index < RISING_SPARK_COUNT; index++) {
        flame_spark_t* spark = &engine->sparks[index];
        if (!spark->active) {
            continue;
        }

        spark->age_ms = (uint16_t)(spark->age_ms + elapsed_ms);
        if (spark->age_ms >= spark->life_ms || spark->y < -(int32_t)FLAME_BODY_CELL_SIZE_PX * PARTICLE_POSITION_SCALE) {
            spark->active = false;
            continue;
        }
        spark->x += (int32_t)spark->velocity_x * elapsed / 1000;
        spark->y += (int32_t)spark->velocity_y * elapsed / 1000;
    }

    if (engine->mode != PARTICLE_MODE_FLAME) {
        engine->spark_emit_elapsed_ms = 0;
        return;
    }

    engine->spark_emit_elapsed_ms += elapsed_ms;
    while (engine->spark_emit_elapsed_ms >= RISING_SPARK_EMIT_INTERVAL_MS) {
        spark_emit(engine);
        engine->spark_emit_elapsed_ms -= RISING_SPARK_EMIT_INTERVAL_MS;
    }
}

void particle_engine_update(particle_engine_t* engine, uint32_t elapsed_ms) {
    if (engine == NULL || !engine->mask_ready) {
        return;
    }
    if (elapsed_ms > 80U) {
        elapsed_ms = 80U;
    }

    if (engine->colon_flare > elapsed_ms / 2U) {
        engine->colon_flare -= (uint8_t)(elapsed_ms / 2U);
    } else {
        engine->colon_flare = 0;
    }

    if (engine->mode != PARTICLE_MODE_FLAME) {
        engine->transition_elapsed_ms += elapsed_ms;
        if (engine->mode == PARTICLE_MODE_DISINTEGRATE && engine->transition_elapsed_ms >= MINUTE_CHANGE_DISINTEGRATE_MS) {
            engine->mode = PARTICLE_MODE_ATTRACT;
        }
    }

    for (size_t index = 0; index < FLAME_PARTICLE_COUNT; index++) {
        particle_t* particle = &engine->particles[index];
        if (engine->mode == PARTICLE_MODE_FLAME) {
            update_flame(engine, particle, index, elapsed_ms);
        } else if (engine->mode == PARTICLE_MODE_DISINTEGRATE) {
            update_disintegrate(engine, particle, elapsed_ms);
        } else {
            update_attract(engine, particle, elapsed_ms);
        }
    }

    if (engine->mode == PARTICLE_MODE_ATTRACT && engine->transition_elapsed_ms >= MINUTE_CHANGE_TOTAL_MS) {
        engine->mode = PARTICLE_MODE_FLAME;
        for (size_t index = 0; index < FLAME_PARTICLE_COUNT; index++) {
            particle_t* particle = &engine->particles[index];
            particle->age_ms = (uint16_t)(particle_random(engine) % 260U);
            particle->life_ms = (uint16_t)random_range(engine, 750, 1450);
            set_flame_velocity(engine, particle);
        }
    }
    update_sparks(engine, elapsed_ms);
    compose_scene(engine);
}

static uint16_t flame_color(uint8_t level) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    if (level < 85U) {
        red = level * 3U;
        green = level / 5U;
        blue = 0;
    } else if (level < 180U) {
        red = 255;
        green = (uint8_t)((level - 85U) * 2U);
        blue = (uint8_t)((level - 85U) / 8U);
    } else {
        red = 255;
        green = (uint8_t)(190U + (level - 180U) * 65U / 75U);
        blue = (uint8_t)(12U + (level - 180U) * 185U / 75U);
    }

    return (uint16_t)(((uint16_t)(red >> 3) << 11) | ((uint16_t)(green >> 2) << 5) | (blue >> 3));
}

static uint8_t particle_level(const particle_engine_t* engine, const particle_t* particle) {
    uint32_t level;
    if (engine->mode == PARTICLE_MODE_DISINTEGRATE) {
        level = 210U - (engine->transition_elapsed_ms * 100U / MINUTE_CHANGE_DISINTEGRATE_MS);
    } else if (engine->mode == PARTICLE_MODE_ATTRACT) {
        level = 130U + (engine->transition_elapsed_ms - MINUTE_CHANGE_DISINTEGRATE_MS) * 110U /
                              (MINUTE_CHANGE_TOTAL_MS - MINUTE_CHANGE_DISINTEGRATE_MS);
    } else {
        const uint32_t progress = (uint32_t)particle->age_ms * 255U / particle->life_ms;
        if (progress < 42U) {
            level = 135U + progress * 120U / 42U;
        } else {
            level = 255U - (progress - 42U) * 180U / 213U;
        }
    }

    if (particle->colon && engine->colon_flare > 0U) {
        level += engine->colon_flare / 2U;
    }
    if (level > 255U) {
        level = 255U;
    }
    return (uint8_t)level;
}

static void heat_add(particle_engine_t* engine, int32_t x, int32_t y, uint8_t level) {
    if (x < 0 || y < 0 || x >= engine->width || y >= engine->height) {
        return;
    }

    uint8_t* heat = &engine->heat[(uint32_t)(y / FLAME_BODY_CELL_SIZE_PX) * engine->heat_width + (uint32_t)(x / FLAME_BODY_CELL_SIZE_PX)];
    if (level > *heat) {
        *heat = level;
    } else {
        const uint16_t combined = *heat + level / 24U;
        *heat = combined > UINT8_MAX ? UINT8_MAX : (uint8_t)combined;
    }
}

static void heat_decay(particle_engine_t* engine) {
    const size_t cell_count = (size_t)engine->heat_width * engine->heat_height;
    for (size_t index = 0; index < cell_count; index++) {
        const uint8_t heat = engine->heat[index];
        engine->heat[index] = heat > 18U ? heat - 18U : 0U;
    }
}

static void compose_scene(particle_engine_t* engine) {
    heat_decay(engine);
    for (size_t index = 0; index < FLAME_PARTICLE_COUNT; index++) {
        const particle_t* particle = &engine->particles[index];
        const int32_t x = particle->x >> PARTICLE_POSITION_FRACTION_BITS;
        const int32_t y = particle->y >> PARTICLE_POSITION_FRACTION_BITS;
        const uint8_t level = particle_level(engine, particle);
        heat_add(engine, x, y, level);
        if (level > 150U) {
            heat_add(engine, x - FLAME_BODY_CELL_SIZE_PX, y, level / 3U);
            heat_add(engine, x + FLAME_BODY_CELL_SIZE_PX, y, level / 3U);
        }
    }

    engine->scene_sprite_count = 0U;
    /* Detail sprites overlay the heat grid; they never cut holes in the flame body. */
    for (size_t index = 0; index < FLAME_ACCENT_SPRITE_COUNT; index++) {
        const size_t particle_index = index * FLAME_ACCENT_PARTICLE_STRIDE % FLAME_PARTICLE_COUNT;
        const particle_t* particle = &engine->particles[particle_index];
        engine->scene_sprites[engine->scene_sprite_count++] = (clock_scene_sprite_t){
            .x = (int16_t)((particle->x >> PARTICLE_POSITION_FRACTION_BITS) - FLAME_ACCENT_SIZE_PX / 2U),
            .y = (int16_t)((particle->y >> PARTICLE_POSITION_FRACTION_BITS) - FLAME_ACCENT_SIZE_PX / 2U),
            .width = FLAME_ACCENT_SIZE_PX,
            .height = FLAME_ACCENT_SIZE_PX,
            .color = flame_color(particle_level(engine, particle)),
        };
    }
    for (size_t index = 0; index < FLAME_DETAIL_SPRITE_COUNT; index++) {
        const size_t particle_index = (FLAME_DETAIL_PARTICLE_START + index * FLAME_DETAIL_PARTICLE_STRIDE) % FLAME_PARTICLE_COUNT;
        const particle_t* particle = &engine->particles[particle_index];
        const uint8_t size = index % FLAME_FINE_DETAIL_EVERY_N == 0U ? FLAME_FINE_DETAIL_SIZE_PX : FLAME_DETAIL_SIZE_PX;
        engine->scene_sprites[engine->scene_sprite_count++] = (clock_scene_sprite_t){
            .x = (int16_t)((particle->x >> PARTICLE_POSITION_FRACTION_BITS) - size / 2U),
            .y = (int16_t)((particle->y >> PARTICLE_POSITION_FRACTION_BITS) - size / 2U),
            .width = size,
            .height = size,
            .color = flame_color(particle_level(engine, particle)),
        };
    }
    for (size_t index = 0; index < RISING_SPARK_COUNT; index++) {
        const flame_spark_t* spark = &engine->sparks[index];
        if (spark->active) {
            const uint32_t progress = (uint32_t)spark->age_ms * 255U / spark->life_ms;
            engine->scene_sprites[engine->scene_sprite_count++] = (clock_scene_sprite_t){
                .x = (int16_t)(spark->x >> PARTICLE_POSITION_FRACTION_BITS),
                .y = (int16_t)(spark->y >> PARTICLE_POSITION_FRACTION_BITS),
                .width = spark->size,
                .height = spark->size,
                .color = flame_color((uint8_t)(230U - progress * 80U / 255U)),
            };
        }
    }
}

void particle_engine_get_scene(const particle_engine_t* engine, clock_scene_t* scene) {
    if (engine == NULL || scene == NULL) {
        return;
    }

    *scene = (clock_scene_t){
        .cells = engine->heat,
        .palette = engine->palette,
        .sprites = engine->scene_sprites,
        .sprite_count = engine->scene_sprite_count,
        .sprite_capacity = FLAME_SCENE_SPRITE_CAPACITY,
        .cell_columns = engine->heat_width,
        .cell_rows = engine->heat_height,
        .cell_stride = engine->heat_width,
        .cell_size = FLAME_BODY_CELL_SIZE_PX,
    };
}

size_t particle_engine_count(const particle_engine_t* engine) {
    return engine == NULL ? 0 : FLAME_PARTICLE_COUNT;
}

size_t particle_engine_spark_count(const particle_engine_t* engine) {
    if (engine == NULL) {
        return 0;
    }

    size_t count = 0;
    for (size_t index = 0; index < RISING_SPARK_COUNT; index++) {
        if (engine->sparks[index].active) {
            count++;
        }
    }
    return count;
}

size_t particle_engine_memory_size(const particle_engine_t* engine) {
    if (engine == NULL) {
        return 0;
    }
    return sizeof(*engine) + (size_t)engine->heat_width * engine->heat_height;
}
