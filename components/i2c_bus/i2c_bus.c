#include "i2c_bus.h"

#include "board.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus = NULL;
static SemaphoreHandle_t s_lock = NULL;

#define I2C_BUS_PORT I2C_NUM_0
#define I2C_BUS_GLITCH_IGNORE_CNT 7

static esp_err_t i2c_bus_lock(void) {
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void i2c_bus_unlock(void) {
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

bool i2c_bus_is_initialized(void) {
    return (s_bus != NULL);
}

i2c_master_bus_handle_t i2c_bus_get_handle(void) {
    return s_bus;
}

esp_err_t i2c_bus_init(void) {
    if (s_bus) {
        return ESP_OK;
    }

    if (BOARD_I2C_SDA_GPIO == GPIO_NUM_NC || BOARD_I2C_SCL_GPIO == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "I2C pins are not configured (SDA=%d SCL=%d)",
                 (int)BOARD_I2C_SDA_GPIO, (int)BOARD_I2C_SCL_GPIO);
        return ESP_ERR_INVALID_STATE;
    }

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_BUS_PORT,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .glitch_ignore_cnt = I2C_BUS_GLITCH_IGNORE_CNT,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_bus_lock();
    if (err != ESP_OK) {
        return err;
    }

    if (s_bus == NULL) {
        err = i2c_new_master_bus(&cfg, &s_bus);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C bus ready: port=%d SDA=%d SCL=%d",
                     (int)I2C_BUS_PORT, (int)BOARD_I2C_SDA_GPIO, (int)BOARD_I2C_SCL_GPIO);
        } else {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        }
    }

    i2c_bus_unlock();
    return err;
}

esp_err_t i2c_bus_probe(uint8_t addr_7bit, int timeout_ms) {
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(s_bus, addr_7bit, timeout_ms);
}

esp_err_t i2c_bus_scan(uint8_t* out_addrs, size_t max_addrs, size_t* out_count) {
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const int timeout_ms = 50;

    //
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        const esp_err_t err = i2c_bus_probe((uint8_t)addr, timeout_ms);
        if (err == ESP_OK) {
            if (*out_count < max_addrs && out_addrs != NULL) {
                out_addrs[*out_count] = (uint8_t)addr;
            }
            (*out_count)++;
        }
    }

    return ESP_OK;
}
