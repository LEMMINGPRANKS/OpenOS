#ifndef OPENOS_GFX_H
#define OPENOS_GFX_H

#include <stdint.h>

int gfx_init(unsigned long mb2_addr);         // parse the mb2 framebuffer tag
int gfx_available(void);
uint32_t gfx_width(void);
uint32_t gfx_height(void);
uint64_t gfx_fb_addr(void);                   // current framebuffer base
void gfx_remap_fb(volatile uint8_t *new_fb);  // VRAM takeover hook

uint32_t gfx_rgb(uint32_t rgb);              // 0x00RRGGBB -> pixel value
void gfx_pixel(uint32_t x, uint32_t y, uint32_t rgb);
uint32_t gfx_read_pixel(uint32_t x, uint32_t y);      // raw fb value
void gfx_write_pixel(uint32_t x, uint32_t y, uint32_t raw);
void gfx_char_fg(uint32_t x, uint32_t y, char c, uint32_t fg);  // transparent bg
void gfx_text_fg(uint32_t x, uint32_t y, const char *s, uint32_t fg);
void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void gfx_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void gfx_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
void gfx_copy(uint32_t dx, uint32_t dy, uint32_t sx, uint32_t sy,
              uint32_t w, uint32_t h);

// 2015-OS chrome: rounded rects, alpha blending, soft shadows
uint32_t gfx_read_rgb(uint32_t x, uint32_t y);          // pixel as 0x00RRGGBB
void gfx_blend_pixel(uint32_t x, uint32_t y, uint32_t rgb, uint32_t alpha);
void gfx_fill_rect_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t rgb, uint32_t alpha);
void gfx_fill_rect_r(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint32_t rad, uint32_t rgb);
void gfx_fill_rect_r_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           uint32_t rad, uint32_t rgb, uint32_t alpha);
void gfx_rect_r(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                uint32_t rad, uint32_t rgb);
void gfx_shadow_r(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t rad);
void gfx_fill_rect_top_r(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t rad, uint32_t rgb);
void gfx_fill_circle(uint32_t cx, uint32_t cy, uint32_t rad, uint32_t rgb);

#endif
void gfx_text_scaled(uint32_t x, uint32_t y, const char *s, uint32_t rgb,
                     int scale, int shear);
