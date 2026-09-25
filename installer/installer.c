/*
 * OC Setup — установщик OC OS на USB-фешку (Windows x64).
 *
 * Выбираете флешку в списке — программа пишет туда встроенный образ OC.
 * Защита вашей основной ОС (как в scripts/flash.sh):
 *   1. системный диск (и весь физический диск с Windows) не показывается;
 *   2. диски больше 64 ГБ скрыты (похожи на винчестеры) — включаются
 *      галочкой «показать все диски» на свой страх и риск;
 *   3. двойное подтверждение перед записью.
 *
 * Сборка: make setup  (нужен ziglang: pip3 install --target .pydeps ziglang)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shellapi.h>
#include <stdio.h>

#include "oc_img.h"

#define MAX_SAFE_DISK   (64ULL << 30) /* 64 ГБ — дальше это уже не флешка */

#define IDC_LIST        101
#define IDC_REFRESH     102
#define IDC_INSTALL     103
#define IDC_SHOWALL     104
#define IDC_STATUS      105

#define MAX_TARGETS     24

typedef struct {
    wchar_t   letter;               /* 'F' */
    unsigned  disk;                 /* номер физического диска */
    unsigned long long size;        /* размер физического диска, байт */
    UINT      type;                 /* DRIVE_REMOVABLE / DRIVE_FIXED */
    wchar_t   label[64];
} Target;

/* структуры IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS (с выравниванием) */
typedef struct {
    DWORD         DiskNumber;
    DWORD         _pad;
    LARGE_INTEGER StartingOffset;
    LARGE_INTEGER ExtentLength;
} DiskExtent;                       /* 24 байта */

typedef struct {
    DWORD      NumberOfDiskExtents;
    DWORD      _pad;
    DiskExtent Extents[8];
} DiskExtents;

static HINSTANCE g_hInst;
static HWND      g_hList, g_hStatus, g_hShowAll;
static Target    g_targets[MAX_TARGETS];
static int       g_ntargets;
static unsigned char *g_img;        /* восстановленный образ, 2 МиБ */

/* ---------------------------------------------------------- безопасность */

static int is_elevated(void)
{
    TOKEN_ELEVATION te;
    DWORD len = 0;
    HANDLE tok = NULL;
    int ok = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenElevation, &te, sizeof te, &len))
            ok = te.TokenIsElevated;
        CloseHandle(tok);
    }
    return ok;
}

static void relaunch_as_admin(void)
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    if ((INT_PTR)ShellExecuteW(NULL, L"runas", path, NULL, NULL,
                               SW_SHOWNORMAL) <= 32) {
        MessageBoxW(NULL,
            L"Установке нужны права администратора (чтобы записать образ "
            L"на флешку).\nНажмите «Да» в запросе контроля учётных записей.",
            L"OC Setup", MB_ICONWARNING);
    }
}

/* Буква системного диска (где Windows) — туда НЕ пишем никогда. */
static wchar_t windows_letter(void)
{
    wchar_t buf[MAX_PATH];
    if (GetWindowsDirectoryW(buf, MAX_PATH))
        return buf[0];
    return L'C';
}

/* Буква -> номер физического диска. 1 = успех. Том на нескольких дисках —
 * отказ (небезопасно). */
static int disk_for_letter(wchar_t letter, unsigned *disk)
{
    DiskExtents info;
    DWORD br = 0;
    HANDLE h;
    wchar_t path[8];

    path[0] = L'\\'; path[1] = L'\\'; path[2] = L'.'; path[3] = L'\\';
    path[4] = letter; path[5] = L':'; path[6] = 0;
    h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
                         &info, sizeof info, &br, NULL)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    if (info.NumberOfDiskExtents != 1)
        return 0;
    *disk = info.Extents[0].DiskNumber;
    return 1;
}

static unsigned long long physical_disk_size(unsigned disk)
{
    wchar_t path[32];
    LARGE_INTEGER len;
    DWORD br = 0;
    HANDLE h;
    swprintf(path, 32, L"\\\\.\\PhysicalDrive%u", disk);
    h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                         &len, sizeof len, &br, NULL))
        len.QuadPart = 0;
    CloseHandle(h);
    return (unsigned long long)len.QuadPart;
}

static void volume_label(wchar_t letter, wchar_t *out, int n)
{
    wchar_t root[4] = { letter, L':', L'\\', 0 };
    out[0] = 0;
    GetVolumeInformationW(root, out, (DWORD)n, NULL, NULL, NULL, NULL, 0);
}

/* ---------------------------------------------------------- список дисков */

static void fill_list(void)
{
    wchar_t buf[512], line[256];
    wchar_t sys = windows_letter();
    unsigned sys_disk = 0;
    int show_all, i, sel = -1;

    show_all = (SendMessageW(g_hShowAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
    disk_for_letter(sys, &sys_disk);

    g_ntargets = 0;
    SendMessageW(g_hList, CB_RESETCONTENT, 0, 0);
    GetLogicalDriveStringsW(511, buf);

    for (i = 0; buf[i] || (i && buf[i - 1]); ) {
        wchar_t *p = &buf[i];
        wchar_t letter = p[0];
        UINT type;
        unsigned disk = 0;
        unsigned long long size;
        Target *t;

        /* шаг до следующей строки "X:\\" */
        while (buf[i])
            i++;
        i++;

        type = GetDriveTypeW(p);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED)
            continue;
        if (letter == sys)
            continue;
        if (!disk_for_letter(letter, &disk))
            continue;
        if (disk == sys_disk) /* тот же физический диск, что и Windows */
            continue;
        size = physical_disk_size(disk);
        if (!show_all && size > MAX_SAFE_DISK)
            continue; /* похоже на винчестер — прячем */

        t = &g_targets[g_ntargets];
        t->letter = letter;
        t->disk = disk;
        t->size = size;
        t->type = type;
        volume_label(letter, t->label, 64);

        if (size)
            swprintf(line, 256, L"%c:  «%s»  %u,%01llu ГБ  — %s",
                     letter,
                     t->label[0] ? t->label : L"без метки",
                     (unsigned)(size >> 30),
                     (unsigned)((size * 10 / (1ULL << 30)) % 10),
                     type == DRIVE_REMOVABLE
                         ? L"съёмный диск (USB-флешка)"
                         : L"⚠ возможно жёсткий диск!");
        else
            swprintf(line, 256, L"%c:  «%s»  (размер неизвестен)  — %s",
                     letter,
                     t->label[0] ? t->label : L"без метки",
                     type == DRIVE_REMOVABLE
                         ? L"съёмный диск (USB-флешка)"
                         : L"⚠ возможно жёсткий диск!");

        i = (int)SendMessageW(g_hList, CB_ADDSTRING, 0, (LPARAM)line);
        SendMessageW(g_hList, CB_SETITEMDATA, i, g_ntargets);
        if (sel < 0 && type == DRIVE_REMOVABLE)
            sel = i; /* по умолчанию — первая флешка */
        g_ntargets++;
    }

    if (g_ntargets == 0) {
        SendMessageW(g_hList, CB_ADDSTRING, 0,
                     (LPARAM)L"(подходящих флешек не найдено)");
        SetWindowTextW(g_hStatus,
            L"Вставьте USB-флешку и нажмите «Обновить». Системные и большие "
            L"диски скрыты для вашей безопасности.");
    } else {
        if (sel < 0)
            sel = 0;
        SendMessageW(g_hList, CB_SETCURSEL, sel, 0);
        SetWindowTextW(g_hStatus,
            L"Образ OC 0.1 (2 МиБ) уже внутри программы. Выберите флешку и "
            L"нажмите «Установить».");
    }
}

/* ---------------------------------------------------------- запись образа */

static int write_image(unsigned disk, wchar_t *err, int errn)
{
    wchar_t path[32];
    HANDLE h;
    DWORD wr = 0;

    swprintf(path, 32, L"\\\\.\\PhysicalDrive%u", disk);
    h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        swprintf(err, errn,
                 L"Не удалось открыть диск %u для записи (код ошибки %lu).\n"
                 L"Попробуйте запустить программу от имени администратора.",
                 disk, GetLastError());
        return 0;
    }
    oc_image_rebuild(g_img);
    if (!WriteFile(h, g_img, OC_IMG_SIZE, &wr, NULL) || wr != OC_IMG_SIZE) {
        swprintf(err, errn, L"Ошибка записи на диск %u (код %lu).",
                 disk, GetLastError());
        CloseHandle(h);
        return 0;
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    return 1;
}

static void do_install(void)
{
    int sel = (int)SendMessageW(g_hList, CB_GETCURSEL, 0, 0);
    int tid = (int)SendMessageW(g_hList, CB_GETITEMDATA, sel, 0);
    Target *t;
    wchar_t msg[512], err[256];
    wchar_t sys;
    unsigned sys_disk = 0;

    if (sel < 0 || tid < 0 || tid >= g_ntargets) {
        MessageBoxW(NULL, L"Сначала выберите флешку в списке.",
                    L"OC Setup", MB_ICONINFORMATION);
        return;
    }
    t = &g_targets[tid];

    /* страховка: системный диск — отказ без вариантов */
    sys = windows_letter();
    disk_for_letter(sys, &sys_disk);
    if (t->letter == sys || t->disk == sys_disk) {
        MessageBoxW(NULL,
            L"⛔ Это системный диск — запись запрещена!\n"
            L"Ваша основная ОС останется нетронутой.",
            L"OC Setup", MB_ICONERROR);
        return;
    }

    /* подтверждение 1 из 2 */
    swprintf(msg, 512,
        L"⚠️ ВНИМАНИЕ!\n\n"
        L"Устройство: %c: %s«%s» — %u,%01llu ГБ (физический диск %u)\n\n"
        L"ВСЁ СОДЕРЖИМОЕ ЭТОГО УСТРОЙСТВА БУДЕТ НЕОБРАТИМО УНИЧТОЖЕНО.\n\n"
        L"Это точно ваша USB-флешка?",
        t->letter,
        t->type == DRIVE_REMOVABLE ? L"(съёмный) " : L"",
        t->label[0] ? t->label : L"без метки",
        (unsigned)(t->size >> 30),
        (unsigned)((t->size * 10 / (1ULL << 30)) % 10),
        t->disk);
    if (MessageBoxW(NULL, msg, L"Подтверждение 1 из 2",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    /* подтверждение 2 из 2 */
    if (t->type != DRIVE_REMOVABLE) {
        swprintf(msg, 512,
            L"⛔ ПОСЛЕДНЕЕ ПРЕДУПРЕЖДЕНИЕ\n\n"
            L"Диск %c: помечен как ЛОКАЛЬНЫЙ ЖЁСТКИЙ, а не флешка.\n"
            L"Если это внешний винчестер или второй диск — нажмите «НЕТ».\n"
            L"Всё на диске %u будет стёрто, включая любые ОС на нём.\n\n"
            L"Записать OC OS на диск %u?",
            t->letter, t->disk, t->disk);
    } else {
        swprintf(msg, 512,
            L"Последняя проверка.\n\n"
            L"Записать OC OS 0.1 на флешку %c: (физический диск %u)?\n"
            L"«Да» — записать. «Нет» — отмена.",
            t->letter, t->disk);
    }
    if (MessageBoxW(NULL, msg, L"Подтверждение 2 из 2",
                    MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2) != IDYES)
        return;

    SetWindowTextW(g_hStatus, L"Пишу образ OC на флешку…");
    if (!write_image(t->disk, err, 256)) {
        MessageBoxW(NULL, err, L"OC Setup", MB_ICONERROR);
        SetWindowTextW(g_hStatus, L"Запись не удалась.");
        return;
    }

    swprintf(msg, 512,
        L"✅ Готово! OC OS записана на %c: (диск %u).\n\n"
        L"Как запустить:\n"
        L"1. Перезагрузитесь и войдите в Boot Menu (F12, F10 или Esc при "
        L"старте).\n"
        L"2. Выберите загрузку с USB — откроется шелл «oc> ».\n\n"
        L"❗ Если Windows предложит «отформатировать диск» — нажмите "
        L"«Отмена»: там наша система, Windows её просто не понимает.\n\n"
        L"Команды: помощь, инфо, память, тест, об. Русская раскладка — "
        L"Ctrl+Space.",
        t->letter, t->disk);
    MessageBoxW(NULL, msg, L"Установка завершена", MB_ICONINFORMATION);
    SetWindowTextW(g_hStatus,
        L"Готово! Перезагрузитесь и загрузитесь с флешки (Boot Menu: F12/Esc).");
}

/* ---------------------------------------------------------- окно */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_REFRESH) {
            fill_list();
            return 0;
        }
        if (LOWORD(wp) == IDC_SHOWALL) {
            if (SendMessageW(g_hShowAll, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                if (MessageBoxW(NULL,
                    L"Показать ВСЕ диски, включая большие?\n\n"
                    L"Запись на случайный диск уничтожит на нём всё "
                    L"(в т.ч. установленную ОС). Продолжить?",
                    L"⚠ Опасный режим",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    SendMessageW(g_hShowAll, BM_SETCHECK, BST_UNCHECKED, 0);
                }
            }
            fill_list();
            return 0;
        }
        if (LOWORD(wp) == IDC_INSTALL) {
            do_install();
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND mk_ctrl(HWND parent, const wchar_t *cls, const wchar_t *text,
                    DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, w, h, parent, (HMENU)(INT_PTR)id,
                           g_hInst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 1);
    return c;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;

    (void)prev; (void)cmd; (void)show;
    SetProcessDPIAware();
    g_hInst = hInst;

    if (!is_elevated()) {
        relaunch_as_admin();
        return 0;
    }

    g_img = (unsigned char *)VirtualAlloc(NULL, OC_IMG_SIZE,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
    if (!g_img) {
        MessageBoxW(NULL, L"Не хватает памяти (нужно 2 МиБ).",
                    L"OC Setup", MB_ICONERROR);
        return 1;
    }

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"OCSetupWnd";
    RegisterClassW(&wc);

    hwnd = CreateWindowW(L"OCSetupWnd",
        L"Установка OC OS 0.1 «Двухсотметровка» на флешку",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 400,
        NULL, NULL, hInst, NULL);

    mk_ctrl(hwnd, L"STATIC",
        L"Шаг 1 — выберите флешку   >   Шаг 2 — подтвердите   >   Шаг 3 — запись",
        0, 16, 10, 570, 20, 0);
    mk_ctrl(hwnd, L"STATIC",
        L"Выберите, куда установить OC (нужна USB-флешка; всё на ней "
        L"будет удалено):",
        0, 16, 34, 570, 20, 0);
    g_hList = mk_ctrl(hwnd, L"COMBOBOX", NULL,
        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        16, 58, 570, 240, IDC_LIST);
    mk_ctrl(hwnd, L"BUTTON", L"Обновить список",
        BS_PUSHBUTTON | WS_TABSTOP, 16, 98, 150, 30, IDC_REFRESH);
    mk_ctrl(hwnd, L"BUTTON", L"Установить на выбранный диск",
        BS_DEFPUSHBUTTON | WS_TABSTOP, 180, 98, 250, 30, IDC_INSTALL);
    g_hShowAll = mk_ctrl(hwnd, L"BUTTON", L"показать все диски (опасно)",
        BS_AUTOCHECKBOX | WS_TABSTOP, 16, 144, 280, 24, IDC_SHOWALL);
    mk_ctrl(hwnd, L"STATIC",
        L"🛡 Ваша защита: системный диск и большие диски не показываются;\n"
        L"перед записью — двойное подтверждение. Программа пишет ТОЛЬКО на\n"
        L"выбранный вами диск.",
        0, 16, 178, 570, 48, 0);
    g_hStatus = mk_ctrl(hwnd, L"STATIC", L"",
        SS_SUNKEN | SS_LEFT, 16, 288, 570, 48, IDC_STATUS);

    fill_list();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
