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
#include "ata.h"
#include "store.h"
#include "part.h"
#include "kupdate.h"
#include "gpu.h"
#include "version.h"

#define MB2_BOOT_MAGIC 0x36D76289

void kmain(unsigned long magic, unsigned long addr)
{
    // parse the firmware handoff FIRST -- the console needs to know
    // whether a framebuffer exists before it picks its backend
    if (magic == MB2_BOOT_MAGIC) {
        initrd_init(addr);              // IR2: find the initramfs
        mm_init(addr);                  // physical memory from firmware map
        gfx_init(addr);                 // linear framebuffer, if GRUB gave us one
        kupdate_scan_mb2(addr);         // which A/B slot booted (OpenBIOS tag)
    }

    term_init();
    heap_init();
    pit_init(TIMER_HZ);
    idt_init();
    __asm__ volatile ("sti");           // the kernel gets a heartbeat
    mouse_init();                       // and a mouse (IRQ12)
    ata_init();                         // DR1: the main drive, if there is one
    part_scan();                        // find our partition on each disk
    gpu_probe();                        // the NVIDIA detective takes the case

    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("\n   OpenOS " OS_VERSION "\n");
    term_puts("   gpu: ");
    term_puts(gpu_ident());
    term_putc('\n');
    term_puts("   booting to desktop...\n\n");
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);

    if (magic != MB2_BOOT_MAGIC)
        term_puts("   warning: multiboot2 magic check FAILED\n");

    ramfs_init();
    int restored = store_load();        // DR1: the filesystem, restored at boot
    if (restored > 0) {
        term_puts("   DR1: ");
        // print_u64 is in shell.c; keep it simple with a small loop-free int
        char digits[12];
        int n = 0, v = restored;
        if (!v) digits[n++] = '0';
        while (v) { digits[n++] = (char)('0' + v % 10); v /= 10; }
        while (n) term_putc(digits[--n]);
        term_puts(" files restored from disk\n");
    }
    if (store_migrated())
        term_puts("   DR1: filesystem moved onto the store drive\n");

    kupdate_confirm();                 // candidate booted + is alive: promote it

    desktop_run();                     // boots into desktop (or plain shell)
}
