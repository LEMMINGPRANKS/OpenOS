#include "gpu.h"
#include "nv.h"
#include "nvregs.h"
#include "curie.h"
#include "vram.h"
#include "gfx.h"
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
    gpu_regs();
}

// Live register state, offsets verified against nouveau (nvregs.h).
// Read-only: this is the doctor checking pulses, not writing prescriptions.
static void reg_line(const char *name, uint32_t reg)
{
    term_puts("  ");
    term_puts(name);
    term_puts(" ");
    hex32(nv_rd32(reg));
    term_putc('\n');
}

void gpu_regs(void)
{
    if (!nv_found()) {
        term_puts("regs     : (software backend -- no card to ask)\n");
        return;
    }
    if (!nv_family_known()) {
        term_puts("regs     : (BAR0 above 4 GiB -- unreachable)\n");
        return;
    }
    term_puts("regs     :\n");
    reg_line("PMC_BOOT_0    ", NV_PMC_BOOT_0);
    reg_line("PMC_INTR      ", NV_PMC_INTR);
    reg_line("PMC_INTR_EN   ", NV_PMC_INTR_EN);
    reg_line("PMC_ENABLE    ", NV_PMC_ENABLE);
    reg_line("PFIFO_INTR    ", NV_PFIFO_INTR);
    reg_line("PFIFO_INTR_EN ", NV_PFIFO_INTR_EN);
    // The 0x610xxx PDISPLAY block is nv50+ only (Tesla and later). Older
    // families (Curie, e.g. the 7300 GT) use the legacy CRTC elsewhere --
    // reading these there would print garbage dressed up as data.
    if (((nv_family() >> 20) & 0x1FF) >= 0x50) {
        reg_line("CRTC0_CLK_CTRL", NV50_PDISPLAY_CRTC_CLK_CTRL(0));
        reg_line("CRTC1_CLK_CTRL", NV50_PDISPLAY_CRTC_CLK_CTRL(1));
        reg_line("SOR0_SLINK    ", NV50_PDISPLAY_OUT_SLINK(0));
    }
}

// One compact line for the boot log (mirrors to COM1 serial automatically)
void gpu_boot_log(void)
{
    if (!nv_found() || !nv_family_known())
        return;
    term_puts("   gpu regs: BOOT0 ");
    hex32(nv_rd32(NV_PMC_BOOT_0));
    term_puts(" PMC_EN ");
    hex32(nv_rd32(NV_PMC_ENABLE));
    if (((nv_family() >> 20) & 0x1FF) >= 0x50) {
        term_puts(" CRTC0 ");
        hex32(nv_rd32(NV50_PDISPLAY_CRTC_CLK_CTRL(0)));
    }
    term_putc('\n');
}

// --- the display engine (1.7 part 3) --------------------------------------
// Curie cards: full ownership. Allocate a framebuffer of OUR OWN in VRAM,
// copy the firmware's picture into it, flip the CRTC onto it, then bring up
// the hardware cursor. nv50+ cards wait for EVO channels (part 4).

static int scanout_owned;
static int hw_cursor;

int gpu_display_init(void)
{
    if (!curie_display_init())
        return 0;
    uint32_t span = vram_fb_span();
    uint64_t va = vram_alloc(span + 2047);
    if (!va)
        return 0;
    va = (va + 2047) & ~(uint64_t)2047;
    const volatile uint8_t *src = (const volatile uint8_t *)gfx_fb_addr();
    volatile uint8_t *dst = (volatile uint8_t *)va;
    for (uint32_t i = 0; i < span; i++)
        dst[i] = src[i];                       // the picture rides across
    if (!curie_flip((uint32_t)(va - nv_bar1())))
        return 0;
    gfx_remap_fb(va);
    scanout_owned = 1;
    hw_cursor = curie_cursor_init();
    return 1;
}

int gpu_hw_cursor(void)
{
    return hw_cursor;
}

void gpu_cursor_move(int x, int y)
{
    curie_cursor_move(x, y);
}
