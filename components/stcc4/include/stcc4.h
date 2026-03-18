#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t product_id;
    uint16_t serial_words[4];
} stcc4_identity_t;

typedef struct {
    bool has_co2;
    bool has_rht;

    int16_t co2_ppm;

    int32_t temperature_m_deg_c;      // milli-degrees Celsius
    int32_t humidity_m_percent_rh;    // milli-%RH

    uint16_t status;
} stcc4_sample_t;

/**
 * Initialize the STCC4 driver.
 *
 * This does not start measurements.
 */
esp_err_t stcc4_init(uint8_t addr_7bit);

/** Returns the current STCC4 7-bit address (0 if not initialized). */
uint8_t stcc4_addr(void);

/** Read product ID + serial number. Must be called while the sensor is idle. */
esp_err_t stcc4_get_identity(stcc4_identity_t* out_identity);

/**
 * Start continuous measurement.
 *
 * The sampling interval is 1 second.
 */
esp_err_t stcc4_start_continuous_measurement(void);

/** Stop continuous measurement (blocks for the sensor execution time). */
esp_err_t stcc4_stop_continuous_measurement(void);

/**
 * Read one measurement sample.
 *
 * Returns ESP_OK if data was read and CRC checks passed.
 * Returns ESP_ERR_NOT_FOUND if the sensor NACKed because no data is available yet.
 */
esp_err_t stcc4_read_measurement(stcc4_sample_t* out);

/**
 * Provide temperature + humidity compensation values to STCC4.
 *
 * Only use this when the STCC4 is NOT controlling an SHT4x via its dedicated
 * controller interface (SDA_C/SCL_C).
 */
esp_err_t stcc4_set_rht_compensation(int32_t temperature_m_deg_c,
                                    int32_t humidity_m_percent_rh);

/**
 * Perform on-chip self test.
 *
 * Returns a 16-bit self test word (see datasheet).
 */
esp_err_t stcc4_perform_self_test(uint16_t* out_self_test);

#ifdef __cplusplus
}
#endif
