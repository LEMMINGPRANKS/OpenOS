#ifndef OPENOS_CURIE_H
#define OPENOS_CURIE_H

#include <stdint.h>

// Curie-family (pre-nv50) display engine: direct-MMIO scanout ownership
// and the hardware cursor. nv50+ cards (MCP89, RTX) use EVO channels
// instead -- that's part 4 of the 1.7 GPU arc.

int      curie_display_init(void);   // find our CRTC; 1 = we drive display
int      curie_active(void);
uint32_t curie_scanout_offset(void); // what NV_PCRTC_START says right now
int      curie_flip(uint32_t vram_off); // move scanout to our own buffer
int      curie_cursor_init(void);    // 64x64 ARGB hw cursor live
void     curie_cursor_move(int x, int y);

#endif
