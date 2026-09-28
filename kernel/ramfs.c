#include "ramfs.h"
#include "heap.h"

struct ramfile {
    char name[RAMFS_NAME_MAX];
    char *data;
    uint32_t size;
    uint8_t used;
    uint8_t is_dir;
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

int ramfs_mkdir(const char *name)
{
    if (!name || name[0] != '/' || !name[1])
        return -1;                      // need a real path like "/docs"
    for (int i = 0; name[i]; i++)
        if (name[i] == '/' && name[i + 1] == '/')
            return -1;                  // empty path part
    int len = 0;
    while (name[len])
        len++;
    if (name[len - 1] == '/')
        return -1;                      // no trailing slash

    struct ramfile *free_slot = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) {
            if (!free_slot)
                free_slot = &files[i];
            continue;
        }
        int same = 1;
        for (int j = 0; ; j++) {
            if (files[i].name[j] != name[j]) { same = 0; break; }
            if (!name[j]) break;
        }
        if (same)
            return files[i].is_dir ? 0 : -2;   // already there (dir ok)
    }
    if (!free_slot)
        return -3;
    for (int i = 0; i < len && i < RAMFS_NAME_MAX - 1; i++)
        free_slot->name[i] = name[i];
    free_slot->name[len < RAMFS_NAME_MAX - 1 ? len : RAMFS_NAME_MAX - 1] = 0;
    free_slot->data = 0;
    free_slot->size = 0;
    free_slot->is_dir = 1;
    free_slot->used = 1;
    return 0;
}

int ramfs_write(const char *name, const char *data, uint32_t size)
{
    struct ramfile *f = find_slot(name);
    if (!f)
        return -1;                      // table full
    if (f->used && f->is_dir)
        return -2;                      // a folder owns this name
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
        if (files[i].used && !files[i].is_dir) {
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

int ramfs_enum(int idx, const char **name, uint32_t *size, int *is_dir)
{
    int n = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++)
        if (files[i].used) {
            if (n == idx) {
                *name = files[i].name;
                *size = files[i].size;
                if (is_dir)
                    *is_dir = files[i].is_dir;
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
