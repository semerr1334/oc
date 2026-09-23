#include "gdt.h"
#include "idt.h"
#include "string.h"

/*
 * Дескрипторы GDT (16 байт на TSS). Раскладка:
 *   0x00 null | 0x08 kernel code | 0x10 kernel data
 *   0x18 user data | 0x20 user code | 0x28 TSS
 * (порядок data/code для пользователя — под будущий sysret).
 */
static u64 gdt[7] ALIGNED(8);
static struct tss64 tss;

static void gdt_set_tss(u32 idx, u64 base, u32 limit)
{
    u64 *e = &gdt[idx];
    e[0] = 0;
    e[1] = 0;
    u8 *b = (u8 *)e;
    /* байты 0-1: limit 15:0, 2-3: base 15:0, 4: base 23:16, 5: type,
       6: limit 19:16 + flags, 7: base 31:24, 8-11: base 63:32 */
    b[0] = (u8)(limit & 0xFF);
    b[1] = (u8)((limit >> 8) & 0xFF);
    b[2] = (u8)(base & 0xFF);
    b[3] = (u8)((base >> 8) & 0xFF);
    b[4] = (u8)((base >> 16) & 0xFF);
    b[5] = 0x89;                    /* present, доступ, 64-bit TSS */
    b[6] = (u8)((limit >> 16) & 0x0F);
    b[7] = (u8)((base >> 24) & 0xFF);
    *(u32 *)(b + 8) = (u32)(base >> 32);
    *(u32 *)(b + 12) = 0;
}

void gdt_set_rsp0(void *stack_top)
{
    tss.rsp0 = (u64)(uintptr_t)stack_top;
}

void gdt_init(void *kernel_stack_top)
{
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = (u64)(uintptr_t)kernel_stack_top;
    tss.iomap_base = sizeof(tss);

    gdt[0] = 0x0000000000000000;
    gdt[1] = 0x00AF9A000000FFFF;    /* 0x08: kernel code (L=1) */
    gdt[2] = 0x00CF92000000FFFF;    /* 0x10: kernel data */
    gdt[3] = 0x00CFF2000000FFFF;    /* 0x18: user data  (DPL3) */
    gdt[4] = 0x00AFFA000000FFFF;    /* 0x20: user code  (DPL3, L=1) */
    gdt_set_tss(5, (u64)(uintptr_t)&tss, sizeof(tss) - 1);

    struct {
        u16 limit;
        u64 base;
    } PACKED desc = { sizeof(gdt) - 1, (u64)(uintptr_t)gdt };

    gdt_flush(&desc, GDT_KERNEL_CODE, GDT_KERNEL_DATA);
    tss_load(GDT_TSS);
}
