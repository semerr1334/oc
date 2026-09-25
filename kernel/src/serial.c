#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* без прерываний */
    outb(COM1 + 3, 0x80);   /* DLAB: делитель */
    outb(COM1 + 0, 0x01);   /* 115200 бод */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1 */
    outb(COM1 + 2, 0xC7);   /* FIFO: вкл, очистка, 14 байт */
    outb(COM1 + 4, 0x0B);   /* DTR|RTS|OUT2 */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');
    while ((inb(COM1 + 5) & 0x20) == 0)
        cpu_pause();
    outb(COM1, (u8)c);
    debugcon(c);            /* дублируем в debugcon (удобно в QEMU/эмуляторе) */
}

void serial_write(const char *s, size_t n)
{
    while (n--)
        serial_putc(*s++);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
