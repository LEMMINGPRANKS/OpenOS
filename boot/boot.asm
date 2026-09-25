; OpenOS boot: multiboot2 header + 32-bit → 64-bit long-mode trampoline
; GRUB loads us in 32-bit protected mode with paging OFF.
; We build page tables, flip the long-mode switch, and jump.

MB2_MAGIC   equ 0xE85250D6          ; magic GRUB searches for
MB2_BOOT_MAGIC equ 0x36D76289       ; value in eax when GRUB boots us

section .multiboot_header
align 8
header_start:
    dd MB2_MAGIC
    dd 0                            ; architecture: i386 (32-bit protected)
    dd header_end - header_start
    dd 0x100000000 - (MB2_MAGIC + 0 + (header_end - header_start)) ; checksum
    ; framebuffer request tag: any preferred graphics mode
    align 8
    dw 5                            ; type = framebuffer
    dw 0                            ; flags = optional
    dd 20                           ; size
    dd 0                            ; width  (0 = preferred)
    dd 0                            ; height (0 = preferred)
    dd 32                           ; depth  (we want 32 bpp)
    ; end tag
    align 8
    dw 0
    dw 8
    dd 0
align 8
header_end:

section .bss
align 4096
pml4:   resb 4096                   ; level-4 page table
pdpt:   resb 4096                   ; level-3
pd0:    resb 4096 * 4               ; level-2 x4 (512 entries each = 1 GiB of 2 MiB pages)
mb_magic: resd 1                    ; parked multiboot2 magic
mb_info:  resd 1                    ; parked multiboot2 info pointer
align 16
stack_bottom:
    resb 65536                      ; 64 KiB bootstrap stack
stack_top:

section .rodata
align 8
gdt_start:
    dq 0                            ; null descriptor
    dq 0x00209A0000000000           ; 64-bit code: L=1, present, executable
    dq 0x0000920000000000           ; 64-bit data: present, writable
gdt_end:
gdt_ptr:
    dw gdt_end - gdt_start - 1
    dq gdt_start

section .text
global _start
extern kmain

bits 32
_start:
    cli
    mov esp, stack_top

    ; GRUB left the multiboot2 magic in eax and the info pointer in ebx.
    ; Park them in memory -- the mapping loops below need every register.
    mov [mb_magic], eax
    mov [mb_info], ebx

    ; --- identity-map ALL 4 GiB with 2 MiB huge pages ---
    ; (QEMU puts the VBE framebuffer way up at 0xFD000000)
    mov eax, pdpt
    or  eax, 0x3                    ; present | writable
    mov [pml4], eax

    xor ecx, ecx                    ; ecx = which GiB (0..3)
.map_gib:
    mov edx, ecx
    shl edx, 12
    mov eax, pd0
    add eax, edx                    ; eax = &pd0[GiB]
    or  eax, 0x3
    mov [pdpt + ecx*8], eax

    mov esi, ecx
    shl esi, 30                     ; esi = base address of this GiB
    xor edi, edi                    ; edi = entry index within the GiB
.fill_pd:
    mov eax, edi
    shl eax, 21                     ; entry * 2 MiB
    add eax, esi
    or  eax, 0x83                   ; present | writable | huge (PS bit)
    mov edx, ecx
    shl edx, 12
    add edx, pd0
    mov [edx + edi*8], eax
    inc edi
    cmp edi, 512
    jne .fill_pd
    inc ecx
    cmp ecx, 4
    jne .map_gib

    ; --- switch the CPU on: PAE, EFER.LME, paging ---
    mov eax, cr4
    or  eax, 1 << 5                 ; PAE
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080             ; EFER MSR
    rdmsr
    or  eax, 1 << 8                 ; LME = long mode enable
    wrmsr

    mov eax, cr0
    or  eax, 0x80000001             ; PG | PE
    mov cr0, eax

    lgdt [gdt_ptr]
    jmp 0x08:long_mode_start        ; far jump loads the 64-bit code segment

bits 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, stack_top
    ; rdi = multiboot2 magic, rsi = info struct (parked in memory earlier)
    mov edi, [mb_magic]
    mov esi, [mb_info]
    call kmain

.hang:
    cli
    hlt
    jmp .hang
