#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_co2;
    bool has_rht;

    uint16_t co2_ppm;
    int32_t temperature_m_deg_c;      // milli-degrees Celsius
    int32_t humidity_m_percent_rh;    // milli-%RH

    // Timestamp (ms since boot) when we last updated from the sensor.
    uint32_t last_co2_ms;
} air_quality_data_t;

esp_err_t air_quality_start(void);

air_quality_data_t air_quality_get_latest(void);

uint8_t air_quality_get_scd4x_addr(void);

#ifdef __cplusplus
}
#endif
