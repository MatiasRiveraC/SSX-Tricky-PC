#!/usr/bin/env python3
"""Publish ebp (g_seh_ebp = ebp) before every call in the generated tree.

A callee seeds its ebp local from g_seh_ebp ("inherit caller's frame"). Tail
jumps published the caller's ebp there; calls never did, so a callee that
works in its caller's frame -- the CRT's hand-written math helpers do --
read whatever an unrelated function last left in g_seh_ebp. Part 180: the
atan2 classifier at 0x0015EED3 loaded a garbage control word and misfiled
its fxam results; atan2/acos returned NaN and the race camera went NaN.
translator.py now emits the store; this applies it to existing code.

Only functions that keep ebp as a C local (`uint32_t ebp;`) are changed:
in the others `ebp` is not in scope.

    fixebppublish.py [--dry-run]
"""
import argparse, glob, io, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.normpath(os.path.join(HERE, "..", "..", "..", "ssx_recomp", "src", "recomp", "gen"))
FN = re.compile(r"^(?:static\s+)?void (\w+)\(void\)\s*$")
CALL = re.compile(r"^(\s*(?:\{ uint32_t _icall_esp = g_esp;\s*)?)(PUSH32\(esp, 0\); (?:\w+\(\)|RECOMP_ICALL))")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=GEN)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    total = 0
    for path in sorted(glob.glob(os.path.join(args.gen, "recomp_*.c"))):
        text = io.open(path, encoding="utf-8", errors="surrogateescape", newline="").read()
        eol = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(eol)
        has_ebp, n = False, 0
        for i, ln in enumerate(lines):
            if FN.match(ln):
                has_ebp = False
            elif ln.strip() == "uint32_t ebp;":
                has_ebp = True
            elif has_ebp and "g_seh_ebp = ebp" not in ln:
                m = CALL.match(ln)
                if m:
                    lines[i] = m.group(1) + "g_seh_ebp = ebp; " + ln[len(m.group(1)):]
                    n += 1
        total += n
        if n and not args.dry_run:
            io.open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write(eol.join(lines))
        if n:
            print("%6d  %s" % (n, os.path.basename(path)))
    print("%d call sites now publish ebp" % total)


if __name__ == "__main__":
    main()
