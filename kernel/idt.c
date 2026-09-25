#include "idt.h"
#include "term.h"
#include "timer.h"
#include "kb.h"
#include "mouse.h"
#include "dev.h"

#define IDT_ENTRIES 48
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void *isr_stub_table[IDT_ENTRIES];

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

static void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(val), "d"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(v) : "d"(port));
    return v;
}

static void io_wait(void)
{
    outb(0x80, 0);
}

static void idt_set(uint8_t n, uint64_t handler)
{
    idt[n].offset_lo = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].ist = 0;
    idt[n].type_attr = 0x8E;     // present, ring 0, interrupt gate
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_hi = (uint32_t)(handler >> 32);
    idt[n].zero = 0;
}

static void pic_remap(void)
{
    outb(PIC1_CMD, 0x11); io_wait();   // ICW1: init + ICW4 needed
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();  // ICW2: PIC1 -> vectors 32-47
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();  // ICW3: slave on IRQ2
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();  // ICW4: 8086 mode
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFC);             // unmask IRQ0 (timer) + IRQ1 (keyboard)
    outb(PIC2_DATA, 0xFF);             // mask all on slave
}

void pic_eoi(uint64_t vector)
{
    if (vector >= 40)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void idt_init(void)
{
    for (int i = 0; i < IDT_ENTRIES; i++)
        idt_set((uint8_t)i, (uint64_t)isr_stub_table[i]);

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idtp));

    pic_remap();
}

void isr_dispatch(uint64_t vector, void *frame)
{
    (void)frame;
    if (vector == 32) {                 // IRQ0: PIT timer
        timer_on_tick();
        ur1_poll();                     // UR1 watchdog check
        pic_eoi(vector);
        return;
    }
    if (vector == 33) {                 // IRQ1: PS/2 keyboard
        kb_on_scancode(inb(0x60));
        pic_eoi(vector);
        return;
    }
    if (vector == 44) {                 // IRQ12: PS/2 mouse
        mouse_on_irq();
        pic_eoi(vector);
        return;
    }
    if (vector < 32) {                  // CPU exception: something is wrong
        term_puts("\n*** EXCEPTION! vector ");
        char hex[3] = { "0123456789ABCDEF"[vector >> 4], "0123456789ABCDEF"[vector & 0xF], 0 };
        term_puts(hex);
        term_puts(" -- the kernel is hurt. Halting.\n");
        for (;;)
            __asm__ volatile ("cli; hlt");
    }
    pic_eoi(vector);
}
