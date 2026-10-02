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

// --- Curie-family display (pre-nv50: the tower's 7300 GT generation).
// Direct MMIO, no command channels -- unlike nv50+ EVO. Offsets verified
// against nouveau dispnv04/nvreg.h. Heads live 0x2000 apart.
#define NV_CRTC_HEAD_STRIDE            0x2000

#define NV_PCRTC_START                 0x600800  // scanout base (VRAM offset)
#define NV_PCRTC_CURSOR_CONFIG         0x600810
#define NV_PCRTC_CC_ENABLE             (1u << 0)
#define NV_PCRTC_CC_DOUBLE_SCAN        (1u << 4)
#define NV_PCRTC_CC_ADDR_SPACE_PNVM    (1u << 8)  // image lives in VRAM
#define NV_PCRTC_CC_CUR_BPP_32         (1u << 12) // ARGB8888
#define NV_PCRTC_CC_CUR_PIXELS_64      (1u << 16)
#define NV_PCRTC_CC_CUR_LINES_64       (4u << 24)

// cursor image address, via the VGA CRT controller's extended indices
#define NV_PRMCIO_CRX                  0x6013D4  // CRTC index port (byte)
#define NV_CIO_HCUR_ADDR2              0x2F      // offset bits 24..31
#define NV_CIO_HCUR_ADDR0              0x30      // ASI | offset bits 17..23
#define NV_CIO_HCUR_ASI                0x80
#define NV_CIO_HCUR_ADDR1              0x31      // enable | offset bits 11..17
#define NV_CIO_HCUR_ENABLE             0x01

// cursor position: x 15:0, y 31:16 (RAMDAC block, same head stride)
#define NV_PRAMDAC_CU_START_POS        0x680300

#endif
