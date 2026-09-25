#ifndef OPENOS_PCI_H
#define OPENOS_PCI_H

#include <stdint.h>

// Minimal PCI configuration-space access: port 0xCF8/0xCFC.

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t v);
uint16_t pci_vendor(uint8_t bus, uint8_t slot);
uint16_t pci_device(uint8_t bus, uint8_t slot);
int pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot);
uint32_t pci_bar_mem(uint8_t bus, uint8_t slot, int barnum);  // physical addr

#endif
