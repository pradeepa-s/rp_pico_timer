#include "touch_screen.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

#include "pico/time.h"
#include <stdio.h>

#define TOUCH_SCREEN_IRQ   (11)
#define GPIO_SPI1_CSn  (13)
#define GPIO_SPI1_RX   (12)
#define GPIO_SPI1_SCK  (14)
#define GPIO_SPI1_TX   (15)

#define READING_MAX   (1800)
#define READING_MIN   (200)
#define X_RESOLUTION  (240)
#define Y_RESOLUTION  (320)


// The touch screen used is XPT2046. (But use ADS7846 datasheet to understand how things work.)
// The working principle:
//      - There are two conductive layers in a touch screen, one for X axis, and the other for Y axis.
//      - Both axis have negative and positive terminals to apply voltage at either ends of the axis.
//      - When someone touch the panel, the two layers shorts at the touch point.
//      - Therefore, when we want to read X axis, apply voltage to the Y- and Y+ and then read X+ using the ADC.

//  Display          Touch
//  Origin           Origin
//     ----------------
//     |              |
//     |    SCREEN.   |
//     |              |
//     |              |
//     |              |
//     |              |
//     |              |
//     -----******-----
//       BOTTOM CONN
//
// Trial and error calibration:
//    Origin --> 200, 200
//    End --> 1800, 1800
//
//  Size of X = 240
//  Size of Y = 320
//  X = (240 * reading) / 1600
//  Y = (320 * reading) / 1600
//
//  The touch origin is converted to display origin here.

// Not sure why the reading is usually maxed at ~1800 and min at ~200. I was expecting it to go to 4096.
// This is detected using manual testing. This might be different for each LEDS. Therefore some calibration
// is needed.

static bool read = false;
static touch_point_t touch_point = {};

static int64_t alarm_cb_continue_read(alarm_id_t id, void *user_data)
{
    read = true;
    return 0;
}

static uint16_t sanitise_reading(const uint16_t reading, const uint16_t pixel_count,
                                 const uint16_t max_reading, const uint16_t min_reading)
{
    uint16_t out_reading = reading;

    // Clip the reading to range
    out_reading = out_reading < min_reading ? min_reading : out_reading;
    out_reading = out_reading > max_reading ? max_reading : out_reading;

    // Adjust for min read error (This is the minimum reading we get when we touch the origin point)
    out_reading -= min_reading;

    // Convert reading to pixels
    out_reading = (pixel_count * out_reading) / (max_reading - min_reading);

    return out_reading;
}

static bool queue_if_valid(const touch_point_t tp)
{
    if (tp.x > 0 && tp.y > 0)
    {
        touch_point.valid = true;
        touch_point.x = X_RESOLUTION - tp.x;
        touch_point.y = tp.y;
        // printf("Cord: %d:%d\n", tp.x, tp.y);
    }
    else
    {
        touch_point.valid = false;
        // printf("Rel\n");
    }
    return touch_point.valid;
}

static uint16_t get_reading(const uint8_t* buffer)
{
    // Only the first 12 bits have valid data.
    // Data is in the buffer MSB first.
    uint16_t reading = (buffer[0] << 8) | buffer[1];
    reading = reading >> 4;
    return reading;
}

void touch_irq()
{
    // We don't enable interrupts until we detect the release. The release is detected by
    // contnuously reading the touch sensor and evaluating the reading.
    gpio_acknowledge_irq(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL);
    gpio_set_irq_enabled(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL, false);
    read = true;
}

void init_touch_screen()
{
    gpio_init(TOUCH_SCREEN_IRQ);
    gpio_set_dir(TOUCH_SCREEN_IRQ, false);
    gpio_add_raw_irq_handler(TOUCH_SCREEN_IRQ, touch_irq);

    gpio_init(GPIO_SPI1_CSn);  // Software controlled
    gpio_init(GPIO_SPI1_RX);
    gpio_init(GPIO_SPI1_SCK);
    gpio_init(GPIO_SPI1_TX);

    gpio_set_dir(GPIO_SPI1_CSn, GPIO_OUT);
    gpio_set_function(GPIO_SPI1_RX, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI1_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI1_TX, GPIO_FUNC_SPI);
    const uint baud = spi_init(spi1, 100000);
    printf("SPI initialised with: %d baudrate\n\r", baud);

    gpio_set_irq_enabled(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL, true);
    gpio_put(GPIO_SPI1_CSn, true);
}

static touch_point_t read_touch_point()
{
    gpio_put(GPIO_SPI1_CSn, false);

    const uint8_t dummy = 0x00;
    const uint8_t read_y = 0b10010000;
    const uint8_t read_x = 0b11010000;
    uint8_t buffer[2];

    spi_write_blocking(spi1, &read_y, sizeof(read_y));
    spi_read_blocking(spi1, dummy, buffer, sizeof(buffer));
    uint16_t reading_y = get_reading(buffer);
    // printf("Y: %d\n", reading_y);
    reading_y = sanitise_reading(reading_y, Y_RESOLUTION, READING_MAX, READING_MIN);

    spi_write_blocking(spi1, &read_x, sizeof(read_x));
    spi_read_blocking(spi1, dummy, buffer, sizeof(buffer));

    uint16_t reading_x = get_reading(buffer);
    // printf("X: %d\n", reading_x);
    reading_x = sanitise_reading(reading_x, X_RESOLUTION, READING_MAX, READING_MIN);

    touch_point_t tp = {.x = reading_x, .y = reading_y};
    gpio_put(GPIO_SPI1_CSn, true);

    return tp;
}

void tick_touch_screen()
{
    if (read) {
        read = false;

        // Read touch point
        const touch_point_t tp = read_touch_point();
        const bool is_valid = queue_if_valid(tp);

        if (is_valid) {
            // If touch point is valid schedule another read in 200 ms.
            add_alarm_in_ms(200, alarm_cb_continue_read, NULL, true);
        }
        else {
            // If touch point is invalid, re-enable interrupts and stop reading.
            gpio_set_irq_enabled(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL, true);
        }
    }
}

touch_point_t get_touch_point()
{
    return touch_point;
}

