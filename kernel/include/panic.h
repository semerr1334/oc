/*
 * OC OS — паника и проверки условий.
 */
#ifndef OC_PANIC_H
#define OC_PANIC_H

#include "types.h"

NORETURN void panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
void kassert_fail(const char *expr, const char *file, int line);
#define kassert(expr) \
    do { if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__); } while (0)

#endif /* OC_PANIC_H */
