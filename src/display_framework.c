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
// - Start stop button colours.
// - Colour theme

static const bool LCD_CMD = false;
static const bool LCD_DATA = true;

static const int HR_INCR = 1;
static const int HR_DECR = 2;
static const int MIN_INCR = 3;
static const int MIN_DECR = 4;
static const int SET_TIME = 5;

static int hr_incr_event = HR_INCR;
static int hr_decr_event = HR_DECR;
static int min_incr_event = MIN_INCR;
static int min_decr_event = MIN_DECR;
static int set_time_event = SET_TIME;

// Pending DMA transfer on SPI bus
static bool pending_xfer = false;

static lv_display_t *lcd_disp = NULL;
static lv_indev_t *touch_panel = NULL;

static const uint32_t LCD_H_RES = 240;
static const uint32_t LCD_V_RES = 320;

static absolute_time_t prev_tick = 0;
static uint32_t active_time_min = 0;
static uint32_t set_time_min = 0;
static uint32_t display_set_time = 0;

static lv_obj_t *label_clock;

static bool started = false;
static bool reset = false;
static bool refresh = false;

typedef enum {
    StartStopTime,
    SetTime
} ui_state_t;

static ui_state_t ui_state = StartStopTime;

static lv_obj_t *start_stop_btn = NULL;
static lv_obj_t *reset_btn = NULL;
static lv_obj_t *incr_hr_btn = NULL;
static lv_obj_t *decr_hr_btn = NULL;
static lv_obj_t *incr_min_btn = NULL;
static lv_obj_t *decr_min_btn = NULL;
static lv_obj_t *set_time_btn = NULL;

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

static void show_screen()
{
    lv_obj_set_flag(incr_hr_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == StartStopTime));
    lv_obj_set_flag(incr_min_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == StartStopTime));
    lv_obj_set_flag(decr_hr_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == StartStopTime));
    lv_obj_set_flag(decr_min_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == StartStopTime));
    lv_obj_set_flag(set_time_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == StartStopTime));

    lv_obj_set_flag(start_stop_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == SetTime));
    lv_obj_set_flag(reset_btn, LV_OBJ_FLAG_HIDDEN, (ui_state == SetTime));

}
static void label_clock_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        ui_state = (ui_state == StartStopTime) ? SetTime : StartStopTime;
        show_screen();
        display_set_time = set_time_min;
    }
}

static void set_time_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        int *event = lv_event_get_user_data(e);
        int hr = display_set_time / 60;
        int min = display_set_time % 60;
        if (event) {
            switch (*event) {
                case HR_INCR:
                    hr = (hr == 23) ? 0 : (hr + 1);
                    refresh = true;
                break;
                case HR_DECR:
                    hr = (hr == 0) ? 23 : (hr - 1);
                    refresh = true;
                break;
                case MIN_INCR:
                    min = (min == 59) ? 0 : (min + 1);
                    refresh = true;
                break;
                case MIN_DECR:
                    min = (min == 0) ? 60 : (min - 1);
                    refresh = true;
                break;
                case SET_TIME:
                    set_time_min = display_set_time;
                    ui_state = StartStopTime;
                    show_screen();
                break;
                default:
                break;
            };
        }

        display_set_time = hr * 60 + min;
    }
}

static void start_stop_button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        lv_obj_t *label = lv_event_get_user_data(e);
        started = !started;
        lv_label_set_text(label, started ? "Stop" : "Start");
    }
}

static void reset_button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED) {
        reset = true;
    }
}

static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    touch_point_t tp = get_touch_point();

    if (tp.valid) {
        data->point.x = tp.x;
        data->point.y = tp.y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
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
    if (!disp || !cmd) {
        return;
    }

    while (pending_xfer);

    gpio_put(GPIO_LCD_DCX, LCD_CMD);
    gpio_put(GPIO_SPI0_CSn, false);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_write_blocking(spi0, cmd, cmd_size);
    
    if (param) {
        gpio_put(GPIO_LCD_DCX, LCD_DATA);
        spi_write_blocking(spi0, param, param_size);
    }

    gpio_put(GPIO_SPI0_CSn, true);
}

static void send_lcd_data(lv_display_t *disp, const uint8_t *cmd, size_t cmd_size, uint8_t *param, size_t param_size)
{
    if (!disp || !cmd) {
        return;
    }

    while (pending_xfer);

    gpio_put(GPIO_LCD_DCX, LCD_CMD);
    gpio_put(GPIO_SPI0_CSn, false);
    pending_xfer = true;
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    spi_write_blocking(spi0, cmd, cmd_size);
    
    if (param) {
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
    if (buf1 == NULL) {
        printf("Buffer1 allocation failed!\n\r");
        return;
    }

    buf2 = lv_malloc(buf_size);
    if (buf2 == NULL) {
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

    /* create Clock label */
    label_clock = lv_label_create(scr);
    lv_label_set_text(label_clock, "00:00");
    lv_obj_set_style_text_color(label_clock, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(label_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_height(label_clock, LV_SIZE_CONTENT);
    lv_obj_set_width(label_clock, LV_SIZE_CONTENT);
    lv_obj_update_layout(label_clock);
    const int clock_height = lv_obj_get_height(label_clock);
    // Center of the top half of the display
    const int32_t timer_y_off = (LCD_V_RES / 4) - (clock_height / 2);
    lv_obj_align(label_clock, LV_ALIGN_TOP_MID, 0, timer_y_off);
    lv_obj_set_flag(label_clock, LV_OBJ_FLAG_CLICKABLE, true);
    lv_obj_add_event_cb(label_clock, label_clock_cb, LV_EVENT_RELEASED, NULL);

    /* Draw a line in the center */
    static lv_style_t line_style;
    lv_style_init(&line_style);
    lv_style_set_line_width(&line_style, 3);
    // TODO: Set line color
    lv_style_set_line_color(&line_style, lv_color_hex(0xE0E0E0));
    lv_style_set_line_rounded(&line_style, true);

    lv_obj_t *center_line = lv_line_create(scr);
    static lv_point_precise_t points[] = {{UI_PROP_BORDER_PADDING_PX, LCD_V_RES / 2}, {LCD_H_RES - UI_PROP_BORDER_PADDING_PX, LCD_V_RES / 2}};
    lv_line_set_points(center_line, points, 2);
    lv_obj_add_style(center_line, &line_style, 0);

    // Reset button is the standard button for the start-stop screen
    reset_btn = lv_button_create(scr);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, -26);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(reset_btn, UI_PROP_INTERNAL_PADDING_PX, 0);
    lv_obj_set_style_pad_bottom(reset_btn, UI_PROP_INTERNAL_PADDING_PX, 0);
    lv_obj_set_style_pad_left(reset_btn, UI_PROP_INTERNAL_PADDING_PX * 2, 0);
    lv_obj_set_style_pad_right(reset_btn, UI_PROP_INTERNAL_PADDING_PX * 2, 0);

    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_set_style_text_color(reset_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(reset_label, &lv_font_montserrat_20, 0);

    lv_obj_update_layout(reset_label);
    lv_obj_update_layout(reset_btn);
    const int reset_btn_height = lv_obj_get_height(reset_btn);
    const int reset_btn_width = lv_obj_get_width(reset_btn);
    lv_obj_add_event_cb(reset_btn, reset_button_event_cb, LV_EVENT_RELEASED, NULL);

    // TODO: Fix offset
    start_stop_btn = lv_button_create(scr);
    lv_obj_align(start_stop_btn, LV_ALIGN_BOTTOM_MID, 0, -78);
    lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_height(start_stop_btn, reset_btn_height);
    lv_obj_set_width(start_stop_btn, reset_btn_width);

    lv_obj_t *start_stop_label = lv_label_create(start_stop_btn);
    lv_label_set_text(start_stop_label, "Start");
    lv_obj_set_style_text_color(start_stop_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(start_stop_label, &lv_font_montserrat_20, 0);
    lv_obj_set_align(start_stop_label, LV_ALIGN_CENTER);
    lv_obj_add_event_cb(start_stop_btn, start_stop_button_event_cb, LV_EVENT_RELEASED, start_stop_label);

    int set_time_btn_width = (LCD_H_RES - UI_PROP_BORDER_PADDING_PX * 2 - UI_PROP_INTERNAL_PADDING_PX * 2);
    set_time_btn_width = set_time_btn_width / 3;

    incr_hr_btn = lv_button_create(scr);
    lv_obj_t *incr_hr_label = lv_label_create(incr_hr_btn);
    lv_label_set_text(incr_hr_label, "H+");
    lv_obj_set_style_text_color(incr_hr_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(incr_hr_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(incr_hr_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_align(incr_hr_label, LV_ALIGN_CENTER);
    lv_obj_set_width(incr_hr_btn, set_time_btn_width);
    lv_obj_set_height(incr_hr_btn, reset_btn_height);
    lv_obj_align(incr_hr_btn, LV_ALIGN_BOTTOM_LEFT, UI_PROP_BORDER_PADDING_PX, -78);
    lv_obj_add_event_cb(incr_hr_btn, set_time_cb, LV_EVENT_RELEASED, &hr_incr_event);

    decr_hr_btn = lv_button_create(scr);
    lv_obj_set_style_bg_color(decr_hr_btn, lv_color_hex(0x333333), 0);
    lv_obj_t *decr_hr_label = lv_label_create(decr_hr_btn);
    lv_label_set_text(decr_hr_label, "H-");
    lv_obj_set_align(decr_hr_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(decr_hr_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(decr_hr_label, &lv_font_montserrat_20, 0);
    lv_obj_align(decr_hr_btn, LV_ALIGN_BOTTOM_LEFT, UI_PROP_BORDER_PADDING_PX, -26);
    lv_obj_set_width(decr_hr_btn, set_time_btn_width);
    lv_obj_set_height(decr_hr_btn, reset_btn_height);
    lv_obj_add_event_cb(decr_hr_btn, set_time_cb, LV_EVENT_RELEASED, &hr_decr_event);

    incr_min_btn = lv_button_create(scr);
    lv_obj_set_style_bg_color(incr_min_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_width(incr_min_btn, set_time_btn_width);
    lv_obj_set_height(incr_min_btn, reset_btn_height);
    lv_obj_t *incr_min_label = lv_label_create(incr_min_btn);
    lv_label_set_text(incr_min_label, "M+");
    lv_obj_set_style_text_color(incr_min_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(incr_min_label, &lv_font_montserrat_20, 0);
    lv_obj_set_align(incr_min_label, LV_ALIGN_CENTER);
    lv_obj_align(incr_min_btn, LV_ALIGN_BOTTOM_RIGHT, -UI_PROP_BORDER_PADDING_PX, -78);
    lv_obj_add_event_cb(incr_min_btn, set_time_cb, LV_EVENT_RELEASED, &min_incr_event);

    decr_min_btn = lv_button_create(scr);
    lv_obj_set_style_bg_color(decr_min_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_width(decr_min_btn, set_time_btn_width);
    lv_obj_set_height(decr_min_btn, reset_btn_height);
    lv_obj_t *decr_min_label = lv_label_create(decr_min_btn);
    lv_label_set_text(decr_min_label, "M-");
    lv_obj_set_align(decr_min_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(decr_min_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(decr_min_label, &lv_font_montserrat_20, 0);
    lv_obj_align(decr_min_btn, LV_ALIGN_BOTTOM_RIGHT, -UI_PROP_BORDER_PADDING_PX, -26);
    lv_obj_add_event_cb(decr_min_btn, set_time_cb, LV_EVENT_RELEASED, &min_decr_event);

    set_time_btn = lv_button_create(scr);
    lv_obj_set_style_bg_color(set_time_btn, lv_color_hex(0x333333), 0);
    lv_obj_set_width(set_time_btn, set_time_btn_width);
    lv_obj_set_height(set_time_btn, reset_btn_height);

    lv_obj_t *set_time_label = lv_label_create(set_time_btn);
    lv_label_set_text(set_time_label, "Set");
    lv_obj_set_align(set_time_label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_color(set_time_label, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(set_time_label, &lv_font_montserrat_20, 0);
    lv_obj_align(set_time_btn, LV_ALIGN_CENTER, 0, LCD_V_RES / 4);
    lv_obj_add_event_cb(set_time_btn, set_time_cb, LV_EVENT_RELEASED, &set_time_event);

    lv_obj_set_flag(incr_hr_btn, LV_OBJ_FLAG_HIDDEN, true);
    lv_obj_set_flag(incr_min_btn, LV_OBJ_FLAG_HIDDEN, true);
    lv_obj_set_flag(decr_hr_btn, LV_OBJ_FLAG_HIDDEN, true);
    lv_obj_set_flag(decr_min_btn, LV_OBJ_FLAG_HIDDEN, true);
    lv_obj_set_flag(set_time_btn, LV_OBJ_FLAG_HIDDEN, true);
}

int initialise_gui()
{
	initialise_lcd_hw();
    initialise_lvgl_framework();
    ui_init(lcd_disp);
    prev_tick = get_absolute_time();
	return 0;
}

void show_time(const uint32_t time_in_min)
{
    char time[10] = "";
    snprintf(time, sizeof(time), "%02d:%02d", time_in_min / 60, time_in_min % 60);
    lv_label_set_text(label_clock, time);
}

void tick_ui()
{
    const absolute_time_t curr_time = get_absolute_time();

    if (started) {
        if (curr_time - prev_tick > 1000 * 1000) {
            prev_tick = curr_time;

            active_time_min = (active_time_min > 0) ? (active_time_min - 1) : 0;
        }
    }
    else {
        prev_tick = curr_time;
    }

    if (ui_state == StartStopTime) {
        if (reset) {
            prev_tick = curr_time;
            active_time_min = set_time_min;
            reset = false;
            show_time(active_time_min);
        }

        if (started) {
            show_time(active_time_min);
        }
        else {
            if (refresh)
            {
                active_time_min = set_time_min;
                show_time(active_time_min);
                refresh = false;
            }
        }
    }
    else {
        show_time(display_set_time);
    }

    lv_timer_handler();
}
