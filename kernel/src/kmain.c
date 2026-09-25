/*
 * OC OS — точка входа ядра.
 */
#include "types.h"
#include "io.h"
#include "boot.h"
#include "layout.h"
#include "console.h"
#include "serial.h"
#include "kprintf.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "mouse.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "shell.h"
#include "gui.h"
#include "panic.h"

extern char _kernel_start[], _kernel_end[];

static void banner(void)
{
    enum oc_color old_fg, old_bg;
    console_get_color(&old_fg, &old_bg);

    console_set_color(C_LIGHT_CYAN, C_BLACK);
    kprintf("\n");
    kprintf("   ____   ____   \n");
    kprintf("  / __ \\ / ___|  \n");
    kprintf(" | |  | | |      \n");
    kprintf(" | |  | | |___   \n");
    kprintf(" | |  | |\\____|  ядро OC 0.1\n");
    kprintf("  \\____/          x86_64 Long Mode\n\n");
    console_set_color(old_fg, old_bg);
}

void kmain(oc_boot_info_t *bi)
{
    serial_init();
    if (!bi || bi->magic != BOOTINFO_MAGIC)
        panic("нет корректной структуры bootinfo");

    console_init(bi);
    banner();

    kprintf("[1/8] GDT + TSS... ");
    gdt_init((void *)(uintptr_t)(TEMP_STACK));
    kprintf("ok\n");

    kprintf("[2/8] IDT... ");
    idt_init();
    kprintf("ok\n");

    kprintf("[3/8] PIC... ");
    pic_init();
    kprintf("ok\n");

    kprintf("[4/8] физ. память (E820: %u записей)... ", bi->e820_count);
    pmm_init(bi, (u64)(uintptr_t)_kernel_start, (u64)(uintptr_t)_kernel_end);
    kprintf("ok: %llu KiB свободно\n",
            (unsigned long long)(pmm_free_frames() * PMM_FRAME_SIZE / 1024));

    kprintf("[5/8] страничная адресация... ");
    vmm_init();
    kprintf("ok (CR3=%lx)\n", vmm_root());

    kprintf("[6/8] куча ядра... ");
    heap_init();
    kprintf("ok (база 0x%llx)\n", (unsigned long long)HEAP_BASE);

    kprintf("[7/8] таймер %u Гц... ", PIT_HZ);
    pit_init();
    kprintf("ok\n");

    kprintf("[8/8] клавиатура и мышь... ");
    kbd_init();
    mouse_init();
    kprintf("ok (раскладка %s)\n", kbd_layout_name(kbd_layout()));

    sti();
    kprintf("\nИнициализация завершена за %llu мс.\n",
            (unsigned long long)pit_uptime_ms());

    gui_run();          /* сразу рабочий стол — как в Windows! */
    shell_run(bi);      /* выход из стола (Esc) — обычный шелл */
}
