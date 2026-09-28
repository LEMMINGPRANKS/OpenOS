#include "gpu.h"
#include "nv.h"
#include "term.h"

static void hex32(uint32_t v)
{
    const char *d = "0123456789ABCDEF";
    term_puts("0x");
    for (int s = 28; s >= 0; s -= 4)
        term_putc(d[(v >> s) & 0xF]);
}

static void dec32(uint32_t v)
{
    char digits[12];
    int n = 0;
    if (!v) digits[n++] = '0';
    while (v) { digits[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) term_putc(digits[--n]);
}

int gpu_probe(void)
{
    return nv_probe();
}

const char *gpu_ident(void)
{
    if (nv_found())
        return nv_ident();
    return "firmware framebuffer (software backend)";
}

// gpuinfo: what the detective knows
void gpu_info(void)
{
    term_puts("gpu      : ");
    term_puts(gpu_ident());
    term_putc('\n');
    if (!nv_found())
        return;
    if (nv_family_known()) {
        term_puts("family   : ");
        hex32((nv_family() >> 20) & 0x1FF);   // BOOT0 (0xaf = MCP89)
        term_putc('\n');
    } else {
        term_puts("family   : (BAR0 above 4 GiB -- named from PCI id only)\n");
    }
    term_puts("regs     : BAR0 ");
    hex32(nv_bar0());
    term_puts(" (");
    dec32(nv_bar_size(0) >> 20);
    term_puts(" MiB)\n");
    term_puts("aperture : BAR1 ");
    dec32(nv_bar_size(1) >> 20);
    term_puts(" MiB\n");
}
