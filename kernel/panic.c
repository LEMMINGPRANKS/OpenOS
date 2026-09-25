#include "panic.h"
#include "term.h"

#define TERM_COLOR_WHITE_ON_RED 0x4F

void kpanic(const char *msg)
{
    __asm__ volatile ("cli");
    term_setcolor(TERM_COLOR_WHITE_ON_RED);
    term_clear();
    term_puts("\n\n   *** OPENOS KERNEL PANIC ***\n\n");
    term_puts("   ");
    term_puts(msg);
    term_puts("\n\n   The system is halted on purpose.\n");
    for (;;)
        __asm__ volatile ("hlt");
}
