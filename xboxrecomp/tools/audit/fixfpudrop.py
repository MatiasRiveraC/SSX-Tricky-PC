#!/usr/bin/env python3
"""Implement x87 instructions the lifter emitted as bare comments.

The recompiler annotates every instruction it translates.  For one family of
x87 opcodes it emitted *only* the annotation:

    /* FPU: fdivr dword ptr [esi + 0x86c] */

That line is a no-op.  The instruction's effect is silently gone, and because
the comment looks like documentation rather than a hole, the site reads as
translated.  402 of them are in the generated tree.

The cost is not theoretical.  `Matrix_BuildOrthographicProjection` builds the
2D projection the title uses for screen-space text, and three of its
instructions -- `fdivr st,st(1)` and two `fdivr st,st(2)` -- are exactly the
divides that produce the matrix's `2/(r-l)` scale terms.  With them dropped the
matrix is garbage, so nothing drawn through it lands anywhere on screen.

This tool rewrites each of those comment-only lines into the operation it
stands for, in the same style the generator uses when it *does* emit these
opcodes, and leaves the original annotation in place as a trailing comment.

    fixfpudrop.py --list              what is dropped, by mnemonic
    fixfpudrop.py --dry-run           show the rewrite for each site
    fixfpudrop.py                     apply
    fixfpudrop.py --only fdivr,fsubr  restrict to some mnemonics

Semantics are Intel's, and the reversed and popping forms were checked against
the game's own bytes rather than recalled:

    d8 e9   fsubr  st,st(1)        ST0 = ST1 - ST0
    d8 f9   fdivr  st,st(1)        ST0 = ST1 / ST0
    de e1   fsubrp st(1),st        ST1 = ST0 - ST1, pop
    de f1   fdivrp st(1),st        ST1 = ST0 / ST1, pop

`FSUBR dst,src` is `dst = src - dst` and `FDIVR dst,src` is `dst = src / dst`,
which is what makes the one-operand `st(i)` spelling read backwards at a
glance -- it is `st, st(i)`, so the *stack top* is the destination.

`fxam` is deliberately not implemented: it reports a classification of ST(0)
across C3/C2/C0, and this runtime models the x87 status word only as a
three-way comparison result, so there is nothing correct to write.  Those sites
are reported, not guessed at.
"""

import argparse
import ssxpaths
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_GEN = os.path.normpath(
    ssxpaths.GEN)

# A whole line that is nothing but the annotation.
LINE = re.compile(r"^(?P<ind>\s*)/\* FPU: (?P<mn>[a-z0-9]+)\s*(?P<ops>[^*]*?)\s*\*/\s*$")

ST0 = "fp_top()"
ST1 = "fp_st1()"


def st(i):
    """ST(i) in the generated code's stack model."""
    if i == 0:
        return ST0
    if i == 1:
        return ST1
    return "_fp_stack[(_fp_top + %d) & 7]" % i


def mem(ops):
    """`dword ptr [esi + 0x14]` -> a C expression reading that operand."""
    m = re.match(r"^(dword|qword|word) ptr \[(.+)\]$", ops)
    if not m:
        return None, None
    size, addr = m.group(1), m.group(2).strip()
    return size, addr


def float_read(ops):
    size, addr = mem(ops)
    if size == "dword":
        return "(double)MEMF(%s)" % addr
    if size == "qword":
        return "MEMD(%s)" % addr
    return None


def int_read(ops):
    size, addr = mem(ops)
    if size == "dword":
        return "(double)SMEM32(%s)" % addr
    if size == "word":
        return "(double)SMEM16(%s)" % addr
    return None


def reg_operand(ops):
    """`st(1)` or `st(1), st(0)` -> (index, is_two_operand) or None."""
    m = re.match(r"^st\((\d)\)$", ops)
    if m:
        return int(m.group(1)), False
    m = re.match(r"^st\((\d)\),\s*st\(0\)$", ops)
    if m:
        return int(m.group(1)), True
    return None, None


# Stack-only opcodes: mnemonic -> C statement(s).
NULLARY = {
    "fcos":    "%s = cos(%s);" % (ST0, ST0),
    "fsin":    "%s = sin(%s);" % (ST0, ST0),
    "fsqrt":   "%s = sqrt(%s);" % (ST0, ST0),
    "fabs":    "%s = fabs(%s);" % (ST0, ST0),
    "fchs":    "%s = -%s;" % (ST0, ST0),
    "frndint": "%s = nearbyint(%s);" % (ST0, ST0),
    "f2xm1":   "%s = pow(2.0, %s) - 1.0;" % (ST0, ST0),
    # ST1 <- ST1 * log2(ST0), then pop, so the result lands in ST0.
    "fyl2x":   "%s = %s * (log(%s) / log(2.0)); fp_pop();" % (ST1, ST1, ST0),
    "fyl2xp1": "%s = %s * (log(%s + 1.0) / log(2.0)); fp_pop();" % (ST1, ST1, ST0),
    "fpatan":  "%s = atan2(%s, %s); fp_pop();" % (ST1, ST1, ST0),
    "fscale":  "%s = %s * pow(2.0, trunc(%s));" % (ST0, ST0, ST1),
    "fprem":   "%s = fmod(%s, %s);" % (ST0, ST0, ST1),
    "fprem1":  "%s = remainder(%s, %s);" % (ST0, ST0, ST1),
    "fldpi":   "fp_push(3.14159265358979323846);",
    "fldl2e":  "fp_push(1.44269504088896340736);",
    "fldl2t":  "fp_push(3.32192809488736234787);",
    "fldlg2":  "fp_push(0.30102999566398119521);",
    "fldln2":  "fp_push(0.69314718055994530942);",
    "fld1":    "fp_push(1.0);",
    "fldz":    "fp_push(0.0);",
    # tan into ST0, then the mandated push of 1.0.
    "fptan":   "%s = tan(%s); fp_push(1.0);" % (ST0, ST0),
    # sin into ST0, then push cos: ST0=cos, ST1=sin.
    "fsincos": "{ double _s = sin(%s), _c = cos(%s); %s = _s; fp_push(_c); }"
               % (ST0, ST0, ST0),
    # Only the global is written: `_fpu_cmp` is a per-function local that the
    # generator emits only for functions that already contain a compare, so a
    # site whose function has none would not compile.  FNSTSW_AX()/FPU_AH()
    # read the global, which is what the following branch consumes.
    "ftst":    "g_fpu_cmp = (%s < 0.0) ? -1 : (%s > 0.0) ? 1 : 0;" % (ST0, ST0),
    # No exception state is modelled, so clearing it really is a no-op --
    # recorded explicitly so it is not mistaken for another hole.
    "fnclex":  "(void)0;",
}

# Memory-operand opcodes: mnemonic -> (reader, expression template).
MEM_FLOAT = {
    "fadd":  "%s = %s + %s;",
    "fmul":  "%s = %s * %s;",
    "fsub":  "%s = %s - %s;",
    "fdiv":  "%s = %s / %s;",
    "fsubr": None,   # reversed, handled below
    "fdivr": None,
}
MEM_INT = {
    "fiadd":  "%s = %s + %s;",
    "fimul":  "%s = %s * %s;",
    "fisub":  "%s = %s - %s;",
    "fidiv":  "%s = %s / %s;",
    "fisubr": None,
    "fidivr": None,
}

SKIP = {
    "fxam": "classifies ST(0) across C3/C2/C0; the runtime models the status "
            "word only as a three-way compare, so there is no correct rewrite",
}


def rewrite(mn, ops):
    """Return the C statement for this instruction, or (None, reason)."""
    if mn in SKIP:
        return None, SKIP[mn]

    if not ops:
        if mn in NULLARY:
            return NULLARY[mn], None
        return None, "no stack-only rule for '%s'" % mn

    # ── register forms ────────────────────────────────────────────────
    if ops.startswith("st("):
        i, two = reg_operand(ops)
        if i is None:
            return None, "unparsed register operand '%s'" % ops
        # One operand means `st, st(i)`: the stack top is the destination.
        if not two:
            if mn == "fsubr":
                return "%s = %s - %s;" % (ST0, st(i), ST0), None
            if mn == "fdivr":
                return "%s = %s / %s;" % (ST0, st(i), ST0), None
            if mn == "fsub":
                return "%s = %s - %s;" % (ST0, ST0, st(i)), None
            if mn == "fdiv":
                return "%s = %s / %s;" % (ST0, ST0, st(i)), None
            if mn == "fadd":
                return "%s = %s + %s;" % (ST0, ST0, st(i)), None
            if mn == "fmul":
                return "%s = %s * %s;" % (ST0, ST0, st(i)), None
            # The popping forms are always `st(i), st` even when spelled with
            # one operand.
            if mn == "fsubrp":
                return "%s = %s - %s; fp_pop();" % (st(i), ST0, st(i)), None
            if mn == "fdivrp":
                return "%s = %s / %s; fp_pop();" % (st(i), ST0, st(i)), None
            if mn == "fsubp":
                return "%s = %s - %s; fp_pop();" % (st(i), st(i), ST0), None
            if mn == "fdivp":
                return "%s = %s / %s; fp_pop();" % (st(i), st(i), ST0), None
            if mn == "faddp":
                return "%s = %s + %s; fp_pop();" % (st(i), st(i), ST0), None
            if mn == "fmulp":
                return "%s = %s * %s; fp_pop();" % (st(i), st(i), ST0), None
            return None, "no register-form rule for '%s'" % mn
        # Explicit `st(i), st(0)`: destination is ST(i).
        if mn == "fsubr":
            return "%s = %s - %s;" % (st(i), ST0, st(i)), None
        if mn == "fdivr":
            return "%s = %s / %s;" % (st(i), ST0, st(i)), None
        if mn == "fsub":
            return "%s = %s - %s;" % (st(i), st(i), ST0), None
        if mn == "fdiv":
            return "%s = %s / %s;" % (st(i), st(i), ST0), None
        if mn == "fadd":
            return "%s = %s + %s;" % (st(i), st(i), ST0), None
        if mn == "fmul":
            return "%s = %s * %s;" % (st(i), st(i), ST0), None
        return None, "no two-operand rule for '%s'" % mn

    # ── memory forms ──────────────────────────────────────────────────
    if mn in MEM_FLOAT:
        r = float_read(ops)
        if r is None:
            return None, "unparsed float operand '%s'" % ops
        if mn == "fsubr":
            return "%s = %s - %s;" % (ST0, r, ST0), None
        if mn == "fdivr":
            return "%s = %s / %s;" % (ST0, r, ST0), None
        return MEM_FLOAT[mn] % (ST0, ST0, r), None

    if mn in MEM_INT:
        r = int_read(ops)
        if r is None:
            return None, "unparsed integer operand '%s'" % ops
        if mn == "fisubr":
            return "%s = %s - %s;" % (ST0, r, ST0), None
        if mn == "fidivr":
            return "%s = %s / %s;" % (ST0, r, ST0), None
        return MEM_INT[mn] % (ST0, ST0, r), None

    if mn in ("ficom", "ficomp"):
        r = int_read(ops)
        if r is None:
            return None, "unparsed integer operand '%s'" % ops
        s = ("{ double _fc = %s; g_fpu_cmp = "
             "(%s < _fc) ? -1 : (%s > _fc) ? 1 : 0;%s }"
             % (r, ST0, ST0, " fp_pop();" if mn == "ficomp" else ""))
        return s, None

    if mn == "fisttp":
        size, addr = mem(ops)
        if size == "dword":
            return "MEM32(%s) = (uint32_t)(int32_t)trunc(%s); fp_pop();" % (addr, ST0), None
        if size == "qword":
            return "MEMD(%s) = trunc(%s); fp_pop();" % (addr, ST0), None
        return None, "unparsed fisttp operand '%s'" % ops

    return None, "no rule for '%s'" % mn


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=DEFAULT_GEN)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--list", action="store_true",
                    help="report what is dropped without proposing rewrites")
    ap.add_argument("--only", default="",
                    help="comma-separated mnemonics to restrict the pass to")
    args = ap.parse_args()

    only = set(x.strip() for x in args.only.split(",") if x.strip())

    files = sorted(f for f in os.listdir(args.gen) if f.endswith(".c"))
    if not files:
        sys.exit("no generated sources under %s" % args.gen)

    counts, applied, skipped = {}, 0, {}

    for name in files:
        path = os.path.join(args.gen, name)
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as fh:
            lines = fh.read().split("\n")

        dirty = False
        for idx, line in enumerate(lines):
            m = LINE.match(line)
            if not m:
                continue
            mn, ops = m.group("mn"), m.group("ops")
            counts[mn] = counts.get(mn, 0) + 1
            if args.list:
                continue
            if only and mn not in only:
                continue

            code, why = rewrite(mn, ops)
            if code is None:
                skipped.setdefault(why, []).append("%s:%d" % (name, idx + 1))
                continue

            new = "%s%s /* FPU: %s%s */" % (
                m.group("ind"), code, mn, (" " + ops) if ops else "")
            if args.dry_run:
                print("%s:%d" % (name, idx + 1))
                print("  -%s" % line.strip())
                print("  +%s" % new.strip())
            else:
                lines[idx] = new
                dirty = True
            applied += 1

        if dirty and not args.dry_run:
            with open(path, "w", encoding="utf-8",
                      errors="surrogateescape", newline="") as fh:
                fh.write("\n".join(lines))

    total = sum(counts.values())
    print("\ndropped x87 instructions found: %d" % total)
    for mn in sorted(counts, key=lambda k: -counts[k]):
        print("    %-10s %4d" % (mn, counts[mn]))

    if not args.list:
        verb = "would rewrite" if args.dry_run else "rewrote"
        print("\n%s %d site(s)" % (verb, applied))
        if skipped:
            print("left alone:")
            for why, where in sorted(skipped.items()):
                print("    %d x %s" % (len(where), why))
                print("        %s" % ", ".join(where[:6]))


if __name__ == "__main__":
    main()
