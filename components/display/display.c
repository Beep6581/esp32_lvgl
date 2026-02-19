/*
 * Minimal LVGL setup for ZX3D95CE01S-TR-4848
 * ESP-IDF: 5.5
 * LVGL: 9.3
 */

#include "board.h"

#include "display.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"

#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"

#include "esp_lcd_gc9503.h"
#include "esp_lcd_panel_io_additions.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
// #include "esp_lcd_panel_io.h"
// #include "esp_lcd_touch_ft5x06.h"

#include "bitwalk.h"

static const char* TAG = "display";

/* Based on:
 * https://components.espressif.com/components/espressif/esp_lcd_gc9503/versions/3.0.1/readme
 */

/* Based on:
 * https://github.com/espressif/esp-bsp/blob/master/components/lcd/esp_lcd_gc9503/esp_lcd_gc9503.c#L172
 * { cmd, {payload bytes...}, payload_len, delay_ms_after }
 */
static const gc9503_lcd_init_cmd_t gc9503v_480_init[] = {
    {0xf0, (uint8_t[]){0x55, 0xaa, 0x52, 0x08, 0x00}, 5, 0},
    {0xf6, (uint8_t[]){0x5a, 0x87}, 2, 0},
    {0xc1, (uint8_t[]){0x3f}, 1, 0},
    {0xc2, (uint8_t[]){0x0e}, 1, 0},
    {0xc6, (uint8_t[]){0xf8}, 1, 0},
    {0xc9, (uint8_t[]){0x10}, 1, 0},
    {0xcd, (uint8_t[]){0x25}, 1, 0},
    {0xf8, (uint8_t[]){0x8a}, 1, 0},
    {0xac, (uint8_t[]){0x45}, 1, 0},
    {0xa0, (uint8_t[]){0xdd}, 1, 0},
    {0xa7, (uint8_t[]){0x47}, 1, 0},
    {0xfa, (uint8_t[]){0x00, 0x00, 0x00, 0x04}, 4, 0},
    {0x86, (uint8_t[]){0x99, 0xa3, 0xa3, 0x51}, 4, 0},
    {0xa3, (uint8_t[]){0xee}, 1, 0},
    {0xfd, (uint8_t[]){0x3c, 0x3c, 0x00}, 3, 0},
    {0x71, (uint8_t[]){0x48}, 1, 0},
    {0x72, (uint8_t[]){0x48}, 1, 0},
    {0x73, (uint8_t[]){0x00, 0x44}, 2, 0},
    {0x97, (uint8_t[]){0xee}, 1, 0},
    {0x83, (uint8_t[]){0x93}, 1, 0},
    {0x9a, (uint8_t[]){0x72}, 1, 0},
    {0x9b, (uint8_t[]){0x5a}, 1, 0},
    {0x82, (uint8_t[]){0x2c, 0x2c}, 2, 0},
    {0x6d, (uint8_t[]){0x00, 0x1f, 0x19, 0x1a, 0x10, 0x0e, 0x0c, 0x0a, 0x02, 0x07, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e,
                       0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x1e, 0x08, 0x01, 0x09, 0x0b, 0x0d, 0x0f, 0x1a, 0x19, 0x1f, 0x00},
     32, 0},
    {0x64, (uint8_t[]){0x38, 0x05, 0x01, 0xdb, 0x03, 0x03, 0x38, 0x04, 0x01, 0xdc, 0x03, 0x03, 0x7a, 0x7a, 0x7a, 0x7a}, 16, 0},
    {0x65, (uint8_t[]){0x38, 0x03, 0x01, 0xdd, 0x03, 0x03, 0x38, 0x02, 0x01, 0xde, 0x03, 0x03, 0x7a, 0x7a, 0x7a, 0x7a}, 16, 0},
    {0x66, (uint8_t[]){0x38, 0x01, 0x01, 0xdf, 0x03, 0x03, 0x38, 0x00, 0x01, 0xe0, 0x03, 0x03, 0x7a, 0x7a, 0x7a, 0x7a}, 16, 0},
    {0x67, (uint8_t[]){0x30, 0x01, 0x01, 0xe1, 0x03, 0x03, 0x30, 0x02, 0x01, 0xe2, 0x03, 0x03, 0x7a, 0x7a, 0x7a, 0x7a}, 16, 0},
    {0x68, (uint8_t[]){0x00, 0x08, 0x15, 0x08, 0x15, 0x7a, 0x7a, 0x08, 0x15, 0x08, 0x15, 0x7a, 0x7a}, 13, 0},
    {0x60, (uint8_t[]){0x38, 0x08, 0x7a, 0x7a, 0x38, 0x09, 0x7a, 0x7a}, 8, 0},
    {0x63, (uint8_t[]){0x31, 0xe4, 0x7a, 0x7a, 0x31, 0xe5, 0x7a, 0x7a}, 8, 0},
    {0x69, (uint8_t[]){0x04, 0x22, 0x14, 0x22, 0x14, 0x22, 0x08}, 7, 0},
    {0x6b, (uint8_t[]){0x07}, 1, 0},
    {0x7a, (uint8_t[]){0x08, 0x13}, 2, 0},
    {0x7b, (uint8_t[]){0x08, 0x13}, 2, 0},
    {0xd1, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    {0xd2, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    {0xd3, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    {0xd4, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    {0xd5, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    {0xd6, (uint8_t[]){0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00, 0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5,
                       0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee, 0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xe0, 0x03, 0xea, 0x03, 0xfa, 0x03, 0xff},
     52, 0},
    //{0xb1, (uint8_t[]){0x10}, 1, 0}, // DISPLAY_CTL, datasheet says default 0x10 (BGR CFA), else 0x30 (RGB CFA). Handled by esp_lcd_panel_dev_config_t
    //{0x3a, (uint8_t[]){0x66}, 1, 0}, // RGB Interface Format. Use 110 18-bit (0x66), because 101 16-bit (0x50) ignores MSB R and B.
    {0x51, (uint8_t[]){0x7F}, 1, 0}, // Write Display Brightness. Default 0x3A or 0x51
    {0xB0, (uint8_t[]){0x00}, 1, 0}, // RGB Interface Signals Control, DE mode, rising edge, polarity high. Sync and pulse bytes ignored in DE mode.
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

static const size_t gc9503v_480_init_count = sizeof(gc9503v_480_init) / sizeof(gc9503v_480_init[0]);

static void backlight_init_off(void) {
    if (BOARD_LCD_BL_GPIO == GPIO_NUM_NC) {
        return;
    }

    gpio_config_t bl = {0};
    bl.pin_bit_mask = 1ULL << BOARD_LCD_BL_GPIO;
    bl.mode = GPIO_MODE_OUTPUT;
    bl.pull_up_en = GPIO_PULLUP_DISABLE;
    bl.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bl.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&bl));

    ESP_ERROR_CHECK(gpio_set_level(BOARD_LCD_BL_GPIO, 0));
}

lv_display_t* display_init(void) {
    backlight_init_off();

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_config));

    ESP_LOGI(TAG, "Install 3-wire SPI panel IO");

    spi_line_config_t line_config = {
        .cs_io_type = IO_TYPE_GPIO,
        .cs_gpio_num = BOARD_LCD_SPI_CS_GPIO,
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = BOARD_LCD_SPI_SCL_GPIO,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = BOARD_LCD_SPI_SDA_GPIO,
        .io_expander = NULL,
    };

    esp_lcd_panel_io_3wire_spi_config_t gc9503_ctrl_3wire_cfg = GC9503_PANEL_IO_3WIRE_SPI_CONFIG(line_config, 0);

    esp_lcd_panel_io_handle_t io_handle = NULL;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&gc9503_ctrl_3wire_cfg, &io_handle));

    ESP_LOGI(TAG, "Install GC9503 panel driver");

    esp_lcd_rgb_timing_t timing = GC9503_480_480_PANEL_60HZ_RGB_TIMING();
    timing.pclk_hz = BOARD_LCD_PCLK_HZ; // default: 16 * 1000 * 1000
    timing.flags.de_idle_high = 0;      // Must be 0 as GC9503V expects DE active-high (B0h DEP=0), else backlight on but black screen.
    timing.hsync_pulse_width = 80;      //  10    80
    timing.hsync_back_porch = 80;       //  40    80
    timing.hsync_front_porch = 40;      //   8    40
    timing.vsync_pulse_width = 80;      //  10    80
    timing.vsync_back_porch = 80;       //  40    80
    timing.vsync_front_porch = 40;      //   8    40

    esp_lcd_rgb_panel_config_t rgb_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT, // LCD_CLK_SRC_DEFAULT == LCD_CLK_SRC_PLL160M
        //.timings = GC9503_480_480_PANEL_60HZ_RGB_TIMING(), // With this, display works but sometimes not vertically centered
        .timings = timing,
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = BOARD_LCD_HRES * 30,
        //.psram_trans_align   = 64,
        //.sram_trans_align    = 0,
        .dma_burst_size = 64, // https://github.com/espressif/esp-bsp/blob/master/components/lcd/esp_lcd_gc9503/README.md
        .hsync_gpio_num = BOARD_LCD_RGB_HSYNC_GPIO,
        .vsync_gpio_num = BOARD_LCD_RGB_VSYNC_GPIO,
        .de_gpio_num = BOARD_LCD_RGB_DE_GPIO,
        .pclk_gpio_num = BOARD_LCD_RGB_PCLK_GPIO,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums =
            {
                BOARD_LCD_RGB_DATA0_GPIO,
                BOARD_LCD_RGB_DATA1_GPIO,
                BOARD_LCD_RGB_DATA2_GPIO,
                BOARD_LCD_RGB_DATA3_GPIO,
                BOARD_LCD_RGB_DATA4_GPIO,
                BOARD_LCD_RGB_DATA5_GPIO,
                BOARD_LCD_RGB_DATA6_GPIO,
                BOARD_LCD_RGB_DATA7_GPIO,
                BOARD_LCD_RGB_DATA8_GPIO,
                BOARD_LCD_RGB_DATA9_GPIO,
                BOARD_LCD_RGB_DATA10_GPIO,
                BOARD_LCD_RGB_DATA11_GPIO,
                BOARD_LCD_RGB_DATA12_GPIO,
                BOARD_LCD_RGB_DATA13_GPIO,
                BOARD_LCD_RGB_DATA14_GPIO,
                BOARD_LCD_RGB_DATA15_GPIO,
            },
        .flags =
            {
                .disp_active_low = 0,
                .refresh_on_demand = 0,
                .fb_in_psram = 1,
                .double_fb = 0,
                .no_fb = 0,
                .bb_invalidate_cache = 1,
            },
    };

    gc9503_vendor_config_t vendor_config = {
        .rgb_config = &rgb_config,
        .init_cmds = gc9503v_480_init, // Uncomment these line if use custom initialization commands
        .init_cmds_size = gc9503v_480_init_count,
        .flags =
            {
                .mirror_by_cmd = 0,     // Only works when auto_del_panel_io is set to 0
                .auto_del_panel_io = 1, // Set to 1 if panel IO is no longer needed after LCD init, to release shared GPIOs.
            },
    };

    const esp_lcd_panel_dev_config_t panel_config = {.reset_gpio_num = BOARD_LCD_RST_GPIO,       // Set to -1 if not used
                                                     .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // Implemented by LCD command `B1h`
                                                     .bits_per_pixel = 18,                       // Implemented by LCD command `3Ah` (16/18/24). Use 18-bit, because 16-bit ignores MSB R and B.
                                                     .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
                                                     .vendor_config = &vendor_config,
                                                     .flags = {.reset_active_high = 0}};

    esp_lcd_panel_handle_t panel_handle = NULL;

    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9503(io_handle, &panel_config, &panel_handle));

    // WT32S3-86S: LCD_RST is coupled to RGB_VSYNC (GPIO41) through RC/diode network.
    // Treating GPIO41 as a normal reset pin and pulsing it breaks bring-up.
    // Therefore do NOT call esp_lcd_panel_reset() or manual reset pulse here.
    // ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    if (BOARD_LCD_BL_GPIO != GPIO_NUM_NC) {
        ESP_ERROR_CHECK(gpio_set_level(BOARD_LCD_BL_GPIO, 1));
    }

    // ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); // Don't call this function if auto_del_panel_io is set to 0 and disp_gpio_num is set to -1

    /*
        esp_lcd_panel_io_i2c_config_t touch_io_i2c_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();

        esp_lcd_touch_config_t touch_cfg = {
            .x_max = BOARD_LCD_HRES,
            .y_max = BOARD_LCD_VRES,
            .rst_gpio_num = -1,
            .int_gpio_num = -1,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };

        esp_lcd_touch_handle_t touch;
        esp_lcd_touch_new_i2c_ft5x06(io_handle, &touch_cfg, &touch);

        esp_lcd_touch_read_data(touch);
    */

#if CONFIG_UI_DEMO_BITWALK
    demo_bitwalk(panel_handle);
    return NULL;
#endif

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        //.control_handle = control_handle,
        .buffer_size = BOARD_LCD_HRES * BOARD_LCD_VRES,
        .double_buffer = true,
        .trans_size = 64,
        .hres = BOARD_LCD_HRES,
        .vres = BOARD_LCD_VRES,
        .monochrome = false,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 1,
                .sw_rotate = 0,
                .swap_bytes = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    lvgl_port_display_rgb_cfg_t rgb_cfg = {.flags = {
                                               .bb_mode = 1,
                                               .avoid_tearing = 0,
                                           }};

    lv_display_t* disp = lvgl_port_add_disp_rgb(&display_config, &rgb_cfg);
    lv_display_set_default(disp);

    ESP_LOGI(TAG, "LVGL display registered, disp=%p", disp);

    return disp;
}
