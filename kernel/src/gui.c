/*
 * OC OS — Рабочий стол: обои, иконки, окна с заголовками, меню «Пуск»,
 * часы/калькулятор/блокнот в окнах. Мышь + клавиатура.
 */
#include "types.h"
#include "string.h"
#include "fb.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "kprintf.h"
#include "console.h"
#include "gui.h"

#define WIN_MAX   4
#define TITLE_H   22
#define TBAR_H    34
#define NAPPS     4

/* ---------- цвета ---------- */
#define C_TITLE   fb_color(40, 90, 180)
#define C_TITLE_A fb_color(60, 120, 220)
#define C_FRAME   fb_color(20, 40, 80)
#define C_BODY    fb_color(235, 235, 235)
#define C_INK     fb_color(20, 20, 30)
#define C_CLOSE   fb_color(200, 60, 50)
#define C_CLOSE_H fb_color(230, 90, 70)
#define C_BTN     fb_color(205, 210, 220)
#define C_BTN_H   fb_color(185, 195, 215)
#define C_TBAR    fb_color(35, 45, 70)
#define C_START   fb_color(50, 140, 80)
#define C_SEL     fb_color(255, 210, 60)
#define C_WHITE   fb_color(255, 255, 255)
#define C_GRAY    fb_color(120, 125, 135)

/* ---------- окно ---------- */
struct gui_win {
    int used, app;
    s32 x, y, w, h;
    char title[24];
    /* калькулятор */
    char expr[48];
    int xlen;
    long shown;
    int fresh;
    /* блокнот */
    char text[2048];
    int tlen;
};

static struct gui_win wins[WIN_MAX];
static int nwins;
static int desk_sel;
static int menu_open, menu_sel;
static int drag = -1;
static s32 drag_dx, drag_dy;
static int need_redraw;

static u32 cw, ch;   /* ячейка текста, пиксели */

/* ---------- синус (6° шаги, sin*256) ---------- */
static const short S256[60] = {
      0,  27,  53,  79, 105, 128, 150, 171, 190, 207,
    222, 234, 243, 251, 255, 256, 255, 251, 243, 234,
    222, 207, 190, 171, 150, 128, 105,  79,  53,  27,
      0, -27, -53, -79,-105,-128,-150,-171,-190,-207,
   -222,-234,-243,-251,-255,-256,-255,-251,-243,-234,
   -222,-207,-190,-171,-150,-128,-105, -79, -53, -27,
};
static int fsin(int a) { return S256[((a % 60) + 60) % 60]; }
static int fcos(int a) { return fsin(a + 15); }

/* ---------- мелочи ---------- */
static int utf8_len(const char *s)
{
    int n = 0;
    while (*s) {
        size_t adv;
        font_utf8_decode(s, &adv);
        s += adv;
        n++;
    }
    return n;
}

static void put_c(u32 x, u32 y, u32 cp, u32 fg, u32 bg)
{
    u8 code = font_encode(cp);
    fb_draw_char(x, y, code ? code : '?', fg, bg);
}

static void draw_c(u32 x, u32 y, u32 w, u32 h, u32 c) { fb_fill_rect(x, y, w, h, c); }

static void str_c(u32 x, u32 y, const char *s, u32 fg, u32 bg)
{
    fb_draw_string(x, y, s, fg, bg);
}

/* центрирование по вертикали внутри rect высотой hh */
static void str_center2(u32 rx, u32 ry, u32 rw, u32 hh, const char *s, u32 fg, u32 bg)
{
    int n = utf8_len(s);
    s32 x = (s32)rx + ((s32)rw - n * (s32)cw) / 2;
    s32 y = (s32)ry + ((s32)hh - (s32)ch) / 2;
    if (x < (s32)rx) x = (s32)rx;
    if (y < (s32)ry) y = (s32)ry;
    str_c((u32)x, (u32)y, s, fg, bg);
}

static void putnum_c(u32 x, u32 y, long v, u32 fg, u32 bg)
{
    char b[24];
    int i = 22;
    b[23] = 0;
    unsigned long u = (unsigned long)(v < 0 ? -v : v);
    do { b[i--] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) b[i--] = '-';
    str_c(x, y, b + i + 1, fg, bg);
}

/* ---------- приложения ---------- */
static void app_clock_draw(struct gui_win *w);
static void app_calc_draw(struct gui_win *w);
static void app_calc_click(struct gui_win *w, s32 lx, s32 ly);
static void app_calc_key(struct gui_win *w, u16 k);
static void app_note_draw(struct gui_win *w);
static void app_note_click(struct gui_win *w, s32 lx, s32 ly);
static void app_note_key(struct gui_win *w, u16 k);
static void app_about_draw(struct gui_win *w);

struct gui_app {
    const char *name, *tile;
    s32 w, h;
    void (*draw)(struct gui_win *);
    void (*click)(struct gui_win *, s32, s32);
    void (*key)(struct gui_win *, u16);
};

static const struct gui_app apps[NAPPS] = {
    { "Калькулятор", "К", 236, 272, app_calc_draw, app_calc_click, app_calc_key },
    { "Часы",        "Ч", 252, 268, app_clock_draw, 0, 0 },
    { "Блокнот",     "Б", 380, 280, app_note_draw, app_note_click, app_note_key },
    { "О системе",   "О", 360, 210, app_about_draw, 0, 0 },
};

/* ---------- геометрия ---------- */
static void content_rect(struct gui_win *w, s32 *x, s32 *y, s32 *rw, s32 *rh)
{
    *x = w->x + 3;
    *y = w->y + TITLE_H + 1;
    *rw = w->w - 6;
    *rh = w->h - TITLE_H - 4;
}

static int in_box(s32 x, s32 y, s32 bx, s32 by, s32 bw, s32 bh)
{
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* ---------- обои ---------- */
static void draw_wall(void)
{
    u32 W = fb_width_px(), H = fb_height_px();
    for (u32 i = 0; i < 24; i++) {
        u8 r = (u8)(12 + i), g = (u8)(28 + i * 2), b = (u8)(64 + i * 3);
        fb_fill_rect(0, i * (H / 24), W, H / 24 + 1, fb_color(r, g, b));
    }
    str_c(W - 20 * cw, H - TBAR_H - ch - 6, "Рабочий стол OC", fb_color(150, 180, 255),
          fb_color(16, 34, 72));
}

/* ---------- иконки ---------- */
#define ICON_X   24
#define ICON_Y   28
#define ICON_SZ  48
#define ICON_DY  92

static void icon_rect(int i, s32 *x, s32 *y, s32 *w, s32 *h)
{
    *x = ICON_X;
    *y = ICON_Y + i * ICON_DY;
    *w = 104;
    *h = ICON_SZ + 26;
}

static void draw_icons(void)
{
    for (int i = 0; i < NAPPS; i++) {
        s32 x, y, w, h;
        icon_rect(i, &x, &y, &w, &h);
        u32 tile = (i == desk_sel) ? C_SEL : C_BTN;
        draw_c(x + (w - ICON_SZ) / 2, y, ICON_SZ, ICON_SZ, tile);
        fb_rect(x + (w - ICON_SZ) / 2, y, ICON_SZ, ICON_SZ, C_FRAME);
        str_center2(x + (w - ICON_SZ) / 2, y, ICON_SZ, ICON_SZ, apps[i].tile, C_INK, tile);
        str_center2(x, y + ICON_SZ + 2, w, 22, apps[i].name,
                    (i == desk_sel) ? C_SEL : C_WHITE, fb_color(16, 34, 72));
    }
}

/* ---------- таскбар ---------- */
static void draw_taskbar(void)
{
    u32 W = fb_width_px(), H = fb_height_px();
    draw_c(0, H - TBAR_H, W, TBAR_H, C_TBAR);
    draw_c(4, H - TBAR_H + 4, 76, TBAR_H - 8, C_START);
    str_center2(4, H - TBAR_H + 4, 76, TBAR_H - 8, "Пуск", C_WHITE, C_START);
    /* кнопки окон */
    s32 bx = 90;
    for (int i = 0; i < nwins; i++) {
        draw_c(bx, H - TBAR_H + 6, 120, TBAR_H - 12, C_BTN);
        str_center2(bx, H - TBAR_H + 6, 120, TBAR_H - 12, wins[i].title, C_INK, C_BTN);
        bx += 128;
    }
    /* часы */
    u64 ms = pit_uptime_ms();
    u32 s = (u32)(ms / 1000);
    char t[16];
    t[0] = (char)('0' + (s / 3600) % 10);
    t[1] = (char)('0' + (s / 360) % 6);
    t[2] = ':';
    t[3] = (char)('0' + (s / 60) % 10);
    t[4] = (char)('0' + (s / 10) % 6);
    t[5] = ':';
    t[6] = (char)('0' + s % 10);
    t[7] = (char)('0' + (u32)(ms / 100) % 10);
    t[8] = 0;
    draw_c(W - 10 * cw - 12, H - TBAR_H + 6, 10 * cw + 4, TBAR_H - 12, C_BODY);
    str_c(W - 10 * cw - 8, H - TBAR_H + (TBAR_H - ch) / 2, t, C_INK, C_BODY);
}

/* ---------- меню «Пуск» ---------- */
#define MENU_N 5
static const char *menu_items[MENU_N] = {
    "Калькулятор", "Часы", "Блокнот", "О системе", "Выход в шелл",
};

static void draw_menu(void)
{
    u32 H = fb_height_px();
    u32 mw = 200, mh = MENU_N * (ch + 12) + 10;
    u32 x = 6, y = H - TBAR_H - mh;
    draw_c(x, y, mw, mh, C_BODY);
    fb_rect(x, y, mw, mh, C_FRAME);
    for (int i = 0; i < MENU_N; i++) {
        u32 iy = y + 5 + i * (ch + 12);
        u32 bg = (i == menu_sel) ? C_TITLE_A : C_BODY;
        draw_c(x + 3, iy, mw - 6, ch + 8, bg);
        str_c(x + 12, iy + 4, menu_items[i], (i == menu_sel) ? C_WHITE : C_INK, bg);
    }
}

/* ---------- окна ---------- */
static void win_draw(struct gui_win *w)
{
    draw_c(w->x, w->y, w->w, w->h, C_FRAME);
    draw_c(w->x + 2, w->y + 2, w->w - 4, TITLE_H - 2, C_TITLE);
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    draw_c(w->x + 2, w->y + TITLE_H, w->w - 4, w->h - TITLE_H - 2, C_BODY);
    str_c(w->x + 8, w->y + (TITLE_H - ch) / 2 + 1, w->title, C_WHITE, C_TITLE);
    /* кнопка [X] */
    draw_c(w->x + w->w - TITLE_H + 2, w->y + 4, TITLE_H - 8, TITLE_H - 12, C_CLOSE);
    str_c(w->x + w->w - TITLE_H + 2 + (TITLE_H - 8 - (s32)cw) / 2,
          w->y + 4 + (TITLE_H - 12 - (s32)ch) / 2, "X", C_WHITE, C_CLOSE);
    if (apps[w->app].draw)
        apps[w->app].draw(w);
}

static int open_app(int idx)
{
    for (int i = 0; i < nwins; i++) {
        if (wins[i].app == idx && wins[i].used) {
            struct gui_win t = wins[i];
            wins[i] = wins[nwins - 1];
            wins[nwins - 1] = t;
            need_redraw = 1;
            return 0;
        }
    }
    if (nwins >= WIN_MAX)
        return 0;
    struct gui_win *w = &wins[nwins++];
    memset(w, 0, sizeof *w);
    w->used = 1;
    w->app = idx;
    w->w = apps[idx].w;
    w->h = apps[idx].h;
    w->x = 180 + (nwins - 1) * 36;
    w->y = 30 + (nwins - 1) * 30;
    if (w->x + w->w > (s32)fb_width_px() - 8)  w->x = (s32)fb_width_px() - w->w - 8;
    if (w->y + w->h > (s32)fb_height_px() - TBAR_H - 4) w->y = 8;
    strncpy(w->title, apps[idx].name, sizeof w->title - 1);
    w->fresh = 1;
    kprintf("[GUI] открыто: %s\n", apps[idx].name);
    need_redraw = 1;
    return 1;
}

static void close_top(void)
{
    if (!nwins)
        return;
    kprintf("[GUI] закрыто: %s\n", wins[nwins - 1].title);
    nwins--;
    need_redraw = 1;
}

/* ---------- часы ---------- */
static void app_clock_draw(struct gui_win *w)
{
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    s32 cx = x + rw / 2, cy = y + (rh - ch) / 2 - 4;
    s32 R = (rw < rh ? rw : rh) / 2 - 10;
    if (R < 30) R = 30;
    /* циферблат */
    for (int a = 0; a < 60; a++) {
        int big = (a % 5) == 0;
        s32 r1 = R - (big ? 8 : 3), r2 = R;
        s32 x1 = cx + (s32)((long)fcos(a) * r1 / 256);
        s32 y1 = cy + (s32)((long)fsin(a) * r1 / 256);
        s32 x2 = cx + (s32)((long)fcos(a) * r2 / 256);
        s32 y2 = cy + (s32)((long)fsin(a) * r2 / 256);
        /* маленький отрезок */
        int steps = big ? 4 : 2;
        for (int s = 0; s <= steps; s++)
            fb_putpixel(x1 + (x2 - x1) * s / steps, y1 + (y2 - y1) * s / steps,
                        big ? C_INK : C_GRAY);
    }
    u64 ms = pit_uptime_ms();
    int sec = (int)(ms / 1000) % 60;
    int min = (int)(ms / 60000) % 60;
    int hur = (int)(ms / 3600000) % 12;
    /* стрелки: часы, минуты, секунды */
    struct { int ang, len; u32 col; } hands[3] = {
        { hur * 5 + min / 12, R / 2, C_INK },
        { min, R * 3 / 4, C_INK },
        { sec, R - 6, fb_color(200, 60, 50) },
    };
    for (int i = 0; i < 3; i++) {
        s32 x2 = cx + (s32)((long)fcos(hands[i].ang) * hands[i].len / 256);
        s32 y2 = cy + (s32)((long)fsin(hands[i].ang) * hands[i].len / 256);
        int steps = hands[i].len > 4 ? hands[i].len : 4;
        for (int s = 0; s <= steps; s++)
            fb_putpixel(cx + (x2 - cx) * s / steps, cy + (y2 - cy) * s / steps, hands[i].col);
    }
    fb_putpixel(cx, cy, C_INK);
    /* цифры */
    char t[12];
    u32 s2 = (u32)(ms / 1000);
    t[0] = (char)('0' + (s2 / 3600) % 10);
    t[1] = (char)('0' + (s2 / 360) % 6);
    t[2] = ':';
    t[3] = (char)('0' + (s2 / 60) % 10);
    t[4] = (char)('0' + (s2 / 10) % 6);
    t[5] = ':';
    t[6] = (char)('0' + s2 % 10);
    t[7] = (char)('0' + (u32)(ms / 100) % 10);
    t[8] = 0;
    str_center2(x, y + rh - ch - 2, rw, ch, t, C_INK, C_BODY);
}

/* ---------- калькулятор (выражения с приоритетом) ---------- */
static const struct { const char *lb; char key; u8 gx, gy, gw; } cbtn[18] = {
    { "C", 'C', 0, 0, 1 }, { "<", '<', 1, 0, 1 }, { "/", '/', 2, 0, 1 }, { "*", '*', 3, 0, 1 },
    { "7", '7', 0, 1, 1 }, { "8", '8', 1, 1, 1 }, { "9", '9', 2, 1, 1 }, { "-", '-', 3, 1, 1 },
    { "4", '4', 0, 2, 1 }, { "5", '5', 1, 2, 1 }, { "6", '6', 2, 2, 1 }, { "+", '+', 3, 2, 1 },
    { "1", '1', 0, 3, 1 }, { "2", '2', 1, 3, 1 }, { "3", '3', 2, 3, 1 }, { "=", '=', 3, 3, 2 },
    { "0", '0', 0, 4, 2 }, { "00", 'd', 2, 4, 1 },
};

static const char *ep;
static long ev_term(void);
static long ev_factor(void);

static long ev_factor(void)
{
    long v = 0;
    while (*ep == ' ') ep++;
    if (*ep == '-') { ep++; return -ev_factor(); }
    if (*ep == '(') { ep++; v = ev_term(); if (*ep == ')') ep++; return v; }
    while (*ep >= '0' && *ep <= '9')
        v = v * 10 + (*ep++ - '0');
    return v;
}

static long ev_term(void)
{
    long v = ev_factor();
    while (*ep == '*' || *ep == '/') {
        char o = *ep++;
        long r = ev_factor();
        v = (o == '*') ? v * r : (r ? v / r : 0);
    }
    return v;
}

static long eval_expr(const char *s)
{
    long v;
    ep = s;
    v = ev_term();
    while (*ep == '+' || *ep == '-') {
        char o = *ep++;
        long r = ev_term();
        v = (o == '+') ? v + r : v - r;
    }
    return v;
}

static void calc_press(struct gui_win *w, char k)
{
    if (k >= '0' && k <= '9') {
        if (w->fresh) {
            w->xlen = 0;
            w->fresh = 0;
        }
        if (w->xlen < (int)sizeof w->expr - 1)
            w->expr[w->xlen++] = k;
    } else if (k == 'd') {
        if (!w->fresh && w->xlen < (int)sizeof w->expr - 2) {
            w->expr[w->xlen++] = '0';
            w->expr[w->xlen++] = '0';
        }
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (w->fresh)
            w->fresh = 0;
        if (w->xlen == 0) {
            if (k == '-')
                w->expr[w->xlen++] = '-';
        } else {
            char last = w->expr[w->xlen - 1];
            if (last == '+' || last == '-' || last == '*' || last == '/')
                w->xlen--;
            if (w->xlen < (int)sizeof w->expr - 1)
                w->expr[w->xlen++] = k;
        }
    } else if (k == '<') {
        if (w->xlen)
            w->xlen--;
    } else if (k == 'C') {
        w->xlen = 0;
        w->shown = 0;
        w->fresh = 1;
    } else if (k == '=') {
        w->expr[w->xlen] = 0;
        long v = eval_expr(w->expr);
        w->shown = v;
        kprintf("[GUI] калькулятор: = %ld\n", v);
        /* результат становится началом следующего выражения */
        char tmp[24];
        int i = 0;
        unsigned long u = (unsigned long)(v < 0 ? -v : v);
        do { tmp[i++] = (char)('0' + u % 10); u /= 10; } while (u);
        w->xlen = 0;
        if (v < 0 && w->xlen < (int)sizeof w->expr - 1)
            w->expr[w->xlen++] = '-';
        while (i && w->xlen < (int)sizeof w->expr - 1)
            w->expr[w->xlen++] = tmp[--i];
        w->fresh = 1;
    }
}

static void app_calc_draw(struct gui_win *w)
{
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    /* дисплей */
    draw_c(x + 2, y + 2, rw - 4, ch + 8, C_WHITE);
    fb_rect(x + 2, y + 2, rw - 4, ch + 8, C_FRAME);
    if (w->xlen) {
        w->expr[w->xlen] = 0;
        int n = (int)strlen(w->expr);
        u32 px = x + rw - 4 - (u32)n * cw;
        if (px < (u32)x + 6) px = x + 6;
        str_c(px, y + 6, w->expr, C_INK, C_WHITE);
    } else {
        putnum_c(x + rw - 4 - 12 * cw, y + 6, w->shown, C_INK, C_WHITE);
    }
    /* кнопки: сетка 4×5, «=» высотой 2 */
    s32 gy0 = y + ch + 16;
    s32 gw = (rw - 10) / 4, gh = (rh - ch - 20) / 5;
    for (int i = 0; i < 18; i++) {
        s32 bx = x + 3 + cbtn[i].gx * (gw + 2);
        s32 by = gy0 + cbtn[i].gy * (gh + 2);
        s32 bw = cbtn[i].gw * gw + (cbtn[i].gw - 1) * 2;
        s32 bh = (cbtn[i].key == '=') ? gh * 2 + 2 : gh;
        draw_c(bx, by, bw, bh, C_BTN);
        fb_rect(bx, by, bw, bh, C_GRAY);
        str_center2(bx, by, bw, bh, cbtn[i].lb, C_INK, C_BTN);
    }
}

static void app_calc_click(struct gui_win *w, s32 lx, s32 ly)
{
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    s32 gy0 = y + ch + 16;
    s32 gw = (rw - 10) / 4, gh = (rh - ch - 20) / 5;
    for (int i = 0; i < 18; i++) {
        s32 bx = x + 3 + cbtn[i].gx * (gw + 2);
        s32 by = gy0 + cbtn[i].gy * (gh + 2);
        s32 bw = cbtn[i].gw * gw + (cbtn[i].gw - 1) * 2;
        s32 bh = (cbtn[i].key == '=') ? gh * 2 + 2 : gh;
        if (in_box(lx, ly, bx, by, bw, bh)) {
            calc_press(w, cbtn[i].key);
            need_redraw = 1;
            return;
        }
    }
}

static void app_calc_key(struct gui_win *w, u16 k)
{
    if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*' || k == '/')
        calc_press(w, (char)k);
    else if (k == '=' || k == '\r' || k == '\n')
        calc_press(w, '=');
    else if (k == 0x08 || k == 0x7F)
        calc_press(w, '<');
    else if (k == 'c' || k == 'C')
        calc_press(w, 'C');
    need_redraw = 1;
}

/* ---------- блокнот ---------- */
static void app_note_draw(struct gui_win *w)
{
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    draw_c(x, y, rw, rh, C_WHITE);
    fb_rect(x, y, rw, rh, C_GRAY);
    int cols = (int)(rw / cw) - 1, rows = (int)(rh / ch) - 1;
    if (cols < 4) cols = 4;
    if (rows < 2) rows = 2;
    /* сколько строк с учётом переноса */
    int lines = 1, col = 0;
    for (int i = 0; i < w->tlen;) {
        size_t adv;
        u32 cp = font_utf8_decode(w->text + i, &adv);
        i += (int)adv;
        if (cp == '\n' || col >= cols) {
            lines++;
            col = 0;
        } else
            col++;
    }
    int skip = lines > rows ? lines - rows : 0;
    int li = 0, cl = 0, cx2 = 0;
    u32 px = x + 6, py = y + 5;
    int caret_li = -1, caret_cx = 0;
    for (int i = 0; i < w->tlen;) {
        size_t adv;
        u32 cp = font_utf8_decode(w->text + i, &adv);
        i += (int)adv;
        if (cp == '\n' || cx2 >= cols) {
            li++;
            cx2 = 0;
            if (li >= skip && li - skip < rows)
                py += ch;
            continue;
        }
        if (li >= skip) {
            put_c(px + cx2 * cw, py, cp, C_INK, C_WHITE);
        }
        cx2++;
    }
    /* каретка */
    (void)cl;
    caret_li = li;
    caret_cx = cx2;
    if (caret_li >= skip) {
        u32 cyp = y + 5 + (u32)(caret_li - skip) * ch;
        u32 cxp = x + 6 + (u32)caret_cx * cw;
        if (cyp + ch < y + rh) {
            draw_c(cxp, cyp, 2, ch, C_TITLE);
        }
    }
}

static void app_note_click(struct gui_win *w, s32 lx, s32 ly)
{
    (void)w; (void)lx; (void)ly;
}

static void app_note_key(struct gui_win *w, u16 k)
{
    if (k == '\r' || k == '\n') {
        if (w->tlen < (int)sizeof w->text - 1)
            w->text[w->tlen++] = '\n';
    } else if (k == 0x08 || k == 0x7F) {
        if (w->tlen) {
            w->tlen--;
            while (w->tlen && ((unsigned char)w->text[w->tlen] & 0xC0) == 0x80)
                w->tlen--;
        }
    } else if (k >= 0x20 && k < 0x1100) {
        /* кодовая точка -> UTF-8 */
        if (k < 0x80) {
            if (w->tlen < (int)sizeof w->text - 1)
                w->text[w->tlen++] = (char)k;
        } else {
            if (w->tlen < (int)sizeof w->text - 2) {
                w->text[w->tlen++] = (char)(0xC0 | (k >> 6));
                w->text[w->tlen++] = (char)(0x80 | (k & 0x3F));
            }
        }
        kprintf("[GUI] блокнот: символов %d\n", w->tlen);
    } else
        return;
    need_redraw = 1;
}

/* ---------- о системе ---------- */
static void app_about_draw(struct gui_win *w)
{
    s32 x, y, rw, rh;
    content_rect(w, &x, &y, &rw, &rh);
    str_c(x + 10, y + 10, "OC v0.3 — Рабочий стол", C_INK, C_BODY);
    str_c(x + 10, y + 10 + ch + 4, "Ядро: x86_64 long mode, с нуля", C_INK, C_BODY);
    int wp = (int)fb_width_px(), hp = (int)fb_height_px();
    str_c(x + 10, y + 10 + (ch + 4) * 2, "Экран:", C_INK, C_BODY);
    putnum_c(x + 10 + 8 * cw, y + 10 + (ch + 4) * 2, wp, C_INK, C_BODY);
    str_c(x + 10 + 8 * cw + 5 * cw, y + 10 + (ch + 4) * 2, "x", C_INK, C_BODY);
    putnum_c(x + 10 + 8 * cw + 6 * cw, y + 10 + (ch + 4) * 2, hp, C_INK, C_BODY);
    str_c(x + 10, y + 10 + (ch + 4) * 3, "Программы: calc game snake piano wiki hello",
          C_INK, C_BODY);
    str_c(x + 10, y + 10 + (ch + 4) * 4, "Esc или [X] — закрыть окно", C_GRAY, C_BODY);
}

/* ---------- отрисовка всего ---------- */
static void draw_all(void)
{
    mouse_hide();
    draw_wall();
    draw_icons();
    for (int i = 0; i < nwins; i++)
        win_draw(&wins[i]);
    draw_taskbar();
    if (menu_open)
        draw_menu();
    mouse_show();
    need_redraw = 0;
}

/* ---------- клики ---------- */
static void handle_click(s32 mx, s32 my)
{
    u32 W = fb_width_px(), H = fb_height_px();
    if (menu_open) {
        u32 mw = 200, mh = MENU_N * (ch + 12) + 10;
        u32 x = 6, y = H - TBAR_H - mh;
        for (int i = 0; i < MENU_N; i++) {
            u32 iy = y + 5 + i * (ch + 12);
            if (in_box(mx, my, (s32)x + 3, (s32)iy, (s32)mw - 6, (s32)ch + 8)) {
                menu_open = 0;
                need_redraw = 1;
                kprintf("[GUI] меню Пуск: %s\n", menu_items[i]);
                if (i < 4)
                    open_app(i);
                else {
                    kprintf("[GUI] выход в шелл\n");
                    nwins = 0;
                }
                return;
            }
        }
        menu_open = 0;
        need_redraw = 1;
        return;
    }
    /* окна: верхний первый */
    for (int i = nwins - 1; i >= 0; i--) {
        struct gui_win *w = &wins[i];
        if (!in_box(mx, my, w->x, w->y, w->w, w->h))
            continue;
        if (in_box(mx, my, w->x + w->w - TITLE_H + 2, w->y + 4, TITLE_H - 8, TITLE_H - 12)) {
            /* [X] */
            struct gui_win t = *w;
            for (int j = i; j < nwins - 1; j++)
                wins[j] = wins[j + 1];
            wins[nwins - 1] = t;
            close_top();
            return;
        }
        if (my < w->y + TITLE_H) {
            /* заголовок: поднять + начать перетаскивание */
            if (i != nwins - 1) {
                struct gui_win t = wins[i];
                for (int j = i; j < nwins - 1; j++)
                    wins[j] = wins[j + 1];
                wins[nwins - 1] = t;
                w = &wins[nwins - 1];
            }
            drag = nwins - 1;
            drag_dx = mx - w->x;
            drag_dy = my - w->y;
            need_redraw = 1;
            return;
        }
        /* тело */
        if (i != nwins - 1) {
            struct gui_win t = wins[i];
            for (int j = i; j < nwins - 1; j++)
                wins[j] = wins[j + 1];
            wins[nwins - 1] = t;
            w = &wins[nwins - 1];
            need_redraw = 1;
        }
        if (apps[w->app].click) {
            s32 x, y, rw, rh;
            content_rect(w, &x, &y, &rw, &rh);
            apps[w->app].click(w, mx, my);
        }
        return;
    }
    /* иконки */
    for (int i = 0; i < NAPPS; i++) {
        s32 x, y, ww, hh;
        icon_rect(i, &x, &y, &ww, &hh);
        if (in_box(mx, my, x, y, ww, hh)) {
            desk_sel = i;
            open_app(i);
            return;
        }
    }
    /* таскбар */
    if (my >= (s32)H - (s32)TBAR_H) {
        if (mx < 84) {
            menu_open = 1;
            menu_sel = 0;
            kprintf("[GUI] меню Пуск\n");
            need_redraw = 1;
        }
        return;
    }
}

/* ---------- главный цикл ---------- */
void gui_run(void)
{
    if (!fb_ready()) {
        kprintf("рабочий стол нужен графический режим (UEFI/VESA)\n");
        return;
    }
    cw = fb_width_px() / fb_cols();
    ch = fb_height_px() / fb_rows();
    if (!cw) cw = 8;
    if (!ch) ch = 16;
    nwins = 0;
    desk_sel = 0;
    menu_open = 0;
    drag = -1;
    kprintf("[GUI] рабочий стол готов\n");
    draw_all();

    u64 last_clock = 0;
    for (;;) {
        /* клавиатура */
        u16 k;
        while (kbd_trykey(&k)) {
            if (k == KEY_LAYOUT)
                continue;
            if (menu_open) {
                if (k == KEY_ESC) {
                    menu_open = 0;
                    need_redraw = 1;
                } else if (k == KEY_DOWN || k == '\t') {
                    menu_sel = (menu_sel + 1) % MENU_N;
                    need_redraw = 1;
                } else if (k == KEY_UP) {
                    menu_sel = (menu_sel + MENU_N - 1) % MENU_N;
                    need_redraw = 1;
                } else if (k == '\r' || k == '\n') {
                    menu_open = 0;
                    need_redraw = 1;
                    kprintf("[GUI] меню Пуск: %s\n", menu_items[menu_sel]);
                    if (menu_sel < 4)
                        open_app(menu_sel);
                    else {
                        kprintf("[GUI] выход в шелл\n");
                        nwins = 0;
                        goto out;
                    }
                }
                continue;
            }
            if (k == KEY_ESC) {
                if (nwins)
                    close_top();
                else {
                    kprintf("[GUI] выход в шелл\n");
                    goto out;
                }
                continue;
            }
            if (nwins && apps[wins[nwins - 1].app].key) {
                apps[wins[nwins - 1].app].key(&wins[nwins - 1], k);
                continue;
            }
            /* рабочий стол: навигация по иконкам */
            if (k == KEY_DOWN) {
                desk_sel = (desk_sel + 1) % NAPPS;
                need_redraw = 1;
            } else if (k == KEY_UP) {
                desk_sel = (desk_sel + NAPPS - 1) % NAPPS;
                need_redraw = 1;
            } else if (k == '\r' || k == '\n') {
                open_app(desk_sel);
            } else if (k >= '1' && k <= '4') {
                desk_sel = (int)(k - '1');
                open_app(desk_sel);
            }
        }
        /* мышь */
        struct mouse_state m;
        mouse_get(&m);
        if (mouse_take_click())
            handle_click(m.x, m.y);
        if (drag >= 0) {
            if (!(m.buttons & 1)) {
                drag = -1;
            } else if (drag < nwins) {
                wins[drag].x = m.x - drag_dx;
                wins[drag].y = m.y - drag_dy;
                if (wins[drag].x < 0) wins[drag].x = 0;
                if (wins[drag].y < 0) wins[drag].y = 0;
                if (wins[drag].x + wins[drag].w > (s32)fb_width_px())
                    wins[drag].x = (s32)fb_width_px() - wins[drag].w;
                if (wins[drag].y + wins[drag].h > (s32)fb_height_px() - (s32)TBAR_H)
                    wins[drag].y = (s32)fb_height_px() - (s32)TBAR_H - wins[drag].h;
                need_redraw = 1;
            }
        }
        /* часы: обновление раз в ~0.5 с */
        u64 t = pit_ticks();
        if (t - last_clock >= 50) {
            last_clock = t;
            for (int i = 0; i < nwins; i++) {
                if (wins[i].app == 1) {
                    mouse_hide();
                    app_clock_draw(&wins[i]);
                    draw_taskbar();
                    mouse_show();
                }
            }
            draw_taskbar();
        }
        if (need_redraw)
            draw_all();
        pit_sleep_ms(15);
    }
out:
    mouse_hide();
    console_clear();
}
