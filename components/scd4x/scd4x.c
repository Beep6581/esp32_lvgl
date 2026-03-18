#include "scd4x.h"

#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c.h"
#include "sensirion_i2c_hal.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "scd4x";

static uint8_t s_addr = 0;

static esp_err_t scd4x_ret_to_esp_err(int16_t ret) {
    switch (ret) {
    case NO_ERROR:
        return ESP_OK;
    case I2C_NACK_ERROR:
        return ESP_ERR_NOT_FOUND;
    case I2C_BUS_ERROR:
        return ESP_ERR_INVALID_STATE;
    case BYTE_NUM_ERROR:
        return ESP_ERR_INVALID_SIZE;
    case CRC_ERROR:
    case NOT_IMPLEMENTED_ERROR:
    default:
        return ESP_FAIL;
    }
}

esp_err_t scd4x_esp_init(uint8_t addr_7bit) {
    s_addr = addr_7bit;

    scd4x_init(addr_7bit);

    sensirion_i2c_hal_init();

    (void)scd4x_wake_up();
    vTaskDelay(pdMS_TO_TICKS(30));
    (void)scd4x_stop_periodic_measurement();
    vTaskDelay(pdMS_TO_TICKS(500));

    // Quick presence check.
    uint16_t serial[3] = {0};
    const int16_t ret = scd4x_get_serial_number(serial, 3);
    if (ret != NO_ERROR) {
        ESP_LOGW(TAG, "SCD4x presence check failed at 0x%02X (err=%d)", (unsigned)addr_7bit, (int)ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SCD4x ready at 0x%02X (serial %04X-%04X-%04X)",
             (unsigned)addr_7bit, (unsigned)serial[0], (unsigned)serial[1], (unsigned)serial[2]);

    return ESP_OK;
}

uint8_t scd4x_esp_addr(void) {
    return s_addr;
}

esp_err_t scd4x_esp_get_serial(uint16_t serial[3]) {
    if (serial == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return scd4x_ret_to_esp_err(scd4x_get_serial_number(serial, 3));
}

esp_err_t scd4x_esp_set_asc_enabled(bool enabled) {
    return scd4x_ret_to_esp_err(scd4x_set_automatic_self_calibration_enabled(enabled ? 1 : 0));
}

esp_err_t scd4x_esp_get_asc_enabled(bool* enabled) {
    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t asc = 0;
    const int16_t ret = scd4x_get_automatic_self_calibration_enabled(&asc);
    if (ret != NO_ERROR) {
        return ESP_FAIL;
    }
    *enabled = (asc != 0);
    return ESP_OK;
}

esp_err_t scd4x_esp_start_periodic(void) {
    return scd4x_ret_to_esp_err(scd4x_start_periodic_measurement());
}

esp_err_t scd4x_esp_stop_periodic(void) {
    return scd4x_ret_to_esp_err(scd4x_stop_periodic_measurement());
}

esp_err_t scd4x_esp_measure_single_shot(void) {
    return scd4x_ret_to_esp_err(scd4x_measure_single_shot());
}

esp_err_t scd4x_esp_measure_single_shot_rht_only(void) {
    return scd4x_ret_to_esp_err(scd4x_measure_single_shot_rht_only());
}

esp_err_t scd4x_esp_get_data_ready(bool* ready) {
    if (ready == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return scd4x_ret_to_esp_err(scd4x_get_data_ready_status(ready));
}

esp_err_t scd4x_esp_read_measurement(scd4x_sample_t* out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t co2 = 0;
    int32_t t_m = 0;
    int32_t rh_m = 0;

    const int16_t ret = scd4x_read_measurement(&co2, &t_m, &rh_m);
    if (ret != NO_ERROR) {
        return ESP_FAIL;
    }

    out->co2_ppm = co2;
    out->temperature_m_deg_c = t_m;
    out->humidity_m_percent_rh = rh_m;
    return ESP_OK;
}
