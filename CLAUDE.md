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

- **v1.0.2** (BDFL decree, 2026-09-26) — **DONE**:
  - Directories in the filesystem: `cd`, `pwd`, folders in Files app
    (kernel/path.c + files_list_dir; implicit dirs, ramfs + IR2 merged)
  - OpenJS: a from-scratch JavaScript interpreter in the kernel (js.c —
    lexer, Pratt parser, tree-walking evaluator) + `run <file.js>`
  - Double-clicky executable files: Files app rows and desktop icons;
    .js runs, .html browses, text views, dirs open Files
  - HTML support + browser demo (browser.c — tag-aware text renderer;
    Browser window; .html in the extension registry)
  - News app: taskbar News button / `news` command fetches /news from
    the spgk server over our own TCP + DHCP + e1000 stack and renders
    it as HTML. HTTP client extracted to kernel/http.c for all apps.
  - Updater: `update` command installs every feature from the server's
    /updates/index into ramfs (new desktop icons appear instantly, no
    reboot); the Browser can also browse to `10.0.2.2:8080/updates` and
    click a feature link to install just that one. Server /version says
    which feature pack is out; kernel/version.h holds OS_VERSION.
- **v1.2.0** (BDFL decree, 2026-09-27) — **DONE**: OpenOS lives on DR1.
  The initramfs is retired from the boot. `tools/mkstore.py` packs every
  initrd/ file into a ready-made DR1 store at build time (superblock +
  table + WMBG archives, exactly what store.c reads); the Makefile stamps
  it into openos.img at LBA 2048. stage2 no longer loads initrd.tar or
  emits a module tag — the kernel boots, brings up ATA, and restores the
  WHOLE filesystem from DR1 (kmain prints "DR1: N files restored").
  `save` flushes back; verified in QEMU: install over TCP -> save ->
  cold reboot -> the downloaded file is the one that comes back.
  GRUB ISO path keeps initrd as a fallback.
- **v1.2.1** (2026-09-27) — **DONE**: the store drive. `store.img` is a
  separate disk (IDE slave) that the filesystem LIVES on; created once
  (seeded), never rebuilt by make, so saved files survive openos.img
  rebuilds. ata.c gained slave-drive support (IDENTIFY both, select per
  command); store.c prefers the store drive, falls back to the boot-disk
  seed, and on first boot with a blank store drive MIGRATES the whole
  filesystem across ("DR1: filesystem moved onto the store drive").
  Verified: install-over-TCP -> save -> `make` rebuild -> boot -> the
  downloaded file is still there. store.img is user data: never gitignore
  it away, never clean it.
- **v1.1.0** (BDFL decree, 2026-09-26) — **DONE**:
  - DR1: ATA PIO driver (kernel/ata.c) — IDENTIFY, LBA28 read/write with
    time-based BSY/DRQ waits (FLUSH CACHE in QEMU is a host fsync: it can
    outrun any fixed poll count), `disk` + `disk test` commands
  - DR1 store (kernel/store.c): files that survive power-off. Superblock
    "OPENOSST" at LBA 2048, file table, each file stored as its own
    QuantumSquish `.WMBG` archive (Freddie's format, method 0 = STORE).
    `save` flushes ramfs to DR1; boot restores. Updater + Browser installs
    auto-flush, so installed features survive a cold no-network reboot
  - **OpenBIOS** (bios/mbr.asm + bios/stage2.asm): our own bootloader,
    GRUB retired to `make isorun`. MBR -> stage2 (E820, A20, loads kernel
    + initrd, protected mode, copies them home, zeroes bss, builds a fake
    multiboot2 info block) -> kernel, unmodified. VBE 2.0 scan picks
    800x600x32 (24 fallback, serial-shell fallback after that) and writes
    the framebuffer tag, so `make run` boots the FULL desktop from our
    own 512-byte MBR. Real hardware: `dd openos.img` onto a USB stick ->
    boots any BIOS PC.
  - The disk-layout law (bios/layout.inc, ONE law for MBR/stage2/Makefile):
    LBA 0 MBR, 1-127 stage2, 128-1151 kernel.flat, 1152-2047 free (was
    initrd.tar until v1.2.0), 2048-3071 DR1 store (superblock + table +
    WMBG files)
- **next**: full from-scratch web browser (DNS -> HTTP -> HTML subset ->
  WM window; Electron is impossible — it needs a host OS underneath)

## Build & run

```
make            # build kernel.bin + openos.iso (GRUB fallback)
make run        # build + boot openos.img through OUR OpenBIOS bootloader
make isorun     # boot the GRUB iso (for comparison)
make headless   # GRUB iso, no window, serial on stdout
make imgheadless# OpenBIOS img, no window, serial on stdout
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
kernel/path.c/.h   # path helpers: resolve, parent, basename
kernel/js.c/.h     # OpenJS: JavaScript interpreter (lexer+parser+eval)
kernel/browser.c/.h # HTML renderer (tag-aware text, colours, links)
kernel/http.c/.h   # shared HTTP client (used by getspgk + news)
kernel/news.c/.h   # News app: fetches /news and renders it
kernel/version.h   # single OS_VERSION string used by every banner
kernel/filemgr.c/.h # Files app: keyboard + mouse file browser
kernel/pci.c/.h    # PCI config-space scan, BAR decode
kernel/e1000.c/.h  # Intel e1000 NIC driver (polling, legacy descriptors)
kernel/net.c/.h    # ETH/ARP/IPv4/UDP + DHCP client
kernel/tcp.c/.h    # minimal TCP client (connect/send/recv/close)
kernel/getspgk.c/.h # getspgk command: package downloader
tools/spgk-server.py # host-side package server (10.0.2.2:8080, /index,
                     # /news from packages/news.html)
tools/mkstore.py    # build-time DR1 seeder: initrd/ -> store-seed.img
kernel/dev.c/.h    # DR/IR/UR device registers + UR1 watchdog
kernel/panic.c/.h  # kpanic: red screen, halt on purpose
kernel/ata.c/.h    # DR1: ATA PIO driver (IDENTIFY, LBA28 read/write)
kernel/tar.c/.h    # shared ustar walker (initrd + store)
kernel/store.c/.h  # DR1 store: OPENOSST superblock + per-file WMBG archives
bios/layout.inc    # the disk-layout law (LBAs + load addresses, ONE source)
bios/mbr.asm       # OpenBIOS stage 1: the first 512 bytes of the drive
bios/stage2.asm    # OpenBIOS stage 2: E820, A20, VBE, fake multiboot2, kernel
initrd/            # base files -- now seeded onto DR1 by mkstore.py
                     # (initrd.tar remains only for the GRUB fallback ISO)
grub/grub.cfg      # GRUB menu for the fallback ISO
linker.ld          # links everything at 1 MiB
```

## Rules

- Kernel C is freestanding: no libc, no stdio. We build our own everything.
- One responsibility per file; split files past ~150 lines.
- All tunables (colours, sizes) get named constants, not magic numbers.
- Verify each milestone in QEMU before moving up the ladder.
