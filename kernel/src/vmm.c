#include "vmm.h"
#include "pmm.h"
#include "io.h"
#include "kprintf.h"
#include "panic.h"

#define PML4_IDX(va) (((va) >> 39) & 0x1FF)
#define PDPT_IDX(va) (((va) >> 30) & 0x1FF)
#define PD_IDX(va)   (((va) >> 21) & 0x1FF)
#define PT_IDX(va)   (((va) >> 12) & 0x1FF)

static u64 root_pml4;

static u64 *table_at(u64 phys)
{
    return (u64 *)(uintptr_t)phys;   /* идентичное отображение */
}

/* возвращает физ. адрес дочерней таблицы, создавая её при необходимости */
static u64 walk_create(u64 table_phys, u64 idx)
{
    u64 *t = table_at(table_phys);
    if (t[idx] & PTE_PRESENT)
        return t[idx] & 0x000FFFFFFFFFF000ULL;
    u64 frame = pmm_alloc_frame();
    if (!frame)
        return 0;
    u64 *nt = table_at(frame);
    for (int i = 0; i < 512; i++)
        nt[i] = 0;
    t[idx] = frame | PTE_PRESENT | PTE_WRITE | PTE_GLOBAL;
    return frame;
}

void vmm_init(void)
{
    u64 frame = pmm_alloc_frames(1);   /* новая PML4 */
    kassert(frame != 0);
    root_pml4 = frame;
    u64 *pml4 = table_at(frame);
    for (int i = 0; i < 512; i++)
        pml4[i] = 0;

    /* идентично: 0..4GiB страницами по 2MiB (PDPT[0..3]) */
    u64 pdpt_phys = walk_create(root_pml4, 0);
    kassert(pdpt_phys != 0);
    u64 *pdpt = table_at(pdpt_phys);

    for (int i = 0; i < 4; i++) {
        u64 pd_phys = pmm_alloc_frame();
        kassert(pd_phys != 0);
        u64 *pd = table_at(pd_phys);
        for (int j = 0; j < 512; j++)
            pd[j] = 0;
        pdpt[i] = pd_phys | PTE_PRESENT | PTE_WRITE | PTE_GLOBAL;
        for (int j = 0; j < 512; j++) {
            u64 pa = (u64)i * 0x40000000ULL + (u64)j * PAGE_2M;
            pd[j] = pa | PTE_PRESENT | PTE_WRITE | PTE_HUGE | PTE_GLOBAL;
        }
    }

    write_cr3(root_pml4);
}

int vmm_map_page(u64 va, u64 pa, u64 flags)
{
    u64 pdpt = walk_create(root_pml4, PML4_IDX(va));
    if (!pdpt)
        return -1;
    u64 pd = walk_create(pdpt, PDPT_IDX(va));
    if (!pd)
        return -1;
    u64 *pd_t = table_at(pd);
    u64 pde = pd_t[PD_IDX(va)];
    if ((pde & PTE_PRESENT) && (pde & PTE_HUGE))
        return -2;                  /* конфликт с 2MiB-страницей */
    u64 pt = walk_create(pd, PD_IDX(va));
    if (!pt)
        return -1;
    u64 *pt_t = table_at(pt);
    pt_t[PT_IDX(va)] = (pa & 0x000FFFFFFFFFF000ULL) | flags | PTE_PRESENT;
    invlpg((void *)(uintptr_t)va);
    return 0;
}

int vmm_unmap_page(u64 va)
{
    u64 *pml4 = table_at(root_pml4);
    if (!(pml4[PML4_IDX(va)] & PTE_PRESENT))
        return -1;
    u64 *pdpt = table_at(pml4[PML4_IDX(va)] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[PDPT_IDX(va)] & PTE_PRESENT))
        return -1;
    u64 *pd = table_at(pdpt[PDPT_IDX(va)] & 0x000FFFFFFFFFF000ULL);
    u64 pde = pd[PD_IDX(va)];
    if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE))
        return -1;
    u64 *pt = table_at(pde & 0x000FFFFFFFFFF000ULL);
    pt[PT_IDX(va)] = 0;
    invlpg((void *)(uintptr_t)va);
    return 0;
}

u64 vmm_translate(u64 va)
{
    u64 *pml4 = table_at(root_pml4);
    if (!(pml4[PML4_IDX(va)] & PTE_PRESENT))
        return 0;
    u64 *pdpt = table_at(pml4[PML4_IDX(va)] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[PDPT_IDX(va)] & PTE_PRESENT))
        return 0;
    u64 pde = ((u64 *)table_at(pdpt[PDPT_IDX(va)] & 0x000FFFFFFFFFF000ULL))
                  [PD_IDX(va)];
    if (!(pde & PTE_PRESENT))
        return 0;
    if (pde & PTE_HUGE)
        return (pde & 0x000FFFFFC0000000ULL) | (va & (PAGE_2M - 1));
    u64 *pt = table_at(pde & 0x000FFFFFFFFFF000ULL);
    u64 pte = pt[PT_IDX(va)];
    if (!(pte & PTE_PRESENT))
        return 0;
    return (pte & 0x000FFFFFFFFFF000ULL) | (va & (PAGE_SIZE - 1));
}

u64 vmm_root(void)
{
    return root_pml4;
}
