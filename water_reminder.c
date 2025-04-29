#include <stdio.h>
#include "pico/stdlib.h"
#include "debug_messages.h"

#define ONE_SECOND_MS  (1000)

static repeating_timer_t debug_messages_timer;

static bool debug_messages_timer_cb(repeating_timer_t *rt);

int main()
{
    stdio_init_all();

    {
        const bool success = initialise_display_framework();
        if (!success) {
            printf("ERROR: Failed to initialise display framework.");
        }
    }

    {
        const bool success = add_repeating_timer_ms(ONE_SECOND_MS, debug_messages_timer_cb, NULL, &debug_messages_timer);
        if (!success) {
            printf("ERROR: Failed to create debug messages timer.");
        }
    }

    while (true) {
        check_for_messages();
    }
}


bool debug_messages_timer_cb(repeating_timer_t *rt)
{
    debug_msg_flush_count++;
}