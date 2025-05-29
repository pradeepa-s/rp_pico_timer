#include <stdio.h>
#include "display_framework.h"
#include "lvgl.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include "pins.h"
#include "tick_count.h"
#include "ui_properties.h"
#include "touch_screen.h"


// IMPROVEMENTS:
//
// - Remove dependency on the touch.

static const bool LCD_CMD = false;
static const bool LCD_DATA = true;

// Pending DMA transfer on SPI bus
static bool pending_xfer = false;

static lv_display_t *lcd_disp = NULL;
static lv_indev_t *touch_panel = NULL;

static const uint32_t LCD_H_RES = 240;
static const uint32_t LCD_V_RES = 320;

static absolute_time_t prev_time = 0;
static uint32_t time_sec = 0;

static lv_obj_t *label_clock;

static bool started = true;

#ifdef ENABLE_REG_READ_FUNC
// Can use this function to read register values for debugging.
void read_register(void)
{
    uint8_t reg = 0x0C;
    uint8_t read[1];
    gpio_put(GPIO_LCD_DCX, LCD_CMD);
    gpio_put(GPIO_SPI0_CSn, false);
    // spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    int val = spi_write_blocking(spi0, &reg, sizeof(reg));
    if (val == 1)
    {
        gpio_put(GPIO_LCD_DCX, LCD_DATA);
        val = spi_read_blocking(spi0, 0xff, read, sizeof(read));
        gpio_put(GPIO_SPI0_CSn, true);
    }
}
#endif

static void button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED)
    {
        lv_obj_t *label = lv_event_get_user_data(e);
        started = !started;
        lv_label_set_text(label, started ? "Start" : "Stop");
    }
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    touch_point_t tp = get_touch_point();

    if (tp.valid)
    {
        data->point.x = tp.x;
        data->point.y = tp.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void initialise_lcd_hw()
{
	// Backlight
    gpio_init(GPIO_LCD_BACKLIGHT_PIN);
    gpio_set_dir(GPIO_LCD_BACKLIGHT_PIN, GPIO_OUT);

    // LCD reset pin
    gpio_init(GPIO_LCD_RESETn);
    gpio_set_dir(GPIO_LCD_RESETn, GPIO_OUT);

    // DCX pin
    gpio_init(GPIO_LCD_DCX);
    gpio_set_dir(GPIO_LCD_DCX, GPIO_OUT);

	// SPI initialisation
    gpio_init(GPIO_SPI0_CSn);  // Software controlled
    gpio_set_dir(GPIO_SPI0_CSn, GPIO_OUT);

    gpio_init(GPIO_SPI0_RX);
    gpio_init(GPIO_SPI0_SCK);
    gpio_init(GPIO_SPI0_TX);
    gpio_set_function(GPIO_SPI0_RX, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI0_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI0_TX, GPIO_FUNC_SPI);
    const uint baud = spi_init(spi0, 50000000);   // Maximum supported is 62.5 MHz
    printf("SPI initialised with: %d baudrate\n\r", baud);

    // Switch on backlight
    gpio_put(GPIO_LCD_BACKLIGHT_PIN, 1);

    // Reset the LCD
    gpio_put(GPIO_LCD_RESETn, false);
    sleep_ms(25);   // This needs to be more than 10 us
    gpio_put(GPIO_LCD_RESETn, true);
    sleep_ms(125);   // The worst case time is 120 ms

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
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
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
    if (!disp || !cmd)
    {
        return;
    }

    while (pending_xfer);

    gpio_put(GPIO_LCD_DCX, LCD_CMD);
    gpio_put(GPIO_SPI0_CSn, false);
    pending_xfer = true;
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_write_blocking(spi0, cmd, cmd_size);
    
    if (param)
    {
        // The data write is alwways 16 bits.
        // The data transfer is MSB first. Refer 8.8.42
        uint16_t* data = (uint16_t*)param;
        size_t data_size = param_size / 2;
        spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        gpio_put(GPIO_LCD_DCX, LCD_DATA);
        spi_write16_blocking(spi0, data, data_size);
    }

    gpio_put(GPIO_SPI0_CSn, true);
    pending_xfer = false;
    lv_display_flush_ready(disp);
}

static void initialise_lvgl_framework()
{
	lv_init();
    lv_tick_set_cb(get_tick_count);
    lcd_disp = lv_st7789_create(LCD_H_RES, LCD_V_RES, LV_LCD_FLAG_NONE, send_lcd_cmd, send_lcd_data);

    // Colour setting is governed by LV_COLOR_DEPTH. It's set to 16 which leads to RGB565 color format
    // being native.
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

    // Initialise touch screen connection
    touch_panel = lv_indev_create();
    lv_indev_set_type(touch_panel, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_panel, read_touch);
}

static void ui_init(lv_display_t *disp)
{
    /* set screen background to dark gray */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_100, 0);

    /* create Timer label */
    lv_obj_t *label_timer = lv_label_create(scr);
    lv_label_set_text(label_timer, "Timer");
    lv_obj_set_style_text_color(label_timer, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_timer, &lv_font_montserrat_28, 0);
    lv_obj_set_height(label_timer, LV_SIZE_CONTENT);
    lv_obj_set_width(label_timer, LV_SIZE_CONTENT);
    lv_obj_update_layout(label_timer);
    lv_coord_t height = lv_obj_get_height(label_timer);
    lv_obj_align(label_timer, LV_ALIGN_TOP_MID, 0, UI_PROP_BORDER_PADDING_PX + 52 - (height / 2));

    /* create Clock label */
    label_clock = lv_label_create(scr);
    lv_label_set_text(label_clock, "00:00");
    lv_obj_set_style_text_color(label_clock, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_46, 0);
    lv_obj_set_height(label_clock, LV_SIZE_CONTENT);
    lv_obj_set_width(label_clock, LV_SIZE_CONTENT);
    lv_obj_update_layout(label_clock);
    height = lv_obj_get_height(label_clock);
    lv_obj_align(label_clock, LV_ALIGN_TOP_MID, 0, UI_PROP_BORDER_PADDING_PX + 124 - (height / 2));

    lv_obj_t *button = lv_button_create(scr);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(button, UI_PROP_INTERNAL_PADDING_PX, 0);
    lv_obj_set_style_pad_bottom(button, UI_PROP_INTERNAL_PADDING_PX, 0);
    lv_obj_set_style_pad_left(button, UI_PROP_INTERNAL_PADDING_PX * 2, 0);
    lv_obj_set_style_pad_right(button, UI_PROP_INTERNAL_PADDING_PX * 2, 0);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Start");
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_20, 0);

    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_RELEASED, button_label);
    prev_time = get_absolute_time();
}

int initialise_gui()
{
	initialise_lcd_hw();
    initialise_lvgl_framework();
    ui_init(lcd_disp);
	return 0;
}

void tick_ui()
{
    const absolute_time_t curr_time = get_absolute_time();

    if (curr_time - prev_time > 1000 * 1000) {
        prev_time = curr_time;
        time_sec++;
        char time[10] = "";
        snprintf(time, sizeof(time), "%02d:%02d", time_sec / 60, time_sec % 60);
        lv_label_set_text(label_clock, time);
    }
    lv_timer_handler();
}
