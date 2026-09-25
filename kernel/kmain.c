#include "term.h"

#define MB2_BOOT_MAGIC 0x36D76289

void kmain(unsigned long magic, unsigned long addr)
{
    term_init();
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_clear();

    term_puts("\n   OpenOS 0.0.1\n");
    term_puts("   64-bit long mode: ACHIEVED\n\n");
    term_setcolor(TERM_COLOR_WHITE_ON_BLACK);

    if (magic == MB2_BOOT_MAGIC)
        term_puts("   multiboot2 magic check: OK\n");
    else
        term_puts("   multiboot2 magic check: FAILED\n");

    term_puts("   kernel alive at 0x100000. Hello, Freddie!\n\n");
    term_puts("   Next milestone: interrupts (the kernel's nervous system).\n");

    for (;;)
        __asm__ volatile ("hlt");
}
