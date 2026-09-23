/*
 * OC OS — стабы обработчиков прерываний и исключений (64-bit).
 *
 * Каждый стаб кладёт номер вектора (+ код ошибки процессором или нулём
 * фиктивно) и прыгает в isr_common, который сохраняет регистры и зовёт
 * isr_dispatch(struct regs *).
 */
#include "layout.h"

    .code64
    .section .text

/* стабы без кода ошибки: дописываем фиктивный 0 */
.macro ISR_NOERR n
    .global isr\n
isr\n:
    pushq   $0
    pushq   $\n
    jmp     isr_common
.endm

/* стабы с кодом ошибки от CPU */
.macro ISR_ERR n
    .global isr\n
isr\n:
    pushq   $\n
    jmp     isr_common
.endm

.macro IRQ_STUB n
    .global irq\n
irq\n:
    pushq   $0
    pushq   $(32 + \n)
    jmp     isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

/* системный вызов: int 0x80 */
    .global isr128
isr128:
    pushq   $0
    pushq   $0x80
    jmp     isr_common

    .extern isr_dispatch
isr_common:
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rsp, %rdi          /* struct regs * */
    cld
    call    isr_dispatch

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax
    addq    $16, %rsp           /* int_no + err */
    iretq

/* ---- загрузка GDT / IDT / TSS ---- */
    .global gdt_flush
gdt_flush:
    lgdt    (%rdi)
    movq    %rdx, %rax          /* data_sel */
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    /* дальний переход, чтобы CS стал code_sel */
    pushq   %rsi                /* code_sel */
    leaq    1f(%rip), %rax
    pushq   %rax
    lretq
1:  ret

    .global idt_load
idt_load:
    lidt    (%rdi)
    ret

    .global tss_load
tss_load:
    ltr     %di
    ret
