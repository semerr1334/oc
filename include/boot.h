/*
 * OC OS — протокол между загрузчиком и ядром.
 *
 * Загрузчик (stage2) собирает структуру по физическому адресу
 * BOOTINFO_PHYS и передаёт указатель в RDI при входе в ядро.
 *
 * Смещения полей заданы макросами BI_OFF_* — их использует и ассемблер
 * загрузчика, и C-структура ниже (проверяется _Static_assert'ом).
 */
#ifndef OC_BOOT_H
#define OC_BOOT_H

#include "layout.h"

#define BOOTINFO_MAGIC      0x49424F43u     /* "COBI" */
#define BOOTINFO_VERSION    1

#define BI_E820_MAX         32
#define BI_E820_ENTRY_SIZE  24

#define BI_OFF_MAGIC        0
#define BI_OFF_VERSION      4
#define BI_OFF_E820_COUNT   8
#define BI_OFF_BOOT_DRIVE   12
#define BI_OFF_KERNEL_SIZE  16
#define BI_OFF_FB_PRESENT   20
#define BI_OFF_E820         24
#define BI_OFF_FB_ADDR      (BI_OFF_E820 + BI_E820_MAX * BI_E820_ENTRY_SIZE) /* 792 */
#define BI_OFF_FB_PITCH     (BI_OFF_FB_ADDR + 8)                             /* 800 */
#define BI_OFF_FB_WIDTH     (BI_OFF_FB_PITCH + 4)                            /* 804 */
#define BI_OFF_FB_HEIGHT    (BI_OFF_FB_WIDTH + 4)                            /* 808 */
#define BI_OFF_FB_BPP       (BI_OFF_FB_HEIGHT + 4)                           /* 812 */
#define BI_OFF_FB_RPOS      (BI_OFF_FB_BPP + 4)                              /* 816 */
#define BI_OFF_FB_RSIZE     (BI_OFF_FB_RPOS + 1)
#define BI_OFF_FB_GPOS      (BI_OFF_FB_RSIZE + 1)
#define BI_OFF_FB_GSIZE     (BI_OFF_FB_GPOS + 1)
#define BI_OFF_FB_BPOS      (BI_OFF_FB_GSIZE + 1)
#define BI_OFF_FB_BSIZE     (BI_OFF_FB_BPOS + 1)
#define BI_OFF_FB_RESV_POS  (BI_OFF_FB_BSIZE + 1)
#define BI_OFF_FB_RESV_SIZE (BI_OFF_FB_RESV_POS + 1)
#define BI_OFF_END          832

/* Типы записей E820 */
#define E820_USABLE         1
#define E820_RESERVED       2
#define E820_ACPI           3
#define E820_NVS            4
#define E820_BAD            5

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <stddef.h>

struct oc_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} __attribute__((packed));

struct oc_boot_info {
    uint32_t magic;
    uint32_t version;
    uint32_t e820_count;
    uint32_t boot_drive;
    uint32_t kernel_size;
    uint32_t fb_present;
    struct oc_e820_entry e820[BI_E820_MAX];   /* +24 */
    uint64_t fb_addr;                         /* +792 */
    uint32_t fb_pitch;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;
    uint8_t  fb_rpos, fb_rsize;
    uint8_t  fb_gpos, fb_gsize;
    uint8_t  fb_bpos, fb_bsize;
    uint8_t  fb_resv_pos, fb_resv_size;
    uint8_t  reserved[8];
} __attribute__((packed));

_Static_assert(sizeof(struct oc_e820_entry) == BI_E820_ENTRY_SIZE, "e820 entry size");
_Static_assert(offsetof(struct oc_boot_info, e820) == BI_OFF_E820, "e820 offset");
_Static_assert(offsetof(struct oc_boot_info, fb_addr) == BI_OFF_FB_ADDR, "fb_addr offset");
_Static_assert(offsetof(struct oc_boot_info, fb_rpos) == BI_OFF_FB_RPOS, "fb_rpos offset");
_Static_assert(sizeof(struct oc_boot_info) == BI_OFF_END, "boot info size");

typedef struct oc_boot_info oc_boot_info_t;

#endif /* !__ASSEMBLER__ */

#endif /* OC_BOOT_H */
