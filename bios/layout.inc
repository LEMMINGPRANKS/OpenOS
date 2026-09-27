; OpenBIOS disk layout law -- ONE law, shared by mbr.asm, stage2.asm, the
; Makefile, tools/mkdisk.py and the kernel's part.c. DUAL BOOT LAW:
; the drive has a REAL MBR partition table at LBA 0; OpenOS lives inside
; its own partition and every LBA below is an offset from the START OF
; THE OPENOS PARTITION (PART_BASE). The MBR finds the partition by
; type byte 0x7F and verifies the "OPOS2" magic in its first sector.
;
;   +LBA     0        partition boot sector: "OPOS2" magic + stage2 body
;                     (stage2 IS the partition boot code: magic header,
;                      then code; entry point = +8)
;   +LBA     0-126    stage2 (up to ~63 KiB, magic law in the first 8 bytes)
;   +LBA     127      kernel header (A/B slot bookkeeping for self-updates)
;   +LBA   128-639    kernel slot A (256 KiB)
;   +LBA   640-1151   kernel slot B (256 KiB)
;   +LBA  1152-2047   free
;   +LBA  2048-8191   DR1 store (superblock + WMBG files = the filesystem)
;
; Absolute disk law (tools/mkdisk.py builds it):
;   LBA 0          MBR code + 4-entry partition table (0x1BE), ends 0xAA55
;   entry 1        the other OS (Linux on pocketdev; 0x83 test in QEMU),
;                  starts at LBA 2048 -- the post-MBR gap stays untouched
;   entry N        OpenOS, type 0x7F, bootable (0x80), 1 MiB-aligned start

PART_TYPE_OPENOS equ 0x7F              ; our partition type byte
PART_TABLE_OFF   equ 0x1BE             ; entries inside the MBR sector

; "OPOS2" as the MBR sees it in the partition's first sector
STAGE2_MAGIC_D  equ 0x534F504F         ; "OPOS" little-endian dword
STAGE2_MAGIC_B  equ 0x32              ; "2"

STAGE2_LBA    equ 0                   ; stage2 IS the partition boot sector
STAGE2_SECT   equ 126
KHDR_LBA      equ 127                 ; the A/B header sector
KSLOT_A_LBA   equ 128                 ; slot A
KSLOT_B_LBA   equ 640                 ; slot B
KSLOT_SECT    equ 512                 ; 256 KiB per slot
KERNEL_LBA    equ KSLOT_A_LBA         ; mkdisk stamps kernel.flat into slot A
STORE_LBA     equ 2048                ; must match kernel/store.h

; the A/B header sector (little-endian u32s at PART_BASE+127):
;   +0   magic  0x4B534C4F ("OLSK")
;   +4   ok_slot / ok_sectors / ok_entry / ok_bss      (last known good)
;   +20  cand_slot / cand_sectors / cand_entry / cand_bss (staged update)
;   +36  booting (1 = a candidate was chosen this boot)
; A field of 0xFFFFFFFF means "none"; ok_sectors 0 means "use the
; stage2 build-time defaults" (first boot from a fresh image).
KSLOT_MAGIC   equ 0x4B534C4F

; the boot menu (OpenBIOS owns the MBR, stage2 draws the menu)
MENU_TICKS    equ 55                  ; BIOS ticks, ~55ms each: ~3 seconds

; real-mode staging addresses (all below 1 MiB -- real mode can't reach higher)
STAGE2_ADDR   equ 0x8000
STAGE2_ENTRY  equ STAGE2_ADDR + 8     ; skip the 8-byte "OPOS2" magic header
KERNEL_LOAD   equ 0x10000
KHDR_BUF      equ 0x7E00              ; header sector lands here
PARTTAB       equ 0x6000              ; MBR partition table copy (64 bytes)
PBR_LOAD      equ 0x7C00              ; chainloaded partition boot sector

; final home, copied up once protected mode is on
KERNEL_ADDR   equ 0x100000            ; kernel link address (1 MiB)

; the fake multiboot2 info block stage2 builds for the kernel
MB2_ADDR      equ 0x7000
E820_BUF      equ 0x5000              ; raw E820 entries land here first

; VBE scratch buffers (controller info 512 B, mode info 256 B)
VBE_INFO      equ 0x9000
MODE_INFO     equ 0x9800
