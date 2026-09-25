/*
 * OC OS — графический фреймбуфер (VBE LFB) и растровая графика.
 */
#ifndef OC_FB_H
#define OC_FB_H

#include "types.h"
#include "boot.h"

#define GLYPH_W 8
#define GLYPH_H 16

bool fb_init(const oc_boot_info_t *bi);
bool fb_ready(void);

u32  fb_width_px(void);
u32  fb_height_px(void);
u32  fb_cols(void);              /* колонок текста */
u32  fb_rows(void);              /* строк текста */

u32  fb_color(u8 r, u8 g, u8 b);
void fb_fill(u32 color);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_putpixel(u32 x, u32 y, u32 color);
void fb_hline(u32 x, u32 y, u32 w, u32 color);
void fb_vline(u32 x, u32 y, u32 h, u32 color);
void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

void fb_draw_glyph(u32 col, u32 row, u32 cp, u32 fg, u32 bg);
void fb_draw_char(u32 x, u32 y, u32 cp, u32 fg, u32 bg);
void fb_draw_string(u32 x, u32 y, const char *s, u32 fg, u32 bg);
void fb_scroll(u32 bg);

/* сохранение/восстановление прямоугольника (для курсора мыши) */
void fb_save_rect(u32 x, u32 y, u32 w, u32 h, void *buf);
void fb_restore_rect(u32 x, u32 y, u32 w, u32 h, const void *buf);

#endif /* OC_FB_H */
