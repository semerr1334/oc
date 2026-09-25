/*
 * OC OS — консоль: единый текстовый вывод поверх VGA-текста и/или
 * графического фреймбуфера. Всё, что печатает ядро, дублируется в COM1.
 */
#ifndef OC_CONSOLE_H
#define OC_CONSOLE_H

#include "types.h"
#include "boot.h"

/* 16 классических цветов */
enum oc_color {
    C_BLACK = 0, C_BLUE, C_GREEN, C_CYAN,
    C_RED, C_MAGENTA, C_BROWN, C_LIGHT_GRAY,
    C_DARK_GRAY, C_LIGHT_BLUE, C_LIGHT_GREEN, C_LIGHT_CYAN,
    C_LIGHT_RED, C_LIGHT_MAGENTA, C_YELLOW, C_WHITE,
};

void console_init(const oc_boot_info_t *bi);
bool console_is_graphic(void);
u32  console_width(void);          /* в символах */
u32  console_height(void);         /* в символах */
void console_clear(void);
void console_set_color(enum oc_color fg, enum oc_color bg);
void console_get_color(enum oc_color *fg, enum oc_color *bg);
void console_putc(char c);
void console_write(const char *s, size_t n);
void console_puts(const char *s);
void console_goto(u32 col, u32 row);

/* палитра: индекс цвета -> RGB */
u32 console_palette(enum oc_color c);

#endif /* OC_CONSOLE_H */
