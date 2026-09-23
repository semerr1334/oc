#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "kprintf.h"
#include "string.h"

struct idt_entry {
    u16 off_lo;
    u16 sel;
    u8  ist;
    u8  type_attr;
    u16 off_mid;
    u32 off_hi;
    u32 zero;
} PACKED;

static struct idt_entry idt[256] ALIGNED(16);
static irq_handler_t irq_handlers[16];


static void idt_set_gate(u8 vec, void (*handler)(void), u8 type_attr)
{
    u64 off = (u64)(uintptr_t)handler;
    idt[vec].off_lo = (u16)(off & 0xFFFF);
    idt[vec].sel = GDT_KERNEL_CODE;
    idt[vec].ist = 0;
    idt[vec].type_attr = type_attr;
    idt[vec].off_mid = (u16)((off >> 16) & 0xFFFF);
    idt[vec].off_hi = (u32)(off >> 32);
    idt[vec].zero = 0;
}

void irq_set_handler(u8 irq, irq_handler_t h)
{
    if (irq < 16)
        irq_handlers[irq] = h;
}

void irq_set_mask(u8 irq, bool masked)
{
    pic_set_mask(irq, masked);
}

void irq_eoi(u8 irq)
{
    pic_eoi(irq);
}

void idt_init(void)
{
    static void (*const stubs[32])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    };
    static void (*const irq_stubs[16])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15,
    };

    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    for (u8 i = 0; i < 32; i++)
        idt_set_gate(i, stubs[i], 0x8E);
    for (u8 i = 0; i < 16; i++)
        idt_set_gate((u8)(IRQ_BASE + i), irq_stubs[i], 0x8E);
    /* int 0x80: доступен из ring 3 (для будущего пользовательского режима) */
    idt_set_gate(0x80, isr128, 0xEE);

    struct {
        u16 limit;
        u64 base;
    } PACKED desc = { sizeof(idt) - 1, (u64)(uintptr_t)idt };
    idt_load(&desc);
}

void isr_dispatch(struct regs *r)
{
    if (r->int_no < 32) {
        panic_regs(r, "%s", "необработанное исключение процессора");
    } else if (r->int_no < 48) {
        u8 irq = (u8)(r->int_no - IRQ_BASE);
        irq_handler_t h = irq_handlers[irq];
        if (h)
            h(r);
        pic_eoi(irq);
    } else if (r->int_no == 0x80) {
        extern u64 syscall_dispatch(struct regs * r);
        r->rax = syscall_dispatch(r);
    }
}
