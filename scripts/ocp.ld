/* Линкер-скрипт программ OC (.ocp): flat-образ, вход — строго с нуля. */
SECTIONS
{
    . = 0x2000000;
    .text : {
        *(.text.entry)      /* _entry — ПЕРВЫМ (вход по смещению 0) */
        *(.text)
        *(.text.*)
    }
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    .data : {
        *(.data)
        *(.data.*)
        *(.bss)
        *(COMMON)
    }
    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
