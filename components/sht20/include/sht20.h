#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t otp_bytes[8];
    uint16_t metal_rom_words[3];
} sht20_identity_t;

typedef struct {
    bool has_rht;
    int32_t temperature_m_deg_c;      // milli-degrees Celsius
    int32_t humidity_m_percent_rh;    // milli-%RH
} sht20_sample_t;

/**
 * Initialize the SHT20 driver for a given 7-bit I2C address.
 * Call i2c_bus_init() before this.
 */
esp_err_t sht20_init(uint8_t addr_7bit);

/** Returns the address previously set with sht20_init(), or 0 if not initialized. */
uint8_t sht20_addr(void);

/** Read the SHT20 electronic identification code (OTP + metal ROM parts). */
esp_err_t sht20_get_identity(sht20_identity_t* out_identity);

/**
 * Read temperature and humidity.
 *
 * Uses no-hold master mode commands and waits for the max measurement time.
 * Returns ESP_OK on success.
 */
esp_err_t sht20_read_rht(sht20_sample_t* out);

#ifdef __cplusplus
}
#endif
