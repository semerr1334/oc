/*
 * OC OS — вход в ядро (64-bit). Загрузчик прыгает сюда с RDI = bootinfo.
 */
    .code64
    .section .text.entry, "ax"
    .global _start
_start:
    cli
    /* сохраняем указатель bootinfo */
    movq    %rdi, %r15

    /* свой стек */
    movq    $boot_stack_top, %rsp
    xorq    %rbp, %rbp

    /* чистим BSS */
    movq    $_bss_start, %rdi
    movq    $_bss_end, %rcx
    subq    %rdi, %rcx
    xorl    %eax, %eax
    cld
    rep     stosb

    movq    $boot_stack_top, %rsp
    movq    %r15, %rdi              /* аргумент kmain */
    call    kmain

1:  hlt
    jmp     1b

    .section .bss
    .align  16
    .global boot_stack
boot_stack:
    .space  32768                   /* 32 KiB стека */
    .global boot_stack_top
boot_stack_top:
