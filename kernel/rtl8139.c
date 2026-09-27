#include "rtl8139.h"
#include "nic.h"
#include "pci.h"
#include "portio.h"
#include "timer.h"

// The RTL8139 receives into one contiguous ring buffer (each packet is a
// 4-byte header + data, dword aligned) and transmits from four fixed
// slots, each with its own address + status register.

#define REALTEK 0x10EC

#define R_IDR0    0x00                  // MAC, 6 bytes
#define R_MAR0    0x08                  // multicast filter, 8 bytes
#define R_TSD0    0x10                  // tx status, 4 x dword
#define R_TSAD0   0x20                  // tx address, 4 x dword
#define R_RBSTART 0x30
#define R_CR      0x37
#define R_CAPR    0x38                  // current address of packet read
#define R_IMR     0x3C
#define R_ISR     0x3E
#define R_TCR     0x40
#define R_RCR     0x44
#define R_CONFIG1 0x52

#define CR_BUFE   0x01                  // rx buffer empty
#define CR_TE     0x04
#define CR_RE     0x08
#define CR_RST    0x10

#define RCR_APM   (1u << 1)             // our MAC
#define RCR_AM    (1u << 2)             // multicast
#define RCR_AB    (1u << 3)             // broadcast
#define RCR_WRAP  (1u << 7)             // packets may run past the ring end
#define RCR_MXDMA (7u << 8)             // unlimited DMA burst
// RCR.RBLEN = 00 -> 8 KiB ring

#define TSD_OWN   (1u << 13)            // DMA to the card finished
#define TSD_TOK   (1u << 15)            // transmit OK
#define TSD_ABORT (1u << 30)

#define RX_RING_LEN 8192
#define RX_PAD      (16 + NIC_MTU)      // WRAP lets a packet overrun the end
#define RX_OK       0x0001              // packet header status bit
#define TX_SLOTS    4
#define RESET_WAIT_MS 100
#define TX_WAIT_MS    1000

static const struct pci_id ids[] = {
    { REALTEK, 0x8139, "Realtek RTL8139" },
    { 0x1113,  0x1211, "Accton EN5251 (RTL8139)" },
    { 0x1186,  0x1300, "D-Link DFE-538TX (RTL8139)" },
    { 0, 0, 0 }
};

static uint16_t io;
static uint8_t  mac[6];
static const char *model = "Realtek RTL8139";
static uint8_t  rxring[RX_RING_LEN + RX_PAD] __attribute__((aligned(16)));
static uint8_t  txbuf[TX_SLOTS][NIC_MTU] __attribute__((aligned(16)));
static uint32_t rx_off;                 // read offset into rxring
static uint32_t tx_slot;

const char *rtl8139_model(void) { return model; }

void rtl8139_mac(uint8_t *out)
{
    for (int i = 0; i < 6; i++)
        out[i] = mac[i];
}

int rtl8139_init(void)
{
    uint8_t bus, slot;
    const struct pci_id *id = pci_find_table(ids, &bus, &slot);
    if (!id)
        return -1;
    io = pci_bar_io(bus, slot, 0);
    if (!io)
        return -2;
    model = id->name;

    outb(io + R_CONFIG1, 0x00);         // power on (LWAKE + LWPTN low)
    outb(io + R_CR, CR_RST);
    uint64_t start = timer_uptime_ms();
    while (inb(io + R_CR) & CR_RST)
        if (timer_uptime_ms() - start > RESET_WAIT_MS) {
            io = 0;
            return -3;                  // card never came out of reset
        }

    for (int i = 0; i < 6; i++)
        mac[i] = inb((uint16_t)(io + R_IDR0 + i));

    outl(io + R_RBSTART, (uint32_t)(uint64_t)rxring);
    outw(io + R_IMR, 0);                // we poll
    outw(io + R_ISR, 0xFFFF);
    for (int i = 0; i < 8; i++)         // accept every multicast group
        outb((uint16_t)(io + R_MAR0 + i), 0xFF);
    outb(io + R_CR, CR_RE | CR_TE);
    outl(io + R_RCR, RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_MXDMA);
    outl(io + R_TCR, 0x03000700);       // standard IFG, max DMA burst
    rx_off = 0;
    tx_slot = 0;
    return 0;
}

int rtl8139_send(const uint8_t *frame, uint32_t len)
{
    if (!io || len > NIC_MTU)
        return -1;
    uint8_t *b = txbuf[tx_slot];        // the card wants dword-aligned data
    for (uint32_t i = 0; i < len; i++)
        b[i] = frame[i];
    uint16_t tsd = (uint16_t)(io + R_TSD0 + tx_slot * 4);
    outl((uint16_t)(io + R_TSAD0 + tx_slot * 4), (uint32_t)(uint64_t)b);
    outl(tsd, len);                     // size + clears OWN: go
    tx_slot = (tx_slot + 1) % TX_SLOTS;

    uint64_t start = timer_uptime_ms();
    for (;;) {
        uint32_t s = inl(tsd);
        if (s & TSD_TOK)
            return 0;
        if (s & TSD_ABORT)
            return -2;
        if (timer_uptime_ms() - start > TX_WAIT_MS)
            return -2;
    }
}

int rtl8139_recv(uint8_t *buf, uint32_t max)
{
    if (!io)
        return -1;
    if (inb(io + R_CR) & CR_BUFE)
        return 0;
    uint8_t *p = rxring + rx_off;
    uint16_t status = (uint16_t)(p[0] | p[1] << 8);
    uint32_t plen = (uint32_t)(p[2] | p[3] << 8);   // includes 4-byte CRC
    if (!(status & RX_OK) || plen < 4 || plen > NIC_MTU + 4) {
        // bad packet: the ring is out of step -- restart the receiver
        outb(io + R_CR, CR_TE);
        outb(io + R_CR, CR_RE | CR_TE);
        outl(io + R_RCR, RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_MXDMA);
        rx_off = 0;
        outw(io + R_CAPR, (uint16_t)(rx_off - 16));
        return 0;
    }
    uint32_t len = plen - 4;
    if (len > max)
        len = max;
    for (uint32_t i = 0; i < len; i++)
        buf[i] = p[4 + i];

    rx_off = (rx_off + plen + 4 + 3) & ~3u;
    rx_off %= RX_RING_LEN;
    outw(io + R_CAPR, (uint16_t)(rx_off - 16));   // the card's odd bias
    outw(io + R_ISR, 0x0001);            // ack ROK
    return (int)len;
}
