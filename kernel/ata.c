#include "ata.h"
#include "dev.h"
#include "timer.h"

// DR1: ATA PIO driver, primary bus, polling. Real metal: ports 0x1F0-0x1F7,
// alternate status at 0x3F6. We speak LBA28 (up to 128 GiB drives).

#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_CMD         0x1F7
#define ATA_ALT_STATUS  0x3F6
#define ATA_CONTROL     0x3F6

#define ATA_DRV_MASTER  0xE0          // master + LBA mode bits
#define ST_ERR          0x01
#define ST_DRQ          0x08
#define ST_DF           0x20
#define ST_BSY          0x80

#define CMD_IDENTIFY    0xEC
#define CMD_READ        0x20
#define CMD_WRITE       0x30
#define CMD_FLUSH       0xE7

#define ATA_MAX_SECTORS_PER_CMD 256   // 0 in the count register means 256

static int present;
static char model[41];
static uint64_t sectors28;
static uint8_t last_status;            // last alternate-status byte seen
static uint8_t first_status;           // first status seen after last cmd
static uint8_t fail_where;             // 1 drq 2 post-data busy 3 flush busy

uint8_t ata_dbg_status(void) { return last_status; }
uint8_t ata_dbg_first_status(void) { return first_status; }
uint8_t ata_dbg_where(void) { return fail_where; }

static void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(val), "d"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(v) : "d"(port));
    return v;
}

// NOTE: rep string ops walk RDI/RSI and RCX themselves -- they MUST be
// read-write operands or gcc assumes the pointers are unchanged.
static void insw(uint16_t port, void *buf, uint32_t words)
{
    __asm__ volatile ("rep insw"
                      : "+D"(buf), "+c"(words)
                      : "d"(port)
                      : "memory");
}

static void outsw(uint16_t port, const void *buf, uint32_t words)
{
    __asm__ volatile ("rep outsw"
                      : "+S"(buf), "+c"(words)
                      : "d"(port)
                      : "memory");
}

// the spec's 400 ns wait: four reads of the alternate status register
static void wait_400ns(void)
{
    for (int i = 0; i < 4; i++)
        (void)inb(ATA_ALT_STATUS);
}

// Poll counts are a lie on emulators: the host's fsync (FLUSH CACHE) can
// take far longer than a fixed spin count. Waits are time-based instead.
#define WAIT_DRQ_MS     1000
#define WAIT_BUSY_MS    2000
#define WAIT_FLUSH_MS   10000

static int wait_not_busy_ms(uint32_t timeout_ms)
{
    uint64_t deadline = timer_uptime_ms() + timeout_ms;
    for (;;) {
        uint8_t st = inb(ATA_ALT_STATUS);
        last_status = st;
        if (!(st & ST_BSY))
            return (st & (ST_ERR | ST_DF)) ? -1 : 0;
        if (timer_uptime_ms() >= deadline)
            return -1;
    }
}

static int wait_drq(void)
{
    uint64_t deadline = timer_uptime_ms() + WAIT_DRQ_MS;
    for (int i = 0; ; i++) {
        uint8_t st = inb(ATA_ALT_STATUS);
        if (i == 0)
            first_status = st;
        last_status = st;
        if (st & ST_DRQ)
            return 0;                   // data ready -- even if BSY set
        if (st & (ST_ERR | ST_DF))
            return -1;
        if (timer_uptime_ms() >= deadline)
            return -1;
    }
}

static void select_master(uint8_t lba_hi_bits)
{
    outb(ATA_DRIVE, (uint8_t)(ATA_DRV_MASTER | lba_hi_bits));
}

// Polling law: keep the bus IRQ line off (nIEN) and read the main status
// register once to clear any interrupt left over from the last command --
// else the first write after an idle gap gets aborted.
static void ata_begin_cmd(void)
{
    outb(ATA_CONTROL, 0x02);           // nIEN: no IRQ14 from this bus
    (void)inb(ATA_STATUS);             // clear INTRQ latch
    wait_not_busy_ms(100);
}

int ata_init(void)
{
    present = 0;
    model[0] = 0;
    sectors28 = 0;

    select_master(0);
    wait_400ns();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, CMD_IDENTIFY);
    wait_400ns();

    if (inb(ATA_STATUS) == 0)         // floating bus: nothing there
        return 0;
    if (wait_drq() != 0)
        return 0;

    uint16_t id[256];
    insw(ATA_DATA, id, 256);

    // words 60-61: LBA28 sector count
    sectors28 = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    // words 27-46: model string, big-endian pairs (first char = high byte)
    for (int w = 0; w < 20; w++) {
        model[w * 2]     = (char)(id[27 + w] >> 8);
        model[w * 2 + 1] = (char)(id[27 + w] & 0xFF);
    }
    model[40] = 0;

    present = 1;
    dev_set_present("DR1", 1);
    return 1;
}

int ata_present(void)   { return present; }
const char *ata_model(void) { return model; }
uint64_t ata_sectors(void)  { return sectors28; }

int ata_read(uint32_t lba, uint32_t nsect, void *buf)
{
    if (!present || nsect == 0 || nsect > ATA_MAX_SECTORS_PER_CMD)
        return -1;
    uint8_t count8 = nsect == 256 ? 0 : (uint8_t)nsect;
    select_master((uint8_t)((lba >> 24) & 0x0F));
    ata_begin_cmd();
    outb(ATA_SECCOUNT, count8);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_READ);
    uint16_t *p = (uint16_t *)buf;
    for (uint32_t s = 0; s < nsect; s++) {
        if (wait_drq() != 0)
            return -1;
        insw(ATA_DATA, p, 256);
        p += 256;
    }
    return wait_not_busy_ms(WAIT_BUSY_MS);
}

int ata_write(uint32_t lba, uint32_t nsect, const void *buf)
{
    if (!present || nsect == 0 || nsect > ATA_MAX_SECTORS_PER_CMD)
        return -1;
    uint8_t count8 = nsect == 256 ? 0 : (uint8_t)nsect;
    select_master((uint8_t)((lba >> 24) & 0x0F));
    ata_begin_cmd();
    outb(ATA_SECCOUNT, count8);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_WRITE);
    const uint16_t *p = (const uint16_t *)buf;
    for (uint32_t s = 0; s < nsect; s++) {
        if (wait_drq() != 0) {
            fail_where = 1;
            return -1;
        }
        outsw(ATA_DATA, p, 256);
        p += 256;
    }
    if (wait_not_busy_ms(WAIT_BUSY_MS) != 0) {
        fail_where = 2;
        return -1;
    }
    outb(ATA_CMD, CMD_FLUSH);
    if (wait_not_busy_ms(WAIT_FLUSH_MS) != 0) {
        fail_where = 3;
        return -1;
    }
    return 0;
}
