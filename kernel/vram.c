#include "vram.h"
#include "nv.h"
#include "gfx.h"
#include "term.h"

// The VRAM layer (1.7 part 2). The firmware set a framebuffer somewhere in
// video memory before we existed; the aperture (BAR1) is a window that maps
// the card's whole VRAM into our address space. Takeover = find the firmware
// framebuffer inside the window and redraw through OUR mapping of the same
// physical bytes -- the desktop keeps lighting up, but through memory we own.
// Allocations hand out from the TOP of the aperture downward, far from the
// firmware framebuffer, and never hand out memory twice.

static uint64_t aperture_base;      // physical, 0 = no reachable BAR1
static uint32_t aperture_size;
static uint32_t cursor;             // next allocation grows DOWN to here
static uint64_t fb_off;             // firmware framebuffer offset in VRAM
static uint32_t fb_span;            // its byte size
static int took_over;               // 1 = gfx draws through the aperture now

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

int vram_init(void)
{
    aperture_base = nv_bar1();
    aperture_size = nv_bar_size(1);
    if (!aperture_base || !aperture_size)
        return 0;
    cursor = aperture_size;

    // Is the firmware framebuffer one of ours? (VBE put it in VRAM.)
    if (gfx_available()) {
        uint64_t span = (uint64_t)gfx_width() * gfx_height() * 4; // worst case bpp
        uint64_t fb = gfx_fb_addr();
        if (fb >= aperture_base && fb + span <= aperture_base + aperture_size) {
            fb_off = fb - aperture_base;
            fb_span = (uint32_t)span;
            gfx_remap_fb((volatile uint8_t *)aperture_base + fb_off);
            took_over = 1;
        }
    }
    return took_over;
}

// Bump allocator, top-down. No free() -- VRAM is freed by rebooting.
// Returns a kernel virtual address, 0 if it doesn't fit.
uint64_t vram_alloc(uint32_t bytes)
{
    if (!aperture_base || bytes > cursor)
        return 0;
    cursor -= bytes;
    return aperture_base + cursor;
}

int vram_taken_over(void) { return took_over; }
uint32_t vram_fb_span(void) { return fb_span; }

// The gpuvram command body
void vram_info(void)
{
    if (!nv_found()) {
        term_puts("vram     : (software backend -- no card memory to own)\n");
        return;
    }
    if (!aperture_base) {
        term_puts("vram     : (BAR1 above 4 GiB -- aperture unreachable)\n");
        return;
    }
    term_puts("aperture : ");
    hex32((uint32_t)aperture_base);
    term_puts(" (");
    dec32(aperture_size >> 20);
    term_puts(" MiB)\n");
    term_puts("scanout  : ");
    if (fb_span) {
        term_puts("firmware fb at +");
        hex32(fb_off);
        term_puts(" (");
        dec32(fb_span >> 10);
        term_puts(" KiB) -- OWNED, drawing through the aperture\n");
    } else {
        term_puts("no framebuffer inside the aperture (borrowed mapping)\n");
    }
    term_puts("free     : ");
    dec32(cursor >> 10);
    term_puts(" KiB (top-down bump)\n");
}
