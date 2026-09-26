#include "ramfs.h"
#include "heap.h"

struct ramfile {
    char name[RAMFS_NAME_MAX];
    char *data;
    uint32_t size;
    uint8_t used;
};

static struct ramfile files[RAMFS_MAX_FILES];

void ramfs_init(void)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        files[i].used = 0;
}

static struct ramfile *find_slot(const char *name)
{
    struct ramfile *free_slot = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].used) {
            for (int j = 0; ; j++) {
                if (j >= RAMFS_NAME_MAX || !files[i].name[j] || !name[j]) {
                    if (files[i].name[j] == name[j])
                        return &files[i];   // exact match: overwrite
                    break;
                }
                if (files[i].name[j] != name[j])
                    break;
            }
        } else if (!free_slot) {
            free_slot = &files[i];
        }
    }
    return free_slot;
}

int ramfs_write(const char *name, const char *data, uint32_t size)
{
    struct ramfile *f = find_slot(name);
    if (!f)
        return -1;                      // table full
    char *buf = kmalloc(size ? size : 1);
    if (!buf)
        return -2;                      // out of heap
    if (f->used)
        kfree(f->data);
    for (uint32_t i = 0; i < RAMFS_NAME_MAX - 1 && name[i]; i++)
        f->name[i] = name[i];
    f->name[RAMFS_NAME_MAX - 1] = 0;
    for (uint32_t i = 0; i < size; i++)
        buf[i] = data[i];
    f->data = buf;
    f->size = size;
    f->used = 1;
    return 0;
}

const char *ramfs_read(const char *name, uint32_t *size)
{
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        if (files[i].used) {
            int same = 1;
            for (int j = 0; ; j++) {
                if (files[i].name[j] != name[j]) { same = 0; break; }
                if (!files[i].name[j]) break;
            }
            if (same) {
                if (size)
                    *size = files[i].size;
                return files[i].data;
            }
        }
    return 0;
}

int ramfs_enum(int idx, const char **name, uint32_t *size)
{
    int n = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        if (files[i].used) {
            if (n == idx) {
                *name = files[i].name;
                *size = files[i].size;
                return 1;
            }
            n++;
        }
    return 0;
}

int ramfs_count(void)
{
    int n = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        n += files[i].used;
    return n;
}
