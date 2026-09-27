#include "part.h"
#include "ata.h"

// MBR partition law (bios/layout.inc + tools/mkdisk.py): OpenOS lives in
// the partition with type byte 0x7F; every kernel-side LBA is relative to
// that partition's start. The boot disk (master) and the store drive
// (slave) each get their base resolved once; disks without our partition
// (old images, plain test disks) use base 0 so absolute-LBA layouts keep
// working.

#define PART_TABLE_OFF 446
#define PART_ENTRIES   4
#define PART_TYPE_OPENOS 0x7F
#define NO_BASE        0xFFFFFFFFu

static uint32_t base_master = NO_BASE;
static uint32_t base_slave = NO_BASE;

static uint32_t scan_disk(int slave)
{
    uint8_t mbr[512];
    int was_slave = ata_slave_selected();
    ata_use_slave(slave);
    int ok = slave ? ata_slave_present() : ata_present();
    if (!ok) {
        ata_use_slave(was_slave);
        return 0;
    }
    if (ata_read(0, 1, mbr) != 0 || mbr[510] != 0x55 || mbr[511] != 0xAA) {
        ata_use_slave(was_slave);
        return 0;
    }
    uint32_t base = 0;
    for (int i = 0; i < PART_ENTRIES; i++) {
        uint8_t type = mbr[PART_TABLE_OFF + i * 16 + 4];
        if (type == PART_TYPE_OPENOS) {
            const uint8_t *p = mbr + PART_TABLE_OFF + i * 16 + 8;
            base = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            break;
        }
    }
    ata_use_slave(was_slave);
    return base;
}

void part_scan(void)
{
    base_master = scan_disk(0);
    base_slave = scan_disk(1);
}

uint32_t part_base(void)
{
    if (base_master == NO_BASE && base_slave == NO_BASE)
        part_scan();
    return ata_slave_selected() ? base_slave : base_master;
}

uint32_t disk_lba(uint32_t rel)
{
    return part_base() + rel;
}
