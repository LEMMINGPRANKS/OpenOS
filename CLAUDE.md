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
- **v1.3.0** (2026-09-27) — **DONE**: Paint! A real drawing app.
  kernel/paint.c: 480x320 RGB canvas (kmalloc'd), 16 colours, 4 brush
  sizes, eraser, clear; mouse strokes with interpolation, incremental
  dirty-rect blits so drawing keeps up with the brush. kernel/png.c:
  a from-scratch PNG codec — CRC-32, Adler-32, zlib stream made of
  deflate *stored* blocks (real PNGs, viewable anywhere, no inflater
  needed to decode our own). **S** saves /paint.png (paint2, 3...) into
  ramfs + DR1 and the icon appears on the desktop instantly; opening a
  .png loads it back into Paint (stored-block PNGs; compressed ones are
  politely refused for now). WM grew pixel-app hooks
  (app_wants_pixels/app_repaint_pixels/app_pointer) alongside text apps.
  Heap 1->4 MiB, store region 2048-8191 (4 MiB) to fit real images.
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
    initrd.tar until v1.2.0), 2048-8191 DR1 store (superblock + table +
    WMBG files; 4 MiB since v1.3.0) — SUPERSEDED in v1.4.0 by the
    partition-relative law below
- **v1.4.0** (2026-09-27) — **DONE**: DUAL BOOT. The drive now has a REAL
  MBR partition table; OpenOS lives inside its own partition (type byte
  0x7F, bootable) and every on-disk structure is PARTITION-RELATIVE:
  +0 stage2 (stage2 IS the partition boot sector: "OPOS2" magic in the
  first 8 bytes, entry at +8), +127 A/B header, +128/+640 kernel slots
  A/B, +2048 store. The MBR scans its own table for type 0x7F, verifies
  the magic, loads stage2 from inside the partition, hands the partition
  base LBA over in DI:BP. stage2 draws a boot menu (3s timeout, default
  OpenOS; key 2 chainloads the first other partition: reads its sector 0
  to 0x7C00 and far-jumps, DL = boot drive). kernel/part.c resolves
  part_base() per drive; store/kupdate/shell use disk_lba(rel). Images
  are assembled by tools/mkdisk.py (boot / store / migrate modes); the
  test "other OS" partition (type 0x83) starts at LBA 2048 so the
  post-MBR gap stays untouched for real GRUB. Verified in QEMU: default
  boot, menu chainload, save/reboot persistence, and the full A/B kernel
  update incl. confirm (a staged-and-hung candidate rolls back on the
  next boot via the "booting" flag). Real pocketdev disk install is a
  LATER milestone with Robin (resize + backups first).
- **v1.4.1** (2026-09-27) — **DONE**: The Internet app + real DNS.
  kernel/dns.c: from-scratch DNS resolver (A records, compression
  pointers, 4-entry cache) over the UDP stack — default resolver
  10.0.2.3, `dns_set_server()`. http.c gained `http_get_url`
  (host[:port]/path, dotted-quad or DNS name — in QEMU user-net this
  fetches REAL internet pages) + `http_post` (form POST). shell: `dns
  <name>` + `fetch <url>`. kernel/internet.c: ONE Internet app (taskbar
  Internet button replaces News/Browser/Update) with five tabs —
  Browser (address bar, link clicks, download-installs), News, Updates
  (the A/B update manager), Comments and Ideas (read + POST; server
  saves to packages/comments.txt + roadmap.txt via new /comments +
  /roadmap endpoints with do_POST). Tab key cycles tabs; tab bar is
  clickable. APP_BROWSER/APP_NEWS/APP_UPDATE retired; .html files open
  the Internet app. Verified in QEMU end to end: example.com fetched
  over our own e1000+DHCP+DNS+TCP stack, comment + idea posted from
  the app and saved on the host, all tabs rendering.
- **v1.4.2** (2026-09-27) — **DONE**: more network chips + network fixes.
  kernel/nic.c: a NIC layer that probes every driver and routes frames to
  the card found. Drivers: Intel e1000/e1000e (~50 IDs incl. 82574L and
  I217/I218/I219; reset, link up, EEPROM MAC fallback), Realtek
  RTL8169/8168/8111/8101, Realtek RTL8139. Fixes: no more ARP to 10.0.2.2
  before every request (broke every non-QEMU network); DNS server taken
  from DHCP option 6; DHCP uses option 54 as server id, checks xid + MAC,
  full 312-byte options; TCP takes segments strictly in order (resends
  and gaps answered with our ACK); handshake needs a SYN-ACK that ACKs
  our SYN, RST|ACK = refused. Verified in QEMU on e1000, e1000e,
  82545EM, 82544GC and rtl8139 (DHCP, DNS, fetch, 90 KB kernel update
  byte-exact on disk). RTL8169 family + PCH Intel parts: untested (QEMU
  has no model for them).
- **v1.4.3** (2026-09-27) — **DONE**: Settings app + network-driver picker.
  kernel/settings.c: a key=value store (/settings.txt in ramfs, flushed to
  DR1 on every change -- survives reboot with no `save`) + a Settings
  window (taskbar Settings button / `settings` command, headless prints
  current values). Network card row cycles auto/e1000/rtl8169/rtl8139 and
  applies INSTANTLY: saves the pref, net_reset(), nic_rebind() re-probes
  (preferred family first, stale pref falls back so boot can never break),
  then re-DHCPs right in the window. Package-server row is a typeable
  ip:port line (Enter applies via http_set_server). At boot nic_init()
  reads the "nic" pref for probe order; http.c applies the "server" pref
  once at first bring-up (http_net_forget re-arms it). Verified in QEMU
  with two NICs: live switch e1000 -> rtl8139 (and back), missing-card
  fallback, server change, cold-reboot persistence.
- **v1.4.4** (2026-09-27) — **DONE**: kernel version selector (stable +
  unstable channels). The server keeps each kernel in its own dir
  (packages/kernel/v<version>/ with kernel.flat + manifest.txt) and a
  `stable` file names the stable channel; /kernel/versions lists them all
  ("1.4.1 stable", "1.4.3 unstable", ...) and the classic
  /kernel/manifest.txt + /kernel/kernel.flat serve whatever stable points
  at, so older OpenOS updaters keep working. In OpenOS: `update kernel`
  lists every version (marking the running one), `update kernel <version>`
  installs that exact one -- downgrades allowed, that's the rollback path.
  Updates tab: l lists versions. `make publish-kernel` publishes the
  current build into its version dir (unstable until promoted);
  `make publish-stable` flips the stable channel to the current version.
  Verified in QEMU: list + pick 1.4.1 from a 1.4.4 boot (rebooted into
  it, A/B confirmed), then back to 1.4.4 through the old updater's
  stable path.
- **v1.4.5** (2026-09-27) — **DONE**: stale-text fixes. (1) The Internet
  app's context line (address bar / say / idea / hint rows) now pads the
  rest of the row with spaces on every redraw, so shorter text can never
  leave the tail of longer text behind. (2) term.c scroll() now resets
  the colour attribute of the bottom row too (chars were cleared but old
  colours leaked through after scrolling -- the getspgk/update residue).
  Verified in QEMU by decoding screendumps against kernel/font.c glyphs.
- **v1.5.0** (2026-09-27) — **DONE**: the search engine + bookmarks +
  history. The Browser's address bar is an omnibar now: words with a
  space always search, and a name that is neither local file nor server
  page falls through to a search too. Search = the server's
  /search?q=... (tools/spgk-server.py): DuckDuckGo instant answers +
  Wikipedia opensearch, fetched in parallel (4s caps each) and returned
  as simple clickable HTML; result links open over OpenOS's own
  DNS + TCP stack. kernel/marks.c: bookmarks + history live as
  /bookmarks.txt + /history.txt (ramfs, flushed to DR1, 24 entries
  each, history newest-first, no duplicates). "home" — the new default
  page, built and rendered locally with no network — lists both as
  clickable links; "bm" bookmarks the page you are on; the omnibar
  clears after every load so "home" can never eat the next word.
  http.c response wait raised 6s -> 15s for slow real-world pages.
  Verified in QEMU end to end: example.com load -> history noted,
  bm -> bookmark saved, cat of both files, and searches for pokemon +
  albert einstein from both the app and the shell fetch (real results
  rendered, E = mc² and all).
- **v1.6.0** (2026-09-27) — **DONE**: Part 1 of the 1.6 overhaul — the
  desktop. 1400x900 framebuffer. 2015-OS light theme: term.c renders
  every VGA attribute through dual ink/paper palettes (dark inks on
  white/light papers). App menu (kernel/menu.c): 3x3 grid of white
  rounded tiles, keyboard arrows + Enter or click, and the Wii rule —
  closing the last window puts you back on the menu. The per-second
  taskbar repaint skips while the menu is open (it was restarting the
  slow full-menu paint halfway through). Wii-style animated app-start
  banners (kernel/banner.c: accent card, bouncing dots, shimmer bar).
  Shared pixel appbar (kernel/appbar.c) + term_protect(): apps reserve
  top console rows for pixel chrome that now survives scrolling,
  focus changes and window drags (wm grew app_after_paint repaint
  hooks). Every app overhauled to one quality bar: Shell, Notepad,
  Viewer/Runner, Files (grey selection bands, pixel folder/file
  icons), Internet (Chrome-style white tab strip, omnibox pill, giant
  red italic INTERNET home page with a real search bar + clickable
  bookmark tiles), Music (clickable tune cards + free-play piano
  keys), Download (package rows with teal discs, one-key install),
  Update (slot info, version list with running/stable dot icons),
  Settings (band-selected rows). http_get now brings the network up
  itself — apps no longer depend on the shell having run getspgk
  first. Shell gained `files`, `music`, `download` window commands.
  Verified headless in QEMU end to end, including a real package
  install and the kernel version list over the network.
- **v1.6.1** (2026-09-28) — **DONE**: Part 2 — full-screen windows. wm_open
  ignores geometry (BDFL decree: no taskbar, so windows fill the screen and
  the title-bar exit button is the way out); drag support removed; taskbar
  deleted (Apps button, window pills, clock) — the Wii rule is the whole
  navigation now. Verified in QEMU.
- **v1.6.2** (2026-09-28) — **DONE**: Part 3 — the GPU layer + NVIDIA
  detective. kernel/gpu.c (device-independent interface: probe/ident today,
  accelerated backends later) + kernel/nv.c (read-only PCI detective for any
  NVIDIA display card: id table for MCP89/RTX 3060/3070/3080/4090, BAR size
  probes, BOOT0 family read when BAR0 is under the 4 GiB identity map —
  coldrivers trick). `gpuinfo` command + gpu name in the boot banner and
  desktop wordmark. QEMU shows the honest software-backend fallback.
  Real-machine verification (Air = 320M, freddiespc = 3070) pending USB
  stick boots. Plan: parts 4-7 = MCP89 deep dive (nv50-tables init),
  accelerated 2D + RTX groundwork, 3D engine in the HTML engine
  (<canvas> + OpenJS arrays/onFrame + fixed-point r3d rasterizer — no
  SSE in this kernel by design), then Circuitstorm for OpenOS on his
  real tracks/*.json. Plan file: ~/.claude/plans/gentle-yawning-flurry.md.
- **v1.7.0** (2026-10-02) — **DONE**: Part 1 of the 1.7 GPU arc — the
  register core. kernel/nvregs.h: named register offsets, every one
  verified against the nouveau source in linux-7.2.4 (PMC BOOT_0/INTR/
  INTR_EN/ENABLE, PFIFO INTR/INTR_EN, nv50 PDISPLAY CRTC clock ctrl +
  SOR SLINK). nv.c grew nv_rd32/nv_wr32 over BAR0 (drops to no-ops when
  BAR0 is above the 4 GiB identity map) and pci_enable()s the card at
  probe (MMIO decode + bus master — the driver era begins). gpuinfo now
  dumps live register state; a compact boot-log line (BOOT0 / PMC_ENABLE /
  CRTC0) rides to COM1 automatically for wedged-boot debugging. Strictly
  read-only on metal. 1.7 plan: parts 2-5 = VRAM takeover -> display
  engine (scanout + hardware cursor) -> PFIFO + nv50-tables ctxprog ->
  hardware 2D fills/blits (plan file: ~/.claude/plans/parallel-purring-map.md).
  Targets BOTH the Air (MCP89 deep dive) and freddiespc (RTX 3070 via USB
  boots). Verified in QEMU: 1.7.0 boots, software path + honest fallback
  lines unchanged; real-metal register dump = the USB-boot milestone.
- **next (1.7)**: part 2 — VRAM takeover (BAR1 aperture + our own buffer);
  also still on the list: a terminal app with colours and scrollback; a
  music player that beeps tunes through the PC speaker

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
kernel/http.c/.h   # shared HTTP client (get/post/get_url; used by getspgk + internet)
kernel/news.c/.h   # headless news fetch (the Internet app News tab uses http directly)
kernel/dns.c/.h    # DNS resolver (A records over UDP, cache)
kernel/internet.c/.h # The Internet app: browser/news/updates/comments/ideas tabs
kernel/settings.c/.h # key=value settings store (/settings.txt -> DR1) + Settings app
kernel/marks.c/.h   # bookmarks + history (/bookmarks.txt, /history.txt) + home page
kernel/version.h   # single OS_VERSION string used by every banner
kernel/filemgr.c/.h # Files app: keyboard + mouse file browser
kernel/menu.c/.h    # app menu overlay: 3x3 tile grid (keyboard + click)
kernel/banner.c/.h  # Wii-style animated app-start splash
kernel/appbar.c/.h  # shared pixel appbar for app windows (3 protected rows)
kernel/music.c/.h   # Music app: tune cards + free-play keys (PC speaker)
kernel/dlapp.c/.h   # Download app: spgk package browser + installer
kernel/updapp.c/.h  # Update app: A/B kernel updater window
kernel/pci.c/.h    # PCI config-space scan, BAR decode
kernel/nic.c/.h    # NIC layer: probes the drivers below, routes frames to the one found
kernel/e1000.c/.h  # Intel e1000/e1000e NIC driver (polling, legacy descriptors)
kernel/e1000_ids.h # Intel device-ID table + per-chip quirks
kernel/rtl8169.c/.h # Realtek RTL8169/8168/8111/8101 NIC driver (IO BAR, polling)
kernel/rtl8139.c/.h # Realtek RTL8139 NIC driver (IO BAR, polling)
kernel/portio.h    # inb/outb/inw/outw/inl/outl for the IO-BAR drivers
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
kernel/paint.c/.h  # Paint: canvas, palette, strokes, PNG save/load
kernel/png.c/.h    # from-scratch PNG codec (stored-block zlib, CRC+Adler)
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
