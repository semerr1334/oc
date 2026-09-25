/*
 * OC OS — файловая система (встроенный ramdisk формата OCFS).
 */
#ifndef OC_FS_H
#define OC_FS_H

#include "types.h"

bool fs_init(void);                      /* распознать образ */
u32  fs_count(void);
const char *fs_name(u32 i);
const void *fs_file(const char *name, u32 *len);   /* NULL если файла нет */

#endif /* OC_FS_H */
