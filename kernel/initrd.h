#ifndef OPENOS_INITRD_H
#define OPENOS_INITRD_H

#include <stdint.h>

void initrd_init(unsigned long mb2_addr);
int initrd_ok(void);
void initrd_bounds(uint64_t *start, uint64_t *end);
void initrd_list(void);
const char *initrd_read(const char *name, uint64_t *size_out);

#endif
