#include "tar.h"

// ustar reader shared by IR2 and the DR1 store. The archive is walked in
// place: every entry is a 512-byte header followed by the data padded to
// a multiple of 512.

struct tar_hdr {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
};

static uint64_t octal(const char *s, int len)
{
    uint64_t v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '7')
            break;
        v = v * 8 + (uint64_t)(s[i] - '0');
    }
    return v;
}

// GNU tar writes entries as "./name" -- strip the leading "./"
static const char *clean_name(const char *n)
{
    if (n[0] == '.' && n[1] == '/')
        return n + 2;
    return n;
}

static uint64_t entry_size(struct tar_hdr *h)
{
    return octal(h->size, 12);
}

static struct tar_hdr *next_entry(uint64_t end, struct tar_hdr *h)
{
    uint64_t size = entry_size(h);
    uint64_t step = 512 + ((size + 511) & ~(uint64_t)511);
    uint64_t n = (uint64_t)h + step;
    if (n + 512 > end || h->name[0] == 0)
        return 0;
    return (struct tar_hdr *)n;
}

int tar_enum(uint64_t base, uint64_t end, int idx,
             const char **name, uint64_t *size, int *is_dir)
{
    struct tar_hdr *h = (struct tar_hdr *)base;
    int n = 0;
    while (h && h->name[0]) {
        const char *nm = clean_name(h->name);
        if (nm[0]) {                     // skip the "./" root entry
            if (n == idx) {
                int len = 0;
                while (nm[len] && len < 100)
                    len++;
                int dir = len > 0 && nm[len - 1] == '/';
                *is_dir = dir;
                *size = dir ? 0 : entry_size(h);
                *name = nm;              // caller sees the trailing '/' for dirs
                return 1;
            }
            n++;
        }
        h = next_entry(end, h);
    }
    return 0;
}

const char *tar_read(uint64_t base, uint64_t end,
                     const char *name, uint64_t *size)
{
    struct tar_hdr *h = (struct tar_hdr *)base;
    while (h && h->name[0]) {
        const char *n = clean_name(h->name);
        int match = 1;
        for (int i = 0; i < 100; i++) {
            if (n[i] != name[i]) { match = 0; break; }
            if (!name[i])
                break;
        }
        if (match) {
            *size = entry_size(h);
            return (const char *)h + 512;
        }
        h = next_entry(end, h);
    }
    return 0;
}
