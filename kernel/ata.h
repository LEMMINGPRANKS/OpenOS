#ifndef OPENOS_ATA_H
#define OPENOS_ATA_H

#include <stdint.h>

// DR1: the main drive. ATA PIO on the primary bus, LBA28, polling.
// One drive (primary master) is all OpenOS needs for now.

int         ata_init(void);                       // IDENTIFY; 1 = present
int         ata_present(void);
const char *ata_model(void);                      // 40-char IDENTIFY string
uint64_t    ata_sectors(void);                    // LBA28 sector count
int         ata_read(uint32_t lba, uint32_t nsect, void *buf);   // 0 = ok
int         ata_write(uint32_t lba, uint32_t nsect, const void *buf);
uint8_t     ata_dbg_status(void);   // last status byte (debugging)
uint8_t     ata_dbg_first_status(void);
uint8_t     ata_dbg_where(void);    // 1 drq 2 post-data 3 flush

#endif
