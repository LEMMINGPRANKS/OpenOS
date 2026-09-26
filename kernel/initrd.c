#include "initrd.h"
#include "tar.h"

// IR2: the initramfs. GRUB loads a ustar tar archive into memory as a
// multiboot2 module; we find it in the info tags and hand the region to
// the shared tar reader.

static uint64_t rd_start;
static uint64_t rd_end;

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

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

// enumerate entries: plain files (full path, e.g. "docs/readme.txt") and
// tar directory entries (trailing '/', e.g. "docs/"). is_dir flags dirs.
int initrd_enum(int idx, const char **name, uint64_t *size, int *is_dir)
{
    if (!initrd_ok())
        return 0;
    return tar_enum(rd_start, rd_end, idx, name, size, is_dir);
}

const char *initrd_read(const char *name, uint64_t *size)
{
    if (!initrd_ok())
        return 0;
    return tar_read(rd_start, rd_end, name, size);
}
