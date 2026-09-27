#include "paint.h"
#include "gfx.h"
#include "heap.h"
#include "files.h"
#include "ramfs.h"
#include "store.h"
#include "png.h"
#include "apps.h"
#include "desktop.h"

// Paint. The canvas is a plain RGB byte array; every repaint blits it to
// the framebuffer inside the window body, and mouse drags paint into it.
// S saves a real PNG to the filesystem (and DR1), C clears, E is eraser.

#define PAL_N 16
#define SIZES_N 4
#define STATUS_MAX 48
#define BAR_SWATCH_H 16                   // row 1 of the bar ends here

static const uint32_t palette[PAL_N] = {
    0x000000, 0x7F7F7F, 0x880015, 0xED1C24, 0xFF7F27, 0xFFF200, 0x22B14C,
    0x00A2E8, 0x3F48CC, 0xA349A4, 0xFFFFFF, 0xC3C3C3, 0xB97A57, 0xFFAEC9,
    0xEFE4B0, 0xB5E61D
};
static const int sizes[SIZES_N] = { 1, 2, 4, 8 };
static const char *msg_default = "draw!  S=save C=clear E=eraser";

static uint8_t *canvas;                   // PAINT_W*PAINT_H*3, kmalloc'd
static uint32_t body_x, body_y;           // window body origin (from repaint)
static int pal_sel = 9;                   // purple, a good default
static int size_sel = 2;
static int eraser;
static int last_x = -1, last_y;           // previous brush spot
static uint8_t prev_left;
static char status[STATUS_MAX];

static void set_status(const char *s)
{
    int i = 0;
    for (; s[i] && i < STATUS_MAX - 1; i++)
        status[i] = s[i];
    status[i] = 0;
}

static uint8_t alive(void)
{
    if (canvas)
        return 1;
    canvas = kmalloc(PAINT_W * PAINT_H * 3);
    if (!canvas)
        return 0;
    for (uint32_t i = 0; i < PAINT_W * PAINT_H * 3; i++)
        canvas[i] = 0xFF;
    return 1;
}

static void canvas_fill(uint32_t rgb)
{
    for (uint32_t i = 0; i < (uint32_t)PAINT_W * PAINT_H; i++) {
        canvas[i * 3] = (uint8_t)(rgb >> 16);
        canvas[i * 3 + 1] = (uint8_t)(rgb >> 8);
        canvas[i * 3 + 2] = (uint8_t)rgb;
    }
}

static void blit_rect(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= PAINT_W) x1 = PAINT_W - 1;
    if (y1 >= PAINT_H) y1 = PAINT_H - 1;
    for (int r = y0; r <= y1; r++)
        for (int c = x0; c <= x1; c++) {
            const uint8_t *p = canvas + ((uint64_t)r * PAINT_W + c) * 3;
            gfx_pixel(body_x + 1 + (uint32_t)c, body_y + 1 + (uint32_t)r,
                      ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]);
        }
}

static void blit(void)
{
    blit_rect(0, 0, PAINT_W - 1, PAINT_H - 1);
}

static void draw_bar(void)
{
    uint32_t by = body_y + 1 + PAINT_H;
    gfx_fill_rect(body_x + 1, by, PAINT_W, PAINT_BAR_H, 0x777777);
    for (int i = 0; i < PAL_N; i++) {          // row 1: colour swatches
        uint32_t sx = body_x + 5 + (uint32_t)i * 20;
        gfx_fill_rect(sx, by + 2, 16, 12, palette[i]);
        gfx_rect(sx, by + 2, 16, 12,
                 i == pal_sel && !eraser ? 0xFFFFFF : 0x333333);
    }
    for (int i = 0; i < SIZES_N; i++) {        // row 2: brush sizes as dots
        uint32_t sx = body_x + 5 + (uint32_t)i * 20;
        int s = sizes[i];
        gfx_fill_rect(sx + 8 - (uint32_t)s / 2, by + 22 - (uint32_t)s / 2,
                      (uint32_t)s, (uint32_t)s,
                      eraser ? 0xFFFFFF : palette[pal_sel]);
        gfx_rect(sx, by + 16, 16, 14,
                 i == size_sel ? 0xFFFFFF : 0x333333);
    }
    const char *btns[3] = { "E", "C", "S" };   // eraser / clear / save
    for (int i = 0; i < 3; i++) {
        uint32_t sx = body_x + 90 + (uint32_t)i * 24;
        gfx_fill_rect(sx, by + 16, 20, 14, 0x3050C8);
        gfx_text(sx + 6, by + 18, btns[i], 0xFFFFFF, 0x3050C8);
        if (i == 0 && eraser)
            gfx_rect(sx, by + 16, 20, 14, 0xFFFFFF);
    }
    gfx_text(body_x + 170, by + 18, status, 0xFFFFFF, 0x777777);
}

void paint_repaint(struct console *con, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h)
{
    (void)con; (void)w; (void)h;
    body_x = x;
    body_y = y;
    if (!alive())
        return;
    blit();
    draw_bar();
}

static void dot(int cx, int cy, uint32_t col)
{
    int s = eraser ? sizes[size_sel] * 2 : sizes[size_sel];
    for (int dy = -(s / 2); dy <= (s - 1) / 2; dy++)
        for (int dx = -(s / 2); dx <= (s - 1) / 2; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px < 0 || px >= PAINT_W || py < 0 || py >= PAINT_H)
                continue;
            uint8_t *p = canvas + ((uint64_t)py * PAINT_W + px) * 3;
            p[0] = (uint8_t)(col >> 16);
            p[1] = (uint8_t)(col >> 8);
            p[2] = (uint8_t)col;
        }
}

static void stroke(int x0, int y0, int x1, int y1, uint32_t col)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int steps = dx > dy ? dx : dy;
    if (steps == 0) {
        dot(x1, y1, col);
        return;
    }
    for (int i = 0; i <= steps; i++)
        dot(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, col);
}

static void save(void)
{
    // find the first free /paint.png, /paint2.png, ...
    char name[16];
    int k = 0;
    const char *pre = "/paint";
    for (; pre[k]; k++) name[k] = pre[k];
    name[k++] = '.';
    name[k++] = 'p';
    name[k++] = 'n';
    name[k++] = 'g';
    name[k] = 0;
    for (int i = 0; i < 98; i++) {
        uint32_t sz = 0;
        if (!files_read(name, &sz))
            break;
        k = 6;
        if (i >= 9)
            name[k++] = (char)('0' + (i + 1) / 10);
        name[k++] = (char)('0' + (i + 1) % 10);
        name[k++] = '.';
        name[k++] = 'p';
        name[k++] = 'n';
        name[k++] = 'g';
        name[k] = 0;
    }

    uint32_t out_max = PAINT_H * (1 + PAINT_W * 3) + 4096;
    uint8_t *buf = kmalloc(out_max);
    if (!buf) {
        set_status("out of memory -- not saved");
        return;
    }
    int32_t n = png_encode(canvas, PAINT_W, PAINT_H, buf, out_max);
    if (n < 0 || ramfs_write(name, (const char *)buf, (uint32_t)n) != 0) {
        set_status("save failed!");
        kfree(buf);
        return;
    }
    kfree(buf);
    store_flush();
    char msg[32];
    int m = 0;
    const char *s = "saved ";
    for (; s[m]; m++) msg[m] = s[m];
    for (int i = 0; name[i] && m < 31; i++) msg[m++] = name[i];
    msg[m] = 0;
    set_status(msg);
    desktop_repaint();                    // a brand-new png icon appears
}

static void bar_click(int lx, int ly)
{
    if (ly < BAR_SWATCH_H) {              // row 1: swatches
        int i = (lx - 5) / 20;
        if (lx >= 5 && i < PAL_N) {
            pal_sel = i;
            eraser = 0;
        }
        return;
    }
    if (lx >= 5 && lx < 5 + SIZES_N * 20) {   // row 2: brush sizes
        int i = (lx - 5) / 20;
        if (i < SIZES_N)
            size_sel = i;
        return;
    }
    if (lx >= 90 && lx < 90 + 3 * 24) {   // E / C / S buttons
        int i = (lx - 90) / 24;
        if (i == 0)
            eraser = !eraser;
        else if (i == 1) {
            canvas_fill(0xFFFFFF);
            blit();
        } else
            save();
    }
}

void paint_pointer(struct console *con, int mx, int my, int left)
{
    (void)con;
    if (!alive())
        return;
    int lx = mx - (int)(body_x + 1);
    int ly = my - (int)(body_y + 1);
    uint8_t press = left && !prev_left;
    prev_left = (uint8_t)left;

    if (lx < 0 || lx >= PAINT_W || ly < 0 || ly >= PAINT_H + PAINT_BAR_H) {
        last_x = -1;
        return;
    }
    if (ly >= PAINT_H) {                  // the tool bar
        if (press)
            bar_click(lx, ly - PAINT_H);
        last_x = -1;
        return;
    }
    if (left) {
        uint32_t col = eraser ? 0xFFFFFF : palette[pal_sel];
        int s = eraser ? sizes[size_sel] * 2 : sizes[size_sel];
        int pad = s + 1;                    // cover the brush + interpolation
        if (last_x < 0) {
            dot(lx, ly, col);
            blit_rect(lx - pad, ly - pad, lx + pad, ly + pad);
        } else {
            stroke(last_x, last_y, lx, ly, col);
            int xlo = last_x < lx ? last_x : lx, xhi = last_x > lx ? last_x : lx;
            int ylo = last_y < ly ? last_y : ly, yhi = last_y > ly ? last_y : ly;
            blit_rect(xlo - pad, ylo - pad, xhi + pad, yhi + pad);
        }
        last_x = lx;
        last_y = ly;
    } else {
        last_x = -1;
    }
}

void paint_input(struct console *con, char c)
{
    (void)con;
    if (!alive())
        return;
    if (c == 's')
        save();
    else if (c == 'c') {
        canvas_fill(0xFFFFFF);
        blit();
    } else if (c == 'e')
        eraser = !eraser;
    else if (c == '[') {
        if (size_sel > 0)
            size_sel--;
    } else if (c == ']') {
        if (size_sel < SIZES_N - 1)
            size_sel++;
    }
    draw_bar();
}

void paint_open(struct console *con)
{
    (void)con;
    if (!alive())
        return;
    const char *arg = app_get_arg();
    if (arg && arg[0]) {
        uint32_t n = 0;
        const uint8_t *data = (const uint8_t *)files_read(arg, &n);
        uint32_t w = 0, h = 0;
        if (data && png_decode(data, n, canvas,
                               PAINT_W * PAINT_H * 3, &w, &h) == 0 &&
            w == PAINT_W && h == PAINT_H) {
            set_status(arg);
        } else {
            canvas_fill(0xFFFFFF);
            set_status("could not open that png");
        }
    } else {
        canvas_fill(0xFFFFFF);
        set_status(msg_default);
    }
    blit();                                // cover anything drawn over us
    draw_bar();
}
