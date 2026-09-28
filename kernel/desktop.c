#include "desktop.h"
#include "gfx.h"
#include "font.h"
#include "mouse.h"
#include "kb.h"
#include "wm.h"
#include "apps.h"
#include "menu.h"
#include "shell.h"
#include "gpu.h"
#include "version.h"

// The desktop OpenOS boots into: light grey-on-white wallpaper and the
// app menu (menu.c). Windows are always FULL SCREEN with an exit button
// in the title bar -- there is no taskbar (BDFL decree, 1.6 part 2).

#define CURSOR_W     8
#define CURSOR_H     8

#define COL_WORDMARK    0xADADBA

static const unsigned char arrow[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
};

static uint32_t cursor_saved[CURSOR_W * CURSOR_H];
static int cursor_shown;
static int32_t last_mx, last_my;

static int name_eq(const char *a, const char *b)
{
    for (int i = 0; ; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
}

static void launch(enum app_id app)
{
    if (app == APP_INTERNET)
        app_set_arg("");
    wm_open(app, 0, 0, gfx_width(), gfx_height());
}

// launch a downloaded-app target: "app:<Name>" opens that built-in app,
// "file:/path" opens the file in whatever app fits it
static int launch_target(const char *t)
{
    if (t[0] == 'a' && t[1] == 'p' && t[2] == 'p' && t[3] == ':') {
        for (int id = 0; id < APP_COUNT; id++)
            if (name_eq(app_name((enum app_id)id), t + 4)) {
                launch((enum app_id)id);
                return 1;
            }
        return 0;
    }
    if (t[0] == 'f' && t[1] == 'i' && t[2] == 'l' && t[3] == 'e' &&
        t[4] == ':')
        return open_file_window(t + 5) == 0;
    return 0;
}

static void wallpaper(void)
{
    uint32_t w = gfx_width(), h = gfx_height();
    for (uint32_t y = 0; y < h; y++) {
        uint32_t t = y * 256 / h;                 // 0 top, 255 bottom
        uint32_t r = 253 - (253 - 237) * t / 255;
        uint32_t g = 253 - (253 - 237) * t / 255;
        uint32_t b = 255 - (255 - 241) * t / 255;
        gfx_fill_rect(0, y, w, 1, (r << 16) | (g << 8) | b);
    }
    gfx_text_fg(28, 26, "OpenOS", COL_WORDMARK);
    gfx_text_fg(28, 26 + FONT_H + 4, OS_VERSION, COL_WORDMARK);
    gfx_text_fg(28, 26 + 2 * FONT_H + 8, gpu_ident(), COL_WORDMARK);
}

void desktop_repaint(void)
{
    if (!gfx_available())
        return;
    wallpaper();
    wm_paint_all();
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
            if ((btn & 1) && menu_open()) {
                enum app_id picked = menu_pick(mx, my);
                const char *target = menu_target();
                if (picked != APP_COUNT) {
                    menu_hide();
                    launch(picked);
                } else if (target[0]) {
                    menu_hide();
                    launch_target(target);
                }
            } else if (btn & 1) {     // fresh press: windows first
                cursor_hide();
                wm_mouse(mx, my, btn);
                cursor_show(mx, my);
            } else {                  // release: let apps finish clicks
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
                    const char *target = menu_target();
                    menu_hide();
                    if (picked != APP_COUNT)
                        launch(picked);
                    else if (target[0])
                        launch_target(target);
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
