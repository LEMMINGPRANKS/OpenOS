#ifndef OPENOS_E1000_IDS_H
#define OPENOS_E1000_IDS_H

#include "pci.h"

// Intel device IDs the e1000 driver drives, and how to treat each one.
// Split out of e1000.c: it's a table, not logic.

#define INTEL 0x8086

// how to read the MAC out of the EEPROM when RAL/RAH come up empty
#define EE_NONE  0                      // no EERD register (82543/82544)
#define EE_SHORT 1                      // 8254x: addr << 8, done = bit 4
#define EE_LONG  2                      // 82541/7, 8257x+: addr << 2, done = bit 1

// PCH on-board parts (82577+, I217/I218/I219): the MAC is tied to the
// chipset's PHY, and a full reset without PHY/ULP handling can leave the
// link dead, so keep the firmware's setup and only (re)enable the rings.
#define E_PCH    0x10

static const struct pci_id e1000_ids[] = {
    // classic 8254x
    { INTEL, 0x1004, "Intel 82543GC" },
    { INTEL, 0x1008, "Intel 82544EI" },
    { INTEL, 0x100C, "Intel 82544GC" },
    { INTEL, 0x100D, "Intel 82544GC" },
    { INTEL, 0x100E, "Intel 82540EM" },  // QEMU -nic model=e1000, VirtualBox
    { INTEL, 0x1015, "Intel 82540EM" },
    { INTEL, 0x1016, "Intel 82540EP" },
    { INTEL, 0x1017, "Intel 82540EP" },
    { INTEL, 0x101E, "Intel 82540EP" },
    { INTEL, 0x100F, "Intel 82545EM" },  // VMware e1000
    { INTEL, 0x1026, "Intel 82545GM" },
    { INTEL, 0x1010, "Intel 82546EB" },
    { INTEL, 0x1079, "Intel 82546GB" },
    { INTEL, 0x1013, "Intel 82541EI" },
    { INTEL, 0x1076, "Intel 82541GI" },
    { INTEL, 0x1078, "Intel 82541ER" },
    { INTEL, 0x107C, "Intel 82541PI" },
    { INTEL, 0x1019, "Intel 82547EI" },
    { INTEL, 0x1075, "Intel 82547GI" },
    // e1000e 8257x/8258x
    { INTEL, 0x105E, "Intel 82571EB" },
    { INTEL, 0x107D, "Intel 82572EI" },
    { INTEL, 0x10B9, "Intel 82572EI" },
    { INTEL, 0x108C, "Intel 82573E" },
    { INTEL, 0x109A, "Intel 82573L" },
    { INTEL, 0x10D3, "Intel 82574L" },   // QEMU -nic model=e1000e
    { INTEL, 0x10F6, "Intel 82574L" },
    { INTEL, 0x150C, "Intel 82583V" },
    // PCH on-board (best effort: relies on firmware having set them up)
    { INTEL, 0x10EA, "Intel 82577LM" },
    { INTEL, 0x10EF, "Intel 82578DM" },
    { INTEL, 0x1502, "Intel 82579LM" },
    { INTEL, 0x1503, "Intel 82579V" },
    { INTEL, 0x153A, "Intel I217-LM" },
    { INTEL, 0x153B, "Intel I217-V" },
    { INTEL, 0x155A, "Intel I218-LM" },
    { INTEL, 0x1559, "Intel I218-V" },
    { INTEL, 0x15A0, "Intel I218-LM" },
    { INTEL, 0x15A1, "Intel I218-V" },
    { INTEL, 0x156F, "Intel I219-LM" },
    { INTEL, 0x1570, "Intel I219-V" },
    { INTEL, 0x15B7, "Intel I219-LM" },
    { INTEL, 0x15B8, "Intel I219-V" },
    { INTEL, 0x15BB, "Intel I219-LM" },
    { INTEL, 0x15BC, "Intel I219-V" },
    { INTEL, 0x15BD, "Intel I219-LM" },
    { INTEL, 0x15BE, "Intel I219-V" },
    { INTEL, 0x15D7, "Intel I219-LM" },
    { INTEL, 0x15D8, "Intel I219-V" },
    { INTEL, 0x15E3, "Intel I219-LM" },
    { INTEL, 0x15D6, "Intel I219-V" },
    { INTEL, 0x0D4E, "Intel I219-LM" },
    { INTEL, 0x0D4F, "Intel I219-V" },
    { INTEL, 0x15FB, "Intel I219-LM" },
    { INTEL, 0x15FC, "Intel I219-V" },
    { 0, 0, 0 }
};

// per-chip quirks, keyed by device ID (anything not listed: EE_SHORT)
static int e1000_quirks(uint16_t dev)
{
    switch (dev) {
    case 0x1004: case 0x1008: case 0x100C: case 0x100D:
        return EE_NONE;
    case 0x1013: case 0x1076: case 0x1078: case 0x107C:
    case 0x1019: case 0x1075:
    case 0x105E: case 0x107D: case 0x10B9: case 0x108C: case 0x109A:
    case 0x10D3: case 0x10F6: case 0x150C:
        return EE_LONG;
    case 0x100E: case 0x1015: case 0x1016: case 0x1017: case 0x101E:
    case 0x100F: case 0x1026: case 0x1010: case 0x1079:
        return EE_SHORT;
    default:
        return E_PCH;                   // PCH parts have no EERD either
    }
}

#endif
