/*
 * OC OS — клавиатура PS/2 (скан-коды, раскладки EN/RU, очередь символов).
 *
 * Переключение раскладки: Ctrl+Space ИЛИ Alt+Shift (как в Windows) ИЛИ
 * Ctrl+Shift — с уведомлением на экране (штука KEY_LAYOUT в очереди).
 */
#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "pic.h"
#include "string.h"

#define KBD_DATA 0x60
#define KBD_STAT 0x64

#define QUEUE_SIZE 256

static volatile u16 queue[QUEUE_SIZE];
static volatile u32 q_head, q_tail;

static bool shift, ctrl, alt, caps;
static bool e0_prefix;
static bool hotkey_used;    /* раскладка уже переключена в этом зажатии */
static enum kbd_layout layout = KBD_EN;

/*
 * Таблицы скан-кодов (set 1) -> Unicode. По индексу — скан-код 0x00..0x39.
 * EN: обычная / с Shift; RU: ЙЦУКЕН (включая твёрдый знак и ё).
 */
static const u16 map_en[2][64] = {
    { /* обычные */
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0, 'a','s',
        'd','f','g','h','j','k','l',';','\'','`',0, '\\','z','x','c','v',
        'b','n','m',',','.','/', 0, '*', 0, ' ', 0,
    },
    { /* shift */
        0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', '\b','\t',
        'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0, 'A','S',
        'D','F','G','H','J','K','L',':','"','~',0, '|','Z','X','C','V',
        'B','N','M','<','>','?', 0, '*', 0, ' ', 0,
    },
};

/* RU: ключи q..p -> й..з, a..; -> ф..ю, z.. -> я..с, и т.д. */
static const u16 map_ru[2][64] = {
    { /* обычные */
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', '\b','\t',
        0x439,0x446,0x443,0x43A,0x435,0x43D,0x433,0x448,0x449,0x437,0x445,0x44A,'\n',0, 0x444,0x44B,
        0x432,0x430,0x43F,0x440,0x43E,0x43B,0x434,0x436,0x44D, 0x451, 0, '\\', 0x44F,0x447,0x441,0x43C,
        0x438,0x442,0x44C,0x431,0x44E,'.', 0, '*', 0, ' ', 0,
    },
    { /* shift */
        0, 0, '!','"',0x2116,';','%',':','?','*','(',')','_','+', '\b','\t',
        0x419,0x426,0x423,0x41A,0x415,0x41D,0x413,0x428,0x429,0x417,0x425,0x42A,'\n',0, 0x424,0x42B,
        0x412,0x410,0x41F,0x420,0x41E,0x41B,0x414,0x416,0x42D, 0x401, 0, '/', 0x42F,0x447,0x421,0x41C,
        0x418,0x422,0x42C,0x411,0x42E,',', 0, '*', 0, ' ', 0,
    },
};

static void push_key(u16 k)
{
    u32 next = (q_head + 1) % QUEUE_SIZE;
    if (next == q_tail)
        return;                     /* очередь полна — теряем */
    queue[q_head] = k;
    q_head = next;
}

static void toggle_layout(void)
{
    layout = (layout == KBD_EN) ? KBD_RU : KBD_EN;
    hotkey_used = true;
    push_key(KEY_LAYOUT);           /* шелл выведет «раскладка: RU/EN» */
}

static void handle_scancode(u8 sc)
{
    if (sc == 0xE0) {
        e0_prefix = true;
        return;
    }
    bool release = (sc & 0x80) != 0;
    u8 code = sc & 0x7F;

    /* модификаторы; Alt+Shift / Ctrl+Shift / Ctrl+Space — смена раскладки */
    bool is_mod = (code == 0x2A || code == 0x36 || code == 0x1D ||
                   code == 0x38);
    if (code == 0x2A || code == 0x36) {
        shift = !release;
        if (!release && !hotkey_used && (ctrl || alt))
            toggle_layout();
    } else if (code == 0x1D) {
        ctrl = !release;
        if (!release && !hotkey_used && (shift || alt))
            toggle_layout();
    } else if (code == 0x38) {
        alt = !release;
        if (!release && !hotkey_used && (shift || ctrl))
            toggle_layout();
    }
    if (!shift && !ctrl && !alt)
        hotkey_used = false;        /* все отпустили — комбинация свежая */
    if (is_mod) {
        e0_prefix = false;
        return;
    }

    if (release) { e0_prefix = false; return; }

    if (code == 0x3A) {             /* CapsLock */
        caps = !caps;
        return;
    }

    /* расширенные клавиши */
    if (e0_prefix) {
        e0_prefix = false;
        switch (code) {
        case 0x48: push_key(KEY_UP); return;
        case 0x50: push_key(KEY_DOWN); return;
        case 0x4B: push_key(KEY_LEFT); return;
        case 0x4D: push_key(KEY_RIGHT); return;
        case 0x47: push_key(KEY_HOME); return;
        case 0x4F: push_key(KEY_END); return;
        case 0x49: push_key(KEY_PGUP); return;
        case 0x51: push_key(KEY_PGDN); return;
        case 0x53: push_key(KEY_DELETE); return;
        default: return;
        }
    }

    /* Ctrl+Space — смена раскладки */
    if (ctrl && code == 0x39 && !hotkey_used) {
        toggle_layout();
        return;
    }
    if (code == 0x01) {             /* Esc */
        push_key(KEY_ESC);
        return;
    }

    if (code >= 64)
        return;

    u16 ch = (layout == KBD_RU ? map_ru : map_en)[shift ? 1 : 0][code];
    if (ch == 0)
        return;
    /* Caps инвертирует регистр букв */
    bool letter = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= 0x430 && ch <= 0x44F) || (ch >= 0x410 && ch <= 0x42F);
    if (letter && caps) {
        if (ch >= 'a' && ch <= 'z')
            ch = (u16)(ch - 'a' + 'A');
        else if (ch >= 'A' && ch <= 'Z')
            ch = (u16)(ch - 'A' + 'a');
        else if (ch >= 0x430 && ch <= 0x44F)
            ch = (u16)(ch - 0x20);
        else if (ch >= 0x410 && ch <= 0x42F)
            ch = (u16)(ch + 0x20);
    }
    if (ctrl && ch >= 'a' && ch <= 'z')
        ch = (u16)(ch - 'a' + 1);   /* Ctrl+A..Z -> 0x01..0x1A */
    push_key(ch);
}

static void kbd_irq(struct regs *r)
{
    (void)r;
    handle_scancode(inb(KBD_DATA));
}

/* ---- инициализация i8042 с защитой от зависаний (таймауты) ---- */

static int kbd_wait_in(void)        /* контроллер готов принимать */
{
    for (int i = 0; i < 50000; i++)
        if (!(inb(KBD_STAT) & 0x02))
            return 1;
    return 0;
}

static int kbd_wait_out(void)       /* есть байт ответа */
{
    for (int i = 0; i < 50000; i++)
        if (inb(KBD_STAT) & 0x01)
            return 1;
    return 0;
}

void kbd_init(void)
{
    q_head = q_tail = 0;
    shift = ctrl = alt = caps = false;
    e0_prefix = false;
    hotkey_used = false;

    /* Приводим к канону: клавиатура — набор скан-кодов 2, контроллер —
     * с трансляцией в набор 1 (как делают BIOS'ы). После UEFI состояние
     * контроллера не гарантировано — из-за этого на новых ноутбуках
     * (например, ваш HP) скан-коды могли приходить «не те». */
    while (inb(KBD_STAT) & 0x01)
        inb(KBD_DATA);              /* сброс буфера */
    if (kbd_wait_in()) outb(KBD_STAT, 0x60);    /* записать конфигурацию */
    if (kbd_wait_in()) outb(KBD_DATA, 0x47);    /* IRQ + трансляция вкл */
    if (kbd_wait_in()) outb(KBD_DATA, 0xF0);    /* выбрать набор... */
    if (kbd_wait_out()) inb(KBD_DATA);          /* ...ACK */
    if (kbd_wait_in()) outb(KBD_DATA, 0x02);    /* ...набор 2 (станет 1) */
    if (kbd_wait_out()) inb(KBD_DATA);          /* ACK */
    if (kbd_wait_in()) outb(KBD_STAT, 0xAE);    /* включить клавиатуру */
    while (inb(KBD_STAT) & 0x01)
        inb(KBD_DATA);

    irq_set_handler(1, kbd_irq);
    irq_set_mask(1, false);
}

bool kbd_trykey(u16 *out)
{
    if (q_head == q_tail)
        return false;
    *out = queue[q_tail];
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    return true;
}

u16 kbd_getkey(void)
{
    u16 k;
    for (;;) {
        if (kbd_trykey(&k))
            return k;
        hlt();
    }
}

enum kbd_layout kbd_layout(void) { return layout; }
void kbd_set_layout(enum kbd_layout l) { layout = l; }
const char *kbd_layout_name(enum kbd_layout l) { return l == KBD_RU ? "RU" : "EN"; }
bool kbd_ctrl_down(void)  { return ctrl; }
bool kbd_shift_down(void) { return shift; }
bool kbd_alt_down(void)   { return alt; }
