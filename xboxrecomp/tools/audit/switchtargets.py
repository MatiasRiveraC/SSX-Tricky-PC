#!/usr/bin/env python3
"""List jump-table targets that no generated function or dispatch entry covers.

A `switch` compiles to `jmp [reg*4 + table]`. When the disassembler ends the
function at that jmp -- it is an unconditional jump, and the case bodies that
follow were never reached by its own walk -- the lifter emits

    RECOMP_ITAIL(MEM32(eax * 4 + 0x84864));

and every case is an ICALL-style miss at run time: the tail jump goes nowhere
and the caller continues with whatever eax held. Found in part 178 at
sub_00084780, where pressing A during the intro picked a missing case and the
title then called through garbage vtables and divided by zero.

This decodes every such table straight from the XBE (entries are read until
one stops pointing into .text) and prints the unregistered targets, ready for
recover_batch.py:

    switchtargets.py              # summary + addresses
    switchtargets.py --addrs      # just the addresses, space separated
"""
import argparse, glob, io, os, re, struct
import ssxpaths
from vtsweep import sections

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
GEN = ssxpaths.GEN
XBE = ssxpaths.XBE
SITE = re.compile(r"RECOMP_ITAIL\(MEM32\((\w+) \* 4 \+ 0x([0-9A-Fa-f]+)\)\)")
BOUND = re.compile(r"CMP_A\((\w+), (0x[0-9A-Fa-f]+|\d+)\)")
BYTEIDX = re.compile(r"(\w+) = ZX8\(MEM8\((\w+) \+ 0x([0-9A-Fa-f]+)\)\)")
TEXT_LO, TEXT_HI = 0x00011000, 0x00187000   # .text through XPP: every x86 code section


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addrs", action="store_true")
    ap.add_argument("--max-entries", type=int, default=256)
    args = ap.parse_args()
    data = io.open(XBE, "rb").read()
    secs = sections(data)

    def foff(va):
        # Each section has its own file offset. VA - 0x10000 holds only for
        # .text; the 10 jump tables in D3D were read 0x80 bytes off with it
        # until part 180.
        for nm, flags, sva, vsz, raw, rsz in secs:
            if sva <= va < sva + rsz:
                return raw + va - sva
        raise ValueError("VA 0x%08X is not file-backed" % va)
    reg = set()
    for l in io.open(os.path.join(GEN, "recomp_dispatch.c"), encoding="utf-8", errors="replace"):
        m = re.search(r"\{ 0x([0-9A-F]{8})u", l)
        if m:
            reg.add(int(m.group(1), 16))
    tables, missing = {}, set()
    for path in sorted(glob.glob(os.path.join(GEN, "recomp_*.c"))):
        lines = io.open(path, encoding="utf-8", errors="replace").read().splitlines()
        for n, line in enumerate(lines):
            m = SITE.search(line)
            if not m:
                continue
            base = int(m.group(2), 16)
            if not (TEXT_LO <= base < TEXT_HI) or base in tables:
                continue
            # The switch's own bound: `cmp reg, N / ja default` just above.
            # A two-level switch indexes the dword table through a byte table
            # (`ecx = ZX8(MEM8(eax + 0xBYTES))`): then the dword count is the
            # largest byte in the first N+1, and reading past it walks into
            # the byte table itself -- that produced the non-code "target"
            # 0x00100100 in part 179.
            count = args.max_entries
            bytetab = None
            for back in lines[max(0, n - 8):n][::-1]:
                bt = BYTEIDX.search(back)
                if bt and bt.group(1) == m.group(1) and bytetab is None:
                    bytetab = (bt.group(2), int(bt.group(3), 16))
                    continue
                b = BOUND.search(back)
                if b and (b.group(1) == m.group(1) or (bytetab and b.group(1) == bytetab[0])):
                    nb = int(b.group(2), 0) + 1
                    if bytetab:
                        off = foff(bytetab[1])
                        count = max(data[off:off + nb]) + 1
                    else:
                        count = nb
                    break
            ents = []
            for i in range(count):
                off = foff(base + 4 * i)
                v = struct.unpack_from("<I", data, off)[0]
                if not (TEXT_LO <= v < TEXT_HI):
                    break
                ents.append(v)
            tables[base] = ents
            missing |= {v for v in ents if v not in reg}
    if args.addrs:
        print(" ".join("0x%08X" % a for a in sorted(missing)))
        return
    bad = {b: [v for v in e if v not in reg] for b, e in tables.items()}
    print("%d jump tables in .text, %d with unregistered targets, %d targets missing"
          % (len(tables), sum(1 for v in bad.values() if v), len(missing)))
    for b, v in sorted(bad.items()):
        if v:
            print("  table 0x%08X: %d/%d missing" % (b, len(set(v)), len(set(tables[b]))))


if __name__ == "__main__":
    main()
