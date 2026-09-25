#ifndef OPENOS_FILES_H
#define OPENOS_FILES_H

#include <stdint.h>

// Unified view of every file OpenOS can see: IR2 (initramfs, read-only)
// plus the ramfs (writable). The shell and the file manager both use this.

#define FILES_MAX  48
#define FS_RAMFS 1
#define FS_IR2   2

struct fileinfo {
    char name[32];
    uint32_t size;
    uint8_t source;                  // FS_RAMFS or FS_IR2
};

int  files_list(struct fileinfo *out, int max);   // returns count
const char *files_read(const char *name, uint32_t *size);  // 0 = missing

#endif
