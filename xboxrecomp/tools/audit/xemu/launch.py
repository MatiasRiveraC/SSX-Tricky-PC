#!/usr/bin/env python3
"""launch.py -- start the user's xemu on SSX Tricky with a private config.

Copies %APPDATA%/xemu/xemu/xemu.toml to ./xemu.toml with port 1 bound to the
keyboard and screenshots (F12) going to ./shots, then starts xemu with
-config_path so the user's own settings are never touched. Drive it with
xk.py (START / A / arrows / shot / shots:N:s); contact sheets with sheet.py.
Close it with WM_CLOSE (xk.py close), not a kill: the game writes to the HDD
image. Only the emulator is run -- the BIOS / boot ROM files it loads are the
user's and are not read or analysed here.
"""
import os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
XEMU = r"E:\Emulators\Xbox Original\xemu-win-release\xemu.exe"
src = os.path.join(os.environ["APPDATA"], "xemu", "xemu", "xemu.toml")
s = open(src, encoding="utf-8").read()
s = re.sub(r"(?m)^port1 = '[^']*'", "port1 = 'keyboard'", s)
shots = os.path.join(HERE, "shots")
os.makedirs(shots, exist_ok=True)
if "screenshot_dir" not in s:
    s = s.replace("[general]\n", "[general]\nscreenshot_dir = '%s'\n" % shots, 1)
cfg = os.path.join(HERE, "xemu.toml")
open(cfg, "w", encoding="utf-8").write(s)
p = subprocess.Popen([XEMU, "-config_path", cfg] + sys.argv[1:], cwd=os.path.dirname(XEMU))
print("xemu pid", p.pid)
