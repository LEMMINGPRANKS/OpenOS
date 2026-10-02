#ifndef OPENOS_NVREGS_H
#define OPENOS_NVREGS_H

// Named NVIDIA register offsets (BAR0-relative). 1.7 part 1 keeps to the
// offsets verified against the nouveau driver in the Linux source
// (linux-7.2.4, drivers/gpu/drm/nouveau/nvkm) -- never invented ones.
// Classic layout (nv04+, stable across every chip we target):

#define NV_PMC_BOOT_0     0x000000    // chip ID; family at bits 20..28
#define NV_PMC_INTR       0x000100    // pending interrupts (all units)
#define NV_PMC_INTR_EN    0x000140    // interrupt enable mask
#define NV_PMC_ENABLE     0x000200    // unit clock gates (mc/nv50.c)

#define NV_PFIFO_INTR     0x002100    // FIFO interrupt pending (fifo/nv50.c)
#define NV_PFIFO_INTR_EN  0x002140    // FIFO interrupt enable

// nv50 PDISPLAY block (nvkm/engine/disp/nv50.c). CRTC clock control tells
// us a scanout is alive; SOR control drives the panel/encoder.
#define NV50_PDISPLAY_CRTC_CLK_CTRL(n)  (0x610b70 + (n) * 0x10)
#define NV50_PDISPLAY_OUT_SLINK(n)      (0x61c004 + (n) * 0x80)

#endif
