#include "wm.h"
#include "term.h"
#include "gfx.h"
#include "timer.h"

// The window manager. Owns the window table, draws chrome (title bar +
// close box), handles focus, drag, and routes keyboard to the focused app.
// Repainting the background is desktop.c's job, so wm calls back into it.

#define WM_TITLE_H     20
#define WM_CLOSE_BOX   12
#define WM_CLOSE_GAP   4
#define WM_TITLE_FOCUS 0x0000AA
#define WM_TITLE_BLUR  0x555555
#define WM_TITLE_TEXT  0xFFFFFF
#define WM_CLOSE_COL   0xAA0000
#define WM_BODY_BG     0x000000
#define WM_BORDER      0xAAAAAA
#define WM_MIN_VISIBLE 60

static struct window wins[WM_MAX_WINDOWS];
static int focus = -1;
static uint8_t prev_left;
static int drag_win = -1;
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
    gfx_fill_rect(w->x, w->y, w->w, WM_TITLE_H,
                  focused ? WM_TITLE_FOCUS : WM_TITLE_BLUR);
    gfx_text(w->x + 4, w->y + 2, app_name(w->app),
             WM_TITLE_TEXT, focused ? WM_TITLE_FOCUS : WM_TITLE_BLUR);
    gfx_fill_rect(w->x + w->w - WM_CLOSE_BOX - WM_CLOSE_GAP,
                  w->y + (WM_TITLE_H - WM_CLOSE_BOX) / 2,
                  WM_CLOSE_BOX, WM_CLOSE_BOX, WM_CLOSE_COL);
    gfx_fill_rect(w->x, w->y + WM_TITLE_H, w->w, w->h - WM_TITLE_H, WM_BODY_BG);
    term_render(w->con);
    gfx_rect(w->x, w->y, w->w, w->h, WM_BORDER);
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
    struct console *con = term_open(x, y + WM_TITLE_H, w, h - WM_TITLE_H);
    if (!con)
        return -1;
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
    uint32_t bx = w->x + w->w - WM_CLOSE_BOX - WM_CLOSE_GAP;
    uint32_t by = w->y + (WM_TITLE_H - WM_CLOSE_BOX) / 2;
    return mx >= (int)bx && mx < (int)(bx + WM_CLOSE_BOX) &&
           my >= (int)by && my < (int)(by + WM_CLOSE_BOX);
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
    prev_left = (uint8_t)left;
    return 0;
}
