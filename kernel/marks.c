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

// up to max bookmark urls (newest first), for the home page's tiles
int marks_bookmarks(char out[][96], int max)
{
    static char lines[MARKS_MAX][MARK_LINE];
    int n = load_lines(BOOKMARKS_FILE, lines);
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < MARK_LINE; j++)
            out[i][j] = lines[i][j];
    return n;
}
