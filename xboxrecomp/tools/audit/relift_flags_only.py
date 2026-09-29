#!/usr/bin/env python3
"""Relift functions but keep ONLY the flag-snapshot / carry changes.

WHY THIS EXISTS
---------------
relift.py regenerates whole bodies from the current lifter. That is only safe
for bodies that never received fixes applied to the generated C directly --
and many did: the x87 memory-operand pass, the ftol argument, the cmpsb carry,
ICALL save points after cdecl argument pushes, and the rename map (a relift
emits `sub_00150950()` where the tree calls `Heap_Free()`, which will not
link). Relifting the MPEG decoder's 268 functions in part 177 would have
silently undone all five.

The lifter's deferred-flag fix (flag-operand snapshots, `_cf` from the setter)
is still wanted everywhere. So this relifts, then compares each function old
vs new and keeps the new body ONLY if every difference is one of:

  * a snapshot line        `_fsa = ...;` / `_fsb = ...;` / the declaration
  * a carry line           `_cf = ...; /* CF from ... */`
  * a deferred-flag marker `(void)0; /* cmp ... - flags set for next jcc */`
  * the old one-off script's `_fcN_reg` snapshot, its declaration and comment
  * a branch / setcc line whose condition now reads `_fsa`/`_fsb`, with the
    same destination (goto target, tail call or setcc register) as before

Anything else -> the old body is restored and the function is listed, with the
reason, for a person to look at.

    relift_flags_only.py 0x00146000-0x0014C000 --exclude NAME ...
    relift_flags_only.py 0x0017E9EF 0x0017992D
"""
import argparse, difflib, io, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
GEN = os.path.join(ROOT, "ssx_recomp", "src", "recomp", "gen")
FN = re.compile(r'^(?:static\s+)?\w[\w\s\*]*?\b(\w+)\s*\(void\)\s*$')

SNAP_LINE = re.compile(
    r'^\s*(_fs[ab] = \(uint32_t\)\(.*\); /\* flag operand snapshot \*/'
    r'|uint32_t _fsa = 0, _fsb = 0; /\* flag-operand snapshots \*/'
    r'|_cf = .*; /\* CF from \w+ \*/'
    r'|\(void\)0; /\* (cmp|test) .* - flags set for next jcc \*/)\s*$')
OLD_FC_LINE = re.compile(
    r'^\s*(_fc\d+_\w+ = \w+; /\* .*|uint32_t _fc\d+_\w+;.*'
    r'|\* comparison is taken against the value live here, as on hardware\. \*/)\s*$')
DEST = re.compile(r'(goto loc_[0-9A-F]+|\bsub_\w+\(\); return;|\w+\(\); return;'
                  r'|SET_\w+\((\w+),|/\* \w+:? [^*]*\*/)')


def dests(line):
    return [m.group(0) for m in DEST.finditer(line)]


def is_cond_line(line):
    s = line.strip()
    return s.startswith("if (") or s.startswith("SET_") or "? 1 : 0" in s


def split_funcs(text):
    """name -> (start, end) line indices, end inclusive of the closing brace."""
    lines = text.split("\n")
    out, i = {}, 0
    while i < len(lines):
        m = FN.match(lines[i].rstrip())
        if m and i + 1 < len(lines) and lines[i + 1].strip() == "{":
            j = i + 1
            while j < len(lines) and lines[j] != "}":
                j += 1
            out[m.group(1)] = (i, j)
            i = j
        i += 1
    return lines, out


def judge(old, new):
    """None if acceptable, else a reason string."""
    sm = difflib.SequenceMatcher(a=old, b=new, autojunk=False)
    for tag, a1, a2, b1, b2 in sm.get_opcodes():
        if tag == "equal":
            continue
        olds, news = old[a1:a2], new[b1:b2]
        # Drop pure snapshot/marker additions and old-script removals.
        news_core = [l for l in news if l.strip() and not SNAP_LINE.match(l)]
        olds_core = [l for l in olds if l.strip() and not OLD_FC_LINE.match(l)]
        if not news_core and not olds_core:
            continue
        if len(news_core) != len(olds_core):
            return f"{tag} {a1+1}: {len(olds_core)} old vs {len(news_core)} new lines"
        for o, n in zip(olds_core, news_core):
            if o == n:
                continue
            if not (is_cond_line(o) and is_cond_line(n)):
                return f"non-flag change: {o.strip()[:70]!r} -> {n.strip()[:70]!r}"
            if "_fs" not in n and "_fc" not in o:
                return f"condition changed without a snapshot: {n.strip()[:80]!r}"
            if dests(o) != dests(n):
                return f"destination changed: {o.strip()[:60]!r} -> {n.strip()[:60]!r}"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addrs", nargs="+", help="VAs, or LO-HI ranges")
    ap.add_argument("--exclude", nargs="*", default=[], help="function names to skip")
    args = ap.parse_args()

    # Resolve ranges through the dispatch table.
    table = {}
    for l in io.open(os.path.join(GEN, "recomp_dispatch.c"), encoding="utf-8", errors="replace"):
        m = re.search(r'\{ 0x([0-9A-F]{8})u, \(recomp_func_t\)(\w+) \}', l)
        if m:
            table[int(m.group(1), 16)] = m.group(2)
    addrs = []
    for a in args.addrs:
        if "-" in a:
            lo, hi = (int(x, 16) for x in a.split("-"))
            addrs += [v for v in sorted(table) if lo <= v < hi and table[v] not in args.exclude]
        else:
            v = int(a, 16)
            if table.get(v) not in args.exclude:
                addrs.append(v)
    if not addrs:
        sys.exit("nothing to do")

    files = [f for f in os.listdir(GEN) if f.startswith("recomp_") and f.endswith(".c")]
    before = {f: io.open(os.path.join(GEN, f), encoding="utf-8", errors="replace").read() for f in files}

    r = subprocess.run([sys.executable, os.path.join(HERE, "relift.py")] + [f"0x{a:08X}" for a in addrs],
                       capture_output=True, text=True)
    print(r.stdout.strip().split("\n")[-1])

    kept, rejected, unchanged = [], [], 0
    for f in files:
        path = os.path.join(GEN, f)
        after = io.open(path, encoding="utf-8", errors="replace").read()
        if after == before[f]:
            continue
        old_lines, old_fns = split_funcs(before[f])
        new_lines, new_fns = split_funcs(after)
        out = list(new_lines)
        # Walk functions from the bottom so earlier indices stay valid.
        for name, (ns, ne) in sorted(new_fns.items(), key=lambda kv: -kv[1][0]):
            if name not in old_fns:
                continue
            os_, oe = old_fns[name]
            old_body, new_body = old_lines[os_:oe + 1], new_lines[ns:ne + 1]
            if old_body == new_body:
                unchanged += 1
                continue
            why = judge(old_body, new_body)
            if why is None:
                kept.append(name)
            else:
                rejected.append((name, why))
                out[ns:ne + 1] = old_body
        io.open(path, "w", encoding="utf-8", newline="").write("\n".join(out))

    print(f"kept {len(kept)} (flag-only changes), restored {len(rejected)}, "
          f"{unchanged} unchanged")
    for n in sorted(kept):
        print(f"  KEPT     {n}")
    for n, why in sorted(rejected):
        print(f"  RESTORED {n}: {why}")


if __name__ == "__main__":
    main()
