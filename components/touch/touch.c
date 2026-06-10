#include "touch.h"

#include "board.h"
#include "i2c_bus.h"
#include "ui.h"

#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define FT6336U_I2C_CLK_HZ (400000)
#define TOUCH_POLL_PERIOD_MS (50)
#define TOUCH_TASK_STACK_SIZE (4096)
#define TOUCH_TASK_PRIORITY (5)

static const char* TAG = "touch";

static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static TaskHandle_t s_touch_task = NULL;

static void touch_task(void* arg) {
    (void)arg;

    while (true) {
        esp_err_t err = esp_lcd_touch_read_data(s_touch);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_lcd_touch_read_data failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
            continue;
        }

        esp_lcd_touch_point_data_t point = {0};
        uint8_t point_count = 0;
        err = esp_lcd_touch_get_data(s_touch, &point, &point_count, 1);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_lcd_touch_get_data failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
            continue;
        }

        if (point_count > 0) {
            ESP_LOGI(TAG, "touch: x=%u y=%u", (unsigned)point.x, (unsigned)point.y);
            ui_touch_set_point(point.x, point.y);
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
    }
}

esp_err_t touch_start(void) {
    if (s_touch_task != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = i2c_bus_get_handle();
    if (bus == NULL) {
        ESP_LOGE(TAG, "I2C bus is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_config.scl_speed_hz = FT6336U_I2C_CLK_HZ;

    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_config, &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_i2c failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_touch_config_t touch_config = {
        .x_max = BOARD_LCD_HRES,
        .y_max = BOARD_LCD_VRES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels =
            {
                .reset = 0,
                .interrupt = 0,
            },
        .flags =
            {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
    };

    err = esp_lcd_touch_new_i2c_ft5x06(s_touch_io, &touch_config, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_touch_new_i2c_ft5x06 failed: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t task_ok = xTaskCreate(touch_task, "touch", TOUCH_TASK_STACK_SIZE, NULL,
                                     TOUCH_TASK_PRIORITY, &s_touch_task);
    if (task_ok != pdPASS) {
        s_touch_task = NULL;
        ESP_LOGE(TAG, "failed to create touch task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FT6336U touch ready at I2C address 0x%02x",
             ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS);
    return ESP_OK;
}
