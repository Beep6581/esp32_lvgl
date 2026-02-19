#include "air_quality.h"
#include "display.h"
#include "i2c_bus.h"
#include "ui.h"

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

    // Start sensor task (runs independently of the UI).
    ESP_ERROR_CHECK(air_quality_start());

    lv_display_t* disp = display_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display_init failed");
        return;
    }

    ui_init(disp);
}
