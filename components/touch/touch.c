#include "touch.h"

#include "board.h"
#include "i2c_bus.h"
#include "ui.h"

#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define FT6336U_I2C_CLK_HZ (400000)

static const char* TAG = "touch";

static esp_lcd_panel_io_handle_t s_touch_io = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static lv_indev_t* s_touch_indev = NULL;
static uint16_t s_last_x = UINT16_MAX;
static uint16_t s_last_y = UINT16_MAX;
static bool s_was_pressed = false;

static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;

    esp_err_t err = esp_lcd_touch_read_data(s_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_touch_read_data failed: %s", esp_err_to_name(err));
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t point = {0};
    uint8_t point_count = 0;
    err = esp_lcd_touch_get_data(s_touch, &point, &point_count, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_touch_get_data failed: %s", esp_err_to_name(err));
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (point_count > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;

        if (!s_was_pressed || point.x != s_last_x || point.y != s_last_y) {
            ESP_LOGI(TAG, "touch: x=%u y=%u", (unsigned)point.x, (unsigned)point.y);
            ui_touch_set_point(point.x, point.y);
            s_last_x = point.x;
            s_last_y = point.y;
        }
        s_was_pressed = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        s_was_pressed = false;
    }
}

esp_err_t touch_start(void) {
    if (s_touch_indev != NULL) {
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

    lv_display_t* disp = lv_display_get_default();
    if (disp == NULL) {
        ESP_LOGE(TAG, "LVGL default display is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    lvgl_port_lock(0);
    s_touch_indev = lv_indev_create();
    if (s_touch_indev != NULL) {
        lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(s_touch_indev, disp);
        lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
    }
    lvgl_port_unlock();

    if (s_touch_indev == NULL) {
        ESP_LOGE(TAG, "failed to create LVGL touch input device");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "FT6336U touch ready at I2C address 0x%02x",
             ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS);
    return ESP_OK;
}
