#include "vga.h"
#include "io.h"

#define VGA_MEM ((volatile u16 *)0xB8000UL)

u8 vga_attr(u8 fg, u8 bg)
{
    return (u8)(fg | (bg << 4));
}

void vga_set_cell(u32 col, u32 row, char c, u8 attr)
{
    VGA_MEM[row * VGA_COLS + col] = (u16)((u8)c | ((u16)attr << 8));
}

void vga_putc_at(u32 col, u32 row, char c, u8 attr)
{
    vga_set_cell(col, row, c, attr);
}

void vga_clear(u8 attr)
{
    u16 fill = (u16)(' ' | ((u16)attr << 8));
    for (u32 i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = fill;
}

void vga_scroll(u8 attr)
{
    for (u32 r = 1; r < VGA_ROWS; r++)
        for (u32 c = 0; c < VGA_COLS; c++)
            VGA_MEM[(r - 1) * VGA_COLS + c] = VGA_MEM[r * VGA_COLS + c];
    u16 fill = (u16)(' ' | ((u16)attr << 8));
    for (u32 c = 0; c < VGA_COLS; c++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + c] = fill;
}

void vga_set_cursor(u32 col, u32 row)
{
    u16 pos = (u16)(row * VGA_COLS + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void vga_init(void)
{
    /* ничего не перепрограммируем — просто чистим и ставим курсор */
    vga_set_cursor(0, 0);
}
