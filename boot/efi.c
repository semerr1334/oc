/*
 * OC OS — UEFI-загрузчик (\\EFI\\BOOT\\BOOTX64.EFI).
 *
 * Для современных ПК без Legacy-режима (как ваш HP). Прошивка UEFI сама
 * находит этот файл на FAT-разделе флешки и запускает его.
 *
 * Что делает: показывает красивое загрузочное меню (стрелки + Enter,
 * выбор: OC или Windows), читает \\KERNEL.BIN через SimpleFileSystem
 * (FAT — прошивка), собирает bootinfo (E820 из карты памяти UEFI, кадр
 * из GOP), строит identity-страницы 0..32 GiB (2 MiB страницы) и входит
 * в ядро. Записи на диск тут тоже НЕТ — только чтение.
 *
 * Пункт «Windows» ищет \\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI на ваших дисках
 * и передаёт ему управление (как вернуться в основную систему).
 *
 * Сборка: zig cc -target x86_64-windows-gnu (MS ABI — как требует UEFI).
 */
#include <stdint.h>
#include <stddef.h>
#include "boot.h"
#include "layout.h"

#define EFIAPI /* x86_64-windows-gnu = Microsoft x64 ABI, как нужно UEFI */

/* компилятор может вызвать их при копировании структур */
void *memset(void *p, int c, size_t n)
{
    unsigned char *d = p;
    while (n--)
        *d++ = (unsigned char)c;
    return p;
}
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

/* ---------------- минимальные определения UEFI ---------------- */

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef struct { uint32_t d1; uint16_t d2, d3; uint8_t d4[8]; } EFI_GUID;
typedef struct { uint64_t sig; uint32_t rev, hdr_sz, crc32, rsvd; } EFI_TABLE_HDR;
typedef struct { uint16_t scancode, unicode; } EFI_INPUT_KEY;

typedef struct {
    uint32_t type;
    uint32_t _pad;
    uint64_t phys, virt, pages, attr;
} EFI_MEM_DESC;

typedef struct EFI_TXT EFI_TXT;
struct EFI_TXT {
    void *Reset;
    int64_t (EFIAPI *OutputString)(EFI_TXT *, const uint16_t *);
    void *TestString, *QueryMode, *SetMode;
    int64_t (EFIAPI *SetAttribute)(EFI_TXT *, uint64_t);
    int64_t (EFIAPI *ClearScreen)(EFI_TXT *);
    int64_t (EFIAPI *SetCursorPosition)(EFI_TXT *, uint64_t, uint64_t);
    void *EnableCursor, *Mode;
};

typedef struct {
    void *Reset;
    int64_t (EFIAPI *ReadKeyStroke)(void *, EFI_INPUT_KEY *);
    void *WaitForKey;
} EFI_TXTIN;

typedef struct EFI_FILE EFI_FILE;
struct EFI_FILE {
    uint64_t rev;
    int64_t (EFIAPI *Open)(EFI_FILE *, EFI_FILE **, const uint16_t *,
                           uint64_t, uint64_t);
    int64_t (EFIAPI *Close)(EFI_FILE *);
    void *Delete;
    int64_t (EFIAPI *Read)(EFI_FILE *, uint64_t *, void *);
    void *Write, *GetPosition, *SetPosition, *GetInfo, *SetInfo, *Flush;
};

typedef struct {
    uint64_t rev;
    int64_t (EFIAPI *OpenVolume)(void *, EFI_FILE **);
} EFI_SFS;

typedef struct { uint32_t rmask, gmask, bmask, xmask; } EFI_BITMASK;
typedef struct {
    uint32_t ver, w, h, fmt;    /* fmt: 0 RGBX, 1 BGRX, 2 маски, 3 BLT */
    EFI_BITMASK bm;
    uint32_t stride;            /* пикселей в строке */
} EFI_GOPINFO;
typedef struct {
    uint32_t max_mode, mode;
    EFI_GOPINFO *info;
    uint64_t info_sz;
    uint64_t fb_base, fb_size;
} EFI_GOPMODE;
typedef struct {
    void *QueryMode, *SetMode, *Blt;
    EFI_GOPMODE *mode;
} EFI_GOP;

typedef struct {
    uint32_t rev;
    uint32_t _p;
    void *parent, *sys_table, *device_handle, *file_path, *reserved;
    uint32_t opt_sz;
    uint32_t _p2;
    void *opt, *image_base;
    uint64_t image_size;
    uint32_t code_type, data_type;
    void *Unload;
} EFI_LOADED_IMAGE;

typedef struct {
    EFI_TABLE_HDR hdr;
    void *RaiseTPL, *RestoreTPL;
    int64_t (EFIAPI *AllocatePages)(uint32_t, uint32_t, uint64_t, uint64_t *);
    void *FreePages;
    int64_t (EFIAPI *GetMemoryMap)(uint64_t *, EFI_MEM_DESC *, uint64_t *,
                                   uint64_t *, uint32_t *);
    void *AllocatePool, *FreePool;
    void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface, *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    int64_t (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath;
    void *InstallConfigurationTable;
    int64_t (EFIAPI *LoadImage)(uint32_t, EFI_HANDLE, void *, void *,
                                uint64_t, EFI_HANDLE *);
    int64_t (EFIAPI *StartImage)(EFI_HANDLE, uint64_t *, uint16_t *);
    void *Exit, *UnloadImage;
    int64_t (EFIAPI *ExitBootServices)(EFI_HANDLE, uint64_t);
    void *GetNextMonotonicCount;
    void (EFIAPI *Stall)(uint64_t);
    int64_t (EFIAPI *SetWatchdogTimer)(uint64_t, uint64_t, uint64_t, uint16_t *);
    void *ConnectController, *DisconnectController, *OpenProtocol;
    void *CloseProtocol, *OpenProtocolInformation, *ProtocolsPerHandle;
    int64_t (EFIAPI *LocateHandleBuffer)(uint32_t, EFI_GUID *, void *,
                                         uint64_t *, EFI_HANDLE **);
    int64_t (EFIAPI *LocateProtocol)(EFI_GUID *, void *, void **);
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32, *CopyMem, *SetMem, *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HDR hdr;
    uint16_t *fw_vendor;
    uint32_t fw_rev;
    uint32_t _pad;
    EFI_HANDLE cin_h;   EFI_TXTIN *cin;
    EFI_HANDLE cout_h;  EFI_TXT *cout;
    EFI_HANDLE cerr_h;  EFI_TXT *cerr;
    void *runtime;
    EFI_BOOT_SERVICES *bs;
} EFI_SYSTEM_TABLE;

static EFI_GUID g_loaded_image = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID g_sfs = {
    0x964e5b22, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static EFI_GUID g_gop_proto = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

/* ---------------- вывод ---------------- */

static EFI_SYSTEM_TABLE *g_st;
static EFI_MEM_DESC g_map[256];     /* вне стека: не нужен chkstk */

static void prints(const uint16_t *s)
{
    if (g_st && g_st->cout)
        g_st->cout->OutputString(g_st->cout, s);
}

static void phex(uint64_t v)
{
    uint16_t b[19];
    int i;
    b[18] = 0;
    for (i = 17; i >= 0; i--) {
        uint64_t d = v & 0xF;
        b[i] = (uint16_t)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
        if (!v && i < 17) {
            prints(&b[i + 1]);
            return;
        }
    }
    prints(b);
}

/* ---------------- графика GOP (быстрая, без лишних вызовов) ---------------- */

extern const unsigned char font8x16[195][16];

static EFI_GOP *g_gop;
static unsigned char *g_fb;
static uint32_t g_pitch, g_w, g_h;
static int g_bgr;
static int g_gfx_ok;

static void gfx_px(uint32_t x, uint32_t y, uint32_t r, uint32_t g, uint32_t b)
{
    if (x >= g_w || y >= g_h)
        return;
    unsigned char *p = g_fb + ((uint64_t)y * g_pitch + x) * 4;
    if (g_bgr) { p[0] = (unsigned char)b; p[1] = (unsigned char)g; p[2] = (unsigned char)r; }
    else       { p[0] = (unsigned char)r; p[1] = (unsigned char)g; p[2] = (unsigned char)b; }
}

/* быстрая заливка прямоугольника — построчно, без вызова gfx_px */
static void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint32_t r, uint32_t g, uint32_t b)
{
    if (x >= g_w || y >= g_h || !w || !h)
        return;
    if ((uint64_t)x + w > g_w) w = g_w - x;
    if ((uint64_t)y + h > g_h) h = g_h - y;
    if (!w || !h)
        return;
    unsigned char br = (unsigned char)b, gg = (unsigned char)g, rr = (unsigned char)r;
    if (g_bgr) {
        /* BGRX */
        for (uint32_t yy = 0; yy < h; yy++) {
            unsigned char *row = g_fb + ((uint64_t)(y + yy) * g_pitch + x) * 4;
            for (uint32_t xx = 0; xx < w; xx++) {
                row[0] = br; row[1] = gg; row[2] = rr; row[3] = 0;
                row += 4;
            }
        }
    } else {
        for (uint32_t yy = 0; yy < h; yy++) {
            unsigned char *row = g_fb + ((uint64_t)(y + yy) * g_pitch + x) * 4;
            for (uint32_t xx = 0; xx < w; xx++) {
                row[0] = rr; row[1] = gg; row[2] = br; row[3] = 0;
                row += 4;
            }
        }
    }
}

static void gfx_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t r, uint32_t g, uint32_t b)
{
    gfx_rect(x, y, w, 1, r, g, b);
    gfx_rect(x, y + h - 1, w, 1, r, g, b);
    gfx_rect(x, y, 1, h, r, g, b);
    gfx_rect(x + w - 1, y, 1, h, r, g, b);
}

static uint8_t enc16(uint16_t cp)
{
    if (cp < 0x80) return (uint8_t)cp;
    if (cp == 0x401) return 0xC0;
    if (cp == 0x451) return 0xC1;
    if (cp == 0x2116) return 0xC2;
    if (cp >= 0x410 && cp <= 0x42F) return (uint8_t)(0x80 + (cp - 0x410));
    if (cp >= 0x430 && cp <= 0x44F) return (uint8_t)(0xA0 + (cp - 0x430));
    return 0;
}

static uint32_t gfx_str_w(const uint16_t *s, uint32_t sc)
{
    uint32_t n = 0;
    while (*s++) n++;
    return n * 8 * sc;
}

static void gfx_str(uint32_t x, uint32_t y, const uint16_t *s,
                    uint32_t r, uint32_t g, uint32_t b, uint32_t sc)
{
    while (*s) {
        uint8_t code = enc16(*s++);
        const unsigned char *gl = font8x16[code ? code : '?'];
        for (uint32_t row = 0; row < 16; row++) {
            uint8_t bits = gl[row];
            if (!bits) continue;
            for (uint32_t col = 0; col < 8; col++) {
                if (bits & (0x80 >> col))
                    gfx_rect(x + col * sc, y + row * sc, sc, sc, r, g, b);
            }
        }
        x += 8 * sc;
    }
}

static void gfx_str_cx(uint32_t cx, uint32_t y, const uint16_t *s,
                       uint32_t r, uint32_t g, uint32_t b, uint32_t sc)
{
    uint32_t w = gfx_str_w(s, sc);
    gfx_str(cx > w / 2 ? cx - w / 2 : 0, y, s, r, g, b, sc);
}

static int gfx_init(void)
{
    EFI_BOOT_SERVICES *bs = g_st->bs;
    EFI_GOPINFO *fi;
    if (bs->LocateProtocol(&g_gop_proto, 0, (void **)&g_gop) || !g_gop)
        return 0;
    fi = g_gop->mode ? g_gop->mode->info : 0;
    if (!fi || !g_gop->mode->fb_base || !fi->w || !fi->h || !fi->stride)
        return 0;
    g_fb = (unsigned char *)g_gop->mode->fb_base;
    g_pitch = fi->stride;
    g_w = fi->w;
    g_h = fi->h;
    g_bgr = (fi->fmt == 1);
    return 1;
}

static void gfx_clear(uint32_t r, uint32_t g, uint32_t b)
{
    if (!g_gfx_ok) return;
    gfx_rect(0, 0, g_w, g_h, r, g, b);
}

static void gfx_loading(const uint16_t *msg)
{
    if (!g_gfx_ok) return;
    gfx_clear(16, 34, 72);
    /* градиент как на столе */
    for (uint32_t i = 0; i < 24; i++)
        gfx_rect(0, i * (g_h / 24), g_w, g_h / 24 + 1,
                 (uint32_t)(12 + i), (uint32_t)(28 + i * 2), (uint32_t)(64 + i * 3));
    uint32_t cx = g_w / 2;
    gfx_str_cx(cx, 40, L"O C", 150, 190, 255, 7);
    gfx_str_cx(cx, 40 + 16 * 7 + 10, msg, 220, 230, 255, 2);
    gfx_str_cx(cx, g_h / 2 + 60,
               L"\x041F\x043E\x0434\x043E\x0436\x0434\x0438\x0442\x0435... \x0441\x0435\x0439\x0447\x0430\x0441 \x0431\x0443\x0434\x0435\x0442 \x0440\x0430\x0431\x043E\x0447\x0438\x0439 \x0441\x0442\x043E\x043B",
               180, 190, 220, 1);
}

static void die(const uint16_t *s)
{
    if (g_gfx_ok && g_fb) {
        gfx_clear(80, 20, 30);
        uint32_t cx = g_w / 2;
        gfx_str_cx(cx, 60, L"\x041E\x0448\x0438\x0431\x043A\x0430 \x0437\x0430\x0433\x0440\x0443\x0437\x043A\x0438 OC", 255, 200, 200, 2);
        gfx_str_cx(cx, 120, s, 255, 255, 255, 1);
        gfx_str_cx(cx, 160,
                   L"\x041D\x0430\x0436\x043C\x0438\x0442\x0435 \x043A\x043D\x043E\x043F\x043A\x0443 \x043F\x0438\x0442\x0430\x043D\x0438\x044F \x0438 \x043F\x043E\x043F\x0440\x043E\x0431\x0443\x0439\x0442\x0435 \x0441\x043D\x043E\x0432\x0430",
                   200, 200, 220, 1);
        prints(L"\r\nOC: ");
        prints(s);
        prints(L"\r\n");
        for (;;) ;
    }
    prints(L"\r\nOC: ");
    prints(s);
    prints(L"\r\n");
    for (;;)
        ;
}

/* ---------------- загрузочное меню (графика GOP в стиле стола) ---------------- */

/* рамка + логотип + пункты; sel = 0..1 — подсвеченный пункт */
static void menu_draw(int sel)
{
    uint32_t cx = g_w / 2;

    /* обои: градиент как на рабочем столе */
    for (uint32_t i = 0; i < 24; i++)
        gfx_rect(0, i * (g_h / 24), g_w, g_h / 24 + 1,
                 (uint32_t)(12 + i), (uint32_t)(28 + i * 2), (uint32_t)(64 + i * 3));

    /* логотип OC */
    gfx_str_cx(cx, 40, L"O C", 150, 190, 255, 7);
    gfx_str_cx(cx, 40 + 16 * 7 + 6, L"\x0414\x0432\x0443\x0445\x0441\x043E\x0442\x043C\x0435\x0442\x0440\x043E\x0432\x043A\x0430 - \x0432\x0430\x0448\x0430 \x0441\x0438\x0441\x0442\x0435\x043C\x0430",
               170, 185, 220, 2);

    /* карточки-пункты в стиле окон */
    uint32_t bw = g_w > 700 ? 640 : g_w - 40;
    uint32_t bx = cx - bw / 2;
    uint32_t by = 260;
    for (int i = 0; i < 2; i++) {
        uint32_t y = by + (uint32_t)i * 96;
        uint32_t rr = (i == (uint32_t)sel) ? 255 : 40, gg = (i == sel) ? 210 : 90,
                 bb = (i == sel) ? 60 : 180;
        /* заголовок-полоска */
        gfx_rect(bx, y, bw, 24, 40, 90, 180);
        gfx_rect(bx + 4, y + 3, 30, 18, i == sel ? 255 : 205, i == sel ? 210 : 210,
                 i == sel ? 60 : 220);
        gfx_str(bx + 12, y + 5, (i == 0) ? L"1" : L"2", 20, 20, 30, 1);
        gfx_str(bx + 44, y + 5,
                (i == 0) ? L"\x0417\x0430\x043F\x0443\x0441\x043A OC - \x0440\x0430\x0431\x043E\x0447\x0438\x0439 \x0441\x0442\x043E\x043B"
                         : L"Windows - \x043E\x0441\x043D\x043E\x0432\x043D\x0430\x044F \x0441\x0438\x0441\x0442\x0435\x043C\x0430",
                255, 255, 255, 1);
        /* тело */
        gfx_rect(bx + 2, y + 26, bw - 4, 44, 235, 235, 235);
        gfx_str(bx + 16, y + 40,
                (i == 0) ? L"\x0413\x0440\x0430\x0444\x0438\x0447\x0435\x0441\x043A\x0430\x044F \x043E\x0431\x043E\x043B\x043E\x0447\x043A\x0430, \x043F\x0440\x043E\x0433\x0440\x0430\x043C\x043C\x044B, \x0438\x0433\x0440\x044B"
                         : L"\x0412\x0430\x0448\x0430 \x043F\x0440\x043E\x0448\x043B\x0430\x044F \x0441\x0438\x0441\x0442\x0435\x043C\x0430 - \x0432\x0441\x0451 \x043D\x0430 \x043C\x0435\x0441\x0442\x0435",
                20, 20, 30, 1);
        gfx_frame(bx, y, bw, 72, rr, gg, bb);
        if (i == sel)
            gfx_frame(bx - 2, y - 2, bw + 4, 76, 255, 210, 60);
    }

    /* подсказка */
    gfx_str_cx(cx, g_h - 56,
               L"\x0421\x0442\x0440\x0435\x043B\x043A\x0438 - \x0432\x044B\x0431\x043E\x0440    Enter - \x0437\x0430\x043F\x0443\x0441\x043A    1/2 - \x0441\x0440\x0430\x0437\x0443",
               150, 165, 200, 1);
}

/* строка статуса: отсчёт (sec>0) или подсказка (sec<=0) */
static void menu_status(int sec)
{
    uint32_t cx = g_w / 2;
    /* чистим нижнюю зону */
    gfx_rect(0, g_h - 40, g_w, 40, 16, 34, 72);
    if (sec > 0) {
        uint16_t line[64];
        const uint16_t *p = L"\x0410\x0432\x0442\x043E\x0437\x0430\x043F\x0443\x0441\x043A OC \x0447\x0435\x0440\x0435\x0437  ";
        int n = 0;
        while (*p) line[n++] = *p++;
        line[n++] = (uint16_t)('0' + sec);
        p = L" \x0441\x0435\x043A";
        while (*p) line[n++] = *p++;
        line[n] = 0;
        gfx_str_cx(cx, g_h - 34, line, 200, 210, 230, 1);
        /* полоска-таймер */
        uint32_t bw = 300;
        gfx_rect(cx - bw / 2, g_h - 14, bw, 8, 40, 50, 70);
        gfx_rect(cx - bw / 2, g_h - 14, (uint32_t)sec * bw / 5, 8, 50, 140, 80);
    } else {
        gfx_str_cx(cx, g_h - 34,
                   L"\x0412\x044B\x0431\x043E\x0440 \x043E\x0441\x0442\x0430\x043B\x0441\x044F \x0437\x0430 \x0432\x0430\x043C\x0438: \x043D\x0430\x0436\x043C\x0438\x0442\x0435 1 \x0438\x043B\x0438 2",
                   200, 210, 230, 1);
    }
}

/* ---------- текстовый запасной вариант (нет GOP) ---------- */
#define ATTR_TITLE  0x0B
#define ATTR_ITEM   0x0F
#define ATTR_DIM    0x08
#define ATTR_SEL    0x70

static void put_pad(const uint16_t *s, int width)
{
    const uint16_t *p = s;
    int n = 0;
    while (*p++) n++;
    prints(s);
    for (int i = n; i < width; i++) prints(L" ");
    prints(L"\r\n");
}

static void menu_draw_text(int sel)
{
    EFI_TXT *o = g_st->cout;
    o->SetCursorPosition(o, 0, 0);
    o->SetAttribute(o, ATTR_DIM);
    put_pad(L"  ==================================================", 52);
    o->SetAttribute(o, ATTR_TITLE);
    put_pad(L"       ____   ____", 52);
    put_pad(L"      / __ \\ / ___|", 52);
    put_pad(L"     | |  | | |", 52);
    put_pad(L"     | |  | | |", 52);
    put_pad(L"     | |__| | |___", 52);
    put_pad(L"      \\____/ \\____/", 52);
    o->SetAttribute(o, ATTR_DIM);
    put_pad(L"  ==================================================", 52);
    o->SetAttribute(o, ATTR_ITEM);
    put_pad(L"   OC v0.3 \xAB\x0414\x0432\x0443\x0445\x0441\x043E\x0442\x043C\x0435\x0442\x0440\x043E\x0432\x043A\x0430\xBB \x2014 \x0432\x0430\x0448\x0430 \x0441\x0438\x0441\x0442\x0435\x043C\x0430", 52);
    put_pad(L"", 52);
    o->SetAttribute(o, sel == 0 ? ATTR_SEL : ATTR_ITEM);
    put_pad(sel == 0
        ? L"  > [1] \x0417\x0430\x043F\x0443\x0441\x0442\x0438\x0442\x044C OC                 (Enter)"
        : L"    [1] \x0417\x0430\x043F\x0443\x0441\x0442\x0438\x0442\x044C OC                 (Enter)", 52);
    o->SetAttribute(o, sel == 1 ? ATTR_SEL : ATTR_ITEM);
    put_pad(sel == 1
        ? L"  > [2] Windows \x2014 \x043E\x0441\x043D\x043E\x0432\x043D\x0430\x044F \x0441\x0438\x0441\x0442\x0435\x043C\x0430"
        : L"    [2] Windows \x2014 \x043E\x0441\x043D\x043E\x0432\x043D\x0430\x044F \x0441\x0438\x0441\x0442\x0435\x043C\x0430", 52);
    put_pad(L"", 52);
}

static void menu_status_text(int sec)
{
    EFI_TXT *o = g_st->cout;
    o->SetAttribute(o, ATTR_DIM);
    o->SetCursorPosition(o, 0, 13);
    if (sec > 0) {
        prints(L"   \x0410\x0432\x0442\x043E\x0437\x0430\x043F\x0443\x0441\x043A OC \x0447\x0435\x0440\x0435\x0437 ");
        uint16_t d[2] = { (uint16_t)('0' + sec), 0 };
        prints(d);
        prints(L" \x0441\x0435\x043A   (\x0441\x0442\x0440\x0435\x043B\x043A\x0438 \x2014 \x0432\x044B\x0431\x043E\x0440, Enter \x2014 \x0441\x0440\x0430\x0437\x0443)          ");
    } else {
        prints(L"   \x0412\x044B\x0431\x043E\x0440: \x0441\x0442\x0440\x0435\x043B\x043A\x0438 \x0432\x0432\x0435\x0440\x0445/\x0432\x043D\x0438\x0437 + Enter (\x0438\x043B\x0438 1/2)   ");
    }
    o->SetAttribute(o, ATTR_ITEM);
}

static void menu_redraw(int sel)
{
    if (g_gfx_ok) menu_draw(sel); else menu_draw_text(sel);
}
static void menu_status_any(int sec)
{
    if (g_gfx_ok) menu_status(sec); else menu_status_text(sec);
}

/* клавиши: стрелки (UEFI scancodes 0x01/0x02), Enter, 1/2, Esc.
 * возвращает 1 = OC, 2 = Windows, 0 = остаёмся в меню */
static int menu_onkey(EFI_INPUT_KEY *key, int *sel)
{
    if (key->scancode == 0x01) {            /* Up */
        *sel = 0;
        menu_redraw(*sel);
        return 0;
    }
    if (key->scancode == 0x02) {            /* Down */
        *sel = 1;
        menu_redraw(*sel);
        return 0;
    }
    if (key->unicode == '1')
        return 1;
    if (key->unicode == '2')
        return 2;
    if (key->unicode == '\r')
        return *sel + 1;
    if (key->scancode == 0x17)              /* Esc */
        return 1;
    return 0;
}

/* Меню: 1 = OC (автозагрузка через 5 с), 2 = Windows. */
static int boot_menu(void)
{
    EFI_BOOT_SERVICES *bs = g_st->bs;
    EFI_TXTIN *cin = g_st->cin;
    EFI_INPUT_KEY key;
    int sel = 0;
    int ticks = 50;                         /* 50 x 100 мс = 5 с; <0 — стоп */
    int last_sec = -2;

    g_gfx_ok = gfx_init();
    if (!g_gfx_ok)
        g_st->cout->ClearScreen(g_st->cout);
    menu_redraw(sel);
    menu_status_any(5);
    last_sec = 5;

    for (;;) {
        if (!cin->ReadKeyStroke(cin, &key)) {
            int a = menu_onkey(&key, &sel);
            if (a)
                return a;
            ticks = -1;                     /* любая клавиша — отсчёт стоп */
            if (last_sec != -1) {
                last_sec = -1;
                menu_status_any(0);
            }
            continue;
        }
        if (ticks > 0) {
            ticks--;
            int sec = (ticks + 9) / 10;
            if (sec > 0 && sec != last_sec) {
                last_sec = sec;
                menu_status_any(sec);
            }
            if (!ticks)
                return 1;                   /* время вышло — грузим OC */
            bs->Stall(100000);              /* 100 мс */
        } else {
            bs->Stall(20000);
        }
    }
}

/* Ищет Windows на ваших дисках и передаёт ей управление.
 * Возврат = не нашёл/не запустилась (меню покажется снова). */
static void try_windows(EFI_HANDLE img)
{
    EFI_BOOT_SERVICES *bs = g_st->bs;
    EFI_HANDLE *handles = 0;
    uint64_t cnt = 0, i;

    prints(L"\r\nИщу Windows на ваших дисках...\r\n");
    if (bs->LocateHandleBuffer(2 /* ByProtocol */, &g_sfs, 0, &cnt, &handles))
        return;

    for (i = 0; i < cnt; i++) {
        EFI_SFS *sfs = 0;
        EFI_FILE *root = 0, *file = 0;
        uint64_t addr = 0, size = 0;
        EFI_HANDLE himg = 0;

        if (bs->HandleProtocol(handles[i], &g_sfs, (void **)&sfs) || !sfs)
            continue;
        if (sfs->OpenVolume(sfs, &root) || !root)
            continue;
        if (root->Open(root, &file,
                       L"\\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI", 1, 0)) {
            root->Close(root);
            continue;
        }
        if (bs->AllocatePages(0, 2, 2048, &addr)) {     /* до 8 МиБ */
            file->Close(file);
            root->Close(root);
            continue;
        }
        for (;;) {                                      /* читаем файл */
            uint64_t chunk = 4096;
            if (size + chunk > 2048 * 4096)
                break;
            if (file->Read(file, &chunk, (uint8_t *)addr + size) || !chunk)
                break;
            size += chunk;
        }
        file->Close(file);
        root->Close(root);
        if (size < 4096)
            continue;

        prints(L"Загружаю Windows...\r\n");
        if (!bs->LoadImage(0, img, 0, (void *)addr, size, &himg) && himg) {
            bs->StartImage(himg, 0, 0);   /* обычно уже не возвращается */
            prints(L"Windows не запустилась, возвращаюсь в меню...\r\n");
        }
    }
}

/* ---------------- GDT / вход в ядро ---------------- */

static uint64_t gdt[3] = {
    0,
    0x00AF9B000000FFFFull,  /* 0x08: код 64-bit */
    0x00CF93000000FFFFull   /* 0x10: данные    */
};

static void jump_kernel(uint64_t entry, uint64_t bootinfo, uint64_t pml4)
{
    struct { uint16_t lim; uint64_t base; } __attribute__((packed)) gdtr;
    gdtr.lim = 23;
    gdtr.base = (uint64_t)gdt;

    __asm__ volatile(
        "cli\n\t"
        "lgdt %0\n\t"
        "movq %1, %%cr3\n\t"
        "movq %2, %%rsp\n\t"
        "pushq $0x08\n\t"
        "pushq %3\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "lretq\n\t"
        :
        : "m"(gdtr), "r"(pml4), "r"((uint64_t)TEMP_STACK), "r"(entry),
          "D"(bootinfo), "a"(0)
        : "memory");
}

/* ---------------- E820 из карты памяти UEFI ---------------- */

static uint32_t e820_from_uefi(EFI_MEM_DESC *map, uint64_t map_sz,
                              uint64_t dsz, oc_boot_info_t *bi)
{
    uint64_t off, n = 0;
    for (off = 0; off + sizeof(EFI_MEM_DESC) <= map_sz; off += dsz) {
        EFI_MEM_DESC *d = (EFI_MEM_DESC *)((uint8_t *)map + off);
        uint32_t t;
        uint64_t base = d->phys, len = d->pages * 4096ull;
        switch (d->type) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 13:
            t = E820_USABLE; break;
        case 7:
            t = E820_BAD; break;
        case 8:
            t = E820_ACPI; break;
        case 9:
            t = E820_NVS; break;
        default:
            t = E820_RESERVED; break;
        }
        if (!len)
            continue;
        if (n && bi->e820[n - 1].type == t &&
            bi->e820[n - 1].base + bi->e820[n - 1].length == base) {
            bi->e820[n - 1].length += len;      /* склеиваем соседние */
        } else if (n < BI_E820_MAX) {
            bi->e820[n].base = base;
            bi->e820[n].length = len;
            bi->e820[n].type = t;
            bi->e820[n].attr = 1;
            n++;
        } else if (n) {
            bi->e820[n - 1].length += len;      /* места мало — дописываем */
        }
    }
    return (uint32_t)n;
}

/* ---------------- точка входа ---------------- */

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE img, EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->bs;
    EFI_LOADED_IMAGE *li = 0;
    EFI_SFS *sfs = 0;
    EFI_FILE *root = 0, *file = 0;
    EFI_GOP *gop = 0;
    oc_boot_info_t *bi;
    uint64_t bi_addr, pt_addr = 0, kaddr_staging = 0, ksize = 0;
    uint64_t map_sz, key, dsz, i, j;
    uint32_t ver;
    EFI_MEM_DESC *map = g_map;
    uint64_t *pml4, *pdpt, *pd;
    int r;

    g_st = st;

    /* watchdog off — чтобы прошивка не перезагрузила нас во время меню */
    if (bs->SetWatchdogTimer)
        bs->SetWatchdogTimer(0, 0, 0, 0);

    /* ---- загрузочное меню: OC или Windows ---- */
    for (;;) {
        int sel = boot_menu();
        if (sel != 2)
            break;
        try_windows(img);       /* вернулись — Windows нет/не стартовала */
    }

    /* показываем экран загрузки (если графика есть) */
    if (g_gfx_ok) {
        gfx_loading(L"\x0417\x0430\x0433\x0440\x0443\x0436\x0430\x044E OC... \x0447\x0438\x0442\x0430\x044E \x044F\x0434\x0440\x043E");
    } else {
        g_st->cout->SetAttribute(g_st->cout, ATTR_ITEM);
        prints(L"\r\nOC UEFI loader\r\n");
    }

    /* ---- ядро с FAT-раздела (прошивка умеет FAT сама) ---- */
    if (bs->HandleProtocol(img, &g_loaded_image, (void **)&li) ||
        bs->HandleProtocol(li->device_handle, &g_sfs, (void **)&sfs) ||
        sfs->OpenVolume(sfs, &root) ||
        root->Open(root, &file, L"\\KERNEL.BIN", 1, 0))
        die(L"cannot open \\KERNEL.BIN");

    /* читаем ядро во временный буфер (AnyPages) — не требуем 0x100000 свободным до ExitBS */
    {
        uint64_t pages = (KERNEL_MAX_BYTES + 4095) / 4096;
        if (bs->AllocatePages(0, 2, pages, &kaddr_staging))
            die(L"cannot allocate kernel staging");
    }

    for (;;) {
        uint64_t chunk = 4096;
        if (ksize + chunk > KERNEL_MAX_BYTES)
            break;
        if (file->Read(file, &chunk, (uint8_t *)kaddr_staging + ksize) || !chunk)
            break;
        ksize += chunk;
    }
    file->Close(file);
    if (ksize < 4096)
        die(L"\\KERNEL.BIN is empty");
    if (!g_gfx_ok) {
        prints(L"kernel loaded (");
        phex(ksize);
        prints(L" bytes)\r\n");
    } else {
        /* обновим статус на экране загрузки */
        gfx_loading(L"\x042F\x0434\x0440\x043E \x0437\x0430\x0433\x0440\x0443\x0436\x0435\x043D\x043E, \x0433\x043E\x0442\x043E\x0432\x043B\x044E \x043F\x0430\x043C\x044F\x0442\x044C...");
    }

    /* ---- bootinfo ---- */
    bi_addr = BOOTINFO_PHYS;
    if (bs->AllocatePages(2, 2, 1, &bi_addr)) {
        bi_addr = 0;
        if (bs->AllocatePages(0, 2, 1, &bi_addr))
            die(L"cannot allocate bootinfo");
    }
    bi = (oc_boot_info_t *)bi_addr;
    memset(bi, 0, sizeof *bi);
    bi->magic = BOOTINFO_MAGIC;
    bi->version = BOOTINFO_VERSION;
    bi->boot_drive = 0x80;
    bi->kernel_size = (uint32_t)ksize;

    /* ---- identity-страницы 0..32 GiB (2 MiB) ---- */
    if (bs->AllocatePages(0, 2, 34, &pt_addr))
        die(L"cannot allocate page tables");
    pml4 = (uint64_t *)pt_addr;
    pdpt = (uint64_t *)(pt_addr + 4096);
    memset((void *)pt_addr, 0, 34 * 4096);
    pml4[0] = (uint64_t)pdpt | 3;
    for (i = 0; i < 32; i++) {
        pd = (uint64_t *)(pt_addr + (2 + i) * 4096);
        pdpt[i] = (uint64_t)pd | 3;
        for (j = 0; j < 512; j++)
            pd[j] = (i << 30) + (j << 21) | 0x83;   /* P+W+2MiB */
    }

    /* ---- видеокадр (GOP) ---- */
    if (!bs->LocateProtocol(&g_gop, 0, (void **)&gop) &&
        gop && gop->mode && gop->mode->info && gop->mode->fb_base &&
        gop->mode->info->fmt != 3) {
        EFI_GOPINFO *mi = gop->mode->info;
        bi->fb_present = 1;
        bi->fb_addr = gop->mode->fb_base;
        bi->fb_pitch = mi->stride * 4;
        bi->fb_width = mi->w;
        bi->fb_height = mi->h;
        bi->fb_bpp = 32;
        bi->fb_resv_pos = 24;
        bi->fb_resv_size = 8;
        if (mi->fmt == 2 && mi->bm.rmask) {          /* явные маски */
            uint32_t m;
            int p;
            uint32_t masks[3] = { mi->bm.rmask, mi->bm.gmask, mi->bm.bmask };
            uint8_t *pos[3] = { &bi->fb_rpos, &bi->fb_gpos, &bi->fb_bpos };
            uint8_t *siz[3] = { &bi->fb_rsize, &bi->fb_gsize, &bi->fb_bsize };
            for (p = 0; p < 3; p++) {
                m = masks[p];
                *pos[p] = 0;
                while (!(m & 1)) { m >>= 1; (*pos[p])++; }
                *siz[p] = 0;
                while (m & 1) { m >>= 1; (*siz[p])++; }
            }
        } else if (mi->fmt == 0) {                   /* RGBX */
            bi->fb_rpos = 0;  bi->fb_gpos = 8;  bi->fb_bpos = 16;
            bi->fb_rsize = bi->fb_gsize = bi->fb_bsize = 8;
        } else {                                     /* BGRX (обычно так) */
            bi->fb_rpos = 16; bi->fb_gpos = 8;  bi->fb_bpos = 0;
            bi->fb_rsize = bi->fb_gsize = bi->fb_bsize = 8;
        }
    } else {
        if (!g_gfx_ok)
            prints(L"no GOP framebuffer, VGA text fallback\r\n");
    }

    /* ---- карта памяти + выход из UEFI ---- */
    for (r = 0; r < 4; r++) {
        map_sz = sizeof g_map;
        if (bs->GetMemoryMap(&map_sz, map, &key, &dsz, &ver))
            die(L"GetMemoryMap failed");
        bi->e820_count = e820_from_uefi(map, map_sz, dsz, bi);
        if (!bs->ExitBootServices(img, key))
            goto exited;
    }
    die(L"ExitBootServices failed");

exited:
    /* дальше только мы: копируем ядро на постоянное место, страницы, GDT, вход */
    if (kaddr_staging != KERNEL_PHYS) {
        memcpy((void *)KERNEL_PHYS, (void *)kaddr_staging, (size_t)ksize);
    }
    jump_kernel(KERNEL_PHYS, bi_addr, (uint64_t)pml4);
    for (;;)
        ;
    return 0;
}
