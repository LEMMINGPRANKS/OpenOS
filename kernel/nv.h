#ifndef OPENOS_NV_H
#define OPENOS_NV_H

#include <stdint.h>

// The NVIDIA detective (part 3 of the 1.6 arc). Read-only on purpose:
// finds any NVIDIA display card on PCI, names it, decodes its BARs, and
// reads BOOT0 for the chip family when BAR0 is reachable under our 4 GiB
// identity map -- the same trick as coldrivers on Linux. Deep init
// (nv50-tables mode-set, push buffers, RTX GSP) climbs on top later.

int  nv_probe(void);            // scan PCI; 1 = NVIDIA card found
int  nv_found(void);
const char *nv_ident(void);     // "GeForce 320M [MCP89]" style
uint32_t nv_family(void);       // BOOT0 family, 0xFFFFFFFF if unread
int  nv_family_known(void);
uint32_t nv_bar0(void);         // MMIO regs base (0 = above 4 GiB)
uint32_t nv_bar_size(int bar);  // decoded size of BAR0/BAR1 (0 = absent)

#endif
