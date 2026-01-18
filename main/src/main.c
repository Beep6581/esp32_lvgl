#include "display.h"
#include "ui.h"

// #include "esp_lvgl_port.h"
// #include "lvgl.h"
//  #include "lv_demos.h"

/*
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/lcd_types.h"
#include "widgets/label/lv_label.h"
// #include "driver/uart.h"

#include "misc/lv_color.h"
#include "widgets/line/lv_line.h"

// UART-485 pins
#define RS485_IO_RTS GPIO_NUM_NC
#define RS485_IO_RX GPIO_NUM_1
#define RS485_IO_TX GPIO_NUM_2

// USB pins
#define USB_IO_DP GPIO_NUM_20
#define USB_IO_DN GPIO_NUM_19

// SHT20 pins
#define SHT20_IO_I2C_SDA BOARD_IO_I2C_SDA
#define SHT20_IO_I2C_SCL BOARD_IO_I2C_SCL


static void create_touch_cross(lv_display_t* disp) {
    lvgl_port_lock(0);

    lv_obj_t* scr = lv_disp_get_scr_act(disp);

    // Make simple line style (visible on white)
    const int line_width = 3;

    // Horizontal line
    cross_h = lv_line_create(scr);
    lv_obj_remove_style_all(cross_h);
    lv_obj_set_style_line_width(cross_h, line_width, 0);
    lv_obj_set_style_line_color(cross_h, lv_color_hex(0xFF0000), 0);

    // Vertical line
    cross_v = lv_line_create(scr);
    lv_obj_remove_style_all(cross_v);
    lv_obj_set_style_line_width(cross_v, line_width, 0);
    lv_obj_set_style_line_color(cross_v, lv_color_hex(0xFF0000), 0);

    // Hide initially
    lv_obj_add_flag(cross_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cross_v, LV_OBJ_FLAG_HIDDEN);

    lvgl_port_unlock();
}
*/

void app_main(void) {
    lv_display_t* disp = display_init();
    if (disp != NULL) {
        ui_init(disp);
    }

    //    create_touch_cross(disp);

    /*
     * Based on:
     * https://components.espressif.com/components/espressif/esp_lvgl_port/versions/2.6.1/readme
     */
}
