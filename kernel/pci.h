#ifndef OPENOS_PCI_H
#define OPENOS_PCI_H

#include <stdint.h>

// Minimal PCI configuration-space access: port 0xCF8/0xCFC.

struct pci_id {
    uint16_t vendor, device;
    const char *name;                   // shown by netinfo
};

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);
uint16_t pci_vendor(uint8_t bus, uint8_t slot);
uint16_t pci_device(uint8_t bus, uint8_t slot);
int pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot);
// first device matching any entry of a {0,0,0}-terminated table;
// returns the matching entry or 0
const struct pci_id *pci_find_table(const struct pci_id *ids,
                                    uint8_t *bus, uint8_t *slot);
void pci_enable(uint8_t bus, uint8_t slot);   // IO + memory decode + bus master
uint32_t pci_bar_mem(uint8_t bus, uint8_t slot, int barnum);  // physical addr
uint16_t pci_bar_io(uint8_t bus, uint8_t slot, int barnum);   // IO port base

#endif
