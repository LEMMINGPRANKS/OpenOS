#include "path.h"

int path_len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

const char *path_basename(const char *path)
{
    int last = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/')
            last = i;
    return path + last + 1;
}

void path_parent(const char *path, char *out)
{
    int len = path_len(path);
    int end = len;
    while (end > 0 && path[end - 1] == '/')
        end--;                         // drop trailing slashes (but keep "/")
    if (end <= 0) {
        out[0] = '/'; out[1] = 0;
        return;
    }
    int cut = end;
    while (cut > 0 && path[cut - 1] != '/')
        cut--;
    if (cut <= 0) {
        out[0] = '/'; out[1] = 0;
        return;
    }
    // keep everything before the final component; "/" stays "/"
    int n = cut > 1 ? cut - 1 : 1;
    for (int i = 0; i < n; i++)
        out[i] = path[i];
    out[n] = 0;
}

// Append one component (no '/' inside) to base, resolving ".." and "."
static void add_component(char *base, const char *comp, int clen)
{
    if (clen == 1 && comp[0] == '.')
        return;
    if (clen == 2 && comp[0] == '.' && comp[1] == '.') {
        path_parent(base, base);
        return;
    }
    int n = path_len(base);
    if (n <= 0) { base[0] = '/'; n = 1; }
    if (base[n - 1] != '/')
        base[n++] = '/';
    for (int i = 0; i < clen && n < PATH_MAX - 1; i++)
        base[n++] = comp[i];
    base[n] = 0;
}

void path_resolve(const char *cwd, const char *arg, char *out)
{
    int n = 0;
    if (arg[0] == '/') {
        out[n++] = '/';
        out[n] = 0;
    } else {
        while (cwd[n] && n < PATH_MAX - 1) {
            out[n] = cwd[n];
            n++;
        }
        out[n] = 0;
    }
    for (int i = 0; arg[i];) {
        while (arg[i] == '/')
            i++;                       // skip separators
        int start = i;
        while (arg[i] && arg[i] != '/')
            i++;
        if (i > start)
            add_component(out, arg + start, i - start);
    }
    if (!out[0]) {
        out[0] = '/'; out[1] = 0;
    }
}
