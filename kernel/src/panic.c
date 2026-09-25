#include "idt.h"
#include "panic.h"
#include "kprintf.h"
#include "console.h"
#include "io.h"
#include "string.h"

static const char *exception_names[32] = {
    "#DE деление на ноль", "#DB отладка", "NMI", "#BP точка останова",
    "#OF переполнение", "#BR BOUND", "#UD недопустимая команда",
    "#NM устройство недоступно", "#DF двойная ошибка", "переполнение сопроцессора",
    "#TS неверный TSS", "#NP сегмент отсутствует", "#SS ошибка стека",
    "#GP общая защита", "#PF страничная ошибка", "зарезервировано",
    "#MF x87 FP", "#AC ошибка выравнивания", "#MC отказ оборудования",
    "#XM SIMD FP", "#VE виртуализация", "#CP защита управления",
    "зарезервировано", "зарезервировано", "зарезервировано",
    "зарезервировано", "зарезервировано", "зарезервировано",
    "зарезервировано", "зарезервировано", "зарезервировано", "зарезервировано",
};

static void dump_regs(struct regs *r)
{
    kprintf("\n  RAX=%016lx RBX=%016lx RCX=%016lx\n"
            "  RDX=%016lx RSI=%016lx RDI=%016lx\n"
            "  RBP=%016lx RSP=%016lx R8 =%016lx\n"
            "  R9 =%016lx R10=%016lx R11=%016lx\n"
            "  R12=%016lx R13=%016lx R14=%016lx\n"
            "  R15=%016lx RIP=%016lx FLG=%016lx\n"
            "  CS=%04lx SS=%04lx INT=%lu ERR=%016lx\n",
            r->rax, r->rbx, r->rcx, r->rdx, r->rsi, r->rdi,
            r->rbp, r->rsp, r->r8, r->r9, r->r10, r->r11,
            r->r12, r->r13, r->r14, r->r15, r->rip, r->rflags,
            r->cs, r->ss, r->int_no, r->err);
}

static NORETURN void halt_forever(void)
{
    for (;;)
        hlt();
}

NORETURN void panic_regs(struct regs *r, const char *fmt, ...)
{
    cli();
    console_set_color(C_WHITE, C_RED);
    kprintf("\n=== ПАНИКА ЯДРА OC ===\n");
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kprintf("%s", buf);
    if (r) {
        if (r->int_no < 32)
            kprintf("\nИсключение: %s", exception_names[r->int_no]);
        if (r->int_no == 14)
            kprintf("  CR2=%016lx", read_cr2());
        dump_regs(r);
    }
    kprintf("\nСистема остановлена.\n");
    halt_forever();
}

NORETURN void panic(const char *fmt, ...)
{
    cli();
    console_set_color(C_WHITE, C_RED);
    kprintf("\n=== ПАНИКА ЯДРА OC ===\n");
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kprintf("%s\nСистема остановлена.\n", buf);
    halt_forever();
}

void kassert_fail(const char *expr, const char *file, int line)
{
    panic("сбой проверки (assert): %s — %s:%d", expr, file, line);
}
