# OpenOS

Freddie's brand-new **64-bit, open-source, from-scratch** operating system.
Written in C + a little assembly. Boots via GRUB (multiboot2), then climbs
into x86-64 long mode under its own power. Bat OS (32-bit) is the ancestor
and inspiration — OpenOS is the modern successor.

Freddie is BDFL. Open source by design.

## The ladder

- **Stage 1 — Kernel**
  - [x] Milestone 1: Boot into 64-bit long mode, print hello (VGA + serial)
  - [ ] Milestone 2: GDT/IDT + interrupts (timer tick, keyboard)
  - [ ] Milestone 3: Physical memory map + page/frame allocator
  - [ ] Milestone 4: Kernel heap (kmalloc/kfree)
  - [ ] Milestone 5: Ramdisk filesystem + tar/initrd loading
- **Stage 2 — Shell**: command line (ls, echo, cat, clear, help)
- **Stage 3 — Desktop**: framebuffer graphics, mouse, windows
- **Stage 4 — Tools**: calculator, text editor, file browser, terminal app

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
kernel/term.c/.h   # VGA text (0xb8000) + COM1 serial console
grub/grub.cfg      # GRUB menu for the ISO
linker.ld          # links everything at 1 MiB
```

## Rules

- Kernel C is freestanding: no libc, no stdio. We build our own everything.
- One responsibility per file; split files past ~150 lines.
- All tunables (colours, sizes) get named constants, not magic numbers.
- Verify each milestone in QEMU before moving up the ladder.
