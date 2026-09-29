#!/usr/bin/env python3
"""List every code pointer stored in the XBE's data sections that has no
registered translation.

vtaudit.py checks one vtable. This sweeps all of them: every run of dwords in a
non-code section that point into .text is a method table, callback table or
function-pointer global, and each unregistered target is an indirect call that
will miss the first time the game reaches it. Part 180 found the post-skip
freeze that way, one run per missing method (0x000823E0, then three, then
five more); recovering a whole table at once avoids the walk.

    vtsweep.py                 # summary per table
    vtsweep.py --addrs         # unregistered targets, space separated
    vtsweep.py --near 0x000A7540,0x000810E0   # only tables holding these

Targets are filtered to plausible function starts: the byte before must be a
return, padding (0xCC / 0x90) or a jmp, or the target must already be a known
function in the Ghidra export. That drops pointers into the middle of code
(case labels, resume points) that would otherwise be lifted as bogus
functions.
"""
import argparse, io, os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
GEN = os.path.join(ROOT, "ssx_recomp", "src", "recomp", "gen")
XBE = os.path.join(ROOT, "ssx_recomp", "build", "game", "default.xbe")
EXPORT = os.path.join(ROOT, "default.xbe.c")
CODE_SECTIONS = (".text", "D3D", "D3DX", "XGRPH", "DSOUND", "XPP")


def sections(data):
    base = struct.unpack_from("<I", data, 0x104)[0]
    n, hdr = struct.unpack_from("<II", data, 0x11C)
    out = []
    for i in range(n):
        o = hdr - base + i * 0x38
        flags, va, vsz, raw, rsz, name = struct.unpack_from("<IIIIII", data, o)
        nm = data[name - base:name - base + 16].split(b"\0")[0].decode("ascii", "replace")
        out.append((nm, flags, va, vsz, raw, rsz))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--addrs", action="store_true")
    ap.add_argument("--near", default="")
    ap.add_argument("--min-run", type=int, default=2)
    ap.add_argument("--code-imm", metavar="TEXT_ASM",
                    help="instead scan a tools/disasm text.asm for code addresses used as "
                         "immediates (mov [g], fn / push fn): callbacks registered at run "
                         "time, which no data table holds -- 0x0001B370, the race loader's "
                         "callback, was one (part 180)")
    args = ap.parse_args()
    data = io.open(XBE, "rb").read()
    secs = sections(data)
    # x86 code: .text plus the XDK library sections. The section flags do not
    # tell code from data (.rdata carries the executable bit), and each
    # library section has its own file offset -- VA - 0x10000 only holds for
    # .text.
    code = [s for s in secs if s[0] in CODE_SECTIONS]

    def in_code(v):
        return any(c[2] <= v < c[2] + c[3] for c in code)

    def code_byte(va):
        for c in code:
            if c[2] <= va < c[2] + c[5]:
                return data[c[4] + va - c[2]]
        return 0

    def code_dword(va):
        return sum(code_byte(va + k) << (8 * k) for k in range(4))

    reg = set()
    for l in io.open(os.path.join(GEN, "recomp_dispatch.c"), encoding="utf-8", errors="replace"):
        m = re.search(r"\{ 0x([0-9A-F]{8})u", l)
        if m:
            reg.add(int(m.group(1), 16))
    known = set(int(a, 16) for a in re.findall(
        r"^\w[^\n(]*\bFUN_([0-9a-f]{8})\(", io.open(EXPORT, encoding="utf-8", errors="replace").read(), re.M))

    def plausible(v):
        if v in reg or v in known:
            return True
        prev = code_byte(v - 1)
        # ret / ret n tail / int3 / nop padding / jmp rel8/rel32 tail
        if prev in (0xC3, 0xCC, 0x90) or code_byte(v - 3) == 0xC2 or code_byte(v - 5) == 0xE9 \
                or code_byte(v - 2) == 0xEB:
            return True
        # A switch's jump table can sit directly in front of the next
        # function, so the byte before is table data: 0x00098C30 (the venue
        # screen's select handler, part 180) was rejected that way.
        if v % 4 == 0 and in_code(code_dword(v - 4)):
            return True
        # Or the target opens like a function: push reg / push ebp; mov
        # ebp,esp / sub esp,n / mov reg,[esp+n].
        b0, b1, b2 = code_byte(v), code_byte(v + 1), code_byte(v + 2)
        return (b0 in (0x53, 0x55, 0x56, 0x57) or (b0 == 0x83 and b1 == 0xEC)
                or (b0 == 0x81 and b1 == 0xEC)
                or (b0 == 0x8B and b1 in (0x44, 0x4C, 0x54) and b2 == 0x24))

    if args.code_imm:
        imm = re.compile(r"^\s+0x([0-9A-F]{8})\s+[0-9a-f]+\s+(mov|push)\s+(.*?),?\s*0x([0-9a-f]+)\s*(;.*)?$")
        found = {}
        for line in io.open(args.code_imm, encoding="utf-8", errors="replace"):
            m = imm.match(line)
            if not m:
                continue
            v = int(m.group(4), 16)
            if in_code(v) and v not in reg and plausible(v) and v != 0x0015CF26:
                found.setdefault(v, int(m.group(1), 16))
        if args.addrs:
            print(" ".join("0x%08X" % a for a in sorted(found)))
        else:
            for v, site in sorted(found.items()):
                print("0x%08X  referenced at 0x%08X" % (v, site))
            print("%d unregistered code addresses used as immediates" % len(found))
        return

    near = set(int(x, 16) for x in args.near.split(",") if x)
    tables = []
    for nm, flags, va, vsz, raw, rsz in secs:
        # DOLBY is DSP microcode, ABORTFONT a bitmap font, $$XTIMAGE an
        # image: values there that happen to land in .text are not pointers.
        if (nm in CODE_SECTIONS or nm in ("DOLBY", "ABORTFONT", "$$XTIMAGE")
                or not nm or rsz < 8):
            continue
        # Only the file-backed part; a partial trailing dword is ignored.
        run = []
        for off in range(0, rsz - 3, 4):
            v = struct.unpack_from("<I", data, raw + off)[0]
            if in_code(v):
                run.append((va + off, v))
                continue
            if len(run) >= args.min_run:
                tables.append((nm, run))
            run = []
        if len(run) >= args.min_run:
            tables.append((nm, run))
    missing, shown = set(), 0
    for nm, run in tables:
        tg = [v for _, v in run]
        if near and not (near & set(tg)):
            continue
        miss = sorted(set(v for v in tg if v not in reg and plausible(v)))
        if not miss:
            continue
        missing |= set(miss)
        if not args.addrs:
            shown += 1
            print("%-8s table 0x%08X: %2d slots, %2d unregistered: %s" % (
                nm, run[0][0], len(run), len(miss), " ".join("%X" % v for v in miss[:10])
                + (" ..." if len(miss) > 10 else "")))
    if args.addrs:
        print(" ".join("0x%08X" % a for a in sorted(missing)))
    else:
        print("%d tables with unregistered targets, %d distinct targets" % (shown, len(missing)))


if __name__ == "__main__":
    main()
