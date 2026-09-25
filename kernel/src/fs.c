/*
 * OC OS — файловая система OCFS (ramdisk внутри ядра).
 */
#include "fs.h"
#include "string.h"

extern u8 ramdisk_start[], ramdisk_end[];

struct ocfs_ent {
    char name[32];
    u32 off, len;
} PACKED;

static const u8 *base;
static u32 count;
static bool ok;

bool fs_init(void)
{
    base = ramdisk_start;
    ok = false;
    count = 0;
    if (ramdisk_end - ramdisk_start < 8)
        return false;
    if (memcmp(base, "OCFS", 4) != 0)
        return false;
    count = *(const u32 *)(base + 4);
    if (8 + (u64)count * 40 > (u64)(ramdisk_end - ramdisk_start))
        return false;
    ok = true;
    return true;
}

u32 fs_count(void) { return ok ? count : 0; }

const char *fs_name(u32 i)
{
    if (!ok || i >= count)
        return "";
    const struct ocfs_ent *e = (const struct ocfs_ent *)(base + 8 + i * 40);
    return e->name;
}

const void *fs_file(const char *name, u32 *len)
{
    if (!ok)
        return 0;
    for (u32 i = 0; i < count; i++) {
        const struct ocfs_ent *e = (const struct ocfs_ent *)(base + 8 + i * 40);
        if (strcmp(e->name, name) == 0) {
            if (len)
                *len = e->len;
            return base + e->off;
        }
    }
    return 0;
}
