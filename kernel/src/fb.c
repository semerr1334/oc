#include "fb.h"
#include "font.h"
#include "string.h"

static struct {
    volatile u32 *base;
    u32 pitch;          /* байт на строку */
    u32 w, h;           /* пиксели */
    u32 bpp;
    u8  rpos, rsize, gpos, gsize, bpos, bsize, resv_pos, resv_size;
    u32 cache[256];     /* кэш преобразования цветов 0xAARRGGBB-ish */
    bool ready;
} fb;

bool fb_init(const oc_boot_info_t *bi)
{
    if (!bi->fb_present || bi->fb_addr == 0)
        return false;
    fb.base  = (volatile u32 *)(uintptr_t)bi->fb_addr;
    fb.pitch = bi->fb_pitch;
    fb.w     = bi->fb_width;
    fb.h     = bi->fb_height;
    fb.bpp   = bi->fb_bpp;
    fb.rpos = bi->fb_rpos;  fb.rsize = bi->fb_rsize;
    fb.gpos = bi->fb_gpos;  fb.gsize = bi->fb_gsize;
    fb.bpos = bi->fb_bpos;  fb.bsize = bi->fb_bsize;
    fb.resv_pos = bi->fb_resv_pos; fb.resv_size = bi->fb_resv_size;
    fb.ready = (fb.bpp == 32 || fb.bpp == 24);
    return fb.ready;
}

bool fb_ready(void)
{
    return fb.ready;
}

u32 fb_width_px(void)  { return fb.w; }
u32 fb_height_px(void) { return fb.h; }
u32 fb_cols(void)      { return fb.w / GLYPH_W; }
u32 fb_rows(void)      { return fb.h / GLYPH_H; }

u32 fb_color(u8 r, u8 g, u8 b)
{
    u32 c = 0;
    c |= ((u32)(r >> (8 - fb.rsize))) << fb.rpos;
    c |= ((u32)(g >> (8 - fb.gsize))) << fb.gpos;
    c |= ((u32)(b >> (8 - fb.bsize))) << fb.bpos;
    return c;
}

void fb_putpixel(u32 x, u32 y, u32 color)
{
    if (x >= fb.w || y >= fb.h)
        return;
    if (fb.bpp == 32) {
        u32 *row = (u32 *)((u8 *)fb.base + y * fb.pitch);
        row[x] = color;
    } else { /* 24 bpp */
        u8 *row = (u8 *)fb.base + y * fb.pitch;
        u8 *p = row + x * 3;
        p[0] = (u8)color;
        p[1] = (u8)(color >> 8);
        p[2] = (u8)(color >> 16);
    }
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (x >= fb.w || y >= fb.h)
        return;
    if (x + w > fb.w) w = fb.w - x;
    if (y + h > fb.h) h = fb.h - y;
    for (u32 j = 0; j < h; j++) {
        if (fb.bpp == 32) {
            u32 *row = (u32 *)((u8 *)fb.base + (y + j) * fb.pitch);
            for (u32 i = 0; i < w; i++)
                row[x + i] = color;
        } else {
            for (u32 i = 0; i < w; i++)
                fb_putpixel(x + i, y + j, color);
        }
    }
}

void fb_fill(u32 color)
{
    fb_fill_rect(0, 0, fb.w, fb.h, color);
}

void fb_hline(u32 x, u32 y, u32 w, u32 color)
{
    fb_fill_rect(x, y, w, 1, color);
}

void fb_vline(u32 x, u32 y, u32 h, u32 color)
{
    fb_fill_rect(x, y, 1, h, color);
}

void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    fb_hline(x, y, w, color);
    fb_hline(x, y + h - 1, w, color);
    fb_vline(x, y, h, color);
    fb_vline(x + w - 1, y, h, color);
}

void fb_draw_char(u32 x, u32 y, u32 cp, u32 fg, u32 bg)
{
    u8 code = font_encode(cp);
    const u8 *glyph = font8x16[code];
    for (u32 row = 0; row < FONT_H; row++) {
        u8 bits = glyph[row];
        for (u32 col = 0; col < FONT_W; col++) {
            u32 color = (bits & (0x80 >> col)) ? fg : bg;
            if (color != 0xFFFFFFFF || bg != 0xFFFFFFFF)
                fb_putpixel(x + col, y + row, color);
        }
    }
}

void fb_draw_glyph(u32 col, u32 row, u32 cp, u32 fg, u32 bg)
{
    fb_draw_char(col * GLYPH_W, row * GLYPH_H, cp, fg, bg);
}

void fb_draw_string(u32 x, u32 y, const char *s, u32 fg, u32 bg)
{
    while (*s) {
        size_t adv;
        u32 cp = font_utf8_decode(s, &adv);
        fb_draw_char(x, y, cp, fg, bg);
        x += GLYPH_W;
        s += adv;
    }
}

void fb_scroll(u32 bg)
{
    u32 line_bytes = fb.pitch;
    u8 *base = (u8 *)fb.base;
    memmove(base, base + line_bytes * GLYPH_H,
            (size_t)(fb.h - GLYPH_H) * line_bytes);
    fb_fill_rect(0, fb.h - GLYPH_H, fb.w, GLYPH_H, bg);
}

void fb_save_rect(u32 x, u32 y, u32 w, u32 h, void *buf)
{
    u32 *out = buf;
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++) {
            u32 px = x + i, py = y + j;
            u32 v = 0;
            if (px < fb.w && py < fb.h) {
                if (fb.bpp == 32) {
                    u32 *row = (u32 *)((u8 *)fb.base + py * fb.pitch);
                    v = row[px];
                } else {
                    u8 *p = (u8 *)fb.base + py * fb.pitch + px * 3;
                    v = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
                }
            }
            *out++ = v;
        }
}

void fb_restore_rect(u32 x, u32 y, u32 w, u32 h, const void *buf)
{
    const u32 *in = buf;
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++)
            fb_putpixel(x + i, y + j, *in++);
}
