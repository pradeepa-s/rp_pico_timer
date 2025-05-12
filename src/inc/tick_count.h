#ifndef _TICK_COUNT_H
#define _TICK_COUNT_H

#include <stdint.h>

void setup_isr();
uint32_t get_tick_count();

#endif