# OpenOS

A 64-bit, open-source operating system written from scratch in C and assembly.

Boots via GRUB (multiboot2) → climbs into x86-64 long mode → kernel takes over.

## Try it

```
make run      # needs gcc, nasm, grub-mkrescue, qemu-system-x86_64
```

Roadmap: kernel → shell → desktop → tools. See CLAUDE.md for the milestone ladder.

Author: Freddie (BDFL). Open source.
