/*
 * hostui.c -- the game window's menu bar (part 183).
 *
 *   Game   Screenshot (F12), Open screenshots folder, Open save folder,
 *          Open log file, Exit (Alt+F4)
 *   Video  Fullscreen (Alt+Enter / F11), Window size, Textures,
 *          Anti-aliasing, Show frame rate
 *   Input  Controls...
 *   Help   Keys, About
 *
 * The window itself is the runtime's (xboxrecomp d3d8_device.c); this builds
 * the menu and acts on it through D3D8HostUiHooks, on the window's own
 * thread. Changes apply at once and, when the game was started from the
 * launcher, are written back to the settings file.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "launcher.h"
#include "controls.h"
#include "hostui.h"

/* Runtime (xboxrecomp/src/d3d/d3d8_xbox.h), declared here to keep this file
 * free of the runtime's Xbox-flavoured headers. */
typedef struct D3D8HostUiHooks {
    HMENU (*create_menu)(void);
    void  (*on_command)(HWND hwnd, UINT id);
    void  (*on_init_menu)(HMENU menu);
    int   (*on_key)(HWND hwnd, UINT vk);
    void  (*on_menu_loop)(int entering);
} D3D8HostUiHooks;
void d3d8_SetHostUiHooks(const D3D8HostUiHooks *hooks);
void d3d8_HostSetFullscreen(int on);
int  d3d8_HostIsFullscreen(void);
int  d3d8_HostIsWidescreen(void);
void d3d8_HostSetClientSize(unsigned w, unsigned h);
void d3d8_HostExit(void);
void d3d8_SetMsaa(int samples);
int  d3d8_GetMsaa(void);
void d3d8_SetAnisotropy(int n);
int  d3d8_GetAnisotropy(void);
void d3d8_SetShowFps(int on);
int  d3d8_GetShowFps(void);
void d3d8_RequestScreenshot(const wchar_t *path);
HWND d3d8_GetHostWindow(void);

enum {
    ID_SCREENSHOT = 40001, ID_OPEN_SHOTS, ID_OPEN_SAVES, ID_OPEN_LOG, ID_EXIT,
    ID_FULLSCREEN, ID_FPS, ID_CONTROLS, ID_KEYS, ID_ABOUT,
    ID_SIZE_BASE  = 40100,   /* + preset index */
    ID_ANISO_BASE = 40200,   /* + index into k_aniso */
    ID_AA_BASE    = 40300,   /* + index into k_msaa */
};

static const int k_aniso[] = { 0, 2, 4, 8, 16 };
static const int k_msaa[]  = { 1, 2, 4, 8 };
static const struct { int w, h; } k_size43[]  = { { 640, 480 }, { 960, 720 }, { 1280, 960 }, { 1440, 1080 }, { 1920, 1440 } };
static const struct { int w, h; } k_size169[] = { { 854, 480 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 } };

static LauncherConfig *s_cfg;
static BOOL s_persist;
static HMENU s_size_menu;

static void persist(void)
{
    if (s_persist && s_cfg) launcher_config_save(s_cfg);
}

static void exe_dir_w(WCHAR *out, size_t n)
{
    DWORD k = GetModuleFileNameW(NULL, out, (DWORD)n);
    WCHAR *slash;
    if (k == 0 || k >= n) { out[0] = 0; return; }
    slash = wcsrchr(out, L'\\');
    if (slash) *slash = 0;
}

static void shots_dir(WCHAR *out, size_t n)
{
    exe_dir_w(out, n);
    wcsncat(out, L"\\Screenshots", n - wcslen(out) - 1);
}

static void take_screenshot(void)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH];
    SYSTEMTIME t;
    shots_dir(dir, MAX_PATH);
    CreateDirectoryW(dir, NULL);
    GetLocalTime(&t);
    swprintf(path, MAX_PATH, L"%ls\\SSX Tricky %04u-%02u-%02u %02u-%02u-%02u.png", dir,
             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    d3d8_RequestScreenshot(path);
}

static HMENU create_menu(void)
{
    HMENU bar = CreateMenu(), game = CreatePopupMenu(), video = CreatePopupMenu();
    HMENU input = CreatePopupMenu(), help = CreatePopupMenu();
    HMENU tex = CreatePopupMenu(), aa = CreatePopupMenu();
    s_size_menu = CreatePopupMenu();

    AppendMenuW(game, MF_STRING, ID_SCREENSHOT, L"Take &screenshot\tF12");
    AppendMenuW(game, MF_STRING, ID_OPEN_SHOTS, L"Open s&creenshots folder");
    AppendMenuW(game, MF_STRING, ID_OPEN_SAVES, L"Open s&ave folder");
    AppendMenuW(game, MF_STRING, ID_OPEN_LOG, L"Open &log file");
    AppendMenuW(game, MF_SEPARATOR, 0, NULL);
    AppendMenuW(game, MF_STRING, ID_EXIT, L"E&xit\tAlt+F4");

    AppendMenuW(tex, MF_STRING, ID_ANISO_BASE + 0, L"As on &Xbox");
    AppendMenuW(tex, MF_STRING, ID_ANISO_BASE + 1, L"Anisotropic &2×");
    AppendMenuW(tex, MF_STRING, ID_ANISO_BASE + 2, L"Anisotropic &4×");
    AppendMenuW(tex, MF_STRING, ID_ANISO_BASE + 3, L"Anisotropic &8×");
    AppendMenuW(tex, MF_STRING, ID_ANISO_BASE + 4, L"Anisotropic &16×");
    AppendMenuW(aa, MF_STRING, ID_AA_BASE + 0, L"&Off");
    AppendMenuW(aa, MF_STRING, ID_AA_BASE + 1, L"&2× MSAA");
    AppendMenuW(aa, MF_STRING, ID_AA_BASE + 2, L"&4× MSAA");
    AppendMenuW(aa, MF_STRING, ID_AA_BASE + 3, L"&8× MSAA");

    AppendMenuW(video, MF_STRING, ID_FULLSCREEN, L"&Fullscreen\tAlt+Enter");
    AppendMenuW(video, MF_POPUP, (UINT_PTR)s_size_menu, L"&Window size");
    AppendMenuW(video, MF_SEPARATOR, 0, NULL);
    AppendMenuW(video, MF_POPUP, (UINT_PTR)tex, L"&Textures");
    AppendMenuW(video, MF_POPUP, (UINT_PTR)aa, L"&Anti-aliasing");
    AppendMenuW(video, MF_SEPARATOR, 0, NULL);
    AppendMenuW(video, MF_STRING, ID_FPS, L"Show frame &rate");

    AppendMenuW(input, MF_STRING, ID_CONTROLS, L"&Controls…");

    AppendMenuW(help, MF_STRING, ID_KEYS, L"&Keys…");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About…");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)game, L"&Game");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)video, L"&Video");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)input, L"&Input");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

/* Window sizes for the current shape that fit on this screen. */
static void fill_size_menu(HWND h)
{
    RECT work, frame = { 0, 0, 0, 0 }, client;
    int i, n, ws = d3d8_HostIsWidescreen();
    while (GetMenuItemCount(s_size_menu) > 0) DeleteMenu(s_size_menu, 0, MF_BYPOSITION);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, TRUE);
    GetClientRect(h, &client);
    n = ws ? (int)(sizeof k_size169 / sizeof k_size169[0]) : (int)(sizeof k_size43 / sizeof k_size43[0]);
    for (i = 0; i < n; i++) {
        int w = ws ? k_size169[i].w : k_size43[i].w, hh = ws ? k_size169[i].h : k_size43[i].h;
        WCHAR label[48];
        UINT flags = MF_STRING;
        if (w + (frame.right - frame.left) > work.right - work.left ||
            hh + (frame.bottom - frame.top) > work.bottom - work.top)
            flags |= MF_GRAYED;
        if (!d3d8_HostIsFullscreen() && client.right == w && client.bottom == hh)
            flags |= MF_CHECKED;
        swprintf(label, 48, L"%d × %d", w, hh);
        AppendMenuW(s_size_menu, flags, ID_SIZE_BASE + i, label);
    }
}

static void on_init_menu(HMENU m)
{
    HWND h = d3d8_GetHostWindow();
    int i;
    CheckMenuItem(m, ID_FULLSCREEN, d3d8_HostIsFullscreen() ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_FPS, d3d8_GetShowFps() ? MF_CHECKED : MF_UNCHECKED);
    for (i = 0; i < 5; i++)
        CheckMenuItem(m, ID_ANISO_BASE + i, k_aniso[i] == d3d8_GetAnisotropy() ? MF_CHECKED : MF_UNCHECKED);
    for (i = 0; i < 4; i++)
        CheckMenuItem(m, ID_AA_BASE + i, k_msaa[i] == d3d8_GetMsaa() ? MF_CHECKED : MF_UNCHECKED);
    EnableMenuItem(m, ID_OPEN_LOG, (s_cfg && s_cfg->log_file && s_persist) ? MF_ENABLED : MF_GRAYED);
    if (m == s_size_menu && h) fill_size_menu(h);
}

static void show_keys(HWND h)
{
    MessageBoxW(h,
        L"Window\n"
        L"   F12\tTake a screenshot\n"
        L"   Alt+Enter, F11\tFullscreen on/off\n"
        L"   Alt or F10\tThe menu bar\n"
        L"   Alt+F4\tExit\n\n"
        L"The game's own buttons are set in Input › Controls, for the keyboard "
        L"and for a controller.",
        L"Keys", MB_OK | MB_ICONINFORMATION);
}

static void show_about(HWND h)
{
    WCHAR text[1024], ini[MAX_PATH];
    char ini_a[MAX_PATH];
    RECT c;
    launcher_config_path(ini_a, sizeof ini_a);
    MultiByteToWideChar(CP_ACP, 0, ini_a, -1, ini, MAX_PATH);
    GetClientRect(h, &c);
    swprintf(text, 1024,
        L"SSX Tricky — a static recompilation of the Xbox game for Windows.\n\n"
        L"Window %ld × %ld, %ls, %ls textures, anti-aliasing %d×.\n\n"
        L"Settings file:\n%ls",
        c.right, c.bottom, d3d8_HostIsWidescreen() ? L"16:9" : L"4:3",
        d3d8_GetAnisotropy() ? L"anisotropic" : L"Xbox", d3d8_GetMsaa(),
        s_persist ? ini : L"(not used: started for testing)");
    MessageBoxW(h, text, L"About", MB_OK | MB_ICONINFORMATION);
}

static void on_command(HWND h, UINT id)
{
    if (id >= ID_SIZE_BASE && id < ID_SIZE_BASE + 16) {
        int i = (int)(id - ID_SIZE_BASE), ws = d3d8_HostIsWidescreen();
        int n = ws ? (int)(sizeof k_size169 / sizeof k_size169[0]) : (int)(sizeof k_size43 / sizeof k_size43[0]);
        if (i < n) d3d8_HostSetClientSize(ws ? k_size169[i].w : k_size43[i].w, ws ? k_size169[i].h : k_size43[i].h);
        return;
    }
    if (id >= ID_ANISO_BASE && id < ID_ANISO_BASE + 5) {
        d3d8_SetAnisotropy(k_aniso[id - ID_ANISO_BASE]);
        if (s_cfg) { s_cfg->aniso = k_aniso[id - ID_ANISO_BASE]; persist(); }
        return;
    }
    if (id >= ID_AA_BASE && id < ID_AA_BASE + 4) {
        d3d8_SetMsaa(k_msaa[id - ID_AA_BASE]);
        if (s_cfg) { s_cfg->msaa = k_msaa[id - ID_AA_BASE]; persist(); }
        return;
    }
    switch (id) {
    case ID_SCREENSHOT: take_screenshot(); break;
    case ID_OPEN_SHOTS: {
        WCHAR dir[MAX_PATH];
        shots_dir(dir, MAX_PATH);
        CreateDirectoryW(dir, NULL);
        ShellExecuteW(h, L"explore", dir, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
    case ID_OPEN_SAVES: {
        char path[MAX_PATH];
        LauncherConfig def;
        if (!s_cfg) { memset(&def, 0, sizeof def); strcpy(def.hdd, "hdd"); }
        launcher_hdd_path(s_cfg ? s_cfg : &def, path, sizeof path);
        launcher_open_path(h, path, TRUE);
        break;
    }
    case ID_OPEN_LOG: {
        char path[MAX_PATH];
        launcher_log_path(path, sizeof path);
        launcher_open_path(h, path, FALSE);
        break;
    }
    case ID_EXIT: d3d8_HostExit(); break;
    case ID_FULLSCREEN: d3d8_HostSetFullscreen(!d3d8_HostIsFullscreen()); break;
    case ID_FPS:
        d3d8_SetShowFps(!d3d8_GetShowFps());
        if (s_cfg) { s_cfg->show_fps = d3d8_GetShowFps(); persist(); }
        break;
    case ID_CONTROLS: {
        ControlMap m;
        controls_get_current(&m);
        if (controls_dialog(h, &m)) {
            controls_set_current(&m);
            if (s_cfg) { s_cfg->controls = m; persist(); }
        }
        break;
    }
    case ID_KEYS: show_keys(h); break;
    case ID_ABOUT: show_about(h); break;
    }
}

static int on_key(HWND h, UINT vk)
{
    (void)h;
    if (vk == VK_F12) { take_screenshot(); return 1; }
    if (vk == VK_F11) { d3d8_HostSetFullscreen(!d3d8_HostIsFullscreen()); return 1; }
    return 0;
}

/* While a menu is open the arrow keys belong to it, not to the game. */
static void on_menu_loop(int entering)
{
    controls_set_suspended(entering ? TRUE : FALSE);
}

void hostui_install(LauncherConfig *cfg, BOOL persist_changes)
{
    static const D3D8HostUiHooks hooks = {
        create_menu, on_command, on_init_menu, on_key, on_menu_loop,
    };
    s_cfg = cfg;
    s_persist = persist_changes;
    d3d8_SetHostUiHooks(&hooks);
}
