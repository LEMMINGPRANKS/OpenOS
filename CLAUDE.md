# OpenOS

Freddie's brand-new **64-bit, open-source, from-scratch** operating system.
Written in C + a little assembly. Boots via GRUB (multiboot2), then climbs
into x86-64 long mode under its own power. Bat OS (32-bit) is the ancestor
and inspiration — OpenOS is the modern successor.

Freddie is BDFL. Open source by design.

## The device-register law (Freddie's design, 2026-09-25)

Device names are `<TYPE><number>`:

- **DR = drive register**: DR1 main drive, DR2 SD card slot, DR3 USB slot
- **IR = internal register**: IR1 RAM, IR2 initramfs
- **UR = unsupported register**: UR1 — watchdog trap that constantly checks
  for unsupported memory; when it fires, the kernel panics ON PURPOSE so the
  bad thing can never sneak past (red screen, deliberate halt)

Never invent other naming schemes. New devices join these registers.

## The ladder

- **Stage 1 — Kernel**
  - [x] Milestone 1: Boot into 64-bit long mode, print hello (VGA + serial)
  - [x] Milestone 2: IDT + PIC + PIT timer (100 Hz) + PS/2 keyboard (2026-09-25)
  - [x] Milestone 3: PMM — multiboot2 memory map, bitmap frame allocator (4 KiB frames, 1 GiB mapped limit) (2026-09-25)
  - [x] Milestone 4: Kernel heap — kmalloc/kfree, free list, split + merge (`mtest` passes) (2026-09-25)
  - [x] Milestone 5: IR2 initramfs — GRUB loads initrd.tar module, kernel parses ustar in place; `ls` + `cat` (2026-09-25)
- **Stage 2 — Shell** (runs in-kernel for now, splits out later)
  - [x] v0.1: prompt, line editor (backspace, ESC cancels line), help/echo/uptime/about/banner/clear/reboot (2026-09-25)
  - [x] v0.2: `ls`, `cat`, `dev` (device register table), `fire UR1` watchdog panic (2026-09-25)
  - [ ] write/create files once the heap + a writable FS land (DR1 via ATA)
- **Stage 3 — Desktop**
  - [x] D1 (2026-09-25): GRUB framebuffer (800x600x24 direct RGB), PS/2 mouse
        (IRQ12), cursor with save/restore, wallpaper + taskbar + live clock,
        welcome window with clickable close box. `desktop` command, ESC exits.
        Boot trampoline now identity-maps all 4 GiB (framebuffer lives at
        0xFD000000). Console renders through the bitmap font (Lat15-Fixed16).
  - [x] D2 (2026-09-25): real boot-to-desktop environment (v0.5.0). Window
        manager (wm.c: open/close/focus/drag), multi-console term (one console
        per window), Shell + Notepad apps, ramfs (writable FS — Notepad
        auto-saves note.txt, `cat` reads it), taskbar launcher buttons +
        live clock. ESC closes the focused window; no-framebuffer machines
        fall back to the plain shell. Old `desktop` command removed.
  - [x] D3 (2026-09-25, v1.0.0): Files app (filemgr.c: keyboard file browser,
        arrow keys + Enter) + viewer window, file extension registry (ext.c —
        ~30 types with VGA colours, `ls` colour-codes), unified file index
        (files.c: ramfs + IR2 together).
- **Stage 4 — Tools**: calculator, text editor, file browser, terminal app
- **getspgk** (curl + apt): network stack ladder — e1000 NIC driver -> DHCP ->
  ARP -> UDP -> TCP -> HTTP GET -> install packages into a RAM filesystem
  - Status (v1.0.1): **WORKS END TO END** — `getspgk list` and
    `getspgk install <pkg>` both verified in QEMU against the host server.
    Four stacked bugs fixed: TCP pseudo-header checksum used byte-swapped IP
    halves, checksum stored without htons, IP padding treated as TCP payload
    (trust ip->total, never the frame length), FIN/ACK close handling.
    Real-hardware readiness: ARP cache (not one gateway MAC), DHCP options 1+3
    (netmask + router) with subnet-aware next-hop (direct vs gateway), runt
    frames padded to 60 bytes, `getspgk server <ip>` points it at any real
    LAN machine. Host server: `python3 tools/spgk-server.py` (guest reaches it
    at 10.0.2.2:8080 over QEMU user-net).

## Roadmap

- **v1.0.2** (BDFL decree, 2026-09-26):
  - Double-clicky executable files in the Files app / desktop
  - HTML support + a web browser **demo** (the full browser is v1.1.0)
  - News app: uses our TCP stack to fetch the latest software updates
  - System updates over our own TCP stack — no GitHub needed ever again
- **v1.1.0**: full from-scratch web browser (DNS -> HTTP -> HTML subset ->
  WM window; Electron is impossible — it needs a host OS underneath) +
  directories in the filesystem (`cd`, `pwd`, folders in Files app)

## Build & run

```
make            # build kernel.bin + openos.iso
make run        # boot it in QEMU (window)
make headless   # boot with no window, output on stdout (serial)
```

Needs: gcc, nasm, ld, grub-mkrescue, xorriso, qemu-system-x86_64.

## Layout

```
boot/boot.asm      # multiboot2 header + 32→64-bit long-mode trampoline
kernel/kmain.c     # C entry point
kernel/term.c/.h   # multi-console text grids (one per window) + COM1 serial
kernel/idt.c/.h    # IDT + PIC remap + interrupt dispatch
kernel/isr.asm     # interrupt stubs (exceptions + IRQs)
kernel/timer.c/.h  # PIT at 100 Hz
kernel/kb.c/.h     # PS/2 keyboard, scancode set 1, ring buffer
kernel/shell.c/.h  # the shell + command table
kernel/initrd.c/.h # IR2: multiboot2 module + ustar tar reader
kernel/mm.c/.h     # PMM: firmware memory map + frame bitmap allocator
kernel/heap.c/.h   # kmalloc/kfree free-list heap (1 MiB pool for now)
kernel/font.c/.h   # 8x16 bitmap font (from Lat15-Fixed16 PSF)
kernel/gfx.c/.h    # framebuffer engine: pixels, rects, text, copy (24/32 bpp)
kernel/mouse.c/.h  # PS/2 mouse (IRQ12; handshake runs with cli)
kernel/desktop.c/.h # Stage 3 desktop: wallpaper, taskbar, clock, launchers
kernel/wm.c/.h     # window manager: focus, drag, close, chrome
kernel/apps.c/.h   # app dispatch (Shell, Notepad)
kernel/ramfs.c/.h  # writable RAM filesystem (kmalloc-backed)
kernel/ext.c/.h    # file extension registry (type names + VGA colours)
kernel/files.c/.h  # unified file index over ramfs + IR2
kernel/filemgr.c/.h # Files app: keyboard-driven file browser
kernel/pci.c/.h    # PCI config-space scan, BAR decode
kernel/e1000.c/.h  # Intel e1000 NIC driver (polling, legacy descriptors)
kernel/net.c/.h    # ETH/ARP/IPv4/UDP + DHCP client
kernel/tcp.c/.h    # minimal TCP client (connect/send/recv/close)
kernel/getspgk.c/.h # getspgk command: package downloader
tools/spgk-server.py # host-side package server (10.0.2.2:8080)
kernel/dev.c/.h    # DR/IR/UR device registers + UR1 watchdog
kernel/panic.c/.h  # kpanic: red screen, halt on purpose
initrd/            # files packed into initrd.tar (the filesystem!)
grub/grub.cfg      # GRUB menu for the ISO
linker.ld          # links everything at 1 MiB
```

## Rules

- Kernel C is freestanding: no libc, no stdio. We build our own everything.
- One responsibility per file; split files past ~150 lines.
- All tunables (colours, sizes) get named constants, not magic numbers.
- Verify each milestone in QEMU before moving up the ladder.
