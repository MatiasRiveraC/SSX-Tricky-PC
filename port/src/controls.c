/*
 * controls.c -- button mapping for the keyboard and the PC controller
 * (part 183).
 *
 * The title reads a console gamepad through its XAPI XInputGetState, which
 * xapi_input_hle.c answers on the host. This module turns a physical XInput
 * controller and the keyboard into that console state through the player's
 * bindings, and provides the Controls window that edits them.
 *
 * Defaults: the controller maps like-for-like, with the bumpers on the two
 * console-only buttons (LB = White, RB = Black, as xemu and Cxbx-Reloaded
 * place them); the keyboard keeps the part-178 keys (Enter = Start, Space = A,
 * Esc = B, C = X, V = Y, Tab = Back, arrows = D-pad and left stick) and adds
 * Q/E for the triggers and R/F for Black/White.
 */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <xinput.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "controls.h"

HWND d3d8_GetHostWindow(void);   /* xboxrecomp d3d8_device.c */

/* ── Names ─────────────────────────────────────────────────────────── */

static const struct { const char *ini; const WCHAR *label; } k_ctl[CTL_COUNT] = {
    { "A", L"A" }, { "B", L"B" }, { "X", L"X" }, { "Y", L"Y" },
    { "Black", L"Black" }, { "White", L"White" },
    { "LeftTrigger", L"Left trigger" }, { "RightTrigger", L"Right trigger" },
    { "Start", L"Start" }, { "Back", L"Back" },
    { "LeftStickPress", L"Left stick press" }, { "RightStickPress", L"Right stick press" },
    { "DpadUp", L"D-pad up" }, { "DpadDown", L"D-pad down" },
    { "DpadLeft", L"D-pad left" }, { "DpadRight", L"D-pad right" },
    { "LeftStickUp", L"Left stick up" }, { "LeftStickDown", L"Left stick down" },
    { "LeftStickLeft", L"Left stick left" }, { "LeftStickRight", L"Left stick right" },
    { "RightStickUp", L"Right stick up" }, { "RightStickDown", L"Right stick down" },
    { "RightStickLeft", L"Right stick left" }, { "RightStickRight", L"Right stick right" },
};

static const struct { const char *ini; const WCHAR *label; } k_pad[PAD_COUNT] = {
    { "None", L"" }, { "A", L"A" }, { "B", L"B" }, { "X", L"X" }, { "Y", L"Y" },
    { "LB", L"Left bumper" }, { "RB", L"Right bumper" },
    { "LT", L"Left trigger" }, { "RT", L"Right trigger" },
    { "Start", L"Start" }, { "Back", L"Back" },
    { "LS", L"Left stick press" }, { "RS", L"Right stick press" },
    { "DpadUp", L"D-pad up" }, { "DpadDown", L"D-pad down" },
    { "DpadLeft", L"D-pad left" }, { "DpadRight", L"D-pad right" },
};

/* XINPUT_GAMEPAD button bit of each digital PAD_* (0 = a trigger). */
static const WORD k_pad_bit[PAD_COUNT] = {
    0, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
    XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0,
    XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK,
    XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
    XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
    XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
};

/* Key names: stable for the .ini, readable in the window. */
static const struct { BYTE vk; const char *name; } k_keys[] = {
    { VK_SPACE, "Space" }, { VK_RETURN, "Enter" }, { VK_ESCAPE, "Escape" },
    { VK_TAB, "Tab" }, { VK_BACK, "Backspace" }, { VK_CAPITAL, "CapsLock" },
    { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" },
    { VK_LSHIFT, "LeftShift" }, { VK_RSHIFT, "RightShift" }, { VK_SHIFT, "Shift" },
    { VK_LCONTROL, "LeftCtrl" }, { VK_RCONTROL, "RightCtrl" }, { VK_CONTROL, "Ctrl" },
    { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" },
    { VK_END, "End" }, { VK_PRIOR, "PageUp" }, { VK_NEXT, "PageDown" },
    { VK_NUMPAD0, "Num0" }, { VK_NUMPAD1, "Num1" }, { VK_NUMPAD2, "Num2" },
    { VK_NUMPAD3, "Num3" }, { VK_NUMPAD4, "Num4" }, { VK_NUMPAD5, "Num5" },
    { VK_NUMPAD6, "Num6" }, { VK_NUMPAD7, "Num7" }, { VK_NUMPAD8, "Num8" },
    { VK_NUMPAD9, "Num9" }, { VK_MULTIPLY, "Num*" }, { VK_ADD, "Num+" },
    { VK_SUBTRACT, "Num-" }, { VK_DECIMAL, "Num." }, { VK_DIVIDE, "Num/" },
    { VK_OEM_1, ";" }, { VK_OEM_PLUS, "=" }, { VK_OEM_COMMA, "," },
    { VK_OEM_MINUS, "-" }, { VK_OEM_PERIOD, "." }, { VK_OEM_2, "/" },
    { VK_OEM_3, "`" }, { VK_OEM_4, "[" }, { VK_OEM_5, "\\" }, { VK_OEM_6, "]" },
    { VK_OEM_7, "'" },
};

static void key_name(BYTE vk, char *out, size_t n)
{
    size_t i;
    if (!vk) { out[0] = '\0'; return; }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { snprintf(out, n, "%c", vk); return; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(out, n, "F%d", vk - VK_F1 + 1); return; }
    for (i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++)
        if (k_keys[i].vk == vk) { snprintf(out, n, "%s", k_keys[i].name); return; }
    snprintf(out, n, "Key0x%02X", vk);
}

static BYTE key_parse(const char *s)
{
    size_t i;
    unsigned v;
    if (!s[0] || !_stricmp(s, "None")) return 0;
    if (!s[1] && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= '0' && s[0] <= '9')))
        return (BYTE)toupper((unsigned char)s[0]);
    if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') {
        int f = atoi(s + 1);
        if (f >= 1 && f <= 24) return (BYTE)(VK_F1 + f - 1);
    }
    for (i = 0; i < sizeof k_keys / sizeof k_keys[0]; i++)
        if (!_stricmp(k_keys[i].name, s)) return k_keys[i].vk;
    if (sscanf(s, "Key0x%x", &v) == 1 && v > 0 && v < 256) return (BYTE)v;
    return 0;
}

/* ── Settings ──────────────────────────────────────────────────────── */

void controls_defaults(ControlMap *m)
{
    static const BYTE keys[CTL_COUNT] = {
        [CTL_A] = VK_SPACE, [CTL_B] = VK_ESCAPE, [CTL_X] = 'C', [CTL_Y] = 'V',
        [CTL_BLACK] = 'R', [CTL_WHITE] = 'F', [CTL_LT] = 'Q', [CTL_RT] = 'E',
        [CTL_START] = VK_RETURN, [CTL_BACK] = VK_TAB,
        [CTL_DUP] = VK_UP, [CTL_DDOWN] = VK_DOWN, [CTL_DLEFT] = VK_LEFT, [CTL_DRIGHT] = VK_RIGHT,
        [CTL_LS_UP] = VK_UP, [CTL_LS_DOWN] = VK_DOWN, [CTL_LS_LEFT] = VK_LEFT, [CTL_LS_RIGHT] = VK_RIGHT,
    };
    static const BYTE pads[CTL_PAD_COUNT] = {
        [CTL_A] = PAD_A, [CTL_B] = PAD_B, [CTL_X] = PAD_X, [CTL_Y] = PAD_Y,
        [CTL_BLACK] = PAD_RB, [CTL_WHITE] = PAD_LB, [CTL_LT] = PAD_LT, [CTL_RT] = PAD_RT,
        [CTL_START] = PAD_START, [CTL_BACK] = PAD_BACK, [CTL_L3] = PAD_LS, [CTL_R3] = PAD_RS,
        [CTL_DUP] = PAD_DUP, [CTL_DDOWN] = PAD_DDOWN, [CTL_DLEFT] = PAD_DLEFT, [CTL_DRIGHT] = PAD_DRIGHT,
    };
    memcpy(m->key, keys, sizeof m->key);
    memcpy(m->pad, pads, sizeof m->pad);
}

void controls_load(ControlMap *m, const char *ini)
{
    int i, j;
    char buf[64];
    controls_defaults(m);
    if (!ini || GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) return;
    for (i = 0; i < CTL_COUNT; i++) {
        if (GetPrivateProfileStringA("Keyboard", k_ctl[i].ini, "\x01", buf, sizeof buf, ini) &&
            buf[0] != '\x01')
            m->key[i] = key_parse(buf);
    }
    for (i = 0; i < CTL_PAD_COUNT; i++) {
        if (GetPrivateProfileStringA("Controller", k_ctl[i].ini, "\x01", buf, sizeof buf, ini) &&
            buf[0] != '\x01') {
            m->pad[i] = PAD_NONE;
            for (j = 0; j < PAD_COUNT; j++)
                if (!_stricmp(buf, k_pad[j].ini)) m->pad[i] = (BYTE)j;
        }
    }
}

void controls_write(const ControlMap *m, FILE *f)
{
    int i;
    char name[32];
    fprintf(f, "\n[Keyboard]\n; One key per Xbox control: a letter or digit, F1-F24, Space, Enter,\n"
               "; Escape, Tab, Up/Down/Left/Right, LeftShift, Num0-Num9 ... or None.\n");
    for (i = 0; i < CTL_COUNT; i++) {
        key_name(m->key[i], name, sizeof name);
        fprintf(f, "%s=%s\n", k_ctl[i].ini, name[0] ? name : "None");
    }
    fprintf(f, "\n[Controller]\n; The PC controller input for each Xbox control: A B X Y LB RB LT RT\n"
               "; Start Back LS RS DpadUp DpadDown DpadLeft DpadRight, or None.\n");
    for (i = 0; i < CTL_PAD_COUNT; i++)
        fprintf(f, "%s=%s\n", k_ctl[i].ini, k_pad[m->pad[i] < PAD_COUNT ? m->pad[i] : 0].ini);
}

/* ── The live mapping ──────────────────────────────────────────────── */

static SRWLOCK s_lock = SRWLOCK_INIT;
static ControlMap s_map;
static volatile LONG s_map_set = 0;
static volatile LONG s_suspended = 0;

void controls_set_current(const ControlMap *m)
{
    AcquireSRWLockExclusive(&s_lock);
    s_map = *m;
    s_map_set = 1;
    ReleaseSRWLockExclusive(&s_lock);
}

void controls_get_current(ControlMap *m)
{
    AcquireSRWLockShared(&s_lock);
    if (s_map_set) *m = s_map;
    ReleaseSRWLockShared(&s_lock);
    if (!s_map_set) controls_defaults(m);
}

void controls_set_suspended(BOOL on) { InterlockedExchange(&s_suspended, on ? 1 : 0); }
BOOL controls_suspended(void)        { return s_suspended != 0; }

/* Console button bit or analog index of each control. */
static void apply(ControlsPad *p, int ctl, BYTE value)
{
    static const WORD digital[CTL_PAD_COUNT] = {
        [CTL_START] = 0x10, [CTL_BACK] = 0x20, [CTL_L3] = 0x40, [CTL_R3] = 0x80,
        [CTL_DUP] = 0x01, [CTL_DDOWN] = 0x02, [CTL_DLEFT] = 0x04, [CTL_DRIGHT] = 0x08,
    };
    if (!value) return;
    if (ctl <= CTL_RT) {
        if (value > p->analog[ctl]) p->analog[ctl] = value;
    } else if (ctl < CTL_PAD_COUNT) {
        p->buttons |= digital[ctl];
    } else {
        switch (ctl) {
        case CTL_LS_UP:    p->ly =  32767; break;
        case CTL_LS_DOWN:  p->ly = -32767; break;
        case CTL_LS_LEFT:  p->lx = -32767; break;
        case CTL_LS_RIGHT: p->lx =  32767; break;
        case CTL_RS_UP:    p->ry =  32767; break;
        case CTL_RS_DOWN:  p->ry = -32767; break;
        case CTL_RS_LEFT:  p->rx = -32767; break;
        case CTL_RS_RIGHT: p->rx =  32767; break;
        }
    }
}

/* 0..255 for a controller input: triggers are analog, buttons 0 or 255. */
static BYTE pad_value(const XINPUT_GAMEPAD *g, int src)
{
    if (src == PAD_LT) return g->bLeftTrigger;
    if (src == PAD_RT) return g->bRightTrigger;
    if (src > PAD_NONE && src < PAD_COUNT && k_pad_bit[src])
        return (g->wButtons & k_pad_bit[src]) ? 255 : 0;
    return 0;
}

BOOL controls_read_pad(DWORD port, ControlsPad *out)
{
    XINPUT_STATE st;
    ControlMap m;
    int i;

    memset(out, 0, sizeof *out);
    memset(&st, 0, sizeof st);
    if (port >= 4 || XInputGetState(port, &st) != ERROR_SUCCESS) return FALSE;
    if (s_suspended) return TRUE;               /* connected, but idle for now */
    controls_get_current(&m);
    for (i = 0; i < CTL_PAD_COUNT; i++) {
        BYTE v = pad_value(&st.Gamepad, m.pad[i]);
        /* A trigger driving a digital control needs a firm press. */
        if (i > CTL_RT && v && v < 30) v = 0;
        apply(out, i, v);
    }
    out->lx = st.Gamepad.sThumbLX;
    out->ly = st.Gamepad.sThumbLY;
    out->rx = st.Gamepad.sThumbRX;
    out->ry = st.Gamepad.sThumbRY;
    return TRUE;
}

void controls_read_keyboard(ControlsPad *io)
{
    ControlMap m;
    int i;
    HWND game = d3d8_GetHostWindow();
    /* Only while the game window itself is in front: not in one of our
     * dialogs, not in another program. */
    if (s_suspended || !game || GetForegroundWindow() != game) return;
    controls_get_current(&m);
    for (i = 0; i < CTL_COUNT; i++)
        if (m.key[i] && (GetAsyncKeyState(m.key[i]) & 0x8000))
            apply(io, i, 255);
}

/* ── The Controls window ───────────────────────────────────────────── */

enum {
    ID_C_LIST = 300, ID_C_SETKEY, ID_C_CLRKEY, ID_C_SETPAD, ID_C_CLRPAD,
    ID_C_DEFAULTS, ID_C_STATUS, ID_C_TIMER = 7,
};

typedef struct {
    HWND  hwnd, list;
    HFONT font;
    UINT  dpi;
    ControlMap map;
    int   capture;          /* 0 none, 1 key, 2 controller button */
    int   capture_row;
    DWORD capture_start;
    WORD  pad_base[4];
    BYTE  trig_base[4][2];
    BOOL  done, ok;
} ControlsUI;

static ControlsUI *s_cui;

static int SC(const ControlsUI *u, int v) { return MulDiv(v, (int)u->dpi, 96); }

/* Wide list-view calls: the ListView_* macros follow UNICODE, which this
 * project does not define, and would send the ANSI messages. */
static void lv_set_text(HWND lv, int row, int col, const WCHAR *text)
{
    LVITEMW it;
    memset(&it, 0, sizeof it);
    it.iSubItem = col;
    it.pszText = (LPWSTR)text;
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&it);
}

static void cui_row_text(ControlsUI *u, int row)
{
    char name[32];
    WCHAR w[64];
    key_name(u->map.key[row], name, sizeof name);
    MultiByteToWideChar(CP_ACP, 0, name, -1, w, 64);
    lv_set_text(u->list, row, 1, w);
    if (row < CTL_PAD_COUNT)
        lv_set_text(u->list, row, 2, k_pad[u->map.pad[row] < PAD_COUNT ? u->map.pad[row] : 0].label);
    else
        lv_set_text(u->list, row, 2, row < CTL_RS_UP ? L"Left stick" : L"Right stick");
}

static void cui_status(ControlsUI *u, const WCHAR *text)
{
    SetDlgItemTextW(u->hwnd, ID_C_STATUS, text);
}

static int cui_row(ControlsUI *u)
{
    int r = ListView_GetNextItem(u->list, -1, LVNI_SELECTED);
    return r < 0 ? 0 : r;
}

static void cui_capture(ControlsUI *u, int mode)
{
    WCHAR msg[160];
    int row = cui_row(u), i;
    if (mode == 2 && row >= CTL_PAD_COUNT) {
        cui_status(u, L"The sticks are passed straight through on a controller.");
        return;
    }
    u->capture = mode;
    u->capture_row = row;
    u->capture_start = GetTickCount();
    for (i = 0; i < 4; i++) {
        XINPUT_STATE st;
        memset(&st, 0, sizeof st);
        XInputGetState((DWORD)i, &st);
        u->pad_base[i] = st.Gamepad.wButtons;
        u->trig_base[i][0] = st.Gamepad.bLeftTrigger;
        u->trig_base[i][1] = st.Gamepad.bRightTrigger;
    }
    swprintf(msg, 160, mode == 1 ? L"Press a key for %ls (Esc cancels)"
                                 : L"Press a controller button for %ls (Esc cancels)",
             k_ctl[row].label);
    cui_status(u, msg);
    SetFocus(u->list);
}

static void cui_capture_end(ControlsUI *u, const WCHAR *text)
{
    u->capture = 0;
    cui_status(u, text ? text : L"Double-click a binding to change it.");
}

static void cui_poll_pad(ControlsUI *u)
{
    int i, s;
    if (u->capture != 2) return;
    if (GetTickCount() - u->capture_start > 8000) { cui_capture_end(u, L"No button was pressed."); return; }
    for (i = 0; i < 4; i++) {
        XINPUT_STATE st;
        memset(&st, 0, sizeof st);
        if (XInputGetState((DWORD)i, &st) != ERROR_SUCCESS) continue;
        for (s = PAD_A; s < PAD_COUNT; s++) {
            BOOL now, before;
            if (s == PAD_LT) { now = st.Gamepad.bLeftTrigger > 100; before = u->trig_base[i][0] > 100; }
            else if (s == PAD_RT) { now = st.Gamepad.bRightTrigger > 100; before = u->trig_base[i][1] > 100; }
            else { now = (st.Gamepad.wButtons & k_pad_bit[s]) != 0; before = (u->pad_base[i] & k_pad_bit[s]) != 0; }
            if (now && !before) {
                u->map.pad[u->capture_row] = (BYTE)s;
                cui_row_text(u, u->capture_row);
                cui_capture_end(u, NULL);
                return;
            }
        }
        u->pad_base[i] = st.Gamepad.wButtons;
        u->trig_base[i][0] = st.Gamepad.bLeftTrigger;
        u->trig_base[i][1] = st.Gamepad.bRightTrigger;
    }
}

/* A key pressed while capturing. TRUE if it was consumed. */
static BOOL cui_key(ControlsUI *u, const MSG *msg)
{
    UINT vk = (UINT)msg->wParam;
    if (!u->capture || (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)) return FALSE;
    if (vk == VK_ESCAPE) { cui_capture_end(u, NULL); return TRUE; }
    if (u->capture != 1) return TRUE;
    if (vk == VK_SHIFT || vk == VK_CONTROL) {
        UINT sc = (UINT)((msg->lParam >> 16) & 0xFF) | ((msg->lParam & (1 << 24)) ? 0xE000u : 0u);
        UINT lr = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
        if (lr) vk = lr;
        if (vk == VK_CONTROL) vk = (msg->lParam & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
    }
    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_F10 || vk == VK_F12 ||
        vk == VK_LWIN || vk == VK_RWIN || vk > 255) {
        cui_status(u, L"That key is reserved (Alt, F10 and F12 are the window's). Try another.");
        return TRUE;
    }
    u->map.key[u->capture_row] = (BYTE)vk;
    cui_row_text(u, u->capture_row);
    cui_capture_end(u, NULL);
    return TRUE;
}

static void cui_layout(ControlsUI *u)
{
    RECT r = { 0, 0, SC(u, 560), SC(u, 540) };
    MONITORINFO mi;
    struct { int id, x, y, w, h; } pos[] = {
        { ID_C_LIST, 16, 16, 528, 380 },
        { ID_C_SETKEY, 16, 404, 100, 28 }, { ID_C_CLRKEY, 120, 404, 90, 28 },
        { ID_C_SETPAD, 222, 404, 140, 28 }, { ID_C_CLRPAD, 366, 404, 94, 28 },
        { ID_C_DEFAULTS, 16, 440, 140, 28 },
        { ID_C_STATUS, 16, 474, 528, 20 },
        { IDOK, 352, 502, 94, 28 }, { IDCANCEL, 450, 502, 94, 28 },
    };
    size_t i;
    if (u->font) DeleteObject(u->font);
    u->font = CreateFontW(-MulDiv(10, (int)u->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    for (i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        HWND c = GetDlgItem(u->hwnd, pos[i].id);
        SetWindowPos(c, NULL, SC(u, pos[i].x), SC(u, pos[i].y), SC(u, pos[i].w), SC(u, pos[i].h),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(c, WM_SETFONT, (WPARAM)u->font, TRUE);
    }
    ListView_SetColumnWidth(u->list, 0, SC(u, 190));
    ListView_SetColumnWidth(u->list, 1, SC(u, 150));
    ListView_SetColumnWidth(u->list, 2, SC(u, 160));
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(u->hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongPtrW(u->hwnd, GWL_EXSTYLE));
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(u->hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(u->hwnd, NULL,
                 mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - (r.right - r.left)) / 2,
                 mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - (r.bottom - r.top)) / 2,
                 r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

static LRESULT CALLBACK controls_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    ControlsUI *u = s_cui;
    switch (msg) {
    case WM_COMMAND:
        if (!u) break;
        switch (LOWORD(wp)) {
        case ID_C_SETKEY: cui_capture(u, 1); return 0;
        case ID_C_SETPAD: cui_capture(u, 2); return 0;
        case ID_C_CLRKEY: {
            int r = cui_row(u);
            u->map.key[r] = 0; cui_row_text(u, r); return 0;
        }
        case ID_C_CLRPAD: {
            int r = cui_row(u);
            if (r < CTL_PAD_COUNT) { u->map.pad[r] = PAD_NONE; cui_row_text(u, r); }
            return 0;
        }
        case ID_C_DEFAULTS: {
            int r;
            controls_defaults(&u->map);
            for (r = 0; r < CTL_COUNT; r++) cui_row_text(u, r);
            cui_status(u, L"Default bindings restored. OK keeps them.");
            return 0;
        }
        case IDOK:     if (!u->capture) { u->ok = TRUE; u->done = TRUE; } return 0;
        case IDCANCEL: if (u->capture) cui_capture_end(u, NULL); else u->done = TRUE; return 0;
        }
        break;
    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR *)lp;
        if (u && nh->idFrom == ID_C_LIST && nh->code == NM_DBLCLK) {
            const NMITEMACTIVATE *ia = (const NMITEMACTIVATE *)lp;
            if (ia->iItem >= 0) cui_capture(u, ia->iSubItem == 2 ? 2 : 1);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (u && wp == ID_C_TIMER) cui_poll_pad(u);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(60, 68, 80));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_CLOSE:
        if (u) u->done = TRUE;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

BOOL controls_dialog(HWND owner, ControlMap *m)
{
    static const WCHAR *cls = L"SsxControls";
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    ControlsUI u;
    LVCOLUMNW col;
    MSG msg;
    int r;
    BOOL was_suspended = controls_suspended();
    HMODULE user = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn dpi_fn = user ? (GetDpiForWindowFn)(void *)GetProcAddress(user, "GetDpiForWindow") : NULL;

    if (s_cui) return FALSE;                     /* already open */
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    memset(&u, 0, sizeof u);
    u.map = *m;
    s_cui = &u;

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = controls_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    u.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, cls, L"Controls",
                             WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, owner, NULL, wc.hInstance, NULL);
    if (!u.hwnd) { s_cui = NULL; return FALSE; }
    u.dpi = dpi_fn ? dpi_fn(u.hwnd) : 96;
    if (!u.dpi) u.dpi = 96;

    u.list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | LVS_REPORT |
                             LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                             0, 0, 10, 10, u.hwnd, (HMENU)(INT_PTR)ID_C_LIST, wc.hInstance, NULL);
    ListView_SetExtendedListViewStyle(u.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    memset(&col, 0, sizeof col);
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 100;
    col.pszText = L"Xbox control"; SendMessageW(u.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    col.pszText = L"Keyboard";     SendMessageW(u.list, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
    col.pszText = L"Controller";   SendMessageW(u.list, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
    for (r = 0; r < CTL_COUNT; r++) {
        LVITEMW it;
        memset(&it, 0, sizeof it);
        it.mask = LVIF_TEXT;
        it.iItem = r;
        it.pszText = (LPWSTR)k_ctl[r].label;
        SendMessageW(u.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        cui_row_text(&u, r);
    }
    ListView_SetItemState(u.list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

#define CUI_CHILD(cls_, text, style, id) \
    CreateWindowExW(0, cls_, text, WS_CHILD | WS_VISIBLE | (style), 0, 0, 10, 10, u.hwnd, \
                    (HMENU)(INT_PTR)(id), wc.hInstance, NULL)
    CUI_CHILD(L"BUTTON", L"Set key", WS_TABSTOP | BS_PUSHBUTTON, ID_C_SETKEY);
    CUI_CHILD(L"BUTTON", L"Clear key", WS_TABSTOP | BS_PUSHBUTTON, ID_C_CLRKEY);
    CUI_CHILD(L"BUTTON", L"Set controller button", WS_TABSTOP | BS_PUSHBUTTON, ID_C_SETPAD);
    CUI_CHILD(L"BUTTON", L"Clear button", WS_TABSTOP | BS_PUSHBUTTON, ID_C_CLRPAD);
    CUI_CHILD(L"BUTTON", L"Restore defaults", WS_TABSTOP | BS_PUSHBUTTON, ID_C_DEFAULTS);
    CUI_CHILD(L"STATIC", L"Double-click a binding to change it.", SS_LEFT, ID_C_STATUS);
    CUI_CHILD(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    CUI_CHILD(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
#undef CUI_CHILD

    cui_layout(&u);
    controls_set_suspended(TRUE);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(u.hwnd, SW_SHOW);
    SetForegroundWindow(u.hwnd);
    SetFocus(u.list);
    SetTimer(u.hwnd, ID_C_TIMER, 30, NULL);

    while (!u.done) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got <= 0) { PostQuitMessage((int)msg.wParam); break; }
        if (cui_key(&u, &msg)) continue;
        if (msg.message == WM_KEYDOWN && msg.hwnd == u.list && !u.capture) {
            if (msg.wParam == VK_RETURN) { cui_capture(&u, 1); continue; }
            if (msg.wParam == VK_DELETE) { SendMessageW(u.hwnd, WM_COMMAND, ID_C_CLRKEY, 0); continue; }
        }
        if (!IsDialogMessageW(u.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    KillTimer(u.hwnd, ID_C_TIMER);
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
    DestroyWindow(u.hwnd);
    if (u.font) DeleteObject(u.font);
    controls_set_suspended(was_suspended);
    s_cui = NULL;
    if (u.ok) *m = u.map;
    return u.ok;
}
