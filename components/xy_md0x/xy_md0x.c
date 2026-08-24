#include "xy_md0x.h"

#include <stddef.h>

#include "board.h"

#include "driver/uart.h"

#include "esp_check.h"

#include "freertos/FreeRTOS.h"

#define XY_MD0X_UART UART_NUM_1
#define XY_MD0X_BAUD_RATE 9600
#define XY_MD0X_RESPONSE_SIZE 9
#define XY_MD0X_TIMEOUT_MS 500

static uint8_t s_slave_address;

static uint16_t modbus_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

esp_err_t xy_md0x_init(uint8_t slave_address) {
    if (slave_address == 0U || slave_address > 247U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t config = {
        .baud_rate = XY_MD0X_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_param_config(XY_MD0X_UART, &config), "xy_md0x", "UART configuration failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(XY_MD0X_UART, BOARD_RS485_CONN2_TX_GPIO, BOARD_RS485_CONN2_RX_GPIO, BOARD_RS485_CONN2_DE_GPIO, UART_PIN_NO_CHANGE), "xy_md0x", "UART pin configuration failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(XY_MD0X_UART, 256, 0, 0, NULL, 0), "xy_md0x", "UART driver installation failed");
    ESP_RETURN_ON_ERROR(uart_set_mode(XY_MD0X_UART, UART_MODE_RS485_HALF_DUPLEX), "xy_md0x", "RS485 mode configuration failed");

    s_slave_address = slave_address;
    return ESP_OK;
}

esp_err_t xy_md0x_read(xy_md0x_sample_t* out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_slave_address == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t request[8] = {
        s_slave_address,
        0x04,
        0x00,
        0x01,
        0x00,
        0x02,
    };
    const uint16_t request_crc = modbus_crc16(request, 6);
    request[6] = (uint8_t)request_crc;
    request[7] = (uint8_t)(request_crc >> 8);

    ESP_RETURN_ON_ERROR(uart_flush_input(XY_MD0X_UART), "xy_md0x", "UART input flush failed");
    if (uart_write_bytes(XY_MD0X_UART, request, sizeof(request)) != (int)sizeof(request)) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(uart_wait_tx_done(XY_MD0X_UART, pdMS_TO_TICKS(XY_MD0X_TIMEOUT_MS)), "xy_md0x", "UART transmit timeout");

    uint8_t response[XY_MD0X_RESPONSE_SIZE];
    const int length = uart_read_bytes(XY_MD0X_UART, response, sizeof(response), pdMS_TO_TICKS(XY_MD0X_TIMEOUT_MS));
    if (length != (int)sizeof(response)) {
        return ESP_ERR_TIMEOUT;
    }
    if (response[0] != s_slave_address || response[1] != 0x04 || response[2] != 0x04) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint16_t response_crc = (uint16_t)response[7] | ((uint16_t)response[8] << 8);
    if (modbus_crc16(response, 7) != response_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    const int16_t temperature_tenths = (int16_t)(((uint16_t)response[3] << 8) | response[4]);
    const uint16_t humidity_tenths = ((uint16_t)response[5] << 8) | response[6];
    out->temperature_m_deg_c = (int32_t)temperature_tenths * 100;
    out->humidity_m_percent_rh = (int32_t)humidity_tenths * 100;
    return ESP_OK;
}
