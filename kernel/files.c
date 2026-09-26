#include "files.h"
#include "ramfs.h"
#include "initrd.h"
#include "path.h"

static int starts_with(const char *s, const char *pre)
{
    for (int i = 0; pre[i]; i++)
        if (s[i] != pre[i])
            return 0;
    return 1;
}

// dir "/" -> prefix "/", IR2 prefix ""; dir "/docs" -> "/docs/", "docs/"
static void prefixes(const char *dir, char *rp, char *ip)
{
    int root = dir[0] == '/' && !dir[1];
    if (root) {
        rp[0] = '/'; rp[1] = 0;
        ip[0] = 0;
        return;
    }
    int n = 0;
    while (dir[n] && n < PATH_MAX - 2) {
        rp[n] = dir[n];
        ip[n] = dir[n + 1];            // skip the leading '/'
        n++;
    }
    if (dir[n])
        n = PATH_MAX - 2;              // truncated: match will just fail
    rp[n] = '/'; rp[n + 1] = 0;
    ip[n - 1] = '/'; ip[n] = 0;
}

static void copy_name(char *dst, const char *src, int len)
{
    if (len > FILES_NAME_MAX - 1)
        len = FILES_NAME_MAX - 1;
    for (int i = 0; i < len; i++)
        dst[i] = src[i];
    dst[len] = 0;
}

static int find_child(struct fileinfo *out, int n, const char *name, int len)
{
    for (int i = 0; i < n; i++) {
        int same = 1;
        for (int j = 0; j < len; j++)
            if (out[i].name[j] != name[j]) { same = 0; break; }
        if (same && out[i].name[len] == 0)
            return i;
    }
    return -1;
}

// add one child (name/len, maybe a dir, from source). Returns 1 if listed.
static int add_child(struct fileinfo *out, int *n, int max,
                     const char *name, int len, uint32_t size,
                     uint8_t source, int is_dir)
{
    int hit = find_child(out, *n, name, len);
    if (hit >= 0) {
        if (is_dir)
            out[hit].is_dir = 1;       // a dir beats a file of the same name
        return 0;                      // already listed (ramfs shadows IR2)
    }
    if (*n >= max)
        return 0;
    copy_name(out[*n].name, name, len);
    out[*n].size = size;
    out[*n].source = source;
    out[*n].is_dir = (uint8_t)(is_dir ? 1 : 0);
    (*n)++;
    return 1;
}

int files_list_dir(const char *dir, struct fileinfo *out, int max)
{
    char rp[PATH_MAX], ip[PATH_MAX];
    prefixes(dir, rp, ip);
    int n = 0;

    // ramfs: names are full paths ("/docs/a.txt"); dirs are implicit
    for (int i = 0; ; i++) {
        const char *nm;
        uint32_t sz;
        if (!ramfs_enum(i, &nm, &sz))
            break;
        if (!starts_with(nm, rp))
            continue;
        const char *rest = nm + path_len(rp);
        if (!rest[0])
            continue;                  // the directory itself
        int len = 0;
        while (rest[len] && rest[len] != '/')
            len++;
        add_child(out, &n, max, rest, len, sz, FS_RAMFS, rest[len] == '/');
    }

    // IR2: tar-relative names, dirs arrive both as entries and via files
    for (int i = 0; ; i++) {
        const char *nm;
        uint64_t sz;
        int is_dir;
        if (!initrd_enum(i, &nm, &sz, &is_dir))
            break;
        if (!starts_with(nm, ip))
            continue;
        const char *rest = nm + path_len(ip);
        if (is_dir && !rest[0])
            continue;                  // the directory's own tar entry
        int len = 0;
        while (rest[len] && rest[len] != '/')
            len++;
        int child_is_dir = is_dir || rest[len] == '/';
        add_child(out, &n, max, rest, len, (uint32_t)sz, FS_IR2, child_is_dir);
    }
    return n;
}

int files_is_dir(const char *path)
{
    if (path[0] == '/' && !path[1])
        return 1;
    char rp[PATH_MAX], ip[PATH_MAX];
    prefixes(path, rp, ip);

    for (int i = 0; ; i++) {
        const char *nm;
        uint32_t sz;
        if (!ramfs_enum(i, &nm, &sz))
            break;
        if (starts_with(nm, rp) && path_len(nm) > path_len(rp))
            return 1;                  // something lives inside
    }
    for (int i = 0; ; i++) {
        const char *nm;
        uint64_t sz;
        int is_dir;
        if (!initrd_enum(i, &nm, &sz, &is_dir))
            break;
        if (starts_with(nm, ip) && (path_len(nm) > path_len(ip) || is_dir))
            return 1;
    }
    return 0;
}

const char *files_read(const char *path, uint32_t *size)
{
    const char *p = ramfs_read(path, size);
    if (p)
        return p;
    if (path[0] != '/')                // callers pass absolute paths
        return 0;
    uint64_t sz64 = 0;
    const char *q = initrd_read(path + 1, &sz64);
    if (!q)
        return 0;
    *size = (uint32_t)sz64;
    return q;
}
