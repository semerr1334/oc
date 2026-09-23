/*
 * OC OS — мышь PS/2 и курсор на фреймбуфере.
 */
#ifndef OC_MOUSE_H
#define OC_MOUSE_H

#include "types.h"

struct mouse_state {
    s32 x, y;
    u8 buttons;     /* 0=ЛКМ 1=ПКМ 2=СКМ */
};

void mouse_init(void);
void mouse_get(struct mouse_state *out);
void mouse_show(void);          /* рисовать курсор (только fb) */
void mouse_hide(void);
void mouse_poll_demo(void);     /* демо до нажатия клавиши */

#endif /* OC_MOUSE_H */
