#!/usr/bin/env python3
"""Carry a lifter change into the generated tree, hunk by hunk.

WHY THIS EXISTS
---------------
A lifter fix is proven by translating the whole program twice -- once with the
old lifter, once with the new -- and diffing the two outputs. That diff is the
exact effect of the fix. But the tree the game builds from cannot simply be
regenerated: many bodies carry fixes applied to the generated C afterwards
(x87 memory operands, ICALL save points, split-function flags, renames), and a
relift silently undoes them. See RE_NOTES part 177 and relift_flags_only.py.

So this takes the old->new diff and transplants each hunk into the matching
function of the real tree. A hunk is applied only where its old lines, with a
line of context on each side, occur exactly once in that function; anything
else is reported and left alone. Nothing outside the hunks is touched.

    apply_lifter_diff.py OLD_GEN NEW_GEN [--gen GEN] [--dry-run]

OLD_GEN / NEW_GEN are the two full translations (tools.recomp --all --split
into separate --gen-dir's); GEN defaults to ssx_recomp/src/recomp/gen.
Functions are matched by address, so renamed functions are found.
"""
import argparse, difflib, io, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
GEN = os.path.join(ROOT, "ssx_recomp", "src", "recomp", "gen")
FN = re.compile(r'^(?:static\s+)?\w[\w\s\*]*?\b(\w+)\s*\(void\)\s*$')
SUB = re.compile(r'^sub_([0-9A-Fa-f]{8})$')


def split_funcs(lines):
    """name -> (start, end) line indices, end = the closing brace."""
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
    return out


def load_tree(d):
    files = {}
    for f in sorted(os.listdir(d)):
        if f.startswith("recomp_") and f.endswith(".c") and "dispatch" not in f:
            files[f] = io.open(os.path.join(d, f), encoding="utf-8",
                               errors="replace").read().split("\n")
    return files


def by_name(files):
    out = {}
    for f, lines in files.items():
        for name, (s, e) in split_funcs(lines).items():
            out[name] = (f, s, e)
    return out


def dispatch_names(gen):
    table = {}
    for l in io.open(os.path.join(gen, "recomp_dispatch.c"), encoding="utf-8",
                     errors="replace"):
        m = re.search(r'\{ 0x([0-9A-F]{8})u, \(recomp_func_t\)(\w+) \}', l)
        if m:
            table[int(m.group(1), 16)] = m.group(2)
    return table


# Lines the real tree gains after generation (instrument_labels.py and the
# label watchdog). They are invisible to matching, so a hunk's context still
# lines up; edits land between them, never on them.
INSTR = re.compile(r'^\s*(RECOMP_LOC\(0x[0-9A-Fa-f]+\);'
                   r'|\{ extern volatile unsigned g_last_loc; g_last_loc = 0x[0-9A-Fa-f]+; \})\s*$')


SNAPVAR = re.compile(r'\b_fs[ab]\b')
SNAPDEF = re.compile(r'^\s*_fs[ab] = ')
SNAPDECL = re.compile(r'uint32_t _fsa = 0, _fsb = 0;')
FINVAR = re.compile(r'\b_fin[0-9A-F]+_j[a-z]+\b')


def find_unique(body, seq):
    """Index into `body` of the unique occurrence of `seq`, skipping
    instrumentation lines in `body`; returns the body indices it matched."""
    idx = [i for i, l in enumerate(body) if not INSTR.match(l)]
    view = [body[i] for i in idx]
    hits = [k for k in range(len(view) - len(seq) + 1) if view[k:k + len(seq)] == seq]
    if len(hits) != 1:
        return None
    return [idx[hits[0] + j] for j in range(len(seq))]


def locate(R, O, a1, a2):
    """Where hunk O[a1:a2] sits in R, as (start, end) body indices of the old
    lines (start == end for an insertion). Tries the widest unique anchor
    first."""
    cands = []
    if a1 > 0 and a2 < len(O):
        cands.append((a1 - 1, a2 + 1))
    if a2 > a1:
        cands.append((a1, a2))
    if a1 > 0:
        cands.append((a1 - 1, a2))
    if a2 < len(O):
        cands.append((a1, a2 + 1))
    for lo, hi in cands:
        seq = O[lo:hi]
        if not seq:
            continue
        m = find_unique(R, seq)
        if m is None:
            continue
        old = m[a1 - lo:a2 - lo]
        if old:
            return old[0], old[-1] + 1
        # Insertion: right after the leading anchor, or right before the
        # trailing one.
        return (m[0] + 1, m[0] + 1) if lo < a1 else (m[0], m[0])
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old_gen")
    ap.add_argument("new_gen")
    ap.add_argument("--gen", default=GEN)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    old_files, new_files = load_tree(args.old_gen), load_tree(args.new_gen)
    old_fn, new_fn = by_name(old_files), by_name(new_files)
    real_files = load_tree(args.gen)
    real_fn = by_name(real_files)
    names = dispatch_names(args.gen)

    applied, failed, touched = 0, [], {}
    edits = {}   # real file -> list of (start, end, new_body_lines)
    for name in sorted(new_fn):
        if name not in old_fn:
            continue
        of, os_, oe = old_fn[name]
        nf, ns, ne = new_fn[name]
        O = old_files[of][os_:oe + 1]
        N = new_files[nf][ns:ne + 1]
        if O == N:
            continue
        m = SUB.match(name)
        real_name = names.get(int(m.group(1), 16), name) if m else name
        if real_name not in real_fn:
            failed.append((name, "not in the real tree"))
            continue
        rf, rs, re_ = real_fn[real_name]
        R = list(real_files[rf][rs:re_ + 1])
        ops = [op for op in difflib.SequenceMatcher(a=O, b=N, autojunk=False).get_opcodes()
               if op[0] != "equal"]
        # Hunks that share state go together or not at all: every hunk that
        # mentions a flag-operand snapshot (_fsa/_fsb, their declaration
        # included), and every hunk that mentions a materialised condition
        # (_finXXXX_*). Applying a reader without its writer would read a
        # stale local. Anything else stands alone.
        #
        # Snapshots are grouped more finely than that: a snapshot write plus
        # the reads that follow it up to the next write form one unit, since a
        # read takes the most recent write. The tree already carries an older
        # snapshot fix (`_fcN_reg`) at some sites, whose hunks cannot match;
        # the other sites in the same function still get theirs. The
        # declaration is required by every unit.
        placed, groups = [], {}
        unit, n_units = None, 0
        for k, (tag, a1, a2, b1, b2) in enumerate(ops):
            where = locate(R, O, a1, a2)
            placed.append((k, where))
            new = N[b1:b2]
            text = "\n".join(new)
            if SNAPDECL.search(text) and not SNAPDEF.search(text):
                # The declarations are inserted together, so one hunk can carry
                # both the snapshot and the materialised-condition locals; it
                # then has to go in with either group.
                key = "snapdecl"
                if FINVAR.search(text):
                    groups.setdefault("fin", []).append(k)
            elif SNAPVAR.search(text):
                first_def = next((i for i, l in enumerate(new) if SNAPDEF.search(l)), None)
                reads_first = any(SNAPVAR.search(l) and not SNAPDEF.search(l)
                                  for l in new[:first_def if first_def is not None else len(new)])
                if reads_first and unit is not None:
                    key = unit                      # continues the open unit
                elif first_def is not None:
                    n_units += 1
                    key = unit = "snap%d" % n_units
                else:
                    key = "orphan"                  # a read with no write: never alone
                    placed[k] = (k, None)
            elif FINVAR.search(text):
                key = "fin"
            else:
                key = k
            groups.setdefault(key, []).append(k)
        ok_hunks = set()
        misses = []
        decl_ok = all(placed[k][1] is not None for k in groups.get("snapdecl", []))
        for key, members in groups.items():
            if key == "snapdecl":
                continue
            needs_decl = isinstance(key, str) and key.startswith("snap")
            if all(placed[k][1] is not None for k in members) and (decl_ok or not needs_decl):
                ok_hunks.update(members)
                if needs_decl:
                    ok_hunks.update(groups.get("snapdecl", []))
            else:
                k = next(k for k in members if placed[k][1] is None)
                a1 = ops[k][1]
                misses.append(f"{'group ' + key if isinstance(key, str) else 'hunk'} "
                              f"at old line {a1 + 1}: "
                              f"{(O[a1] if a1 < len(O) else O[a1 - 1]).strip()[:60]!r}")
        spans = sorted((placed[k][1], k) for k in ok_hunks)
        for (x, y) in zip(spans, spans[1:]):
            if x[0][1] > y[0][0] or x[0] == y[0]:
                misses.append("overlapping hunks")
                ok_hunks = set()
                break
        # Bottom-up so earlier positions stay valid.
        for (s0, s1), k in sorted(((placed[k][1], k) for k in ok_hunks), reverse=True):
            tag, a1, a2, b1, b2 = ops[k]
            keep = [l for l in R[s0:s1] if INSTR.match(l)]   # instrumentation stays
            R[s0:s1] = keep + N[b1:b2]
        if misses:
            failed.append((real_name, f"{len(ok_hunks)}/{len(ops)} hunks applied; "
                                      + "; ".join(misses[:2])))
        if not ok_hunks:
            continue
        edits.setdefault(rf, []).append((rs, re_, R, real_name))
        applied += 1

    for rf, lst in edits.items():
        lines = real_files[rf]
        for rs, re_, R, real_name in sorted(lst, key=lambda t: -t[0]):
            lines[rs:re_ + 1] = R
            touched.setdefault(rf, []).append(real_name)
        if not args.dry_run:
            io.open(os.path.join(args.gen, rf), "w", encoding="utf-8",
                    newline="").write("\n".join(lines))

    print(f"{'would apply' if args.dry_run else 'applied'} to {applied} functions, "
          f"{len(failed)} not applied")
    for rf in sorted(touched):
        print(f"  {rf}: {', '.join(sorted(touched[rf]))}")
    for name, why in failed:
        print(f"  NOT APPLIED {name}: {why}")


if __name__ == "__main__":
    main()
