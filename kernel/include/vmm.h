/*
 * OC OS — виртуальная память: таблицы страниц 4-уровневые, страницы
 * 4 KiB и 2 MiB. Идентичное отображение первых 4 GiB (2MiB-страницы)
 * + постраничная карта для кучи.
 */
#ifndef OC_VMM_H
#define OC_VMM_H

#include "types.h"

#define PAGE_SIZE   0x1000ULL
#define PAGE_2M     0x200000ULL

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_HUGE     (1ULL << 7)
#define PTE_GLOBAL   (1ULL << 8)
#define PTE_NX       (1ULL << 63)

void vmm_init(void);
int  vmm_map_page(u64 va, u64 pa, u64 flags);   /* 0 = ok */
int  vmm_unmap_page(u64 va);
u64  vmm_translate(u64 va);                     /* 0 = не отображено */
u64  vmm_root(void);                            /* физ. адрес PML4 */

#endif /* OC_VMM_H */
