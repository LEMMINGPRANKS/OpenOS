#!/usr/bin/env python3
"""mkdisk: assemble OpenOS's dual-boot disk images (the layout.inc law).

Modes:
  boot  mkdisk.py boot openos.img mbr.bin stage2.bin other.bin kernel.flat store-seed.img
  store mkdisk.py store store.img store-seed.img

Both images get a REAL MBR partition table. In boot images partition 1 is
the other OS (fake PBR for QEMU, type 0x83) starting at LBA 2048 -- the
post-MBR gap stays untouched so a real GRUB's core.img keeps working --
and the OpenOS partition (type 0x7F, bootable) starts 1 MiB-aligned near
the end. Store images have one OpenOS-type partition. All OpenOS blobs
are stamped at partition_base + the partition-relative LBAs from
bios/layout.inc.
"""
import struct
import sys

SECT = 512
# bios/layout.inc law (partition-relative LBAs)
STAGE2_LBA = 0                   # stage2 IS the partition boot sector
KSLOT_A_LBA = 128
STORE_LBA = 2048
OPENOS_TYPE = 0x7F


def entry(bootable, ptype, start, count):
    return struct.pack("<B3sB3sII", bootable, b"\x00" * 3,
                       ptype, b"\x00" * 3, start, count)


def stamp(img, lba, data):
    off = lba * SECT
    end = off + len(data)
    if end > len(img):
        raise SystemExit(f"mkdisk: stamp at LBA {lba} overflows the image")
    img[off:end] = data


def build_boot(dst, mbr_path, stage2_path, other_path, kernel_path, seed_path):
    size = 16 * 1024 * 1024
    total = size // SECT
    img = bytearray(size)

    other_start, other_count = 2048, 8192          # 4 MiB test partition
    opos_start = 12288                               # 6 MiB, 1 MiB-aligned
    opos_count = total - opos_start                  # 10 MiB

    mbr = bytearray(open(mbr_path, "rb").read())
    if len(mbr) != SECT:
        raise SystemExit(f"mkdisk: {mbr_path} is not exactly one sector")
    table = (entry(0x00, 0x83, other_start, other_count) +
             entry(0x80, OPENOS_TYPE, opos_start, opos_count) +
             entry(0, 0, 0, 0) + entry(0, 0, 0, 0))
    mbr[446:510] = table                             # code area stays intact

    stamp(img, 0, mbr)
    stamp(img, other_start, open(other_path, "rb").read())
    stamp(img, opos_start + STAGE2_LBA, open(stage2_path, "rb").read())
    stamp(img, opos_start + KSLOT_A_LBA, open(kernel_path, "rb").read())
    stamp(img, opos_start + STORE_LBA, open(seed_path, "rb").read())
    open(dst, "wb").write(img)
    print(f"mkdisk: {dst} -- other OS at LBA {other_start}, "
          f"OpenOS partition at LBA {opos_start} ({opos_count} sectors)")


def build_store(dst, seed_path):
    size = 16 * 1024 * 1024
    total = size // SECT
    img = bytearray(size)

    opos_start, opos_count = 2048, total - 2048      # 1 MiB-aligned

    mbr = bytearray(SECT)
    mbr[446:510] = (entry(0x80, OPENOS_TYPE, opos_start, opos_count) +
                    entry(0, 0, 0, 0) * 3)
    mbr[510:512] = b"\x55\xAA"

    stamp(img, 0, mbr)
    stamp(img, opos_start + STORE_LBA, open(seed_path, "rb").read())
    open(dst, "wb").write(img)
    print(f"mkdisk: {dst} -- OpenOS store partition at LBA {opos_start}")


def has_openos_part(sector):
    if len(sector) < SECT or sector[510:512] != b"\x55\xAA":
        return False
    return any(sector[446 + i * 16 + 4] == OPENOS_TYPE for i in range(4))


def migrate(path):
    """Old-style store.img (store at absolute LBA 2048, no table) ->
    partitioned layout (OpenOS partition at 2048, store at 2048+2048).
    Saved files keep working; already-migrated images are left alone."""
    old = open(path, "rb").read()
    mbr = old[:SECT]
    if has_openos_part(mbr):
        return
    size = 16 * 1024 * 1024
    total = size // SECT
    img = bytearray(size)
    img[:SECT] = mbr                                 # keep any old MBR code
    img[446:510] = (entry(0x80, OPENOS_TYPE, 2048, total - 2048) +
                    entry(0, 0, 0, 0) * 3)
    img[510:512] = b"\x55\xAA"
    store = old[2048 * SECT:2048 * SECT + 8192 * SECT]   # STORE_MAX_SECTORS law
    stamp(img, 2048 + STORE_LBA, store)
    open(path, "wb").write(img)
    print(f"mkdisk: {path} -- migrated old-style store to the partitioned layout")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "boot" and len(sys.argv) == 8:
        build_boot(*sys.argv[2:])
    elif len(sys.argv) == 4 and sys.argv[1] == "store":
        build_store(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3 and sys.argv[1] == "migrate":
        migrate(sys.argv[2])
    else:
        raise SystemExit(__doc__)
