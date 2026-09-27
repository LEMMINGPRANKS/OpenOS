; Fake "other OS" partition boot sector for QEMU dual-boot tests. When
; stage2's menu chainloads it, it prints a banner and halts -- proof the
; chainload worked without needing a whole second OS in the test image.

bits 16
org 0x7C00

start:
    xor ax, ax
    mov ds, ax
    mov si, msg
.puts:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .puts
.done:
    hlt
    jmp .done

msg: db "Other OS booted! (chainload works)", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
