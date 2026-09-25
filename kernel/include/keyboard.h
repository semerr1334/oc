/*
 * OC OS — клавиатура PS/2 (скан-коды, раскладки EN/RU, очередь символов).
 */
#ifndef OC_KEYBOARD_H
#define OC_KEYBOARD_H

#include "types.h"

/* спецкоды очереди (выше диапазона Unicode) */
#define KEY_UP      0x1100
#define KEY_DOWN    0x1101
#define KEY_LEFT    0x1102
#define KEY_RIGHT   0x1103
#define KEY_HOME    0x1104
#define KEY_END     0x1105
#define KEY_PGUP    0x1106
#define KEY_PGDN    0x1107
#define KEY_DELETE  0x1108
#define KEY_ESC     0x1110
#define KEY_LAYOUT  0x1120    /* «раскладка переключена» (уведомление драйвера) */

enum kbd_layout { KBD_EN = 0, KBD_RU = 1 };

void kbd_init(void);
u16  kbd_getkey(void);          /* блокирующее чтение символа/спецкода */
bool kbd_trykey(u16 *out);      /* неблокирующее */
enum kbd_layout kbd_layout(void);
void kbd_set_layout(enum kbd_layout l);
const char *kbd_layout_name(enum kbd_layout l);
bool kbd_ctrl_down(void);
bool kbd_shift_down(void);
bool kbd_alt_down(void);

#endif /* OC_KEYBOARD_H */
