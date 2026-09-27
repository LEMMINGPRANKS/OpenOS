#include "rtl8169.h"
#include "nic.h"
#include "pci.h"
#include "portio.h"
#include "timer.h"

// RTL8169 family: 16-byte descriptors, OWN bit hands each one between us
// and the card. Registers are reached through the IO BAR (BAR0) on every
// chip in the family, which also dodges 64-bit memory BARs above 4 GiB.

#define REALTEK 0x10EC

#define R_IDR0    0x00                  // MAC, 6 bytes
#define R_MAR0    0x08                  // multicast filter, 8 bytes
#define R_TNPDS   0x20                  // tx normal-priority ring, 64-bit
#define R_CR      0x37
#define R_TPPOLL  0x38
#define R_IMR     0x3C
#define R_ISR     0x3E
#define R_TCR     0x40
#define R_RCR     0x44
#define R_9346CR  0x50                  // config register lock
#define R_RMS     0xDA                  // max rx packet size
#define R_CPCR    0xE0                  // C+ command
#define R_RDSAR   0xE4                  // rx ring, 64-bit
#define R_MTPS    0xEC                  // max tx packet size, 128-byte units

#define CR_TE     0x04
#define CR_RE     0x08
#define CR_RST    0x10
#define TPPOLL_NPQ 0x40                 // "normal queue has work"
#define CFG_UNLOCK 0xC0
#define CFG_LOCK   0x00

#define RCR_BITS  0x0000E70Eu           // no rx threshold, max DMA, APM|AM|AB
#define TCR_BITS  0x03000700u           // standard IFG, max DMA burst
#define CPCR_RXVLAN_CHKSUM_OFF 0x0000   // plain frames, no offloads

#define D_OWN     (1u << 31)
#define D_EOR     (1u << 30)            // end of ring
#define D_FS      (1u << 29)            // first segment
#define D_LS      (1u << 28)            // last segment
#define D_LEN     0x3FFFu

#define TX_RING 4
#define RX_RING 16
#define RX_BUF  2048
#define RESET_WAIT_MS 100
#define TX_WAIT_MS    1000
#define MTPS_UNITS    0x3B              // ~7.5 KiB, the documented default

struct desc {
    uint32_t opts1, opts2;
    uint64_t addr;
};

static const struct pci_id ids[] = {
    { REALTEK, 0x8161, "Realtek RTL8168 (8161)" },
    { REALTEK, 0x8167, "Realtek RTL8169SC" },
    { REALTEK, 0x8168, "Realtek RTL8168/8111" },
    { REALTEK, 0x8169, "Realtek RTL8169" },
    { REALTEK, 0x8136, "Realtek RTL8101/8102/8103" },
    { 0x1186,  0x4300, "D-Link DGE-528T (RTL8169)" },
    { 0x1259,  0xC107, "Allied Telesyn (RTL8169)" },
    { 0x16EC,  0x0116, "USRobotics (RTL8169)" },
    { 0x1737,  0x1032, "Linksys EG1032 (RTL8169)" },
    { 0, 0, 0 }
};

static uint16_t io;
static uint8_t  mac[6];
static const char *model = "Realtek RTL8169";
// descriptor rings must be 256-byte aligned
static volatile struct desc txring[TX_RING] __attribute__((aligned(256)));
static volatile struct desc rxring[RX_RING] __attribute__((aligned(256)));
static uint8_t txbuf[NIC_MTU] __attribute__((aligned(16)));
static uint8_t rxbuf[RX_RING][RX_BUF] __attribute__((aligned(16)));
static uint32_t tx_next, rx_next;

const char *rtl8169_model(void) { return model; }

void rtl8169_mac(uint8_t *out)
{
    for (int i = 0; i < 6; i++)
        out[i] = mac[i];
}

static void rx_give(uint32_t i)         // hand descriptor i back to the card
{
    rxring[i].opts2 = 0;
    rxring[i].addr = (uint64_t)(uintptr_t)rxbuf[i];
    rxring[i].opts1 = D_OWN | (i == RX_RING - 1 ? D_EOR : 0) | RX_BUF;
}

int rtl8169_init(void)
{
    uint8_t bus, slot;
    const struct pci_id *id = pci_find_table(ids, &bus, &slot);
    if (!id)
        return -1;
    io = pci_bar_io(bus, slot, 0);
    if (!io)
        return -2;
    model = id->name;

    outb(io + R_CR, CR_RST);
    uint64_t start = timer_uptime_ms();
    while (inb(io + R_CR) & CR_RST)
        if (timer_uptime_ms() - start > RESET_WAIT_MS) {
            io = 0;
            return -3;
        }
    for (int i = 0; i < 6; i++)
        mac[i] = inb((uint16_t)(io + R_IDR0 + i));

    for (uint32_t i = 0; i < TX_RING; i++) {
        txring[i].opts1 = (i == TX_RING - 1) ? D_EOR : 0;
        txring[i].opts2 = 0;
        txring[i].addr = 0;
    }
    for (uint32_t i = 0; i < RX_RING; i++)
        rx_give(i);

    outb(io + R_9346CR, CFG_UNLOCK);
    outw(io + R_IMR, 0);                // we poll
    outw(io + R_ISR, 0xFFFF);
    outw(io + R_CPCR, CPCR_RXVLAN_CHKSUM_OFF);
    outw(io + R_RMS, RX_BUF);
    outb(io + R_MTPS, MTPS_UNITS);
    outl(io + R_TNPDS, (uint32_t)(uint64_t)txring);
    outl(io + R_TNPDS + 4, 0);
    outl(io + R_RDSAR, (uint32_t)(uint64_t)rxring);
    outl(io + R_RDSAR + 4, 0);
    for (int i = 0; i < 8; i++)         // accept every multicast group
        outb((uint16_t)(io + R_MAR0 + i), 0xFF);
    outl(io + R_RCR, RCR_BITS);
    outl(io + R_TCR, TCR_BITS);
    outb(io + R_CR, CR_RE | CR_TE);
    // newer 8168 revisions only take RCR/TCR once rx/tx are enabled
    outl(io + R_RCR, RCR_BITS);
    outl(io + R_TCR, TCR_BITS);
    outb(io + R_9346CR, CFG_LOCK);

    tx_next = 0;
    rx_next = 0;
    return 0;
}

int rtl8169_send(const uint8_t *frame, uint32_t len)
{
    if (!io || len > NIC_MTU)
        return -1;
    for (uint32_t i = 0; i < len; i++)
        txbuf[i] = frame[i];
    volatile struct desc *d = &txring[tx_next];
    d->opts2 = 0;
    d->addr = (uint64_t)(uintptr_t)txbuf;
    d->opts1 = D_OWN | D_FS | D_LS | (tx_next == TX_RING - 1 ? D_EOR : 0) |
               (len & D_LEN);
    tx_next = (tx_next + 1) % TX_RING;
    outb(io + R_TPPOLL, TPPOLL_NPQ);

    uint64_t start = timer_uptime_ms();
    while (d->opts1 & D_OWN)            // card clears OWN once it's sent
        if (timer_uptime_ms() - start > TX_WAIT_MS)
            return -2;
    return 0;
}

int rtl8169_recv(uint8_t *buf, uint32_t max)
{
    if (!io)
        return -1;
    volatile struct desc *d = &rxring[rx_next];
    uint32_t o = d->opts1;
    if (o & D_OWN)
        return 0;                       // still the card's: nothing new
    uint32_t len = 0;
    if ((o & D_FS) && (o & D_LS)) {     // whole frame in one buffer
        len = o & D_LEN;
        len = len > 4 ? len - 4 : 0;    // drop the CRC
        if (len > max) len = max;
        if (len > RX_BUF) len = RX_BUF;
        for (uint32_t i = 0; i < len; i++)
            buf[i] = rxbuf[rx_next][i];
    }
    rx_give(rx_next);
    rx_next = (rx_next + 1) % RX_RING;
    return (int)len;
}
