#include <sensirion_i2c_hal.h>

#include <sensirion_common.h>
#include <sensirion_i2c.h>

#include "i2c_bus.h"

#include "driver/i2c_master.h"

#include "esp_log.h"
#include "esp_rom_sys.h" // esp_rom_delay_us

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "sensirion_i2c_hal";

// Per-device speed setting (SCD4x supports up to 400 kHz).
#define SENSIRION_I2C_SCL_HZ 400000

// Default transfer timeout for a single I2C transaction.
#define SENSIRION_I2C_TIMEOUT_MS 100

static i2c_master_bus_handle_t s_bus = NULL;

int16_t sensirion_i2c_hal_select_bus(uint8_t bus_idx) {
    // Single-bus setup. If the app requests any other bus index, we can't do it.
    return (bus_idx == 0) ? NO_ERROR : NOT_IMPLEMENTED_ERROR;
}

void sensirion_i2c_hal_init(void) {
    s_bus = i2c_bus_get_handle();
    if (s_bus == NULL) {
        ESP_LOGW(TAG, "i2c_bus not initialized yet");
    }
}

void sensirion_i2c_hal_free(void) {
    s_bus = NULL;
}

static int8_t map_esp_err_to_sensirion(esp_err_t err) {
    if (err == ESP_OK) {
        return NO_ERROR;
    }

    // ESP-IDF uses ESP_ERR_NOT_FOUND for NACK in i2c_master_probe().
    if (err == ESP_ERR_NOT_FOUND) {
        return I2C_NACK_ERROR;
    }

    return I2C_BUS_ERROR;
}

static int8_t i2c_temp_device_add(uint8_t address, i2c_master_dev_handle_t* out_dev) {
    if (s_bus == NULL || out_dev == NULL) {
        return I2C_BUS_ERROR;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = SENSIRION_I2C_SCL_HZ,
    };

    i2c_master_dev_handle_t dev = NULL;
    const esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return map_esp_err_to_sensirion(err);
    }

    *out_dev = dev;
    return NO_ERROR;
}

static void i2c_temp_device_remove(i2c_master_dev_handle_t dev) {
    if (dev) {
        (void)i2c_master_bus_rm_device(dev);
    }
}

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t* data, uint8_t count) {
    if (data == NULL || count == 0) {
        return I2C_BUS_ERROR;
    }

    i2c_master_dev_handle_t dev = NULL;
    int8_t ret = i2c_temp_device_add(address, &dev);
    if (ret != NO_ERROR) {
        return ret;
    }

    const esp_err_t err = i2c_master_receive(dev, data, count, SENSIRION_I2C_TIMEOUT_MS);
    i2c_temp_device_remove(dev);
    return map_esp_err_to_sensirion(err);
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t* data, uint8_t count) {
    if (data == NULL || count == 0) {
        return I2C_BUS_ERROR;
    }

    i2c_master_dev_handle_t dev = NULL;
    int8_t ret = i2c_temp_device_add(address, &dev);
    if (ret != NO_ERROR) {
        return ret;
    }

    const esp_err_t err = i2c_master_transmit(dev, data, count, SENSIRION_I2C_TIMEOUT_MS);
    i2c_temp_device_remove(dev);
    return map_esp_err_to_sensirion(err);
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds) {
    // vTaskDelay() is ms based. For short delays we use esp_rom_delay_us().
    if (useconds >= 1000) {
        const TickType_t ticks = pdMS_TO_TICKS(useconds / 1000);
        if (ticks > 0) {
            vTaskDelay(ticks);
        }
        useconds = useconds % 1000;
    }

    if (useconds > 0) {
        esp_rom_delay_us(useconds);
    }
}
