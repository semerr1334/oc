#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "panic.h"
#include "kprintf.h"

#define HEAP_CHUNK   (16 * PAGE_SIZE)     /* расширяем на 64 KiB */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((u64)(a) - 1))

struct block {
    u64 size;               /* размер данных (без заголовка) */
    struct block *prev;
    struct block *next;
    u32 free;
    u32 magic;
} PACKED;

#define BLK_MAGIC 0x48454150u   /* "HEAP" */
#define BLK_HDR   sizeof(struct block)

static struct block *head;
static u64 heap_end;            /* текущий конец отображённой кучи */
static u64 heap_mapped;

static int grow(u64 need)
{
    u64 add = ALIGN_UP(need, HEAP_CHUNK);
    if (heap_end + add > HEAP_BASE + HEAP_MAX_SIZE)
        return -1;
    for (u64 va = heap_end; va < heap_end + add; va += PAGE_SIZE) {
        u64 pa = pmm_alloc_frame();
        if (!pa)
            return -1;
        if (vmm_map_page(va, pa, PTE_WRITE) != 0)
            return -1;
    }
    heap_end += add;
    heap_mapped += add;
    return 0;
}

void heap_init(void)
{
    heap_end = HEAP_BASE;
    heap_mapped = 0;
    kassert(grow(HEAP_CHUNK) == 0);
    head = (struct block *)(uintptr_t)HEAP_BASE;
    head->size = heap_end - HEAP_BASE - BLK_HDR;
    head->prev = head->next = NULL;
    head->free = 1;
    head->magic = BLK_MAGIC;
}

static void split(struct block *b, u64 size)
{
    if (b->size < size + BLK_HDR + 16)
        return;
    struct block *n = (struct block *)((u8 *)b + BLK_HDR + size);
    n->size = b->size - size - BLK_HDR;
    n->free = 1;
    n->magic = BLK_MAGIC;
    n->prev = b;
    n->next = b->next;
    if (n->next)
        n->next->prev = n;
    b->next = n;
    b->size = size;
}

static void merge_next(struct block *b)
{
    struct block *n = b->next;
    if (!n || !n->free)
        return;
    b->size += BLK_HDR + n->size;
    b->next = n->next;
    if (b->next)
        b->next->prev = b;
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;
    size = ALIGN_UP(size, 16);

    for (;;) {
        for (struct block *b = head; b; b = b->next) {
            kassert(b->magic == BLK_MAGIC);
            if (b->free && b->size >= size) {
                split(b, size);
                b->free = 0;
                return (u8 *)b + BLK_HDR;
            }
        }
        /* не нашли — расширяем кучу и сливаем хвост */
        u64 before = heap_mapped;
        if (grow(size + BLK_HDR) != 0)
            return NULL;
        /* новый свободный кусок в конец списка */
        struct block *last = head;
        while (last->next)
            last = last->next;
        if (last->free) {
            last->size += heap_mapped - before;
        } else {
            struct block *n =
                (struct block *)((u8 *)last + BLK_HDR + last->size);
            n->size = heap_mapped - before - BLK_HDR;
            n->free = 1;
            n->magic = BLK_MAGIC;
            n->prev = last;
            n->next = NULL;
            last->next = n;
        }
    }
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    struct block *b = (struct block *)((u8 *)ptr - BLK_HDR);
    kassert(b->magic == BLK_MAGIC);
    b->free = 1;
    merge_next(b);
    if (b->prev && b->prev->free)
        merge_next(b->prev);
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    struct block *b = (struct block *)((u8 *)ptr - BLK_HDR);
    if (b->size >= size)
        return ptr;
    void *n = kmalloc(size);
    if (!n)
        return NULL;
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

void heap_stats(u64 *used, u64 *free_bytes, u64 *mapped)
{
    u64 u = 0, f = 0;
    for (struct block *b = head; b; b = b->next) {
        if (b->free)
            f += b->size;
        else
            u += b->size;
    }
    if (used) *used = u;
    if (free_bytes) *free_bytes = f;
    if (mapped) *mapped = heap_mapped;
}
