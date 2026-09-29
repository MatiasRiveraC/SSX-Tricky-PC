/*
 * launcher.c -- the window the game opens with (part 183).
 *
 * Start game is enabled only once a disc image has been chosen in Settings
 * and checked to be the build this executable was recompiled from. Settings
 * holds the disc image, the folder used as the Xbox hard disk (saves), the
 * render resolution, 4:3 or 16:9, and fullscreen; they are written to an
 * .ini beside the executable.
 *
 * Plain Win32 with themed common controls, so it needs nothing beyond what
 * Windows ships. Keyboard (Tab, Enter, Esc) and a controller (D-pad moves,
 * A presses, Start starts) both work on the main window.
 */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <xinput.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "launcher.h"
#include "kernel/xbox_xdvdfs.h"

/* ── Settings file ─────────────────────────────────────────────────── */

typedef struct { int w, h; } Res;

/* Render sizes offered, per shape. The first 4:3 entry is the console's own. */
static const Res k_res43[] = {
    { 640, 480 }, { 960, 720 }, { 1280, 960 }, { 1440, 1080 },
    { 1920, 1440 }, { 2560, 1920 }, { 2880, 2160 },
};
static const Res k_res169[] = {
    { 854, 480 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 },
    { 2560, 1440 }, { 3840, 2160 },
};
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static uint32_t s_expected_entry = 0;

void launcher_init(uint32_t expected_entry_point)
{
    s_expected_entry = expected_entry_point;
}

static void exe_dir(char *out, size_t n)
{
    DWORD k = GetModuleFileNameA(NULL, out, (DWORD)n);
    char *slash;
    if (k == 0 || k >= n) { out[0] = '\0'; return; }
    slash = strrchr(out, '\\');
    if (slash) *slash = '\0';
}

void launcher_config_path(char *out, size_t out_sz)
{
    char exe[MAX_PATH];
    char *dot;
    DWORD k = GetModuleFileNameA(NULL, exe, (DWORD)sizeof exe);
    if (k == 0 || k >= sizeof exe) { snprintf(out, out_sz, "settings.ini"); return; }
    dot = strrchr(exe, '.');
    if (dot && !strchr(dot, '\\')) *dot = '\0';
    snprintf(out, out_sz, "%s.ini", exe);
}

/* The largest preset of the given shape whose window fits the primary
 * monitor's work area: a sensible first-run default. */
static void default_resolution(int widescreen, int *w, int *h)
{
    const Res *list = widescreen ? k_res169 : k_res43;
    int n = widescreen ? COUNT(k_res169) : COUNT(k_res43);
    RECT work, frame = { 0, 0, 0, 0 };
    int i, best = 0;

    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    for (i = 0; i < n; i++) {
        if (list[i].w + (frame.right - frame.left) <= work.right - work.left &&
            list[i].h + (frame.bottom - frame.top) <= work.bottom - work.top)
            best = i;
    }
    *w = list[best].w;
    *h = list[best].h;
}

static void config_defaults(LauncherConfig *c)
{
    memset(c, 0, sizeof *c);
    strcpy(c->hdd, "hdd");
    c->widescreen = 0;
    c->fullscreen = 0;
    c->aniso = 0;
    c->msaa = 1;
    c->show_fps = 0;
    c->log_file = 0;
    default_resolution(0, &c->width, &c->height);
    controls_defaults(&c->controls);
}

void launcher_log_path(char *out, size_t out_sz)
{
    char ini[MAX_PATH];
    size_t n;
    launcher_config_path(ini, sizeof ini);
    n = strlen(ini);
    if (n > 4 && !_stricmp(ini + n - 4, ".ini")) ini[n - 4] = '\0';
    snprintf(out, out_sz, "%s.log", ini);
}

void launcher_hdd_path(const LauncherConfig *cfg, char *out, size_t out_sz)
{
    const char *p = cfg->hdd[0] ? cfg->hdd : "hdd";
    char joined[MAX_PATH * 2];
    if ((p[0] && p[1] == ':') || (p[0] == '\\' && p[1] == '\\')) {
        snprintf(joined, sizeof joined, "%s", p);
    } else {
        char base[MAX_PATH];
        exe_dir(base, sizeof base);
        snprintf(joined, sizeof joined, "%s\\%s", base, p);
    }
    if (!GetFullPathNameA(joined, (DWORD)out_sz, out, NULL))
        snprintf(out, out_sz, "%s", joined);
}

void launcher_open_path(HWND owner, const char *path, BOOL folder)
{
    WCHAR w[MAX_PATH];
    if (folder) CreateDirectoryA(path, NULL);
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, w, MAX_PATH)) return;
    if ((INT_PTR)ShellExecuteW(owner, folder ? L"explore" : L"open", w, NULL, NULL, SW_SHOWNORMAL) <= 32) {
        WCHAR msg[MAX_PATH + 64];
        swprintf(msg, MAX_PATH + 64, L"Could not open:\n%ls", w);
        MessageBoxW(owner, msg, L"SSX Tricky", MB_ICONWARNING);
    }
}

void launcher_config_load(LauncherConfig *cfg)
{
    char ini[MAX_PATH], buf[64];
    int w = 0, h = 0, v;

    config_defaults(cfg);
    launcher_config_path(ini, sizeof ini);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) return;

    GetPrivateProfileStringA("Game", "DiscImage", "", cfg->iso, sizeof cfg->iso, ini);
    GetPrivateProfileStringA("Game", "SaveFolder", "hdd", cfg->hdd, sizeof cfg->hdd, ini);
    if (!cfg->hdd[0]) strcpy(cfg->hdd, "hdd");

    GetPrivateProfileStringA("Display", "AspectRatio", "4:3", buf, sizeof buf, ini);
    cfg->widescreen = (strcmp(buf, "16:9") == 0);
    GetPrivateProfileStringA("Display", "Resolution", "", buf, sizeof buf, ini);
    if (sscanf(buf, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240 && w <= 7680 && h <= 4320) {
        cfg->width = w;
        cfg->height = h;
    } else {
        default_resolution(cfg->widescreen, &cfg->width, &cfg->height);
    }
    cfg->fullscreen = GetPrivateProfileIntA("Display", "Fullscreen", 0, ini) != 0;
    v = GetPrivateProfileIntA("Display", "AnisotropicFiltering", 0, ini);
    cfg->aniso = (v == 2 || v == 4 || v == 8 || v == 16) ? v : 0;
    v = GetPrivateProfileIntA("Display", "AntiAliasing", 1, ini);
    cfg->msaa = (v == 2 || v == 4 || v == 8) ? v : 1;
    cfg->show_fps = GetPrivateProfileIntA("Display", "ShowFrameRate", 0, ini) != 0;
    cfg->log_file = GetPrivateProfileIntA("Troubleshooting", "LogFile", 0, ini) != 0;
    controls_load(&cfg->controls, ini);
}

/* Written whole, with comments, so the file explains itself to anyone who
 * opens it; the Get/WritePrivateProfile API cannot write comments. */
BOOL launcher_config_save(const LauncherConfig *cfg)
{
    char ini[MAX_PATH], tmp[MAX_PATH + 8];
    FILE *f;

    launcher_config_path(ini, sizeof ini);
    snprintf(tmp, sizeof tmp, "%s.new", ini);
    f = fopen(tmp, "w");
    if (!f) return FALSE;
    fprintf(f,
        "; SSX Tricky (recompiled) settings, written by the launcher and the game\n"
        "; window's menu. Safe to edit by hand while the game is closed.\n"
        "\n"
        "[Game]\n"
        "; Xbox disc image (.iso) of SSX Tricky (USA). Start game stays disabled\n"
        "; until this is set.\n"
        "DiscImage=%s\n"
        "; Folder used as the Xbox hard disk (game saves). A relative path is\n"
        "; relative to the folder the game is in.\n"
        "SaveFolder=%s\n"
        "\n"
        "[Display]\n"
        "; Render resolution, WIDTHxHEIGHT.\n"
        "Resolution=%dx%d\n"
        "; 4:3, or 16:9 to use the game's own widescreen mode.\n"
        "AspectRatio=%s\n"
        "; 1 = borderless fullscreen. Alt+Enter switches while playing.\n"
        "Fullscreen=%d\n"
        "; 0 = the game's own texture filtering, or 2, 4, 8, 16 (anisotropic).\n"
        "AnisotropicFiltering=%d\n"
        "; Samples per pixel: 1 (off), 2, 4 or 8.\n"
        "AntiAliasing=%d\n"
        "; 1 = frame rate in the title bar.\n"
        "ShowFrameRate=%d\n"
        "\n"
        "[Troubleshooting]\n"
        "; 1 = write everything the game reports to a .log file beside it.\n"
        "LogFile=%d\n",
        cfg->iso, cfg->hdd, cfg->width, cfg->height,
        cfg->widescreen ? "16:9" : "4:3", cfg->fullscreen ? 1 : 0,
        cfg->aniso, cfg->msaa, cfg->show_fps ? 1 : 0, cfg->log_file ? 1 : 0);
    controls_write(&cfg->controls, f);
    if (fclose(f) != 0) { DeleteFileA(tmp); return FALSE; }
    if (!MoveFileExA(tmp, ini, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp);
        return FALSE;
    }
    return TRUE;
}

/* ── Disc image check ──────────────────────────────────────────────── */

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

BOOL launcher_check_iso(const char *path, char *why, size_t why_sz)
{
    unsigned char hdr[0x1000];
    uint32_t sector = 0, size = 0, entry;
    uint8_t attrs = 0;
    DWORD fa;

    if (!path || !path[0]) {
        snprintf(why, why_sz, "No disc image has been chosen.");
        return FALSE;
    }
    fa = GetFileAttributesA(path);
    if (fa == INVALID_FILE_ATTRIBUTES || (fa & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(why, why_sz, "The disc image could not be found: %s", path);
        return FALSE;
    }
    if (xdvdfs_is_mounted()) xdvdfs_unmount();
    if (!xdvdfs_mount(path)) {
        snprintf(why, why_sz, "This file is not an Xbox disc image.");
        return FALSE;
    }
    memset(hdr, 0, sizeof hdr);
    if (!xdvdfs_find("default.xbe", &sector, &size, &attrs) ||
        (attrs & XDVDFS_ATTR_DIRECTORY) || size < 0x200 ||
        xdvdfs_read(sector, size, 0, hdr, size < sizeof hdr ? size : sizeof hdr) < 0x200) {
        xdvdfs_unmount();
        snprintf(why, why_sz, "The disc image has no game on it (no default.xbe).");
        return FALSE;
    }
    xdvdfs_unmount();

    if (memcmp(hdr, "XBEH", 4) != 0) {
        snprintf(why, why_sz, "The game on this disc image is damaged (bad default.xbe).");
        return FALSE;
    }
    /* Retail images XOR the entry point with 0xA8FC57AB, debug ones with
     * 0x94859D4B; either is accepted if it lands on the expected address. */
    entry = rd32(hdr + 0x128);
    if (s_expected_entry &&
        (entry ^ 0xA8FC57ABu) != s_expected_entry &&
        (entry ^ 0x94859D4Bu) != s_expected_entry) {
        char title[41] = "another game";
        uint32_t base = rd32(hdr + 0x104), cert = rd32(hdr + 0x118) - base;
        if (cert + 0x0C + 80 <= sizeof hdr) {
            int i;
            for (i = 0; i < 40; i++) {
                unsigned c = hdr[cert + 0x0C + i * 2] | (hdr[cert + 0x0D + i * 2] << 8);
                if (!c) break;
                title[i] = (c >= 32 && c < 127) ? (char)c : '?';
            }
            if (i) title[i] = '\0';
        }
        snprintf(why, why_sz,
                 "This disc image is %s. This build runs only SSX Tricky (USA).",
                 title);
        return FALSE;
    }
    return TRUE;
}

/* ── DPI ───────────────────────────────────────────────────────────── */

typedef BOOL (WINAPI *SetDpiCtxFn)(HANDLE);
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

void launcher_enable_dpi_awareness(void)
{
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SetDpiCtxFn set = u ? (SetDpiCtxFn)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
    if (!set || !set((HANDLE)(INT_PTR)-4))
        SetProcessDPIAware();
}

static UINT window_dpi(HWND h)
{
    static GetDpiForWindowFn fn = NULL;
    static int looked = 0;
    if (!looked) {
        HMODULE u = GetModuleHandleW(L"user32.dll");
        fn = u ? (GetDpiForWindowFn)(void *)GetProcAddress(u, "GetDpiForWindow") : NULL;
        looked = 1;
    }
    if (fn && h) { UINT d = fn(h); if (d) return d; }
    {
        HDC dc = GetDC(NULL);
        int d = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(NULL, dc);
        return d > 0 ? (UINT)d : 96;
    }
}

/* ── Shared UI helpers ─────────────────────────────────────────────── */

#define CLR_HEADER     RGB(13, 38, 66)
#define CLR_HEADER_SUB RGB(146, 186, 226)
#define CLR_ACCENT     RGB(236, 128, 36)
#define CLR_TEXT       RGB(28, 32, 38)
#define CLR_MUTED      RGB(96, 104, 116)
#define CLR_ERROR      RGB(186, 32, 32)
#define CLR_OK         RGB(26, 120, 60)

typedef struct {
    UINT  dpi;
    HFONT body, bold, title, sub, big;
} Fonts;

static int S(const Fonts *f, int v) { return MulDiv(v, (int)f->dpi, 96); }

static HFONT make_font(UINT dpi, int pt, int weight)
{
    return CreateFontW(-MulDiv(pt, (int)dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void fonts_free(Fonts *f)
{
    if (f->body)  DeleteObject(f->body);
    if (f->bold)  DeleteObject(f->bold);
    if (f->title) DeleteObject(f->title);
    if (f->sub)   DeleteObject(f->sub);
    if (f->big)   DeleteObject(f->big);
    memset(f, 0, sizeof *f);
}

static void fonts_make(Fonts *f, UINT dpi)
{
    fonts_free(f);
    f->dpi   = dpi;
    f->body  = make_font(dpi, 10, FW_NORMAL);
    f->bold  = make_font(dpi, 10, FW_SEMIBOLD);
    f->title = make_font(dpi, 24, FW_BOLD);
    f->sub   = make_font(dpi, 10, FW_NORMAL);
    f->big   = make_font(dpi, 12, FW_SEMIBOLD);
}

static void place(HWND parent, int id, const Fonts *f, int x, int y, int w, int h, HFONT font)
{
    HWND c = GetDlgItem(parent, id);
    if (!c) return;
    SetWindowPos(c, NULL, S(f, x), S(f, y), S(f, w), S(f, h), SWP_NOZORDER | SWP_NOACTIVATE);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
}

static HWND child(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int id)
{
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(NULL), NULL);
}

/* Resize a window so its client area is w x h DIPs, centred on its monitor. */
static void size_and_center(HWND h, const Fonts *f, int w, int hgt)
{
    RECT r = { 0, 0, S(f, w), S(f, hgt) };
    MONITORINFO mi;
    DWORD style = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    DWORD ex = (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE);
    int ww, wh;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    ww = r.right - r.left;
    wh = r.bottom - r.top;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(h, NULL,
                 mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2,
                 mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2,
                 ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void to_wide(const char *s, WCHAR *w, int n)
{
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, w, n)) w[0] = 0;
}

/* The runtime opens files through the ANSI API, so a path must survive the
 * conversion. If it does not (characters outside the code page), the short
 * 8.3 form usually does; failing that the path is refused. */
static BOOL to_path(const WCHAR *w, char *out, size_t n)
{
    BOOL lossy = FALSE;
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w, -1, out, (int)n, NULL, &lossy))
        return FALSE;
    if (lossy) {
        WCHAR sh[MAX_PATH];
        lossy = FALSE;
        if (!GetShortPathNameW(w, sh, MAX_PATH) ||
            !WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, sh, -1, out, (int)n, NULL, &lossy) ||
            lossy)
            return FALSE;
    }
    return TRUE;
}

static void file_name_of(const char *path, char *out, size_t n)
{
    const char *s = strrchr(path, '\\');
    const char *t = strrchr(path, '/');
    if (t && (!s || t > s)) s = t;
    snprintf(out, n, "%s", s ? s + 1 : path);
}

/* A modern file or folder picker, starting in `start` (or the game's
 * folder). Returns FALSE if the player cancelled. */
static BOOL pick_path(HWND owner, BOOL folder, const WCHAR *title,
                      const char *start, char *out, size_t n)
{
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL, *dir = NULL;
    PWSTR wpath = NULL;
    BOOL ok = FALSE;
    DWORD opts = 0;
    WCHAR wstart[MAX_PATH];
    char s[MAX_PATH];

    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&dlg)) || !dlg)
        return FALSE;
    IFileOpenDialog_GetOptions(dlg, &opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folder) opts |= FOS_PICKFOLDERS;
    else        opts |= FOS_FILEMUSTEXIST;
    IFileOpenDialog_SetOptions(dlg, opts);
    IFileOpenDialog_SetTitle(dlg, title);
    if (!folder) {
        static const COMDLG_FILTERSPEC types[] = {
            { L"Xbox disc image (*.iso)", L"*.iso" },
            { L"All files (*.*)",         L"*.*" },
        };
        IFileOpenDialog_SetFileTypes(dlg, 2, types);
    }

    /* Start where the current choice is, else beside the game. */
    s[0] = '\0';
    if (start && start[0]) {
        char full[MAX_PATH];
        char *slash;
        if (!(start[0] && start[1] == ':') && !(start[0] == '\\' && start[1] == '\\')) {
            char base[MAX_PATH];
            exe_dir(base, sizeof base);
            snprintf(full, sizeof full, "%s\\%s", base, start);
        } else {
            snprintf(full, sizeof full, "%s", start);
        }
        snprintf(s, sizeof s, "%s", full);
        if (!folder && (slash = strrchr(s, '\\')) != NULL) *slash = '\0';
    }
    if (!s[0] || GetFileAttributesA(s) == INVALID_FILE_ATTRIBUTES)
        exe_dir(s, sizeof s);
    to_wide(s, wstart, MAX_PATH);
    if (SUCCEEDED(SHCreateItemFromParsingName(wstart, NULL, &IID_IShellItem, (void **)&dir)) && dir) {
        IFileOpenDialog_SetFolder(dlg, dir);
        IShellItem_Release(dir);
    }

    if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner)) &&
        SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item)) && item) {
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wpath)) && wpath) {
            if (to_path(wpath, out, n)) ok = TRUE;
            else MessageBoxW(owner, L"That path contains characters the game cannot open.\n"
                                    L"Move the file to a folder with a plain name and try again.",
                             L"SSX Tricky", MB_ICONWARNING);
            CoTaskMemFree(wpath);
        }
        IShellItem_Release(item);
    }
    IFileOpenDialog_Release(dlg);
    return ok;
}

/* ── Settings window ───────────────────────────────────────────────── */

enum {
    ID_S_ISO_LABEL = 200, ID_S_ISO, ID_S_ISO_BROWSE,
    ID_S_HDD_LABEL, ID_S_HDD, ID_S_HDD_BROWSE, ID_S_HDD_OPEN,
    ID_S_GAME_HEAD, ID_S_DISPLAY_HEAD, ID_S_MORE_HEAD,
    ID_S_ASPECT_LABEL, ID_S_ASPECT, ID_S_RES_LABEL, ID_S_RES,
    ID_S_TEX_LABEL, ID_S_TEX, ID_S_AA_LABEL, ID_S_AA,
    ID_S_FULLSCREEN, ID_S_FPS, ID_S_NOTE,
    ID_S_CONTROLS, ID_S_LOG, ID_S_LOG_OPEN,
    ID_S_STATUS,
};

/* Choices for the two filtering combos, in list order. */
static const int k_aniso[] = { 0, 2, 4, 8, 16 };
static const int k_msaa[]  = { 1, 2, 4, 8 };

typedef struct {
    HWND  hwnd;
    Fonts fonts;
    LauncherConfig cfg;     /* edited copy */
    BOOL  done, saved;
    int   status_level;     /* 0 hint, 1 ok, 2 error */
} SettingsUI;

static SettingsUI *s_settings;

static void settings_fill_resolutions(SettingsUI *ui, int keep_w, int keep_h)
{
    HWND cb = GetDlgItem(ui->hwnd, ID_S_RES);
    int ws = (int)SendMessageW(GetDlgItem(ui->hwnd, ID_S_ASPECT), CB_GETCURSEL, 0, 0) == 1;
    const Res *list = ws ? k_res169 : k_res43;
    int n = ws ? COUNT(k_res169) : COUNT(k_res43);
    int i, sel = -1, best = 0, bestd = 0x7FFFFFFF;

    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < n; i++) {
        WCHAR label[64];
        const WCHAR *tag = L"";
        if (!ws && i == 0) tag = L"  (original)";
        else if (list[i].h == 1080 && list[i].w == 1920) tag = L"  (1080p)";
        else if (list[i].h == 720) tag = L"  (720p)";
        else if (list[i].h == 1440 && ws) tag = L"  (1440p)";
        else if (list[i].h == 2160) tag = L"  (4K)";
        swprintf(label, 64, L"%d × %d%ls", list[i].w, list[i].h, tag);
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)label);
        if (list[i].w == keep_w && list[i].h == keep_h) sel = i;
        {
            int d = abs(list[i].h - keep_h);
            if (d < bestd) { bestd = d; best = i; }
        }
    }
    /* Switching shape keeps the height where it can (1440x1080 <-> 1920x1080). */
    SendMessageW(cb, CB_SETCURSEL, sel >= 0 ? sel : best, 0);
}

static void settings_set_status(SettingsUI *ui, const char *msg, int level)
{
    WCHAR w[512];
    to_wide(msg, w, 512);
    ui->status_level = level;
    SetDlgItemTextW(ui->hwnd, ID_S_STATUS, w);
    InvalidateRect(GetDlgItem(ui->hwnd, ID_S_STATUS), NULL, TRUE);
}

static void settings_check_iso_field(SettingsUI *ui)
{
    WCHAR w[MAX_PATH];
    char path[MAX_PATH], why[512];
    GetDlgItemTextW(ui->hwnd, ID_S_ISO, w, MAX_PATH);
    if (!w[0]) { settings_set_status(ui, "Choose your SSX Tricky disc image to enable Start game.", 0); return; }
    if (!to_path(w, path, sizeof path)) { settings_set_status(ui, "The disc image path contains characters the game cannot open.", 2); return; }
    if (launcher_check_iso(path, why, sizeof why))
        settings_set_status(ui, "Disc image OK: SSX Tricky (USA).", 1);
    else
        settings_set_status(ui, why, 2);
}

static void settings_layout(SettingsUI *ui, UINT dpi)
{
    const Fonts *f = &ui->fonts;
    HWND h = ui->hwnd;
    fonts_make(&ui->fonts, dpi);

    place(h, ID_S_GAME_HEAD,     f, 20,  14, 520, 22, f->big);
    place(h, ID_S_ISO_LABEL,     f, 20,  42, 520, 20, f->body);
    place(h, ID_S_ISO,           f, 20,  64, 412, 26, f->body);
    place(h, ID_S_ISO_BROWSE,    f, 440, 63, 100, 28, f->body);
    place(h, ID_S_HDD_LABEL,     f, 20, 100, 520, 20, f->body);
    place(h, ID_S_HDD,           f, 20, 122, 300, 26, f->body);
    place(h, ID_S_HDD_BROWSE,    f, 328, 121, 104, 28, f->body);
    place(h, ID_S_HDD_OPEN,      f, 440, 121, 100, 28, f->body);

    place(h, ID_S_DISPLAY_HEAD,  f, 20, 166, 520, 22, f->big);
    place(h, ID_S_ASPECT_LABEL,  f, 20, 200, 106, 20, f->body);
    place(h, ID_S_ASPECT,        f, 128, 196, 160, 200, f->body);
    place(h, ID_S_RES_LABEL,     f, 300, 200, 86, 20, f->body);
    place(h, ID_S_RES,           f, 386, 196, 154, 300, f->body);
    place(h, ID_S_TEX_LABEL,     f, 20, 236, 106, 20, f->body);
    place(h, ID_S_TEX,           f, 128, 232, 160, 200, f->body);
    place(h, ID_S_AA_LABEL,      f, 300, 236, 86, 20, f->body);
    place(h, ID_S_AA,            f, 386, 232, 154, 200, f->body);
    place(h, ID_S_FULLSCREEN,    f, 20, 270, 270, 22, f->body);
    place(h, ID_S_FPS,           f, 300, 270, 240, 22, f->body);
    place(h, ID_S_NOTE,          f, 20, 298, 520, 38, f->body);

    place(h, ID_S_MORE_HEAD,     f, 20, 344, 520, 22, f->big);
    place(h, ID_S_CONTROLS,      f, 20, 374, 140, 30, f->body);
    place(h, ID_S_LOG,           f, 176, 378, 236, 22, f->body);
    place(h, ID_S_LOG_OPEN,      f, 420, 374, 120, 30, f->body);

    place(h, ID_S_STATUS,        f, 20, 418, 520, 40, f->bold);
    place(h, IDOK,               f, 344, 464, 96, 30, f->body);
    place(h, IDCANCEL,           f, 444, 464, 96, 30, f->body);
    size_and_center(h, f, 560, 510);
}

static int combo_pick(HWND dlg, int id, const int *values, int n)
{
    int i = (int)SendMessageW(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
    return (i >= 0 && i < n) ? values[i] : values[0];
}

static void combo_select(HWND dlg, int id, const int *values, int n, int value)
{
    int i, sel = 0;
    for (i = 0; i < n; i++) if (values[i] == value) sel = i;
    SendMessageW(GetDlgItem(dlg, id), CB_SETCURSEL, sel, 0);
}

static BOOL settings_save(SettingsUI *ui)
{
    WCHAR w[MAX_PATH];
    char iso[MAX_PATH], hdd[MAX_PATH], why[512];
    int ws, ri;
    const Res *list;

    GetDlgItemTextW(ui->hwnd, ID_S_ISO, w, MAX_PATH);
    if (!to_path(w, iso, sizeof iso)) {
        settings_set_status(ui, "The disc image path contains characters the game cannot open.", 2);
        return FALSE;
    }
    if (iso[0] && !launcher_check_iso(iso, why, sizeof why)) {
        settings_set_status(ui, why, 2);
        SetFocus(GetDlgItem(ui->hwnd, ID_S_ISO));
        return FALSE;
    }
    GetDlgItemTextW(ui->hwnd, ID_S_HDD, w, MAX_PATH);
    if (!to_path(w, hdd, sizeof hdd)) {
        settings_set_status(ui, "The save folder path contains characters the game cannot open.", 2);
        return FALSE;
    }
    if (!hdd[0]) strcpy(hdd, "hdd");

    ws = (int)SendMessageW(GetDlgItem(ui->hwnd, ID_S_ASPECT), CB_GETCURSEL, 0, 0) == 1;
    ri = (int)SendMessageW(GetDlgItem(ui->hwnd, ID_S_RES), CB_GETCURSEL, 0, 0);
    list = ws ? k_res169 : k_res43;
    if (ri < 0 || ri >= (ws ? COUNT(k_res169) : COUNT(k_res43))) ri = 0;

    snprintf(ui->cfg.iso, sizeof ui->cfg.iso, "%s", iso);
    snprintf(ui->cfg.hdd, sizeof ui->cfg.hdd, "%s", hdd);
    ui->cfg.widescreen = ws;
    ui->cfg.width  = list[ri].w;
    ui->cfg.height = list[ri].h;
    ui->cfg.fullscreen = IsDlgButtonChecked(ui->hwnd, ID_S_FULLSCREEN) == BST_CHECKED;
    ui->cfg.show_fps   = IsDlgButtonChecked(ui->hwnd, ID_S_FPS) == BST_CHECKED;
    ui->cfg.log_file   = IsDlgButtonChecked(ui->hwnd, ID_S_LOG) == BST_CHECKED;
    ui->cfg.aniso = combo_pick(ui->hwnd, ID_S_TEX, k_aniso, COUNT(k_aniso));
    ui->cfg.msaa  = combo_pick(ui->hwnd, ID_S_AA, k_msaa, COUNT(k_msaa));

    if (!launcher_config_save(&ui->cfg)) {
        char ini[MAX_PATH], msg[MAX_PATH + 64];
        launcher_config_path(ini, sizeof ini);
        snprintf(msg, sizeof msg, "Could not write the settings file: %s", ini);
        settings_set_status(ui, msg, 2);
        return FALSE;
    }
    return TRUE;
}

static LRESULT CALLBACK settings_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    SettingsUI *ui = s_settings;
    switch (msg) {
    case WM_COMMAND:
        if (!ui) break;
        switch (LOWORD(wp)) {
        case ID_S_ISO_BROWSE: {
            char cur[MAX_PATH], got[MAX_PATH];
            WCHAR w[MAX_PATH];
            GetDlgItemTextW(h, ID_S_ISO, w, MAX_PATH);
            if (!to_path(w, cur, sizeof cur)) cur[0] = '\0';
            if (pick_path(h, FALSE, L"Choose the SSX Tricky disc image", cur, got, sizeof got)) {
                to_wide(got, w, MAX_PATH);
                SetDlgItemTextW(h, ID_S_ISO, w);
                settings_check_iso_field(ui);
            }
            return 0;
        }
        case ID_S_HDD_BROWSE: {
            char cur[MAX_PATH], got[MAX_PATH];
            WCHAR w[MAX_PATH];
            GetDlgItemTextW(h, ID_S_HDD, w, MAX_PATH);
            if (!to_path(w, cur, sizeof cur)) cur[0] = '\0';
            if (pick_path(h, TRUE, L"Choose the folder for the Xbox hard disk (saves)", cur, got, sizeof got)) {
                to_wide(got, w, MAX_PATH);
                SetDlgItemTextW(h, ID_S_HDD, w);
            }
            return 0;
        }
        case ID_S_HDD_OPEN: {
            LauncherConfig tmp = ui->cfg;
            WCHAR w[MAX_PATH];
            char path[MAX_PATH];
            GetDlgItemTextW(h, ID_S_HDD, w, MAX_PATH);
            if (to_path(w, tmp.hdd, sizeof tmp.hdd)) {
                launcher_hdd_path(&tmp, path, sizeof path);
                launcher_open_path(h, path, TRUE);
            }
            return 0;
        }
        case ID_S_LOG_OPEN: {
            char path[MAX_PATH];
            launcher_log_path(path, sizeof path);
            if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
                settings_set_status(ui, "There is no log yet: turn on the log file and play once.", 0);
            else
                launcher_open_path(h, path, FALSE);
            return 0;
        }
        case ID_S_CONTROLS:
            if (controls_dialog(h, &ui->cfg.controls))
                settings_set_status(ui, "Controls changed. Save keeps them.", 0);
            return 0;
        case ID_S_ISO:
            if (HIWORD(wp) == EN_KILLFOCUS) settings_check_iso_field(ui);
            return 0;
        case ID_S_ASPECT:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int ri = (int)SendMessageW(GetDlgItem(h, ID_S_RES), CB_GETCURSEL, 0, 0);
                int was_ws = !((int)SendMessageW(GetDlgItem(h, ID_S_ASPECT), CB_GETCURSEL, 0, 0) == 1);
                const Res *old = was_ws ? k_res169 : k_res43;
                int on = was_ws ? COUNT(k_res169) : COUNT(k_res43);
                if (ri < 0 || ri >= on) ri = 0;
                settings_fill_resolutions(ui, old[ri].w, old[ri].h);
            }
            return 0;
        case IDOK:
            if (settings_save(ui)) { ui->saved = TRUE; ui->done = TRUE; }
            return 0;
        case IDCANCEL:
            ui->done = TRUE;
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        HWND c = (HWND)lp;
        int id = GetDlgCtrlID(c);
        SetBkMode(dc, TRANSPARENT);
        if (id == ID_S_STATUS)
            SetTextColor(dc, !ui ? CLR_MUTED : ui->status_level == 2 ? CLR_ERROR
                           : ui->status_level == 1 ? CLR_OK : CLR_MUTED);
        else if (id == ID_S_NOTE)
            SetTextColor(dc, CLR_MUTED);
        else if (id == ID_S_GAME_HEAD || id == ID_S_DISPLAY_HEAD || id == ID_S_MORE_HEAD)
            SetTextColor(dc, CLR_HEADER);
        else
            SetTextColor(dc, CLR_TEXT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DPICHANGED:
        if (ui) {
            const RECT *r = (const RECT *)lp;
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            settings_layout(ui, HIWORD(wp));
        }
        return 0;
    case WM_CLOSE:
        if (ui) ui->done = TRUE;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* Modal: the owner is disabled while this is open. TRUE if saved. */
static BOOL settings_run(HWND owner, LauncherConfig *cfg)
{
    static const WCHAR *cls = L"SsxLauncherSettings";
    WNDCLASSEXW wc;
    SettingsUI ui;
    MSG msg;
    WCHAR w[MAX_PATH];

    memset(&ui, 0, sizeof ui);
    ui.cfg = *cfg;
    s_settings = &ui;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = settings_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    ui.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, L"Settings",
                              WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                              owner, NULL, wc.hInstance, NULL);
    if (!ui.hwnd) { s_settings = NULL; return FALSE; }

    child(ui.hwnd, L"STATIC", L"Game", SS_LEFT, ID_S_GAME_HEAD);
    child(ui.hwnd, L"STATIC", L"Disc image (.iso)", SS_LEFT, ID_S_ISO_LABEL);
    child(ui.hwnd, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, ID_S_ISO);
    child(ui.hwnd, L"BUTTON", L"Browse…", WS_TABSTOP | BS_PUSHBUTTON, ID_S_ISO_BROWSE);
    child(ui.hwnd, L"STATIC", L"Save data folder (the emulated Xbox hard disk)", SS_LEFT, ID_S_HDD_LABEL);
    child(ui.hwnd, L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, ID_S_HDD);
    child(ui.hwnd, L"BUTTON", L"Browse…", WS_TABSTOP | BS_PUSHBUTTON, ID_S_HDD_BROWSE);
    child(ui.hwnd, L"BUTTON", L"Open folder", WS_TABSTOP | BS_PUSHBUTTON, ID_S_HDD_OPEN);

    child(ui.hwnd, L"STATIC", L"Display", SS_LEFT, ID_S_DISPLAY_HEAD);
    child(ui.hwnd, L"STATIC", L"Aspect ratio", SS_LEFT, ID_S_ASPECT_LABEL);
    child(ui.hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_S_ASPECT);
    child(ui.hwnd, L"STATIC", L"Resolution", SS_LEFT, ID_S_RES_LABEL);
    child(ui.hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_S_RES);
    child(ui.hwnd, L"STATIC", L"Textures", SS_LEFT, ID_S_TEX_LABEL);
    child(ui.hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_S_TEX);
    child(ui.hwnd, L"STATIC", L"Anti-aliasing", SS_LEFT, ID_S_AA_LABEL);
    child(ui.hwnd, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, ID_S_AA);
    child(ui.hwnd, L"BUTTON", L"Fullscreen (Alt+Enter switches)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_S_FULLSCREEN);
    child(ui.hwnd, L"BUTTON", L"Show frame rate", WS_TABSTOP | BS_AUTOCHECKBOX, ID_S_FPS);
    child(ui.hwnd, L"STATIC",
          L"16:9 uses the game's own widescreen mode, as on a console set to widescreen. "
          L"Textures, anti-aliasing and the frame rate also change from the game's Video menu.",
          SS_LEFT, ID_S_NOTE);

    child(ui.hwnd, L"STATIC", L"Controls and troubleshooting", SS_LEFT, ID_S_MORE_HEAD);
    child(ui.hwnd, L"BUTTON", L"Controls…", WS_TABSTOP | BS_PUSHBUTTON, ID_S_CONTROLS);
    child(ui.hwnd, L"BUTTON", L"Write a log file (for bug reports)", WS_TABSTOP | BS_AUTOCHECKBOX, ID_S_LOG);
    child(ui.hwnd, L"BUTTON", L"Open log", WS_TABSTOP | BS_PUSHBUTTON, ID_S_LOG_OPEN);

    child(ui.hwnd, L"STATIC", L"", SS_LEFT, ID_S_STATUS);
    child(ui.hwnd, L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    child(ui.hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);

    SendDlgItemMessageW(ui.hwnd, ID_S_ASPECT, CB_ADDSTRING, 0, (LPARAM)L"4:3  (standard)");
    SendDlgItemMessageW(ui.hwnd, ID_S_ASPECT, CB_ADDSTRING, 0, (LPARAM)L"16:9  (widescreen)");
    SendDlgItemMessageW(ui.hwnd, ID_S_ASPECT, CB_SETCURSEL, cfg->widescreen ? 1 : 0, 0);
    settings_fill_resolutions(&ui, cfg->width, cfg->height);
    SendDlgItemMessageW(ui.hwnd, ID_S_TEX, CB_ADDSTRING, 0, (LPARAM)L"As on Xbox");
    SendDlgItemMessageW(ui.hwnd, ID_S_TEX, CB_ADDSTRING, 0, (LPARAM)L"Anisotropic 2×");
    SendDlgItemMessageW(ui.hwnd, ID_S_TEX, CB_ADDSTRING, 0, (LPARAM)L"Anisotropic 4×");
    SendDlgItemMessageW(ui.hwnd, ID_S_TEX, CB_ADDSTRING, 0, (LPARAM)L"Anisotropic 8×");
    SendDlgItemMessageW(ui.hwnd, ID_S_TEX, CB_ADDSTRING, 0, (LPARAM)L"Anisotropic 16×");
    combo_select(ui.hwnd, ID_S_TEX, k_aniso, COUNT(k_aniso), cfg->aniso);
    SendDlgItemMessageW(ui.hwnd, ID_S_AA, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendDlgItemMessageW(ui.hwnd, ID_S_AA, CB_ADDSTRING, 0, (LPARAM)L"2× MSAA");
    SendDlgItemMessageW(ui.hwnd, ID_S_AA, CB_ADDSTRING, 0, (LPARAM)L"4× MSAA");
    SendDlgItemMessageW(ui.hwnd, ID_S_AA, CB_ADDSTRING, 0, (LPARAM)L"8× MSAA");
    combo_select(ui.hwnd, ID_S_AA, k_msaa, COUNT(k_msaa), cfg->msaa);
    CheckDlgButton(ui.hwnd, ID_S_FULLSCREEN, cfg->fullscreen ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(ui.hwnd, ID_S_FPS, cfg->show_fps ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(ui.hwnd, ID_S_LOG, cfg->log_file ? BST_CHECKED : BST_UNCHECKED);
    to_wide(cfg->iso, w, MAX_PATH);
    SetDlgItemTextW(ui.hwnd, ID_S_ISO, w);
    to_wide(cfg->hdd, w, MAX_PATH);
    SetDlgItemTextW(ui.hwnd, ID_S_HDD, w);
    SendDlgItemMessageW(ui.hwnd, ID_S_ISO, EM_SETCUEBANNER, TRUE, (LPARAM)L"Choose your SSX Tricky .iso");

    settings_layout(&ui, window_dpi(ui.hwnd));
    settings_check_iso_field(&ui);

    EnableWindow(owner, FALSE);
    ShowWindow(ui.hwnd, SW_SHOW);
    SetFocus(GetDlgItem(ui.hwnd, cfg->iso[0] ? IDOK : ID_S_ISO_BROWSE));

    while (!ui.done) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) { PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(ui.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(ui.hwnd);
    fonts_free(&ui.fonts);
    s_settings = NULL;
    if (ui.saved) *cfg = ui.cfg;
    return ui.saved;
}

/* ── Main window ───────────────────────────────────────────────────── */

enum { ID_M_SETTINGS = 100, ID_M_STATUS, ID_M_DETAIL, ID_M_TIMER = 1 };

typedef struct {
    HWND  hwnd;
    Fonts fonts;
    LauncherConfig *cfg;
    BOOL  iso_ok;
    BOOL  start;
    WORD  pad_prev;
} MainUI;

static MainUI *s_main;

static void main_refresh(MainUI *ui)
{
    char why[512], name[MAX_PATH], line[1024];
    WCHAR w[1024];
    ui->iso_ok = launcher_check_iso(ui->cfg->iso, why, sizeof why);
    if (ui->iso_ok) {
        file_name_of(ui->cfg->iso, name, sizeof name);
        snprintf(line, sizeof line, "Disc image: %s", name);
    } else if (!ui->cfg->iso[0]) {
        snprintf(line, sizeof line, "Open Settings and choose your SSX Tricky disc image to enable Start game.");
    } else {
        snprintf(line, sizeof line, "%s", why);
    }
    to_wide(line, w, 1024);
    SetDlgItemTextW(ui->hwnd, ID_M_STATUS, w);
    swprintf(w, 1024, L"%d × %d  ·  %ls  ·  %ls",
             ui->cfg->width, ui->cfg->height,
             ui->cfg->widescreen ? L"16:9 widescreen" : L"4:3",
             ui->cfg->fullscreen ? L"fullscreen" : L"windowed");
    SetDlgItemTextW(ui->hwnd, ID_M_DETAIL, w);
    EnableWindow(GetDlgItem(ui->hwnd, IDOK), ui->iso_ok);
    InvalidateRect(ui->hwnd, NULL, TRUE);
}

static void main_layout(MainUI *ui, UINT dpi)
{
    const Fonts *f = &ui->fonts;
    fonts_make(&ui->fonts, dpi);
    place(ui->hwnd, ID_M_STATUS,   f, 24, 118, 400, 40, f->body);
    place(ui->hwnd, ID_M_DETAIL,   f, 24, 160, 400, 20, f->body);
    place(ui->hwnd, IDOK,          f, 24, 196, 400, 44, f->big);
    place(ui->hwnd, ID_M_SETTINGS, f, 24, 250, 196, 32, f->body);
    place(ui->hwnd, IDCANCEL,      f, 228, 250, 196, 32, f->body);
    size_and_center(ui->hwnd, f, 448, 302);
}

static void main_start(MainUI *ui)
{
    if (!ui->iso_ok) return;
    ui->start = TRUE;
    DestroyWindow(ui->hwnd);
}

/* Controller: D-pad moves between buttons, A presses the focused one, Start
 * starts the game. Polled; XInput has no messages. */
static void main_poll_pad(MainUI *ui)
{
    XINPUT_STATE st;
    DWORD i;
    WORD b = 0, pressed;
    for (i = 0; i < 4; i++) {
        memset(&st, 0, sizeof st);
        if (XInputGetState(i, &st) == ERROR_SUCCESS) b |= st.Gamepad.wButtons;
    }
    pressed = b & (WORD)~ui->pad_prev;
    ui->pad_prev = b;
    if (!pressed || GetForegroundWindow() != ui->hwnd) return;
    if (pressed & XINPUT_GAMEPAD_START) { main_start(ui); return; }
    if (pressed & (XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT)) {
        BOOL back = (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT)) != 0;
        HWND next = GetNextDlgTabItem(ui->hwnd, GetFocus(), back);
        if (next) SendMessageW(ui->hwnd, WM_NEXTDLGCTL, (WPARAM)next, TRUE);
    }
    if (pressed & XINPUT_GAMEPAD_A) {
        HWND fcs = GetFocus();
        if (fcs && GetParent(fcs) == ui->hwnd && IsWindowEnabled(fcs))
            SendMessageW(fcs, BM_CLICK, 0, 0);
    }
}

static LRESULT CALLBACK main_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    MainUI *ui = s_main;
    switch (msg) {
    case WM_COMMAND:
        if (!ui) break;
        switch (LOWORD(wp)) {
        case IDOK:
            main_start(ui);
            return 0;
        case ID_M_SETTINGS:
            if (settings_run(h, ui->cfg)) main_refresh(ui);
            SetFocus(GetDlgItem(h, ui->iso_ok ? IDOK : ID_M_SETTINGS));
            return 0;
        case IDCANCEL:
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_TIMER:
        if (ui && wp == ID_M_TIMER && IsWindowEnabled(h)) main_poll_pad(ui);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (ui) {
            const Fonts *f = &ui->fonts;
            RECT rc, band, line;
            HBRUSH br;
            GetClientRect(h, &rc);
            band = rc; band.bottom = S(f, 96);
            br = CreateSolidBrush(CLR_HEADER);
            FillRect(dc, &band, br);
            DeleteObject(br);
            line = rc; line.top = band.bottom; line.bottom = band.bottom + S(f, 4);
            br = CreateSolidBrush(CLR_ACCENT);
            FillRect(dc, &line, br);
            DeleteObject(br);
            SetBkMode(dc, TRANSPARENT);
            SelectObject(dc, f->title);
            SetTextColor(dc, RGB(255, 255, 255));
            TextOutW(dc, S(f, 24), S(f, 16), L"SSX Tricky", 10);
            SelectObject(dc, f->sub);
            SetTextColor(dc, CLR_HEADER_SUB);
            TextOutW(dc, S(f, 26), S(f, 62), L"Xbox static recompilation for PC", 32);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        SetBkMode(dc, TRANSPARENT);
        if (id == ID_M_STATUS)
            SetTextColor(dc, ui && ui->iso_ok ? CLR_TEXT : (ui && ui->cfg->iso[0] ? CLR_ERROR : CLR_MUTED));
        else
            SetTextColor(dc, CLR_MUTED);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_DPICHANGED:
        if (ui) {
            const RECT *r = (const RECT *)lp;
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            main_layout(ui, HIWORD(wp));
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, ID_M_TIMER);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

BOOL launcher_run(LauncherConfig *cfg)
{
    static const WCHAR *cls = L"SsxLauncher";
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    MainUI ui;
    MSG msg;
    HRESULT com;

    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    /* For the file pickers. Balanced below, so the game later starts on a
     * thread with no COM apartment, as before. */
    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    memset(&ui, 0, sizeof ui);
    ui.cfg = cfg;
    s_main = &ui;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = main_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    ui.hwnd = CreateWindowExW(0, cls, L"SSX Tricky",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                              NULL, NULL, wc.hInstance, NULL);
    if (!ui.hwnd) {
        s_main = NULL;
        if (SUCCEEDED(com)) CoUninitialize();
        return FALSE;
    }
    child(ui.hwnd, L"STATIC", L"", SS_LEFT, ID_M_STATUS);
    child(ui.hwnd, L"STATIC", L"", SS_LEFT, ID_M_DETAIL);
    child(ui.hwnd, L"BUTTON", L"Start game", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    child(ui.hwnd, L"BUTTON", L"Settings", WS_TABSTOP | BS_PUSHBUTTON, ID_M_SETTINGS);
    child(ui.hwnd, L"BUTTON", L"Exit", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);

    main_layout(&ui, window_dpi(ui.hwnd));
    main_refresh(&ui);
    ShowWindow(ui.hwnd, SW_SHOW);
    SetForegroundWindow(ui.hwnd);
    SetFocus(GetDlgItem(ui.hwnd, ui.iso_ok ? IDOK : ID_M_SETTINGS));
    SetTimer(ui.hwnd, ID_M_TIMER, 50, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsWindow(ui.hwnd) && IsDialogMessageW(ui.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    fonts_free(&ui.fonts);
    s_main = NULL;
    if (SUCCEEDED(com)) CoUninitialize();
    return ui.start;
}
