#!/usr/bin/env python3
"""xk.py ACTION... -- drive a running xemu (private config, keyboard on port 1).

Actions, run in order:
  START / A / B / X / Y / BACK / UP / DOWN / LEFT / RIGHT   tap that pad button
  NAME:ms          hold for ms (default 120)
  wait:s           sleep s seconds
  shot             F12 screenshot; prints the new PNG path
  shots:N:s        N screenshots s seconds apart
  close            ask xemu to exit (WM_CLOSE)

xemu's default keyboard map: A=a B=b X=x Y=y Start=Return Back=Backspace,
d-pad = arrow keys. Screenshots land in ./shots (screenshot_dir in xemu.toml).
"""
import ctypes, ctypes.wintypes as wt, glob, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, "shots")
u32 = ctypes.windll.user32
KEYS = {  # vk, scan, extended
    "A": (0x41, 0x1E, 0), "B": (0x42, 0x30, 0), "X": (0x58, 0x2D, 0), "Y": (0x59, 0x15, 0),
    "START": (0x0D, 0x1C, 0), "BACK": (0x08, 0x0E, 0),
    "UP": (0x26, 0x48, 1), "DOWN": (0x28, 0x50, 1), "LEFT": (0x25, 0x4B, 1), "RIGHT": (0x27, 0x4D, 1),
    "F12": (0x7B, 0x58, 0),
}


def xemu_window():
    found = []
    PROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(h, _):
        if u32.IsWindowVisible(h):
            n = u32.GetWindowTextLengthW(h)
            b = ctypes.create_unicode_buffer(n + 1)
            u32.GetWindowTextW(h, b, n + 1)
            if b.value.startswith("xemu"):
                found.append(h)
        return True
    u32.EnumWindows(PROC(cb), 0)
    return found[0] if found else None


def focus(h):
    if u32.GetForegroundWindow() == h:
        return
    u32.keybd_event(0x12, 0x38, 0, 0)          # an Alt tap lets SetForegroundWindow succeed
    u32.SetForegroundWindow(h)
    u32.keybd_event(0x12, 0x38, 2, 0)
    time.sleep(0.15)


def tap(name, ms=120):
    vk, sc, ext = KEYS[name]
    f = 1 if ext else 0
    u32.keybd_event(vk, sc, f, 0)
    time.sleep(ms / 1000.0)
    u32.keybd_event(vk, sc, f | 2, 0)
    time.sleep(0.05)


def shot(h):
    before = set(glob.glob(os.path.join(SHOTS, "*.png")))
    focus(h)
    tap("F12", 80)
    for _ in range(40):
        time.sleep(0.1)
        new = set(glob.glob(os.path.join(SHOTS, "*.png"))) - before
        if new:
            p = sorted(new)[-1]
            print("shot", p)
            sys.stdout.flush()
            time.sleep(1.05)                     # file names have one-second resolution
            return p
    print("shot FAILED")
    return None


def main():
    h = xemu_window()
    if not h:
        sys.exit("no xemu window")
    for a in sys.argv[1:]:
        name, _, arg = a.partition(":")
        name = name.upper()
        if name == "CLOSE":
            u32.PostMessageW(h, 0x0010, 0, 0)
            print("closing")
        elif name == "WAIT":
            time.sleep(float(arg))
        elif name == "SHOT":
            shot(h)
        elif name == "SHOTS":
            n, _, s = arg.partition(":")
            for _ in range(int(n)):
                shot(h)
                time.sleep(max(0.0, float(s or 1) - 1.05))
        else:
            focus(h)
            tap(name, int(arg) if arg else 120)
            print("pressed", name)


if __name__ == "__main__":
    main()
