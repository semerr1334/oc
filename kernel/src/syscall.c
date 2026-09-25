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
#include "ocp.h"
#include "speaker.h"

#define SYS_WRITE   0
#define SYS_UPTIME  1
#define SYS_GETKEY  2
#define SYS_EXIT    3
#define SYS_SLEEP   4
#define SYS_CLEAR   5
#define SYS_TONE    6

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
    case SYS_EXIT:
        ocp_exit((int)r->rdi);
        return 0;                       /* недостижимо */
    case SYS_SLEEP:
        __asm__ volatile("sti");        /* прерывания для таймера */
        pit_sleep_ms(r->rdi);
        __asm__ volatile("cli");
        return 0;
    case SYS_CLEAR:
        console_clear();
        return 0;
    case SYS_TONE:                      /* rdi = Гц, 0 = выключить */
        speaker_freq((u32)r->rdi);
        return 0;
    default:
        return (u64)-1;
    }
}
