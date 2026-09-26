#ifndef OPENOS_FILES_H
#define OPENOS_FILES_H

#include <stdint.h>

// Unified view of every file OpenOS can see: IR2 (initramfs, read-only)
// plus the ramfs (writable), presented as ONE directory tree rooted at "/".
// Paths are absolute ("/docs/readme.txt"); ramfs copies shadow IR2 files.

#define FILES_MAX       48
#define FILES_NAME_MAX  64
#define FS_RAMFS 1
#define FS_IR2   2

struct fileinfo {
    char name[FILES_NAME_MAX];        // child name only, no path, no '/'
    uint32_t size;
    uint8_t source;                   // FS_RAMFS or FS_IR2
    uint8_t is_dir;
};

// Direct children of dir ("/" = root). Directories are not duplicated;
// a ramfs file shadows an IR2 file of the same name. Returns count.
int files_list_dir(const char *dir, struct fileinfo *out, int max);

// Is this path a directory (has children, or "/" itself)?
int files_is_dir(const char *path);

// Read a file by absolute path. ramfs first, then IR2. 0 = missing.
const char *files_read(const char *path, uint32_t *size);

#endif
