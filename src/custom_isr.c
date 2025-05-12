#include "tick_count.h"
#include "hardware/structs/systick.h"

static uint32_t tick_count = 0;

uint32_t get_tick_count()
{
    return tick_count;
}

void setup_isr()
{
    systick_hw_t* systick = systick_hw;
    systick->cvr = 0x00;
    systick->csr = 0x07;
    systick->rvr = 124999;
}

extern void isr_systick()
{
    tick_count++; 
}