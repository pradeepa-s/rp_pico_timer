#include <pico/time.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "debug_messages.h"
#include "display_framework.h"
#include "tick_count.h"
#include "touch_screen.h"
#include "battery_monitor.h"

#define ONE_SECOND_MS  (1000)

static repeating_timer_t debug_messages_timer;
static repeating_timer_t battery_monitor_timer;

static bool debug_messages_timer_cb(repeating_timer_t *rt);
static bool check_battery_health_cb(repeating_timer_t *rt);

int main()
{
    stdio_init_all();
    setup_isr();

    {
        const bool success = (initialise_gui() == 0);
        if (!success) {
            printf("ERROR: Failed to initialise display framework.");
        }
    }

    {
        init_touch_screen();
    }

    {
        const bool success = add_repeating_timer_ms(ONE_SECOND_MS, debug_messages_timer_cb, NULL, &debug_messages_timer);
        if (!success) {
            printf("ERROR: Failed to create debug messages timer.");
        }
    }

    {
        battery_monitor_init();
        const bool success = add_repeating_timer_ms(ONE_SECOND_MS, check_battery_health_cb, NULL, &battery_monitor_timer);
    }

    while (true) {
        tick_ui();
        tick_touch_screen();
        tick_battery_monitor();
    }
}

bool debug_messages_timer_cb(repeating_timer_t *rt) {
    debug_msg_flush_count++;
}

bool check_battery_health_cb(repeating_timer_t *rt) {
    battery_monitor_cnt++;
}
