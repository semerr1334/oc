/* piano.ocp — ПИАНИНО OC: играй на динамике ПК! */
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
    while (v > 0 && i > 0) {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    put(&b[i]);
}

/* Q W E R T Y U и пробел — ноты; на RU эти же клавиши дают Ц У К Е Н Г Ш */
static const struct { u16 k1, k2; const char *name; long hz; } notes[] = {
    { 'q', 0x439, "до",    262 },
    { 'w', 0x446, "ре",    294 },
    { 'e', 0x443, "ми",    330 },
    { 'r', 0x43A, "фа",    349 },
    { 't', 0x435, "соль",  392 },
    { 'y', 0x43D, "ля",    440 },
    { 'u', 0x433, "си",    494 },
    { ' ', 0,     "до-2",  523 },
};

void ocp_start(void)
{
    writes("\nПИАНИНО OC!\n");
    writes("Ноты: Q W E R T Y U — до ре ми фа соль ля си.\n");
    writes("На RU: те же клавиши (Ц У К Е Н Г Ш). Пробел — высокое до.\n");
    writes("Esc — выход из пианино.\n");

    for (;;) {
        u16 k = getkey();
        if (!k) {
            sleep_ms(20);
            continue;
        }
        if (k == 0x1110) {
            tone(0);
            writes("Пианино закрыто.\n");
            exit(0);
        }
        for (int i = 0; i < 8; i++) {
            if (k == notes[i].k1 || (notes[i].k2 && k == notes[i].k2)) {
                writes("нота: ");
                put(notes[i].name);
                writes(" — ");
                putnum(notes[i].hz);
                writes(" Гц\n");
                tone(notes[i].hz);
                sleep_ms(60);
                tone(0);
                break;
            }
        }
    }
}
