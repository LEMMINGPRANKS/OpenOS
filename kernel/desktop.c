#include "desktop.h"
#include "gfx.h"
#include "font.h"
#include "mouse.h"
#include "kb.h"
#include "timer.h"
#include "term.h"
#include "wm.h"
#include "apps.h"
#include "shell.h"

// The desktop OpenOS boots into: wallpaper, taskbar with app launchers
// and a live clock, and windows managed by wm.c.

#define TASKBAR_H   32
#define CURSOR_W    8
#define CURSOR_H    8

#define TB_BADGE_X  8
#define TB_BADGE_W  (6 * FONT_W + 12)
#define TB_BTN_Y    6
#define TB_BTN_H    20
#define TB_BTN_GAP  8
#define TB_SHELL_X  (TB_BADGE_X + TB_BADGE_W + TB_BTN_GAP)
#define TB_SHELL_W  (5 * FONT_W + 12)
#define TB_NOTE_X   (TB_SHELL_X + TB_SHELL_W + TB_BTN_GAP)
#define TB_NOTE_W   (7 * FONT_W + 12)
#define TB_FILES_X  (TB_NOTE_X + TB_NOTE_W + TB_BTN_GAP)
#define TB_FILES_W  (5 * FONT_W + 12)

#define SHELL_WIN_W 496
#define SHELL_WIN_H 320
#define NOTE_WIN_W  432
#define NOTE_WIN_H  300
#define CASCADE_STEP 24
#define CASCADE_MAX  6

static const unsigned char arrow[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
};

static uint32_t cursor_saved[CURSOR_W * CURSOR_H];
static int cursor_shown;
static int32_t last_mx, last_my;

static void wallpaper(void)
{
    uint32_t w = gfx_width(), h = gfx_height() - TASKBAR_H;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t t = y * 256 / h;                 // 0 at top, 255 at bottom
        uint32_t r = (10 * (255 - t)) / 255;
        uint32_t g = (24 * (255 - t)) / 255;
        uint32_t b = (90 * (255 - t) + 20 * t) / 255;
        gfx_fill_rect(0, y, w, 1, (r << 16) | (g << 8) | b);
    }
    gfx_text_fg(24, 28, "OpenOS", 0x2A3C5E);
    gfx_text_fg(24, 28 + FONT_H + 4, "0.5.0 desktop", 0x2A3C5E);
}

static void taskbar(void)
{
    uint32_t w = gfx_width();
    uint32_t y = gfx_height() - TASKBAR_H;
    gfx_fill_rect(0, y, w, TASKBAR_H, 0x181818);
    gfx_fill_rect(0, y, w, 2, 0x3A5FCD);
    gfx_fill_rect(TB_BADGE_X, y + TB_BTN_Y, TB_BADGE_W, TB_BTN_H, 0x2244AA);
    gfx_text(TB_BADGE_X + 6, y + TB_BTN_Y + 2, "OpenOS", 0xFFFFFF, 0x2244AA);
    gfx_fill_rect(TB_SHELL_X, y + TB_BTN_Y, TB_SHELL_W, TB_BTN_H, 0x3050C8);
    gfx_text(TB_SHELL_X + 6, y + TB_BTN_Y + 2, "Shell", 0xFFFFFF, 0x3050C8);
    gfx_fill_rect(TB_NOTE_X, y + TB_BTN_Y, TB_NOTE_W, TB_BTN_H, 0x3050C8);
    gfx_text(TB_NOTE_X + 6, y + TB_BTN_Y + 2, "Notepad", 0xFFFFFF, 0x3050C8);
    gfx_fill_rect(TB_FILES_X, y + TB_BTN_Y, TB_FILES_W, TB_BTN_H, 0x3050C8);
    gfx_text(TB_FILES_X + 6, y + TB_BTN_Y + 2, "Files", 0xFFFFFF, 0x3050C8);

    uint64_t s = timer_uptime_ms() / 1000;
    char clock[16];
    int n = 0;
    uint64_t hh = s / 3600, mm = (s / 60) % 60, ss = s % 60;
    if (hh >= 10) clock[n++] = (char)('0' + hh / 10);
    clock[n++] = (char)('0' + hh % 10);
    clock[n++] = ':';
    clock[n++] = (char)('0' + mm / 10);
    clock[n++] = (char)('0' + mm % 10);
    clock[n++] = ':';
    clock[n++] = (char)('0' + ss / 10);
    clock[n++] = (char)('0' + ss % 10);
    clock[n] = 0;
    gfx_text(w - 8 * FONT_W - 14, y + TB_BTN_Y + 2, clock, 0xFFFFFF, 0x181818);
}

// full repaint: wallpaper, windows on top, taskbar above them
void desktop_repaint(void)
{
    if (!gfx_available())
        return;
    wallpaper();
    wm_paint_all();
    taskbar();
}

static void cursor_hide(void)
{
    if (!cursor_shown)
        return;
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            gfx_write_pixel((uint32_t)(last_mx + c), (uint32_t)(last_my + r),
                            cursor_saved[r * CURSOR_W + c]);
    cursor_shown = 0;
}

static void cursor_show(int32_t x, int32_t y)
{
    if (x + CURSOR_W >= (int32_t)gfx_width())
        x = (int32_t)gfx_width() - CURSOR_W;
    if (y + CURSOR_H >= (int32_t)gfx_height())
        y = (int32_t)gfx_height() - CURSOR_H;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            cursor_saved[r * CURSOR_W + c] =
                gfx_read_pixel((uint32_t)(x + c), (uint32_t)(y + r));
    last_mx = x;
    last_my = y;
    cursor_shown = 1;
    for (int r = 0; r < CURSOR_H; r++)
        for (int c = 0; c < CURSOR_W; c++)
            if (arrow[r] & (0x80 >> c))
                gfx_pixel((uint32_t)(x + c), (uint32_t)(y + r), 0xFFFFFF);
}

static void taskbar_click(int mx, int my)
{
    uint32_t y = gfx_height() - TASKBAR_H + TB_BTN_Y;
    static uint32_t cascade;
    uint32_t wx = 60 + cascade * CASCADE_STEP;
    uint32_t wy = 40 + cascade * CASCADE_STEP;
    cascade = (cascade + 1) % CASCADE_MAX;
    if (mx >= (int32_t)TB_SHELL_X && mx < (int32_t)(TB_SHELL_X + TB_SHELL_W) &&
        my >= (int32_t)y && my < (int32_t)(y + TB_BTN_H))
        wm_open(APP_SHELL, wx, wy, SHELL_WIN_W, SHELL_WIN_H);
    else if (mx >= (int32_t)TB_NOTE_X && mx < (int32_t)(TB_NOTE_X + TB_NOTE_W) &&
             my >= (int32_t)y && my < (int32_t)(y + TB_BTN_H))
        wm_open(APP_NOTEPAD, wx, wy, NOTE_WIN_W, NOTE_WIN_H);
    else if (mx >= (int32_t)TB_FILES_X && mx < (int32_t)(TB_FILES_X + TB_FILES_W) &&
             my >= (int32_t)y && my < (int32_t)(y + TB_BTN_H))
        wm_open(APP_FILES, wx, wy, NOTE_WIN_W, NOTE_WIN_H);
}

void desktop_run(void)
{
    if (!gfx_available()) {
        shell_run();                  // no framebuffer: plain shell
        return;
    }

    wm_init(desktop_repaint);
    desktop_repaint();
    cursor_show((int32_t)gfx_width() / 2, (int32_t)gfx_height() / 2);
    wm_open(APP_SHELL, 60, 40, SHELL_WIN_W, SHELL_WIN_H);

    uint8_t prev_btn = 0;
    uint64_t last_sec = (uint64_t)-1;
    for (;;) {
        int32_t mx, my;
        uint8_t btn;
        mouse_get(&mx, &my, &btn);

        if (mx != last_mx || my != last_my || !cursor_shown) {
            cursor_hide();
            cursor_show(mx, my);
        }

        if (btn != prev_btn) {        // fresh press or release
            if ((btn & 1) && my >= (int32_t)(gfx_height() - TASKBAR_H)) {
                taskbar_click(mx, my);
            } else {
                cursor_hide();
                wm_mouse(mx, my, btn);
                cursor_show(mx, my);
            }
        } else if (btn & 1) {         // button held (dragging)
            cursor_hide();
            wm_mouse(mx, my, btn);
            cursor_show(mx, my);
        }
        prev_btn = btn;

        uint64_t sec = timer_uptime_ms() / 1000;
        if (sec != last_sec) {
            last_sec = sec;
            cursor_hide();
            taskbar();
            cursor_show(last_mx, last_my);
        }

        while (kb_haschar()) {
            char c = kb_getchar();
            cursor_hide();
            wm_key(c);
            cursor_show(last_mx, last_my);
        }

        __asm__ volatile ("hlt");
    }
}
