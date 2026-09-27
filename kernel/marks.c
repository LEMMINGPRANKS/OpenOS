#include "marks.h"
#include "ramfs.h"
#include "store.h"

#define BOOKMARKS_FILE "/bookmarks.txt"
#define HISTORY_FILE   "/history.txt"
#define MARK_LINE      96        // longest url we keep
#define MARKS_MAX      24        // bookmarks kept
#define HISTORY_MAX    24        // history entries kept

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int str_same(const char *a, const char *b)
{
    for (int i = 0; ; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
}

// split a file's contents into lines (max chars each), returns line count
static int split_lines(const char *data, uint32_t size,
                       char out[MARKS_MAX][MARK_LINE])
{
    int n = 0, c = 0;
    for (uint32_t i = 0; i < size && n < MARKS_MAX; i++) {
        char ch = data[i];
        if (ch == '\n' || ch == '\r') {
            if (c) { out[n][c] = 0; n++; c = 0; }
        } else if (c < MARK_LINE - 1) {
            out[n][c++] = ch;
        }
    }
    if (c && n < MARKS_MAX)
        out[n++][c] = 0;
    return n;
}

static int load_lines(const char *path, char out[MARKS_MAX][MARK_LINE])
{
    uint32_t size = 0;
    const char *data = ramfs_read(path, &size);
    if (!data)
        return 0;
    return split_lines(data, size, out);
}

static void save_lines(const char *path, char lines[MARKS_MAX][MARK_LINE], int n)
{
    static char buf[MARKS_MAX * MARK_LINE];
    int k = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; lines[i][j]; j++)
            buf[k++] = lines[i][j];
        buf[k++] = '\n';
    }
    buf[k] = 0;
    ramfs_write(path, buf, (uint32_t)k);
    store_flush();                     // survives power-off, like settings
}

void marks_bookmark(const char *url)
{
    static char lines[MARKS_MAX][MARK_LINE];
    int n = load_lines(BOOKMARKS_FILE, lines);
    for (int i = 0; i < n; i++)
        if (str_same(lines[i], url))
            return;                    // already remembered
    if (n >= MARKS_MAX) {              // full: drop the oldest
        for (int i = 1; i < MARKS_MAX; i++)
            for (int j = 0; j < MARK_LINE; j++)
                lines[i - 1][j] = lines[i][j];
        n = MARKS_MAX - 1;
    }
    int u = str_len(url);
    if (u > MARK_LINE - 1)
        u = MARK_LINE - 1;
    for (int i = 0; i < u; i++)
        lines[n][i] = url[i];
    lines[n][u] = 0;
    save_lines(BOOKMARKS_FILE, lines, n + 1);
}

void marks_history_add(const char *url)
{
    static char lines[MARKS_MAX][MARK_LINE];
    int n = load_lines(HISTORY_FILE, lines);
    for (int i = 0; i < n; i++)
        if (str_same(lines[i], url))
            return;                    // already the newest story of this page
    int keep = n < HISTORY_MAX ? n : HISTORY_MAX - 1;
    for (int i = keep - 1; i >= 0; i--)     // shift down, newest stays on top
        for (int j = 0; j < MARK_LINE; j++)
            lines[i + 1][j] = lines[i][j];
    int u = str_len(url);
    if (u > MARK_LINE - 1)
        u = MARK_LINE - 1;
    for (int i = 0; i < u; i++)
        lines[0][i] = url[i];
    lines[0][u] = 0;
    save_lines(HISTORY_FILE, lines, keep + 1);
}

// put "<li><a href='url'>label</a></li>" into buf
static int put_link(char *buf, int max, int k, const char *url)
{
    static const char pre[] = "<li><a href='";
    static const char mid[] = "'>";
    static const char post[] = "</a></li>\n";
    for (int i = 0; pre[i] && k < max - 1; i++)  buf[k++] = pre[i];
    for (int i = 0; url[i] && k < max - 1; i++)  buf[k++] = url[i];
    for (int i = 0; mid[i] && k < max - 1; i++)  buf[k++] = mid[i];
    for (int i = 0; url[i] && k < max - 1; i++)  buf[k++] = url[i];
    for (int i = 0; post[i] && k < max - 1; i++) buf[k++] = post[i];
    return k;
}

static int put_str(char *buf, int max, int k, const char *s)
{
    for (int i = 0; s[i] && k < max - 1; i++)
        buf[k++] = s[i];
    return k;
}

int marks_home_page(char *buf, int max)
{
    static char bm[MARKS_MAX][MARK_LINE];
    static char hi[MARKS_MAX][MARK_LINE];
    int nb = load_lines(BOOKMARKS_FILE, bm);
    int nh = load_lines(HISTORY_FILE, hi);

    int k = 0;
    k = put_str(buf, max, k,
        "<html><head><title>Home</title></head><body><h1>OpenOS Home</h1>"
        "<p>type an address like example.com -- or just words to search "
        "the whole internet.</p><h2>Bookmarks</h2>");
    if (!nb)
        k = put_str(buf, max, k, "<p>(none yet -- load a page, then type "
                                 "bm and press Enter)</p>");
    for (int i = 0; i < nb; i++)
        k = put_link(buf, max, k, bm[i]);
    k = put_str(buf, max, k, "<h2>Recently visited</h2>");
    if (!nh)
        k = put_str(buf, max, k, "<p>(nothing yet)</p>");
    for (int i = 0; i < nh; i++)
        k = put_link(buf, max, k, hi[i]);
    k = put_str(buf, max, k,
        "<p><a href='/news'>news</a> <a href='/comments'>comments</a> "
        "<a href='/roadmap'>ideas</a> <a href='/updates'>updates</a></p>"
        "</body></html>");
    buf[k] = 0;
    return k;
}
