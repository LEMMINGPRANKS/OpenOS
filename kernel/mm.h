#ifndef OPENOS_MM_H
#define OPENOS_MM_H

#include <stdint.h>

// Physical memory manager: bitmap over 4 KiB frames from the firmware
// memory map. We can only hand out frames below 1 GiB for now, because
// that is how much the boot trampoline identity-mapped.

void mm_init(unsigned long mb2_addr);
uint64_t mm_total_usable(void);
uint64_t mm_free_bytes(void);
uint64_t pmm_alloc_frame(void);        // returns physical address, 0 if full
void pmm_free_frame(uint64_t addr);

#endif
