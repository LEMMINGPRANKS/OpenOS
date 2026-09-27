#include "menu.h"
#include "gfx.h"
#include "font.h"

// The app menu overlay. 3x3 grid of white rounded tiles on a translucent
// white wash, each tile carrying its app's accent colour. Click or Enter
// launches; ESC closes.

#define MENU_COLS    3
#define MENU_ROWS    3
#define TILE_W       320
#define TILE_H       170
#define TILE_GAP     44
#define TITLE_H      24
#define MENU_PAD_TOP 90

#define COL_WASH     235
#define COL_TILE     0xFFFFFF
#define COL_TILE_OFF 0xF4F4F7
#define COL_TEXT     0x3A3A42
#define COL_SUB      0x9A9AA4

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

static int up;
static int sel;
static void (*repaint)(void);

void menu_init(void (*repaint_cb)(void))
{
    repaint = repaint_cb;
    sel = 0;
}

void menu_show(void)
{
    up = 1;
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

static void tile_xy(int row, int col, uint32_t *x, uint32_t *y)
{
    uint32_t gw = MENU_COLS * TILE_W + (MENU_COLS - 1) * TILE_GAP;
    uint32_t gh = MENU_ROWS * TILE_H + (MENU_ROWS - 1) * TILE_GAP;
    *x = (gfx_width() - gw) / 2 + (uint32_t)col * (TILE_W + TILE_GAP);
    *y = MENU_PAD_TOP + (gfx_height() - MENU_PAD_TOP - gh) / 3 +
         (uint32_t)row * (TILE_H + TILE_GAP);
}

void menu_paint(void)
{
    if (!up)
        return;
    gfx_fill_rect_blend(0, 0, gfx_width(), gfx_height(), 0xFFFFFF, COL_WASH);

    const char *title = "Apps";
    int tw = 0;
    while (title[tw]) tw++;
    gfx_text_fg((gfx_width() - tw * FONT_W) / 2, 40, title, COL_SUB);

    for (int r = 0; r < MENU_ROWS; r++)
        for (int c = 0; c < MENU_COLS; c++) {
            uint32_t x, y;
            tile_xy(r, c, &x, &y);
            int i = r * MENU_COLS + c;
            int on = i == sel;
            if (on)
                gfx_shadow_r(x - 3, y - 5, TILE_W + 6, TILE_H + 10, 16);
            gfx_fill_rect_r(x, y, TILE_W, TILE_H, 16,
                            on ? COL_TILE : COL_TILE_OFF);
            gfx_rect_r(x, y, TILE_W, TILE_H, 16,
                       on ? app_accent(tiles[r][c]) : 0xDEDEE4);
            // accent icon block + app name + subtitle
            gfx_fill_rect_r(x + 24, y + 24, 44, 44, 10,
                            app_accent(tiles[r][c]));
            gfx_text_fg(x + 24, y + TILE_H - 56, app_name(tiles[r][c]),
                        COL_TEXT);
            gfx_text_fg(x + 24, y + TILE_H - 36, subs[r][c], COL_SUB);
        }
}

void menu_hover(int mx, int my)
{
    if (!up)
        return;
    for (int r = 0; r < MENU_ROWS; r++)
        for (int c = 0; c < MENU_COLS; c++) {
            uint32_t x, y;
            tile_xy(r, c, &x, &y);
            if (mx >= (int)x && mx < (int)(x + TILE_W) &&
                my >= (int)y && my < (int)(y + TILE_H)) {
                int i = r * MENU_COLS + c;
                if (i != sel) {
                    sel = i;
                    menu_paint();
                }
                return;
            }
        }
}

enum app_id menu_pick(int mx, int my)
{
    for (int r = 0; r < MENU_ROWS; r++)
        for (int c = 0; c < MENU_COLS; c++) {
            uint32_t x, y;
            tile_xy(r, c, &x, &y);
            if (mx >= (int)x && mx < (int)(x + TILE_W) &&
                my >= (int)y && my < (int)(y + TILE_H)) {
                sel = r * MENU_COLS + c;
                return tiles[r][c];
            }
        }
    return APP_COUNT;
}

void menu_move(int delta)
{
    int r = sel / MENU_COLS, c = sel % MENU_COLS;
    if (delta < 0) {
        if (c > 0) c--;
        else if (r > 0) { r--; c = MENU_COLS - 1; }
    } else {
        if (c < MENU_COLS - 1) c++;
        else if (r < MENU_ROWS - 1) { r++; c = 0; }
    }
    sel = r * MENU_COLS + c;
    menu_paint();
}

enum app_id menu_selected(void)
{
    int r = sel / MENU_COLS, c = sel % MENU_COLS;
    return tiles[r][c];
}
