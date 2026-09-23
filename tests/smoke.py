#!/usr/bin/env python3
"""
Смоук-тесты OC: прогон всей системы в эмуляторе (tests/harness.py).

    python3 tests/smoke.py build/oc.img

Проверяется: загрузчик (E820/VBE/A20/Long Mode), ядро (GDT/IDT/память),
клавиатура, шелл, системные вызовы int 0x80, перезагрузка.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from harness import PC, KEY_SC   # noqa: E402

IMAGE = sys.argv[1] if len(sys.argv) > 1 else "build/oc.img"
KERNEL_ELF = os.path.join(os.path.dirname(IMAGE), "kernel.elf")
VERBOSE = "-v" in sys.argv

passed, failed = 0, 0


def check(name, cond, detail=""):
    global passed, failed
    if cond:
        passed += 1
        print(f"  [ OK ] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name} {detail}")


def fresh_pc():
    return PC(IMAGE, KERNEL_ELF if os.path.exists(KERNEL_ELF) else None,
              verbose=VERBOSE)


def main():
    global passed, failed

    print("=== Тест 1: загрузчик ===")
    pc = fresh_pc()
    ok = pc.run_until_boot_text("kernel loaded", max_insns=40_000_000)
    check("stage2 дошёл до kernel loaded", ok, pc.status or "")
    dbg = pc.debug()
    check("E820 собран (E)", "E" in dbg, repr(dbg))
    check("Long Mode доступен (C)", "C" in dbg, repr(dbg))
    check("A20 включена (A)", "A" in dbg, repr(dbg))
    check("ядро загружено (K)", "K" in dbg, repr(dbg))
    check("VBE включён (V)", "V" in dbg, repr(dbg))
    check("таблицы страниц (P)", "P" in dbg, repr(dbg))
    bt = pc.boot_text()
    check("VBE-режим 1024x768x32", "1024x768x32" in bt, repr(bt[-80:]))

    print("=== Тест 2: ядро стартовало ===")
    pc = fresh_pc()
    ok = pc.run_until_serial("Инициализация завершена", max_insns=80_000_000)
    check("инициализация ядра", ok, pc.status or pc.serial()[-200:])
    s = pc.serial()
    for part in ("[1/8] GDT", "[2/8] IDT", "[3/8] PIC", "[4/8] физ. память",
                 "[5/8] страничная", "[6/8] куча", "[7/8] таймер",
                 "[8/8] клавиатура"):
        check(f"шаг {part}", part in s, "")
    ok = pc.run_until_serial("oc>", max_insns=20_000_000)
    check("приглашение шелла", ok, pc.status or "")

    print("=== Тест 3: шелл ===")
    pc = fresh_pc()
    pc.run_until_serial("oc>", max_insns=80_000_000)
    check("загрузка до шелла", "oc>" in pc.serial(), pc.status or "")

    pc.type_text("help\n")
    ok = pc.run_until_serial("Команды OC", max_insns=30_000_000)
    check("команда help", ok, pc.status or pc.serial()[-150:])

    pc.type_text("info\n")
    ok = pc.run_until_serial("Long Mode (x86_64)", max_insns=40_000_000)
    check("команда info", ok, pc.status or "")
    check("видео: графика", "графика" in pc.serial(), "")

    pc.type_text("mem\n")
    ok = pc.run_until_serial("Физическая память", max_insns=30_000_000)
    check("команда mem", ok, pc.status or "")

    pc.type_text("ktest\n")
    ok = pc.run_until_serial("стресс-тест кучи", max_insns=30_000_000)
    ok2 = pc.run_until_serial("операций", max_insns=30_000_000)
    check("ktest (kmalloc/kfree)", ok and ok2, pc.status or "")

    pc.type_text("sysdemo\n")
    ok = pc.run_until_serial("привет из int 0x80", max_insns=30_000_000)
    check("системный вызов int 0x80", ok, pc.status or pc.serial()[-150:])

    pc.type_text("about\n")
    ok = pc.run_until_serial("Двухсотметровка", max_insns=80_000_000)
    check("команда about", ok, pc.status or pc.serial()[-200:])

    print("=== Тест 4: перезагрузка ===")
    pc.type_text("reboot\n")
    st = pc.run(max_insns=80_000_000)
    check("reboot (порт 0xCF9)", st == "reboot", repr(st) + pc.serial()[-120:])

    # ---- 5. защита вашей основной системы ----
    print("=== Тест 5: защита вашей ОС ===")
    check("ОС не пишет на диски (ваша система в безопасности)",
          not getattr(pc, "disk_writes", []),
          repr(getattr(pc, "disk_writes", []))[:150])

    # ---- 6. образ готов и к UEFI (ноутбуки без Legacy-режима) ----
    print("=== Тест 6: UEFI-загрузка ===")
    import struct

    with open(IMAGE, "rb") as f:
        img = f.read()

    ptype = img[446 + 4]
    part_lba = struct.unpack_from("<I", img, 446 + 8)[0]
    part_n = struct.unpack_from("<I", img, 446 + 12)[0]
    check("MBR: раздел типа 0xEF (EFI System Partition)", ptype == 0xEF,
          hex(ptype))
    check("MBR: раздел указывает на FAT-зону", part_n > 0, f"{part_lba}")

    sec = img[part_lba * 512:(part_lba + part_n) * 512]
    bps = struct.unpack_from("<H", sec, 11)[0]
    spc = sec[13]
    reserved = struct.unpack_from("<H", sec, 14)[0]
    nfats = sec[16]
    rootents = struct.unpack_from("<H", sec, 17)[0]
    spf = struct.unpack_from("<H", sec, 22)[0]
    ok_fs = (sec[510:512] == b"\x55\xAA" and bps == 512 and
             sec[54:58] == b"FAT1")
    check("FAT-раздел (ESP) корректен", ok_fs, "")

    fat = sec[reserved * bps:(reserved + spf) * bps]
    root_off = (reserved + nfats * spf) * bps
    data_off = root_off + rootents * 32

    def chain(first, size):
        out = b""
        c = first
        while 2 <= c < 0xFF8 and len(out) < size + 512:
            goff = data_off + (c - 2) * spc * bps
            out += sec[goff:goff + spc * bps]
            i = c * 3 // 2
            v = fat[i] | (fat[i + 1] << 8)
            c = (v >> 4) if (c & 1) else (v & 0xFFF)
        return out[:size]

    def find(dirbytes, name83):
        for i in range(0, len(dirbytes), 32):
            e = dirbytes[i:i + 32]
            if len(e) < 32 or e[0] in (0x00, 0xE5):
                continue
            if e[:11] == name83:
                return (struct.unpack_from("<H", e, 26)[0],
                        struct.unpack_from("<I", e, 28)[0])
        return None

    root = sec[root_off:root_off + rootents * 32]
    kern = find(root, b"KERNEL  BIN")
    check("FAT: \\KERNEL.BIN для UEFI-загрузки",
          kern is not None and kern[1] > 4096, repr(kern))
    efi_dir = find(root, b"EFI        ")
    have_bootx = None
    if efi_dir:
        efi = chain(efi_dir[0], 512)
        boot = find(efi, b"BOOT       ")
        if boot:
            bootdir = chain(boot[0], 512)
            have_bootx = find(bootdir, b"BOOTX64 EFI")
    check("FAT: \\EFI\\BOOT\\BOOTX64.EFI на месте",
          have_bootx is not None and have_bootx[1] > 512, repr(have_bootx))
    if have_bootx:
        blob = chain(have_bootx[0], have_bootx[1])
        check("BOOTX64.EFI — PE-приложение UEFI", blob[:2] == b"MZ", repr(blob[:8]))
    else:
        check("BOOTX64.EFI — PE-приложение UEFI", False, "нет файла")

    print(f"\nИтог: {passed} OK, {failed} FAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
