; OpenBIOS MBR -- the first 512 bytes of the drive. BIOS loads us at
; 0x7C00; we read stage2 in with BIOS extended disk reads and jump.

%include "layout.inc"

bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov si, msg_boot
    call puts

    ; BIOS extended reads available? (int 13h AH=41h)
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc  err_noext
    cmp bx, 0xAA55
    jne err_noext

    ; load stage2: LBA 1, 128 sectors -> 0x8000
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  err_disk

    mov dl, [boot_drive]             ; stage2 wants the boot drive
    jmp 0x0000:STAGE2_ADDR           ; far jump sets CS to match org

err_noext:
    mov si, msg_noext
    call puts
    jmp halt
err_disk:
    mov si, msg_disk
    call puts
halt:
    hlt
    jmp halt

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

align 4
dap:
    db 0x10, 0                       ; packet size, reserved
    dw STAGE2_SECT                   ; sectors to read
    dw STAGE2_ADDR, 0x0000           ; dest offset:segment
    dq STAGE2_LBA                    ; start LBA

boot_drive: db 0

msg_boot:  db "OpenBIOS MBR", 13, 10, 0
msg_noext: db "no BIOS disk extensions (int 13h 42h)", 13, 10, 0
msg_disk:  db "disk read failed", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
