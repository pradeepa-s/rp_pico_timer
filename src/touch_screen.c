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

// The touch screen used is XPT2046.
// The working principle:
//      - There are two conductive layers in a touch screen, one for X axis, and the other for Y axis.
//      - Both axis have negative and positive terminals to apply voltage at either ends of the axis.
//      - When someone touch the panel, the two layers shorts at the touch point.
//      - Therefore, when we want to read X axis, apply voltage to the Y- and Y+ and then read X+ using the ADC.

//          X <---   0, 0
//     ----------------
//     |              |
//     |              |
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

static bool touch_detected = false;

static int64_t enable_touch_interrupt(alarm_id_t id, void *user_data)
{
    touch_detected = false;
    gpio_set_irq_enabled(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL, true);
    return 0;
}

void touch_irq()
{
    gpio_acknowledge_irq(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL);
    gpio_set_irq_enabled(TOUCH_SCREEN_IRQ, GPIO_IRQ_EDGE_FALL, false);
    touch_detected = true;
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

void tick_touch_screen()
{
    if (touch_detected)
    {
        gpio_put(GPIO_SPI1_CSn, false);

        const uint8_t dummy = 0x00;
        const uint8_t read_y = 0b10010000;
        const uint8_t read_x = 0b11010000;
        uint8_t buffer[2];

        spi_write_blocking(spi1, &read_y, sizeof(read_y));
        sleep_ms(50); 
        spi_read_blocking(spi1, dummy, buffer, sizeof(buffer));
        uint16_t y_reading = (buffer[0] << 8) | buffer[1];
        y_reading = y_reading >> 4;
        y_reading = y_reading < 200 ? 200 : y_reading;
        y_reading = y_reading > 1800 ? 1800 : y_reading;
        y_reading -= 200;
        y_reading = (320 * y_reading) / 1600;

        spi_write_blocking(spi1, &read_x, sizeof(read_x));
        sleep_ms(50); 
        spi_read_blocking(spi1, dummy, buffer, sizeof(buffer));

        uint16_t reading_x = (buffer[0] << 8) | buffer[1];
        reading_x = reading_x >> 4;
        reading_x = reading_x < 200 ? 200 : reading_x;
        reading_x = reading_x > 1800 ? 1800 : reading_x;
        reading_x -= 200;
        reading_x = (240 * reading_x) / 1600;

        if (reading_x > 0 && y_reading > 0)
        {
            printf("Coord: (%d, %d)\n", reading_x, y_reading);
        }
        gpio_put(GPIO_SPI1_CSn, true);

        add_alarm_in_ms(200, enable_touch_interrupt, NULL, true);
        touch_detected = false;
    }
}