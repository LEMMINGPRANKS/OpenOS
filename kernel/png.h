#ifndef OPENOS_PNG_H
#define OPENOS_PNG_H

#include <stdint.h>

// Real PNG files: 8-bit RGB, no interlace. The zlib stream uses stored
// (uncompressed) deflate blocks, so any real image viewer can open what
// we write -- and we can read our own files back without an inflater.

// rgb = w*h*3 bytes (0x00RRGGBB per pixel). Returns file size, or -1 if
// it doesn't fit in out_max.
int32_t png_encode(const uint8_t *rgb, uint32_t w, uint32_t h,
                   uint8_t *out, uint32_t out_max);

// Decodes into rgb (w*h*3). Only stored-block PNGs (ours); compressed
// PNGs from the internet return -2. -1 = malformed.
int32_t png_decode(const uint8_t *file, uint32_t n, uint8_t *rgb,
                   uint32_t rgb_max, uint32_t *w_out, uint32_t *h_out);

#endif
