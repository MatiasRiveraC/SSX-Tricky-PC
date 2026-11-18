#!/usr/bin/env python3
"""Restore the pops that x87 compares and fistp lost in the generated tree.

`fcomp`, `fucomp`, `ficomp` pop once; `fcompp`, `fucompp` pop twice; `fistp`
pops. Earlier lifter output -- and the post-passes that repaired its compare
operands -- left many of these without the pop:

    { double _fc = (double)MEMF(0x1a9f30); _fpu_cmp = ...; g_fpu_cmp = _fpu_cmp; } /* fcomp dword ptr [0x1a9f30] */
    _fpu_cmp = (fp_top() < fp_st1()) ? -1 : ...; /* fcompp  */
    MEM32(esp + 0xC) = (int32_t)fp_top(); /* fistp */

While every function had a private x87 stack a missing pop only skewed st(N)
inside that one function. Since part 179 the stack is shared per thread, so
each one leaves a value behind for the next function. Register compares
against st(i), i > 1, also compared with st(1); they now name st(i).

    fixfppops.py [--dry-run]
"""
import argparse, glob, io, os, re
import ssxpaths

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = ssxpaths.GEN

MEMCMP = re.compile(r"^(?P<ind>\s*)\{ double _fc = (?P<rhs>[^;]+); (?P<body>_fpu_cmp = [^}]*?) \} "
                    r"/\* (?P<mn>f(?:i|u)?comp) (?P<ops>[^*]*)\*/")
REGCMP = re.compile(r"^(?P<ind>\s*)_fpu_cmp = \(fp_top\(\) < fp_st1\(\)\) \? -1 : \(fp_top\(\) > fp_st1\(\)\) \? 1 : 0;"
                    r"(?P<pops>(?: fp_popp?\(\);)*) /\* (?P<mn>f(?:u)?com(?:pp?)?) ?(?P<ops>[^*]*)\*/")
FISTP = re.compile(r"^(?P<ind>\s*)(?P<store>MEM(?:16|32|64)\([^;]+\) = \([u]?int(?:16|32|64)_t\)[^;]*fp_top\(\)\)?;) /\* fistp \*/\s*$")


def st(i):
    return {0: "fp_top()", 1: "fp_st1()"}.get(i, "_fp_stack[(_fp_top + %d) & 7]" % i)


def fix_line(line):
    m = MEMCMP.match(line)
    if m and "fp_pop" not in m.group("body"):
        new = "%s{ double _fc = %s; %s fp_pop(); } /* %s %s*/" % (
            m.group("ind"), m.group("rhs"), m.group("body").rstrip(), m.group("mn"), m.group("ops"))
        return new, "memory %s gets its pop" % m.group("mn")
    m = REGCMP.match(line)
    if m:
        mn, ops = m.group("mn"), m.group("ops").strip()
        want = 2 if mn.endswith("pp") else (1 if mn.endswith("p") else 0)
        have = m.group("pops").count("fp_pop")
        # The naive form compared st(1) whatever the operand was; a memory
        # operand survives only in the comment ("dword ptr [0x1a9f34]").
        mem = re.match(r"^(dword|qword) ptr \[([^\]]+)\]$", ops)
        if mem:
            acc = "(double)MEMF" if mem.group(1) == "dword" else "MEMD"
            rhs, what = "%s(%s)" % (acc, mem.group(2)), "memory %s (was compared with st(1))" % mn
        else:
            r = re.match(r"^st\((\d)\)$", ops)
            i = int(r.group(1)) if r else 1
            if want == have and i == 1:
                return None, None
            rhs, what = st(i), "register %s (st(%d), %d pops)" % (mn, i, want)
        pops = " fp_pop();" * want
        new = ("%s{ double _fc = %s; _fpu_cmp = (fp_top() < _fc) ? -1 : (fp_top() > _fc) ? 1 : 0;%s } "
               "/* %s %s */" % (m.group("ind"), rhs, pops, mn, ops)).replace("  */", " */")
        return new, what
    m = FISTP.match(line)
    if m:
        return "%s%s fp_pop(); /* fistp */" % (m.group("ind"), m.group("store")), "fistp gets its pop"
    return None, None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=GEN)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    counts = {}
    for path in sorted(glob.glob(os.path.join(args.gen, "recomp_*.c"))):
        text = io.open(path, encoding="utf-8", errors="surrogateescape", newline="").read()
        eol = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(eol)
        changed = False
        for k, l in enumerate(lines):
            new, why = fix_line(l)
            if new is None:
                continue
            counts[why] = counts.get(why, 0) + 1
            if args.dry_run and counts[why] <= 2:
                print("%s:%d\n  - %s\n  + %s" % (os.path.basename(path), k + 1, l.strip(), new.strip()))
            lines[k] = new
            changed = True
        if changed and not args.dry_run:
            io.open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write(eol.join(lines))
    for why, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print("%5d  %s" % (n, why))


if __name__ == "__main__":
    main()
