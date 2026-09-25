/*
 * OC OS — графический фреймбуфер (VBE LFB / UEFI GOP) и растровая графика.
 *
 * ВАЖНО: fb_draw_char принимает уже код OC8 (индекс глифа шрифта),
 * а fb_draw_string — UTF-8 (сам декодирует). Дважды кодировку НЕ применяем!
 * На больших экранах шрифт рисуется масштабом fb.sc (1 или 2) — «крупнее».
 */
#include "fb.h"
#include "font.h"
#include "string.h"

static struct {
    volatile u8 *base;
    u32 pitch;          /* байт на строку */
    u32 w, h;           /* пиксели */
    u32 bpp;
    u8  rpos, rsize, gpos, gsize, bpos, bsize, resv_pos, resv_size;
    u32 sc;             /* масштаб глифа: пиксель шрифта = sc x sc пикселей */
    bool ready;
} fb;

bool fb_init(const oc_boot_info_t *bi)
{
    if (!bi->fb_present || bi->fb_addr == 0)
        return false;
    fb.base  = (volatile u8 *)(uintptr_t)bi->fb_addr;
    fb.pitch = bi->fb_pitch;
    fb.w     = bi->fb_width;
    fb.h     = bi->fb_height;
    fb.bpp   = bi->fb_bpp;
    fb.rpos = bi->fb_rpos;  fb.rsize = bi->fb_rsize;
    fb.gpos = bi->fb_gpos;  fb.gsize = bi->fb_gsize;
    fb.bpos = bi->fb_bpos;  fb.bsize = bi->fb_bsize;
    fb.resv_pos = bi->fb_resv_pos; fb.resv_size = bi->fb_resv_size;
    fb.ready = (fb.bpp == 32 || fb.bpp == 24);
    /* выше 1024x768 обычный 8x16 не читается — рисуем вдвое крупнее */
    fb.sc = ((u64)fb.w * fb.h > 1024ull * 768ull) ? 2 : 1;
    return fb.ready;
}

bool fb_ready(void)
{
    return fb.ready;
}

u32 fb_width_px(void)  { return fb.w; }
u32 fb_height_px(void) { return fb.h; }
u32 fb_scale(void)     { return fb.sc; }
u32 fb_cols(void)      { return fb.w / (GLYPH_W * fb.sc); }
u32 fb_rows(void)      { return fb.h / (GLYPH_H * fb.sc); }

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
        u32 *row = (u32 *)(fb.base + (size_t)y * fb.pitch);
        row[x] = color;
    } else { /* 24 bpp */
        u8 *row = fb.base + (size_t)y * fb.pitch;
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
            u32 *row = (u32 *)(fb.base + (size_t)(y + j) * fb.pitch);
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

/* x, y — пиксели; code — УЖЕ код OC8 (индекс font8x16), не codepoint! */
void fb_draw_char(u32 x, u32 y, u32 code, u32 fg, u32 bg)
{
    const u8 *glyph = font8x16[(u8)code];
    u32 s = fb.sc;
    for (u32 row = 0; row < FONT_H; row++) {
        u8 bits = glyph[row];
        for (u32 col = 0; col < FONT_W; col++) {
            u32 color = (bits & (0x80 >> col)) ? fg : bg;
            fb_fill_rect(x + col * s, y + row * s, s, s, color);
        }
    }
}

void fb_draw_glyph(u32 col, u32 row, u32 cp, u32 fg, u32 bg)
{
    fb_draw_char(col * GLYPH_W * fb.sc, row * GLYPH_H * fb.sc, cp, fg, bg);
}

/* s — UTF-8; сам декодирует и переводит в OC8 ровно один раз */
void fb_draw_string(u32 x, u32 y, const char *s, u32 fg, u32 bg)
{
    while (*s) {
        size_t adv;
        u32 cp = font_utf8_decode(s, &adv);
        u8 code = font_encode(cp);
        fb_draw_char(x, y, code ? code : '?', fg, bg);
        x += GLYPH_W * fb.sc;
        s += adv;
    }
}

void fb_scroll(u32 bg)
{
    u32 cell = GLYPH_H * fb.sc;
    u8 *base = fb.base;
    memmove(base, base + (size_t)fb.pitch * cell,
            (size_t)(fb.h - cell) * fb.pitch);
    fb_fill_rect(0, fb.h - cell, fb.w, cell, bg);
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
                    u32 *row = (u32 *)(fb.base + (size_t)py * fb.pitch);
                    v = row[px];
                } else {
                    u8 *p = fb.base + (size_t)py * fb.pitch + px * 3;
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
