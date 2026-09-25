/*
 * OC OS — загрузчик программ .ocp: flat-бинарь по адресу 0x2000000,
 * вход с нулевого смещения, кольцо 3 (IOPL=0: cli/in/out у программ — #GP).
 *
 * Стек программе — внутри резерва; для syscall'ов — свой стек через TSS.rsp0.
 */
#include "ocp.h"
#include "fs.h"
#include "gdt.h"
#include "pmm.h"
#include "string.h"
#include "kprintf.h"
#include "layout.h"

#define OCP_ADDR     0x2000000UL
#define OCP_RESV     0x200000UL           /* резерв 2 МиБ: код + стек */
#define OCP_STACK    (OCP_ADDR + 0x1FF000UL)  /* верх стека программы */
#define OCP_MAX      0x1F0000UL
#define OCP_USER_CS  0x23                 /* user code (GDT 0x20 | RPL3) */
#define OCP_USER_SS  0x1B                 /* user data (GDT 0x18 | RPL3) */

static u8 ocp_kstack[8192] ALIGNED(16);   /* стек для int 0x80 из ring3 */
u64 ocp_krsp, ocp_krip;
int ocp_exit_code;
static bool reserved;

int ocp_run(const char *name)
{
    u32 len = 0;
    const void *img = fs_file(name, &len);

    if (!img) {
        kprintf("нет программы %s\n", name);
        return -1;
    }
    if (len == 0 || len > OCP_MAX) {
        kprintf("программа %s не подходит по размеру\n", name);
        return -2;
    }
    if (!reserved) {
        pmm_reserve(OCP_ADDR, OCP_RESV / PMM_FRAME_SIZE);
        reserved = true;
    }
    memcpy((void *)OCP_ADDR, img, len);

    kprintf("запускаю %s (%u байт, ring3)...\n", name, len);
    gdt_set_rsp0(ocp_kstack + sizeof(ocp_kstack));

    /* вход в пользовательский режим: iretq с полным кадром */
    __asm__ volatile(
        "movq %%rsp, ocp_krsp(%%rip)\n"
        "leaq 1f(%%rip), %%rax\n"
        "movq %%rax, ocp_krip(%%rip)\n"
        "pushq %[uss]\n"
        "pushq %[usp]\n"
        "pushfq\n"
        "popq %%rax\n"
        "orq $0x200, %%rax\n"           /* IF=1: программа живёт с IRQ */
        "andq $~0x3000, %%rax\n"        /* IOPL=0: порты и cli — запрет */
        "pushq %%rax\n"
        "pushq %[ucs]\n"
        "pushq %[entry]\n"
        "iretq\n"
        "1:\n"
        :
        : [uss] "i"(OCP_USER_SS), [usp] "r"(OCP_STACK - 16),
          [ucs] "i"(OCP_USER_CS), [entry] "r"(OCP_ADDR)
        : "rax", "memory");

    gdt_set_rsp0((void *)TEMP_STACK);
    return ocp_exit_code;
}

void ocp_exit(int code)
{
    ocp_exit_code = code;
    /* вернуться в шелл; прерывания включить (popfq: в ring0 ставит IF;
       юникорн-стенд молча игнорирует — это ок) */
    __asm__ volatile(
        "movq ocp_krsp(%rip), %rsp\n"
        "pushfq\n"
        "popq %rax\n"
        "orq $0x200, %rax\n"
        "pushq %rax\n"
        "popfq\n"
        "jmpq *ocp_krip(%rip)\n");
}
