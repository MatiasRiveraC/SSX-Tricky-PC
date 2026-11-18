#!/usr/bin/env python3
"""Find generated bodies whose x87 instruction sequence no longer matches a
fresh lift of the same function.

Post-pass scripts rewrite generated C in place.  When one of them mispairs a
site it can duplicate or drop an x87 statement, or turn `fxch st(2)` into
`fxch`, and nothing downstream notices: the body still compiles and still
carries plausible instruction comments.  `sub_000F7830` (the far-terrain patch
splitter) was damaged like that and drew the course's distant ground as a
shattered pattern until it was relifted (part 183).

Every x87 statement the lifter emits carries the instruction as a comment
(`/* fxch st(2) */`, `/* fsub dword ptr [eax + 0xc] */`, `/* fstp */`).  This
tool lists those comments per function, in order, for the working tree and for
a fresh full translation, and reports every function where the two lists
differ.  It compares the mnemonic and, when both sides spell it, a register
operand `st(N)`; memory operands are ignored because fixfpmem.py legitimately
adds them to comments the lifter left bare.

    x87seq.py FRESH_GEN_DIR [--gen GEN_DIR] [--show N] [--function NAME]

A fresh translation comes from the two commands in
reference_apply_lifter_diff (disasm with every dispatch address as a seed, then
`tools.recomp --all --split 1000 --gen-dir DIR`).  Functions are matched by the
start address in their `Original:` header, so renames do not matter.

A reported function is a candidate, not a verdict: relift it and read the diff
(relift.py --dry-run) before replacing anything, because a difference can also
be a hand fix the lifter still lacks.
"""

import argparse
import glob
import io
import os
import re
import sys

import ssxpaths

FUNC = re.compile(r"^void (?P<name>[A-Za-z_]\w*)\(void\)$")
ORIGIN = re.compile(r"^\s*\*\s*Original:\s*0x(?P<start>[0-9A-Fa-f]{8})")
X87 = {
    "fld", "fst", "fstp", "fild", "fist", "fistp", "fisttp", "fbld", "fbstp",
    "fxch", "fadd", "faddp", "fiadd", "fsub", "fsubp", "fisub", "fsubr",
    "fsubrp", "fisubr", "fmul", "fmulp", "fimul", "fdiv", "fdivp", "fidiv",
    "fdivr", "fdivrp", "fidivr", "fcom", "fcomp", "fcompp", "fucom", "fucomp",
    "fucompp", "ficom", "ficomp", "fcomi", "fcomip", "fucomi", "fucomip",
    "ftst", "fxam", "fchs", "fabs", "fsqrt", "fsin", "fcos", "fsincos",
    "fptan", "fpatan", "fscale", "frndint", "fprem", "fprem1", "f2xm1",
    "fyl2x", "fyl2xp1", "fxtract", "fld1", "fldz", "fldpi", "fldl2e",
    "fldl2t", "fldlg2", "fldln2", "fnstsw", "fstsw", "fnstcw", "fstcw",
    "fldcw", "ffree", "fincstp", "fdecstp", "fcmovb", "fcmove", "fcmovbe",
    "fcmovu", "fcmovnb", "fcmovne", "fcmovnbe", "fcmovnu",
}
COMMENT = re.compile(r"/\*\s*(?:FPU:\s*)?(?P<mn>f[a-z0-9]+)(?P<rest>(?:[^*]|\*(?!/))*)\*/")
REG = re.compile(r"st\((\d)\)")


ARITH = {"fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr"}
OPERAND = re.compile(r"^st(?:\((\d)\))?$")


def norm(mn, rest):
    """One comparable token: mnemonic plus register operands if spelled.

    Arithmetic is written out in the two-operand form, because the one-operand
    spellings mean different things: `fadd st(2)` is `st, st(2)` while
    `faddp st(2)` is `st(2), st`."""
    rest = rest.strip()
    if rest.startswith(":"):          # "fst: stores st(0), does NOT pop"
        rest = ""
    if "ptr" in rest or "[" in rest:
        return mn
    regs = []
    for tok in [t.strip() for t in rest.split(",")] if rest else []:
        m = OPERAND.match(tok)
        if not m:
            return mn                 # prose or something unparsed: mnemonic only
        regs.append(m.group(1) or "0")
    if mn == "fxch" and not regs:
        regs = ["1"]                  # bare fxch is fxch st(1)
    if not regs and mn[:-1] in ARITH and mn.endswith("p"):
        regs = ["1", "0"]             # bare faddp is faddp st(1), st
    if len(regs) == 1 and mn in ARITH:
        regs = ["0", regs[0]]
    elif len(regs) == 1 and mn[:-1] in ARITH and mn.endswith("p"):
        regs = [regs[0], "0"]
    if regs:
        return "%s %s" % (mn, ",".join(regs))
    return mn


def bodies(path):
    """{start address: (name, [token, ...])} for one generated file."""
    out = {}
    lines = io.open(path, encoding="utf-8", errors="replace").read().split("\n")
    start, name, seq, on = None, None, None, False
    for line in lines:
        line = line.rstrip("\r")
        if not on:
            m = ORIGIN.match(line)
            if m:
                start = int(m.group("start"), 16)
                continue
            m = FUNC.match(line)
            if m:
                on, name, seq = True, m.group("name"), []
            continue
        if line == "}":
            if start is not None:
                out.setdefault(start, (name, seq))
            on, start = False, None
            continue
        if "#define" in line:
            continue
        for m in COMMENT.finditer(line):
            mn = m.group("mn")
            if mn in X87:
                seq.append(norm(mn, m.group("rest")))
    return out


def load(d):
    out = {}
    for p in sorted(glob.glob(os.path.join(d, "*.c"))):
        for k, v in bodies(p).items():
            out.setdefault(k, v + (os.path.basename(p),))
    return out


def same(x, y):
    """Tokens match when mnemonics agree and, if both spell registers, the
    registers agree.  Older lifter comments often omit the operand of
    fld/fst/fstp/fcom (`/* fld */` for `fld st(1)`), so a bare token matches
    any register form of the same mnemonic."""
    if x == y:
        return True
    xm, _, xr = x.partition(" ")
    ym, _, yr = y.partition(" ")
    return xm == ym and (not xr or not yr)


def first_diff(a, b):
    for i, (x, y) in enumerate(zip(a, b)):
        if not same(x, y):
            return i
    return min(len(a), len(b))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fresh")
    ap.add_argument("--gen", default=ssxpaths.GEN)
    ap.add_argument("--show", type=int, default=6, help="context tokens to print")
    ap.add_argument("--function", default="")
    a = ap.parse_args()

    gen, fresh = load(a.gen), load(a.fresh)
    both = sorted(set(gen) & set(fresh))
    bad = []
    for va in both:
        gname, gseq, gfile = gen[va]
        _fname, fseq, _ff = fresh[va]
        if a.function and a.function not in (gname, "0x%08X" % va):
            continue
        if len(gseq) != len(fseq) or first_diff(gseq, fseq) < len(gseq):
            bad.append((va, gname, gfile, gseq, fseq))
    x87 = sum(1 for va in both if gen[va][1] or fresh[va][1])
    print("functions: %d in tree, %d in fresh lift, %d matched, %d use x87"
          % (len(gen), len(fresh), len(both), x87))
    print("x87 sequence differs: %d" % len(bad))
    for va, name, f, g, n in bad:
        i = first_diff(g, n)
        lo = max(0, i - 2)
        # Same length: a content difference, the damage class this tool is
        # for.  A different length is usually a boundary (a split or a
        # recovered fragment) or a hand-written body.
        kind = ("content, %d op(s) differ" % sum(1 for x, y in zip(g, n) if not same(x, y))
                if len(g) == len(n) else "length")
        print("\n0x%08X %s (%s)  tree %d ops, fresh %d ops, first difference at op %d [%s]"
              % (va, name, f, len(g), len(n), i, kind))
        print("  tree : " + " | ".join(g[lo:i + a.show]))
        print("  fresh: " + " | ".join(n[lo:i + a.show]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
