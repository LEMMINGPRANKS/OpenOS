#include "shell.h"
#include "term.h"
#include "kb.h"
#include "timer.h"
#include "dev.h"
#include "initrd.h"
#include "ramfs.h"
#include "files.h"
#include "ext.h"
#include "path.h"
#include "js.h"
#include "getspgk.h"
#include "news.h"
#include "gfx.h"
#include "wm.h"
#include "apps.h"
#include "paint.h"
#include "http.h"
#include "net.h"
#include "dns.h"
#include "desktop.h"
#include "version.h"
#include "mm.h"
#include "heap.h"
#include "ata.h"
#include "part.h"
#include "store.h"
#include "kupdate.h"
#include "settings.h"
#include "nic.h"

#define LINE_MAX 128
#define SHELL_STATES 5          // [0] = boot/VGA console, rest = windows

struct shell_state {
    struct console *con;
    char line[LINE_MAX];
    int n;
    uint8_t used;
};

static struct shell_state states[SHELL_STATES];
static char cwd[PATH_MAX] = "/";

static void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(val), "d"(port));
}

static void print_u64(uint64_t v)
{
    char digits[20];
    int n = 0;
    if (v == 0) {
        term_putc('0');
        return;
    }
    while (v) {
        digits[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n)
        term_putc(digits[--n]);
}

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static void print_hex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    term_putc(hex[v >> 4]);
    term_putc(hex[v & 0xF]);
}

static struct shell_state *state_for(struct console *con)
{
    for (int i = 0; i < SHELL_STATES; i++)
        if (states[i].used && states[i].con == con)
            return &states[i];
    for (int i = 1; i < SHELL_STATES; i++)          // slot 0 = NULL console
        if (!states[i].used) {
            states[i].used = 1;
            states[i].con = con;
            states[i].n = 0;
            states[i].line[0] = 0;
            return &states[i];
        }
    return &states[0];
}

static void prompt(void)
{
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts(" ");
    if (cwd[1])
        term_puts(path_basename(cwd));   // "docs>" inside /docs
    else
        term_puts("openos");
    term_puts("> ");
}

static void cmd_cd(char *arg)
{
    char full[PATH_MAX];
    if (!arg || !arg[0]) {              // bare cd goes home
        cwd[0] = '/'; cwd[1] = 0;
        return;
    }
    path_resolve(cwd, arg, full);
    if (!files_is_dir(full)) {
        term_puts("cd: not a directory: ");
        term_puts(arg);
        term_putc('\n');
        return;
    }
    for (int i = 0; i < PATH_MAX; i++) {
        cwd[i] = full[i];
        if (!full[i])
            break;
    }
}

static void cmd_help(void)
{
    term_puts("commands:\n");
    term_puts("  help          this list\n");
    term_puts("  echo <text>   say it back\n");
    term_puts("  ls            list this directory (colour-coded)\n");
    term_puts("  cd <dir>      change directory (cd .. goes up, cd goes home)\n");
    term_puts("  pwd           print the current directory\n");
    term_puts("  cat <file>    read a file (DR1 first, then IR2)\n");
    term_puts("  run <file.js> execute an OpenJS script\n");
    term_puts("  file <name>   show a file's type (.txt .cpp .iso ...)\n");
    term_puts("  getspgk list  packages on the spgk server\n");
    term_puts("  getspgk install <pkg>  download a package into ramfs\n");
    term_puts("  getspgk server <ip>    use a real LAN machine as the server\n");
    term_puts("  netinfo       show network info (ip, mac)\n");
    term_puts("  dns <name>    look up a name (try: dns example.com)\n");
    term_puts("  fetch <url>   download a page (try: fetch example.com/)\n");
    term_puts("  news          the News, in a window\n");
    term_puts("  internet      the Internet app (browser + news + updates + comments)\n");
    term_puts("  settings      settings app (network card, package server)\n");
    term_puts("  update        install fresh features from the update server\n");
    term_puts("  update kernel install a new KERNEL into the other slot (A/B)\n");
    term_puts("  dev           show device registers (DR/IR/UR)\n");
    term_puts("  disk          DR1 info ('disk test' writes + reads a sector)\n");
    term_puts("  save          copy all ramfs files to DR1 (survives reboot)\n");
    term_puts("  fire <dev>    trip a trap device (try: fire UR1)\n");
    term_puts("  meminfo       RAM map, free pages, heap use\n");
    term_puts("  mtest         exercise kmalloc/kfree, prove it works\n");
    term_puts("  uptime        how long since boot\n");
    term_puts("  about         what is OpenOS\n");
    term_puts("  banner        show the boot banner\n");
    term_puts("  clear         wipe the screen\n");
    term_puts("  reboot        restart the machine\n");
}

static void cmd_echo(char *arg)
{
    if (arg)
        term_puts(arg);
    term_putc('\n');
}

static void cmd_uptime(void)
{
    uint64_t ms = timer_uptime_ms();
    print_u64(ms / 1000);
    term_puts(".");
    if (ms % 1000 < 100) term_putc('0');
    if (ms % 1000 < 10) term_putc('0');
    print_u64(ms % 1000);
    term_puts(" seconds\n");
}

// DR1: show the main drive, or prove the metal with a write+read round trip
#define DISK_SCRATCH_LBA 3100          // above the store area (2048..3071)

static void print_dbg(char *tag)
{
    term_puts(tag);
    term_puts(": where ");
    print_u64(ata_dbg_where());
    term_puts(" first 0x");
    print_u64(ata_dbg_first_status() >> 4);
    print_u64(ata_dbg_first_status() & 0xF);
    term_puts(" last 0x");
    print_u64(ata_dbg_status() >> 4);
    print_u64(ata_dbg_status() & 0xF);
    term_putc('\n');
}

static void cmd_disk(char *arg)
{
    if (!ata_present()) {
        term_puts("DR1: no drive found\n");
        return;
    }
    if (arg && !strcmp(arg, "test")) {
        static uint8_t wbuf[512], rbuf[512];
        for (int i = 0; i < 512; i++)
            wbuf[i] = (uint8_t)(i ^ 0x5A);
        if (ata_write(DISK_SCRATCH_LBA, 1, wbuf) != 0) {
            print_dbg("disk test: WRITE failed");
            return;
        }
        if (ata_read(DISK_SCRATCH_LBA, 1, rbuf) != 0) {
            print_dbg("disk test: READ failed");
            return;
        }
        for (int i = 0; i < 512; i++)
            if (rbuf[i] != wbuf[i]) {
                term_puts("disk test: FAIL at byte ");
                print_u64((uint64_t)i);
                term_putc('\n');
                return;
            }
        term_puts("disk test: PASS (512 bytes written + read back)\n");
        return;
    }
    term_puts("DR1: ");
    term_puts(ata_model());
    term_putc('\n');
    term_puts("  sectors: ");
    print_u64(ata_sectors());
    term_puts("  (");
    print_u64(ata_sectors() / 2048);
    term_puts(" MB)\n");

    // the MBR partition table (dual boot: we share the drive)
    static uint8_t mbr[512];
    if (ata_read(0, 1, mbr) == 0 && mbr[510] == 0x55 && mbr[511] == 0xAA) {
        term_puts("  partitions:\n");
        for (int i = 0; i < 4; i++) {
            uint8_t type = mbr[446 + i * 16 + 4];
            if (!type)
                continue;
            const uint8_t *p = mbr + 446 + i * 16 + 8;
            uint32_t start = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                             ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            term_puts("    ");
            term_putc('1' + i);
            term_puts(": type 0x");
            print_hex8(type);
            term_puts(start == part_base() ? "  <-- OpenOS lives here  " : "  at LBA ");
            if (start != part_base())
                print_u64(start);
            term_putc('\n');
        }
    }
    term_puts("  try 'disk test' to prove the metal\n");
}

// DR1 persistence: copy every ramfs file onto the disk store
static void cmd_save(void)
{
    int n = store_flush();
    if (n < 0) {
        term_puts("save failed (code ");
        print_u64((uint64_t)-n);
        term_puts(")\n");
        return;
    }
    term_puts("saved ");
    print_u64((uint64_t)n);
    term_puts(n == 1 ? " file to DR1 -- it survives reboot\n"
                     : " files to DR1 -- they survive reboot\n");
}

static void cmd_about(void)
{
    term_puts("OpenOS " OS_VERSION " -- a 64-bit open-source OS from scratch.\n");
    term_puts("BDFL: Freddie. Kernel + shell + desktop + files + network (getspgk).\n");
}

static void cmd_banner(void)
{
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("\n   OpenOS " OS_VERSION "   cd + OpenJS + browser + news + updates\n\n");
}

// the updater: ask the server what's new and install it live
static void cmd_update(char *arg)
{
    if (arg && !strcmp(arg, "kernel")) {     // system-level: new kernel -> A/B slot
        kupdate_install();
        return;
    }
    term_puts("checking the update server...\n");
    if (http_ensure_net() != 0)
        return;
    static uint8_t buf[HTTP_MAX];
    int n = http_get("/version", buf, HTTP_MAX);
    if (n <= 0) {
        term_puts("could not reach the update server\n");
        return;
    }
    int vn = n < (int)sizeof buf - 1 ? n : (int)sizeof buf - 1;
    buf[vn] = 0;
    term_puts("server feature pack: ");
    term_puts((const char *)buf);
    term_putc('\n');
    term_puts("this OpenOS       : ");
    term_puts(OS_VERSION);
    term_putc('\n');

    n = http_get("/updates/index", buf, HTTP_MAX);
    if (n <= 0) {
        term_puts("no updates listed on the server\n");
        return;
    }
    int installed = 0;
    for (int i = 0; i < n; ) {
        char name[64];
        int ln = 0;
        while (i < n && buf[i] != '\n' && buf[i] != '\r')
            if (ln < 62) name[ln++] = (char)buf[i++];
            else i++;
        name[ln] = 0;
        while (i < n && (buf[i] == '\n' || buf[i] == '\r'))
            i++;
        if (!ln)
            continue;
        char upath[80];
        int un = 0;
        const char *pre = "/updates/";
        while (*pre && un < 78) upath[un++] = *pre++;
        for (int k = 0; name[k] && un < 78; k++)
            upath[un++] = name[k];
        upath[un] = 0;
        static uint8_t fbuf[HTTP_MAX];
        int fn = http_get(upath, fbuf, HTTP_MAX);
        if (fn <= 0) {
            term_puts("  could not fetch ");
            term_puts(name);
            term_putc('\n');
            continue;
        }
        char fpath[80];
        fpath[0] = '/';
        int fpn = 1;
        for (int k = 0; name[k] && fpn < 78; k++)
            fpath[fpn++] = name[k];
        fpath[fpn] = 0;
        if (ramfs_write(fpath, (const char *)fbuf, (uint32_t)fn) != 0) {
            term_puts("  ramfs full, could not install ");
            term_puts(name);
            term_putc('\n');
            continue;
        }
        term_puts("  installed ");
        term_puts(name);
        term_putc('\n');
        installed++;
    }
    if (installed) {
        desktop_repaint();               // fresh icons, right now
        if (store_flush() >= 0)
            term_puts("saved to DR1 -- they survive reboot now\n");
        term_puts("new features are on the desktop -- no reboot needed!\n");
        term_puts("(or browse to 10.0.2.2:8080/updates to pick by hand)\n");
    } else {
        term_puts("nothing to install\n");
    }
}

static void print_hex(uint64_t v)
{
    term_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint64_t n = (v >> shift) & 0xF;
        if (n || shift == 0)
            term_putc("0123456789ABCDEF"[n]);
    }
}

static void cmd_meminfo(void)
{
    uint64_t usable = mm_total_usable();
    uint64_t freeb = mm_free_bytes();
    term_puts("physical memory (from firmware map):\n");
    term_puts("  usable RAM : ");
    print_u64(usable / (1024 * 1024));
    term_puts(" MiB\n");
    term_puts("  free       : ");
    print_u64(freeb / 1024);
    term_puts(" KiB (");
    print_u64(freeb / 4096);
    term_puts(" free pages of 4 KiB)\n");
    term_puts("kernel heap:\n  used ");
    print_u64(heap_used());
    term_puts(" of ");
    print_u64(heap_total());
    term_puts(" bytes\n");
}

static void cmd_mtest(void)
{
    term_puts("allocating...\n");
    char *a = kmalloc(16);
    char *b = kmalloc(100);
    char *c = kmalloc(4096);
    if (!a || !b || !c) {
        term_puts("FAIL: kmalloc returned NULL\n");
        return;
    }
    for (int i = 0; i < 16; i++) a[i] = (char)(i * 7);
    for (int i = 0; i < 100; i++) b[i] = 0x5A;
    for (int i = 0; i < 4096; i++) c[i] = (char)(i & 0xFF);

    int ok = 1;
    for (int i = 0; i < 4096; i++)
        if (c[i] != (char)(i & 0xFF)) { ok = 0; break; }
    for (int i = 0; i < 16; i++)
        if (a[i] != (char)(i * 7)) { ok = 0; break; }

    term_puts("  a(16)   = "); print_hex((uint64_t)a); term_putc('\n');
    term_puts("  b(100)  = "); print_hex((uint64_t)b); term_putc('\n');
    term_puts("  c(4096) = "); print_hex((uint64_t)c); term_putc('\n');

    kfree(b);
    char *d = kmalloc(50);               // should recycle b's space
    term_puts("  d(50)   = "); print_hex((uint64_t)d);
    term_puts("   (after kfree(b))\n");
    if (d == b)
        term_puts("  recycle check: d landed exactly on b -- merging works\n");

    term_puts(ok ? "  pattern check: PASS\n" : "  pattern check: FAIL\n");
    term_puts("  heap used now: ");
    print_u64(heap_used());
    term_puts(" bytes\n");
    kfree(a); kfree(c); kfree(d);
    term_puts("  all freed. heap used back to ");
    print_u64(heap_used());
    term_puts(" bytes\n");
}

static void cmd_reboot(void)
{
    term_puts("rebooting...\n");
    outb(0x64, 0xFE);                  // 8042 pulse reset line
    for (;;)
        __asm__ volatile ("hlt");
}

void shell_execute(char *cmdline)
{
    char *arg = 0;
    while (*cmdline == ' ' || *cmdline == '\t')   // leading spaces are free
        cmdline++;
    for (char *p = cmdline; *p; p++) {
        if (*p == ' ' || *p == '\t') {
            *p = 0;
            arg = p + 1;
            break;
        }
    }
    if (!cmdline[0])
        return;
    if (!strcmp(cmdline, "help"))        cmd_help();
    else if (!strcmp(cmdline, "echo"))   cmd_echo(arg);
    else if (!strcmp(cmdline, "uptime")) cmd_uptime();
    else if (!strcmp(cmdline, "about"))  cmd_about();
    else if (!strcmp(cmdline, "banner")) cmd_banner();
    else if (!strcmp(cmdline, "clear"))  term_clear();
    else if (!strcmp(cmdline, "reboot")) cmd_reboot();
    else if (!strcmp(cmdline, "cd"))     cmd_cd(arg);
    else if (!strcmp(cmdline, "pwd"))    { term_puts(cwd); term_putc('\n'); }
    else if (!strcmp(cmdline, "ls")) {
        struct fileinfo fl[FILES_MAX];
        int n = files_list_dir(cwd, fl, FILES_MAX);
        if (!n) {
            term_puts("(empty directory)\n");
            return;
        }
        for (int i = 0; i < n; i++) {
            if (fl[i].is_dir) {
                term_setcolor(TERM_COLOR_DIR);
                term_puts("  ");
                term_puts(fl[i].name);
                term_puts("/\n");
                continue;
            }
            term_setcolor(ext_lookup(fl[i].name)->vga_color);
            term_puts("  ");
            term_puts(fl[i].name);
            term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
            term_puts("  [");
            term_puts(fl[i].source == FS_RAMFS ? "DR1" : "IR2");
            term_puts("] ");
            print_u64(fl[i].size);
            term_putc('\n');
        }
    }
    else if (!strcmp(cmdline, "cat")) {
        if (!arg) { term_puts("usage: cat <file>\n"); return; }
        char full[PATH_MAX];
        path_resolve(cwd, arg, full);
        uint32_t size = 0;
        const char *data = files_read(full, &size);
        if (!data) {
            term_puts("cat: no such file: ");
            term_puts(arg);
            term_putc('\n');
            return;
        }
        for (uint32_t i = 0; i < size; i++)
            term_putc(data[i]);
        if (size && data[size - 1] != '\n')
            term_putc('\n');
    }
    else if (!strcmp(cmdline, "run")) {
        if (!arg) { term_puts("usage: run <file.js>\n"); return; }
        char full[PATH_MAX];
        path_resolve(cwd, arg, full);
        uint32_t size = 0;
        const char *data = files_read(full, &size);
        if (!data) {
            term_puts("run: no such file: ");
            term_puts(arg);
            term_putc('\n');
            return;
        }
        char err[80];
        if (js_run(data, size, err, sizeof err) != 0)
            term_puts(err);
    }
    else if (!strcmp(cmdline, "file")) {
        if (!arg) { term_puts("usage: file <name>\n"); return; }
        const struct ext_type *t = ext_lookup(arg);
        term_puts(arg);
        term_puts(": ");
        term_puts(t->desc);
        term_putc('\n');
    }
    else if (!strcmp(cmdline, "getspgk")) cmd_getspgk(arg);
    else if (!strcmp(cmdline, "fetch")) {
        if (!arg) { term_puts("usage: fetch <name/path>\n"); return; }
        static uint8_t buf[HTTP_MAX];
        int n = http_get_url(arg, buf, HTTP_MAX);
        if (n == -1) {
            term_puts("fetch: could not reach it\n");
            return;
        }
        if (n == -2) {
            term_puts("fetch: the server said no (404?)\n");
            return;
        }
        if (n >= (int)sizeof buf - 1)
            n = (int)sizeof buf - 1;
        buf[n] = 0;
        term_puts((const char *)buf);
        if (n && buf[n - 1] != '\n')
            term_putc('\n');
        term_puts("-- ");
        print_u64((uint64_t)n);
        term_puts(" bytes\n");
    }
    else if (!strcmp(cmdline, "netinfo")) cmd_netinfo();
    else if (!strcmp(cmdline, "dns")) {
        if (!arg) { term_puts("usage: dns <name>\n"); return; }
        if (http_ensure_net() != 0)
            return;
        char s[16];
        net_ip_str(dns_server(), s);
        term_puts("asking ");
        term_puts(s);
        term_puts(" for ");
        term_puts(arg);
        term_puts("...\n");
        uint32_t ip = dns_resolve(arg);
        if (!ip) {
            term_puts("dns: no answer for ");
            term_puts(arg);
            term_putc('\n');
            return;
        }
        net_ip_str(ip, s);
        term_puts(arg);
        term_puts(" is at ");
        term_puts(s);
        term_putc('\n');
    }
    else if (!strcmp(cmdline, "update")) cmd_update(arg);
    else if (!strcmp(cmdline, "news")) {
        if (gfx_available()) {
            app_set_arg("");
            wm_open(APP_INTERNET, 60, 40, 432, 424);
        } else
            news_fetch(term_active());  // headless: straight to the console
    }
    else if (!strcmp(cmdline, "internet")) {
        if (gfx_available()) {
            app_set_arg("");
            wm_open(APP_INTERNET, 60, 40, 432, 424);
        } else
            term_puts("internet needs the desktop (framebuffer)\n");
    }
    else if (!strcmp(cmdline, "paint")) {
        if (gfx_available()) {
            app_set_arg(arg ? arg : "");
            wm_open(APP_PAINT, 60, 40, PAINT_WIN_W, PAINT_WIN_H);
        } else
            term_puts("paint needs the desktop (framebuffer)\n");
    }
    else if (!strcmp(cmdline, "settings")) {
        if (gfx_available()) {
            wm_open(APP_SETTINGS, 60, 40, 400, 300);
        } else {
            // headless: print what's set
            char v[32];
            term_puts("nic: ");
            if (settings_get("nic", v, sizeof v) == 0)
                term_puts(v);
            else
                term_puts("auto");
            term_puts(" (active: ");
            term_puts(nic_name());
            term_puts(")\nserver: ");
            if (settings_get("server", v, sizeof v) == 0)
                term_puts(v);
            else {
                char ipstr[16];
                net_ip_str(http_server_ip(), ipstr);
                term_puts(ipstr);
                term_puts(" (default)");
            }
            term_putc('\n');
        }
    }
    else if (!strcmp(cmdline, "dev"))    dev_list();
    else if (!strcmp(cmdline, "disk"))   cmd_disk(arg);
    else if (!strcmp(cmdline, "save"))   cmd_save();
    else if (!strcmp(cmdline, "meminfo")) cmd_meminfo();
    else if (!strcmp(cmdline, "mtest"))  cmd_mtest();
    else if (!strcmp(cmdline, "fire")) {
        if (!arg) { term_puts("usage: fire <dev>\n"); return; }
        int r = dev_fire(arg);
        if (r == 0) {
            term_puts("UR1 armed. Unsupported device incoming...\n");
            return;                  // the watchdog will catch it
        }
        if (r < 0) {
            term_puts("fire: not a trap device: ");
            term_puts(arg);
            term_putc('\n');
        }
    }
    else {
        term_puts("unknown command: ");
        term_puts(cmdline);
        term_puts("  (try 'help')\n");
    }
}

// --- shell as an app ------------------------------------------------------

void shell_app_open(struct console *con)
{
    state_for(con);
    if (con)
        term_use(con);
    term_puts("OpenOS shell. type 'help' for commands.\n\n");
    prompt();
}

void shell_app_input(struct console *con, char c)
{
    struct shell_state *st = state_for(con);
    if (con)
        term_use(con);
    if (c == '\n') {
        term_putc('\n');
        st->line[st->n] = 0;
        shell_execute(st->line);
        st->n = 0;
        prompt();
        return;
    }
    if (c == '\b') {
        if (st->n > 0) {
            st->n--;
            term_puts("\b \b");
        }
        return;
    }
    if (c == 27) {                     // ESC reaches here only in fallback
        while (st->n > 0) {
            st->n--;
            term_puts("\b \b");
        }
        return;
    }
    if (c >= 32 && c < 127 && st->n < LINE_MAX - 1) {
        st->line[st->n++] = c;
        term_putc(c);
    }
}

void shell_run(void)
{
    states[0].used = 1;
    shell_app_input(0, '\0');          // force state alloc, harmless char
    term_puts("\n");
    prompt();
    for (;;)
        shell_app_input(0, kb_getchar());
}
