#!/usr/bin/env python3
"""Decode text from a QEMU screendump (.ppm) of the OpenOS screen.

Every console character is an 8x16 cell drawn with the kernel's Lat15
Fixed16 font (kernel/font.c), so we can read the screen back by matching
cell bitmaps against the font. Light theme: ink pixels are DARK.

The console grid can sit at any pixel offset (window chrome), so we try
all 8x16 offsets and keep the one where the most cells match a glyph.

Usage: readscreen.py shot.ppm [--all]
Only rows with text are printed unless --all is given.
"""
import re
import sys

CW, CH = 8, 16


def load_font(path):
    src = open(path, encoding="utf-8").read()
    body = src[src.index("font8x16["):]
    glyphs = []
    for row in re.findall(r"\{([^}]*)\}", body):
        vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", row)]
        if len(vals) == CH:
            glyphs.append(vals)
    if len(glyphs) != 95:
        sys.exit(f"font parse failed: got {len(glyphs)} glyphs, wanted 95")
    return {chr(32 + i): g for i, g in enumerate(glyphs)}


def load_ppm(path):
    data = open(path, "rb").read()
    if not data.startswith(b"P6"):
        sys.exit("not a raw PPM")
    parts = data.split(None, 4)  # magic, w, h, maxval, raster
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][: w * h * 3]


def bitgrid(px, w, h):
    """One bit per pixel: 1 = ink. Light theme means any clearly
    non-white pixel counts (yellow dir names are ink too)."""
    bits = bytearray(w * h)
    for i in range(w * h):
        o = i * 3
        if px[o] < 200 or px[o + 1] < 200 or px[o + 2] < 200:
            bits[i] = 1
    return bits


def cell_rows(bits, w, x0, y0):
    """The 16 bitmap rows of the cell at pixel (x0, y0)."""
    rows = []
    for y in range(y0, y0 + CH):
        base = y * w + x0
        b = 0
        for x in range(CW):
            b = (b << 1) | bits[base + x]
        rows.append(b)
    return tuple(rows)


def best_glyph(rows, exact, font):
    if not any(rows):
        return " "
    ch = exact.get(rows)
    if ch and ch != " ":
        return ch
    best, best_err = "?", 6
    for c, g in font.items():
        if c == " ":
            continue
        err = sum(bin(a ^ b).count("1") for a, b in zip(rows, g))
        if err < best_err:
            best, best_err = c, err
    return best if best_err <= 4 else "?"


def find_grid(bits, w, h, font):
    """Score every 8x16 offset by how many cells exactly match a glyph."""
    exact = {tuple(g): c for c, g in font.items()}
    exact.pop(tuple(font[" "]), None)       # blank cell is not evidence
    best = (0, (0, 0))
    cols, rows_n = w // CW, h // CH
    for dy in range(CH):
        for dx in range(CW):
            hits = 0
            for gy in range(1, rows_n - 1, 3):   # sample: every 3rd row
                for gx in range(1, cols - 1, 3):  # and 3rd column
                    r = cell_rows(bits, w, dx + gx * CW, dy + gy * CH)
                    if r in exact:
                        hits += 1
            if hits > best[0]:
                best = (hits, (dx, dy))
    return best[1]


def read(path, font_path, show_all=False):
    font = load_font(font_path)
    exact = {tuple(g): c for c, g in font.items()}
    w, h, px = load_ppm(path)
    bits = bitgrid(px, w, h)
    dx, dy = find_grid(bits, w, h, font)
    out = []
    y = dy
    while y + CH <= h:
        line = []
        x = dx
        while x + CW <= w:
            line.append(best_glyph(cell_rows(bits, w, x, y), exact, font))
            x += CW
        out.append("".join(line).rstrip())
        y += CH
    return out if show_all else [l for l in out if l.strip()]


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    root = __file__.rsplit("/", 2)[0]      # repo root from tools/
    for line in read(sys.argv[1], root + "/kernel/font.c",
                     "--all" in sys.argv):
        print(line)
