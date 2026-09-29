#!/usr/bin/env python3
"""Find every place in the image that stores a given address as data.

A function with no direct caller is not dead -- it is almost always a virtual
method, reached only through a table this recompiler never sees as code. Three
separate blockers took a whole part each to find because the question "who
calls this?" was answered with grep over the generated C, which by construction
can only show *direct* calls.

    dataxref.py 0x00148E00              # who points at VideoPlayer_Open?
    dataxref.py 0x00148E00 --vtable     # ...and which vtable slot is it
    dataxref.py 0x00148E00 --code       # also scan .text for the immediate

For each hit it reports the containing section and, with --vtable, walks
backwards to the start of the run of code pointers to name the table base and
the slot index -- which is what an ICALL site in the generated code actually
uses (`MEM32(vt + 0x6C)`), so it can be matched against one directly.

Deliberately *not* the same scan as `referenced_from_text` in vtaudit.py: that
one asks whether a candidate vtable is stored anywhere in code, this one asks
what refers to an arbitrary address, in either direction.
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vtaudit import Image, load_dispatch, DISPATCH  # noqa: E402


def slot_of(img, hit_va):
    """Walk back over consecutive code pointers to find the table base."""
    base = hit_va
    while True:
        prev = base - 4
        v = img.u32(prev)
        if not img.is_code(v):
            break
        base = prev
        if hit_va - base > 4096:      # runaway: not a table
            return None, None
    n = 0
    while img.is_code(img.u32(base + 4 * n)):
        n += 1
        if n > 512:
            break
    return base, (hit_va - base) // 4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--vtable", action="store_true",
                    help="resolve each data hit to a table base and slot")
    ap.add_argument("--code", action="store_true",
                    help="also report hits inside .text (immediates)")
    ap.add_argument("--xbe", default=None)
    a = ap.parse_args()

    va = int(a.addr, 0)
    xbe = a.xbe or os.path.join(
        os.path.normpath(os.path.join(HERE, "..", "..", "..")),
        "Game Data", "default.xbe")
    img = Image(xbe)
    names = {addr: n for addr, n in load_dispatch(DISPATCH)}

    print("references to 0x%08X (%s)"
          % (va, names.get(va, "<unregistered>")))
    pat = struct.pack("<I", va)
    total = 0
    for name, sva, vsize, raw in img.secs:
        if name == ".text" and not a.code:
            continue
        seg = img.data[raw:raw + vsize]
        i = seg.find(pat)
        while i >= 0:
            hit = sva + i
            total += 1
            line = "  %-8s 0x%08X" % (name, hit)
            if a.vtable and name != ".text":
                base, slot = slot_of(img, hit)
                if base is not None:
                    line += ("   vtable 0x%08X slot %d (+0x%02X)"
                             % (base, slot, slot * 4))
            print(line)
            i = seg.find(pat, i + 1)
    if not total:
        print("  none -- nothing in the image stores this address")
    return 0


if __name__ == "__main__":
    sys.exit(main())
