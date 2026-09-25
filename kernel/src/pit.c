#include "pit.h"
#include "idt.h"
#include "io.h"
#include "pic.h"

#define PIT_CH0  0x40
#define PIT_MODE 0x43
#define PIT_BASE 1193182u

static volatile u64 ticks;

static void pit_irq(struct regs *r)
{
    (void)r;
    ticks++;
}

void pit_init(void)
{
    ticks = 0;
    u32 divisor = PIT_BASE / PIT_HZ;
    outb(PIT_MODE, 0x36);           /* канал 0, lo/hi, режим 3 */
    outb(PIT_CH0, (u8)(divisor & 0xFF));
    outb(PIT_CH0, (u8)((divisor >> 8) & 0xFF));
    irq_set_handler(0, pit_irq);
    irq_set_mask(0, false);
}

u64 pit_ticks(void) { return ticks; }
u64 pit_uptime_ms(void) { return ticks * (1000u / PIT_HZ); }

void pit_sleep_ms(u64 ms)
{
    u64 target = ticks + (ms * PIT_HZ + 999) / 1000 + 1;
    while (ticks < target)
        hlt();
}
