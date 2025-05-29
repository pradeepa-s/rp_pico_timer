#ifndef _TOUCH_SCREEN_H
#define _TOUCH_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    uint16_t x;
    uint16_t y;
} touch_point_t;

void init_touch_screen();

// The following functions must be executed in normal context.
// They should never interrupt each other.
void tick_touch_screen();

// Read the last touch point. The last touch point is latching until released.
touch_point_t get_touch_point();

#endif   // _TOUCH_SCREEN_H
