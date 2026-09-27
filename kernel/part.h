#ifndef OPENOS_PART_H
#define OPENOS_PART_H

#include <stdint.h>

// MBR partition law: the OpenOS partition has type byte 0x7F; all kernel
// disk LBAs are relative to its start (dual boot -- we share the disk).

void     part_scan(void);            // resolve the base on both disks
uint32_t part_base(void);            // base LBA of the current disk (0 = none)
uint32_t disk_lba(uint32_t rel);     // part_base() + rel -- use everywhere

#endif
