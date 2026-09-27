; OpenBIOS MBR -- owns LBA 0 of the boot drive. Dual boot law: the drive
; has a REAL partition table; we find the OpenOS partition (type byte
; 0x7F, "OPOS2" magic in its first sector), copy the table to PARTTAB
; for stage2's boot menu, load stage2 from inside the partition, and
; hand over the partition base LBA in DI:BP.

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

    ; --- scan our own partition table for the OpenOS partition (type 0x7F)
    mov si, 0x7C00 + PART_TABLE_OFF
    mov cx, 4
.scan:
    cmp byte [si+4], PART_TYPE_OPENOS
    je  .found
    add si, 16
    loop .scan
    jmp err_nopart
.found:
    mov bx, si                       ; bx = our partition entry

    ; copy the whole table (64 bytes) to PARTTAB for stage2's menu
    mov di, PARTTAB
    mov si, 0x7C00 + PART_TABLE_OFF
    mov cx, 32
    rep movsw

    ; --- verify the "OPOS2" magic in the partition's first sector
    mov eax, [bx+8]                  ; partition start LBA (low 32 bits)
    mov dword [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov word [dap_count], 1
    mov word [dap_off], 0x7E00
    mov word [dap_seg], 0x0000
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc  err_disk
    cmp dword [0x7E00], STAGE2_MAGIC_D
    jne err_magic
    cmp byte [0x7E00+4], STAGE2_MAGIC_B
    jne err_magic

    ; --- load stage2 from inside the partition -> 0x8000
    mov eax, [bx+8]
    add eax, STAGE2_LBA
    mov dword [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov word [dap_count], STAGE2_SECT
    mov word [dap_off], 0               ; seg:off pair, not a linear address
    mov word [dap_seg], STAGE2_ADDR >> 4
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc  err_disk

    ; hand over: DL = boot drive, DI:BP = 32-bit partition base LBA
    mov dl, [boot_drive]
    mov di, [bx+8]
    mov bp, [bx+10]
    jmp 0x0000:STAGE2_ENTRY

err_noext:
    mov si, msg_noext
    call puts
    jmp halt
err_nopart:
    mov si, msg_nopart
    call puts
    jmp halt
err_magic:
    mov si, msg_magic
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
dap_count: dw 0                      ; sectors to read
dap_off:   dw 0                      ; dest offset
dap_seg:   dw 0                      ; dest segment
dap_lba:   dq 0                      ; start LBA

boot_drive: db 0

msg_boot:   db "OpenBIOS MBR", 13, 10, 0
msg_noext:  db "no BIOS disk extensions", 13, 10, 0
msg_nopart: db "no OpenOS partition (type 0x7F)", 13, 10, 0
msg_magic:  db "OpenOS partition: bad magic", 13, 10, 0
msg_disk:   db "disk read failed", 13, 10, 0

times 446-($-$$) db 0                ; code+data must fit before the table
times 64  db 0                       ; the partition table lives here
dw 0xAA55
