#include "pci.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

static void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %%eax, %%dx" :: "a"(val), "d"(port));
}

static uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %%dx, %%eax" : "=a"(v) : "d"(port));
    return v;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t addr = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC) | 0x80000000u;
    outl(PCI_ADDR, addr);
    return inl(PCI_DATA);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v)
{
    uint32_t addr = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC) | 0x80000000u;
    outl(PCI_ADDR, addr);
    outl(PCI_DATA, v);
}

uint16_t pci_vendor(uint8_t bus, uint8_t slot)
{
    return (uint16_t)(pci_read32(bus, slot, 0, 0) & 0xFFFF);
}

uint16_t pci_device(uint8_t bus, uint8_t slot)
{
    return (uint16_t)(pci_read32(bus, slot, 0, 0) >> 16);
}

int pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot)
{
    for (uint32_t b = 0; b < 256; b++)
        for (uint32_t s = 0; s < 32; s++) {
            if (pci_vendor((uint8_t)b, (uint8_t)s) == 0xFFFF)
                continue;
            if (pci_vendor((uint8_t)b, (uint8_t)s) == vendor &&
                pci_device((uint8_t)b, (uint8_t)s) == device) {
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                return 1;
            }
        }
    return 0;
}

const struct pci_id *pci_find_table(const struct pci_id *ids,
                                    uint8_t *bus, uint8_t *slot)
{
    for (uint32_t b = 0; b < 256; b++)
        for (uint32_t s = 0; s < 32; s++) {
            uint16_t v = pci_vendor((uint8_t)b, (uint8_t)s);
            if (v == 0xFFFF)
                continue;
            uint16_t d = pci_device((uint8_t)b, (uint8_t)s);
            for (const struct pci_id *id = ids; id->vendor; id++)
                if (id->vendor == v && id->device == d) {
                    *bus = (uint8_t)b;
                    *slot = (uint8_t)s;
                    return id;
                }
        }
    return 0;
}

void pci_enable(uint8_t bus, uint8_t slot)
{
    // decode on + bus master (DMA). SeaBIOS usually sets decode already;
    // real firmware often leaves bus mastering off for NICs it didn't use.
    uint32_t cmd = pci_read32(bus, slot, 0, 4);
    pci_write32(bus, slot, 0, 4, cmd | 0x7);
}

uint32_t pci_bar_mem(uint8_t bus, uint8_t slot, int barnum)
{
    pci_enable(bus, slot);
    uint8_t off = (uint8_t)(0x10 + barnum * 4);
    uint32_t raw = pci_read32(bus, slot, 0, off);
    if (raw & 1)
        return 0;                       // IO BAR, not memory
    if ((raw & 0x6) == 0x4 && off + 4 <= 0x24 &&
        pci_read32(bus, slot, 0, (uint8_t)(off + 4)) != 0)
        return 0;                       // 64-bit BAR above 4 GiB: not mapped
    return raw & 0xFFFFFFF0;            // below 4 GiB: identity-mapped
}

uint16_t pci_bar_io(uint8_t bus, uint8_t slot, int barnum)
{
    pci_enable(bus, slot);
    uint32_t raw = pci_read32(bus, slot, 0, (uint8_t)(0x10 + barnum * 4));
    if (!(raw & 1))
        return 0;                       // memory BAR, not IO
    return (uint16_t)(raw & 0xFFFC);
}
