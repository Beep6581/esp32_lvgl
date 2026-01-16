#pragma once

#include "driver/gpio.h"    // GPIO_NUM_NC (-1)
#include "hal/gpio_types.h" // gpio_num_t
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert Kconfig int -> gpio_num_t, mapping -1 to GPIO_NUM_NC
static inline gpio_num_t board_gpio_from_kconfig(int gpio) {
    return (gpio < 0) ? GPIO_NUM_NC : (gpio_num_t)gpio;
}

// Common buses
#define BOARD_I2C_SDA_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_I2C_SDA))
#define BOARD_I2C_SCL_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_I2C_SCL))

// LCD/backlight
#define BOARD_LCD_BL_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_BL))
#define BOARD_LCD_RST_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RST))

// LCD 3-wire SPI control
#define BOARD_LCD_SPI_CS_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_SPI_CS))
#define BOARD_LCD_SPI_SCL_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_SPI_SCL))
#define BOARD_LCD_SPI_SDA_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_SPI_SDA))

// LCD RGB
#define BOARD_LCD_RGB_PCLK_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_PCLK))
#define BOARD_LCD_RGB_DE_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_DE))
#define BOARD_LCD_RGB_HSYNC_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_HSYNC))
#define BOARD_LCD_RGB_VSYNC_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_VSYNC))

#define BOARD_LCD_RGB_DATA0_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D0))
#define BOARD_LCD_RGB_DATA1_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D1))
#define BOARD_LCD_RGB_DATA2_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D2))
#define BOARD_LCD_RGB_DATA3_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D3))
#define BOARD_LCD_RGB_DATA4_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D4))
#define BOARD_LCD_RGB_DATA5_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D5))
#define BOARD_LCD_RGB_DATA6_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D6))
#define BOARD_LCD_RGB_DATA7_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D7))
#define BOARD_LCD_RGB_DATA8_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D8))
#define BOARD_LCD_RGB_DATA9_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D9))
#define BOARD_LCD_RGB_DATA10_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D10))
#define BOARD_LCD_RGB_DATA11_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D11))
#define BOARD_LCD_RGB_DATA12_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D12))
#define BOARD_LCD_RGB_DATA13_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D13))
#define BOARD_LCD_RGB_DATA14_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D14))
#define BOARD_LCD_RGB_DATA15_GPIO (board_gpio_from_kconfig(CONFIG_BOARD_LCD_RGB_D15))

// LCD geometry/timing
#define BOARD_LCD_HRES (CONFIG_BOARD_LCD_HRES)
#define BOARD_LCD_VRES (CONFIG_BOARD_LCD_VRES)
#define BOARD_LCD_PCLK_HZ (CONFIG_BOARD_LCD_PCLK_HZ)

#ifdef __cplusplus
}
#endif
