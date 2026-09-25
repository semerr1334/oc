#include "pic.h"
#include "io.h"

void pic_init(void)
{
    /* начальная последовательность ICW1-4: векторы 0x20..0x2F */
    outb(PIC_MASTER_CMD, 0x11);
    io_wait();
    outb(PIC_SLAVE_CMD, 0x11);
    io_wait();
    outb(PIC_MASTER_DATA, IRQ_BASE);
    io_wait();
    outb(PIC_SLAVE_DATA, IRQ_BASE + 8);
    io_wait();
    outb(PIC_MASTER_DATA, 0x04);    /* slave на IRQ2 */
    io_wait();
    outb(PIC_SLAVE_DATA, 0x02);
    io_wait();
    outb(PIC_MASTER_DATA, 0x01);    /* 8086-режим */
    io_wait();
    outb(PIC_SLAVE_DATA, 0x01);
    io_wait();
    /* все линии замаскированы */
    outb(PIC_MASTER_DATA, 0xFB);    /* IRQ2 не маскируем (каскад) */
    outb(PIC_SLAVE_DATA, 0xFF);
}

void pic_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC_SLAVE_CMD, PIC_EOI);
    outb(PIC_MASTER_CMD, PIC_EOI);
}

void pic_set_mask(u8 irq, bool masked)
{
    u16 port = irq < 8 ? PIC_MASTER_DATA : PIC_SLAVE_DATA;
    u8 bit = (u8)(1 << (irq & 7));
    u8 v = inb(port);
    if (masked)
        v |= bit;
    else
        v &= (u8)~bit;
    outb(port, v);
}

u16 pic_get_mask(void)
{
    return (u16)(inb(PIC_MASTER_DATA) | ((u16)inb(PIC_SLAVE_DATA) << 8));
}
