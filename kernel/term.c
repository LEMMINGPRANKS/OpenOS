#include "term.h"
#include "gfx.h"
#include "font.h"
#include "heap.h"

#define COM1_PORT 0x3F8
#define VGA_TEXT  ((volatile uint16_t *)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

// 2015-OS light theme: consoles still store classic VGA attributes, but
// rendering maps them through these two palettes -- every background becomes
// a white paper, every foreground a dark ink. One choke point, every app
// restyles at once.
static const uint32_t ink_pal[16] = {
    0x3A3A42, 0x1E5AA8, 0x2E7D32, 0x00838F,   // black, blue, green, cyan
    0xC62828, 0x8E24AA, 0xB8860B, 0x8A8A92,   // red, magenta, brown, grey
    0x9A9AA4, 0x3B78C7, 0x43A047, 0x00ACC1,   // dark grey + bright variants
    0xE53935, 0xAB47BC, 0xB8860B, 0x28282E,   // bright red/purple/gold/white
};
static const uint32_t paper_pal[16] = {
    0xFFFFFF, 0xFFFFFF, 0xF3F9F3, 0xF0F7F8,   // black+blue -> paper white
    0xFDF3F3, 0xF8F2FA, 0xFDF8EF, 0xEAEAEF,   // faint tints, grey selection
    0xF4F4F7, 0xF4F4F7, 0xF4F4F7, 0xF4F4F7,
    0xF4F4F7, 0xF4F4F7, 0xF4F4F7, 0xFFFFFF,
};

// Scrollback: every line that scrolls off the top is kept in a per-console
// ring (SB_LINES lines). The screen can then be scrolled back through
// history like a real terminal -- history lines + live rows are one virtual
// buffer. 256 lines costs ~88 KiB per console; if the heap can't pay,
// scrollback silently doesn't exist for that console and all else is normal.
#define SB_LINES 256

struct console {
    uint32_t vx, vy, vw, vh;            // pixel viewport
    uint16_t rows, cols;
    uint16_t crow, ccol;
    uint16_t protect;                   // top rows owned by pixel toolbars
    uint8_t color;                      // vga attr for new writes
    uint8_t *cells;                     // rows*cols*2: char, vga color
    uint8_t *hist;                      // SB_LINES*cols*2 ring, or 0
    uint16_t hcount;                    // lines in the ring (<= SB_LINES)
    uint16_t hhead;                     // next ring line to overwrite
    uint16_t sbview;                    // lines scrolled back (0 = live)
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
    if (c->sbview)
        return;                         // scrolled back: screen shows history
    uint8_t *p = cell(c, r, col);
    gfx_char(c->vx + col * FONT_W, c->vy + r * FONT_H, (char)p[0],
             ink_pal[p[1] & 0xF], paper_pal[(p[1] >> 4) & 0xF]);
}

// The virtual line at index v: 0..hcount-1 = history (oldest first),
// then the live rows from `protect` down. Returns NULL if v is live.
static const uint8_t *virt_line(const struct console *c, uint32_t v)
{
    if (c->hist && v < c->hcount) {
        uint32_t line = ((uint32_t)c->hhead + SB_LINES - c->hcount + v)
                        % SB_LINES;
        return c->hist + line * c->cols * 2;
    }
    return 0;
}

void term_render(struct console *c)
{
    if (!c)
        return;
    uint16_t visrows = (uint16_t)(c->rows - c->protect);
    uint32_t total = c->hcount + visrows;
    uint32_t start = total - visrows;   // bottom view shows the live rows
    if (c->sbview) {
        uint32_t back = c->sbview;
        if (back > c->hcount)
            back = c->hcount;
        start = total - visrows - back;
    }
    for (uint16_t r = c->protect; r < c->rows; r++) {
        uint32_t v = start + (uint32_t)(r - c->protect);
        const uint8_t *src = c->hist ? virt_line(c, v) : 0;
        if (!src) {
            uint16_t lr = (uint16_t)(c->protect + (v - c->hcount));
            src = cell(c, lr, 0);
        }
        for (uint16_t col = 0; col < c->cols; col++) {
            const uint8_t *p = src + col * 2;
            gfx_char(c->vx + col * FONT_W, c->vy + r * FONT_H, (char)p[0],
                     ink_pal[p[1] & 0xF], paper_pal[(p[1] >> 4) & 0xF]);
        }
    }
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
    c->hist = kmalloc((uint32_t)SB_LINES * c->cols * 2);   // 0 = no scrollback
    c->hcount = 0;
    c->hhead = 0;
    c->sbview = 0;
    return c;
}

void term_close(struct console *c)
{
    if (!c)
        return;
    if (active == c)
        active = 0;
    kfree(c->cells);
    kfree(c->hist);
    kfree(c);
}

void term_use(struct console *c)
{
    active = c;
    term_render(c);
}

struct console *term_active(void)
{
    return active;
}

void term_view(const struct console *c, uint32_t *x, uint32_t *y,
               uint32_t *w, uint32_t *h)
{
    if (!c) { *x = *y = *w = *h = 0; return; }
    *x = c->vx; *y = c->vy; *w = c->vw; *h = c->vh;
}

void term_move(struct console *c, uint32_t px, uint32_t py)
{
    if (!c)
        return;
    c->vx = px;
    c->vy = py;
}

int term_goto(struct console *c, int col, int row)
{
    if (!c || col < 0 || row < 0 || col >= c->cols || row >= c->rows)
        return 0;
    c->crow = (uint16_t)row;
    c->ccol = (uint16_t)col;
    return 1;
}

uint16_t term_cols(struct console *c)
{
    return c ? c->cols : 0;
}

uint16_t term_visible_rows(struct console *c)
{
    return c ? (uint16_t)(c->rows - c->protect) : 0;
}

void term_pos(struct console *c, int *col, int *row)
{
    if (!c) { *col = 0; *row = 0; return; }
    *col = c->ccol;
    *row = c->crow;
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
    if (c->hist) {                       // the leaving line becomes history
        uint8_t *dst = c->hist + (uint32_t)c->hhead * c->cols * 2;
        const uint8_t *src = cell(c, c->protect, 0);
        for (uint32_t i = 0; i < (uint32_t)c->cols * 2; i++)
            dst[i] = src[i];
        c->hhead = (uint16_t)((c->hhead + 1) % SB_LINES);
        if (c->hcount < SB_LINES)
            c->hcount++;
    }
    for (uint32_t r = c->protect + 1; r < c->rows; r++)
        for (uint32_t col = 0; col < c->cols; col++) {
            uint8_t *dst = cell(c, (uint16_t)(r - 1), (uint16_t)col);
            uint8_t *src = cell(c, (uint16_t)r, (uint16_t)col);
            dst[0] = src[0];
            dst[1] = src[1];
        }
    for (uint32_t col = 0; col < c->cols; col++) {
        uint8_t *p = cell(c, (uint16_t)(c->rows - 1), (uint16_t)col);
        p[0] = ' ';
        p[1] = c->color;                // old colours must not leak through
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
    for (uint32_t r = active->protect; r < active->rows; r++)
        for (uint32_t col = 0; col < active->cols; col++) {
            uint8_t *p = cell(active, (uint16_t)r, (uint16_t)col);
            p[0] = ' ';
            p[1] = active->color;
        }
    active->crow = active->protect;
    active->ccol = 0;
    active->sbview = 0;
    term_render(active);
}

// reserve the top n rows for a pixel toolbar: scrolling and full re-renders
// leave them alone, so drawn chrome survives anything the app prints
void term_protect(struct console *con, int rows)
{
    if (!con)
        return;
    if (rows < 0)
        rows = 0;
    if (rows > con->rows - 2)
        rows = con->rows - 2;
    con->protect = (uint16_t)rows;
    if (con->crow < con->protect)
        con->crow = con->protect;
    if (con == active)
        term_render(con);
}

// --- scrollback navigation ----------------------------------------------
// +lines = back in time, -lines = forward. New output does NOT yank the
// view back (xterm behaviour); typing does, via term_scroll_end.

void term_scroll_by(struct console *c, int lines)
{
    if (!c || !c->hist || !lines)
        return;
    int nv = (int)c->sbview + lines;
    if (nv < 0)
        nv = 0;
    if (nv > c->hcount)
        nv = c->hcount;
    if (nv == c->sbview)
        return;
    c->sbview = (uint16_t)nv;
    term_render(c);
}

void term_scroll_home(struct console *c)
{
    if (c && c->hist && c->sbview != c->hcount) {
        c->sbview = c->hcount;
        term_render(c);
    }
}

void term_scroll_end(struct console *c)
{
    if (c && c->sbview) {
        c->sbview = 0;
        term_render(c);
    }
}

uint16_t term_scroll_view(const struct console *c)
{
    return c ? c->sbview : 0;
}
