#include "mouse.h"
#include "idt.h"
#include "io.h"
#include "fb.h"
#include "console.h"
#include "keyboard.h"
#include "kprintf.h"
#include "pit.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64

#define CURSOR_W 11
#define CURSOR_H 16

/* курсор-стрелка: битовая масца (1 = цвет рамки, 2 = заливка) */
static const u8 cursor_mask[CURSOR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFC, 0xD8, 0x88, 0x08, 0x0C, 0x00, 0x00, 0x00,
};
static const u8 cursor_fill[CURSOR_H] = {
    0x00, 0x40, 0x60, 0x70, 0x78, 0x7C, 0x7E, 0x7F,
    0x7C, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static volatile struct mouse_state st;
static volatile u8 cycle, pkt[3];
static bool visible;
static u32 save_buf[CURSOR_W * CURSOR_H * 2];
static s32 save_x, save_y;

static void ps2_wait_write(void)
{
    while (inb(PS2_STAT) & 2)
        cpu_pause();
}

static void ps2_wait_read(void)
{
    while ((inb(PS2_STAT) & 1) == 0)
        cpu_pause();
}

static void ps2_write_cmd(u8 cmd)
{
    ps2_wait_write();
    outb(PS2_STAT, cmd);
}

static void ps2_write_data(u8 data)
{
    ps2_wait_write();
    outb(PS2_DATA, data);
}

static u8 ps2_read(void)
{
    ps2_wait_read();
    return inb(PS2_DATA);
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    u8 b = inb(PS2_DATA);
    pkt[cycle++] = b;
    if (cycle == 1 && (b & 0x08) == 0)
        cycle = 0;                  /* рассинхрон — начинаем заново */
    if (cycle == 3) {
        cycle = 0;
        u8 flags = pkt[0];
        s32 dx = (s32)(s8)pkt[1];
        s32 dy = (s32)(s8)pkt[2];
        st.x += dx;
        st.y -= dy;                 /* ось Y вверх */
        if (st.x < 0) st.x = 0;
        if (st.y < 0) st.y = 0;
        if (fb_ready()) {
            if ((s32)fb_width_px() - CURSOR_W < st.x)
                st.x = (s32)fb_width_px() - CURSOR_W;
            if ((s32)fb_height_px() - CURSOR_H < st.y)
                st.y = (s32)fb_height_px() - CURSOR_H;
        }
        st.buttons = flags & 7;
    }
}

static void draw_cursor(void)
{
    if (!fb_ready())
        return;
    save_x = st.x;
    save_y = st.y;
    fb_save_rect((u32)save_x, (u32)save_y, CURSOR_W + 4, CURSOR_H + 4, save_buf);
    u32 black = fb_color(0, 0, 0), white = fb_color(255, 255, 255);
    for (u32 y = 0; y < CURSOR_H; y++) {
        for (u32 x = 0; x < 8; x++) {
            u8 bit = (u8)(0x80 >> x);
            if (cursor_mask[y] & bit)
                fb_putpixel((u32)save_x + x, (u32)save_y + y, black);
            else if (cursor_fill[y] & bit)
                fb_putpixel((u32)save_x + x, (u32)save_y + y, white);
        }
    }
    visible = true;
}

static void erase_cursor(void)
{
    if (!visible || !fb_ready())
        return;
    fb_restore_rect((u32)save_x, (u32)save_y, CURSOR_W + 4, CURSOR_H + 4, save_buf);
    visible = false;
}

void mouse_init(void)
{
    st.x = (s32)(fb_ready() ? fb_width_px() / 2 : 40);
    st.y = (s32)(fb_ready() ? fb_height_px() / 2 : 12);
    st.buttons = 0;
    cycle = 0;
    visible = false;

    /* включаем aux-порт */
    ps2_write_cmd(0xA8);
    /* разрешаем прерывания от aux в контроллере (командный байт) */
    ps2_write_cmd(0x20);
    u8 cb = ps2_read();
    cb |= 0x02;                     /* IRQ12 */
    cb &= (u8)~0x20;                /* включить клок aux */
    ps2_write_cmd(0x60);
    ps2_write_data(cb);

    /* дефолты мыши, включаем поток */
    ps2_write_cmd(0xD4); ps2_write_data(0xF6); ps2_read();   /* defaults */
    ps2_write_cmd(0xD4); ps2_write_data(0xF4); ps2_read();   /* enable */

    irq_set_handler(12, mouse_irq);
    irq_set_mask(12, false);
}

void mouse_get(struct mouse_state *out)
{
    out->x = st.x;
    out->y = st.y;
    out->buttons = st.buttons;
}

void mouse_show(void)
{
    if (!visible)
        draw_cursor();
}

void mouse_hide(void)
{
    erase_cursor();
}

void mouse_poll_demo(void)
{
    if (!fb_ready()) {
        kprintf("мышь: демо доступно только в графическом режиме\n");
        return;
    }
    kprintf("Демо мыши: двигайте курсор, любая клавиша — выход.\n");
    mouse_show();
    u16 key;
    for (;;) {
        erase_cursor();
        draw_cursor();
        if (kbd_trykey(&key))
            break;
        pit_sleep_ms(15);
    }
    mouse_hide();
    kprintf("Демо мыши завершено.\n");
}
