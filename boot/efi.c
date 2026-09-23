/*
 * OC OS — UEFI-загрузчик (\\EFI\\BOOT\\BOOTX64.EFI).
 *
 * Для современных ПК без Legacy-режима (как ваш HP). Прошивка UEFI сама
 * находит этот файл на FAT-разделе флешки и запускает его.
 *
 * Что делает: читает \KERNEL.BIN через SimpleFileSystem (FAT — прошивка),
 * собирает bootinfo (E820 из карты памяти UEFI, кадр из GOP), строит
 * identity-страницы 0..64 GiB (2 MiB страницы) и входит в ядро.
 * Записи на диск тут тоже НЕТ — только чтение, ваша система в безопасности.
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

typedef struct {
    uint32_t type;
    uint32_t _pad;
    uint64_t phys, virt, pages, attr;
} EFI_MEM_DESC;

typedef struct EFI_TXT EFI_TXT;
struct EFI_TXT {
    void *Reset;
    int64_t (EFIAPI *OutputString)(EFI_TXT *, const uint16_t *);
    void *TestString, *QueryMode, *SetMode, *SetAttribute, *ClearScreen;
    void *SetCursorPosition, *EnableCursor, *Mode;
};

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
    uint32_t ver, w, h, fmt;    /* fmt: 0 RGBX, 1 BGRX, 2 маски, 3 только BLT */
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
    void *Reserved, *RegisterProtocolNotify, *LocateHandle;
    void *LocateDevicePath, *InstallConfigurationTable;
    void *LoadImage, *StartImage, *Exit, *UnloadImage;
    int64_t (EFIAPI *ExitBootServices)(EFI_HANDLE, uint64_t);
    void *GetNextMonotonicCount, *Stall, *SetWatchdogTimer;
    void *ConnectController, *DisconnectController, *OpenProtocol;
    void *CloseProtocol, *OpenProtocolInformation, *ProtocolsPerHandle;
    void *LocateHandleBuffer;
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
    EFI_HANDLE cin_h;   void *cin;
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
    prints(L"OC UEFI loader\r\n");

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
