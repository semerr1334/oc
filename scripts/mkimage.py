#!/usr/bin/env python3
"""Сборка загрузочного образа OC (raw-диск для флешки / QEMU).

Схема образа (см. include/layout.h):
  LBA 0        stage1 (MBR: код + таблица разделов: тип 0xEF)
  LBA 1..32    stage2 (путь BIOS/Legacy)
  LBA 33..     kernel.bin (путь BIOS/Legacy)
  LBA PART_LBA..  FAT12 (ESP для UEFI): \\EFI\\BOOT\\BOOTX64.EFI, \\KERNEL.BIN

Образ грузится ДВУМЯ способами: через Legacy BIOS (stage1) и через UEFI
(BOOTX64.EFI на FAT-разделе).
"""
import sys
import os
import struct


def consts():
    """Читает #define'ы из include/layout.h (простые выражения)."""
    vals = {}
    path = os.path.join(os.path.dirname(__file__), "..", "include", "layout.h")
    with open(path) as f:
        for line in f:
            line = line.split("/*")[0].strip()
            if line.startswith("#define "):
                parts = line.split(None, 2)
                if len(parts) == 3:
                    name, expr = parts[1], parts[2]
                    try:
                        vals[name] = int(eval(expr, {"__builtins__": {}}, vals))
                    except Exception:
                        pass
    return vals


# ---------------------------------------------------------------- FAT12

def fat12_image(files, total_sectors):
    """Сырой раздел FAT12: files = {'KERNEL.BIN': b'...', 'EFI/BOOT/BOOTX64.EFI': b'...'}"""
    SEC = 512
    RESERVED, NFATS, ROOT_ENTS, SPC = 1, 2, 16, 1
    root_secs = ROOT_ENTS * 32 // SEC

    fat_secs = 2
    while True:
        data_secs = total_sectors - RESERVED - NFATS * fat_secs - root_secs
        clusters = data_secs // SPC
        if (clusters + 2) * 1.5 <= fat_secs * SEC:
            break
        fat_secs += 1
        assert fat_secs < 64, "FAT12 не помещается"

    fat = [0] * (clusters + 2)
    fat[0], fat[1] = 0xFF8, 0xFFF
    next_cluster = 2

    def alloc(n):
        nonlocal next_cluster
        first = next_cluster
        for i in range(n):
            c = next_cluster + i
            fat[c] = c + 1 if i < n - 1 else 0xFFF
        next_cluster += n
        return first

    def dirent(name, ext, attr, cluster, size):
        nm = name.upper().encode("ascii")[:8].ljust(8)
        ex = ext.upper().encode("ascii")[:3].ljust(3)
        return nm + ex + bytes([attr, 0, 0]) + b"\0" * 12 + \
            struct.pack("<H", cluster) + struct.pack("<I", size)

    # раскладываем файлы по кластерам и строим каталоги
    blobs = {}      # путь -> (первый кластер, размер)
    for path, data in sorted(files.items()):
        n = max(1, (len(data) + SEC * SPC - 1) // (SEC * SPC))
        blobs[path.upper()] = (alloc(n), len(data), data)

    def dir_chain(name, ext, path_prefix, extra_dots):
        """Каталог: возвращает кластер каталога и его содержимое."""
        entries = []
        if extra_dots:
            entries.append(dirent(".", "", 0x10, 0, 0))
            entries.append(dirent("..", "", 0x10, 0, 0))
        for p, (cl, sz, _) in sorted(blobs.items()):
            parts = p.split("/")
            if len(parts) == len(path_prefix) + 1 and parts[:len(path_prefix)] == path_prefix:
                base, _, ext2 = parts[-1].partition(".")
                entries.append(dirent(base, ext2, 0x20, cl, sz))
        subdirs = {}
        for p in sorted(blobs):
            parts = p.split("/")
            if len(parts) > len(path_prefix) + 1 and parts[:len(path_prefix)] == path_prefix:
                subdirs.setdefault(parts[len(path_prefix)], None)
        for sd in subdirs:
            entries.append(dirent(sd, "", 0x10, 0, 0))  # кластр. допишем ниже
        raw = b"".join(entries[:ROOT_ENTS]) if not extra_dots else b"".join(entries)
        assert len(raw) <= SEC * max(1, (len(entries) * 32 + SEC - 1) // SEC)
        nsec = max(1, (len(entries) * 32 + SEC - 1) // SEC)
        cl = alloc(nsec)
        raw = raw.ljust(nsec * SEC, b"\0")
        return cl, raw, entries, [e for e in subdirs]

    # root: файлы верхнего уровня + подкаталоги (создаём снизу вверх)
    dirdata = {}
    def build_dir(prefix, is_root):
        entries = []
        if not is_root:
            entries.append(dirent(".", "", 0x10, 0, 0))
            entries.append(dirent("..", "", 0x10, 0, 0))
        subdirs = []
        for p in sorted(blobs):
            parts = p.split("/")
            if parts[:len(prefix)] != prefix:
                continue
            rest = parts[len(prefix):]
            if len(rest) == 1:
                base, _, ext2 = rest[0].partition(".")
                entries.append(dirent(base, ext2, 0x20, blobs[p][0], blobs[p][1]))
            elif rest[0] not in subdirs:
                subdirs.append(rest[0])
        for sd in subdirs:
            sub_cl = build_dir(prefix + [sd], False)
            entries.append(dirent(sd, "", 0x10, sub_cl, 0))
        nsec = max(1, (len(entries) * 32 + SEC - 1) // SEC)
        if is_root:
            assert len(entries) <= ROOT_ENTS, "корневой каталог переполнен"
            cl = 0
        else:
            cl = alloc(nsec)
        raw = b"".join(entries)
        limit = root_secs * SEC if is_root else nsec * SEC
        dirdata[cl] = (raw.ljust(limit, b"\0"), limit)
        return cl

    build_dir([], True)

    # монтируем раздел
    img = bytearray(total_sectors * SEC)
    bps, spf = SEC, fat_secs
    boot = bytearray(SEC)
    boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"OCBOOT  "
    struct.pack_into("<H", boot, 11, bps)
    boot[13] = SPC
    struct.pack_into("<H", boot, 14, RESERVED)
    boot[16] = NFATS
    struct.pack_into("<H", boot, 17, ROOT_ENTS)
    if total_sectors < 0x10000:
        struct.pack_into("<H", boot, 19, total_sectors)
    else:
        struct.pack_into("<I", boot, 32, total_sectors)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, spf)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    boot[38] = 0x29
    struct.pack_into("<I", boot, 39, 0x0C052026)
    boot[43:54] = b"OC BOOT    "
    boot[54:62] = b"FAT12   "
    boot[62:64] = b"\xEB\xFE"          # hlt-loop, UEFI сюда не заходит
    boot[510:512] = b"\x55\xAA"
    img[0:SEC] = boot

    # FAT-ы (FAT12: две записи на три байта)
    packed = bytearray()
    for i in range(0, clusters + 2, 2):
        a, b = fat[i], fat[i + 1] if i + 1 < clusters + 2 else 0
        packed += bytes([a & 0xFF, ((a >> 8) & 0x0F) | ((b & 0x0F) << 4),
                         (b >> 4) & 0xFF])
    packed = packed.ljust(spf * SEC, b"\0")
    off = RESERVED * SEC
    for _ in range(NFATS):
        img[off:off + spf * SEC] = packed
        off += spf * SEC

    # каталоги и файлы
    for cl, (raw, limit) in dirdata.items():
        if cl == 0:
            img[off:off + limit] = raw          # root
        else:
            start = off + root_secs * SEC + (cl - 2) * SEC * SPC
            img[start:start + limit] = raw
    for p, (cl, sz, data) in blobs.items():
        start = off + root_secs * SEC + (cl - 2) * SEC * SPC
        img[start:start + len(data)] = data
    return bytes(img)


def main():
    if len(sys.argv) not in (5, 6):
        print("usage: mkimage.py stage1.bin stage2.bin kernel.bin out.img [BOOTX64.EFI]")
        return 1

    with open(sys.argv[1], "rb") as f:
        stage1 = f.read()
    with open(sys.argv[2], "rb") as f:
        stage2 = f.read()
    with open(sys.argv[3], "rb") as f:
        kernel = f.read()
    out_path = sys.argv[4]
    efi = None
    if len(sys.argv) == 6:
        with open(sys.argv[5], "rb") as f:
            efi = f.read()

    c = consts()
    sector = c.get("SECTOR_SIZE", 512)
    image_size = c.get("IMAGE_SIZE", 2 * 1024 * 1024)
    stage2_max = c.get("STAGE2_MAX_BYTES", 16 * 1024)
    kernel_max = c.get("KERNEL_MAX_BYTES", 448 * 1024)
    kernel_lba = c.get("KERNEL_LBA", 33)
    part_lba = c.get("PART_LBA", 960)
    part_sectors = c.get("PART_SECTORS", image_size // sector - part_lba)

    assert len(stage1) == sector, \
        f"stage1 должен быть ровно {sector} байт (сейчас {len(stage1)})"
    assert stage1[510:512] == b"\x55\xaa", "нет сигнатуры 0x55AA"
    assert len(stage2) <= stage2_max, f"stage2 больше {stage2_max} байт"
    assert len(kernel) <= kernel_max, f"kernel.bin больше {kernel_max} байт"
    kend_lba = kernel_lba + (len(kernel) + sector - 1) // sector
    assert kend_lba <= part_lba, "ядро налезает на FAT-раздел (PART_LBA)"

    img = bytearray(image_size)
    img[0:sector] = stage1
    img[sector:sector + len(stage2)] = stage2
    koff = sector * kernel_lba
    img[koff:koff + len(kernel)] = kernel

    if efi is not None:
        files = {
            "KERNEL.BIN": kernel,
            "EFI/BOOT/BOOTX64.EFI": efi,
        }
        fat = fat12_image(files, part_sectors)
        assert len(fat) == part_sectors * sector
        poff = sector * part_lba
        img[poff:poff + len(fat)] = fat
        kind = "BIOS + UEFI"
    else:
        kind = "BIOS"

    with open(out_path, "wb") as f:
        f.write(img)
    sectors = (len(kernel) + sector - 1) // sector
    print(f"образ {out_path}: {image_size} байт ({kind}), ядро {len(kernel)} байт "
          f"({sectors} секторов)" + (f", BOOTX64.EFI {len(efi)} байт" if efi else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
