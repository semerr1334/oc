/*
 * OC OS — встроенный шрифт 8x16 (собственная кодировка OC8).
 *
 * Индекс глифа:
 *   0x00..0x7F — ASCII;
 *   0x80..0x9F — заглавные А..Я;
 *   0xA0..0xBF — строчные а..я;
 *   0xC0 — Ё, 0xC1 — ё, 0xC2 — №.
 */
#ifndef OC_FONT_H
#define OC_FONT_H

#include "types.h"

#define FONT_W        8
#define FONT_H        16
#define FONT_FIRST    0x20
#define FONT_LAST     0xC2
#define FONT_COUNT    (FONT_LAST + 1)

/* битовая карта: FONT_COUNT глифов по FONT_H байт (бит 7 = левый пиксель) */
extern const u8 font8x16[FONT_COUNT][FONT_H];

/* UTF-8 -> код OC8 (0, если глифа нет) */
u32 font_utf8_decode(const char *s, size_t *advance);
u8  font_encode(u32 codepoint);

#endif /* OC_FONT_H */
