#include <stdio.h>
#include "display_framework.h"
#include "lvgl.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include "pins.h"

static const bool LCD_CMD = false;
static const bool LCD_DATA = true;

// Pending DMA transfer on SPI bus
static bool pending_xfer = false;

static lv_display_t *lcd_disp = NULL;

static const uint32_t LCD_H_RES = 240;
static const uint32_t LCD_V_RES = 320;

static void initialise_lcd_hw()
{
	// Backlight
    gpio_init(GPIO_LCD_BACKLIGHT_PIN);
    gpio_set_dir(GPIO_LCD_BACKLIGHT_PIN, GPIO_OUT);
    gpio_put(GPIO_LCD_BACKLIGHT_PIN, 1);

    // LCD reset pin
    gpio_init(GPIO_LCD_RESETn);
    gpio_set_dir(GPIO_LCD_RESETn, GPIO_OUT);

    // DCX pin
    gpio_init(GPIO_LCD_DCX);
    gpio_set_dir(GPIO_LCD_DCX, GPIO_OUT);

	// SPI initialisation
    gpio_init(GPIO_SPI0_CSn);  // Software controlled

    gpio_init(GPIO_SPI0_RX);
    gpio_init(GPIO_SPI0_SCK);
    gpio_init(GPIO_SPI0_TX);
    gpio_set_function(GPIO_SPI0_RX, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI0_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI0_TX, GPIO_FUNC_SPI);
    const uint baud = spi_init(spi0, 50000000);
    printf("SPI initialised with: %d baudrate\n\r", baud);

    // Reset the LCD
    gpio_put(GPIO_LCD_RESETn, false);
    sleep_ms(100);   // This needs to be more than 10 us
    gpio_put(GPIO_LCD_RESETn, true);
    sleep_ms(200);   // The worst case time is 120 ms

    gpio_put(GPIO_LCD_DCX, LCD_DATA);
    gpio_put(GPIO_SPI0_CSn, true);
}

static void send_lcd_cmd(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, const uint8_t *param, size_t param_size)
{
    if (!disp || !cmd)
    {
        return;
    }

    while (pending_xfer);

    gpio_put(GPIO_LCD_DCX, LCD_CMD);
    gpio_put(GPIO_SPI0_CSn, false);
    spi_write_blocking(spi0, cmd, cmd_size);
    
    if (param)
    {
        gpio_put(GPIO_LCD_DCX, LCD_DATA);
        spi_write_blocking(spi0, param, param_size);
    }

    gpio_put(GPIO_SPI0_CSn, true);
}

static void send_lcd_data(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, uint8_t *param, size_t param_size)
{
    send_lcd_cmd(disp, cmd, cmd_size, param, param_size);
}

static void initialise_lvgl_framework()
{
	lv_init();

    lcd_disp = lv_st7789_create(LCD_H_RES, LCD_V_RES, LV_LCD_FLAG_NONE, send_lcd_cmd, send_lcd_data);
    lv_disp_set_rotation(lcd_disp, LV_DISP_ROTATION_0);

    lv_color_t *buf1 = NULL;
    lv_color_t *buf2 = NULL;

    // For partial rendering the buffer is set to 1/10th of display size
    const uint32_t buf_size = LCD_H_RES * LCD_V_RES * lv_color_format_get_size(lv_display_get_color_format(lcd_disp)) / 10;
    buf1 = lv_malloc(buf_size);
    if (buf1 == NULL){
        printf("Buffer1 allocation failed!\n\r");
        return;
    }

    buf2 = lv_malloc(buf_size);
    if (buf2 == NULL){
        printf("Buffer2 allocation failed!\n\r");
        lv_free(buf1);
        return;
    }

    lv_display_set_buffers(lcd_disp, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

int initialise_gui()
{
	initialise_lcd_hw();
    initialise_lvgl_framework();
	return 0;
}
