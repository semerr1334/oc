/*
 * OC OS — IDT, исключения, IRQ и системные вызовы (int 0x80).
 */
#ifndef OC_IDT_H
#define OC_IDT_H

#include "types.h"

/*
 * Стек-кадр прерывания (см. isr.S): снизу вверх — регистры в порядке
 * обратном push'ам, затем int_no/err (push'аются стабом), затем
 * аппаратный кадр iretq (rip, cs, rflags, rsp, ss).
 */
struct regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, err;
    u64 rip, cs, rflags, rsp, ss;
};

typedef void (*irq_handler_t)(struct regs *r);

void idt_init(void);
void irq_set_handler(u8 irq, irq_handler_t h);
void irq_set_mask(u8 irq, bool masked);
void irq_eoi(u8 irq);

NORETURN void panic_regs(struct regs *r, const char *fmt, ...);

/* точки входа стабов (isr.S) */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void isr128(void);

/* asm-хелперы */
void gdt_flush(void *gdt_desc, u16 code_sel, u16 data_sel);
void idt_load(void *idtr);
void tss_load(u16 sel);

#endif /* OC_IDT_H */
