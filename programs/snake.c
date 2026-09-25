/* snake.ocp — ЗМЕЙКА! Первая игра с движением. */
#include "ocp.h"

void _entry(void);
__attribute__((section(".text.entry"))) void _entry(void) { ocp_start(); }

#define W 20
#define H 10

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
    static char frame[(W + 2) * (H + 2) + 8];
    int bx[64], by[64];              /* bx[0],by[0] — голова */
    int len = 3, dx = 1, dy = 0;
    int ax = 12, ay = 5;             /* яблоко */
    int score = 0;
    for (int i = 0; i < len; i++) {
        bx[i] = 8 - i;
        by[i] = 5;
    }

    writes("\nЗМЕЙКА OC!\n");
    writes("Управление: W A S D — или Ц Ф Ы В на RU.\n");
    writes("Esc — выход. Любая клавиша — старт! ");
    while (!getkey())
        sleep_ms(20);
    clear();

    long last = uptime_ms() - 1000;      /* первый кадр — сразу */
    for (;;) {
        u16 k = getkey();
        if (k == 0x1110 || k == 'q' || k == 0x439) {       /* Esc / q / й */
            clear();
            writes("Игра окончена! Счёт: ");
            putnum(score);
            writes("\n");
            exit(0);
        }
        if ((k == 'w' || k == 0x446) && dy == 0) { dx = 0; dy = -1; } /* W/Ц */
        if ((k == 's' || k == 0x44b) && dy == 0) { dx = 0; dy = 1; }  /* S/Ы */
        if ((k == 'a' || k == 0x444) && dx == 0) { dx = -1; dy = 0; } /* A/Ф */
        if ((k == 'd' || k == 0x432) && dx == 0) { dx = 1; dy = 0; }  /* D/В */

        long now = uptime_ms();
        if (now - last < 350) {
            sleep_ms(15);
            continue;
        }
        last = now;

        int nx = bx[0] + dx, ny = by[0] + dy;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) {
            clear();
            writes("ЗМЕЙКА разбилась о стену! Счёт: ");
            putnum(score);
            writes("\nИгра окончена!\n");
            exit(0);
        }
        for (int i = 0; i < len - 1; i++)
            if (bx[i] == nx && by[i] == ny) {
                clear();
                writes("ЗМЕЙКА укусила себя! Счёт: ");
                putnum(score);
                writes("\nИгра окончена!\n");
                exit(0);
            }

        if (nx == ax && ny == ay) {
            score += 10;
            if (len < 63)
                len++;
            ax = (int)((uptime_ms() / 7) % W);
            ay = (int)((uptime_ms() / 13) % H);
            for (int i = 0; i < len; i++)
                if (bx[i] == ax && by[i] == ay) {
                    ax = (ax + 3) % W;
                    ay = (ay + 2) % H;
                }
        }
        for (int i = len - 1; i > 0; i--) {
            bx[i] = bx[i - 1];
            by[i] = by[i - 1];
        }
        bx[0] = nx;
        by[0] = ny;

        int p = 0;
        for (int y = 0; y < H + 2; y++) {
            for (int x = 0; x < W + 2; x++) {
                char c = ' ';
                if (y == 0 || y == H + 1 || x == 0 || x == W + 1)
                    c = '#';
                else if (x - 1 == ax && y - 1 == ay)
                    c = '*';
                else
                    for (int i = 0; i < len; i++)
                        if (bx[i] == x - 1 && by[i] == y - 1) {
                            c = i ? 'o' : '@';
                            break;
                        }
                frame[p++] = c;
            }
            frame[p++] = '\n';
        }
        frame[p] = 0;
        clear();
        writes("ЗМЕЙКА! Счёт: ");
        putnum(score);
        writes("\n");
        writes(frame);
    }
}
