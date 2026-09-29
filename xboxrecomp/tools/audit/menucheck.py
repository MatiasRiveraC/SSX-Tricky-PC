#!/usr/bin/env python3
"""Regression check for the frontend: walk into the menus and compare them
against reference frames from a known-good build.

The boot gate (recover_batch.py, xbrun.py) never presses a button, so it only
ever sees the intro. Part 180 broke the frontend three times while that gate
stayed 3/3 clean: an `fnstsw` change blanked the title art and 3D menus, and
removing the x87 ST(0) return hand-off left the 3D scenes mostly lit but
dropped the menu UI -- which a first version of this check, measuring only
how much of the screen was lit, passed. So it compares against references:

    menucheck_ref/title.png          title screen with its text
    menucheck_ref/select_mode.png    3D Select Mode with the UI panels
    menucheck_ref/select_event.png   Select Event (Race / Showoff / ...)
    menucheck_ref/difficulty.png     Select Difficulty
    menucheck_ref/venue.png          Select Venue

The walk taps START/A through the default choices and dumps a frame every 20
presents across the whole sequence; each reference must match *some* captured
frame (screens arrive at slightly different times run to run) within
MAX_DIFF mean absolute difference on a 160x120 grayscale thumbnail.

    menucheck.py                 # PASS/FAIL, exit status 0/1
    menucheck.py --keep DIR      # keep the captured frames
    menucheck.py --update-refs   # replace the references from this walk
                                 # (only from a build you have looked at)
"""
import argparse, glob, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BUILD = os.path.join(ROOT, "ssx_recomp", "build")
EXE = os.path.join(BUILD, "SSX Tricky.exe")
REFS = os.path.join(HERE, "menucheck_ref")
PRESSES = "START@11,START@14,A@18+1.5x16"
FIRST, EVERY, COUNT = 560, 20, 60   # presents now count real frames (part 182)
MAX_DIFF = 30.0   # calibrated part 180: good runs 4-16, missing UI 40+


def thumb(path):
    from PIL import Image
    return list(Image.open(path).convert("L").resize((160, 120)).getdata())


def diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / float(len(a))


def capture(out, seconds):
    for f in glob.glob(os.path.join(out, "*.bmp")):
        os.remove(f)
    env = dict(os.environ, XBOX_INPUT_HOST="0", XBOX_INPUT_AUTOPRESS=PRESSES,
               XBOX_D3D_DUMP=os.path.join(out, "f"), XBOX_D3D_DUMP_FROM=str(FIRST),
               XBOX_D3D_DUMP_EVERY=str(EVERY), XBOX_D3D_DUMP_MAX=str(COUNT))
    __import__("stale").kill_test_instances()
    p = subprocess.Popen([EXE], cwd=BUILD, env=env, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL)
    try:
        p.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
    return sorted(glob.glob(os.path.join(out, "*.bmp")))


def walk(args):
    out = args.keep or tempfile.mkdtemp(prefix="menucheck_")
    os.makedirs(out, exist_ok=True)
    shots = capture(out, args.seconds)
    thumbs = [(s, thumb(s)) for s in shots]
    ok = bool(thumbs)
    refs = sorted(glob.glob(os.path.join(REFS, "*.png")))
    if not refs:
        print("  no references in %s" % REFS)
        ok = False
    for r in refs:
        rt = thumb(r)
        best = min(((diff(rt, t), s) for s, t in thumbs), default=(999.0, "-"))
        good = best[0] <= MAX_DIFF
        ok = ok and good
        print("  %-14s best %5.1f  (%s)  %s" % (os.path.basename(r)[:-4], best[0],
              os.path.basename(best[1]), "ok" if good else "MISMATCH"))
    if not args.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keep", default=None)
    ap.add_argument("--seconds", type=float, default=42)
    ap.add_argument("--update-refs", action="store_true")
    args = ap.parse_args()
    if args.update_refs:
        sys.exit("--update-refs: pick frames by hand from a --keep capture into %s" % REFS)
    # One retry: the intermittent blank boot (~1 run in 15, a known open
    # defect) must not read as a menu regression; a real regression fails both.
    rc = walk(args)
    if rc:
        print("  first walk failed; retrying once")
        rc = walk(args)
        print("MENUCHECK %s" % ("PASS (after one retry)" if rc == 0 else "FAIL"))
    else:
        print("MENUCHECK PASS")
    return rc


if __name__ == "__main__":
    sys.exit(main())
