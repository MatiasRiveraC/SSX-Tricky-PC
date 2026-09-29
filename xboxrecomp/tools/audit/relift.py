#!/usr/bin/env python3
"""Re-lift functions that already have bodies, and replace them in place.

`recover_batch.py` recovers functions the tree does not have. This is the other
half: after a lifter fix, the functions that were *already* translated still
carry the old, wrong translation, and splicing a second copy just fails to
link. Fixing the lifter without this step changes nothing in the build.

    relift.py 0x001493E0 0x00149450 ...
    relift.py --dry-run 0x001493E0        # show the diff, change nothing

Bodies are matched by `void <name>(void)` through the closing brace at column
zero, in whichever generated file holds them, and a backup of every touched
file goes to RE_NOTES/recover_batches/relift-<stamp>/ first. Verify with
xbrun.py afterwards: this replaces working code, so it can regress.
"""

import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
XBOX = os.path.join(ROOT, "xboxrecomp")
XBE = os.path.join(ROOT, "Game Data", "default.xbe")
ANALYSIS = os.path.join(ROOT, "xboxrecomp_output", "ssx_analysis.json")
GEN = os.path.join(ROOT, "ssx_recomp", "src", "recomp", "gen")
# Lift call targets under the names the tree already uses (lifter._tree_names).
os.environ.setdefault("XLIFT_NAME_MAP", os.path.join(GEN, "recomp_dispatch.c"))
BACKUPS = os.path.join(ROOT, "RE_NOTES", "recover_batches")
BODY = re.compile(r"^void (sub_[0-9A-F]{8}|[A-Za-z_]\w*)\(void\)$", re.M)


def run(cmd, cwd=None, timeout=900):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def lift(work, addrs):
    os.makedirs(work, exist_ok=True)
    seeds = os.path.join(work, "seeds.json")
    io.open(seeds, "w", encoding="utf-8").write(
        "[" + ",".join('{"start":"0x%08X"}' % a for a in addrs) + "]")
    rc, out = run([sys.executable, "-m", "tools.disasm", XBE, "-o",
                   os.path.join(work, "disasm"), "--text-only", "--force",
                   "--analysis-json", ANALYSIS, "--seed-functions", seeds],
                  cwd=XBOX)
    if rc != 0:
        sys.exit("disasm failed:\n" + out[-2000:])
    run([sys.executable, "-m", "tools.func_id", XBE,
         "--functions", os.path.join(work, "disasm", "functions.json"),
         "--strings", os.path.join(work, "disasm", "strings.json"),
         "--xrefs", os.path.join(work, "disasm", "xrefs.json"),
         "-o", os.path.join(work, "funcid")], cwd=XBOX)
    out_bodies = {}
    for a in addrs:
        rc, out = run([sys.executable, "-m", "tools.recomp", XBE, "-f",
                       "0x%08X" % a, "-o", os.path.join(work, "o"),
                       "--disasm-dir", os.path.join(work, "disasm"),
                       "--func-id-dir", os.path.join(work, "funcid"),
                       "--skip-binary-check"], cwd=XBOX)
        keep, on, name = [], False, None
        for line in out.split("\n"):
            line = line.rstrip("\r")
            m = BODY.match(line)
            if m:
                on, name = True, m.group(1)
            if on:
                keep.append(line)
            if on and line == "}":
                break
        if name and keep:
            out_bodies[name] = "\n".join(keep)
    return out_bodies


def find_body(text, name):
    """(start, end) of `void name(void) { ... }` including the closing brace."""
    m = re.search(r"^void %s\(void\)$" % re.escape(name), text, re.M)
    if not m:
        return None
    open_brace = text.find("{", m.end())
    if open_brace < 0:
        return None
    close = text.find("\n}\n", open_brace)
    if close < 0:
        return None
    return m.start(), close + 3


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addrs", nargs="+")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    addrs = [int(x, 0) for x in a.addrs]

    tag = "relift-" + time.strftime("%Y%m%d-%H%M%S")
    work = os.path.join(BACKUPS, tag, "work")
    bodies = lift(work, addrs)
    print("lifted %d of %d" % (len(bodies), len(addrs)))

    files = sorted(f for f in os.listdir(GEN) if f.endswith(".c"))
    touched, replaced = set(), 0
    for name, body in sorted(bodies.items()):
        for fn in files:
            path = os.path.join(GEN, fn)
            text = io.open(path, encoding="utf-8", errors="surrogateescape").read()
            span = find_body(text, name)
            if not span:
                continue
            old = text[span[0]:span[1]]
            if old.strip() == body.strip():
                print("  %-34s unchanged" % name)
                break
            print("  %-34s %s  %d -> %d bytes"
                  % (name, fn, len(old), len(body)))
            if not a.dry_run:
                if fn not in touched:
                    d = os.path.join(BACKUPS, tag)
                    os.makedirs(d, exist_ok=True)
                    shutil.copyfile(path, os.path.join(d, fn))
                    touched.add(fn)
                io.open(path, "w", encoding="utf-8", errors="surrogateescape",
                        newline="").write(text[:span[0]] + body + "\n"
                                          + text[span[1]:])
            replaced += 1
            break
        else:
            print("  %-34s NOT FOUND in any generated file" % name)
    if a.dry_run:
        print("dry run -- nothing written")
    else:
        print("replaced %d bod%s; backup -> %s"
              % (replaced, "y" if replaced == 1 else "ies",
                 os.path.join(BACKUPS, tag)))
        # A re-lifted body arrives without RECOMP_LOC(), which would make it
        # invisible to runtime probes. instrument_labels.py is idempotent.
        rc = subprocess.call(
            [sys.executable,
             os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "instrument_labels.py")],
            cwd=ROOT, stdout=subprocess.DEVNULL)
        if rc != 0:
            print("  WARNING: label instrumentation failed -- runtime probes "
                  "will miss the re-lifted bodies")
    return 0


if __name__ == "__main__":
    sys.exit(main())
