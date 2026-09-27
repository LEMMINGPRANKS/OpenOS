#ifndef OPENOS_STORE_H
#define OPENOS_STORE_H

#include <stdint.h>

// DR1 store: every ramfs file squished to the main drive in Freddie's
// .WMBG format (QuantumSquish container, method 0 = store) so it survives
// power-off. store_flush() writes them; store_load() copies them back
// into ramfs at boot. Superblock + file table + data sectors.

#define STORE_LBA         2048        // first sector of the store region
#define STORE_MAX_SECTORS 1024        // 512 KiB of files
#define STORE_MAGIC       "OPENOSST"

int store_load(void);                 // boot: returns files restored (-1 err)
int store_flush(void);                // save ramfs -> DR1; returns file count
int store_files(void);                // files in the on-disk store
int store_migrated(void);             // 1 = filesystem moved to the store drive

#endif
