#include "nv.h"
#include "pci.h"

#define NV_VENDOR 0x10DE
#define NV_BOOT0  0x000000       // family lives at bits 20..28 (coldrivers)

struct nv_known {
    uint16_t dev;
    const char *name;
    const char *arch;
};

static const struct nv_known known[] = {
    { 0x08A3, "GeForce 320M",        "MCP89" },
    { 0x2503, "GeForce RTX 3060",    "GA106" },
    { 0x2504, "GeForce RTX 3060",    "GA106" },
    { 0x2484, "GeForce RTX 3070",    "GA104" },
    { 0x2206, "GeForce RTX 3080",    "GA102" },
    { 0x2208, "GeForce RTX 3080 Ti", "GA102" },
    { 0x2684, "GeForce RTX 4090",    "AD102" },
};

static uint8_t  nv_bus, nv_slot;
static int      found;
static uint16_t dev_id;
static uint32_t bar0;             // MMIO regs base, 0 if unreachable
static uint32_t bar_sz[2];
static uint32_t boot0;
static int      boot0_ok;
static char     idstr[44];

int nv_found(void) { return found; }

const char *nv_ident(void)
{
    return found ? idstr : "no NVIDIA card";
}

uint32_t nv_family(void)  { return boot0_ok ? boot0 : 0xFFFFFFFF; }
int nv_family_known(void) { return boot0_ok; }
uint32_t nv_bar0(void)    { return bar0; }
uint32_t nv_bar_size(int bar)
{
    return (bar == 0 || bar == 1) ? bar_sz[bar] : 0;
}

// config-space size probe: save the BAR, ask for the biggest window,
// put it back (what every OS does at boot; harmless flicker at worst)
static uint32_t probe_bar_size(uint8_t b, uint8_t s, uint8_t off)
{
    uint32_t save = pci_read32(b, s, 0, off);
    pci_write32(b, s, 0, off, 0xFFFFFFFF);
    uint32_t v = pci_read32(b, s, 0, off);
    pci_write32(b, s, 0, off, save);
    if (v == 0 || v == 0xFFFFFFFF || (v & 1))
        return 0;                  // absent, broken, or I/O (not ours)
    v &= ~0xFu;                    // memory BAR: low flag bits stripped
    return (~v) + 1;               // mask -> size
}

int nv_probe(void)
{
    for (uint32_t b = 0; b < 256 && !found; b++) {
        for (uint32_t s = 0; s < 32 && !found; s++) {
            if (pci_vendor((uint8_t)b, (uint8_t)s) != NV_VENDOR)
                continue;
            uint32_t cls = pci_read32((uint8_t)b, (uint8_t)s, 0, 8) >> 24;
            if (cls != 0x03)       // display controller only
                continue;
            nv_bus = (uint8_t)b;
            nv_slot = (uint8_t)s;
            found = 1;
        }
    }
    if (!found)
        return 0;

    dev_id = pci_device(nv_bus, nv_slot);
    const struct nv_known *k = 0;
    for (uint32_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].dev == dev_id) { k = &known[i]; break; }

    // name it (known table first, honest fallback for new-chip day)
    char *p = idstr;
    const char *src = k ? k->name : "NVIDIA GPU";
    while (*src) *p++ = *src++;
    *p++ = ' ';
    *p++ = '[';
    src = k ? k->arch : "unknown";
    while (*src) *p++ = *src++;
    *p++ = ']';
    *p = 0;

    // BAR0 = MMIO registers. Only touch it if the whole BAR lives under
    // our 4 GiB identity map (64-bit BARs up high get named, not poked).
    uint32_t lo = pci_read32(nv_bus, nv_slot, 0, 0x10);
    uint32_t hi = pci_read32(nv_bus, nv_slot, 0, 0x14);
    if (!(lo & 1) && hi == 0 && (lo & ~0xFu) != 0) {
        bar0 = lo & ~0xFu;
        boot0 = *(volatile uint32_t *)(uint64_t)bar0;
        boot0_ok = 1;
    }
    bar_sz[0] = probe_bar_size(nv_bus, nv_slot, 0x10);
    bar_sz[1] = probe_bar_size(nv_bus, nv_slot, 0x18);
    return 1;
}
