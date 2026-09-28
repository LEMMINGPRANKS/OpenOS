#include "menu.h"
#include "gfx.h"
#include "font.h"
#include "files.h"

// The app menu overlay. Pages of 3x3 white rounded tiles on a translucent
// white wash, each tile carrying its app's accent colour. Click or Enter
// launches; ESC closes. Page 0 = built-in apps, page 1 = downloaded
// software from /apps/*.app manifests (name / target / subtitle lines),
// with empty slots for what's coming next. Click or keyboard-arrow past
// the edge flips pages.

#define MENU_COLS    3
#define MENU_ROWS    3
#define SLOTS        (MENU_ROWS * MENU_COLS)
#define TILE_W       320
#define TILE_H       170
#define TILE_GAP     44
#define MENU_PAD_TOP 90

#define COL_WASH     235
#define COL_TILE     0xFFFFFF
#define COL_TILE_OFF 0xF4F4F7
#define COL_TEXT     0x3A3A42
#define COL_SUB      0x9A9AA4
#define COL_DASH     0xDEDEE4
#define COL_DOWN_ACC 0x0F766E        // the downloaded-software accent

static const enum app_id tiles[MENU_ROWS][MENU_COLS] = {
    { APP_SHELL,    APP_FILES,     APP_INTERNET },
    { APP_NOTEPAD,  APP_PAINT,     APP_MUSIC    },
    { APP_DOWNLOAD, APP_UPDATE,    APP_SETTINGS },
};

static const char *const subs[MENU_ROWS][MENU_COLS] = {
    { "command line", "browse the disk", "browse the web" },
    { "write a note", "draw pictures",   "pc speaker"    },
    { "get packages", "new kernels",     "system prefs"  },
};

struct mslot {
    char name[16];
    char target[48];
    char sub[28];
};

static struct mslot slots[SLOTS];
static int nslots;
static int up;
static int page;
static int sel;
static char cur_target[48];
static void (*repaint)(void);

void menu_init(void (*repaint_cb)(void))
{
    repaint = repaint_cb;
    sel = 0;
    page = 0;
}

// read one line (trim \r\n) from a buffer; advances *pp past it
static int next_line(const char **pp, const char *end, char *out, int max)
{
    const char *s = *pp;
    if (s >= end)
        return 0;
    int n = 0;
    while (s < end && s[0] != '\n' && n < max - 1)
        out[n++] = *s++;
    while (n > 0 && (out[n - 1] == '\r' || out[n - 1] == ' '))
        n--;
    out[n] = 0;
    if (s < end && s[0] == '\n')
        s++;
    *pp = s;
    return 1;
}

static void scan_downloads(void)
{
    nslots = 0;
    struct fileinfo fi[16];
    int n = files_list_dir("/apps", fi, 16);
    for (int i = 0; i < n && nslots < SLOTS; i++) {
        if (fi[i].is_dir)
            continue;
        int len = 0;
        while (fi[i].name[len])
            len++;
        if (len < 5 || fi[i].name[len - 4] != '.' || fi[i].name[len - 3] != 'a' ||
            fi[i].name[len - 2] != 'p' || fi[i].name[len - 1] != 'p')
            continue;                      // only *.app manifests
        char path[80] = "/apps/";
        for (int j = 0; j < len && j < 70; j++)
            path[6 + j] = fi[i].name[j];
        path[6 + (len < 70 ? len : 70)] = 0;
        uint32_t sz = 0;
        const char *data = files_read(path, &sz);
        if (!data)
            continue;
        const char *p = data, *end = data + sz;
        struct mslot *s = &slots[nslots];
        if (!next_line(&p, end, s->name, sizeof s->name) ||
            !next_line(&p, end, s->target, sizeof s->target)) {
            s->name[0] = 0;
            continue;
        }
        if (!next_line(&p, end, s->sub, sizeof s->sub))
            s->sub[0] = 0;
        if (s->name[0] && s->target[0])
            nslots++;
    }
}

void menu_show(void)
{
    up = 1;
    scan_downloads();
    if (repaint)
        repaint();               // desktop_repaint paints the menu last
}

void menu_hide(void)
{
    up = 0;
    if (repaint)
        repaint();
}

int menu_open(void)
{
    return up;
}

const char *menu_target(void)
{
    return cur_target;
}

static void tile_xy(int row, int col, uint32_t *x, uint32_t *y)
{
    uint32_t gw = MENU_COLS * TILE_W + (MENU_COLS - 1) * TILE_GAP;
    uint32_t gh = MENU_ROWS * TILE_H + (MENU_ROWS - 1) * TILE_GAP;
    *x = (gfx_width() - gw) / 2 + (uint32_t)col * (TILE_W + TILE_GAP);
    *y = MENU_PAD_TOP + (gfx_height() - MENU_PAD_TOP - gh) / 3 +
         (uint32_t)row * (TILE_H + TILE_GAP);
}

// clickable page arrows at the screen edges (chevrons drawn in paint)
static int arrow_hit(int mx, int my, int want_page)
{
    if (want_page < 0 || want_page > 1)
        return 0;
    uint32_t h = gfx_height();
    int ax = want_page == 0 ? 16 : (int)gfx_width() - 76;
    return mx >= ax && mx < ax + 60 && my > (int)(h / 2 - 60) &&
           my < (int)(h / 2 + 60);
}

static void flip(int to)
{
    if (to == page || to < 0 || to > 1)
        return;
    page = to;
    sel = 0;
    menu_paint();
}

static void draw_arrow(int x, int dir)
{
    uint32_t cy = gfx_height() / 2;
    for (int i = 0; i < 7; i++) {
        int hh = 8 + i * 4;              // grows away from the point
        int xx = dir < 0 ? x + i * 6 : x + (6 - i) * 6;
        gfx_fill_rect((uint32_t)xx, cy - (uint32_t)hh / 2, 6,
                      (uint32_t)hh, COL_DASH);
    }
}

static void dash_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    for (uint32_t i = 0; i < w; i += 16) {
        uint32_t len = w - i < 10 ? w - i : 10;
        gfx_fill_rect(x + i, y, len, 2, COL_DASH);
        gfx_fill_rect(x + i, y + h - 2, len, 2, COL_DASH);
    }
    for (uint32_t i = 0; i < h; i += 16) {
        uint32_t len = h - i < 10 ? h - i : 10;
        gfx_fill_rect(x, y + i, 2, len, COL_DASH);
        gfx_fill_rect(x + w - 2, y + i, 2, len, COL_DASH);
    }
}

void menu_paint(void)
{
    if (!up)
        return;
    gfx_fill_rect_blend(0, 0, gfx_width(), gfx_height(), 0xFFFFFF, COL_WASH);

    const char *title = page == 0 ? "Apps" : "Downloads";
    int tw = 0;
    while (title[tw]) tw++;
    gfx_text_fg((gfx_width() - tw * FONT_W) / 2, 40, title, COL_SUB);
    // page dots under the title
    uint32_t dx = gfx_width() / 2 - 12;
    for (int i = 0; i < 2; i++)
        gfx_fill_rect(dx + (uint32_t)i * 16, 66, 8, 8,
                      i == page ? COL_TEXT : COL_DASH);
    if (page == 1)
        draw_arrow(16, -1);
    if (page == 0)
        draw_arrow((int)gfx_width() - 58, 1);

    for (int r = 0; r < MENU_ROWS; r++)
        for (int c = 0; c < MENU_COLS; c++) {
            uint32_t x, y;
            tile_xy(r, c, &x, &y);
            int i = r * MENU_COLS + c;
            int on = i == sel;

            if (page == 1 && i >= nslots) {       // empty slot
                gfx_fill_rect(x, y, TILE_W, TILE_H, 0xFAFAFC);
                if (on)
                    dash_rect(x - 3, y - 3, TILE_W + 6, TILE_H + 6);
                else
                    dash_rect(x, y, TILE_W, TILE_H);
                gfx_text_fg(x + 24, y + TILE_H - 56, "empty", COL_DASH);
                gfx_text_fg(x + 24, y + TILE_H - 36,
                            "install from the Download app", COL_DASH);
                continue;
            }

            if (on)
                gfx_shadow_r(x - 3, y - 5, TILE_W + 6, TILE_H + 10, 16);
            gfx_fill_rect_r(x, y, TILE_W, TILE_H, 16,
                            on ? COL_TILE : COL_TILE_OFF);
            uint32_t acc = page == 0 ? app_accent(tiles[r][c]) : COL_DOWN_ACC;
            gfx_rect_r(x, y, TILE_W, TILE_H, 16,
                       on ? acc : 0xDEDEE4);
            // accent icon block + app name + subtitle
            gfx_fill_rect_r(x + 24, y + 24, 44, 44, 10, acc);
            const char *nm = page == 0 ? app_name(tiles[r][c]) : slots[i].name;
            const char *sub = page == 0 ? subs[r][c] : slots[i].sub;
            gfx_text_fg(x + 24, y + TILE_H - 56, nm, COL_TEXT);
            gfx_text_fg(x + 24, y + TILE_H - 36, sub, COL_SUB);
        }
}

static int tile_at(int mx, int my, int *row, int *col)
{
    for (int r = 0; r < MENU_ROWS; r++)
        for (int c = 0; c < MENU_COLS; c++) {
            uint32_t x, y;
            tile_xy(r, c, &x, &y);
            if (mx >= (int)x && mx < (int)(x + TILE_W) &&
                my >= (int)y && my < (int)(y + TILE_H)) {
                *row = r;
                *col = c;
                return 1;
            }
        }
    return 0;
}

void menu_hover(int mx, int my)
{
    if (!up)
        return;
    int r, c;
    if (tile_at(mx, my, &r, &c)) {
        int i = r * MENU_COLS + c;
        if (i != sel) {
            sel = i;
            menu_paint();
        }
    }
}

enum app_id menu_pick(int mx, int my)
{
    if (arrow_hit(mx, my, 0))
        flip(0);
    else if (arrow_hit(mx, my, 1))
        flip(1);
    int r, c;
    if (!tile_at(mx, my, &r, &c))
        return APP_COUNT;
    sel = r * MENU_COLS + c;
    cur_target[0] = 0;
    if (page == 1) {
        if (sel >= nslots)
            return APP_COUNT;              // empty slot: nothing yet
        for (int i = 0; cur_target[i] = slots[sel].target[i]; i++)
            ;
        return APP_COUNT;                  // desktop reads menu_target()
    }
    return tiles[r][c];
}

void menu_move(int delta)
{
    int r = sel / MENU_COLS, c = sel % MENU_COLS;
    if (delta < 0) {
        if (c > 0) c--;
        else if (r > 0) { r--; c = MENU_COLS - 1; }
        else if (page == 1) {
            flip(0);                       // off the left edge: back a page
            return;                        // flip() puts sel on slot 0
        } else
            return;
    } else {
        if (c < MENU_COLS - 1) c++;
        else if (r < MENU_ROWS - 1) { r++; c = 0; }
        else if (page == 0) {
            flip(1);                       // off the right edge: next page
            return;                        // flip() puts sel on slot 0
        } else
            return;
    }
    sel = r * MENU_COLS + c;
    menu_paint();
}

enum app_id menu_selected(void)
{
    cur_target[0] = 0;
    if (page == 1) {
        if (sel >= nslots)
            return APP_COUNT;              // empty slot: nothing yet
        for (int i = 0; cur_target[i] = slots[sel].target[i]; i++)
            ;
        return APP_COUNT;
    }
    int r = sel / MENU_COLS, c = sel % MENU_COLS;
    return tiles[r][c];
}
