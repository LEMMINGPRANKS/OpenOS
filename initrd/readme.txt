Welcome to OpenOS!

This file lives on DR1 -- the main drive -- wrapped in a .WMBG archive
(Freddie's QuantumSquish format). OpenBIOS boots the kernel from disk,
the kernel's own ATA driver reads this store, and `save` writes it back.
No initramfs, no GRUB: a real OS on a real drive.

You are reading a file, from a filesystem, inside a 64-bit OS,
built from scratch. Nice.

-- Freddie, BDFL
