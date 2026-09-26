; OpenBIOS stage2 -- real mode: memory map (E820), A20, load the kernel
; and initrd from disk; then protected mode: copy them up to their final
; homes, zero the kernel's bss, build a fake multiboot2 info block, and
; boot the kernel exactly the way GRUB would have.
;
; The Makefile patches in real sizes:
;   ENTRY_OFF      _start offset inside kernel.flat
;   KERNEL_SECTORS sectors of kernel.flat on disk
;   INITRD_SECTORS sectors of initrd.tar on disk
;   INITRD_BYTES   exact size of initrd.tar
;   BSS_END_OFF    __kernel_end - 1 MiB (zero up to here)

%include "layout.inc"

%ifndef ENTRY_OFF
%define ENTRY_OFF 0x30
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 192
%endif
%ifndef INITRD_SECTORS
%define INITRD_SECTORS 32
%endif
%ifndef INITRD_BYTES
%define INITRD_BYTES 16384
%endif
%ifndef BSS_END_OFF
%define BSS_END_OFF 0x1B0000
%endif

MB2_BOOT_MAGIC equ 0x36D76289
SMAP           equ 0x534D4150
E820_MAX       equ 128               ; entries we accept

bits 16
org STAGE2_ADDR

start:
    mov [boot_drive], dl
    mov si, msg_stage2
    call puts

    ; --- A20: fast gate (port 0x92), belt+braces BIOS call ---
    in  al, 0x92
    or  al, 2
    and al, 0xFE                     ; bit 0 is RESET -- never set it
    out 0x92, al
    mov ax, 0x2401
    int 0x15

    ; --- memory map: int 15h E820 -> E820_BUF (24-byte entries) ---
    xor ebx, ebx
    mov di, E820_BUF
    mov dword [e820_count], 0
.e820:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, SMAP
    int 0x15
    jc  .e820_end_try_fallback
    cmp eax, SMAP
    jne .e820_end_try_fallback
    inc dword [e820_count]
    add di, 24
    cmp dword [e820_count], E820_MAX
    jae .e820_done
    test ebx, ebx
    jnz .e820
.e820_done:
    jmp .e820_loaded

.e820_end_try_fallback:
    cmp dword [e820_count], 0
    jne .e820_loaded
    ; ancient BIOS: no E820. Assume the classic map: low 640 KiB usable,
    ; 1 MiB up to 512 MiB usable. (mm.c re-marks the low MiB used anyway)
    mov di, E820_BUF
    xor eax, eax
    o32 stosd                        ; base = 0
    o32 stosd
    mov eax, 0x9FC00
    o32 stosd
    xor eax, eax
    o32 stosd                            ; len = 0x9FC00
    mov eax, 1
    o32 stosd                            ; type = usable
    xor eax, eax
    o32 stosd                            ; acpi flags
    mov eax, 0x00100000
    o32 stosd
    xor eax, eax
    o32 stosd                            ; base = 1 MiB
    mov eax, 0x1FF00000
    o32 stosd
    xor eax, eax
    o32 stosd                            ; len = 511 MiB
    mov eax, 1
    o32 stosd
    xor eax, eax
    o32 stosd
    mov dword [e820_count], 2

.e820_loaded:
    mov si, msg_mem
    call puts

    ; --- load the kernel: KERNEL_LBA -> KERNEL_LOAD ---
    mov si, msg_kernel
    call puts
    mov ebx, KERNEL_LBA
    mov cx, KERNEL_SECTORS
    mov ax, KERNEL_LOAD >> 4
    mov es, ax
    xor di, di
    call read_disk

    ; --- load the initrd: INITRD_LBA -> INITRD_LOAD ---
    mov si, msg_initrd
    call puts
    mov ebx, INITRD_LBA
    mov cx, INITRD_SECTORS
    mov ax, INITRD_LOAD >> 4
    mov es, ax
    xor di, di
    call read_disk

    mov si, msg_pm
    call puts

    ; --- VBE: find + set 800x600, 24 or 32 bpp, linear framebuffer ---
    mov ax, 0
    mov es, ax
    mov di, VBE_INFO
    mov dword [VBE_INFO], 0x32454256  ; "VBE2" signature the BIOS wants
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe
    mov byte [vbe_want], 32
.vbe_pass:
    mov si, [VBE_INFO + 14]           ; mode list: far ptr at +14 (off, seg)
    mov word [vbe_found], 0
.vbe_scan:
    mov ax, [VBE_INFO + 16]
    mov gs, ax
    gs lodsw                          ; next mode number (list seg != our ds)
    cmp ax, 0xFFFF
    je  .vbe_scan_done
    mov [vbe_mode], ax
    mov cx, ax
    mov ax, 0x4F01
    mov di, MODE_INFO
    int 0x10
    cmp ax, 0x004F
    jne .vbe_scan
    cmp word [MODE_INFO + 0x12], 800
    jne .vbe_scan
    cmp word [MODE_INFO + 0x14], 600
    jne .vbe_scan
    mov al, [MODE_INFO + 0x19]
    cmp al, [vbe_want]
    jne .vbe_scan
    ; (SeaBIOS never sets mode-attr bit 7, so we trust PhysBasePtr != 0
    ;  as the proof a linear framebuffer exists for this mode)
    cmp dword [MODE_INFO + 0x28], 0
    je  .vbe_scan
    mov al, [MODE_INFO + 0x1B]        ; memory model: 4 packed / 6 direct
    cmp al, 4
    je  .vbe_found
    cmp al, 6
    je  .vbe_found
    jmp .vbe_scan
.vbe_found:
    mov word [vbe_found], 1
.vbe_scan_done:
    cmp word [vbe_found], 0
    jne .vbe_set
    cmp byte [vbe_want], 32
    jne .no_vbe
    mov byte [vbe_want], 24           ; no 32 bpp mode? try 24 before giving up
    jmp .vbe_pass
.vbe_set:
    mov bx, [vbe_mode]
    or  bx, 0x4000                    ; set with the LFB bit
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .no_vbe
    mov byte [vbe_ok], 1
    jmp .vbe_done
.no_vbe:
    mov si, msg_novbe
    call puts
.vbe_done:

    ; --- protected mode ---
    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_start

; read_disk: ebx = start LBA, cx = sectors, es:di = dest.
; Chunks of 32 sectors (BIOS-safe, and es:di never crosses 64 KiB).
read_disk:
.chunk:
    mov ax, cx
    cmp ax, 32
    jbe .got
    mov ax, 32
.got:
    mov word [dap_count], ax
    mov [dap_off], di
    mov [dap_seg], es
    mov [dap_lba], ebx
    push ax
    push cx
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    mov [disk_err], ah             ; capture before pops clobber ah
    pop cx
    pop ax
    jc  err_disk
    movzx eax, ax                    ; eax's upper bits hold stale garbage (SMAP!) -- next LBA math must be clean
    add ebx, eax                     ; next LBA
    sub cx, ax
    jz  .done
    shl ax, 9                        ; sectors -> bytes
    add di, ax
    jnc .no_wrap
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_wrap:
    jmp .chunk
.done:
    ret

puts:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp puts
.done:
    ret
putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

put_hex8:                          ; print AL as two hex digits
    mov dl, al                      ; keep a copy: put_nib clobbers ah
    shr al, 4
    call put_nib
    mov al, dl
    and al, 0x0F
    call put_nib
    ret
put_nib:
    cmp al, 10
    jb .digit
    add al, 'A'-10
    jmp .emit
.digit:
    add al, '0'
.emit:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret

err_disk:
    mov si, msg_disk
    call puts
    mov al, [disk_err]
    call put_hex8
    mov si, msg_crlf
    call puts
.halt:
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0                             ; null
    dq 0x00CF9A000000FFFF            ; 32-bit code, base 0, limit 4 GiB
    dq 0x00CF92000000FFFF            ; 32-bit data, base 0, limit 4 GiB
gdt_end:
gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

align 4
dap:
    db 0x10, 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

boot_drive:  db 0
e820_count:  dd 0
disk_err:    db 0
vbe_want:    db 32
vbe_found:   dw 0
vbe_mode:    dw 0
vbe_ok:      db 0

msg_stage2: db "OpenBIOS stage2", 13, 10, 0
msg_mem:    db " memory mapped", 13, 10, 0
msg_kernel: db " loading kernel", 13, 10, 0
msg_initrd: db " loading initrd", 13, 10, 0
msg_pm:     db " protected mode...", 13, 10, 0
msg_novbe:  db " no VBE -- serial shell boot", 13, 10, 0
msg_disk:   db "disk read failed 0x", 0
msg_crlf:   db 13, 10, 0

; --- 32-bit protected mode -----------------------------------------------

bits 32
pm_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x7FF00                 ; scratch stack below the initrd staging

    ; copy kernel.flat up to its link address
    mov esi, KERNEL_LOAD
    mov edi, KERNEL_ADDR
    mov ecx, KERNEL_SECTORS * 128    ; sectors -> dwords
    rep movsd

    ; copy the initrd up too
    mov esi, INITRD_LOAD
    mov edi, INITRD_ADDR
    mov ecx, (INITRD_BYTES + 3) / 4
    rep movsd

    ; zero the kernel's bss (GRUB did this for ELF; we do it for flat)
    mov edi, KERNEL_ADDR + KERNEL_SECTORS * 512
    mov ecx, (BSS_END_OFF - KERNEL_SECTORS * 512) / 4
    xor eax, eax
    rep stosd

    ; --- build the fake multiboot2 info block at MB2_ADDR ---
    ; every tag: u32 type, u32 size, payload, padded to 8 bytes
    mov edi, MB2_ADDR
    xor eax, eax
    stosd                            ; total_size -- patched at the end
    stosd                            ; reserved

    ; tag 2: bootloader name "OpenBIOS"
    mov eax, 2
    stosd
    mov eax, 24                      ; 8 hdr + 9 string, rounded up to 8
    stosd
    mov eax, 'Open'
    stosd
    mov eax, 'BIOS'
    stosd
    xor eax, eax
    stosd
    stosd                            ; string NUL + pad -> 24 bytes total

    ; tag 3: module (the initrd)
    mov eax, 3
    stosd
    mov eax, 32                      ; 8 hdr + 8 addrs + 11 string, rounded
    stosd
    mov eax, INITRD_ADDR
    stosd                            ; mod_start
    mov eax, INITRD_ADDR + INITRD_BYTES
    stosd                            ; mod_end
    mov eax, 'init'
    stosd
    mov eax, 'rd.t'
    stosd
    mov eax, 'ar'                    ; "ar\0\0"
    stosd
    xor eax, eax
    stosd                            ; pad -> 32 bytes total

    ; tag 6: memory map -- E820 entries copied verbatim (24 bytes each)
    mov eax, 6
    stosd
    mov eax, [e820_count]
    mov edx, eax
    imul edx, edx, 24
    lea eax, [edx + 16]              ; header + entries
    stosd                            ; tag size
    mov eax, 24
    stosd                            ; entry_size
    xor eax, eax
    stosd                            ; entry_version
    mov esi, E820_BUF
    mov ecx, edx
    shr ecx, 2                       ; bytes -> dwords
    rep movsd

    ; tag 8: framebuffer -- only if VBE set a mode for us
    cmp byte [vbe_ok], 0
    je  .no_fb_tag
    mov eax, 8
    stosd
    mov eax, 40
    stosd
    mov eax, [MODE_INFO + 0x28]
    stosd                            ; fb addr low
    xor eax, eax
    stosd                            ; fb addr high (QEMU's LFB is < 4 GiB)
    movzx eax, word [MODE_INFO + 0x10]
    stosd                            ; pitch
    movzx eax, word [MODE_INFO + 0x12]
    stosd                            ; width
    movzx eax, word [MODE_INFO + 0x14]
    stosd                            ; height
    movzx eax, byte [MODE_INFO + 0x19]
    mov ah, 1                        ; bpp | fb_type(1 = direct RGB) << 8
    stosd
    mov eax, 0x08080810              ; r_pos 16, r_size 8, g_pos 8, g_size 8
    stosd
    mov eax, 0x00000800              ; b_pos 0, b_size 8, pad
    stosd
.no_fb_tag:

    ; end tag
    xor eax, eax
    stosd
    mov eax, 8
    stosd

    ; patch total_size
    mov eax, edi
    sub eax, MB2_ADDR
    mov [MB2_ADDR], eax

    ; hand over: the multiboot2 magic + info pointer, exactly like GRUB
    mov eax, MB2_BOOT_MAGIC
    mov ebx, MB2_ADDR
    jmp KERNEL_ADDR + ENTRY_OFF

.hang:
    cli
    hlt
    jmp .hang
