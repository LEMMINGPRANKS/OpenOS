; OpenBIOS disk layout law -- ONE law, shared by mbr.asm, stage2.asm and
; the Makefile. LBA numbers are sectors from the start of the drive.
;
;   LBA      0        MBR (512 bytes, ends in 0xAA55)
;   LBA      1-127    stage2 (up to 64 KiB)
;   LBA    128-1151   kernel.flat (up to 512 KiB)
;   LBA   1152-2047   free (old initrd.tar home; available for growth)
;   LBA   2048-8191   DR1 store (superblock + WMBG files = the filesystem)

STAGE2_LBA    equ 1
STAGE2_SECT   equ 128
KERNEL_LBA    equ 128
STORE_LBA     equ 2048              ; must match kernel/store.h

; real-mode staging addresses (all below 1 MiB -- real mode can't reach higher)
STAGE2_ADDR   equ 0x8000
KERNEL_LOAD   equ 0x10000

; final home, copied up once protected mode is on
KERNEL_ADDR   equ 0x100000           ; kernel link address (1 MiB)

; the fake multiboot2 info block stage2 builds for the kernel
MB2_ADDR      equ 0x7000
E820_BUF      equ 0x5000             ; raw E820 entries land here first

; VBE scratch buffers (controller info 512 B, mode info 256 B)
VBE_INFO      equ 0x9000
MODE_INFO     equ 0x9800
