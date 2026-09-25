#ifndef OPENOS_TIMER_H
#define OPENOS_TIMER_H

#include <stdint.h>

#define TIMER_HZ 100

void pit_init(uint32_t hz);
void timer_on_tick(void);
uint64_t timer_ticks(void);
uint64_t timer_uptime_ms(void);

#endif
