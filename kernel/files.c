#include "files.h"
#include "ramfs.h"
#include "initrd.h"

static void copy_name(char *dst, const char *src)
{
    for (int i = 0; i < 31; i++) {
        dst[i] = src[i];
        if (!src[i])
            return;
    }
    dst[31] = 0;
}

int files_list(struct fileinfo *out, int max)
{
    int n = 0;

    // ramfs files first (saved/written stuff)
    for (int i = 0; n < max; i++) {
        const char *nm;
        uint32_t sz;
        if (!ramfs_enum(i, &nm, &sz))
            break;
        copy_name(out[n].name, nm);
        out[n].size = sz;
        out[n].source = FS_RAMFS;
        n++;
    }

    // then IR2 (initramfs) entries
    for (int i = 0; n < max; i++) {
        const char *nm;
        uint64_t sz;
        if (!initrd_enum(i, &nm, &sz))
            break;
        copy_name(out[n].name, nm);
        out[n].size = (uint32_t)sz;
        out[n].source = FS_IR2;
        n++;
    }
    return n;
}

const char *files_read(const char *name, uint32_t *size)
{
    const char *p = ramfs_read(name, size);
    if (p)
        return p;
    uint64_t sz64 = 0;
    const char *q = initrd_read(name, &sz64);
    if (!q)
        return 0;
    *size = (uint32_t)sz64;
    return q;
}
