#include "console.h"
#include "serial.h"
#include "vga.h"
#include "fb.h"
#include "font.h"
#include "string.h"

/* RGB-палитра 16 классических цветов */
static const u32 palette_rgb[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xAA }, { 0x00, 0xAA, 0x00 },
    { 0x00, 0xAA, 0xAA }, { 0xAA, 0x00, 0x00 }, { 0xAA, 0x00, 0xAA },
    { 0xAA, 0x55, 0x00 }, { 0xAA, 0xAA, 0xAA }, { 0x55, 0x55, 0x55 },
    { 0x55, 0x55, 0xFF }, { 0x55, 0xFF, 0x55 }, { 0x55, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55 }, { 0xFF, 0x55, 0xFF }, { 0xFF, 0xFF, 0x55 },
    { 0xFF, 0xFF, 0xFF },
};

static struct {
    bool graphic;
    u32 cols, rows;
    u32 col, row;
    u8 fg, bg;
} con;

u32 console_palette(enum oc_color c)
{
    if (c > C_WHITE)
        c = C_WHITE;
    return fb_color((u8)palette_rgb[c][0],
                    (u8)palette_rgb[c][1],
                    (u8)palette_rgb[c][2]);
}

void console_init(const oc_boot_info_t *bi)
{
    con.fg = C_LIGHT_GRAY;
    con.bg = C_BLACK;
    con.col = con.row = 0;

    con.graphic = fb_init(bi);
    if (con.graphic) {
        con.cols = fb_cols();
        con.rows = fb_rows();
        fb_fill(console_palette(con.bg));
    } else {
        vga_init();
        con.cols = VGA_COLS;
        con.rows = VGA_ROWS;
        vga_clear(vga_attr(con.fg, con.bg));
    }
}

bool console_is_graphic(void) { return con.graphic; }
u32 console_width(void)       { return con.cols; }
u32 console_height(void)      { return con.rows; }

void console_set_color(enum oc_color fg, enum oc_color bg)
{
    con.fg = (u8)fg;
    con.bg = (u8)bg;
}

void console_get_color(enum oc_color *fg, enum oc_color *bg)
{
    if (fg) *fg = (enum oc_color)con.fg;
    if (bg) *bg = (enum oc_color)con.bg;
}

void console_goto(u32 col, u32 row)
{
    if (col >= con.cols) col = con.cols - 1;
    if (row >= con.rows) row = con.rows - 1;
    con.col = col;
    con.row = row;
    if (!con.graphic)
        vga_set_cursor(col, row);
}

void console_clear(void)
{
    if (con.graphic)
        fb_fill(console_palette(con.bg));
    else
        vga_clear(vga_attr(con.fg, con.bg));
    con.col = con.row = 0;
    console_goto(0, 0);
}

static void scroll_if_needed(void)
{
    while (con.row >= con.rows) {
        if (con.graphic)
            fb_scroll(console_palette(con.bg));
        else
            vga_scroll(vga_attr(con.fg, con.bg));
        con.row--;
    }
}

static void draw_cell(u32 col, u32 row, u32 cp)
{
    if (con.graphic)
        fb_draw_glyph(col, row, cp,
                      console_palette((enum oc_color)con.fg),
                      console_palette((enum oc_color)con.bg));
    else
        vga_set_cell(col, row, (char)cp, vga_attr(con.fg, con.bg));
}

/* посимвольный (в кодировке OC8) вывод */
static void putc_oc8(u8 c)
{
    switch (c) {
    case '\n':
        con.col = 0;
        con.row++;
        break;
    case '\r':
        con.col = 0;
        break;
    case '\b':
        if (con.col > 0) {
            con.col--;
            draw_cell(con.col, con.row, ' ');
        } else if (con.row > 0) {
            con.row--;
            con.col = con.cols - 1;
            draw_cell(con.col, con.row, ' ');
        }
        break;
    case '\t': {
        u32 next = (con.col + 4) & ~(u32)3;
        while (con.col < next && con.col < con.cols) {
            draw_cell(con.col, con.row, ' ');
            con.col++;
        }
        break;
    }
    default:
        draw_cell(con.col, con.row, c);
        con.col++;
        if (con.col >= con.cols) {
            con.col = 0;
            con.row++;
        }
        break;
    }
    scroll_if_needed();
    if (!con.graphic)
        vga_set_cursor(con.col, con.row);
}

void console_putc(char ch)
{
    /* всё, что печатается на экран, дублируется в COM1 (в UTF-8) */
    serial_putc(ch);

    /* UTF-8 -> OC8 */
    static u8 utf8_buf[4];
    static u32 utf8_len, utf8_need;
    u8 c = (u8)ch;

    if (utf8_need == 0) {
        if (c < 0x80) {
            putc_oc8(c);
            return;
        }
        if ((c & 0xE0) == 0xC0) {
            utf8_need = 2;
            utf8_buf[0] = c;
            utf8_len = 1;
        } else if ((c & 0xF0) == 0xE0) {
            utf8_need = 3;
            utf8_buf[0] = c;
            utf8_len = 1;
        } else {
            putc_oc8('?');
        }
        return;
    }
    if ((c & 0xC0) != 0x80) {   /* сломанная последовательность */
        utf8_need = 0;
        putc_oc8('?');
        console_putc(ch);
        return;
    }
    utf8_buf[utf8_len++] = c;
    if (utf8_len == utf8_need) {
        utf8_need = 0;
        size_t adv;
        u32 cp = font_utf8_decode((const char *)utf8_buf, &adv);
        u8 code = font_encode(cp);
        putc_oc8(code ? code : '?');
    }
}

void console_write(const char *s, size_t n)
{
    while (n--)
        console_putc(*s++);
}

void console_puts(const char *s)
{
    while (*s)
        console_putc(*s++);
}
