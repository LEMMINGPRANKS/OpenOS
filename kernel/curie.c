#include "curie.h"
#include "nv.h"
#include "nvregs.h"
#include "vram.h"
#include "gfx.h"

// The Curie-family display engine (pre-nv50 -- the tower's GeForce 7300 GT).
// Unlike nv50+ EVO, everything here is direct MMIO: the CRTC scans out from
// NV_PCRTC_START, and the hardware cursor is three VGA-indexed address
// registers + one config register + one position register. All offsets and
// bit fields verified against nouveau's dispnv04 (linux-7.2.4).

static int head = -1;              // the CRTC that scans out our framebuffer
static uint64_t cursor_va;         // 64x64 ARGB image, VRAM (0 = off)

static uint32_t crtc(uint32_t reg)
{
    return reg + (uint32_t)head * NV_CRTC_HEAD_STRIDE;
}

static void cio_write(uint8_t idx, uint8_t val)
{
    uint32_t port = NV_PRMCIO_CRX + (uint32_t)head * NV_CRTC_HEAD_STRIDE;
    nv_wr8(port, idx);
    nv_wr8(port + 1, val);
}

int curie_active(void)
{
    return head >= 0;
}

// Find the CRTC that scans out our (already aperture-owned) framebuffer.
// Read-only until we're sure which head is ours.
int curie_display_init(void)
{
    head = -1;
    cursor_va = 0;
    if (!nv_found() || !nv_family_known() || !vram_taken_over())
        return 0;
    if (((nv_family() >> 20) & 0x1FF) >= 0x50)
        return 0;                  // nv50+: EVO channels, part 4's job
    uint64_t fb = gfx_fb_addr();
    for (int h = 0; h < 2; h++) {
        uint32_t start = nv_rd32(NV_PCRTC_START + (uint32_t)h * NV_CRTC_HEAD_STRIDE);
        if ((uint64_t)nv_bar1() + start == fb) {
            head = h;
            break;
        }
    }
    return head >= 0;
}

uint32_t curie_scanout_offset(void)
{
    return head >= 0 ? nv_rd32(crtc(NV_PCRTC_START)) : 0;
}

// Flip scanout to another VRAM offset. The CRTC obeys on the next frame.
int curie_flip(uint32_t vram_off)
{
    if (head < 0)
        return 0;
    nv_wr32(crtc(NV_PCRTC_START), vram_off);
    return nv_rd32(crtc(NV_PCRTC_START)) == vram_off;
}

// The classic arrow (2x the desktop sprite's size for 64x64), opaque black
// with a white drop shadow so it reads on any background.
static const unsigned char arrow8[8] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
};

int curie_cursor_init(void)
{
    if (head < 0)
        return 0;
    uint64_t va = vram_alloc(64 * 64 * 4 + 2047);
    if (!va)
        return 0;
    cursor_va = (va + 2047) & ~(uint64_t)2047;   // CIO addr regs: 2 KiB steps
    uint32_t off = (uint32_t)(cursor_va - nv_bar1());

    volatile uint32_t *img = (volatile uint32_t *)cursor_va;
    for (int i = 0; i < 64 * 64; i++)
        img[i] = 0x00000000;                     // transparent
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if (arrow8[r] & (0x80 >> c)) {
                img[(r + 1) * 64 + (c + 1)] = 0xFFFFFFFF;  // white drop shadow
                img[r * 64 + c] = 0xFF28282E;              // opaque ink on top
            }

    cio_write(NV_CIO_HCUR_ADDR2, (uint8_t)(off >> 24));
    cio_write(NV_CIO_HCUR_ADDR0, (uint8_t)(NV_CIO_HCUR_ASI | ((off >> 17) & 0x7F)));
    cio_write(NV_CIO_HCUR_ADDR1,
              (uint8_t)(NV_CIO_HCUR_ENABLE | (((off >> 11) & 0x3F) << 2)));
    nv_wr32(crtc(NV_PCRTC_CURSOR_CONFIG),
            NV_PCRTC_CC_ENABLE | NV_PCRTC_CC_CUR_BPP_32 |
            NV_PCRTC_CC_ADDR_SPACE_PNVM |
            NV_PCRTC_CC_CUR_PIXELS_64 | NV_PCRTC_CC_CUR_LINES_64);
    curie_cursor_move(0, 0);       // Curie bug: CIO cursor writes need a
    return 1;                      // position poke to take effect
}

void curie_cursor_move(int x, int y)
{
    if (head < 0)
        return;
    nv_wr32(NV_PRAMDAC_CU_START_POS + (uint32_t)head * NV_CRTC_HEAD_STRIDE,
            ((uint32_t)x & 0xFFFF) | (((uint32_t)y & 0xFFFF) << 16));
}
