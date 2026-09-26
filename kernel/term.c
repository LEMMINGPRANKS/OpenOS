#include "term.h"
#include "gfx.h"
#include "font.h"
#include "heap.h"

#define COM1_PORT 0x3F8
#define VGA_TEXT  ((volatile uint16_t *)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

static const uint32_t vga_pal[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

struct console {
    uint32_t vx, vy, vw, vh;            // pixel viewport
    uint16_t rows, cols;
    uint16_t crow, ccol;
    uint8_t color;                      // vga attr for new writes
    uint8_t *cells;                     // rows*cols*2: char, vga color
};

static struct console *active;

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

static int serial_ok;                   // real machines may have no COM1

static void serial_init(void)
{
    // probe the scratch register: if we can't write it, there's no port
    // (QEMU always has one; real PCs might not)
    outb(COM1_PORT + 7, 0x5A);
    if (inb(COM1_PORT + 7) != 0x5A) {
        serial_ok = 0;
        return;
    }
    serial_ok = 1;
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void serial_putc(char c)
{
    if (!serial_ok)
        return;
    int ready = 0;
    while (!ready)
        ready = inb(COM1_PORT + 5) & 0x20;
    outb(COM1_PORT, (uint8_t)c);
}

void term_init(void)
{
    serial_init();
}

// --- cell helpers -------------------------------------------------------

static uint8_t *cell(struct console *c, uint16_t r, uint16_t col)
{
    return c->cells + (r * c->cols + col) * 2;
}

static void render_cell(struct console *c, uint16_t r, uint16_t col)
{
    uint8_t *p = cell(c, r, col);
    gfx_char(c->vx + col * FONT_W, c->vy + r * FONT_H, (char)p[0],
             vga_pal[p[1] & 0xF], vga_pal[(p[1] >> 4) & 0xF]);
}

void term_render(struct console *c)
{
    if (!c)
        return;
    for (uint16_t r = 0; r < c->rows; r++)
        for (uint16_t col = 0; col < c->cols; col++)
            render_cell(c, r, col);
}

// --- console lifecycle --------------------------------------------------

struct console *term_open(uint32_t px, uint32_t py, uint32_t pw, uint32_t ph)
{
    struct console *c = kmalloc(sizeof *c);
    if (!c)
        return 0;
    c->vx = px; c->vy = py; c->vw = pw; c->vh = ph;
    c->cols = (uint16_t)(pw / FONT_W);
    c->rows = (uint16_t)(ph / FONT_H);
    c->crow = 0; c->ccol = 0;
    c->color = TERM_COLOR_WHITE_ON_BLUE;
    c->cells = kmalloc((uint32_t)c->rows * c->cols * 2);
    if (!c->cells) {
        kfree(c);
        return 0;
    }
    for (uint32_t i = 0; i < (uint32_t)c->rows * c->cols * 2; i += 2) {
        c->cells[i] = ' ';
        c->cells[i + 1] = TERM_COLOR_WHITE_ON_BLUE;
    }
    return c;
}

void term_close(struct console *c)
{
    if (!c)
        return;
    if (active == c)
        active = 0;
    kfree(c->cells);
    kfree(c);
}

void term_use(struct console *c)
{
    active = c;
    term_render(c);
}

void term_move(struct console *c, uint32_t px, uint32_t py)
{
    if (!c)
        return;
    c->vx = px;
    c->vy = py;
}

int term_locate(struct console *c, int mx, int my, int *col, int *row)
{
    if (!c)
        return 0;
    if (mx < (int)c->vx || my < (int)c->vy)
        return 0;
    int cc = (mx - (int)c->vx) / FONT_W;
    int rr = (my - (int)c->vy) / FONT_H;
    if (cc >= c->cols || rr >= c->rows)
        return 0;
    *col = cc;
    *row = rr;
    return 1;
}

// --- scrolling + putc ----------------------------------------------------

static void scroll(struct console *c)
{
    for (uint32_t r = 1; r < c->rows; r++)
        for (uint32_t col = 0; col < c->cols; col++) {
            uint8_t *dst = cell(c, (uint16_t)(r - 1), (uint16_t)col);
            uint8_t *src = cell(c, (uint16_t)r, (uint16_t)col);
            dst[0] = src[0];
            dst[1] = src[1];
        }
    for (uint32_t col = 0; col < c->cols; col++) {
        uint8_t *p = cell(c, (uint16_t)(c->rows - 1), (uint16_t)col);
        p[0] = ' ';
    }
    term_render(c);
}

static void newline(struct console *c)
{
    c->ccol = 0;
    if (++c->crow >= c->rows) {
        c->crow = c->rows - 1;
        scroll(c);
    }
}

static void putc_con(struct console *c, char ch)
{
    uint8_t *p;
    switch (ch) {
    case '\n':
        newline(c);
        return;
    case '\b':
        if (c->ccol > 0)
            c->ccol--;
        p = cell(c, c->crow, c->ccol);
        p[0] = ' ';
        render_cell(c, c->crow, c->ccol);
        return;
    case '\r':
        c->ccol = 0;
        return;
    case '\t':
        ch = ' ';
        break;
    }
    p = cell(c, c->crow, c->ccol);
    p[0] = (uint8_t)ch;
    p[1] = c->color;
    render_cell(c, c->crow, c->ccol);
    if (++c->ccol >= c->cols)
        newline(c);
}

// --- legacy VGA-text fallback (no framebuffer machines) ------------------

static int vrow, vcol;
static uint8_t vcolor = TERM_COLOR_WHITE_ON_BLUE;

static void vga_scroll(void)
{
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        VGA_TEXT[i] = VGA_TEXT[i + VGA_COLS];
    for (int i = VGA_COLS * (VGA_ROWS - 1); i < VGA_COLS * VGA_ROWS; i++)
        VGA_TEXT[i] = (uint16_t)(vcolor << 8 | ' ');
    vrow = VGA_ROWS - 1;
}

static void putc_vga(char c)
{
    if (c == '\n') {
        vcol = 0;
        if (++vrow >= VGA_ROWS) vga_scroll();
        return;
    }
    if (c == '\b') {
        if (vcol > 0) vcol--;
        return;
    }
    if (c == '\r') {
        vcol = 0;
        return;
    }
    VGA_TEXT[vrow * VGA_COLS + vcol] = (uint16_t)(vcolor << 8 | (uint8_t)c);
    if (++vcol >= VGA_COLS) {
        vcol = 0;
        if (++vrow >= VGA_ROWS) vga_scroll();
    }
}

// --- public API ----------------------------------------------------------

void term_putc(char c)
{
    serial_putc(c);
    if (active)
        putc_con(active, c);
    else if (!gfx_available())
        putc_vga(c);
}

void term_puts(const char *s)
{
    while (*s)
        term_putc(*s++);
}

void term_setcolor(uint8_t col)
{
    if (active)
        active->color = col;
    else
        vcolor = col;                  // VGA-text fallback
}

void term_clear(void)
{
    if (!active)
        return;
    for (uint32_t r = 0; r < active->rows; r++)
        for (uint32_t col = 0; col < active->cols; col++) {
            uint8_t *p = cell(active, (uint16_t)r, (uint16_t)col);
            p[0] = ' ';
            p[1] = active->color;
        }
    active->crow = 0;
    active->ccol = 0;
    term_render(active);
}
