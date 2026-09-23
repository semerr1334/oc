#include "shell.h"
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
        "  раскладка/layout     переключить EN/RU (Ctrl+Space)\n"
        "  вызов/sysdemo        демонстрация int 0x80\n"
        "  перезагруз/reboot    перезагрузка ПК\n"
        "  стоп/halt            остановка\n"
        "  об/about             об этой системе\n");
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
    else if (IS("layout", "раскладка")) {
        kbd_set_layout(kbd_layout() == KBD_EN ? KBD_RU : KBD_EN);
        kprintf("раскладка: %s\n", kbd_layout_name(kbd_layout()));
    } else if (IS("sysdemo", "вызов")) cmd_sysdemo();
    else if (IS("reboot", "перезагруз")) cmd_reboot();
    else if (IS("halt", "стоп")) {
        kprintf("останов. HLT.\n");
        cli();
        for (;;)
            hlt();
    } else if (IS("about", "об"))      cmd_about();
    else
        kprintf("неизвестная команда: %s (help — справка)\n", cmd);
#undef IS
}

void shell_run(const oc_boot_info_t *bi)
{
    bootinfo = bi;
    line_len = 0;
    line[0] = '\0';
    hist_count = hist_browse = 0;

    kprintf("\nДобро пожаловать в OC! help — справка.\n");
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
