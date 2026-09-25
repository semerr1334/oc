/*
 * OC OS — базовые типы.
 */
#ifndef OC_TYPES_H
#define OC_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;
typedef int64_t   s64;

#define PACKED        __attribute__((packed))
#define ALIGNED(x)    __attribute__((aligned(x)))
#define NORETURN      __attribute__((noreturn))
#define UNUSED        __attribute__((unused))

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* OC_TYPES_H */
