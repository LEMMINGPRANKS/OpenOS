#include "timer.h"

#define PIT_CMD  0x43
#define PIT_CH0  0x40
#define PIT_BASE_FREQ 1193182

static volatile uint64_t ticks;
static uint32_t tick_ms;

static void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(val), "d"(port));
}

void pit_init(uint32_t hz)
{
    uint32_t divisor = PIT_BASE_FREQ / hz;
    tick_ms = 1000 / hz;
    outb(PIT_CMD, 0x36);                       // channel 0, lobyte/hibyte, mode 3
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

void timer_on_tick(void)
{
    ticks++;
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t timer_uptime_ms(void)
{
    return ticks * tick_ms;
}
