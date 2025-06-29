#include "battery_monitor.h"
#include "hardware/adc.h"

#include <stdio.h>

int battery_monitor_cnt = 0;
static int prev_cnt = 0;

void battery_monitor_init()
{
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
}

void tick_battery_monitor()
{
    if (prev_cnt != battery_monitor_cnt)
    {
        prev_cnt = battery_monitor_cnt;
        // Sample the ADC to check the battery health.
        const uint16_t result = adc_read();
        printf("ADC reading: %d\n", result);
    }
}