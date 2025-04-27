#include "debug_messages.h"
#include <stdio.h>

int32_t debug_msg_flush_count;
static int32_t prev_debug_msg_flush_count = 0;

void check_for_messages()
{
    if (prev_debug_msg_flush_count != debug_msg_flush_count) {
        prev_debug_msg_flush_count = debug_msg_flush_count;
        printf("Hello!\n\r");
    }
}