#!/usr/bin/env python3
"""Генератор шрифта OC: рендерит глифы из DejaVu Sans Mono в битмапы 8x16.

Кодировка OC8 (см. kernel/include/font.h):
  0x00..0x7F  ASCII
  0x80..0x9F  А..Я
  0xA0..0xBF  а..я
  0xC0 Ё, 0xC1 ё, 0xC2 №

Запуск: scripts/fontgen.py > kernel/src/font8x16.c
"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
CELL_W, CELL_H = 8, 16

# один общий шрифт: кегль 14 идеально ложится в ячейку 8x16
# (16-й кегль срезал нижнее подчёркивание и хвосты: «_», «щ», «Ц», «g»)
FONT = ImageFont.truetype(FONT_PATH, 14)


def glyph_rows(ch: str):
    """Список из 16 байт (бит 7 = левый пиксель)."""
    img = Image.new("L", (CELL_W, CELL_H), 0)
    d = ImageDraw.Draw(img)
    d.text((0, 0), ch, fill=255, font=FONT, anchor="la")
    px = img.load()
    rows = []
    for y in range(CELL_H):
        bits = 0
        for x in range(CELL_W):
            if px[x, y] >= 100:
                bits |= 0x80 >> x
        rows.append(bits)
    return rows


def main():
    # карта код OC8 -> Unicode
    mapping = {}
    for cp in range(0x20, 0x7F):
        mapping[cp] = chr(cp)
    for i in range(32):
        mapping[0x80 + i] = chr(0x410 + i)   # А..Я
        mapping[0xA0 + i] = chr(0x430 + i)   # а..я
    mapping[0xC0] = "Ё"
    mapping[0xC1] = "ё"
    mapping[0xC2] = "№"

    print("/* Генерируется scripts/fontgen.py — не редактировать вручную. */")
    print('#include "font.h"')
    print()
    print(f"const u8 font8x16[FONT_COUNT][FONT_H] = {{")

    for code in range(0xC3):
        ch = mapping.get(code)
        if ch is None:
            rows = [0] * CELL_H
        else:
            rows = glyph_rows(ch)
            # защита от «пустого» шрифта (как было с битым суперсемплингом)
            lit = sum(bin(b).count("1") for b in rows)
            if ch != " " and lit < 2:
                print(f"ошибка: глиф {ch!r} (0x{code:02X}) почти пустой ({lit} px)",
                      file=sys.stderr)
                return 1
        label = repr(ch) if ch else "пусто"
        body = ", ".join(f"0x{b:02X}" for b in rows)
        print(f"    {{ {body} }}, /* 0x{code:02X} {label} */")

    print("};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
