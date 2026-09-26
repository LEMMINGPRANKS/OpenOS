#ifndef OPENOS_INITRD_H
#define OPENOS_INITRD_H

#include <stdint.h>

void initrd_init(unsigned long mb2_addr);
int initrd_ok(void);
void initrd_bounds(uint64_t *start, uint64_t *end);

// enumerate entries (files + tar directory entries). Names are tar-relative
// ("docs/readme.txt"); dirs come back with a trailing '/' and is_dir=1.
int initrd_enum(int idx, const char **name, uint64_t *size_out, int *is_dir);

// find a file by tar-relative name ("docs/readme.txt"), 0 = missing
const char *initrd_read(const char *name, uint64_t *size_out);

#endif
