#include "filemgr.h"
#include "files.h"
#include "ext.h"
#include "term.h"
#include "kb.h"
#include "apps.h"
#include "path.h"
#include "appbar.h"
#include "gfx.h"
#include "font.h"

#define FM_STATES 2

#define FM_ACCENT   0x1E5AA8
#define FM_FOLDER   0xB8860B
#define FM_FILEDOT  0x78909C
#define FM_SEL_BAND 0x70              // attr: grey paper for the selected row

struct fm_state {
    struct console *con;
    uint8_t used;
    char cwd[PATH_MAX];
    int sel;
    int count;                      // entries below the ".." row (if shown)
    int has_parent;                 // 1 when ".." row is drawn
    struct fileinfo files[FILES_MAX];
};

static struct fm_state fms[FM_STATES];

static struct fm_state *fm_for(struct console *con)
{
    for (int i = 0; i < FM_STATES; i++)
        if (fms[i].used && fms[i].con == con)
            return &fms[i];
    for (int i = 0; i < FM_STATES; i++)
        if (!fms[i].used) {
            fms[i].used = 1;
            fms[i].con = con;
            fms[i].cwd[0] = '/'; fms[i].cwd[1] = 0;
            fms[i].sel = 0;
            fms[i].count = 0;
            return &fms[i];
        }
    return &fms[0];
}

static void print_u32(uint32_t v)
{
    char digits[12];
    int n = 0;
    if (!v) digits[n++] = '0';
    while (v) { digits[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) term_putc(digits[--n]);
}

static void fm_refresh(struct fm_state *st)
{
    st->count = files_list_dir(st->cwd, st->files, FILES_MAX);
    st->has_parent = !(st->cwd[0] == '/' && !st->cwd[1]);
    if (st->sel >= st->count)
        st->sel = st->count ? st->count - 1 : 0;
}

// first console row of the list (".." or entry 0)
static int list_top(void)
{
    return APPBAR_ROWS + 1;
}

static void fm_render(struct fm_state *st)
{
    struct console *con = st->con;
    term_use(con);
    term_protect(con, APPBAR_ROWS);
    term_clear();

    char status[32];
    int sn = 0;
    status[sn++] = '0' + (st->count % 100) / 10;
    status[sn++] = '0' + st->count % 10;
    const char *it = " items";
    for (int i = 0; it[i] && sn < 30; i++) status[sn++] = it[i];
    status[sn] = 0;
    appbar_paint(con, st->cwd, status, FM_ACCENT);

    term_goto(con, 0, list_top());
    if (st->has_parent) {
        term_setcolor(st->sel == -1 ? (uint8_t)(FM_SEL_BAND | 0x06)
                                    : TERM_COLOR_DIR);
        term_puts(st->sel == -1 ? "    ..  up one folder\n" : "    ..\n");
    }
    if (!st->count) {
        term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
        term_puts("    (empty -- press r to refresh)\n");
        return;
    }
    for (int i = 0; i < st->count; i++) {
        struct fileinfo *f = &st->files[i];
        uint8_t fg = f->is_dir ? 0x06 : ext_lookup(f->name)->vga_color & 0x0F;
        term_setcolor(i == st->sel ? (uint8_t)(FM_SEL_BAND | fg) : fg);
        term_puts("      ");          // room for the pixel icon
        term_puts(f->name);
        if (f->is_dir) {
            term_putc('/');
        } else {
            term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
            term_puts("  [");
            term_puts(f->source == FS_RAMFS ? "DR1" : "IR2");
            term_puts("] ");
            print_u32(f->size);
        }
        term_putc('\n');
    }

    // pixel icons after the text, one little tile per row
    uint32_t vx, vy, vw, vh;
    term_view(con, &vx, &vy, &vw, &vh);
    (void)vh;
    int row = list_top();
    if (st->has_parent) {
        gfx_fill_rect_r(vx + 12, vy + row * FONT_H + 4, 12, 9, 3, FM_FILEDOT);
        row++;
    }
    for (int i = 0; i < st->count; i++, row++) {
        uint32_t c = st->files[i].is_dir ? FM_FOLDER : FM_FILEDOT;
        gfx_fill_rect_r(vx + 12, vy + row * FONT_H + 3, 13, 11, 3, c);
    }
}

void filemgr_open(struct console *con)
{
    struct fm_state *st = fm_for(con);
    fm_refresh(st);
    fm_render(st);
}

// focus changes re-render every cell: put our icons back
void filemgr_app_repaint(struct console *con)
{
    for (int i = 0; i < FM_STATES; i++)
        if (fms[i].used && fms[i].con == con && fms[i].count | fms[i].has_parent) {
            fm_render(&fms[i]);
            return;
        }
}

// full path of the selected entry (works for ".." too)
static void selected_path(struct fm_state *st, char *out)
{
    if (st->has_parent && st->sel == -1) {
        path_parent(st->cwd, out);
        return;
    }
    path_resolve(st->cwd, st->files[st->sel].name, out);
}

static void open_selected(struct fm_state *st)
{
    if (st->has_parent && st->sel == -1) {      // ".."
        char up[PATH_MAX];
        path_parent(st->cwd, up);
        for (int i = 0; i < PATH_MAX; i++) {
            st->cwd[i] = up[i];
            if (!up[i]) break;
        }
        st->sel = 0;
        fm_refresh(st);
        fm_render(st);
        return;
    }
    if (!st->count)
        return;
    struct fileinfo *f = &st->files[st->sel];
    if (f->is_dir) {
        char full[PATH_MAX];
        path_resolve(st->cwd, f->name, full);
        for (int i = 0; i < PATH_MAX; i++) {
            st->cwd[i] = full[i];
            if (!full[i]) break;
        }
        st->sel = 0;
        fm_refresh(st);
        fm_render(st);
        return;
    }
    char full[PATH_MAX];
    selected_path(st, full);
    if (open_file_window(full) != 0) {
        fm_render(st);
        term_setcolor(0x0E);
        term_puts("\n  no viewer for this type yet (binary?)\n");
        term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    }
    // open_file_window's wm_open repaints in the right order (the new
    // window is focused = painted last), so no redraw here
}

// mouse support: click selects, double-click opens. rows: the toolbar is
// protected pixels, then ".." (if shown) and the entries.
void filemgr_click(struct console *con, int mx, int my, int dbl)
{
    struct fm_state *st = fm_for(con);
    int col, row;
    if (!term_locate(con, mx, my, &col, &row) || row < list_top())
        return;
    term_use(con);
    int idx = row - list_top();
    if (st->has_parent) {
        if (idx == 0) {
            st->sel = -1;
            if (!dbl)
                fm_render(st);
            else
                open_selected(st);
            return;
        }
        idx--;
    }
    if (idx >= st->count)
        return;
    if (st->sel != idx || !dbl) {
        st->sel = idx;
        if (!dbl)
            fm_render(st);
    }
    if (dbl)
        open_selected(st);
}

void filemgr_input(struct console *con, char c)
{
    struct fm_state *st = fm_for(con);
    int min = st->has_parent ? -1 : 0;
    if (c == KEY_UP && st->sel > min)
        st->sel--;
    else if (c == KEY_DOWN && st->sel < st->count - 1)
        st->sel++;
    else if (c == '\n') {
        open_selected(st);
        return;
    }
    else if (c == '\b' && st->has_parent) {
        st->sel = -1;                    // jump to ".."
        open_selected(st);
        return;
    }
    else if (c == 'r')
        fm_refresh(st);
    else
        return;
    fm_render(st);
}
