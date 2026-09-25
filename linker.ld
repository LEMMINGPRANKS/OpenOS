ENTRY(_start)
SECTIONS
{
    . = 1M;

    .text : ALIGN(4K) {
        KEEP(*(.multiboot_header))
        *(.text*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    .data : ALIGN(4K) {
        *(.data*)
    }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
    }

    __kernel_end = ALIGN(4K);

    /DISCARD/ : {
        *(.eh_frame*) *(.note*) *(.comment)
    }
}
