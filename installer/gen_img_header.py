#!/usr/bin/env python3
"""Генерирует installer/oc_img.h: образ OC, встроенный в установщик.

Нули в образе не хранятся — только ненулевые куски (спаны), а при
установке образ восстанавливается. Это сжимает данные в ~40 раз.
"""
import pathlib
import sys

src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "dist/oc-v0.1.img")
dst = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "installer/oc_img.h")
img = src.read_bytes()
n = len(img)

GAP = 24  # нулевой просвет меньше этого сливается в один спан

spans = []  # (offset, bytes)
i = 0
while i < n:
    if img[i] == 0:
        i += 1
        continue
    j = i
    while j < n:
        if img[j] != 0:
            j += 1
            continue
        k = j
        while k < n and img[k] == 0 and k - j < GAP:
            k += 1
        if k < n and img[k] != 0 and k - j < GAP:
            j = k + 1
        else:
            break
    spans.append((i, img[i:j]))
    i = j

data = b"".join(s[1] for s in spans)

out = [
    "/* АВТО-ГЕНЕРАЦИЯ (installer/gen_img_header.py) — НЕ править вручную. */",
    "#pragma once",
    "",
    f"#define OC_IMG_SIZE   {n}u",
    f"#define OC_IMG_NSPAN  {len(spans)}u",
    "",
    "static const struct { unsigned int off, len; } oc_img_spans[OC_IMG_NSPAN] = {",
]
for off, blob in spans:
    out.append(f"    {{ {off}u, {len(blob)}u }},")
out += [
    "};",
    "",
    f"static const unsigned char oc_img_data[{max(len(data), 1)}] = {{",
]
for i in range(0, len(data), 20):
    out.append("    " + ",".join(f"0x{b:02x}" for b in data[i:i + 20]) + ",")
out += [
    "};",
    "",
    "/* Собрать полный образ из спанов (буфер должен быть OC_IMG_SIZE байт). */",
    "static void oc_image_rebuild(unsigned char *buf)",
    "{",
    "    unsigned int i, o = 0, j;",
    "    for (i = 0; i < OC_IMG_SIZE; i++)",
    "        buf[i] = 0;",
    "    for (i = 0; i < OC_IMG_NSPAN; i++) {",
    "        for (j = 0; j < oc_img_spans[i].len; j++)",
    "            buf[oc_img_spans[i].off + j] = oc_img_data[o + j];",
    "        o += oc_img_spans[i].len;",
    "    }",
    "}",
    "",
]
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text("\n".join(out), encoding="utf-8")
print(f"{dst}: образ {n} Б, спанов {len(spans)}, данные {len(data)} Б")
