#include "term.h"
#include "idt.h"
#include "timer.h"
#include "initrd.h"
#include "mm.h"
#include "heap.h"
#include "gfx.h"
#include "mouse.h"
#include "ramfs.h"
#include "desktop.h"

#define MB2_BOOT_MAGIC 0x36D76289

void kmain(unsigned long magic, unsigned long addr)
{
    // parse the firmware handoff FIRST -- the console needs to know
    // whether a framebuffer exists before it picks its backend
    if (magic == MB2_BOOT_MAGIC) {
        initrd_init(addr);              // IR2: find the initramfs
        mm_init(addr);                  // physical memory from firmware map
        gfx_init(addr);                 // linear framebuffer, if GRUB gave us one
    }

    term_init();
    heap_init();
    pit_init(TIMER_HZ);
    idt_init();
    __asm__ volatile ("sti");           // the kernel gets a heartbeat
    mouse_init();                       // and a mouse (IRQ12)

    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("\n   OpenOS 1.0.0\n");
    term_puts("   booting to desktop...\n\n");
    term_setcolor(TERM_COLOR_WHITE_ON_BLACK);

    if (magic != MB2_BOOT_MAGIC)
        term_puts("   warning: multiboot2 magic check FAILED\n");

    ramfs_init();
    desktop_run();                     // boots into desktop (or plain shell)
}
