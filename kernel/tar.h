#ifndef OPENOS_TAR_H
#define OPENOS_TAR_H

#include <stdint.h>

// Shared ustar (tar) reader: walks an archive in memory, in place.
// Used by IR2 (the GRUB-loaded initramfs) and DR1 (the on-disk store).

// enumerate entry idx: name (tar-relative, dirs keep their trailing '/'),
// size, is_dir. Returns 0 when idx is past the end.
int tar_enum(uint64_t base, uint64_t end, int idx,
             const char **name, uint64_t *size, int *is_dir);

// find a file by exact name; returns a pointer into the archive or 0
const char *tar_read(uint64_t base, uint64_t end,
                     const char *name, uint64_t *size);

#endif
