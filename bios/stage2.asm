; OpenBIOS stage2 -- real mode: memory map (E820), A20, load the kernel
; from disk; then protected mode: copy it up to its final home, zero the
; kernel's bss, build a fake multiboot2 info block, and boot the kernel
; exactly the way GRUB would have. No initramfs: every file lives in the
; DR1 store on disk, and the kernel reads them with its own ATA driver.
;
; The Makefile patches in real sizes:
;   ENTRY_OFF      _start offset inside kernel.flat
;   KERNEL_SECTORS sectors of kernel.flat on disk
;   BSS_END_OFF    __kernel_end - 1 MiB (zero up to here)

%include "layout.inc"

%ifndef ENTRY_OFF
%define ENTRY_OFF 0x30
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 192
%endif
%ifndef BSS_END_OFF
%define BSS_END_OFF 0x1B0000
%endif

MB2_BOOT_MAGIC equ 0x36D76289
SMAP           equ 0x534D4150
E820_MAX       equ 128               ; entries we accept

bits 16
org STAGE2_ADDR

; partition boot header: the MBR verifies this magic before booting us.
; entry point is STAGE2_ENTRY (offset 8), straight past it.
    db "OPOS2", 0, 0, 0

start:
    mov [boot_drive], dl
    mov [part_base], di               ; 32-bit partition base LBA from MBR
    mov [part_base+2], bp

    ; --- dual boot menu (OpenBIOS owns the MBR, stage2 draws the menu)
    call boot_menu

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

    ; --- pick the kernel slot from the A/B header (self-update bookkeeping)
    ; defaults first: the values the Makefile baked in for THIS image
    mov word [chosen_sectors], KERNEL_SECTORS
    mov dword [chosen_entry], ENTRY_OFF
    mov dword [chosen_bss], BSS_END_OFF
    mov byte [chosen_slot], 0
    mov byte [chosen_cand], 0
    mov ebx, [part_base]                ; header sector -> KHDR_BUF
    add ebx, KHDR_LBA
    mov cx, 1
    mov ax, KHDR_BUF >> 4
    mov es, ax
    xor di, di
    call read_disk
    xor ax, ax                          ; gs = 0 so we can read KHDR_BUF
    mov gs, ax
    cmp dword [gs:KHDR_BUF], KSLOT_MAGIC
    jne .khdr_done                      ; fresh image: baked defaults

    mov eax, [gs:KHDR_BUF+20]           ; cand_slot
    cmp eax, 2                          ; 0=A 1=B, anything else = none
    jae .khdr_rollback_chk
    cmp dword [gs:KHDR_BUF+36], 0       ; already attempting a candidate?
    jne .khdr_rollback_chk
    ; stage the candidate: mark "booting" FIRST, so if it hangs we fall
    ; back to the last good kernel on the next reboot
    mov dword [gs:KHDR_BUF+36], 1
    call write_hdr
    jc .khdr_ok                         ; couldn't mark: don't risk it
    mov byte [chosen_cand], 1
    mov eax, [gs:KHDR_BUF+20]
    mov [chosen_slot], al
    mov eax, [gs:KHDR_BUF+24]
    mov [chosen_sectors], ax
    mov eax, [gs:KHDR_BUF+28]
    cmp eax, 0xFFFFFFFF
    je .khdr_ok                         ; incomplete entry: treat as bad
    mov [chosen_entry], eax
    mov eax, [gs:KHDR_BUF+32]
    cmp eax, 0xFFFFFFFF
    je .khdr_ok
    mov [chosen_bss], eax
    jmp .khdr_done
.khdr_rollback_chk:
    cmp dword [gs:KHDR_BUF+36], 0
    je .khdr_ok
    ; we marked booting last time but the kernel never confirmed itself:
    ; the candidate is bad -- scrap it and boot the last good kernel
    mov dword [gs:KHDR_BUF+20], 0xFFFFFFFF
    mov dword [gs:KHDR_BUF+36], 0
    call write_hdr
    mov si, msg_rollback
    call puts
.khdr_ok:
    cmp dword [gs:KHDR_BUF+4], 2        ; ok_slot valid?
    jae .khdr_done
    mov eax, [gs:KHDR_BUF+4]
    mov [chosen_slot], al
    mov eax, [gs:KHDR_BUF+8]
    test eax, eax
    jz .khdr_done                       ; sectors 0 = keep baked defaults
    mov [chosen_sectors], ax
    mov eax, [gs:KHDR_BUF+12]
    cmp eax, 0xFFFFFFFF
    je .khdr_done
    mov [chosen_entry], eax
    mov eax, [gs:KHDR_BUF+16]
    cmp eax, 0xFFFFFFFF
    je .khdr_done
    mov [chosen_bss], eax
.khdr_done:

    ; --- load the chosen kernel slot -> KERNEL_LOAD ---
    mov si, msg_kernel
    call puts
    movzx eax, byte [chosen_slot]
    shl eax, 9                          ; * KSLOT_SECT (512)
    add eax, KSLOT_A_LBA
    add eax, [part_base]
    mov ebx, eax
    mov cx, [chosen_sectors]
    mov ax, KERNEL_LOAD >> 4
    mov es, ax
    xor di, di
    call read_disk
    mov al, [chosen_slot]               ; say which slot booted
    add al, 'A'
    call putc
    mov si, msg_crlf
    call puts

    mov si, msg_pm
    call puts

    ; --- Bochs VBE dispi extension (QEMU/VBox/Bochs): set 1400x900x32 ---
    ; 1400x900 is not a standard VESA mode, so on virtual machines we program
    ; the dispi registers directly; real hardware falls through to the scan
    mov dx, 0x01CE
    mov ax, 0x0000                   ; index 0 = ID
    out dx, ax
    inc dx                           ; 0x01CF = data port
    in  ax, dx
    and ax, 0xFFF0
    cmp ax, 0xB0C0
    jne .no_dispi                    ; no dispi -> classic VBE scan below
    mov dx, 0x01CE
    mov ax, 0x0004                   ; ENABLE
    out dx, ax
    inc dx
    mov ax, 0x0000                   ; disable while we change geometry
    out dx, ax
    mov dx, 0x01CE
    mov ax, 0x0001                   ; XRES
    out dx, ax
    inc dx
    mov ax, 1400
    out dx, ax
    mov dx, 0x01CE
    mov ax, 0x0002                   ; YRES
    out dx, ax
    inc dx
    mov ax, 900
    out dx, ax
    mov dx, 0x01CE
    mov ax, 0x0003                   ; BPP
    out dx, ax
    inc dx
    mov ax, 32
    out dx, ax
    mov dx, 0x01CE
    mov ax, 0x0004                   ; ENABLE
    out dx, ax
    inc dx
    mov ax, 0x0041                   ; enabled | linear framebuffer
    out dx, ax
    ; read XRES back: if the card refused the geometry, use the scan instead
    mov dx, 0x01CE
    mov ax, 0x0001
    out dx, ax
    inc dx
    in  ax, dx
    cmp ax, 1400
    jne .no_dispi
    mov word [MODE_INFO + 0x10], 5600     ; pitch = 1400 * 4
    mov word [MODE_INFO + 0x12], 1400
    mov word [MODE_INFO + 0x14], 900
    mov byte [MODE_INFO + 0x19], 32
    mov byte [MODE_INFO + 0x1B], 6        ; memory model: direct colour
    mov dword [MODE_INFO + 0x28], 0xFD000000  ; QEMU std-VGA LFB (PCI BAR)
    mov byte [vbe_ok], 1
    jmp .vbe_done
.no_dispi:

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

; write the A/B header sector back (KHDR_BUF -> PART_BASE+KHDR_LBA).
; cf set = error.
write_hdr:
    mov word [dap_count], 1
    mov word [dap_off], KHDR_BUF & 0xF
    mov word [dap_seg], KHDR_BUF >> 4
    mov eax, [part_base]
    add eax, KHDR_LBA
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ax, 0x4300                      ; extended write, no verify
    int 0x13
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

; --- dual boot menu -------------------------------------------------------
; Lists OpenOS (1, the default) plus every non-empty non-OpenOS partition
; from the MBR table copy at PARTTAB (keys 2..4). ~3 s BIOS-tick timeout,
; then the default boots. Another partition = chainload: read its first
; sector to PBR_LOAD and jump with DL = drive, DS:SI -> its table entry.
boot_menu:
    mov cx, 4
    xor dx, dx                       ; others found so far
    mov si, PARTTAB
    mov di, menu_others
.scan:
    mov al, [si+4]                   ; type byte
    test al, al
    jz .next                         ; empty entry
    cmp al, PART_TYPE_OPENOS
    je .next                         ; ours: not a menu option
    mov ax, 4
    sub ax, cx                       ; entry number (0-3)
    mov [di], al
    inc di
    inc dx
.next:
    add si, 16
    loop .scan
    mov [menu_count], dl
    test dx, dx
    jz .ret                          ; no other partitions: straight to OpenOS

    mov si, msg_menu
    call puts

    push ds                          ; wait for a key, BIOS ticks as the clock
    mov ax, 0x0040
    mov ds, ax
    mov ax, [0x006C]
    add ax, MENU_TICKS               ; deadline (midnight wrap: vanishingly rare)
    mov bx, ax
.wait:
    mov ah, 1
    int 0x16
    jz  .tick
    mov ah, 0                        ; consume the keypress
    int 0x16
    jmp .got
.tick:
    mov ax, [0x006C]
    cmp ax, bx
    jb .wait                         ; while now < deadline (wrap-safe enough)
    mov al, '1'                      ; timeout: default OpenOS
.got:
    pop ds
    cmp al, '1'
    je .ret
    cmp al, '2'
    jb .ret
    cmp al, '4'
    ja .ret
    sub al, '2'
    cmp al, [menu_count]
    jae .ret                         ; no such entry: default
    mov bx, menu_others
    xlat                             ; al = partition entry number (ds = 0)
    mov cl, 16
    mul cl                           ; ax = entry * 16
    mov si, PARTTAB
    add si, ax                       ; ds:si -> the partition entry
    push si                          ; puts trashes si AND bx (teletype page)

    mov si, msg_other_boot
    call puts

    pop si
    mov eax, [si+8]                  ; that partition's start LBA
    mov dword [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov word [dap_count], 1
    mov word [dap_off], 0            ; seg:off pair: 0x7C0:0 -> linear 0x7C00
    mov word [dap_seg], PBR_LOAD >> 4
    mov bx, si                       ; keep the entry for the handover
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    mov [disk_err], ah               ; err_disk prints this
    jc  err_disk
    mov dl, [boot_drive]
    mov si, bx                       ; chainload law: DS:SI -> table entry
    jmp 0x0000:PBR_LOAD
.ret:
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
part_base:   dd 0                   ; OpenOS partition start LBA (from MBR)
menu_others: db 0, 0, 0             ; entry numbers of the other partitions
menu_count:  db 0
e820_count:  dd 0
disk_err:    db 0
vbe_want:    db 32
vbe_found:   dw 0
vbe_mode:    dw 0
vbe_ok:      db 0
chosen_sectors: dw KERNEL_SECTORS
chosen_entry:   dd ENTRY_OFF
chosen_bss:     dd BSS_END_OFF
chosen_slot:    db 0
chosen_cand:    db 0

msg_stage2:   db "OpenBIOS stage2", 13, 10, 0
msg_menu:     db "OpenBIOS boot menu", 13, 10, " 1) OpenOS", 13, 10, " 2) other OS (Linux)", 13, 10, 0
msg_other_boot: db " chainloading the other OS...", 13, 10, 0
msg_mem:      db " memory mapped", 13, 10, 0
msg_kernel:   db " loading kernel slot ", 0
msg_rollback: db " kernel update failed -- rolling back", 13, 10, 0
msg_pm:       db " protected mode...", 13, 10, 0
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

    ; copy kernel.flat up to its link address (runtime slot size)
    mov esi, KERNEL_LOAD
    mov edi, KERNEL_ADDR
    movzx ecx, word [chosen_sectors]
    shl ecx, 7                       ; sectors -> dwords
    rep movsd

    ; zero the kernel's bss (GRUB did this for ELF; we do it for flat)
    movzx eax, word [chosen_sectors]
    shl eax, 9                       ; sectors -> bytes
    mov edi, KERNEL_ADDR
    add edi, eax
    mov ecx, [chosen_bss]
    shr eax, 2
    sub ecx, eax                     ; (bss_end - loaded) -> dwords
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

    ; (no module tag: OpenOS boots with DR1 as its filesystem)

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

    ; tag 0x1337 (ours): which kernel slot booted + candidate flag, so the
    ; kernel can confirm itself after a self-update
    mov eax, 0x1337
    stosd
    mov eax, 16
    stosd
    mov eax, 0
    mov al, [chosen_slot]
    stosd
    mov eax, 0
    mov al, [chosen_cand]
    stosd

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
    mov edx, KERNEL_ADDR
    add edx, [chosen_entry]
    jmp edx

.hang:
    cli
    hlt
    jmp .hang
