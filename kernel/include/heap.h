/*
 * OC OS — куча ядра (kmalloc/kfree) поверх страниц, выделенных VMM+PMM.
 */
#ifndef OC_HEAP_H
#define OC_HEAP_H

#include "types.h"

#define HEAP_BASE     0x0000000400000000ULL   /* вне identity-окна */
#define HEAP_MAX_SIZE (64ULL << 20)           /* до 64 MiB */

void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
void  heap_stats(u64 *used, u64 *free_bytes, u64 *mapped);

#endif /* OC_HEAP_H */
