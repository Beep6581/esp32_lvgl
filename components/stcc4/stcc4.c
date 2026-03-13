#include "stcc4.h"

#include <stddef.h>

#include "i2c_bus.h"

#include "driver/i2c_master.h"

#include "esp_log.h"
#include "esp_rom_sys.h"  // esp_rom_delay_us

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "stcc4";

#define STCC4_I2C_SCL_HZ          400000
#define STCC4_I2C_TIMEOUT_MS      100

// Command words (MSB first) from STCC4 datasheet.
#define STCC4_CMD_START_CONTINUOUS_MEASUREMENT   0x218B
#define STCC4_CMD_STOP_CONTINUOUS_MEASUREMENT    0x3F86
#define STCC4_CMD_READ_MEASUREMENT               0xEC05
#define STCC4_CMD_SET_RHT_COMPENSATION           0xE000
#define STCC4_CMD_PERFORM_SELF_TEST              0x278C

static i2c_master_bus_handle_t s_bus = NULL;
static uint8_t s_addr = 0;

static uint8_t stcc4_crc8(const uint8_t* data, size_t len) {
    // CRC-8, polynomial 0x31, init 0xFF
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static esp_err_t stcc4_dev_add(i2c_master_dev_handle_t* out_dev) {
    if (s_bus == NULL || s_addr == 0 || out_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = STCC4_I2C_SCL_HZ,
    };

    return i2c_master_bus_add_device(s_bus, &cfg, out_dev);
}

static void stcc4_dev_rm(i2c_master_dev_handle_t dev) {
    if (dev) {
        (void)i2c_master_bus_rm_device(dev);
    }
}

static esp_err_t stcc4_write_cmd_u16(uint16_t cmd) {
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = stcc4_dev_add(&dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t buf[2];
    buf[0] = (uint8_t)((cmd >> 8) & 0xFF);
    buf[1] = (uint8_t)(cmd & 0xFF);

    err = i2c_master_transmit(dev, buf, sizeof(buf), STCC4_I2C_TIMEOUT_MS);
    stcc4_dev_rm(dev);
    return err;
}

static esp_err_t stcc4_read_words_with_crc(uint8_t* out, size_t out_len) {
    if (out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = stcc4_dev_add(&dev);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_receive(dev, out, out_len, STCC4_I2C_TIMEOUT_MS);
    stcc4_dev_rm(dev);
    return err;
}

static uint16_t be_u16(const uint8_t* b) {
    return (uint16_t)((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static int16_t be_i16(const uint8_t* b) {
    return (int16_t)be_u16(b);
}

static int32_t temp_raw_to_mdegc(uint16_t raw) {
    // T[°C] = 175 * raw / (2^16 - 1) - 45
    const double t_c = 175.0 * (double)raw / 65535.0 - 45.0;
    if (t_c >= 0.0) {
        return (int32_t)(t_c * 1000.0 + 0.5);
    }
    return (int32_t)(t_c * 1000.0 - 0.5);
}

static int32_t rh_raw_to_mpercent(uint16_t raw) {
    // RH[%] = 125 * raw / (2^16 - 1) - 6
    const double rh = 125.0 * (double)raw / 65535.0 - 6.0;
    if (rh >= 0.0) {
        return (int32_t)(rh * 1000.0 + 0.5);
    }
    return (int32_t)(rh * 1000.0 - 0.5);
}

static uint16_t temp_mdegc_to_raw(int32_t temperature_m_deg_c) {
    // raw = (T + 45) * (2^16 - 1) / 175
    const double t_c = (double)temperature_m_deg_c / 1000.0;
    double raw = (t_c + 45.0) * 65535.0 / 175.0;
    if (raw < 0.0) raw = 0.0;
    if (raw > 65535.0) raw = 65535.0;
    if (raw >= 0.0) {
        return (uint16_t)(raw + 0.5);
    }
    return (uint16_t)(raw - 0.5);
}

static uint16_t rh_mpercent_to_raw(int32_t humidity_m_percent_rh) {
    // raw = (RH + 6) * (2^16 - 1) / 125
    const double rh = (double)humidity_m_percent_rh / 1000.0;
    double raw = (rh + 6.0) * 65535.0 / 125.0;
    if (raw < 0.0) raw = 0.0;
    if (raw > 65535.0) raw = 65535.0;
    if (raw >= 0.0) {
        return (uint16_t)(raw + 0.5);
    }
    return (uint16_t)(raw - 0.5);
}

esp_err_t stcc4_init(uint8_t addr_7bit) {
    s_bus = i2c_bus_get_handle();
    if (s_bus == NULL) {
        ESP_LOGW(TAG, "i2c_bus not initialized yet");
        return ESP_ERR_INVALID_STATE;
    }

    s_addr = addr_7bit;
    ESP_LOGI(TAG, "init: addr=0x%02X bus=%p", (unsigned)s_addr, (void*)s_bus);
    return ESP_OK;
}

uint8_t stcc4_addr(void) {
    return s_addr;
}

esp_err_t stcc4_start_continuous_measurement(void) {
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return stcc4_write_cmd_u16(STCC4_CMD_START_CONTINUOUS_MEASUREMENT);
}

esp_err_t stcc4_stop_continuous_measurement(void) {
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = stcc4_write_cmd_u16(STCC4_CMD_STOP_CONTINUOUS_MEASUREMENT);

    // Datasheet execution time for stop command is 1200 ms.
    vTaskDelay(pdMS_TO_TICKS(1200));

    return err;
}

esp_err_t stcc4_read_measurement(stcc4_sample_t* out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Send read_measurement command.
    esp_err_t err = stcc4_write_cmd_u16(STCC4_CMD_READ_MEASUREMENT);
    if (err != ESP_OK) {
        return err;
    }

    // Command execution time is 1 ms.
    vTaskDelay(pdMS_TO_TICKS(1));

    // Read 4 words (CO2, T, RH, status), each followed by 1 CRC byte => 12 bytes.
    // If no data is available yet, the sensor responds with a NACK. The datasheet recommends
    // waiting 150 ms and retrying.
    uint8_t buf[12] = {0};

    for (int attempt = 0; attempt < 2; attempt++) {
        err = stcc4_read_words_with_crc(buf, sizeof(buf));
        if (err == ESP_OK) {
            break;
        }

        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND) {
            // Treat as "no data yet".
            if (attempt == 0) {
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }
            return ESP_ERR_NOT_FOUND;
        }

        return err;
    }

    // CRC check per 16-bit word.
    for (int i = 0; i < 4; i++) {
        const uint8_t* w = &buf[i * 3];
        const uint8_t crc = stcc4_crc8(w, 2);
        if (crc != w[2]) {
            ESP_LOGW(TAG, "CRC mismatch at word %d (got 0x%02X expected 0x%02X)", i, (unsigned)w[2], (unsigned)crc);
            return ESP_FAIL;
        }
    }

    const int16_t co2 = be_i16(&buf[0]);
    const uint16_t t_raw = be_u16(&buf[3]);
    const uint16_t rh_raw = be_u16(&buf[6]);
    const uint16_t status = be_u16(&buf[9]);

    out->has_co2 = true;
    out->has_rht = true;

    out->co2_ppm = co2;
    out->temperature_m_deg_c = temp_raw_to_mdegc(t_raw);
    out->humidity_m_percent_rh = rh_raw_to_mpercent(rh_raw);
    out->status = status;

    return ESP_OK;
}

esp_err_t stcc4_set_rht_compensation(int32_t temperature_m_deg_c,
                                    int32_t humidity_m_percent_rh) {
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint16_t t_raw = temp_mdegc_to_raw(temperature_m_deg_c);
    const uint16_t rh_raw = rh_mpercent_to_raw(humidity_m_percent_rh);

    uint8_t buf[8] = {0};

    // Command word
    buf[0] = (uint8_t)((STCC4_CMD_SET_RHT_COMPENSATION >> 8) & 0xFF);
    buf[1] = (uint8_t)(STCC4_CMD_SET_RHT_COMPENSATION & 0xFF);

    // Temperature word + CRC
    buf[2] = (uint8_t)((t_raw >> 8) & 0xFF);
    buf[3] = (uint8_t)(t_raw & 0xFF);
    buf[4] = stcc4_crc8(&buf[2], 2);

    // Humidity word + CRC
    buf[5] = (uint8_t)((rh_raw >> 8) & 0xFF);
    buf[6] = (uint8_t)(rh_raw & 0xFF);
    buf[7] = stcc4_crc8(&buf[5], 2);

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = stcc4_dev_add(&dev);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit(dev, buf, sizeof(buf), STCC4_I2C_TIMEOUT_MS);
    stcc4_dev_rm(dev);

    // Execution time is 1 ms.
    vTaskDelay(pdMS_TO_TICKS(1));

    return err;
}

esp_err_t stcc4_perform_self_test(uint16_t* out_self_test) {
    if (out_self_test == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Send self test command.
    esp_err_t err = stcc4_write_cmd_u16(STCC4_CMD_PERFORM_SELF_TEST);
    if (err != ESP_OK) {
        return err;
    }

    // Execution time is 360 ms.
    vTaskDelay(pdMS_TO_TICKS(360));

    // Read one word + CRC (3 bytes).
    uint8_t buf[3] = {0};
    err = stcc4_read_words_with_crc(buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    const uint8_t crc = stcc4_crc8(buf, 2);
    if (crc != buf[2]) {
        return ESP_FAIL;
    }

    *out_self_test = be_u16(buf);
    return ESP_OK;
}
