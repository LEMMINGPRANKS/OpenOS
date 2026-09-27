#include "desktop.h"
#include "gfx.h"
#include "font.h"
#include "mouse.h"
#include "kb.h"
#include "timer.h"
#include "term.h"
#include "wm.h"
#include "apps.h"
#include "menu.h"
#include "paint.h"
#include "shell.h"
#include "version.h"

// The desktop OpenOS boots into: light grey-on-white wallpaper, a slim
// taskbar (Apps button, running windows, clock) and windows managed by
// wm.c. Apps live in the menu (menu.c), not on the desktop.

#define TASKBAR_H    44
#define CURSOR_W     8
#define CURSOR_H     8

#define TB_BTN_Y     8
#define TB_BTN_H     28
#define TB_MENU_X    12
#define TB_MENU_W    (4 * FONT_W + 24)
#define TB_PILL_X0   (TB_MENU_X + TB_MENU_W + 20)
#define TB_PILL_H    28
#define TB_PILL_PAD  12

#define COL_BG_TOP      0xFDFDFF
#define COL_BG_BOT      0xEDEDF1
#define COL_WORDMARK    0xADADBA
#define COL_BAR         0xF7F7FA
#define COL_BAR_LINE    0xD8D8DE
#define COL_BAR_TEXT    0x4A4A52
#define COL_PILL        0xEAEAEF
#define COL_PILL_FOCUS  0xD9D9E0
#define COL_PILL_TEXT   0x3A3A42

// default window size per app (1400x900 desktop has room)
struct app_geom {
    enum app_id app;
    uint16_t w, h;
};

static const struct app_geom app_geoms[] = {
    { APP_SHELL,    720, 480 },
    { APP_NOTEPAD,  640, 440 },
    { APP_FILES,    640, 460 },
    { APP_INTERNET, 780, 560 },
    { APP_SETTINGS, 560, 420 },
    { APP_MUSIC,    560, 430 },
    { APP_DOWNLOAD, 620, 440 },
    { APP_UPDATE,   600, 430 },
};

static const unsigned char arrow[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
};

static uint32_t cursor_saved[CURSOR_W * CURSOR_H];
static int cursor_shown;
static int32_t last_mx, last_my;

static void launch(enum app_id app)
{
    static uint32_t cascade;
    uint32_t w = 600, h = 420;
    for (uint32_t i = 0; i < sizeof app_geoms / sizeof app_geoms[0]; i++)
        if (app_geoms[i].app == app) {
            w = app_geoms[i].w;
            h = app_geoms[i].h;
        }
    if (app == APP_PAINT) {
        w = PAINT_WIN_W;
        h = PAINT_WIN_H;
    }
    uint32_t x = (gfx_width() - w) / 2 + cascade * 28 - 40;
    uint32_t y = (gfx_height() - TASKBAR_H - h) / 2 + cascade * 24 - 40;
    cascade = (cascade + 1) % 6;
    if (app == APP_INTERNET)
        app_set_arg("");
    wm_open(app, x, y, w, h);
}

static void wallpaper(void)
{
    uint32_t w = gfx_width(), h = gfx_height() - TASKBAR_H;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t t = y * 256 / h;                 // 0 top, 255 bottom
        uint32_t c = 0xF0 + ((253 - 0xF0) * (255 - t)) / 255;  // rough blend
        uint32_t r = 253 - (253 - 237) * t / 255;
        uint32_t g = 253 - (253 - 237) * t / 255;
        uint32_t b = 255 - (255 - 241) * t / 255;
        (void)c;
        gfx_fill_rect(0, y, w, 1, (r << 16) | (g << 8) | b);
    }
    gfx_text_fg(28, 26, "OpenOS", COL_WORDMARK);
    gfx_text_fg(28, 26 + FONT_H + 4, OS_VERSION, COL_WORDMARK);
}

static void draw_pill(uint32_t x, const char *label, int focus)
{
    int n = 0;
    while (label[n]) n++;
    uint32_t w = (uint32_t)n * FONT_W + 2 * TB_PILL_PAD;
    uint32_t y = gfx_height() - TASKBAR_H + TB_BTN_Y;
    gfx_fill_rect_r(x, y, w, TB_PILL_H, TB_PILL_H / 2,
                    focus ? COL_PILL_FOCUS : COL_PILL);
    gfx_text(x + TB_PILL_PAD, y + (TB_PILL_H - FONT_H) / 2, label,
             COL_PILL_TEXT, focus ? COL_PILL_FOCUS : COL_PILL);
}

static void taskbar(void)
{
    uint32_t w = gfx_width();
    uint32_t y = gfx_height() - TASKBAR_H;
    gfx_fill_rect(0, y, w, TASKBAR_H, COL_BAR);
    gfx_fill_rect(0, y, w, 1, COL_BAR_LINE);

    // Apps button (opens/closes the menu)
    draw_pill(TB_MENU_X, "Apps", menu_open());

    // running windows
    int slots[WM_MAX_WINDOWS];
    enum app_id apps[WM_MAX_WINDOWS];
    int focused[WM_MAX_WINDOWS];
    int n = wm_enum(slots, apps, focused, WM_MAX_WINDOWS);
    uint32_t px = TB_PILL_X0;
    for (int i = 0; i < n; i++) {
        const char *nm = app_name(apps[i]);
        int ln = 0;
        while (nm[ln]) ln++;
        draw_pill(px, nm, focused[i]);
        px += (uint32_t)ln * FONT_W + 2 * TB_PILL_PAD + 10;
    }

    // clock, right-aligned
    uint64_t s = timer_uptime_ms() / 1000;
    char clock[16];
    int n2 = 0;
    uint64_t hh = s / 3600, mm = (s / 60) % 60;
    clock[n2++] = (char)('0' + hh / 10);
    clock[n2++] = (char)('0' + hh % 10);
    clock[n2++] = ':';
    clock[n2++] = (char)('0' + mm / 10);
    clock[n2++] = (char)('0' + mm % 10);
    clock[n2] = 0;
    gfx_text(w - 5 * FONT_W - 14, y + (TASKBAR_H - FONT_H) / 2, clock,
             COL_BAR_TEXT, COL_BAR);
}

void desktop_repaint(void)
{
    if (!gfx_available())
        return;
    wallpaper();
    wm_paint_all();
    taskbar();
    if (menu_open())
        menu_paint();
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
                gfx_pixel((uint32_t)(x + c), (uint32_t)(y + r), 0x28282E);
}

static int in_menu_button(int mx, int my)
{
    uint32_t y = gfx_height() - TASKBAR_H + TB_BTN_Y;
    return mx >= (int32_t)TB_MENU_X &&
           mx < (int32_t)(TB_MENU_X + TB_MENU_W) &&
           my >= (int32_t)y && my < (int32_t)(y + TB_BTN_H);
}

static void taskbar_click(int mx, int my)
{
    if (in_menu_button(mx, my)) {
        if (menu_open())
            menu_hide();
        else
            menu_show();
        return;
    }
    // running-window pills
    int slots[WM_MAX_WINDOWS];
    enum app_id apps[WM_MAX_WINDOWS];
    int focused[WM_MAX_WINDOWS];
    int n = wm_enum(slots, apps, focused, WM_MAX_WINDOWS);
    uint32_t px = TB_PILL_X0;
    for (int i = 0; i < n; i++) {
        const char *nm = app_name(apps[i]);
        int ln = 0;
        while (nm[ln]) ln++;
        uint32_t w = (uint32_t)ln * FONT_W + 2 * TB_PILL_PAD;
        if (mx >= (int32_t)px && mx < (int32_t)(px + w) && !focused[i]) {
            wm_focus_slot(slots[i]);
            return;
        }
        px += w + 10;
    }
}

void desktop_run(void)
{
    if (!gfx_available()) {
        shell_run();                  // no framebuffer: plain shell
        return;
    }

    wm_init(desktop_repaint);
    menu_init(desktop_repaint);
    menu_show();                      // boot straight into the app menu
    cursor_show((int32_t)gfx_width() / 2, (int32_t)gfx_height() / 2);

    uint8_t prev_btn = 0;
    uint64_t last_sec = (uint64_t)-1;
    for (;;) {
        int32_t mx, my;
        uint8_t btn;
        mouse_get(&mx, &my, &btn);

        if (mx != last_mx || my != last_my || !cursor_shown) {
            if (menu_open()) {
                cursor_hide();
                menu_hover(mx, my);
                cursor_show(mx, my);
            } else {
                cursor_hide();
                cursor_show(mx, my);
            }
        }

        if (btn != prev_btn) {        // fresh press or release
            if ((btn & 1) && my >= (int32_t)(gfx_height() - TASKBAR_H)) {
                taskbar_click(mx, my);
            } else if ((btn & 1) && menu_open()) {
                enum app_id picked = menu_pick(mx, my);
                if (picked != APP_COUNT) {
                    menu_hide();
                    launch(picked);
                }
            } else if (btn & 1) {     // fresh press: windows first
                cursor_hide();
                wm_mouse(mx, my, btn);
                cursor_show(mx, my);
            } else {                  // release: let the wm finish drags
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

        // Wii rule: with no windows left, you're back on the app menu
        if (!wm_any_open() && !menu_open())
            menu_show();

        uint64_t sec = timer_uptime_ms() / 1000;
        if (sec != last_sec) {
            last_sec = sec;
            // the menu is static and the taskbar sits under its translucent
            // wash -- repainting it every second just restarts the slow
            // full-menu paint halfway through (permanent half-drawn menu)
            if (!menu_open()) {
                cursor_hide();
                taskbar();
                cursor_show(last_mx, last_my);
            }
        }

        while (kb_haschar()) {
            char c = kb_getchar();
            if (menu_open()) {
                if (c == 27) {
                    menu_hide();
                } else if (c == KEY_LEFT) {
                    menu_move(-1);
                } else if (c == KEY_RIGHT) {
                    menu_move(1);
                } else if (c == '\n') {
                    enum app_id picked = menu_selected();
                    menu_hide();
                    launch(picked);
                }
            } else {
                cursor_hide();
                wm_key(c);
                cursor_show(last_mx, last_my);
            }
        }

        __asm__ volatile ("hlt");
    }
}
