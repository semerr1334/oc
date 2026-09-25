/*
 * OC OS — единый источник истины для раскладки диска и памяти.
 * Общий заголовок для ассемблера загрузчика (.S, проходит через cpp)
 * и для ядра на C.
 */
#ifndef OC_LAYOUT_H
#define OC_LAYOUT_H

/* ---------- Дисковый образ (raw, пишется на флешку) ---------- */
#define IMAGE_SIZE          (2 * 1024 * 1024)
#define SECTOR_SIZE         512
#define IMAGE_SECTORS       (IMAGE_SIZE / SECTOR_SIZE)

#define STAGE1_LBA          0
#define STAGE2_LBA          1
#define STAGE2_SECTORS      32                  /* 16 KiB на stage2 */
#define STAGE2_MAX_BYTES    (STAGE2_SECTORS * SECTOR_SIZE)

#define KERNEL_LBA          (STAGE2_LBA + STAGE2_SECTORS)   /* 33 */

/* Раздел-ESP (FAT12) для UEFI: \EFI\BOOT\BOOTX64.EFI + \KERNEL.BIN.
 * Тип 0xEF в MBR — прошивка UEFI сама находит загрузчик на флешке. */
#define PART_TYPE           0xEF
#define PART_LBA            960
#define PART_SECTORS        (IMAGE_SECTORS - PART_LBA)

/* ---------- Раскладка низкой памяти (real mode / загрузчик) ---------- */
#define BOOTINFO_PHYS       0x7000              /* структура oc_boot_info  */
#define BOOT_STACK          0x7C00              /* вершина стека real mode */

#define STAGE2_ENTRY        0x8000              /* физический адрес stage2 */

#define KERNEL_STAGING      0x00010000          /* временная площадка ядра  */
#define KERNEL_MAX_BYTES    (0x00080000 - KERNEL_STAGING) /* 448 KiB      */
#define KERNEL_PHYS         0x00100000          /* постоянный адрес ядра    */

#define BOOT_PML4           0x1000              /* стартовые таблицы страниц */
#define BOOT_PDPT           0x2000
#define BOOT_PD             0x3000              /* 4 таблицы: 0x3000..0x6000 */

#define TEMP_STACK          0x00090000          /* временный стек long mode */

/* ---------- Селекторы GDT загрузчика ---------- */
#define GDT_CODE32          0x08
#define GDT_DATA32          0x10
#define GDT_CODE64          0x18

#endif /* OC_LAYOUT_H */
