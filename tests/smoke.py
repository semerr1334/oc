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

    # ---- 7. пиксели: текст реально рисуется, и по-русски тоже ----
    print("=== Тест 7: отрисовка текста (пиксели) ===")
    import re

    pc7 = fresh_pc()
    pc7.run_until_serial("oc>", max_insns=80_000_000)
    pc7.type_text("about\n")
    ok = pc7.run_until_serial("Двухсотметровка", max_insns=80_000_000)
    check("about отработал", ok, pc7.status or "")

    bi = bytes(pc7.uc.mem_read(0x7000, 832))
    fb_addr = struct.unpack_from("<Q", bi, 792)[0]
    pitch, w, h, bpp = struct.unpack_from("<4I", bi, 800)
    check("кадр в bootinfo (адрес/размер)", fb_addr and w and h and bpp == 32,
          f"{fb_addr:#x} {w}x{h}x{bpp} pitch={pitch}")
    fb = bytes(pc7.uc.mem_read(fb_addr, pitch * h))

    def px(x, y):
        return struct.unpack_from("<I", fb, y * pitch + x * 4)[0] & 0xFFFFFF

    lit = 0
    for y in range(0, h, 4):
        for x in range(0, w, 2):
            if px(x, y):
                lit += 1
    check("экран не пустой (есть светящиеся пиксели)", lit > 200, str(lit))

    # шрифт из сборки: ищем глифы прямо в кадре (сетка ячеек 8x16)
    fsrc = open(os.path.join(os.path.dirname(IMAGE), "..",
                             "kernel", "src", "font8x16.c")).read()
    fsrc = re.sub(r"/\*.*?\*/", "", fsrc, flags=re.S)  # комментарии мешают
    nums = [int(m, 16) for m in re.findall(r"0x([0-9A-Fa-f]{2})", fsrc)]
    check("шрифт разобран корректно (195 глифов)", len(nums) == 195 * 16,
          str(len(nums)))

    def glyph_on_screen(code):
        g = nums[code * 16:code * 16 + 16]
        if len(g) < 16 or not any(g):
            return False
        for gy in range(0, h - 16, 16):
            for gx in range(0, w - 8, 8):
                fg = None
                good = True
                for row in range(16):
                    bits = g[row]
                    for col in range(8):
                        if bits & (0x80 >> col):
                            c = px(gx + col, gy + row)
                            if not c or (fg is not None and c != fg):
                                good = False
                                break
                            fg = c
                    if not good:
                        break
                if good and fg is not None:
                    return True
        return False

    check("латиница рисуется (глиф «D» из GDT/IDT в кадре)",
          glyph_on_screen(0x44), "")
    check("кириллица рисуется (глиф «Д» из «Двухсотметровка»)",
          glyph_on_screen(0x84), "")

    # ---- 8. раскладка: Ctrl+Space и Alt+Shift ----
    print("=== Тест 8: раскладка клавиатуры ===")
    pc8 = fresh_pc()
    pc8.run_until_serial("oc>", max_insns=80_000_000)
    # Ctrl+Space (скан-коды set 1): Ctrl нажать, Space нажать/отпустить, Ctrl отпустить
    for sc in (0x1D, 0x39, 0x39 | 0x80, 0x1D | 0x80):
        pc8.queue_scancode(sc)
    ok = pc8.run_until_serial("раскладка: RU", max_insns=30_000_000)
    check("Ctrl+Space включает RU (с подтверждением)", ok,
          pc8.status or pc8.serial()[-150:])
    # клавиша W (0x11) в RU = «ц»
    pc8.queue_scancode(0x11)
    pc8.queue_scancode(0x11 | 0x80)
    ok = pc8.run_until_serial("ц", max_insns=20_000_000)
    check("W печатает «ц» (ЙЦУКЕН работает)", ok, pc8.serial()[-100:])
    # Alt+Shift вернёт EN
    for sc in (0x38, 0x2A, 0x2A | 0x80, 0x38 | 0x80):
        pc8.queue_scancode(sc)
    ok = pc8.run_until_serial("раскладка: EN", max_insns=30_000_000)
    check("Alt+Shift возвращает EN (как в Windows)", ok,
          pc8.status or pc8.serial()[-150:])

    # ---- 9. меню и команды раскладки «без тупика» ----
    print("=== Тест 9: меню и залипшая раскладка ===")
    pc9 = fresh_pc()
    pc9.run_until_serial("oc>", max_insns=80_000_000)
    # команду layout ЛЕГКО набрать при любой раскладке (латиница)
    pc9.type_text("layout\n")
    ok = pc9.run_until_serial("раскладка: RU", max_insns=20_000_000)
    check("команда layout (латиница) включает RU", ok,
          pc9.status or pc9.serial()[-120:])
    # теперь RU активна: те же пальцы «layout» дадут «дфнщге» — алиас обязан спасти
    pc9.type_text("layout\n")
    ok = pc9.run_until_serial("раскладка: EN", max_insns=20_000_000)
    check("«layout» на RU-пальцах (дфнщге) тоже работает", ok,
          pc9.status or pc9.serial()[-120:])
    # главное меню
    pc9.type_text("menu\n")
    ok = pc9.run_until_serial("Меню OC", max_insns=20_000_000)
    check("меню открывается (команда menu)", ok,
          pc9.status or pc9.serial()[-120:])
    pc9.queue_scancode(0x150)          # стрелка вниз (E0,50)
    pc9.type_text("\n")                # Enter — запуск пункта
    ok = pc9.run_until_serial("[меню] Память и карты", max_insns=20_000_000)
    check("стрелки + Enter запускают пункт меню", ok,
          pc9.status or pc9.serial()[-120:])
    pc9.type_text("\n")                # обратно в меню
    pc9.queue_scancode(0x01)           # Esc
    ok = pc9.run_until_serial("меню закрыто", max_insns=20_000_000)
    check("Esc закрывает меню", ok, pc9.status or pc9.serial()[-120:])

    # ---- 10. звук, заставка и мышь в меню ----
    print("=== Тест 10: звук, звёзды, мышь ===")
    pc10 = fresh_pc()
    pc10.run_until_serial("oc>", max_insns=80_000_000)
    pc10.type_text("beep\n")
    ok = pc10.run_until_serial("писк! 880 Гц", max_insns=10_000_000)
    check("динамик пищит (beep)", ok, pc10.status or pc10.serial()[-120:])
    pc10.type_text("song\n")
    ok = pc10.run_until_serial("играю мелодию", max_insns=15_000_000)
    check("мелодия играет (song)", ok, pc10.status or pc10.serial()[-120:])
    pc10.type_text("stars 20\n")
    ok = pc10.run_until_serial("звёздное небо", max_insns=10_000_000)
    check("заставка «Звёздное небо» стартовала", ok,
          pc10.status or pc10.serial()[-120:])
    ok = pc10.run_until_serial("звёзды:", max_insns=30_000_000)
    check("заставка завершилась (20 кадров)", ok,
          pc10.status or pc10.serial()[-120:])
    # меню мышью: движение вниз (48 px = 2 пункта) + клик
    pc10.type_text("menu\n")
    pc10.run_until_serial("Меню OC", max_insns=15_000_000)
    pc10.send_mouse_packet(0, -48)      # вниз на 2 пункта -> «Тест железа»
    pc10.send_mouse_packet(0, 0, 1)     # клик (нажать)
    pc10.send_mouse_packet(0, 0, 0)     # клик (отпустить)
    ok = pc10.run_until_serial("[меню] Тест железа", max_insns=30_000_000)
    check("мышь: движение + клик запускают пункт меню", ok,
          pc10.status or pc10.serial()[-150:])
    pc10.type_text("\n")                # обратно в меню
    pc10.queue_scancode(0x01)           # Esc — закрыть

    # ---- 11. файлы и программы .ocp (ring3!) ----
    print("=== Тест 11: файлы и программы .ocp ===")
    pc11 = fresh_pc()
    pc11.run_until_serial("oc>", max_insns=80_000_000)
    pc11.type_text("ls\n")
    ok = pc11.run_until_serial("wiki.ocp", max_insns=15_000_000)
    check("файлы: образ показывает wiki.ocp", ok,
          pc11.status or pc11.serial()[-120:])
    pc11.type_text("cat notes.txt\n")
    ok = pc11.run_until_serial("Первая записка", max_insns=15_000_000)
    check("тип: текстовый файл читается", ok, pc11.status or pc11.serial()[-120:])
    # программа по имени — как в Windows!
    pc11.type_text("hello\n")
    ok = pc11.run_until_serial("Привет из ПРОГРАММЫ", max_insns=30_000_000)
    check("hello.ocp запускается (ring3, int 0x80!)", ok,
          pc11.status or pc11.serial()[-150:])
    pc11.type_text("x")                 # программа ждала клавишу
    ok = pc11.run_until_serial("Пока-пока", max_insns=15_000_000)
    check("hello.ocp завершилась сама (exit)", ok,
          pc11.status or pc11.serial()[-120:])
    # мини-браузер: страницы и ссылки (свежая загрузка: стенду тяжело
    # последовательно два ring3-сеанса, а функции проверяем по полной)
    pcw = fresh_pc()
    pcw.run_until_serial("oc>", max_insns=80_000_000)
    pcw.type_text("wiki\n")
    ok = pcw.run_until_serial("Стр. 1 — Добро", max_insns=30_000_000)
    check("wiki.ocp (мини-браузер) открыл главную", ok,
          pcw.status or pcw.serial()[-150:])
    pcw.type_text("2")                  # ссылка [2]
    ok = pcw.run_until_serial("Стр. 2 — Как вернуться", max_insns=15_000_000)
    check("ссылка-цифра ведёт на страницу", ok,
          pcw.status or pcw.serial()[-120:])
    pcw.type_text("q")                  # выход
    ok = pcw.run_until_serial("До встречи в OC", max_insns=15_000_000)
    check("браузер закрывается (q)", ok, pcw.status or pcw.serial()[-120:])

    # ---- 12. рабочий набор приложений (как в Linux/Windows) ----
    print("=== Тест 12: калькулятор, игра, пианино, змейка ===")
    pc12 = fresh_pc()
    pc12.run_until_serial("oc>", max_insns=80_000_000)
    pc12.type_text("programs\n")
    ok = pc12.run_until_serial("snake.ocp", max_insns=15_000_000)
    check("программы: список приложений (.ocp)", ok,
          pc12.status or pc12.serial()[-120:])
    pc12.type_text("calc\n")
    ok = pc12.run_until_serial("КАЛЬКУЛЯТОР", max_insns=20_000_000)
    check("calc: калькулятор запустился", ok,
          pc12.status or pc12.serial()[-120:])
    pc12.type_text("2+3*4\n")
    ok = pc12.run_until_serial("= 14", max_insns=25_000_000)
    check("calc: 2+3*4 = 14 (приоритет операций!)", ok,
          pc12.status or pc12.serial()[-120:])
    pc12.queue_scancode(0x01); pc12._pump()          # Esc
    ok = pc12.run_until_serial("Калькулятор закрыт", max_insns=15_000_000)
    check("calc: закрывается по Esc", ok, pc12.status or pc12.serial()[-80:])

    pcg = fresh_pc()
    pcg.run_until_serial("oc>", max_insns=80_000_000)
    pcg.type_text("game\n")
    ok = pcg.run_until_serial("УГАДАЙ ЧИСЛО", max_insns=20_000_000)
    check("game: «Угадай число» запустилась", ok,
          pcg.status or pcg.serial()[-120:])
    pcg.type_text("50\n")
    ok = pcg.run_until_serial("(попытка", max_insns=20_000_000)
    check("game: подсказка «больше/меньше» работает", ok,
          pcg.status or pcg.serial()[-120:])
    pcg.queue_scancode(0x01); pcg._pump()
    ok = pcg.run_until_serial("Сыграем в другой раз", max_insns=15_000_000)
    check("game: выход по Esc", ok, pcg.status or pcg.serial()[-80:])

    pcp = fresh_pc()
    pcp.run_until_serial("oc>", max_insns=80_000_000)
    pcp.type_text("piano\n")
    ok = pcp.run_until_serial("ПИАНИНО", max_insns=20_000_000)
    check("piano: пианино запустилось", ok,
          pcp.status or pcp.serial()[-120:])
    pcp.type_text("q")
    ok = pcp.run_until_serial("262 Гц", max_insns=20_000_000)
    check("piano: клавиша Q играет ноту до (262 Гц)", ok,
          pcp.status or pcp.serial()[-120:])
    pcp.queue_scancode(0x01); pcp._pump()
    ok = pcp.run_until_serial("Пианино закрыто", max_insns=15_000_000)
    check("piano: выход по Esc", ok, pcp.status or pcp.serial()[-80:])

    pcs = fresh_pc()
    pcs.run_until_serial("oc>", max_insns=80_000_000)
    pcs.type_text("snake\n")
    ok = pcs.run_until_serial("ЗМЕЙКА OC", max_insns=20_000_000)
    check("snake: змейка запустилась", ok, pcs.status or pcs.serial()[-120:])
    pcs.type_text("x")                              # старт
    ok = pcs.run_until_serial("Счёт: 0", max_insns=25_000_000)
    check("snake: поле рисуется, счёт 0", ok, pcs.status or pcs.serial()[-150:])
    pcs.queue_scancode(0x01); pcs._pump()
    ok = pcs.run_until_serial("Игра окончена", max_insns=15_000_000)
    check("snake: выход по Esc", ok, pcs.status or pcs.serial()[-80:])

    # ---- 13. графический рабочий стол (окна, иконки) ----
    print("=== Тест 13: рабочий стол (окна, иконки, мышь) ===")
    pg = fresh_pc()
    pg.run_until_serial("oc>", max_insns=80_000_000)
    pg.type_text("desktop\n")
    ok = pg.run_until_serial("[GUI] рабочий стол готов", max_insns=30_000_000)
    check("рабстол: графический рабочий стол запустился", ok,
          pg.status or pg.serial()[-120:])
    pg.type_text("1")                            # иконка «Калькулятор»
    ok = pg.run_until_serial("[GUI] открыто: Калькулятор", max_insns=20_000_000)
    check("рабстол: окно калькулятора открылось", ok,
          pg.status or pg.serial()[-120:])
    pg.type_text("2+3*4")
    pg.type_text("\n")                           # Enter = «=»
    ok = pg.run_until_serial("[GUI] калькулятор: = 14", max_insns=30_000_000)
    check("калькулятор в окне: 2+3*4 = 14 (приоритет!)", ok,
          pg.status or pg.serial()[-120:])
    pg.queue_scancode(0x01); pg._pump()          # Esc
    ok = pg.run_until_serial("[GUI] закрыто: Калькулятор", max_insns=15_000_000)
    check("окно закрывается по Esc", ok, pg.status or pg.serial()[-80:])
    pg.type_text("2")                            # «Часы»
    ok = pg.run_until_serial("[GUI] открыто: Часы", max_insns=20_000_000)
    check("рабстол: окно часов открылось", ok, pg.status or pg.serial()[-120:])
    pg.queue_scancode(0x01); pg._pump()
    ok = pg.run_until_serial("[GUI] закрыто: Часы", max_insns=15_000_000)
    check("часы закрываются", ok, pg.status or pg.serial()[-80:])
    pg.type_text("3")                            # «Блокнот»
    ok = pg.run_until_serial("[GUI] открыто: Блокнот", max_insns=20_000_000)
    check("рабстол: блокнот открылся", ok, pg.status or pg.serial()[-120:])
    pg.type_text("abc")
    ok = pg.run_until_serial("[GUI] блокнот: символов 3", max_insns=25_000_000)
    check("блокнот: печатает текст", ok, pg.status or pg.serial()[-120:])
    pg.queue_scancode(0x01); pg._pump()
    ok = pg.run_until_serial("[GUI] закрыто: Блокнот", max_insns=15_000_000)
    check("блокнот закрылся", ok, pg.status or pg.serial()[-80:])
    pg.type_text("4")                            # «О системе»
    ok = pg.run_until_serial("[GUI] открыто: О системе", max_insns=20_000_000)
    check("рабстол: окно «О системе» открылось", ok, pg.status or pg.serial()[-120:])
    pg.queue_scancode(0x01); pg._pump()
    ok = pg.run_until_serial("[GUI] закрыто: О системе", max_insns=15_000_000)
    check("«О системе» закрылось", ok, pg.status or pg.serial()[-80:])
    pg.queue_scancode(0x01); pg._pump()          # Esc на пустом столе — выход
    ok = pg.run_until_serial("[GUI] выход в шелл", max_insns=15_000_000)
    check("рабстол: выход в шелл по Esc", ok, pg.status or pg.serial()[-80:])

    print(f"\nИтог: {passed} OK, {failed} FAIL")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
