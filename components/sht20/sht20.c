#include "sht20.h"

#include <stddef.h>

#include "i2c_bus.h"

#include "driver/i2c_master.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHT20_I2C_SCL_HZ          100000
#define SHT20_I2C_TIMEOUT_MS      100

// No-hold master mode commands (datasheet Table 6).
#define CMD_TRIG_T_NOHOLD         0xF3
#define CMD_TRIG_RH_NOHOLD        0xF5
#define CMD_SOFT_RESET            0xFE
#define CMD_READ_OTP_PREFIX       0xFA
#define CMD_READ_OTP_ADDR_ID      0x0F
#define CMD_READ_METAL_ROM        0xFCC9

// Worst-case measurement times from datasheet Table 7 (ms).
#define T_MEAS_MS_MAX             85
#define RH_MEAS_MS_MAX            29

static const char* TAG = "sht20";

static i2c_master_bus_handle_t s_bus = NULL;
static uint8_t s_addr = 0;

static uint8_t sht20_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
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

static esp_err_t i2c_temp_device_add(uint8_t address, i2c_master_dev_handle_t* out_dev) {
    if (s_bus == NULL || out_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = SHT20_I2C_SCL_HZ,
    };

    return i2c_master_bus_add_device(s_bus, &dev_cfg, out_dev);
}

static void i2c_temp_device_remove(i2c_master_dev_handle_t dev) {
    if (dev) {
        (void)i2c_master_bus_rm_device(dev);
    }
}

static esp_err_t sht20_transmit(const uint8_t* tx, size_t tx_len) {
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_temp_device_add(s_addr, &dev);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_transmit(dev, tx, tx_len, SHT20_I2C_TIMEOUT_MS);
    i2c_temp_device_remove(dev);
    return err;
}

static esp_err_t sht20_receive(uint8_t* rx, size_t rx_len) {
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_temp_device_add(s_addr, &dev);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_receive(dev, rx, rx_len, SHT20_I2C_TIMEOUT_MS);
    i2c_temp_device_remove(dev);
    return err;
}

static esp_err_t sht20_write_cmd(uint8_t cmd) {
    return sht20_transmit(&cmd, 1);
}

static esp_err_t sht20_read_3(uint8_t out3[3]) {
    return sht20_receive(out3, 3);
}

static int32_t convert_temp_mdeg_c(uint16_t raw) {
    raw &= 0xFFFC;
    int64_t num = (int64_t)175720 * (int64_t)raw + 32768;
    int32_t t = (int32_t)(num / 65536) - 46850;
    return t;
}

static int32_t convert_rh_mpercent(uint16_t raw) {
    raw &= 0xFFFC;
    int64_t num = (int64_t)125000 * (int64_t)raw + 32768;
    int32_t rh = (int32_t)(num / 65536) - 6000;
    if (rh < 0) {
        rh = 0;
    }
    if (rh > 100000) {
        rh = 100000;
    }
    return rh;
}

esp_err_t sht20_init(uint8_t addr_7bit) {
    s_bus = i2c_bus_get_handle();
    if (s_bus == NULL) {
        ESP_LOGW(TAG, "i2c_bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_addr = addr_7bit;

    (void)sht20_write_cmd(CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

uint8_t sht20_addr(void) {
    return s_addr;
}

esp_err_t sht20_get_identity(sht20_identity_t* out_identity) {
    if (out_identity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t otp_cmd[2] = { CMD_READ_OTP_PREFIX, CMD_READ_OTP_ADDR_ID };
    esp_err_t err = sht20_transmit(otp_cmd, sizeof(otp_cmd));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t otp_raw[16] = {0};
    err = sht20_receive(otp_raw, sizeof(otp_raw));
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 8; i++) {
        const uint8_t data_byte = otp_raw[i * 2 + 0];
        const uint8_t crc_byte = otp_raw[i * 2 + 1];
        const uint8_t crc = sht20_crc8(&data_byte, 1);
        if (crc != crc_byte) {
            return ESP_FAIL;
        }
        out_identity->otp_bytes[i] = data_byte;
    }

    uint8_t rom_cmd[2] = {
        (uint8_t)((CMD_READ_METAL_ROM >> 8) & 0xFF),
        (uint8_t)(CMD_READ_METAL_ROM & 0xFF),
    };
    err = sht20_transmit(rom_cmd, sizeof(rom_cmd));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t rom_raw[9] = {0};
    err = sht20_receive(rom_raw, sizeof(rom_raw));
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 3; i++) {
        const uint8_t* w = &rom_raw[i * 3];
        const uint8_t crc = sht20_crc8(w, 2);
        if (crc != w[2]) {
            return ESP_FAIL;
        }
        out_identity->metal_rom_words[i] = (uint16_t)((w[0] << 8) | w[1]);
    }

    return ESP_OK;
}

esp_err_t sht20_read_rht(sht20_sample_t* out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[3] = {0};

    esp_err_t err = sht20_write_cmd(CMD_TRIG_T_NOHOLD);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(T_MEAS_MS_MAX));
    err = sht20_read_3(buf);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t t_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    const int32_t t_m = convert_temp_mdeg_c(t_raw);

    err = sht20_write_cmd(CMD_TRIG_RH_NOHOLD);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(RH_MEAS_MS_MAX));
    err = sht20_read_3(buf);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t rh_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    const int32_t rh_m = convert_rh_mpercent(rh_raw);

    out->has_rht = true;
    out->temperature_m_deg_c = t_m;
    out->humidity_m_percent_rh = rh_m;
    return ESP_OK;
}
