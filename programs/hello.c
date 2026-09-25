/* hello.ocp — первая программа OC. Крутится в ring3 (ring 3 = песочница). */
#include "ocp.h"

/* вход по нулевому смещению файла — секция .text.entry (см. scripts/ocp.ld) */
void _entry(void);
__attribute__((section(".text.entry"))) void _entry(void) { ocp_start(); }

void ocp_start(void)
{
    writes("\nПривет из ПРОГРАММЫ!\n");
    writes("Я — приложение формата .ocp и кручусь в ring3,\n");
    writes("как настоящие программы: ядро защищено от меня.\n\n");
    writes("Любая клавиша — выйти...\n");
    while (!getkey())
        sleep_ms(20);
    writes("Пока-пока!\n");
    exit(0);
}
