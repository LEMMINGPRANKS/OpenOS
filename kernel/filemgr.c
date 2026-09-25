#include "filemgr.h"
#include "files.h"
#include "ext.h"
#include "term.h"
#include "kb.h"
#include "wm.h"
#include "apps.h"

#define FM_STATES 2
#define VIEW_WIN_W 448
#define VIEW_WIN_H 340

struct fm_state {
    struct console *con;
    uint8_t used;
    int sel;
    int count;
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
    st->count = files_list(st->files, FILES_MAX);
    if (st->sel >= st->count)
        st->sel = st->count ? st->count - 1 : 0;
}

static void fm_render(struct fm_state *st)
{
    term_use(st->con);
    term_setcolor(TERM_COLOR_WHITE_ON_BLACK);
    term_clear();
    term_puts("FILES  (up/down arrows, Enter opens, r refreshes)\n\n");
    if (!st->count) {
        term_puts("  no files found\n");
        return;
    }
    for (int i = 0; i < st->count; i++) {
        struct fileinfo *f = &st->files[i];
        const struct ext_type *t = ext_lookup(f->name);
        term_setcolor(TERM_COLOR_WHITE_ON_BLACK);
        term_puts(i == st->sel ? " > " : "   ");
        term_setcolor(t->vga_color);
        term_puts(f->name);
        term_setcolor(TERM_COLOR_WHITE_ON_BLACK);
        term_puts("  [");
        term_puts(f->source == FS_RAMFS ? "ramfs" : "IR2");
        term_puts("] ");
        print_u32(f->size);
        term_puts("b  ");
        term_puts(t->desc);
        term_putc('\n');
    }
}

void filemgr_open(struct console *con)
{
    struct fm_state *st = fm_for(con);
    fm_refresh(st);
    fm_render(st);
}

static void open_selected(struct fm_state *st)
{
    if (!st->count)
        return;
    struct fileinfo *f = &st->files[st->sel];
    if (!ext_is_text(f->name)) {
        fm_render(st);               // redraw, then complain below list
        term_setcolor(0x0E);
        term_puts("\n  no viewer for this type yet (binary?)\n");
        term_setcolor(TERM_COLOR_WHITE_ON_BLACK);
        return;
    }
    app_set_arg(f->name);
    wm_open(APP_VIEWER, 110, 80, VIEW_WIN_W, VIEW_WIN_H);
    // wm_open already repainted everything in the right order
    // (viewer focused = painted last); redrawing here would
    // stomp the viewer with the files console
}

void filemgr_input(struct console *con, char c)
{
    struct fm_state *st = fm_for(con);
    if (c == KEY_UP && st->sel > 0)
        st->sel--;
    else if (c == KEY_DOWN && st->sel < st->count - 1)
        st->sel++;
    else if (c == '\n') {
        open_selected(st);
        return;
    }
    else if (c == 'r')
        fm_refresh(st);
    else
        return;
    fm_render(st);
}
