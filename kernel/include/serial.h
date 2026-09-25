/*
 * OC OS — последовательный порт COM1 + отладочный порт 0xE9.
 */
#ifndef OC_SERIAL_H
#define OC_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_puts(const char *s);

#endif /* OC_SERIAL_H */
