#ifndef OPENOS_VRAM_H
#define OPENOS_VRAM_H

#include <stdint.h>

// VRAM aperture ownership (1.7 part 2): map the card's memory through the
// BAR1 window, redraw the firmware framebuffer through our own mapping,
// and hand out VRAM for what comes next (hardware cursor image, 2D ops).

int      vram_init(void);          // call once after gpu_probe + gfx_init
int      vram_taken_over(void);    // 1 = gfx draws through the aperture
uint64_t vram_alloc(uint32_t bytes); // kernel VA from the top of VRAM, 0 = full
uint32_t vram_fb_span(void);       // framebuffer byte size inside the aperture
void     vram_info(void);          // the gpuvram command body

#endif
