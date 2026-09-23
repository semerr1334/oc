/*
 * OC OS — VGA-текстовый режим 80x25.
 */
#ifndef OC_VGA_H
#define OC_VGA_H

#include "types.h"

void vga_init(void);
void vga_clear(u8 attr);
void vga_set_cell(u32 col, u32 row, char c, u8 attr);
void vga_putc_at(u32 col, u32 row, char c, u8 attr);
void vga_set_cursor(u32 col, u32 row);
void vga_scroll(u8 attr);
u8   vga_attr(u8 fg, u8 bg);

#define VGA_COLS 80
#define VGA_ROWS 25

#endif /* OC_VGA_H */
