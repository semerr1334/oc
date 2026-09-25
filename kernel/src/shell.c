#include "shell.h"
#include "gui.h"
#include "console.h"
#include "kprintf.h"
#include "keyboard.h"
#include "pit.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "fb.h"
#include "vga.h"
#include "mouse.h"
#include "speaker.h"
#include "fs.h"
#include "ocp.h"
#include "string.h"
#include "io.h"
#include "panic.h"

#define LINE_MAX   160
#define HIST_MAX   8
#define ARGV_MAX   8

static const oc_boot_info_t *bootinfo;
static char line[LINE_MAX];
static u32 line_len;
static char history[HIST_MAX][LINE_MAX];
static u32 hist_count, hist_browse;

/* ---------------- редактирование строки ---------------- */

static void redraw_prompt(void)
{
    console_set_color(C_LIGHT_GREEN, C_BLACK);
    kprintf("oc> ");
    console_set_color(C_LIGHT_GRAY, C_BLACK);
}

static void redraw_line(void)
{
    kprintf("%s", line);
    kprintf(" \b");
}

static void line_insert(char c)
{
    if (line_len >= LINE_MAX - 1)
        return;
    line[line_len++] = c;
    line[line_len] = '\0';
    console_putc(c);
}

static void line_backspace(void)
{
    if (line_len == 0)
        return;
    line_len--;
    line[line_len] = '\0';
    console_putc('\b');
}

static void history_add(const char *s)
{
    if (!s[0])
        return;
    if (hist_count > 0 && strcmp(history[(hist_count - 1) % HIST_MAX], s) == 0)
        return;
    strncpy(history[hist_count % HIST_MAX], s, LINE_MAX - 1);
    history[hist_count % HIST_MAX][LINE_MAX - 1] = '\0';
    hist_count++;
    hist_browse = hist_count;
}

static void history_show(int dir)
{
    if (hist_count == 0)
        return;
    if (dir < 0) {
        if (hist_browse == 0)
            return;
        hist_browse--;
    } else {
        if (hist_browse >= hist_count)
            return;
        hist_browse++;
    }
    /* перерисовываем строку */
    while (line_len)
        line_backspace();
    const char *s = (hist_browse < hist_count)
        ? history[hist_browse % HIST_MAX] : "";
    strncpy(line, s, LINE_MAX - 1);
    line[LINE_MAX - 1] = '\0';
    line_len = (u32)strlen(line);
    redraw_line();
}

/* ---------------- команды ---------------- */

static void cmd_help(void)
{
    kprintf(
        "\nКоманды OC:\n"
        "  помощь/help          эта справка\n"
        "  очистить/clear       очистить экран\n"
        "  инфо/info            сведения о системе\n"
        "  память/mem           физическая память и куча\n"
        "  карты/mmap           карта памяти E820\n"
        "  таймер/ticks         счётчик таймера и аптайм\n"
        "  сон/sleep N          пауза N мс\n"
        "  тест/ktest           стресс-тест кучи\n"
        "  цвет/color N         цвет текста (0..15)\n"
        "  залить/fill N        залить экран цветом N\n"
        "  мышь/mouse           демо мыши\n"
        "  писк/beep            писк динамика\n"
        "  мелодия/song         мелодия динамиком\n"
        "  звёзды/stars [N]     заставка «Звёздное небо»\n"
        "  меню/menu            главное меню (мышь + стрелки + Enter)\n"
        "  рабстол/desktop       графический рабочий стол (окна, мышь!)\n"
        "  файлы/ls             файлы в системе\n"
        "  тип/cat ИМЯ          показать файл (например notes.txt)\n"
        "  пуск/run ИМЯ         запустить программу (.ocp)\n"
        "  программы/programs   все программы: calc game snake piano wiki hello\n"
        "  calc, game, snake…   запуск программы по имени файла!\n"
        "  раскладка/layout     EN/RU: Ctrl+Space, Alt+Shift или эта команда\n"
        "  ru/en                принудительно RU или EN\n"
        "  вызов/sysdemo        демонстрация int 0x80\n"
        "  перезагруз/reboot    перезагрузка ПК\n"
        "  стоп/halt            остановка\n"
        "  об/about             об этой системе\n"
        "\n"
        "Если раскладка «залипла» и русские буквы не набираются:\n"
        "  наберите layout (латиницей!) или ru — и раскладка переключится.\n"
        "  Пальцы «на автомате» тоже работают: hfcrkflrf / дфнщге.\n");
}

static void cmd_info(void)
{
    char vendor[13];
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';

    __asm__ volatile("cpuid" : "=a"(a) : "a"(1) : "rbx", "rcx", "rdx");
    u32 family = (a >> 8) & 0xF, model = (a >> 4) & 0xF, step = a & 0xF;

    kprintf("\nПроцессор : %s (family %u, model %u, step %u)\n",
            vendor, family, model, step);
    kprintf("Режим     : Long Mode (x86_64)\n");
    kprintf("Видео     : %s", console_is_graphic() ? "графика " : "текст VGA");
    if (console_is_graphic())
        kprintf("%ux%u", fb_width_px(), fb_height_px());
    kprintf("\nКонсоль   : %ux%u символов\n", console_width(), console_height());
    kprintf("Ядро      : %u байт (загружено)\n", bootinfo->kernel_size);
    kprintf("Загрузка с диска BIOS #%u\n", bootinfo->boot_drive);
    kprintf("Таймер    : %llu мс, CR3=%llx\n",
            (unsigned long long)pit_uptime_ms(),
            (unsigned long long)vmm_root());
}

static void cmd_mem(void)
{
    u64 total = pmm_total_frames() * PMM_FRAME_SIZE;
    u64 used = pmm_used_frames() * PMM_FRAME_SIZE;
    u64 free = pmm_free_frames() * PMM_FRAME_SIZE;
    kprintf("\nФизическая память (0..4GiB):\n");
    kprintf("  всего %llu KiB, занято %llu KiB, свободно %llu KiB\n",
            (unsigned long long)(total / 1024),
            (unsigned long long)(used / 1024),
            (unsigned long long)(free / 1024));
    u64 hu, hf, hm;
    heap_stats(&hu, &hf, &hm);
    kprintf("Куча: карта %llu KiB, занято %llu, свободно %llu\n",
            (unsigned long long)(hm / 1024),
            (unsigned long long)hu,
            (unsigned long long)hf);
}

static void cmd_mmap(void)
{
    kprintf("\nКарта памяти E820 (%u записей):\n", bootinfo->e820_count);
    kprintf("    база               размер             тип\n");
    for (u32 i = 0; i < bootinfo->e820_count && i < BI_E820_MAX; i++) {
        const struct oc_e820_entry *e = &bootinfo->e820[i];
        const char *t = e->type == E820_USABLE ? "доступно" :
                        e->type == E820_RESERVED ? "занято" :
                        e->type == E820_ACPI ? "ACPI" :
                        e->type == E820_NVS ? "ACPI NVS" : "битое";
        kprintf("  %016llx  %016llx  %s\n",
                (unsigned long long)e->base,
                (unsigned long long)e->length, t);
    }
}

static void cmd_ticks(void)
{
    kprintf("\nТактов таймера: %llu, аптайм: %llu.%03llu с\n",
            (unsigned long long)pit_ticks(),
            (unsigned long long)(pit_uptime_ms() / 1000),
            (unsigned long long)(pit_uptime_ms() % 1000));
}

static void cmd_sleep(const char *arg)
{
    u64 ms = 0;
    while (*arg >= '0' && *arg <= '9')
        ms = ms * 10 + (u64)(*arg++ - '0');
    if (ms == 0)
        ms = 1000;
    kprintf("сплю %llu мс...\n", (unsigned long long)ms);
    pit_sleep_ms(ms);
    kprintf("готово.\n");
}

static void cmd_ktest(void)
{
    kprintf("стресс-тест кучи: 64 блока...\n");
    void *ptrs[64];
    u32 ok = 0;
    for (int i = 0; i < 64; i++) {
        size_t sz = (size_t)(16 + i * 37);
        ptrs[i] = kmalloc(sz);
        if (ptrs[i]) {
            memset(ptrs[i], 0xA5, sz);
            ok++;
        }
    }
    for (int i = 0; i < 64; i += 2)
        kfree(ptrs[i]);
    for (int i = 0; i < 64; i += 2) {
        size_t sz = (size_t)(16 + i * 37);
        ptrs[i] = kmalloc(sz);
        if (ptrs[i]) {
            memset(ptrs[i], 0x5A, sz);
            ok++;
        }
    }
    for (int i = 0; i < 64; i++)
        kfree(ptrs[i]);
    u64 hu, hf, hm;
    heap_stats(&hu, &hf, &hm);
    kprintf("выполнено %u операций; куча занята %llu, свободно %llu\n",
            ok, (unsigned long long)hu, (unsigned long long)hf);
}

static void cmd_color(const char *arg)
{
    u32 n = 0;
    while (*arg >= '0' && *arg <= '9')
        n = n * 10 + (u32)(*arg++ - '0');
    if (n > 15) {
        kprintf("цвет: нужен индекс 0..15\n");
        return;
    }
    enum oc_color bg;
    console_get_color(NULL, &bg);
    console_set_color((enum oc_color)n, bg);
    kprintf("цвет текста: %u\n", n);
}

static void cmd_fill(const char *arg)
{
    u32 n = 0;
    while (*arg >= '0' && *arg <= '9')
        n = n * 10 + (u32)(*arg++ - '0');
    if (n > 15) {
        kprintf("цвет: нужен индекс 0..15\n");
        return;
    }
    console_clear();
    if (fb_ready())
        fb_fill(console_palette((enum oc_color)n));
    else {
        for (u32 r = 0; r < VGA_ROWS; r++)
            for (u32 c = 0; c < VGA_COLS; c++)
                ; /* текстовый режим: просто очистили */
    }
    kprintf("экран залит цветом %u\n", n);
    (void)0;
}

static void cmd_sysdemo(void)
{
    const char msg[] = "привет из int 0x80 (syscall write)!\n";
    u64 ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(0), "D"(msg), "S"(sizeof(msg) - 1)
                     : "memory", "rcx", "r11");
    u64 ms;
    __asm__ volatile("int $0x80" : "=a"(ms) : "a"(1) : "rcx", "r11");
    kprintf("syscall write вернул %llu, uptime syscall: %llu мс\n",
            (unsigned long long)ret, (unsigned long long)ms);
}

static void cmd_reboot(void)
{
    kprintf("перезагрузка...\n");
    /* современный метод: 0xCF9 */
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);
    /* запасной: через контроллер клавиатуры */
    for (int i = 0; i < 100000; i++)
        io_wait();
    outb(0x64, 0xFE);
    panic("перезагрузка не удалась");
}

static void cmd_about(void)
{
    kprintf(
        "\nOC — «своя система» с нуля.\n"
        "  Собственный загрузчик (MBR + stage2), Long Mode, ядро на C.\n"
        "  Прерывания, таймер, клавиатура, мышь, память, куча, шелл.\n"
        "  (c) 2026, проект OC. Версия 0.1 «Двухсотметровка».\n");
}

/* ---------------- файлы и программы ---------------- */
static void cmd_progs(void)
{
    kprintf("программы OC (.ocp) — набери имя и Enter:\n");
    for (u32 i = 0; i < fs_count(); i++) {
        const char *n = fs_name(i);
        u32 l = 0;
        while (n[l])
            l++;
        if (l > 4 && n[l - 4] == '.' && n[l - 3] == 'o' &&
            n[l - 2] == 'c' && n[l - 1] == 'p')
            kprintf("  %s\n", n);
    }
    kprintf("набери имя (например calc) — программа запустится\n");
}

static void cmd_files(void)
{
    u32 n = fs_count();
    kprintf("\nФайлы в системе (%u):\n", n);
    for (u32 i = 0; i < n; i++) {
        u32 len = 0;
        const char *nm = fs_name(i);
        fs_file(nm, &len);
        kprintf("  %s (%u байт)\n", nm, len);
    }
    if (!n)
        kprintf("  (пусто)\n");
}

static void cmd_cat(const char *arg)
{
    u32 len = 0;
    if (!arg[0]) {
        kprintf("usage: тип ИМЯ (например: тип notes.txt)\n");
        return;
    }
    const char *p = fs_file(arg, &len);
    if (!p) {
        kprintf("нет файла %s\n", arg);
        return;
    }
    kprintf("\n--- %s (%u байт) ---\n", arg, len);
    for (u32 i = 0; i < len; i++) {
        char c = p[i];
        if (c == '\n')
            console_putc(c);
        else if (c == '\t' || (u8)c >= ' ')
            console_putc(c);
    }
    kprintf("--- конец ---\n");
}

static void cmd_run(const char *arg)
{
    if (!arg[0]) {
        kprintf("usage: пуск ИМЯ (например: пуск wiki.ocp)\n");
        return;
    }
    int code = ocp_run(arg);
    kprintf("программа завершилась (код %d)\n", code);
}

/* имя файла — сразу запуск (как в Windows) */
static bool cmd_autorun(const char *cmd)
{
    char full[40];
    u32 len, n = (u32)strlen(cmd);

    if (!n || n > 32)
        return false;
    if (fs_file(cmd, &len)) {
        strncpy(full, cmd, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
    } else {
        if (n + 5 > sizeof(full))
            return false;
        memcpy(full, cmd, n);
        memcpy(full + n, ".ocp", 5);
        if (!fs_file(full, &len))
            return false;
    }
    cmd_run(full);
    return true;
}

/* ---------------- звук и заставка ---------------- */

static void cmd_beep(void)
{
    kprintf("писк! 880 Гц\n");
    speaker_beep();
}

static void cmd_song(void)
{
    kprintf("играю мелодию: до-ми-соль-до (x2)\n");
    speaker_song();
}

static void cmd_stars(const char *arg)
{
    u32 frames = 150;
    if (*arg >= '0' && *arg <= '9') {
        frames = 0;
        while (*arg >= '0' && *arg <= '9')
            frames = frames * 10 + (u32)(*arg++ - '0');
        if (frames == 0 || frames > 2000)
            frames = 150;
    }
    if (!fb_ready()) {
        kprintf("звёзды: заставка работает только в графическом режиме\n");
        return;
    }
    kprintf("звёздное небо (%u кадров) — любая клавиша: выход\n", frames);

    enum { NSTARS = 80 };
    u16 sx[NSTARS], sy[NSTARS];
    u8 br[NSTARS];
    u32 w = fb_width_px(), h = fb_height_px();
    u32 rnd = 0x2B992DDFu ^ (u32)pit_ticks();
    for (int i = 0; i < NSTARS; i++) {
        rnd = rnd * 1664525u + 1013904223u;
        sx[i] = (u16)(rnd % w);
        rnd = rnd * 1664525u + 1013904223u;
        sy[i] = (u16)(rnd % h);
        rnd = rnd * 1664525u + 1013904223u;
        br[i] = (u8)(rnd % 3);
    }
    u32 sky = fb_color(2, 2, 8);
    u32 done;
    u16 k;
    s16 ox[NSTARS], oy[NSTARS];     /* где рисовали в прошлом кадре */

    fb_fill(sky);                   /* ночь — один раз */
    for (int i = 0; i < NSTARS; i++)
        ox[i] = -1;

    for (done = 0; done < frames; done++) {
        for (int i = 0; i < NSTARS; i++) {
            u32 lvl = (br[i] + done + (u32)i) % 3;
            u32 c = lvl == 2 ? fb_color(255, 255, 255)
                  : lvl == 1 ? fb_color(160, 160, 200)
                             : fb_color(60, 60, 110);
            /* стереть прошлое место (только звезду, не весь кадр) */
            if (ox[i] >= 0) {
                fb_putpixel((u32)ox[i], (u32)oy[i], sky);
                if (oy[i] > 0)
                    fb_putpixel((u32)ox[i], (u32)(oy[i] - 1), sky);
                if (oy[i] + 1 < (s32)h)
                    fb_putpixel((u32)ox[i], (u32)(oy[i] + 1), sky);
                if (ox[i] > 0)
                    fb_putpixel((u32)(ox[i] - 1), (u32)oy[i], sky);
                if (ox[i] + 1 < (s32)w)
                    fb_putpixel((u32)(ox[i] + 1), (u32)oy[i], sky);
            }
            /* звёзды падают вниз */
            sy[i] = (u16)(sy[i] + 1 + (br[i] & 1));
            if (sy[i] >= h) {
                sy[i] = 0;
                sx[i] = (u16)((sx[i] * 7 + 13 + (u32)i) % w);
            }
            ox[i] = (s16)sx[i];
            oy[i] = (s16)sy[i];
            fb_putpixel(sx[i], sy[i], c);
            if (lvl == 2) {                 /* яркие — крестом */
                if (sx[i] > 0)        fb_putpixel(sx[i] - 1, sy[i], c);
                if (sx[i] + 1 < w)    fb_putpixel(sx[i] + 1, sy[i], c);
                if (sy[i] > 0)        fb_putpixel(sx[i], sy[i] - 1, c);
                if (sy[i] + 1 < h)    fb_putpixel(sx[i], sy[i] + 1, c);
            }
        }
        if (kbd_trykey(&k))
            break;
        pit_sleep_ms(20);
    }
    console_clear();
    kprintf("звёзды: %u кадров, %u звёзд — небо собралось\n",
            done, (u32)NSTARS);
}

/* ---------------- главное меню ---------------- */

static void run_line(char *s);

static const struct { const char *title; const char *cmd; } menu_items[] = {
    { "Информация о системе", "инфо" },
    { "Память и карты",       "память" },
    { "Тест железа",          "тест" },
    { "Таймер и аптайм",      "таймер" },
    { "Раскладка EN/RU",      "раскладка" },
    { "Мышь: живой пиксель",  "мышь" },
    { "Прогон int 0x80",      "вызов" },
    { "Вики-браузер (wiki)",  "пуск wiki.ocp" },
    { "Перезагрузка ПК",      "перезагруз" },
    { "Стоп (HLT)",           "стоп" },
};
#define MENU_N ((int)(sizeof(menu_items) / sizeof(menu_items[0])))

static void cmd_menu(void)
{
    int sel = 0;
    s32 last_my = 0, step_acc = 0;
    bool first = true, need_draw = true;
    struct mouse_state m;

    mouse_show();
    for (;;) {
        if (need_draw) {
            mouse_hide();
            console_clear();
            console_set_color(C_LIGHT_CYAN, C_BLACK);
            kprintf("Меню OC — выбор куда идти\n");
            console_set_color(C_DARK_GRAY, C_BLACK);
            kprintf("========================================\n");
            for (int i = 0; i < MENU_N; i++) {
                if (i == sel) {
                    console_set_color(C_YELLOW, C_BLACK);
                    kprintf("  > %s\n", menu_items[i].title);
                } else {
                    console_set_color(C_LIGHT_GRAY, C_BLACK);
                    kprintf("    %s\n", menu_items[i].title);
                }
            }
            console_set_color(C_DARK_GRAY, C_BLACK);
            kprintf("========================================\n");
            kprintf("мышь: движение — выбор, клик — запуск\n");
            kprintf("клавиши: стрелки/цифры 1-9 — выбор, Enter — запуск, Esc — выход\n");
            console_set_color(C_LIGHT_GRAY, C_BLACK);
            mouse_show();
            need_draw = false;
        }

        /* мышь: 24 px по вертикали — шаг выбора; клик ЛКМ — запуск */
        mouse_get(&m);
        if (first) {
            last_my = m.y;
            first = false;
        }
        if (m.y != last_my) {
            step_acc += m.y - last_my;
            last_my = m.y;
            while (step_acc >= 24) {
                sel = (sel + 1) % MENU_N;
                step_acc -= 24;
                need_draw = true;
            }
            while (step_acc <= -24) {
                sel = (sel + MENU_N - 1) % MENU_N;
                step_acc += 24;
                need_draw = true;
            }
        }
        bool click = mouse_take_click();   /* край ловится в прерывании */

        u16 k = 0;
        if (click) {
            k = '\n';
        } else if (!kbd_trykey(&k)) {
            pit_sleep_ms(15);
            continue;
        }

        if (k == KEY_LAYOUT) {
            continue;                   /* мигнули — перерисуем */
        } else if (k == KEY_UP) {
            sel = (sel + MENU_N - 1) % MENU_N;
            need_draw = true;
        } else if (k == KEY_DOWN) {
            sel = (sel + 1) % MENU_N;
            need_draw = true;
        } else if (k == '\n' || (k >= '1' && k <= '9')) {
            if (k != '\n') {
                sel = (int)(k - '1');
                if (sel >= MENU_N)
                    continue;
            }
            mouse_hide();
            kprintf("\n[меню] %s\n", menu_items[sel].title);
            char buf[LINE_MAX];
            strncpy(buf, menu_items[sel].cmd, LINE_MAX - 1);
            buf[LINE_MAX - 1] = '\0';
            run_line(buf);
            kprintf("(Enter — в меню, Esc — выход)\n");
            if (kbd_getkey() != '\n')
                break;
            need_draw = true;
        } else if (k == KEY_ESC) {
            break;
        }
    }
    mouse_hide();
    kprintf("меню закрыто\n");
}

/* ---------------- разбор и цикл ---------------- */

static void run_line(char *s)
{
    /* токенизация */
    char *argv[ARGV_MAX];
    int argc = 0;
    while (*s && argc < ARGV_MAX) {
        while (*s == ' ')
            *s++ = '\0';
        if (!*s)
            break;
        argv[argc++] = s;
        while (*s && *s != ' ')
            s++;
    }
    if (argc == 0)
        return;

    const char *cmd = argv[0];
    const char *arg = argc > 1 ? argv[1] : "";

#define IS(a, b) (strcmp(cmd, a) == 0 || strcmp(cmd, b) == 0)

    if (IS("help", "помощь"))          cmd_help();
    else if (IS("clear", "очистить"))  console_clear();
    else if (IS("info", "инфо"))       cmd_info();
    else if (IS("mem", "память"))      cmd_mem();
    else if (IS("mmap", "карты"))      cmd_mmap();
    else if (IS("ticks", "таймер"))    cmd_ticks();
    else if (IS("sleep", "сон"))       cmd_sleep(arg);
    else if (IS("ktest", "тест"))      cmd_ktest();
    else if (IS("color", "цвет"))      cmd_color(arg);
    else if (IS("fill", "залить"))     cmd_fill(arg);
    else if (IS("mouse", "мышь"))      mouse_poll_demo();
    else if (IS("beep", "писк"))        cmd_beep();
    else if (IS("song", "мелодия"))     cmd_song();
    else if (IS("stars", "звёзды"))     cmd_stars(arg);
    else if (IS("layout", "раскладка") || strcmp(cmd, "lang") == 0 ||
             strcmp(cmd, "hfcrkflrf") == 0 ||  /* «раскладка» пальцами на EN */
             strcmp(cmd, "дфнщге") == 0 ||     /* «layout» пальцами на RU */
             strcmp(cmd, "дфтп") == 0) {       /* «lang» пальцами на RU */
        kbd_set_layout(kbd_layout() == KBD_EN ? KBD_RU : KBD_EN);
        kprintf("раскладка: %s\n", kbd_layout_name(kbd_layout()));
    } else if (strcmp(cmd, "ru") == 0 || strcmp(cmd, "RU") == 0) {
        kbd_set_layout(KBD_RU);
        kprintf("раскладка: RU\n");
    } else if (strcmp(cmd, "en") == 0 || strcmp(cmd, "EN") == 0) {
        kbd_set_layout(KBD_EN);
        kprintf("раскладка: EN\n");
    } else if (IS("menu", "меню") || strcmp(cmd, "менг") == 0 ||
               strcmp(cmd, "vty.") == 0) {     /* «меню»/«menu» кривыми руками */
        cmd_menu();
    } else if (IS("sysdemo", "вызов")) cmd_sysdemo();
    else if (IS("desktop", "рабстол") || IS("gui", "гуй")) gui_run();
    else if (IS("reboot", "перезагруз")) cmd_reboot();
    else if (IS("halt", "стоп")) {
        kprintf("останов. HLT.\n");
        cli();
        for (;;)
            hlt();
    } else if (IS("about", "об"))      cmd_about();
    else if (IS("ls", "файлы"))        cmd_files();
    else if (IS("programs", "программы")) cmd_progs();
    else if (IS("cat", "тип"))         cmd_cat(arg);
    else if (IS("run", "пуск"))        cmd_run(arg);
    else if (cmd_autorun(cmd))
        ;                               /* имя файла — программа запустилась */
    else
        kprintf("неизвестная команда: %s (help — справка)\n", cmd);
#undef IS
}

void shell_run(const oc_boot_info_t *bi)
{
    bootinfo = bi;
    fs_init();                          /* встроенные файлы и программы */
    line_len = 0;
    line[0] = '\0';
    hist_count = hist_browse = 0;

    speaker_jingle();       /* до-ми-соль-до: система ожила! */
    kprintf("\nДобро пожаловать в OC! help — справка, меню — главное меню.\n");
    kprintf("Раскладка EN/RU: Ctrl+Space или Alt+Shift (видно подтверждение).\n");
    kprintf("Не переключается? Наберите просто: layout  (или ru / en)\n");
    redraw_prompt();

    for (;;) {
        u16 k = kbd_getkey();
        if (k == '\n') {
            console_putc('\n');
            line[line_len] = '\0';
            history_add(line);
            run_line(line);
            line_len = 0;
            line[0] = '\0';
            redraw_prompt();
        } else if (k == '\b') {
            line_backspace();
        } else if (k == KEY_UP) {
            history_show(-1);
        } else if (k == KEY_DOWN) {
            history_show(+1);
        } else if (k == KEY_ESC) {
            while (line_len)
                line_backspace();
        } else if (k == KEY_LAYOUT) {
            /* драйвер уже переключил — показываем подтверждение */
            kprintf("\nраскладка: %s\n", kbd_layout_name(kbd_layout()));
            redraw_prompt();
            redraw_line();
        } else if (k >= 0x20 && k < 0x1100) {
            /* Unicode -> UTF-8 в строку */
            char buf[4];
            u32 n = 0;
            if (k < 0x80) {
                buf[0] = (char)k;
                n = 1;
            } else if (k < 0x800) {
                buf[0] = (char)(0xC0 | (k >> 6));
                buf[1] = (char)(0x80 | (k & 0x3F));
                n = 2;
            } else {
                buf[0] = (char)(0xE0 | (k >> 12));
                buf[1] = (char)(0x80 | ((k >> 6) & 0x3F));
                buf[2] = (char)(0x80 | (k & 0x3F));
                n = 3;
            }
            for (u32 i = 0; i < n; i++)
                line_insert(buf[i]);
        }
    }
}
