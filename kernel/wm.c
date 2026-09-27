#include "wm.h"
#include "term.h"
#include "gfx.h"
#include "timer.h"
#include "banner.h"

// The window manager. Owns the window table, draws chrome (2015-OS style:
// white rounded plate, soft shadow, light title bar with the app's accent
// colour), handles focus, drag, and routes keyboard to the focused app.
// Repainting the background is desktop.c's job, so wm calls back into it.

#define WM_TITLE_H     28
#define WM_RAD         10
#define WM_CLOSE_R     8              // close circle radius
#define WM_CLOSE_GAP   18
#define WM_PLATE       0xFFFFFF
#define WM_TITLE_FOCUS 0xF3F3F6
#define WM_TITLE_BLUR  0xEBEBEF
#define WM_TEXT_FOCUS  0x28282E
#define WM_TEXT_BLUR   0x8A8A92
#define WM_BORDER      0xD5D5DB
#define WM_INSET       2              // body inset so rounding shows
#define WM_MIN_VISIBLE 60

static struct window wins[WM_MAX_WINDOWS];
static int focus = -1;
static uint8_t prev_left;
static int drag_win = -1;
static int ptr_win = -1;                   // pixel app owning the pointer
static int drag_dx, drag_dy;
static void (*repaint_all)(void);

void wm_init(void (*repaint_cb)(void))
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        wins[i].used = 0;
    focus = -1;
    drag_win = -1;
    prev_left = 0;
    repaint_all = repaint_cb;
}

static void paint_window(struct window *w, int focused)
{
    gfx_shadow_r(w->x, w->y, w->w, w->h, WM_RAD);
    gfx_fill_rect_r(w->x, w->y, w->w, w->h, WM_RAD, WM_PLATE);
    gfx_fill_rect_top_r(w->x, w->y, w->w, WM_TITLE_H, WM_RAD,
                        focused ? WM_TITLE_FOCUS : WM_TITLE_BLUR);
    if (focused)                          // accent strip along the title top
        gfx_fill_rect_top_r(w->x, w->y, w->w, 4, WM_RAD, app_accent(w->app));
    gfx_text(w->x + 12, w->y + 7, app_name(w->app),
             focused ? WM_TEXT_FOCUS : WM_TEXT_BLUR,
             focused ? WM_TITLE_FOCUS : WM_TITLE_BLUR);
    // close: a circle with an x, bottom-right of the title bar
    uint32_t cx = w->x + w->w - WM_CLOSE_GAP - WM_CLOSE_R;
    uint32_t cy = w->y + WM_TITLE_H / 2;
    gfx_fill_circle(cx, cy, WM_CLOSE_R,
                    focused ? 0xC4C4CC : 0xB8B8C0);
    for (int d = -3; d <= 3; d++) {
        gfx_pixel(cx + (uint32_t)d, cy + (uint32_t)d, 0x55555E);
        gfx_pixel(cx + (uint32_t)d, cy - (uint32_t)d, 0x55555E);
    }
    if (app_wants_pixels(w->app))
        app_repaint_pixels(w->app, w->con, w->x + WM_INSET,
                           w->y + WM_TITLE_H, w->w - 2 * WM_INSET,
                           w->h - WM_TITLE_H - WM_INSET);
    else {
        term_render(w->con);
        app_after_paint(w->app, w->con);   // pixel overlays get put back
    }
    gfx_rect_r(w->x, w->y, w->w, w->h, WM_RAD, WM_BORDER);
}

void wm_paint_all(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (wins[i].used && i != focus)
            paint_window(&wins[i], 0);
    if (focus >= 0 && wins[focus].used)
        paint_window(&wins[focus], 1);
}

int wm_open(enum app_id app, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (!wins[i].used) { slot = i; break; }
    if (slot < 0)
        return -1;
    struct console *con = term_open(x + WM_INSET, y + WM_TITLE_H,
                                    w - 2 * WM_INSET,
                                    h - WM_TITLE_H - WM_INSET);
    if (!con)
        return -1;
    banner_play(app);                // Wii-style start splash
    wins[slot].used = 1;
    wins[slot].app = app;
    wins[slot].con = con;
    wins[slot].x = x;
    wins[slot].y = y;
    wins[slot].w = w;
    wins[slot].h = h;
    focus = slot;
    if (repaint_all)
        repaint_all();               // chrome first, then app output on top
    app_open(app, con);
    return 0;
}

int wm_enum(int *slots, enum app_id *apps, int *focused, int max)
{
    int n = 0;
    for (int i = 0; i < WM_MAX_WINDOWS && n < max; i++)
        if (wins[i].used) {
            slots[n] = i;
            apps[n] = wins[i].app;
            focused[n] = (i == focus);
            n++;
        }
    return n;
}

void wm_focus_slot(int slot)
{
    if (slot < 0 || slot >= WM_MAX_WINDOWS || !wins[slot].used)
        return;
    if (focus == slot)
        return;
    focus = slot;
    if (repaint_all)
        repaint_all();
}

static int top_any(void)
{
    for (int i = WM_MAX_WINDOWS - 1; i >= 0; i--)
        if (wins[i].used)
            return i;
    return -1;
}

static int window_at(int mx, int my)
{
    if (focus >= 0 && wins[focus].used) {
        struct window *w = &wins[focus];
        if (mx >= (int)w->x && mx < (int)(w->x + w->w) &&
            my >= (int)w->y && my < (int)(w->y + w->h))
            return focus;
    }
    for (int i = WM_MAX_WINDOWS - 1; i >= 0; i--) {
        if (!wins[i].used || i == focus)
            continue;
        struct window *w = &wins[i];
        if (mx >= (int)w->x && mx < (int)(w->x + w->w) &&
            my >= (int)w->y && my < (int)(w->y + w->h))
            return i;
    }
    return -1;
}

static int in_close(struct window *w, int mx, int my)
{
    int32_t cx = (int32_t)(w->x + w->w - WM_CLOSE_GAP - WM_CLOSE_R);
    int32_t cy = (int32_t)(w->y + WM_TITLE_H / 2);
    int32_t dx = mx - cx, dy = my - cy;
    return dx * dx + dy * dy <= (WM_CLOSE_R + 3) * (WM_CLOSE_R + 3);
}

void wm_close_focused(void)
{
    if (focus < 0 || !wins[focus].used)
        return;
    term_close(wins[focus].con);
    wins[focus].used = 0;
    focus = top_any();
    if (repaint_all)
        repaint_all();
}

int wm_any_open(void)
{
    return top_any() >= 0;
}

int wm_focused_app(void)
{
    if (focus < 0 || !wins[focus].used)
        return -1;
    return wins[focus].app;
}

void wm_key(char c)
{
    if (focus < 0 || !wins[focus].used)
        return;
    if (c == 27) {                    // ESC closes the focused window
        wm_close_focused();
        return;
    }
    app_input(wins[focus].app, wins[focus].con, c);
}

// returns 1 if the click landed on a window (so callers can fall back to
// their own hit-testing, e.g. desktop icons)
int wm_mouse(int mx, int my, uint8_t buttons)
{
    int left = buttons & 1;
    int32_t sw = (int32_t)gfx_width();
    int32_t sh = (int32_t)gfx_height();

    if (drag_win >= 0) {              // mid-drag
        if (!left) {
            drag_win = -1;
        } else {
            struct window *w = &wins[drag_win];
            int nx = mx - drag_dx;
            int ny = my - drag_dy;
            if (nx < 0) nx = 0;
            if (nx > sw - WM_MIN_VISIBLE) nx = sw - WM_MIN_VISIBLE;
            if (ny < 0) ny = 0;
            if (ny > sh - WM_TITLE_H - 1) ny = sh - WM_TITLE_H - 1;
            w->x = (uint32_t)nx;
            w->y = (uint32_t)ny;
            term_move(w->con, w->x, w->y + WM_TITLE_H);
            if (repaint_all)
                repaint_all();
        }
        prev_left = (uint8_t)left;
        return 1;                     // a window owns the pointer mid-drag
    }

    if (left && !prev_left) {         // fresh press
        int hit = window_at(mx, my);
        if (hit >= 0) {
            struct window *w = &wins[hit];
            if (in_close(w, mx, my)) {
                term_close(w->con);
                w->used = 0;
                if (focus == hit)
                    focus = top_any();
                if (repaint_all)
                    repaint_all();
            } else {
                if (focus != hit) {
                    focus = hit;
                    if (repaint_all)
                        repaint_all();
                }
                if (my < (int)(w->y + WM_TITLE_H)) {  // grab the title bar
                    drag_win = hit;
                    drag_dx = mx - (int)w->x;
                    drag_dy = my - (int)w->y;
                } else if (app_wants_pixels(w->app)) {
                    ptr_win = hit;     // pixel app: press/drag/release
                    app_pointer(w->app, w->con, mx, my, 1);
                } else {              // click inside the app body
                    static uint64_t last_ms;
                    static int last_win = -1;
                    uint64_t now = timer_uptime_ms();
                    int dbl = hit == last_win && now - last_ms < 400;
                    last_ms = now;
                    last_win = hit;
                    app_click(w->app, w->con, mx, my, dbl);
                }
            }
            prev_left = (uint8_t)left;
            return 1;
        }
    }
    if (ptr_win >= 0) {                // pixel app drag / release
        if (!wins[ptr_win].used)
            ptr_win = -1;
        else {
            if (left)
                app_pointer(wins[ptr_win].app, wins[ptr_win].con, mx, my, 1);
            else {
                app_pointer(wins[ptr_win].app, wins[ptr_win].con, mx, my, 0);
                ptr_win = -1;
            }
        }
        prev_left = (uint8_t)left;
        return 1;
    }
    prev_left = (uint8_t)left;
    return 0;
}
