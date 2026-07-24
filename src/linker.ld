ENTRY(_start)

SECTIONS {
    . = 0x80000;
    kernel_start = .;
    
    .text : {
        *boot*.o(.text .text.*)
        *boot*.obj(.text .text.*)
        *(.text .text.*)
        *(.rodata .rodata.*)
    }

    /* Round the executable region up to a 2MB boundary so the MMU can map it
       as a distinct block: code is read-only+executable (must not be
       EL0-writable), while writable data lives in the following block(s). */
    . = ALIGN(0x200000);
    text_end = .;

    .data : {
        *(.data .data.*)
    }

    /* The early BSS-clear loop in boot.S zeroes 8 bytes at a time, so both
       bounds must be 8-byte aligned (16 for headroom); otherwise the first
       store faults on an unaligned address. */
    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;
    __bss_size = __bss_end - __bss_start;
    
    . = ALIGN(16);
    stack_bottom = .;
    .stack (NOLOAD) : {
        . = . + 0x10000;
    }
    stack_top = .;
    
    . = ALIGN(16);
    heap_bottom = .;
    
    _end = .;
}
