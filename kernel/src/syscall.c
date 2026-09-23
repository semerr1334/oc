/*
 * OC OS — системные вызовы: int 0x80.
 *
 *   rax=0  write(rdi=буфер, rsi=длина)  -> вывод на консоль
 *   rax=1  uptime_ms()                  -> миллисекунды с загрузки
 *   rax=2  getkey()                     -> символ из очереди (0, если пусто)
 */
#include "types.h"
#include "idt.h"
#include "pit.h"
#include "keyboard.h"
#include "console.h"

#define SYS_WRITE   0
#define SYS_UPTIME  1
#define SYS_GETKEY  2

u64 syscall_dispatch(struct regs *r)
{
    switch (r->rax) {
    case SYS_WRITE: {
        const char *buf = (const char *)(uintptr_t)r->rdi;
        u64 len = r->rsi;
        console_write(buf, (size_t)len);
        return len;
    }
    case SYS_UPTIME:
        return pit_uptime_ms();
    case SYS_GETKEY: {
        u16 k;
        return kbd_trykey(&k) ? k : 0;
    }
    default:
        return (u64)-1;
    }
}
