/* Version: 2026-10-05 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clock_mask.h"
#include "clock_scene.h"

typedef struct particle_engine particle_engine_t;

particle_engine_t* particle_engine_create(uint16_t width, uint16_t height);
void particle_engine_destroy(particle_engine_t* engine);
void particle_engine_set_mask(particle_engine_t* engine, const clock_mask_t* mask, bool transition);
void particle_engine_pulse_colon(particle_engine_t* engine);
void particle_engine_update(particle_engine_t* engine, uint32_t elapsed_ms);
void particle_engine_get_scene(const particle_engine_t* engine, clock_scene_t* scene);
size_t particle_engine_count(const particle_engine_t* engine);
size_t particle_engine_spark_count(const particle_engine_t* engine);
size_t particle_engine_memory_size(const particle_engine_t* engine);
