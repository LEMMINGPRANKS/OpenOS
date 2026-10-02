#ifndef OPENOS_GPU_H
#define OPENOS_GPU_H

// The GPU layer (1.6 arc part 3). For now: probe + identify, and drawing
// keeps going through the firmware framebuffer via gfx.c -- the software
// backend. Later parts hang accelerated backends (MCP89 nv50 init,
// push-buffer 2D, and one day 3D) off this same interface so apps and
// the 3D engine never care which GPU they sit on.

int gpu_probe(void);            // call once at boot
const char *gpu_ident(void);    // card name, or the software-backend line
void gpu_info(void);            // the gpuinfo command body
void gpu_regs(void);            // live register dump (read-only)
void gpu_boot_log(void);        // one compact boot-log line (-> serial)

#endif
