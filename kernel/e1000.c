#include "e1000.h"
#include "pci.h"
#include "timer.h"

// e1000 (82540EM, QEMU's default NIC). We use legacy descriptors and
// poll instead of using MSI/line interrupts -- simpler, and fast enough
// for getspgk downloads.

#define E1000_VENDOR 0x8086
#define E1000_DEVICE 0x100E

// register offsets (bytes into BAR0)
#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_IMC    0x00D8
#define REG_RCTL   0x0100
#define REG_RDBAL  0x2800
#define REG_RDBAH  0x2804
#define REG_RDLEN  0x2808
#define REG_RDH    0x2810
#define REG_RDT    0x2818
#define REG_TCTL   0x0400
#define REG_TDBAL  0x3800
#define REG_TDBAH  0x3804
#define REG_TDLEN  0x3808
#define REG_TDH    0x3810
#define REG_TDT    0x3818
#define REG_TIPG   0x0410
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define TX_RING 8
#define RX_RING 16

struct tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso, cmd;
    uint8_t  status, css;
    uint16_t special;
};

struct rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status, errors;
    uint16_t special;
};

static volatile uint32_t *regs;
static struct tx_desc txring[TX_RING] __attribute__((aligned(16)));
static struct rx_desc rxring[RX_RING] __attribute__((aligned(16)));
static uint8_t rxbuffers[RX_RING][E1000_MTU] __attribute__((aligned(16)));
static uint32_t tx_next;
static uint32_t rx_next;               // next descriptor to check

static uint32_t rd(uint32_t off)            { return regs[off / 4]; }
static void wr(uint32_t off, uint32_t v)    { regs[off / 4] = v; }

int e1000_up(void) { return regs != 0; }

void e1000_mac(uint8_t *mac)
{
    uint32_t lo = rd(REG_RAL0), hi = rd(REG_RAH0);
    mac[0] = lo & 0xFF; mac[1] = (lo >> 8) & 0xFF;
    mac[2] = (lo >> 16) & 0xFF; mac[3] = (lo >> 24) & 0xFF;
    mac[4] = hi & 0xFF; mac[5] = (hi >> 8) & 0xFF;
}

int e1000_init(void)
{
    uint8_t bus, slot;
    if (!pci_find(E1000_VENDOR, E1000_DEVICE, &bus, &slot))
        return -1;
    uint32_t bar = pci_bar_mem(bus, slot, 0);
    if (!bar)
        return -2;
    regs = (volatile uint32_t *)(uint64_t)bar;   // identity-mapped by boot

    wr(REG_IMC, 0xFFFFFFFF);           // no interrupts, we poll

    // transmit ring
    wr(REG_TDBAL, (uint32_t)(uint64_t)txring);
    wr(REG_TDBAH, 0);
    wr(REG_TDLEN, TX_RING * 16);
    wr(REG_TDH, 0);
    wr(REG_TDT, 0);
    wr(REG_TCTL, 0x2 | 0x8 | (0x10 << 4) | (0x40 << 12));  // EN|PSP|CT|COLD
    wr(REG_TIPG, 0x0060200A);

    // receive ring: hand all buffers to the card
    wr(REG_RDBAL, (uint32_t)(uint64_t)rxring);
    wr(REG_RDBAH, 0);
    wr(REG_RDLEN, RX_RING * 16);
    for (int i = 0; i < RX_RING; i++) {
        rxring[i].addr = (uint64_t)(uintptr_t)rxbuffers[i];
        rxring[i].status = 0;
    }
    wr(REG_RDH, 0);
    wr(REG_RDT, RX_RING - 1);
    wr(REG_RCTL, 0x2 | 0x8 | 0x10 | 0x8000);  // EN|UPE|MPE|BAM: take everything

    tx_next = 0;
    rx_next = 0;
    return 0;
}

int e1000_send(const uint8_t *frame, uint32_t len)
{
    if (!regs || len > E1000_MTU)
        return -1;
    if (len < 60) {                     // pad runts: real switches drop <60
        for (uint32_t i = len; i < 60; i++)
            ((uint8_t *)frame)[i] = 0;
        len = 60;
    }

    struct tx_desc *d = &txring[tx_next];
    d->addr = (uint64_t)(uintptr_t)frame;   // caller's buffer, sent at once
    d->length = (uint16_t)len;
    d->cmd = 0x0B;                       // EOP | IFCS | RS
    d->status = 0;
    tx_next = (tx_next + 1) % TX_RING;
    wr(REG_TDT, tx_next);               // ring runs TDH..TDT-1: bump PAST it

    uint64_t start = timer_uptime_ms();
    while (!(d->status & 0x01)) {        // wait for done
        if (timer_uptime_ms() - start > 1000)
            return -2;                   // transmit timeout
    }
    return 0;
}

int e1000_recv(uint8_t *buf, uint32_t max)
{
    if (!regs)
        return -1;
    struct rx_desc *d = &rxring[rx_next];
    if (!(d->status & 0x01))             // descriptor done?
        return 0;
    uint32_t len = d->length;
    if (len > max)
        len = max;
    if (len > E1000_MTU)
        len = E1000_MTU;
    for (uint32_t i = 0; i < len; i++)
        buf[i] = rxbuffers[rx_next][i];
    d->status = 0;                       // recycle the buffer
    wr(REG_RDT, rx_next);                // give it back to the card
    rx_next = (rx_next + 1) % RX_RING;
    return (int)len;
}
