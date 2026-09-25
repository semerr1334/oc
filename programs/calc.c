/* calc.ocp — калькулятор OC. Целые числа, приоритет * / над + -. */
#include "ocp.h"

void _entry(void);
__attribute__((section(".text.entry"))) void _entry(void) { ocp_start(); }

static void put(const char *s) { writes(s); }

static void put1(u16 c)
{
    char b[2];
    b[0] = (char)c;
    b[1] = 0;
    writes(b);
}

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
    char line[80];

    writes("\nКАЛЬКУЛЯТОР OC\n");
    writes("Целые числа, приоритет: сначала * /, потом + -.\n");
    writes("Деление: '/' или '.' (точка — на обеих раскладках).\n");
    writes("Пример: 2+3*4  Enter.  Esc или q — выход.\n");

    for (;;) {
        writes("\nвыражение> ");
        int n = 0;
        for (;;) {
            u16 k = getkey();
            if (!k) {
                sleep_ms(20);
                continue;
            }
            if (k == 0x1110 || k == 'q' || k == 0x439) {   /* Esc / q / й */
                writes("\nКалькулятор закрыт.\n");
                exit(0);
            }
            if (k == '\n' || k == '\r')
                break;
            if (k == 8 || k == 127) {
                if (n > 0) { n--; put("\b \b"); }
                continue;
            }
            if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*') {
                if (n < 70) { line[n++] = (char)k; put1(k); }
                continue;
            }
            if (k == '/' || k == '.') {                   /* деление */
                if (n < 70) { line[n++] = '/'; put1('/'); }
                continue;
            }
        }
        line[n] = 0;
        if (n == 0)
            continue;

        long nums[36];
        char ops[36];
        int nn = 0, no = 0, i = 0, bad = 0;
        while (i < n) {
            if (line[i] >= '0' && line[i] <= '9') {
                long v = 0;
                while (i < n && line[i] >= '0' && line[i] <= '9')
                    v = v * 10 + (line[i++] - '0');
                nums[nn++] = v;
            } else if (line[i] == '+' || line[i] == '-' ||
                       line[i] == '*' || line[i] == '/') {
                if (nn == no) { bad = 1; break; }
                ops[no++] = line[i++];
            } else {
                bad = 1;
                break;
            }
        }
        if (nn != no + 1)
            bad = 1;
        if (bad) {
            writes("не понял выражение. Пример: 2+3*4\n");
            continue;
        }

        /* первый проход: * и / */
        int m = 1;
        for (i = 0; i < no; i++) {
            long b = nums[i + 1];
            char op = ops[i];
            if (op == '*') {
                nums[m - 1] *= b;
            } else if (op == '/') {
                if (b == 0) { bad = 1; break; }
                nums[m - 1] /= b;
            } else {
                nums[m] = b;
                ops[m - 1] = op;
                m++;
            }
        }
        if (bad) {
            writes("на ноль делить нельзя!\n");
            continue;
        }
        /* второй проход: + и - */
        long res = nums[0];
        for (i = 0; i < m - 1; i++) {
            if (ops[i] == '+')
                res += nums[i + 1];
            else
                res -= nums[i + 1];
        }
        writes("= ");
        putnum(res);
        writes("\n");
    }
}
