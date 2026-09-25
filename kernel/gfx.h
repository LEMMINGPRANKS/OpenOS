#ifndef OPENOS_GFX_H
#define OPENOS_GFX_H

#include <stdint.h>

int gfx_init(unsigned long mb2_addr);         // parse the mb2 framebuffer tag
int gfx_available(void);
uint32_t gfx_width(void);
uint32_t gfx_height(void);

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

#endif
