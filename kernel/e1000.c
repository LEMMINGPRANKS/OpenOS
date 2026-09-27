#include "e1000.h"
#include "e1000_ids.h"
#include "nic.h"
#include "pci.h"
#include "timer.h"

// e1000 family (82540EM is QEMU's default NIC). We use legacy descriptors
// and poll instead of using MSI/line interrupts -- simpler, and fast
// enough for getspgk downloads. Legacy descriptors work across the whole
// family, e1000e parts included.

// register offsets (bytes into BAR0)
#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EERD   0x0014
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
#define REG_MTA    0x5200               // multicast table, 128 dwords
#define REG_RAL0   0x5400
#define REG_RAH0   0x5404

#define CTRL_ASDE  (1u << 5)            // auto speed detect
#define CTRL_SLU   (1u << 6)            // set link up
#define CTRL_RST   (1u << 26)
#define RAH_AV     (1u << 31)           // receive address valid

#define RCTL_EN    (1u << 1)
#define RCTL_UPE   (1u << 3)            // unicast promiscuous
#define RCTL_MPE   (1u << 4)            // multicast promiscuous
#define RCTL_BAM   (1u << 15)           // accept broadcast
#define RCTL_SECRC (1u << 26)           // strip the ethernet CRC
// RCTL.BSIZE left at 00 = 2048-byte buffers, matching RX_BUF

#define TX_RING 8
#define RX_RING 16
#define RX_BUF  2048                    // what RCTL.BSIZE=00 tells the card

#define RESET_WAIT_MS 20
#define EERD_WAIT_MS  10
#define TX_WAIT_MS    1000

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
static volatile struct tx_desc txring[TX_RING] __attribute__((aligned(16)));
static volatile struct rx_desc rxring[RX_RING] __attribute__((aligned(16)));
static uint8_t rxbuffers[RX_RING][RX_BUF] __attribute__((aligned(16)));
static uint32_t tx_next;
static uint32_t rx_next;               // next descriptor to check
static uint8_t mac[6];
static const char *model = "Intel e1000";

static uint32_t rd(uint32_t off)            { return regs[off / 4]; }
static void wr(uint32_t off, uint32_t v)    { regs[off / 4] = v; }

static void wait_ms(uint32_t ms)
{
    uint64_t start = timer_uptime_ms();
    while (timer_uptime_ms() - start < ms)
        __asm__ volatile ("pause");
}

const char *e1000_model(void) { return model; }

void e1000_mac(uint8_t *out)
{
    for (int i = 0; i < 6; i++)
        out[i] = mac[i];
}

// one 16-bit word from the EEPROM via EERD; -1 when it never finishes
static int eeprom_word(int kind, uint8_t addr)
{
    uint32_t cmd, done;
    if (kind == EE_SHORT) { cmd = 1u | ((uint32_t)addr << 8); done = 1u << 4; }
    else                  { cmd = 1u | ((uint32_t)addr << 2); done = 1u << 1; }
    wr(REG_EERD, cmd);
    uint64_t start = timer_uptime_ms();
    uint32_t v;
    while (!((v = rd(REG_EERD)) & done))
        if (timer_uptime_ms() - start > EERD_WAIT_MS)
            return -1;
    return (int)(v >> 16);
}

// the MAC: the card loads RAL0/RAH0 from the EEPROM at reset; when it
// hasn't (AV clear), read the EEPROM ourselves and program RAL0/RAH0
static int load_mac(int quirks)
{
    uint32_t hi = rd(REG_RAH0);
    if (hi & RAH_AV) {
        uint32_t lo = rd(REG_RAL0);
        mac[0] = lo & 0xFF; mac[1] = (lo >> 8) & 0xFF;
        mac[2] = (lo >> 16) & 0xFF; mac[3] = (lo >> 24) & 0xFF;
        mac[4] = hi & 0xFF; mac[5] = (hi >> 8) & 0xFF;
        return 0;
    }
    int kind = quirks & 0x3;
    if (kind == EE_NONE)
        return -1;
    for (uint8_t w = 0; w < 3; w++) {
        int v = eeprom_word(kind, w);
        if (v < 0)
            return -1;
        mac[w * 2] = (uint8_t)(v & 0xFF);
        mac[w * 2 + 1] = (uint8_t)(v >> 8);
    }
    wr(REG_RAL0, (uint32_t)mac[0] | (uint32_t)mac[1] << 8 |
                 (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24);
    wr(REG_RAH0, (uint32_t)mac[4] | (uint32_t)mac[5] << 8 | RAH_AV);
    return 0;
}

int e1000_init(void)
{
    uint8_t bus, slot;
    const struct pci_id *id = pci_find_table(e1000_ids, &bus, &slot);
    if (!id)
        return -1;
    uint32_t bar = pci_bar_mem(bus, slot, 0);
    if (!bar)
        return -2;
    regs = (volatile uint32_t *)(uint64_t)bar;   // identity-mapped by boot
    model = id->name;
    int quirks = e1000_quirks(id->device);

    wr(REG_IMC, 0xFFFFFFFF);           // no interrupts, we poll
    if (!(quirks & E_PCH)) {
        wr(REG_CTRL, rd(REG_CTRL) | CTRL_RST);   // clean slate: firmware
        wait_ms(RESET_WAIT_MS);                  // may have left it running
        wr(REG_IMC, 0xFFFFFFFF);       // reset re-enables nothing, but be sure
    }
    wr(REG_CTRL, rd(REG_CTRL) | CTRL_SLU | CTRL_ASDE);
    if (load_mac(quirks) != 0) {
        regs = 0;
        return -3;
    }
    for (int i = 0; i < 128; i++)
        wr(REG_MTA + i * 4, 0);

    // transmit ring
    for (int i = 0; i < TX_RING; i++) {
        txring[i].addr = 0;
        txring[i].cmd = 0;
        txring[i].status = 0;
    }
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
    wr(REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_BAM | RCTL_SECRC);

    tx_next = 0;
    rx_next = 0;
    return 0;
}

int e1000_send(const uint8_t *frame, uint32_t len)
{
    if (!regs || len > NIC_MTU)
        return -1;
    volatile struct tx_desc *d = &txring[tx_next];
    d->addr = (uint64_t)(uintptr_t)frame;   // caller's buffer, sent at once
    d->length = (uint16_t)len;
    d->cmd = 0x0B;                       // EOP | IFCS | RS
    d->status = 0;
    tx_next = (tx_next + 1) % TX_RING;
    wr(REG_TDT, tx_next);               // ring runs TDH..TDT-1: bump PAST it

    uint64_t start = timer_uptime_ms();
    while (!(d->status & 0x01)) {        // wait for done
        if (timer_uptime_ms() - start > TX_WAIT_MS)
            return -2;                   // transmit timeout
    }
    return 0;
}

int e1000_recv(uint8_t *buf, uint32_t max)
{
    if (!regs)
        return -1;
    volatile struct rx_desc *d = &rxring[rx_next];
    if (!(d->status & 0x01))             // descriptor done?
        return 0;
    uint32_t len = d->length;
    if (len > max)
        len = max;
    if (len > RX_BUF)
        len = RX_BUF;
    for (uint32_t i = 0; i < len; i++)
        buf[i] = rxbuffers[rx_next][i];
    d->status = 0;                       // recycle the buffer
    wr(REG_RDT, rx_next);                // give it back to the card
    rx_next = (rx_next + 1) % RX_RING;
    return (int)len;
}
