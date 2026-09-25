/*
 * OC OS — запуск программ .ocp в ring 3 (песочница).
 */
#ifndef OC_OCP_H
#define OC_OCP_H

int  ocp_run(const char *name);     /* найти файл и запуск; код выхода */
void ocp_exit(int code);            /* из syscall exit — не возвращается */

#endif /* OC_OCP_H */
