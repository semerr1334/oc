/*
 * OC OS — kprintf: форматированный вывод на консоль + COM1.
 */
#ifndef OC_KPRINTF_H
#define OC_KPRINTF_H

#include "types.h"

int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* OC_KPRINTF_H */
