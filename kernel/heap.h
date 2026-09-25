#ifndef OPENOS_HEAP_H
#define OPENOS_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
uint64_t heap_used(void);
uint64_t heap_total(void);

#endif
