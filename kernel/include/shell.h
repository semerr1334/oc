/*
 * OC OS — командная оболочка.
 */
#ifndef OC_SHELL_H
#define OC_SHELL_H

#include "types.h"
#include "boot.h"

void shell_run(const oc_boot_info_t *bi);

#endif /* OC_SHELL_H */
