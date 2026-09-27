#!/usr/bin/env python3
"""mkstore: seed the DR1 store from the initrd/ files, at BUILD time.

Builds the exact on-disk format kernel/store.c reads:
  LBA 2048  superblock  "OPENOSST" + version + file count
  LBA 2049  entry table (8 sectors): name[64], lba, size, sectors
  LBA 2057+ one .WMBG archive per file (QuantumSquish method 0 = store)

Usage: python3 tools/mkstore.py [initrd_dir] [store.img]
The Makefile runs this and stamps the result into openos.img at LBA 2048,
so OpenOS boots with every file already living on DR1 -- no initramfs.
"""
import os
import struct
import sys

INITRD_DIR = sys.argv[1] if len(sys.argv) > 1 else "initrd"
OUT = sys.argv[2] if len(sys.argv) > 2 else "store.img"

STORE_LBA = 2048
STORE_TABLE_SECTORS = 8
STORE_DATA_LBA = STORE_LBA + 1 + STORE_TABLE_SECTORS
STORE_MAX_SECTORS = 1024                 # 512 KiB, matches store.h
RAMFS_NAME_MAX = 64
WMBG_HDR = 14

SECTOR = 512


def collect_files(root):
    out = []
    for dirpath, _dirs, files in os.walk(root):
        for f in sorted(files):
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            out.append(("/" + rel, full))
    return sorted(out)


def main():
    files = collect_files(INITRD_DIR)
    if not files:
        sys.exit(f"mkstore: nothing found in {INITRD_DIR}/")

    data = b""
    entries = []
    for name, path in files:
        raw = open(path, "rb").read()
        if len(name) >= RAMFS_NAME_MAX:
            sys.exit(f"mkstore: name too long ({RAMFS_NAME_MAX} max): {name}")
        arch = b"WMBG" + bytes([1, 0]) + struct.pack("<Q", len(raw)) + raw
        sect = (len(arch) + SECTOR - 1) // SECTOR
        entries.append((name, STORE_DATA_LBA + len(data) // SECTOR,
                        len(raw), sect))
        data += arch.ljust(sect * SECTOR, b"\0")

    total_sectors = 1 + STORE_TABLE_SECTORS + len(data) // SECTOR
    if total_sectors > STORE_MAX_SECTORS:
        sys.exit(f"mkstore: store too big ({total_sectors} > "
                 f"{STORE_MAX_SECTORS} sectors)")

    img = bytearray(STORE_MAX_SECTORS * SECTOR)
    sb = struct.pack("<8sII", b"OPENOSST", 1, len(entries))
    img[0:len(sb)] = sb
    off = SECTOR
    for name, lba, size, sect in entries:
        e = struct.pack(f"<{RAMFS_NAME_MAX}sIII",
                        name.encode(), lba, size, sect)
        img[off:off + len(e)] = e
        off += len(e)
    img[(1 + STORE_TABLE_SECTORS) * SECTOR:
        (1 + STORE_TABLE_SECTORS) * SECTOR + len(data)] = data

    open(OUT, "wb").write(img)
    print(f"mkstore: {len(entries)} files, "
          f"{total_sectors} sectors -> {OUT}")
    for name, _lba, size, _sect in entries:
        print(f"  {name} ({size} bytes)")


if __name__ == "__main__":
    main()
