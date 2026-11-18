#!/usr/bin/env python3
"""Audit a guest vtable against the recompiler's dispatch table.

Four separate blockers this project has hit were the same shape: an object's
method table points at an address that has no translated body, or has one that
is not registered, so the indirect call is dropped silently. The call returns
whatever was in `eax`, and the title sits waiting on an answer that is never
computed.

    vtaudit.py 0x0019A724                  # audit one vtable
    vtaudit.py --find-screens              # locate every screen-class vtable
    vtaudit.py 0x0019A724 --slots 24       # force a slot count

For each slot it reports the target address, the registered symbol, and
whether `recomp_lookup`'s **binary search** can actually reach it -- an entry
present but out of sort order is unreachable no matter how correct its body
is, which is its own separate failure mode (see RE_NOTES part 113).

Two traps this tool exists to avoid, both of which produced confident wrong
answers when the same check was done by hand:

  * The generated sources are CRLF, so a grep anchored `^void name(void)$`
    never matches -- it once reported every slot of a fully-translated vtable
    as missing.
  * A placeholder written on one line is still a definition, so "no body"
    from a line-oriented search can mean "there is a stub here that draws
    nothing".

The dispatch table is used as the oracle instead: an entry there has to
resolve to a defined symbol or the link fails.
"""

import argparse
import ssxpaths
import io
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
XBE = ssxpaths.XBE
DISPATCH = os.path.join(ssxpaths.GEN, "recomp_dispatch.c")

ENTRY = re.compile(r"^\s*\{ 0x([0-9A-Fa-f]{8})u, \(recomp_func_t\)"
                   r"([A-Za-z_][A-Za-z0-9_]*)")


def load_sections(data):
    """(name, va, vsize, raw) for each XBE section."""
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    table = struct.unpack_from("<I", data, 0x120)[0] - base
    out = []
    for i in range(count):
        off = table + i * 0x38
        va, vsize, raw, _rsize = struct.unpack_from("<IIII", data, off + 4)
        nameptr = struct.unpack_from("<I", data, off + 0x14)[0] - base
        end = data.index(b"\x00", nameptr)
        out.append((data[nameptr:end].decode("ascii", "replace"),
                    va, vsize, raw))
    return out


class Image(object):
    def __init__(self, path):
        self.data = io.open(path, "rb").read()
        self.secs = load_sections(self.data)
        text = [s for s in self.secs if s[0] == ".text"][0]
        self.text_lo, self.text_hi = text[1], text[1] + text[2]

    def u32(self, va):
        for _name, v, vsize, raw in self.secs:
            if v <= va < v + vsize:
                off = raw + (va - v)
                if off + 4 <= len(self.data):
                    return struct.unpack_from("<I", self.data, off)[0]
        return None

    def is_code(self, x):
        return x is not None and self.text_lo <= x < self.text_hi


def load_dispatch(path):
    rows = []
    for line in io.open(path, encoding="utf-8", errors="surrogateescape"):
        m = ENTRY.match(line.rstrip("\r\n"))
        if m:
            rows.append((int(m.group(1), 16), m.group(2)))
    return rows


def make_reachable(rows):
    """Mirror recomp_lookup exactly, including its sortedness assumption."""
    addrs = [a for a, _ in rows]

    def reachable(key):
        lo, hi = 0, len(addrs)
        while lo < hi:
            mid = lo + (hi - lo) // 2
            if addrs[mid] < key:
                lo = mid + 1
            elif addrs[mid] > key:
                hi = mid
            else:
                return True
        return False
    return reachable


def referenced_from_text(img, va):
    """A real vtable is stored into objects, so .text holds its address.

    Without this check a walk-back over .data can run into unrelated words
    that merely look like code addresses and report a confident audit of
    something that is not a vtable at all -- observed on 0x001C72C0, whose
    18 "methods" all started mid-instruction.
    """
    text = [s for s in img.secs if s[0] == ".text"][0]
    seg = img.data[text[3]:text[3] + text[2]]
    return seg.find(struct.pack("<I", va)) >= 0


def audit(img, rows, base, slots=None):
    names = {a: n for a, n in rows}
    reachable = make_reachable(rows)
    targets = []
    i = 0
    while slots is None or i < slots:
        t = img.u32(base + 4 * i)
        if not img.is_code(t):
            break
        targets.append(t)
        i += 1
        if slots is None and i > 256:
            break
    print("vtable 0x%08X -- %d slot(s)" % (base, len(targets)))
    if not referenced_from_text(img, base):
        print("  WARNING: nothing in .text stores this address, so it is "
              "probably not a vtable.")
    missing, unreachable = [], []
    for i, t in enumerate(targets):
        off = i * 4
        if t not in names:
            print("  +0x%02X = 0x%08X   *** NOT REGISTERED ***" % (off, t))
            missing.append(t)
        elif not reachable(t):
            print("  +0x%02X = 0x%08X   %-34s *** UNREACHABLE ***"
                  % (off, t, names[t]))
            unreachable.append(t)
        else:
            print("  +0x%02X = 0x%08X   %s" % (off, t, names[t]))
    print("")
    if missing:
        print("not registered: %s"
              % " ".join("0x%08X" % a for a in sorted(set(missing))))
    if unreachable:
        print("registered but unreachable by the binary search: %s"
              % " ".join("0x%08X" % a for a in sorted(set(unreachable))))
    if not missing and not unreachable:
        print("every slot resolves.")
    return missing, unreachable


def find_screens(img):
    """Screen classes share a base vtable their destructors restore."""
    base_vt = 0x0019654C
    pat = struct.pack("<I", base_vt)
    text = [s for s in img.secs if s[0] == ".text"][0]
    seg = img.data[text[3]:text[3] + text[2]]
    sites = []
    i = 0
    while True:
        j = seg.find(pat, i)
        if j < 0:
            break
        sites.append(text[1] + j)
        i = j + 1
    print("destructors restoring base vtable 0x%08X: %s"
          % (base_vt, " ".join("0x%08X" % s for s in sites)))
    seen = set()
    for _name, v, vsize, raw in img.secs:
        if _name == ".text":
            continue
        end = min(raw + vsize, len(img.data)) - 4
        for off in range(raw, end, 4):
            p = struct.unpack_from("<I", img.data, off)[0]
            if not any(s - 0x40 <= p <= s for s in sites):
                continue
            va = v + (off - raw)
            b = va
            while img.is_code(img.u32(b - 4)) and va - b < 0x400:
                b -= 4
            if b in seen:
                continue
            seen.add(b)
            n = 0
            while img.is_code(img.u32(b + 4 * n)):
                n += 1
            print("  vtable 0x%08X  %2d slots  (destructor at +0x%02X)"
                  % (b, n, va - b))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("vtable", nargs="?", help="vtable VA, e.g. 0x0019A724")
    ap.add_argument("--slots", type=int, default=None)
    ap.add_argument("--xbe", default=XBE)
    ap.add_argument("--dispatch", default=DISPATCH)
    ap.add_argument("--find-screens", action="store_true")
    args = ap.parse_args()

    img = Image(args.xbe)
    if args.find_screens:
        find_screens(img)
        return
    if not args.vtable:
        ap.error("give a vtable address, or --find-screens")
    rows = load_dispatch(args.dispatch)
    if not rows:
        sys.exit("no dispatch entries parsed from %s" % args.dispatch)
    missing, unreachable = audit(img, rows, int(args.vtable, 16), args.slots)
    sys.exit(1 if (missing or unreachable) else 0)


if __name__ == "__main__":
    main()
