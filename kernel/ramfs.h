#ifndef OPENOS_RAMFS_H
#define OPENOS_RAMFS_H

#include <stdint.h>

// Writable RAM filesystem (kmalloc-backed). The initramfs (IR2) is
// read-only, so saved files live here. `cat` and `ls` check ramfs first.
// Names are absolute paths ("/note.txt"); directories exist implicitly
// when a stored path has that prefix, or explicitly via ramfs_mkdir
// (so empty folders survive too).

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX  64

void ramfs_init(void);
int  ramfs_write(const char *name, const char *data, uint32_t size); // 0 = ok
int  ramfs_mkdir(const char *name);   // 0 ok, -1 bad name, -2 file exists, -3 full
const char *ramfs_read(const char *name, uint32_t *size);            // 0 = missing
int  ramfs_enum(int idx, const char **name, uint32_t *size, int *is_dir); // 0 = no more

#endif
