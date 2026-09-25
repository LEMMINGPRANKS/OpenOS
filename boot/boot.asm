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
    ; end tag
    dw 0
    dw 8
    dd 0
align 8
header_end:

section .bss
align 4096
pml4:   resb 4096                   ; level-4 page table
pdpt:   resb 4096                   ; level-3
pd:     resb 4096                   ; level-2 (512 entries = 1 GiB of 2 MiB pages)
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
    ; Park them in edi/esi so they survive into 64-bit mode as rdi/rsi
    ; (the first two C arguments).
    mov edi, eax
    mov esi, ebx

    ; --- identity-map the first 1 GiB with 2 MiB huge pages ---
    mov eax, pdpt
    or  eax, 0x3                    ; present | writable
    mov [pml4], eax
    mov eax, pd
    or  eax, 0x3
    mov [pdpt], eax

    xor ecx, ecx
.map_pd:
    mov eax, ecx
    shl eax, 21                     ; ecx * 2 MiB
    or  eax, 0x83                   ; present | writable | huge (PS bit)
    mov [pd + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jne .map_pd

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
    ; rdi = multiboot2 magic, rsi = info struct (parked in 32-bit code)
    call kmain

.hang:
    cli
    hlt
    jmp .hang
