/*
 * OC OS — физический менеджер памяти (битовая карта кадров 4 KiB).
 */
#ifndef OC_PMM_H
#define OC_PMM_H

#include "types.h"
#include "boot.h"

#define PMM_FRAME_SIZE  4096ULL
#define PMM_MAX_PHYS    (4ULL << 30)    /* управляем первыми 4 GiB */

void pmm_init(const oc_boot_info_t *bi, u64 kernel_start, u64 kernel_end);
u64  pmm_alloc_frame(void);             /* физ. адрес или 0 */
void pmm_free_frame(u64 phys);
u64  pmm_alloc_frames(u64 count);       /* непрерывный блок или 0 */
void pmm_reserve(u64 base, u64 frames); /* занять диапазон кадров */
u64  pmm_total_frames(void);
u64  pmm_used_frames(void);
u64  pmm_free_frames(void);

#endif /* OC_PMM_H */
