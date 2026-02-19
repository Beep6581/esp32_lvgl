#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t co2_ppm;
    int32_t temperature_m_deg_c;      // milli-degrees Celsius
    int32_t humidity_m_percent_rh;    // milli-%RH
} scd4x_sample_t;

/**
 * Initialize the SCD4x driver for a given 7-bit I2C address.
 * Call i2c_bus_init() before this.
 */
esp_err_t scd4x_esp_init(uint8_t addr_7bit);

/** Returns the address previously set with scd4x_esp_init(). */
uint8_t scd4x_esp_addr(void);

/** Get the 48-bit serial number (3 words). */
esp_err_t scd4x_esp_get_serial(uint16_t serial[3]);

/** Enable/disable automatic self calibration (ASC). */
esp_err_t scd4x_esp_set_asc_enabled(bool enabled);

/** Start/stop periodic measurement mode. */
esp_err_t scd4x_esp_start_periodic(void);
esp_err_t scd4x_esp_stop_periodic(void);

/** Single-shot measurements (SCD41 only). */
esp_err_t scd4x_esp_measure_single_shot(void);
esp_err_t scd4x_esp_measure_single_shot_rht_only(void);

/** Poll data-ready status. */
esp_err_t scd4x_esp_get_data_ready(bool* ready);

/** Read last measurement results. */
esp_err_t scd4x_esp_read_measurement(scd4x_sample_t* out);

#ifdef __cplusplus
}
#endif
