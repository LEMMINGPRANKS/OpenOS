#include "apps.h"
#include "shell.h"
#include "filemgr.h"
#include "ramfs.h"
#include "files.h"
#include "ext.h"
#include "term.h"

// Apps are the things that can live inside a window. Each app gets
// keyboard chars through app_input with the window's console.

#define NOTE_MAX 2048
#define NOTE_STATES 3

struct note_state {
    struct console *con;
    char buf[NOTE_MAX];
    int n;
    uint8_t used;
};

static struct note_state notes[NOTE_STATES];

static char app_arg[32];              // filename for the viewer

void app_set_arg(const char *arg)
{
    for (int i = 0; i < 31 && arg[i]; i++)
        app_arg[i] = arg[i];
    app_arg[31] = 0;
}

static void viewer_open(struct console *con)
{
    term_use(con);
    const struct ext_type *t = ext_lookup(app_arg);
    term_setcolor(t->vga_color);
    term_puts("--- ");
    term_puts(app_arg);
    term_puts(" ---\n");
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("type: ");
    term_puts(t->desc);
    term_puts("\n\n");
    uint32_t size = 0;
    const char *data = files_read(app_arg, &size);
    if (!data) {
        term_puts("(file not found)\n");
        return;
    }
    uint32_t cap = size > 4096 ? 4096 : size;
    for (uint32_t i = 0; i < cap; i++)
        term_putc(data[i]);
    if (size && data[size - 1] != '\n')
        term_putc('\n');
    if (size > cap) {
        term_puts("... (");
        char digits[12];
        int n = 0;
        uint32_t s = size - cap;
        while (s) { digits[n++] = (char)('0' + s % 10); s /= 10; }
        while (n) term_putc(digits[--n]);
        term_puts(" more bytes)\n");
    }
}

static struct note_state *note_for(struct console *con)
{
    for (int i = 0; i < NOTE_STATES; i++)
        if (notes[i].used && notes[i].con == con)
            return &notes[i];
    for (int i = 0; i < NOTE_STATES; i++)
        if (!notes[i].used) {
            notes[i].used = 1;
            notes[i].con = con;
            notes[i].n = 0;
            notes[i].buf[0] = 0;
            return &notes[i];
        }
    return &notes[0];
}

static void notepad_open(struct console *con)
{
    note_for(con);
    term_use(con);
    term_puts("NOTEPAD -- typing is saved to note.txt\n");
    term_puts("(the shell can read it: cat note.txt)\n\n");
    ramfs_write("/note.txt", "", 0);
}

static void notepad_input(struct console *con, char c)
{
    struct note_state *st = note_for(con);
    term_use(con);
    if (c == '\n' || c == '\b') {
        if (c == '\b' && st->n == 0)
            return;
        if (c == '\b')
            st->n--;
        else if (st->n < NOTE_MAX - 1)
            st->buf[st->n++] = '\n';
        term_putc(c);
    } else if (c >= 32 && c < 127 && st->n < NOTE_MAX - 1) {
        st->buf[st->n++] = c;
        term_putc(c);
    } else {
        return;                       // unchanged: no save needed
    }
    st->buf[st->n] = 0;
    ramfs_write("/note.txt", st->buf, (uint32_t)st->n);
}

void app_open(enum app_id app, struct console *con)
{
    if (app == APP_SHELL)
        shell_app_open(con);
    else if (app == APP_NOTEPAD)
        notepad_open(con);
    else if (app == APP_FILES)
        filemgr_open(con);
    else if (app == APP_VIEWER)
        viewer_open(con);
}

void app_input(enum app_id app, struct console *con, char c)
{
    if (app == APP_SHELL)
        shell_app_input(con, c);
    else if (app == APP_NOTEPAD)
        notepad_input(con, c);
    else if (app == APP_FILES)
        filemgr_input(con, c);
}

const char *app_name(enum app_id app)
{
    if (app == APP_SHELL)   return "Shell";
    if (app == APP_NOTEPAD) return "Notepad";
    if (app == APP_FILES)   return "Files";
    if (app == APP_VIEWER)  return "Viewer";
    return "?";
}
