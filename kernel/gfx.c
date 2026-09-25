#include "gfx.h"
#include "font.h"

// Linear framebuffer graphics. GRUB sets a VBE mode for us (via the
// framebuffer tag in our multiboot2 header) and reports where it lives.
// We support what GRUB hands us: 24 or 32 bpp direct RGB.

//
// multiboot2 framebuffer tag layout:
//   +8  u64 addr
//   +16 u32 pitch
//   +20 u32 width
//   +24 u32 height
//   +28 u32 bpp
//   +32 u8  fb_type (2 = direct RGB)
//   +34 u8  red_pos, +35 red_size, +36 green_pos, +37 green_size,
//   +38 u8  blue_pos, +39 blue_size

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};


static volatile uint8_t *fb;
static uint32_t fb_w, fb_h, fb_pitch, fb_bpp;
static uint32_t bpp_bytes;
static uint8_t r_pos, g_pos, b_pos;
static int ok;

static uint8_t dbg_found, dbg_type;

void gfx_dbg(uint8_t *found, uint8_t *type, uint8_t *bpp)
{
    *found = dbg_found;
    *type = dbg_type;
    *bpp = (uint8_t)fb_bpp;
}

int gfx_init(unsigned long mb2_addr)
{
    uint32_t total = *(volatile uint32_t *)mb2_addr;
    uint64_t p = mb2_addr + 8;
    while (p < mb2_addr + total) {
        volatile struct mb2_tag *tag = (volatile struct mb2_tag *)p;
        if (tag->type == 0)
            break;
        if (tag->type == 8) {
            uint64_t addr = *(volatile uint64_t *)(p + 8);
            fb_pitch = *(volatile uint32_t *)(p + 16);
            fb_w     = *(volatile uint32_t *)(p + 20);
            fb_h     = *(volatile uint32_t *)(p + 24);
            fb_bpp   = *(volatile uint8_t *)(p + 28);
            uint8_t fb_type = *(volatile uint8_t *)(p + 29);
            // spec: framebuffer_type 0 = indexed, 1 = direct RGB, 2 = EGA
            if ((fb_bpp == 24 || fb_bpp == 32) && fb_type == 1) {
                r_pos = *(volatile uint8_t *)(p + 32);
                g_pos = *(volatile uint8_t *)(p + 34);
                b_pos = *(volatile uint8_t *)(p + 36);
                bpp_bytes = fb_bpp / 8;
                fb = (volatile uint8_t *)addr;
                ok = 1;
            }
            return ok;
        }
        p = (p + tag->size + 7) & ~(uint64_t)7;
    }
    return 0;
}

int gfx_available(void) { return ok; }
uint32_t gfx_width(void) { return fb_w; }
uint32_t gfx_height(void) { return fb_h; }

uint32_t gfx_rgb(uint32_t rgb)
{
    uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    if (fb_bpp == 32)
        return (r << r_pos) | (g << g_pos) | (b << b_pos);
    return (r << 16) | (g << 8) | b;              // 24bpp: RGB in 3 bytes
}

static volatile uint8_t *px(uint32_t x, uint32_t y)
{
    return fb + y * fb_pitch + x * bpp_bytes;
}

void gfx_write_pixel(uint32_t x, uint32_t y, uint32_t raw)
{
    if (!ok || x >= fb_w || y >= fb_h)
        return;
    volatile uint8_t *p = px(x, y);
    p[0] = raw & 0xFF;
    p[1] = (raw >> 8) & 0xFF;
    if (bpp_bytes == 4)
        *(volatile uint32_t *)p = raw;
    else
        p[2] = (raw >> 16) & 0xFF;
}

uint32_t gfx_read_pixel(uint32_t x, uint32_t y)
{
    if (!ok || x >= fb_w || y >= fb_h)
        return 0;
    volatile uint8_t *p = px(x, y);
    if (bpp_bytes == 4)
        return *(volatile uint32_t *)p;
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

void gfx_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    gfx_write_pixel(x, y, gfx_rgb(rgb));
}

void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    if (!ok)
        return;
    uint32_t val = gfx_rgb(rgb);
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    for (uint32_t r = y; r < y + h; r++)
        for (uint32_t c = x; c < x + w; c++)
            gfx_write_pixel(c, r, val);
}

void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    for (uint32_t c = x; c < x + w; c++) {
        gfx_pixel(c, y, rgb);
        gfx_pixel(c, y + h - 1, rgb);
    }
    for (uint32_t r = y; r < y + h; r++) {
        gfx_pixel(x, r, rgb);
        gfx_pixel(x + w - 1, r, rgb);
    }
}

static const unsigned char *glyph_of(char c)
{
    if (c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT)
        c = '?';
    return font8x16[(uint8_t)c - FONT_FIRST];
}

void gfx_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg)
{
    if (!ok)
        return;
    const unsigned char *glyph = glyph_of(c);
    uint32_t fg_v = gfx_rgb(fg);
    uint32_t bg_v = gfx_rgb(bg);
    for (int r = 0; r < FONT_H; r++) {
        unsigned char bits = glyph[r];
        for (int col = 0; col < FONT_W; col++)
            gfx_write_pixel(x + col, y + r, (bits & (0x80 >> col)) ? fg_v : bg_v);
    }
}

void gfx_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s) {
        gfx_char(x, y, *s++, fg, bg);
        x += FONT_W;
    }
}

void gfx_char_fg(uint32_t x, uint32_t y, char c, uint32_t fg)
{
    if (!ok)
        return;
    const unsigned char *glyph = glyph_of(c);
    uint32_t fg_v = gfx_rgb(fg);
    for (int r = 0; r < FONT_H; r++) {
        unsigned char bits = glyph[r];
        for (int col = 0; col < FONT_W; col++)
            if (bits & (0x80 >> col))
                gfx_write_pixel(x + col, y + r, fg_v);
    }
}

void gfx_text_fg(uint32_t x, uint32_t y, const char *s, uint32_t fg)
{
    while (*s) {
        gfx_char_fg(x, y, *s++, fg);
        x += FONT_W;
    }
}

void gfx_copy(uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy,
              uint32_t w, uint32_t h)
{
    if (!ok)
        return;
    // copy downward when destination is below source, so rows don't
    // overwrite themselves mid-copy
    int down = (dy > sy);
    for (uint32_t i = 0; i < h; i++) {
        uint32_t r = down ? h - 1 - i : i;
        if (sy + r >= fb_h || dy + r >= fb_h)
            continue;
        volatile uint8_t *src = px(sx, sy + r);
        volatile uint8_t *dst = px(dx, dy + r);
        for (uint32_t c = 0; c < w && sx + c < fb_w && dx + c < fb_w; c++) {
            for (uint32_t b = 0; b < bpp_bytes; b++)
                dst[b] = src[b];
            src += bpp_bytes;
            dst += bpp_bytes;
        }
    }
}
