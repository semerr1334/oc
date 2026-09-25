/* game.ocp — «Угадай число»: первая игра OC! */
#include "ocp.h"

void _entry(void);
__attribute__((section(".text.entry"))) void _entry(void) { ocp_start(); }

static void put(const char *s) { writes(s); }

static void putnum(long v)
{
    char b[24];
    int i = 23;
    b[i] = 0;
    if (v == 0) { put("0"); return; }
    if (v < 0) { put("-"); v = -v; }
    while (v > 0 && i > 0) {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    put(&b[i]);
}

void ocp_start(void)
{
    long secret = (uptime_ms() % 100) + 1;
    int tries = 0;

    writes("\nУГАДАЙ ЧИСЛО! (игра OC)\n");
    writes("Я загадал число от 1 до 100. Угадай!\n");
    writes("Введи число и Enter. Esc — выйти.\n");

    for (;;) {
        writes("\nтвоё число> ");
        char line[12];
        int n = 0;
        for (;;) {
            u16 k = getkey();
            if (!k) {
                sleep_ms(20);
                continue;
            }
            if (k == 0x1110) {
                writes("\nСыграем в другой раз.\n");
                exit(1);
            }
            if (k == '\n' || k == '\r')
                break;
            if (k == 8 || k == 127) {
                if (n > 0) {
                    char b[4] = "\b \b";
                    n--;
                    writes(b);
                }
                continue;
            }
            if (k >= '0' && k <= '9' && n < 10) {
                char b[2];
                b[0] = (char)k;
                b[1] = 0;
                line[n++] = (char)k;
                writes(b);
            }
        }
        if (n == 0)
            continue;

        long v = 0;
        for (int i = 0; i < n; i++)
            v = v * 10 + (line[i] - '0');

        if (v < 1 || v > 100) {
            writes("нужно число от 1 до 100!\n");
            continue;
        }
        tries++;
        if (v == secret) {
            writes("УГАДАЛ!!! За ");
            putnum(tries);
            writes(tries == 1 ? " попытку!\n" : " попыток!\n");
            writes("Сыграем ещё: набери снова game\n");
            exit(0);
        }
        writes(v < secret ? "Больше!" : "Меньше!");
        writes(" (попытка ");
        putnum(tries);
        writes(")\n");
    }
}
