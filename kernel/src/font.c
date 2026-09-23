#include "font.h"

u32 font_utf8_decode(const char *s, size_t *advance)
{
    const u8 *p = (const u8 *)s;
    u32 cp;
    size_t n;

    if (p[0] < 0x80) {
        cp = p[0];
        n = 1;
    } else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = (u32)((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        n = 2;
    } else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
               (p[2] & 0xC0) == 0x80) {
        cp = (u32)((p[0] & 0x0F) << 12) | (u32)((p[1] & 0x3F) << 6) |
             (p[2] & 0x3F);
        n = 3;
    } else {
        cp = 0xFFFD;
        n = 1;
    }
    if (advance)
        *advance = n;
    return cp;
}

u8 font_encode(u32 cp)
{
    if (cp < 0x80)
        return (u8)cp;
    /* Ё, ё */
    if (cp == 0x401)
        return 0xC0;
    if (cp == 0x451)
        return 0xC1;
    if (cp == 0x2116)   /* № */
        return 0xC2;
    /* А..Я -> 0x80..0x9F */
    if (cp >= 0x410 && cp <= 0x42F)
        return (u8)(0x80 + (cp - 0x410));
    /* а..я -> 0xA0..0xBF */
    if (cp >= 0x430 && cp <= 0x44F)
        return (u8)(0xA0 + (cp - 0x430));
    return 0; /* нет глифа */
}
