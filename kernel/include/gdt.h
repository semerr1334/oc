/*
 * OC OS — GDT и TSS (64-битный режим).
 */
#ifndef OC_GDT_H
#define OC_GDT_H

#include "types.h"

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

struct tss64 {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} PACKED;

void gdt_init(void *kernel_stack_top);
void gdt_set_rsp0(void *stack_top);

#endif /* OC_GDT_H */
