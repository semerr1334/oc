#include "pmm.h"
#include "string.h"
#include "kprintf.h"

#define MAX_FRAMES (PMM_MAX_PHYS / PMM_FRAME_SIZE)      /* 1M кадров */

static u8 bitmap[MAX_FRAMES / 8];       /* 1 = занято, 128 KiB в .bss */
static u64 frames_total, frames_used;
static u64 search_hint;

static inline void bit_set(u64 f)   { bitmap[f / 8] |= (u8)(1u << (f % 8)); }
static inline void bit_clear(u64 f) { bitmap[f / 8] &= (u8)~(1u << (f % 8)); }
static inline bool bit_test(u64 f)  { return (bitmap[f / 8] >> (f % 8)) & 1; }

static void mark_used(u64 start, u64 end)
{
    u64 f0 = start / PMM_FRAME_SIZE;
    u64 f1 = (end + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    if (f1 > MAX_FRAMES)
        f1 = MAX_FRAMES;
    for (u64 f = f0; f < f1; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            frames_used++;
        }
    }
}

static void mark_free(u64 start, u64 end)
{
    u64 f0 = (start + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    u64 f1 = end / PMM_FRAME_SIZE;
    if (f1 > MAX_FRAMES)
        f1 = MAX_FRAMES;
    for (u64 f = f0; f < f1; f++) {
        if (bit_test(f)) {
            bit_clear(f);
            frames_used--;
        }
    }
}

void pmm_reserve(u64 base, u64 frames)
{
    mark_used(base, base + frames * PMM_FRAME_SIZE);
}

void pmm_init(const oc_boot_info_t *bi, u64 kernel_start, u64 kernel_end)
{
    /* всё занято */
    memset(bitmap, 0xFF, sizeof(bitmap));
    frames_total = MAX_FRAMES;
    frames_used = MAX_FRAMES;
    search_hint = 0;

    /* окна доступной памяти из E820 */
    for (u32 i = 0; i < bi->e820_count && i < BI_E820_MAX; i++) {
        const struct oc_e820_entry *e = &bi->e820[i];
        if (e->type != E820_USABLE || e->length == 0)
            continue;
        u64 start = e->base;
        u64 end = e->base + e->length;
        if (start >= PMM_MAX_PHYS)
            continue;
        if (end > PMM_MAX_PHYS)
            end = PMM_MAX_PHYS;
        mark_free(start, end);
    }

    /* резервируем: низкие 1 MiB, ядро, фреймбуфер */
    mark_used(0, 0x100000);
    mark_used(kernel_start, kernel_end);
    if (bi->fb_present && bi->fb_addr) {
        u64 fb_size = (u64)bi->fb_pitch * bi->fb_height;
        mark_used(bi->fb_addr, bi->fb_addr + fb_size);
    }
}

u64 pmm_alloc_frame(void)
{
    for (u64 i = 0; i < MAX_FRAMES; i++) {
        u64 f = (search_hint + i) % MAX_FRAMES;
        if (!bit_test(f)) {
            bit_set(f);
            frames_used++;
            search_hint = f + 1;
            return f * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(u64 phys)
{
    u64 f = phys / PMM_FRAME_SIZE;
    if (f >= MAX_FRAMES)
        return;
    if (bit_test(f)) {
        bit_clear(f);
        frames_used--;
    }
}

u64 pmm_alloc_frames(u64 count)
{
    if (count == 0)
        return 0;
    u64 run = 0, start = 0;
    for (u64 f = 0; f < MAX_FRAMES; f++) {
        if (!bit_test(f)) {
            if (run == 0)
                start = f;
            run++;
            if (run == count) {
                for (u64 g = start; g < start + count; g++)
                    bit_set(g);
                frames_used += count;
                return start * PMM_FRAME_SIZE;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

u64 pmm_total_frames(void) { return frames_total; }
u64 pmm_used_frames(void)  { return frames_used; }
u64 pmm_free_frames(void)  { return frames_total - frames_used; }
