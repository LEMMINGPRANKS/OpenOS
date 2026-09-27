#include "png.h"

// PNG, written from scratch. Chunks are length+type+data+CRC32; the pixel
// data rides inside a zlib stream, and we always use deflate "stored"
// blocks (BTYPE=00), which are legal-but-uncompressed -- so the files are
// genuine PNGs everywhere, and decoding needs no inflater.

#define PNG_SIG_LEN 8

static const uint8_t png_sig[PNG_SIG_LEN] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

static uint32_t crc_table_ready;
static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    if (!crc_table_ready)
        crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}



static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_chunk(uint8_t *out, uint32_t *o, const char *type,
                      const uint8_t *data, uint32_t n)
{
    be32(out + *o, n);
    out[*o + 4] = (uint8_t)type[0];
    out[*o + 5] = (uint8_t)type[1];
    out[*o + 6] = (uint8_t)type[2];
    out[*o + 7] = (uint8_t)type[3];
    for (uint32_t i = 0; i < n; i++)
        out[*o + 8 + i] = data[i];
    uint32_t crc = crc32(out + *o + 4, n + 4);
    be32(out + *o + 8 + n, crc);
    *o += 12 + n;
}

int32_t png_encode(const uint8_t *rgb, uint32_t w, uint32_t h,
                   uint8_t *out, uint32_t out_max)
{
    // exact output size, so the caller can size the buffer up front
    uint32_t raw = h * (1 + w * 3);
    uint32_t blocks = (raw + 65534) / 65535;
    uint32_t idat = 2 + blocks * 5 + raw + 4;
    uint32_t total = PNG_SIG_LEN + 25 + 12 + idat + 12;
    if (total > out_max || w == 0 || h == 0 || w > 0x7FFFFFFFu)
        return -1;

    uint32_t o = 0;
    for (int i = 0; i < PNG_SIG_LEN; i++)
        out[o++] = png_sig[i];

    uint8_t ihdr[13];
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;                    // bit depth
    ihdr[9] = 2;                    // colour type: truecolour RGB
    ihdr[10] = 0;                   // deflate
    ihdr[11] = 0;                   // adaptive filtering
    ihdr[12] = 0;                   // no interlace
    put_chunk(out, &o, "IHDR", ihdr, 13);

    // IDAT chunk: header first, then the zlib stream written inline
    uint32_t idat_len = idat;
    be32(out + o, idat_len);
    out[o + 4] = 'I';
    out[o + 5] = 'D';
    out[o + 6] = 'A';
    out[o + 7] = 'T';
    uint32_t idat_data = o + 8;
    o = idat_data;
    out[o++] = 0x78;                        // CMF: deflate, 32K window
    out[o++] = 0x01;                        // FLG (check bits valid)
    uint32_t sent = 0;
    while (sent < raw) {
        uint32_t blk = raw - sent;
        if (blk > 65535)
            blk = 65535;
        uint8_t final = (sent + blk == raw) ? 1 : 0;
        out[o++] = final;
        out[o++] = (uint8_t)(blk & 0xFF);
        out[o++] = (uint8_t)(blk >> 8);
        out[o++] = (uint8_t)(~blk & 0xFF);
        out[o++] = (uint8_t)((~blk >> 8) & 0xFF);
        // interleave the filter-byte-per-row into the block data
        uint32_t put = 0;
        while (put < blk) {
            uint32_t pos = sent + put;      // index into the raw stream
            uint32_t row = pos / (1 + w * 3);
            uint32_t col = pos % (1 + w * 3);
            out[o++] = col == 0 ? 0 : rgb[(row * w + (col - 1) / 3) * 3 +
                                          ((col - 1) % 3)];
            put++;
        }
        sent += blk;
    }
    // adler32 over the raw scanline stream, stored big-endian
    uint32_t a = 1, b = 0;
    for (uint32_t row = 0; row < h; row++) {
        a = (a + 0) % 65521u;               // filter byte 0
        b = (b + a) % 65521u;
        const uint8_t *px = rgb + (uint64_t)row * w * 3;
        for (uint32_t i = 0; i < w * 3; i++) {
            a = (a + px[i]) % 65521u;
            b = (b + a) % 65521u;
        }
    }
    out[o++] = (uint8_t)(b >> 8);
    out[o++] = (uint8_t)b;
    out[o++] = (uint8_t)(a >> 8);
    out[o++] = (uint8_t)a;
    be32(out + idat_data + idat_len, crc32(out + idat_data - 4, idat_len + 4));
    o += 4;

    put_chunk(out, &o, "IEND", (const uint8_t *)"", 0);
    return (int32_t)o;
}

int32_t png_decode(const uint8_t *file, uint32_t n, uint8_t *rgb,
                   uint32_t rgb_max, uint32_t *w_out, uint32_t *h_out)
{
    if (n < PNG_SIG_LEN + 8 + 13 + 12)
        return -1;
    for (int i = 0; i < PNG_SIG_LEN; i++)
        if (file[i] != png_sig[i])
            return -1;

    uint32_t w = 0, h = 0;
    uint8_t depth = 0, ctype = 0, interlace = 0;
    int have_ihdr = 0;
    const uint8_t *z = 0;
    uint32_t zn = 0;

    uint32_t p = PNG_SIG_LEN;
    while (p + 12 <= n) {
        uint32_t len = rd_be32(file + p);
        if (p + 12 + len > n)
            return -1;
        const uint8_t *data = file + p + 8;
        if (data[-4] == 'I' && data[-3] == 'H' && data[-2] == 'D' &&
            data[-1] == 'R') {
            if (len != 13)
                return -1;
            w = rd_be32(data);
            h = rd_be32(data + 4);
            depth = data[8];
            ctype = data[9];
            interlace = data[12];
            have_ihdr = 1;
        } else if (data[-4] == 'I' && data[-3] == 'D' && data[-2] == 'A' &&
                   data[-1] == 'T') {
            z = data;                    // concatenated IDATs: we take the
            zn = len;                    // first one (ours is a single IDAT)
        } else if (data[-4] == 'I' && data[-3] == 'E' && data[-2] == 'N' &&
                   data[-1] == 'D') {
            break;
        }
        p += 12 + len;
    }
    if (!have_ihdr || !z || zn < 8)
        return -1;
    if (depth != 8 || (ctype != 2 && ctype != 6) || interlace != 0)
        return -2;                       // beyond us (for now)
    uint32_t bpp = ctype == 6 ? 4 : 3;
    if ((uint64_t)w * h * 3 > rgb_max)
        return -1;

    if (z[0] != 0x78)
        return -2;
    uint32_t ip = 2;
    uint32_t op = 0;                     // index into rgb
    for (;;) {
        if (ip >= zn)
            return -1;
        uint8_t hdr = z[ip++];
        if ((hdr >> 1 & 3) != 0)
            return -2;                   // compressed block: no inflater yet
        uint8_t final = hdr & 1;
        if (ip + 4 > zn)
            return -1;
        uint32_t blk = z[ip] | ((uint32_t)z[ip + 1] << 8);
        if (ip + 4 + blk > zn)
            return -1;
        ip += 4;
        for (uint32_t i = 0; i < blk; i++) {
            uint8_t byte = z[ip + i];
            uint32_t stride = 1 + w * bpp;
            uint32_t pos = op;
            uint32_t row = pos / stride, col = pos % stride;
            if (col == 0) {
                if (byte != 0)
                    return -2;           // filtered scanline
            } else {
                uint32_t idx = (col - 1) % bpp;
                if (idx < 3)
                    rgb[(row * w + (col - 1) / bpp) * 3 + idx] = byte;
            }
            op++;
        }
        ip += blk;
        if (final)
            break;
        if (op >= h * (1 + w * bpp))
            break;
    }
    *w_out = w;
    *h_out = h;
    return 0;
}
