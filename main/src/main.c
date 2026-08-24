#include "air_quality.h"
#include "display.h"
#include "i2c_bus.h"
#include "touch.h"
#include "ui.h"
#include "xy_md0x.h"

#include "esp_log.h"

/*
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
*/

static const char* TAG = "main";

void app_main(void) {
    ESP_ERROR_CHECK(i2c_bus_init());

    ESP_ERROR_CHECK(air_quality_start());

    lv_display_t* disp = display_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display_init failed");
        return;
    }

    ui_init(disp);

    ESP_ERROR_CHECK(touch_start());

    xy_md0x_sample_t modbus_sample;
    ESP_ERROR_CHECK(xy_md0x_init(1));
    const esp_err_t modbus_err = xy_md0x_read(&modbus_sample);
    if (modbus_err == ESP_OK) {
        ESP_LOGI(TAG, "XY-MD0x Modbus response: T_mC=%ld RH_mpercent=%ld", (long)modbus_sample.temperature_m_deg_c, (long)modbus_sample.humidity_m_percent_rh);
    } else {
        ESP_LOGW(TAG, "XY-MD0x Modbus read failed (%s)", esp_err_to_name(modbus_err));
    }
}
