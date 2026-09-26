#include "initrd.h"

// IR2: the initramfs. GRUB loads a ustar tar archive into memory as a
// multiboot2 module; we find it in the info tags and read files in place.

static uint64_t rd_start;
static uint64_t rd_end;

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

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

void initrd_init(unsigned long mb2_addr)
{
    uint32_t total = *(volatile uint32_t *)mb2_addr;
    uint64_t p = mb2_addr + 8;
    while (p < mb2_addr + total) {
        volatile struct mb2_tag *tag = (volatile struct mb2_tag *)p;
        if (tag->type == 0)
            break;                      // end tag
        if (tag->type == 3) {           // module
            uint32_t start = *(volatile uint32_t *)(p + 8);
            uint32_t end = *(volatile uint32_t *)(p + 12);
            rd_start = start;
            rd_end = end;
            return;
        }
        p = (p + tag->size + 7) & ~(uint64_t)7;
    }
}

int initrd_ok(void)
{
    return rd_end > rd_start;
}

void initrd_bounds(uint64_t *start, uint64_t *end)
{
    *start = rd_start;
    *end = rd_end;
}

// GNU tar writes entries as "./name" -- strip the leading "./"
static const char *clean_name(const char *n)
{
    if (n[0] == '.' && n[1] == '/')
        return n + 2;
    return n;
}

static struct tar_hdr *next_entry(struct tar_hdr *h)
{
    uint64_t size = octal(h->size, 12);
    uint64_t step = 512 + ((size + 511) & ~(uint64_t)511);
    struct tar_hdr *n = (struct tar_hdr *)((uint64_t)h + step);
    if ((uint64_t)n + 512 > rd_end || h->name[0] == 0)
        return 0;
    return n;
}

// enumerate entries: plain files (full path, e.g. "docs/readme.txt") and
// tar directory entries (trailing '/', e.g. "docs/"). is_dir flags dirs.
int initrd_enum(int idx, const char **name, uint64_t *size_out, int *is_dir)
{
    if (!initrd_ok())
        return 0;
    struct tar_hdr *h = (struct tar_hdr *)rd_start;
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
                *size_out = dir ? 0 : octal(h->size, 12);
                *name = nm;              // caller sees the trailing '/' for dirs
                return 1;
            }
            n++;
        }
        h = next_entry(h);
    }
    return 0;
}

const char *initrd_read(const char *name, uint64_t *size_out)
{
    if (!initrd_ok())
        return 0;
    struct tar_hdr *h = (struct tar_hdr *)rd_start;
    while (h && h->name[0]) {
        const char *n = clean_name(h->name);
        int match = 1;
        for (int i = 0; i < 100; i++) {
            if (n[i] != name[i]) { match = 0; break; }
            if (!name[i])
                break;
        }
        if (match) {
            *size_out = octal(h->size, 12);
            return (const char *)h + 512;
        }
        h = next_entry(h);
    }
    return 0;
}
