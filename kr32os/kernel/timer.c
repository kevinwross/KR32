#include "timer.h"

static unsigned int ticks = 0;

void timer_init(void) {
    unsigned int *reload = (unsigned int *)0xFFF00004;
    unsigned int *irq = (unsigned int *)0xFFF00010;
    unsigned int *control = (unsigned int *)0xFFF00000;
    unsigned int *route = (unsigned int *)0xFFF03000;
    *route = 1;
    *reload = 10000;
    *irq = 0;
    *control = 7;
}

void timer_tick(void) {
    unsigned int *status = (unsigned int *)0xFFF0000C;
    *status = 1;
    ++ticks;
}

unsigned int timer_ticks(void) {
    return ticks;
}
