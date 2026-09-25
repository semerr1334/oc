/*
 * OC OS — программируемый таймер 8253/8254 (PIT), 100 Гц.
 */
#ifndef OC_PIT_H
#define OC_PIT_H

#include "types.h"

#define PIT_HZ 100

void pit_init(void);
u64  pit_ticks(void);
u64  pit_uptime_ms(void);
void pit_sleep_ms(u64 ms);

#endif /* OC_PIT_H */
