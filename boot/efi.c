/*
 * OC OS — UEFI-загрузчик (\\EFI\\BOOT\\BOOTX64.EFI).
 *
 * Для современных ПК без Legacy-режима (как ваш HP). Прошивка UEFI сама
 * находит этот файл на FAT-разделе флешки и запускает его.
 *
 * Что делает: показывает красивое загрузочное меню (стрелки + Enter,
 * выбор: OC или Windows), читает \KERNEL.BIN через SimpleFileSystem
 * (FAT — прошивка), собирает bootinfo (E820 из карты памяти UEFI, кадр
 * из GOP), строит identity-страницы 0..64 GiB (2 MiB страницы) и входит
 * в ядро. Записи на диск тут тоже НЕТ — только чтение.
 *
 * Пункт «Windows» ищет \EFI\MICROSOFT\BOOT\BOOTMGFW.EFI на ваших дисках
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
    void *SetWatchdogTimer;
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
static EFI_GUID g_gop = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

/* ---------------- вывод ---------------- */

static EFI_SYSTEM_TABLE *g_st;
static EFI_MEM_DESC g_map[256];     /* вне стека: не нужен chkstk */

static void prints(const uint16_t *s)
{
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

static void die(const uint16_t *s)
{
    prints(L"\r\nOC: ");
    prints(s);
    prints(L"\r\n");
    for (;;)
        ;
}

/* ---------------- загрузочное меню ---------------- */

#define ATTR_TITLE  0x0B        /* светло-голубой */
#define ATTR_ITEM   0x0F        /* белый */
#define ATTR_DIM    0x08        /* серый */
#define ATTR_SEL    0x70        /* подсветка: чёрным по белому */
#define ATTR_WARN   0x0E        /* жёлтый */

static void put_pad(const uint16_t *s, int width)
{
    const uint16_t *p = s;
    int n = 0;
    while (*p++)
        n++;
    prints(s);
    for (int i = n; i < width; i++)
        prints(L" ");
    prints(L"\r\n");
}

/* рамка + логотип + пункты; sel = 0..1 — подсвеченный пункт */
static void menu_draw(int sel)
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
    put_pad(L"   OC OS 0.1 \xAB\x0414\x0432\x0443\x0445\x0441\x043E\x0442\x043C\x0435\x0442\x0440\x043E\x0432\x043A\x0430\xBB \x2014 \x0432\x0430\x0448\x0430 \x0441\x0438\x0441\x0442\x0435\x043C\x0430", 52);
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

/* строка статуса: отсчёт (sec>0) или подсказка (sec<=0) */
static void menu_status(int sec)
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

/* клавиши: стрелки (UEFI scancodes 0x01/0x02), Enter, 1/2, Esc.
 * возвращает 1 = OC, 2 = Windows, 0 = остаёмся в меню */
static int menu_onkey(EFI_INPUT_KEY *key, int *sel)
{
    if (key->scancode == 0x01) {            /* Up */
        *sel = 0;
        menu_draw(*sel);
        return 0;
    }
    if (key->scancode == 0x02) {            /* Down */
        *sel = 1;
        menu_draw(*sel);
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

    g_st->cout->ClearScreen(g_st->cout);
    menu_draw(sel);
    menu_status(5);
    last_sec = 5;

    for (;;) {
        if (!cin->ReadKeyStroke(cin, &key)) {
            int a = menu_onkey(&key, &sel);
            if (a)
                return a;
            ticks = -1;                     /* любая клавиша — отсчёт стоп */
            if (last_sec != -1) {
                last_sec = -1;
                menu_status(0);
            }
            continue;
        }
        if (ticks > 0) {
            ticks--;
            int sec = (ticks + 9) / 10;
            if (sec > 0 && sec != last_sec) {
                last_sec = sec;
                menu_status(sec);           /* цифра в той же ячейке */
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
    uint64_t bi_addr, pt_addr = 0, kaddr = KERNEL_PHYS, ksize = 0;
    uint64_t map_sz, key, dsz, i, j;
    uint32_t ver;
    EFI_MEM_DESC *map = g_map;
    uint64_t *pml4, *pdpt, *pd;
    int r;

    g_st = st;

    /* ---- загрузочное меню: OC или Windows ---- */
    for (;;) {
        int sel = boot_menu();
        if (sel != 2)
            break;
        try_windows(img);       /* вернулись — Windows нет/не стартовала */
    }

    g_st->cout->SetAttribute(g_st->cout, ATTR_ITEM);
    prints(L"\r\nOC UEFI loader\r\n");

    /* ---- ядро с FAT-раздела (прошивка умеет FAT сама) ---- */
    if (bs->HandleProtocol(img, &g_loaded_image, (void **)&li) ||
        bs->HandleProtocol(li->device_handle, &g_sfs, (void **)&sfs) ||
        sfs->OpenVolume(sfs, &root) ||
        root->Open(root, &file, L"\\KERNEL.BIN", 1, 0))
        die(L"cannot open \\KERNEL.BIN");

    if (bs->AllocatePages(2 /*Address*/, 2 /*LoaderData*/,
                          (KERNEL_MAX_BYTES + 4095) / 4096, &kaddr))
        die(L"cannot allocate kernel memory");

    for (;;) {
        uint64_t chunk = 4096;
        if (ksize + chunk > KERNEL_MAX_BYTES)
            break;
        if (file->Read(file, &chunk, (uint8_t *)kaddr + ksize) || !chunk)
            break;
        ksize += chunk;
    }
    file->Close(file);
    if (ksize < 4096)
        die(L"\\KERNEL.BIN is empty");
    prints(L"kernel loaded (");
    phex(ksize);
    prints(L" bytes)\r\n");

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

    /* ---- identity-страницы 0..64 GiB (2 MiB) ---- */
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
    /* дальше только мы: страницы, GDT, вход в ядро */
    jump_kernel(KERNEL_PHYS, bi_addr, (uint64_t)pml4);
    for (;;)
        ;
    return 0;
}
