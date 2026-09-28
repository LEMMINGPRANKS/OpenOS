#include "codeapp.h"
#include "term.h"
#include "appbar.h"
#include "ramfs.h"
#include "files.h"
#include "store.h"
#include "timer.h"
#include "apps.h"

// The Code app. Editing model matches Notepad (type, backspace, Enter --
// append-only) but every keystroke re-renders the file through the
// syntax colouriser, so what you see is always the highlighted code.
// Saves to ramfs on every change and to DR1 on a typing pause.

#define CODE_MAX      4096
#define CODE_STATES   4

// VGA inks through the light palette: keyword blue, string green,
// number red, comment brown, everything else the default ink.
#define INK_KW     0x01
#define INK_STR    0x02
#define INK_NUM    0x04
#define INK_COM    0x06
#define INK_DEF    0x07

enum lang { LANG_CPP, LANG_JS, LANG_HTML, LANG_TEXT };

struct code_state {
    uint8_t used;
    struct console *con;
    char path[64];
    uint16_t n;
    char buf[CODE_MAX];
};

static struct code_state states[CODE_STATES];
static uint64_t last_flush;

static int ext_is(const char *path, const char *ext)
{
    int n = 0, e = 0;
    while (path[n])
        n++;
    while (ext[e])
        e++;
    if (n <= e)
        return 0;
    for (int i = 0; i <= e; i++)
        if (path[n - e - 1 + i] != (i == 0 ? '.' : ext[i - 1]))
            return 0;
    return 1;
}

static enum lang lang_of(const char *path)
{
    if (ext_is(path, "html") || ext_is(path, "htm"))
        return LANG_HTML;
    if (ext_is(path, "cpp") || ext_is(path, "hpp") || ext_is(path, "h"))
        return LANG_CPP;
    if (ext_is(path, "js"))
        return LANG_JS;
    return LANG_TEXT;
}

static int is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '#';
}

static int kw_match(const char *s, int len, const char *const *kws)
{
    for (int i = 0; kws[i]; i++) {
        const char *k = kws[i];
        int kl = 0;
        while (k[kl])
            kl++;
        if (kl == len) {
            int same = 1;
            for (int j = 0; j < len; j++)
                if (k[j] != s[j]) { same = 0; break; }
            if (same)
                return 1;
        }
    }
    return 0;
}

static const char *const cpp_kws[] = {
    "int", "char", "void", "bool", "long", "short", "float", "double",
    "unsigned", "signed", "const", "static", "struct", "class", "public",
    "private", "protected", "if", "else", "while", "for", "return",
    "break", "continue", "new", "delete", "true", "false", "nullptr",
    "#include", "#define", 0
};

static const char *const js_kws[] = {
    "var", "let", "const", "function", "return", "if", "else", "while",
    "for", "class", "new", "true", "false", "null", "undefined", "this",
    "break", "continue", 0
};

// emit one coloured character-run
static void emit(const char *s, int len, uint8_t ink)
{
    term_setcolor(ink);
    for (int i = 0; i < len; i++)
        term_putc(s[i]);
}

static void render(struct code_state *st)
{
    enum lang lg = lang_of(st->path);
    const char *b = st->buf;
    int n = st->n;
    for (int i = 0; i < n;) {
        if (lg == LANG_HTML) {
            if (b[i] == '<') {              // tag: <word ...> in blue
                int j = i + 1;
                while (j < n && b[j] != '>' && b[j] != ' ' && b[j] != '\n')
                    j++;
                emit(b + i, j - i, INK_KW);
                i = j;
            } else if (b[i] == '"') {       // attribute string
                int j = i + 1;
                while (j < n && b[j] != '"' && b[j] != '\n')
                    j++;
                if (j < n && b[j] == '"')
                    j++;
                emit(b + i, j - i, INK_STR);
                i = j;
            } else {                        // plain text until markup
                int j = i;
                while (j < n && b[j] != '<' && b[j] != '"')
                    j++;
                emit(b + i, j - i, INK_DEF);
                i = j;
            }
            continue;
        }
        // CPP + JS: comments, strings, keywords, numbers
        if (b[i] == '/' && i + 1 < n && b[i + 1] == '/') {
            int j = i;
            while (j < n && b[j] != '\n')
                j++;
            emit(b + i, j - i, INK_COM);
            i = j;
        } else if (b[i] == '/' && i + 1 < n && b[i + 1] == '*') {
            int j = i + 2;
            while (j + 1 < n && !(b[j] == '*' && b[j + 1] == '/'))
                j++;
            j = j + 1 < n ? j + 2 : n;
            emit(b + i, j - i, INK_COM);
            i = j;
        } else if (b[i] == '"' || b[i] == '\'') {
            char q = b[i];
            int j = i + 1;
            while (j < n && b[j] != q && b[j] != '\n')
                j++;
            if (j < n && b[j] == q)
                j++;
            emit(b + i, j - i, INK_STR);
            i = j;
        } else if (is_ident(b[i])) {
            int j = i;
            while (j < n && (is_ident(b[j]) ||
                   (b[j] >= '0' && b[j] <= '9')))
                j++;
            uint8_t ink = INK_DEF;
            if (b[i] == '#')
                ink = INK_KW;
            else if (b[i] >= '0' && b[i] <= '9')
                ink = INK_NUM;
            else if (kw_match(b + i, j - i,
                       lg == LANG_JS ? js_kws : cpp_kws))
                ink = INK_KW;
            emit(b + i, j - i, ink);
            i = j;
        } else if (b[i] >= '0' && b[i] <= '9') {
            int j = i;
            while (j < n && b[j] >= '0' && b[j] <= '9')
                j++;
            emit(b + i, j - i, INK_NUM);
            i = j;
        } else {
            term_setcolor(INK_DEF);
            term_putc(b[i]);
            i++;
        }
    }
}

static void repaint(struct code_state *st)
{
    term_use(st->con);
    term_protect(st->con, APPBAR_ROWS);
    term_clear();
    const char *base = st->path;
    for (int i = 0; st->path[i]; i++)
        if (st->path[i] == '/')
            base = st->path + i + 1;
    appbar_paint(st->con, base, "auto-saved to DR1", 0x007ACC);
    term_setcolor(INK_DEF);
    term_putc('\n');
    render(st);
}

static struct code_state *state_for(struct console *con)
{
    for (int i = 0; i < CODE_STATES; i++)
        if (states[i].used && states[i].con == con)
            return &states[i];
    for (int i = 0; i < CODE_STATES; i++)
        if (!states[i].used) {
            states[i].used = 1;
            states[i].con = con;
            states[i].path[0] = '/';
            states[i].path[1] = 0;
            states[i].n = 0;
            states[i].buf[0] = 0;
            return &states[i];
        }
    return &states[0];
}

void code_open(struct console *con)
{
    struct code_state *st = state_for(con);
    const char *arg = app_get_arg();
    if (arg && arg[0]) {
        int i = 0;
        for (; i < 63 && arg[i]; i++)
            st->path[i] = arg[i];
        st->path[i] = 0;
        app_set_arg("");
    } else {
        const char *d = "/untitled.cpp";
        for (int i = 0; d[i]; i++)
            st->path[i] = d[i];
        st->path[13] = 0;
    }
    uint32_t sz = 0;
    const char *data = files_read(st->path, &sz);
    st->n = 0;
    if (data) {
        for (uint32_t i = 0; i < sz && st->n < CODE_MAX - 1; i++)
            st->buf[st->n++] = data[i];
    }
    st->buf[st->n] = 0;
    repaint(st);
}

void code_input(struct console *con, char c)
{
    struct code_state *st = state_for(con);
    if (c == '\n' || c == '\b') {
        if (c == '\b' && st->n == 0)
            return;
        if (c == '\b')
            st->n--;
        else if (st->n < CODE_MAX - 1)
            st->buf[st->n++] = '\n';
    } else if (c >= 32 && c < 127 && st->n < CODE_MAX - 1) {
        st->buf[st->n++] = c;
    } else {
        return;
    }
    st->buf[st->n] = 0;
    ramfs_write(st->path, st->buf, st->n);
    repaint(st);
    uint64_t now = timer_ticks();
    if (now - last_flush >= 200) {       // 2s typing pause -> DR1
        last_flush = now;
        store_flush();
    }
}
