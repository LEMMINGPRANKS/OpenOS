#ifndef OPENOS_KUPDATE_H
#define OPENOS_KUPDATE_H

#include <stdint.h>

// System-level updates: the kernel can replace ITSELF from the spgk
// server. The boot drive keeps two kernel slots (A + B, 256 KiB each,
// bios/layout.inc is the law); stage2 tries the staged candidate and
// rolls back to the last good kernel if it never confirms itself --
// a half-written or broken update can never brick the machine.

#define KUPDATE_MAX  (256 * 1024)     // one slot

// Read stage2's tag 0x1337 from the multiboot2 info block: which slot
// booted + whether it's a candidate update. Call from kmain (magic branch).
void kupdate_scan_mb2(unsigned long addr);

int  kupdate_boot_slot(void);         // 0 = A, 1 = B
int  kupdate_was_candidate(void);

// Called once the new kernel is alive: promote the candidate to
// last-known-good so stage2 keeps booting it. 0 = ok / nothing to do.
int  kupdate_confirm(void);

// Ask the server about a kernel update. ver = 0 checks the stable channel,
// a string like "1.4.1" checks that exact version. 0 = there is one (fills
// version, size, entry, bss), 1 = we're current, -1 = couldn't reach.
int  kupdate_check(const char *ver, char *ver_out, int ver_max,
                   uint32_t *size, uint32_t *entry, uint32_t *bss);

// Print every kernel version the server offers (stable + unstable).
int  kupdate_list(void);

// Download + install a kernel into the other slot and stage it in the A/B
// header; the caller reboots. ver = 0 (or "stable") installs the stable
// channel, any other version string installs that one (downgrades allowed
// -- that's how you roll back). 0 = ok, negative = fail.
int  kupdate_install(const char *ver);

#endif
