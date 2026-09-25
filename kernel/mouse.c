#include "mouse.h"
#include "gfx.h"
#include "term.h"

// PS/2 mouse: enable the aux port on the 8042 controller, put the mouse
// in streaming mode, then eat 3-byte packets on IRQ12.

#define PS2_CMD  0x64
#define PS2_DATA 0x60
#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1

static int32_t mx, my;                  // screen position
static uint8_t buttons;
static uint8_t pkt[3];
static int cycle;

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

static void wait_write(void)
{
    for (int i = 0; i < 1000000; i++)
        if (!(inb(PS2_CMD) & 0x02))
            return;
}

static uint8_t wait_read(void)
{
    for (int i = 0; i < 1000000; i++)
        if (inb(PS2_CMD) & 0x01)
            return inb(PS2_DATA);
    return 0xFF;
}

static void drain_output(void)
{
    for (int i = 0; i < 1000 && (inb(PS2_CMD) & 0x01); i++)
        inb(PS2_DATA);
}

static void mouse_cmd(uint8_t b)
{
    wait_write();
    outb(PS2_CMD, 0xD4);                 // next byte goes to the mouse
    wait_write();
    outb(PS2_DATA, b);
    wait_read();                         // eat the 0xFA ack
}

void mouse_init(void)
{
    mx = 512;
    my = 384;

    // On QEMU, controller responses arrive tagged like keyboard bytes
    // and raise IRQ1 -- the keyboard ISR would eat them before our
    // polling sees them. Keep interrupts off for the whole handshake.
    __asm__ volatile ("cli");

    wait_write();
    outb(PS2_CMD, 0xA8);                 // enable aux port

    wait_write();                        // read controller config byte
    outb(PS2_CMD, 0xA8);                 // enable aux port

    wait_write();                        // read controller config byte
    uint8_t cfg = 0xFF;
    for (int tries = 0; tries < 4 && cfg == 0xFF; tries++) {
        drain_output();
        wait_write();
        outb(PS2_CMD, 0x20);
        cfg = wait_read();
    }
    __asm__ volatile ("sti");            // handshake done
    if (cfg == 0xFF) {
        // controller not talking: leave everything alone rather than
        // freezing the keyboard by writing a bogus config
        term_puts("[mouse] controller silent, mouse off\n");
        return;
    }
    cfg |= 0x03;                         // enable IRQ1 + IRQ12
    cfg &= ~(uint8_t)0x20;               // enable mouse clock
    wait_write();
    outb(PS2_CMD, 0x60);
    wait_write();
    outb(PS2_DATA, cfg);

    mouse_cmd(0xF6);                     // default settings
    mouse_cmd(0xF4);                     // start reporting

    // unmask cascade (IRQ2) + IRQ12 on the PICs
    outb(PIC1_DATA, inb(PIC1_DATA) & ~(uint8_t)0x04);
    outb(PIC2_DATA, inb(PIC2_DATA) & ~(uint8_t)0x10);
}

void mouse_on_irq(void)
{
    uint8_t st = inb(PS2_CMD);
    if (!(st & 0x01))
        return;
    uint8_t b = inb(PS2_DATA);
    if (!(st & 0x20))
        return;                          // keyboard byte, not ours

    if (cycle == 0 && !(b & 0x08))
        return;                          // desynced: wait for a real header
    pkt[cycle++] = b;
    if (cycle < 3)
        return;
    cycle = 0;

    buttons = pkt[0] & 0x07;
    int dx = (int32_t)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int dy = (int32_t)pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);

    mx += dx;
    my -= dy;                            // mouse Y grows upward
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (gfx_available()) {
        if (mx >= (int32_t)gfx_width())  mx = (int32_t)gfx_width() - 1;
        if (my >= (int32_t)gfx_height()) my = (int32_t)gfx_height() - 1;
    }
}

void mouse_get(int32_t *x, int32_t *y, uint8_t *btn)
{
    *x = mx;
    *y = my;
    *btn = buttons;
}
