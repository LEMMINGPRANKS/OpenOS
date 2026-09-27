#include "store.h"
#include "ata.h"
#include "part.h"
#include "ramfs.h"
#include "heap.h"

// The store is Freddie's own .WMBG format (from QuantumSquish!) on DR1:
// sector STORE_LBA holds a superblock (magic + file count), the next
// STORE_TABLE_SECTORS hold one entry per file (name, lba, size), then
// every file lives on disk as its own WMBG archive. Flush rewrites the
// whole region -- simple and robust.
//
// WMBG container law (same as qsquish):
//   "WMBG" | version u8 | method u8 | size u64 LE | payload
// We write version 1, method 0 (store). Real compression comes when
// QuantumSquish grows Huffman + LZ77 and we link it in.

#define WMBG_MAGIC       "WMBG"
#define WMBG_VERSION     1
#define WMBG_METHOD_STORE 0
#define WMBG_HDR         14

#define STORE_TABLE_SECTORS 8
#define STORE_DATA_LBA    (STORE_LBA + 1 + STORE_TABLE_SECTORS)
#define STORE_DATA_SECTORS (STORE_MAX_SECTORS - 1 - STORE_TABLE_SECTORS)

struct store_sb {
    char magic[8];
    uint32_t version;
    uint32_t nfiles;
};

struct store_entry {                  // on-disk file table row
    char name[RAMFS_NAME_MAX];
    uint32_t lba;                     // first sector of the WMBG archive
    uint32_t size;                    // payload bytes (unsquished)
    uint32_t sect;                    // sectors the archive occupies
};

static int sb_ok(struct store_sb *sb)
{
    for (int i = 0; i < 8; i++)
        if (sb->magic[i] != STORE_MAGIC[i])
            return 0;
    return sb->nfiles <= RAMFS_MAX_FILES;
}

// Store-drive law: if a second disk is attached, the filesystem LIVES there
// (the boot disk's copy is just the seed). store_disk_on()/off() wrap every
// store access so nothing else notices the drive switch.
static int migrated;                    // set when we copied seed -> store drive

static void store_disk_on(void)  { ata_use_slave(ata_slave_present()); }
static void store_disk_off(void) { ata_use_slave(0); }

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

// multi-sector ATA transfers are capped at 256 sectors per command
static int ata_rw(int write, uint32_t lba, uint32_t nsect, void *buf)
{
    uint8_t *p = buf;
    while (nsect) {
        uint32_t chunk = nsect > 256 ? 256 : nsect;
        int r = write ? ata_write(lba, chunk, p) : ata_read(lba, chunk, p);
        if (r != 0)
            return -1;
        p += (size_t)chunk * 512;
        lba += chunk;
        nsect -= chunk;
    }
    return 0;
}

// read the store on one disk (1 = store drive, 0 = boot-disk seed) into
// ramfs. Returns files restored, or -1 if there is no store there.
static int load_from(int slave_disk)
{
    static uint8_t sbsec[512];
    ata_use_slave(slave_disk);
    if (ata_read(disk_lba(STORE_LBA), 1, sbsec) != 0)
        return -1;
    struct store_sb *sb = (struct store_sb *)sbsec;
    if (!sb_ok(sb))
        return -1;                      // blank disk: nothing to restore

    static uint8_t table[STORE_TABLE_SECTORS * 512];
    if (ata_rw(0, disk_lba(STORE_LBA + 1), STORE_TABLE_SECTORS, table) != 0)
        return -1;

    int count = 0;
    for (uint32_t f = 0; f < sb->nfiles; f++) {
        struct store_entry *e = (struct store_entry *)
            (table + (size_t)f * sizeof *e);
        if (!e->name[0] || e->sect == 0)
            continue;
        uint8_t *buf = kmalloc(e->sect * 512);
        if (!buf)
            break;
        if (ata_rw(0, disk_lba(e->lba), e->sect, buf) == 0 &&
            buf[0] == 'W' && buf[1] == 'M' && buf[2] == 'B' && buf[3] == 'G' &&
            buf[4] == WMBG_VERSION && buf[5] == WMBG_METHOD_STORE &&
            rd64(buf + 6) == e->size &&
            ramfs_write(e->name, (const char *)buf + WMBG_HDR, e->size) == 0)
            count++;
        kfree(buf);
    }
    return count;
}

int store_load(void)
{
    if (!ata_present() && !ata_slave_present())
        return 0;
    int n = load_from(1);               // the store drive, if attached
    if (n >= 0)
        return n;
    n = load_from(0);                   // else the boot-disk seed
    if (n < 0)
        return 0;                       // blank everywhere: fresh machine
    if (ata_slave_present() && store_flush() >= 0)
        migrated = 1;                   // first boot: filesystem moved onto
    return n;                           // the store drive, saves survive
}                                       // boot-image rebuilds from now on

int store_migrated(void) { return migrated; }

int store_files(void)
{
    if (!ata_present() && !ata_slave_present())
        return 0;
    static uint8_t sbsec[512];
    store_disk_on();
    if (ata_read(disk_lba(STORE_LBA), 1, sbsec) != 0) {
        store_disk_off();
        return 0;
    }
    struct store_sb *sb = (struct store_sb *)sbsec;
    if (!sb_ok(sb)) {
        store_disk_off();
        return 0;
    }
    int n = (int)sb->nfiles;
    store_disk_off();
    return n;
}

static int flush_locked(void)
{

    // pass 1: size the data area so we only kmalloc what we need
    uint32_t need_total = 0;
    int nfiles = 0;
    for (int i = 0; ; i++) {
        const char *nm;
        uint32_t sz;
        if (!ramfs_enum(i, &nm, &sz))
            break;
        if (nfiles >= RAMFS_MAX_FILES)
            break;
        need_total += ((WMBG_HDR + sz) + 511) & ~(uint32_t)511;
        nfiles++;
    }
    uint32_t cap = STORE_DATA_SECTORS * 512;
    if (need_total > cap)
        need_total = cap;               // store full: flush what fits

    uint8_t *buf = 0;
    if (need_total) {
        buf = kmalloc(need_total);
        if (!buf)
            return -2;                  // -2: out of heap
        for (uint32_t i = 0; i < need_total; i++)
            buf[i] = 0;
    }

    // pass 2: squish every file into the buffer, build the table
    static uint8_t table[STORE_TABLE_SECTORS * 512];
    for (uint32_t i = 0; i < sizeof table; i++)
        table[i] = 0;
    uint32_t used = 0;
    int count = 0;
    for (int i = 0; i < nfiles; i++) {
        const char *nm;
        uint32_t sz;
        if (!ramfs_enum(i, &nm, &sz))
            break;
        uint32_t need = ((WMBG_HDR + sz) + 511) & ~(uint32_t)511;
        if (used + need > need_total)
            break;                      // store full: keep what fits
        uint32_t dsz = 0;
        const char *data = ramfs_read(nm, &dsz);
        if (!data) {
            if (buf) kfree(buf);
            return -3;                  // -3: ramfs changed under us
        }
        uint8_t *arch = buf + used;     // the file's own WMBG archive
        arch[0] = 'W'; arch[1] = 'M'; arch[2] = 'B'; arch[3] = 'G';
        arch[4] = WMBG_VERSION;
        arch[5] = WMBG_METHOD_STORE;
        wr64(arch + 6, dsz);
        for (uint32_t j = 0; j < dsz; j++)
            arch[WMBG_HDR + j] = (uint8_t)data[j];

        struct store_entry *e = (struct store_entry *)
            (table + (size_t)count * sizeof *e);
        int k = 0;
        for (; nm[k] && k < RAMFS_NAME_MAX - 1; k++)
            e->name[k] = nm[k];
        e->name[k] = 0;
        e->lba = STORE_DATA_LBA + used / 512;
        e->size = dsz;
        e->sect = need / 512;

        used += need;
        count++;
    }

    // superblock, then table, then the squished data
    static uint8_t sbsec[512];
    for (int i = 0; i < 512; i++)
        sbsec[i] = 0;
    struct store_sb *sb = (struct store_sb *)sbsec;
    for (int i = 0; i < 8; i++)
        sb->magic[i] = STORE_MAGIC[i];
    sb->version = 1;
    sb->nfiles = count;
    if (ata_write(disk_lba(STORE_LBA), 1, sbsec) != 0) {
        if (buf) kfree(buf);
        return -4;                      // -4: superblock write failed
    }
    if (ata_rw(1, disk_lba(STORE_LBA + 1), STORE_TABLE_SECTORS, table) != 0) {
        if (buf) kfree(buf);
        return -5;                      // -5: table write failed
    }
    if (buf) {
        uint32_t nsect = (used + 511) / 512;
        if (nsect && ata_rw(1, disk_lba(STORE_DATA_LBA), nsect, buf) != 0) {
            kfree(buf);
            return -6;                  // -6: data write failed
        }
        kfree(buf);
    }
    return count;
}

int store_flush(void)
{
    if (!ata_present() && !ata_slave_present())
        return -1;                      // -1: no drive
    store_disk_on();
    int n = flush_locked();
    store_disk_off();
    return n;
}
