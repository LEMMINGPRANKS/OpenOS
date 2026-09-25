#ifndef OPENOS_RAMFS_H
#define OPENOS_RAMFS_H

#include <stdint.h>

// Writable RAM filesystem (kmalloc-backed). The initramfs (IR2) is
// read-only, so saved files live here. `cat` and `ls` check ramfs first.

#define RAMFS_MAX_FILES 16
#define RAMFS_NAME_MAX  32

void ramfs_init(void);
int  ramfs_write(const char *name, const char *data, uint32_t size); // 0 = ok
const char *ramfs_read(const char *name, uint32_t *size);            // 0 = missing
int ramfs_enum(int idx, const char **name, uint32_t *size);          // 0 = no more
void ramfs_list(void);
int  ramfs_count(void);

#endif
