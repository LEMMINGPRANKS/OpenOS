#include "kupdate.h"
#include "ata.h"
#include "part.h"
#include "heap.h"
#include "http.h"
#include "term.h"
#include "version.h"

// The A/B law lives in bios/layout.inc; these must match it. All LBAs
// here are partition-relative -- disk_lba() adds the OpenOS partition base.
#define KHDR_LBA      127
#define KSLOT_A_LBA   128
#define KSLOT_SECT    512                  // sectors per slot
#define KSLOT_MAGIC   0x4B534C4Fu          // "OLSK"

// header sector fields (little-endian u32s)
#define H_OK_SLOT     4
#define H_OK_SECTORS  8
#define H_OK_ENTRY    12
#define H_OK_BSS      16
#define H_CAND_SLOT   20
#define H_CAND_SECTORS 24
#define H_CAND_ENTRY  28
#define H_CAND_BSS    32
#define H_BOOTING     36

#define NONE          0xFFFFFFFFu

static int boot_slot;
static int was_candidate;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// route ATA to the boot disk (master) for header/slot work, then back
static void ata_boot_disk(void)
{
    ata_use_slave(0);
}

static void ata_restore(void)
{
    ata_use_slave(ata_slave_present());
}

void kupdate_scan_mb2(unsigned long addr)
{
    boot_slot = 0;
    was_candidate = 0;
    uint8_t *p = (uint8_t *)addr + 8;
    for (;;) {
        uint32_t type = rd32(p);
        uint32_t size = rd32(p + 4);
        if (type == 0)
            break;
        if (type == 0x1337 && rd32(p + 4) >= 16) {
            boot_slot = (int)rd32(p + 8);
            was_candidate = (int)rd32(p + 12);
            if (boot_slot > 1)
                boot_slot = 0;
        }
        p += (size + 7) & ~7u;
    }
}

int kupdate_boot_slot(void)      { return boot_slot; }
int kupdate_was_candidate(void)  { return was_candidate; }

int kupdate_confirm(void)
{
    if (!was_candidate)
        return 0;
    static uint8_t hdr[512];
    ata_boot_disk();
    if (ata_read(disk_lba(KHDR_LBA), 1, hdr) != 0) {
        ata_restore();
        return -1;
    }
    if (rd32(hdr) != KSLOT_MAGIC) {
        ata_restore();
        return -1;
    }
    // promote: ok <- cand, scrap the candidate, clear the booting flag
    wr32(hdr + H_OK_SLOT, rd32(hdr + H_CAND_SLOT));
    wr32(hdr + H_OK_SECTORS, rd32(hdr + H_CAND_SECTORS));
    wr32(hdr + H_OK_ENTRY, rd32(hdr + H_CAND_ENTRY));
    wr32(hdr + H_OK_BSS, rd32(hdr + H_CAND_BSS));
    wr32(hdr + H_CAND_SLOT, NONE);
    wr32(hdr + H_BOOTING, 0);
    int r = ata_write(disk_lba(KHDR_LBA), 1, hdr);
    ata_restore();
    if (r == 0)
        term_puts("   kernel update confirmed -- this kernel is now the good one\n");
    return r;
}

static uint32_t parse_dec(const char *s, int len)
{
    uint32_t v = 0;
    for (int i = 0; i < len && s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (uint32_t)(s[i] - '0');
    return v;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a == (uint8_t)*b;
}

static int streq_len(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

// "/kernel/manifest.txt" (stable channel) or "/kernel/v1.4.1/manifest.txt"
static void kpath(char *out, const char *ver, const char *file)
{
    char *p = out;
    for (const char *s = "/kernel/"; *s; s++) *p++ = *s;
    if (ver && ver[0]) {
        *p++ = 'v';
        for (const char *s = ver; *s; s++) *p++ = *s;
        *p++ = '/';
    }
    for (const char *s = file; *s; s++) *p++ = *s;
    *p = 0;
}

// manifest.txt lines: version=1.3.1  size=83968  entry=48  bss=4931584
int kupdate_check(const char *ver, char *ver_out, int ver_max,
                  uint32_t *size, uint32_t *entry, uint32_t *bss)
{
    if (http_ensure_net() != 0)
        return -1;
    static char buf[512];
    char path[48];
    kpath(path, ver, "manifest.txt");
    int n = http_get(path, (uint8_t *)buf, sizeof buf - 1);
    if (n <= 0)
        return -1;
    buf[n] = 0;

    char version[24] = { 0 };
    uint32_t vsize = 0, ventry = NONE, vbss = NONE;
    int i = 0;
    while (i < n) {
        int e = i;
        while (e < n && buf[e] != '\n' && buf[e] != '\r')
            e++;
        int len = e - i;
        if (len > 8 && streq_len(buf + i, "version=", 8)) {
            int k = 0;
            for (int j = 8; j < len && k < 22; j++, k++)
                version[k] = buf[i + j];
            version[k] = 0;
        } else if (len > 5 && streq_len(buf + i, "size=", 5))
            vsize = parse_dec(buf + i + 5, len - 5);
        else if (len > 6 && streq_len(buf + i, "entry=", 6))
            ventry = parse_dec(buf + i + 6, len - 6);
        else if (len > 4 && streq_len(buf + i, "bss=", 4))
            vbss = parse_dec(buf + i + 4, len - 4);
        i = e + 1;
        while (i < n && (buf[i] == '\n' || buf[i] == '\r'))
            i++;
    }
    if (!version[0] || !vsize || ventry == NONE || vbss == NONE)
        return -1;
    if (streq(version, OS_VERSION))
        return 1;
    if (ver_out) {
        int k = 0;
        for (; version[k] && k < ver_max - 1; k++)
            ver_out[k] = version[k];
        ver_out[k] = 0;
    }
    *size = vsize;
    *entry = ventry;
    *bss = vbss;
    return 0;
}

static int same512(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 512; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

int kupdate_list(void)
{
    if (http_ensure_net() != 0)
        return -1;
    static char buf[512];
    int n = http_get("/kernel/versions", (uint8_t *)buf, sizeof buf - 1);
    if (n <= 0) {
        term_puts("could not reach the update server\n");
        return -1;
    }
    buf[n] = 0;
    term_puts("kernel versions on the server:\n");
    int i = 0;
    while (i < n) {
        int e = i;
        while (e < n && buf[e] != '\n' && buf[e] != '\r')
            e++;
        if (e > i) {
            term_puts("  ");
            for (int k = i; k < e; k++)
                term_putc(buf[k]);
            int vl = 0;
            while (OS_VERSION[vl]) vl++;
            int tl = i;
            while (tl < e && buf[tl] != ' ')
                tl++;
            if (tl - i == vl && streq_len(buf + i, OS_VERSION, vl))
                term_puts("   <- running");
            term_putc('\n');
        }
        i = e + 1;
        while (i < n && (buf[i] == '\n' || buf[i] == '\r'))
            i++;
    }
    term_puts("pick one: update kernel <version>\n");
    return 0;
}

int kupdate_install(const char *ver)
{
    if (ver && streq(ver, "stable"))
        ver = 0;
    char verbuf[24];
    uint32_t size = 0, entry = 0, bss = 0;
    int c = kupdate_check(ver, verbuf, sizeof verbuf, &size, &entry, &bss);
    if (c == 1) {
        term_puts("kernel is already up to date (");
        term_puts(OS_VERSION ")\n");
        return 1;
    }
    if (c != 0)
        return -1;
    if (size > KUPDATE_MAX) {
        term_puts("kernel too big for a slot (");
        term_puts("over 256 KiB)\n");
        return -1;
    }
    term_puts("downloading kernel ");
    term_puts(verbuf);
    term_puts("...\n");

    uint8_t *kbuf = kmalloc(KUPDATE_MAX);
    if (!kbuf)
        return -1;
    for (uint32_t i = 0; i < KUPDATE_MAX; i++)
        kbuf[i] = 0;
    char flatpath[48];
    kpath(flatpath, ver, "kernel.flat");
    int n = http_get(flatpath, kbuf, size);
    if (n != (int)size) {
        term_puts("download failed or short (got ");
        char d[12];
        int dn = 0, v = n < 0 ? -n : n;
        if (!v) d[dn++] = '0';
        while (v) { d[dn++] = (char)('0' + v % 10); v /= 10; }
        while (dn) term_putc(d[--dn]);
        term_puts(" bytes)\n");
        kfree(kbuf);
        return -1;
    }

    // sanity: a real multiboot2 header magic near the start
    int magic_ok = 0;
    for (uint32_t off = 0; off + 4 <= 8192 && off + 4 <= size; off += 4)
        if (rd32(kbuf + off) == 0xE85250D6u) {
            magic_ok = 1;
            break;
        }
    if (!magic_ok) {
        term_puts("that is not an OpenOS kernel (bad magic) -- not installing\n");
        kfree(kbuf);
        return -1;
    }

    int slot = boot_slot == 0 ? 1 : 0;
    uint32_t nsect = (size + 511) / 512;
    ata_boot_disk();                   // base must resolve on the BOOT disk,
    uint32_t slot_lba = disk_lba(KSLOT_A_LBA + (uint32_t)slot * KSLOT_SECT);
    term_puts(slot ? "writing kernel slot B...\n" : "writing kernel slot A...\n");
    if (ata_write(slot_lba, nsect, kbuf) != 0) {
        term_puts("disk write failed\n");
        kfree(kbuf);
        ata_restore();
        return -1;
    }

    // read the whole thing back -- a half-written update must never boot
    static uint8_t scratch[512];
    for (uint32_t s = 0; s < nsect; s++) {
        if (ata_read(slot_lba + s, 1, scratch) != 0 ||
            !same512(scratch, kbuf + s * 512)) {
            term_puts("readback FAILED -- not staging this kernel\n");
            kfree(kbuf);
            ata_restore();
            return -1;
        }
    }
    kfree(kbuf);

    // stage it in the A/B header: ok_* untouched, candidate armed
    static uint8_t hdr[512];
    if (ata_read(disk_lba(KHDR_LBA), 1, hdr) != 0 || rd32(hdr) != KSLOT_MAGIC) {
        for (int i = 0; i < 512; i++)
            hdr[i] = 0;
        wr32(hdr, KSLOT_MAGIC);
        wr32(hdr + H_OK_SLOT, 0);
        wr32(hdr + H_OK_SECTORS, 0);      // 0 = stage2 baked defaults (slot A)
        wr32(hdr + H_OK_ENTRY, NONE);
        wr32(hdr + H_OK_BSS, NONE);
    }
    wr32(hdr + H_CAND_SLOT, (uint32_t)slot);
    wr32(hdr + H_CAND_SECTORS, nsect);
    wr32(hdr + H_CAND_ENTRY, entry);
    wr32(hdr + H_CAND_BSS, bss);
    wr32(hdr + H_BOOTING, 0);
    if (ata_write(disk_lba(KHDR_LBA), 1, hdr) != 0) {
        term_puts("could not write the A/B header\n");
        ata_restore();
        return -1;
    }
    ata_restore();
    term_puts("kernel ");
    term_puts(verbuf);
    term_puts(" staged in slot ");
    term_putc('A' + slot);
    term_puts(" -- reboot to switch to it\n");
    term_puts("(if it doesn't boot, OpenBIOS rolls back automatically)\n");
    return 0;
}
