/*
 * OC OS — раскладка ядра в памяти (плоский бинарник по адресу 1MiB).
 */
ENTRY(_start)

SECTIONS
{
    . = 0x00100000;
    _kernel_start = .;

    .text : {
        *(.text.entry)          /* _start обязан быть первым — загрузчик прыгает на 0x100000 */
        *(.text .text.*)
    }
    . = ALIGN(16);
    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(16);
    .data : {
        *(.data .data.*)
    }

    /* .bss не входит в файл — ядро само её обнуляет */
    . = ALIGN(4096);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(4096);
    _bss_end = .;
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame*)
        *(.note*)
        *(.interp)
    }
}

ASSERT(_start == 0x00100000, "ERROR: _start must be at 0x100000 (boot jumps there)")
