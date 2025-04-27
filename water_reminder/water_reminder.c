#include "pico/stdlib.h"
#include "debug_messages.h"


static repeating_timer_t one_second_timer;

static bool one_second_timer_cb(repeating_timer_t *rt);

int main()
{
    stdio_init_all();

    bool success = add_repeating_timer_ms(1000, one_second_timer_cb, NULL, &one_second_timer);
    while (true) {
    }
}


bool one_second_timer_cb(repeating_timer_t *rt)
{
    debug_msg_flush_count++;
}