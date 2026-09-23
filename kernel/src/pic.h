/*
 * OC OS — контроллер прерываний 8259A.
 */
#ifndef OC_PIC_H
#define OC_PIC_H

#include "types.h"

#define PIC_MASTER_CMD  0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_CMD   0xA0
#define PIC_SLAVE_DATA  0xA1
#define PIC_EOI         0x20

#define IRQ_BASE        32

void pic_init(void);
void pic_eoi(u8 irq);
void pic_set_mask(u8 irq, bool masked);
u16  pic_get_mask(void);

#endif /* OC_PIC_H */
