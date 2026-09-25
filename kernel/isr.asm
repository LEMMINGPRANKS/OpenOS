; OpenOS interrupt stubs — one per vector, 0-31 exceptions, 32-47 IRQs

extern isr_dispatch

%macro ISR 1
isr_stub_%1:
    push qword %1               ; vector number as fake first arg
    jmp common_stub
%endmacro

%assign v 0
%rep 48
    ISR v
%assign v v+1
%endrep

section .text
bits 64
common_stub:
    ; save all registers (15 pushes = 120 bytes)
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, [rsp + 120]        ; vector
    mov rsi, rsp                ; pointer to saved registers
    sub rsp, 8                  ; 16-byte align for the C ABI
    call isr_dispatch
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 8                  ; drop vector
    iretq

section .data
global isr_stub_table
isr_stub_table:
%assign v 0
%rep 48
    dq isr_stub_ %+ v
%assign v v+1
%endrep
