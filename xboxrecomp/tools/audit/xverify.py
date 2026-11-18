#!/usr/bin/env python3
"""Audit recompiled code against the instructions it claims to implement.

The recompiler annotates each translated statement with the x86 instruction it
came from.  When it cannot translate something it still emits the annotation --
just with no code attached:

    /* fnstsw ax - store FPU status word */          <- nothing happens
    (void)0; /* test eax, eax - flags set for next jcc */

Both forms are silent.  The build is clean, the function looks translated, and
at runtime the instruction simply does not exist.  This tool finds them,
groups them by opcode, and tells you how much of the program each class touches.

That matters because these defects are *systematic*, never one-off.  The
`fnstsw ax` case is 2,074 sites -- every floating-point comparison in the
program -- and each one leaves the following `test ah, 0x41` reading a stale
register.  Nothing about a single site looks alarming; the count is what makes
the case.

    xverify.py                     rank every dropped-instruction class
    xverify.py --opcode fnstsw     every site for one opcode, with functions
    xverify.py --function sub_X    audit one function against the XBE itself
    xverify.py --json out.json     machine-readable, for diffing across runs

`--function` is the strong check: it disassembles the real bytes from the XBE
and lines them up against the generated statements, so it catches instructions
the recompiler dropped *without* leaving an annotation, which the scan cannot
see.  Use it when a specific function misbehaves; use the scan to decide which
functions are worth that attention.
"""

import argparse
import ssxpaths
import io
import json
import os
import re
import struct
import subprocess
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

DEFAULT_GEN = ssxpaths.GEN
DEFAULT_XBE = ssxpaths.XBE

# A line whose entire content is an instruction annotation: the recompiler had
# something to say about this instruction but emitted no code for it.
DROPPED = re.compile(r"^\s*/\*\s*(?P<op>[a-z][a-z0-9_.]*)\b(?P<rest>[^*]*)\*/\s*$")

# A statement that exists only to carry the annotation.  Not always a bug --
# this is how deferred flag computation is expressed -- but it is the same
# shape, and a `cmp` deferred to a branch that never consumes it is dead.
NOOP = re.compile(r"^\s*\(void\)0;\s*/\*\s*(?P<op>[a-z][a-z0-9_.]*)\b(?P<rest>[^*]*)\*/\s*$")

FUNC = re.compile(r"^void (?P<name>\w+)\(void\)$")
ORIGIN = re.compile(r"^\s*\*\s*Original:\s*0x(?P<start>[0-9A-Fa-f]{8})\s*-\s*"
                    r"0x(?P<end>[0-9A-Fa-f]{8})")

# Opcodes whose omission is known to change behaviour rather than just timing.
# Anything here is reported as consequential even at low counts.
CONSEQUENTIAL = {
    "fnstsw":  "FP compare result never reaches AH; the following test/branch reads a stale register",
    "fstp":    "FP store dropped: the value never reaches memory and the stack is left one deep",
    "fld":     "FP load dropped: the stack is one shallow, so every later st(N) is off by one",
    "fldcw":   "rounding/precision control never applied",
    "fnstcw":  "control word never read back",
    "fistp":   "FP-to-integer store dropped",
    "pand":    "SIMD mask dropped",
    "por":     "SIMD or dropped",
    "pxor":    "SIMD xor dropped",
    "sahf":    "flags never loaded from AH",
    "lahf":    "AH never loaded from flags",
    "bsf":     "bit scan dropped",
    "bsr":     "bit scan dropped",
    "rep":     "string operation dropped",
    "movs":    "string move dropped",
    "stos":    "string store dropped",
    "cmps":    "string compare dropped",
    "div":     "division dropped",
    "idiv":    "division dropped",
    "repe":    "repeated string compare dropped",
    "repne":   "repeated string scan dropped",
    "std":     "direction flag never set; any following string op runs the wrong way",
    "andps":   "SIMD and dropped",
}

# Opcodes whose omission is genuinely harmless.
BENIGN = {"nop", "wait", "hlt", "lea", "emms", "cld"}

# The recompiler defers flag-setting instructions to the branch that consumes
# them, which is a deliberate design rather than a dropped instruction.  It is
# still worth counting: the deferral is only sound while nothing writes the
# operand in between, and when something does, the branch silently tests the
# new value.  See flagclobber.py for that check.
BY_DESIGN = {
    "cmp":  "deferred to the consuming branch (by design; see flagclobber.py)",
    "test": "deferred to the consuming branch (by design; see flagclobber.py)",
}

# Words that appear at the start of prose comments and are not opcodes.
NOT_OPCODES = {
    "the", "this", "not", "was", "see", "and", "return", "fall", "nop",
    "align", "no", "it", "its", "a", "an", "so", "but", "then", "each",
}


def gen_files(gen_dir):
    out = []
    for name in sorted(os.listdir(gen_dir)):
        if name.endswith(".c") and not name.endswith(".bak"):
            out.append(os.path.join(gen_dir, name))
    return out


def scan(gen_dir):
    """Walk the generated sources, attributing every dropped instruction to the
    function it sits in."""
    hits = []
    for path in gen_files(gen_dir):
        try:
            lines = io.open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
        except OSError:
            continue
        func, origin = None, None
        for i, line in enumerate(lines):
            m = ORIGIN.match(line)
            if m:
                origin = (int(m.group("start"), 16), int(m.group("end"), 16))
                continue
            m = FUNC.match(line)
            if m:
                func = m.group("name")
                continue
            for kind, pat in (("dropped", DROPPED), ("noop", NOOP)):
                m = pat.match(line)
                if not m:
                    continue
                op = m.group("op")
                # The recompiler also uses bare comments for prose.  An opcode
                # is a short lowercase token; prose is not.
                if len(op) > 8 or op in NOT_OPCODES:
                    continue
                hits.append({
                    "file": os.path.basename(path), "line": i + 1,
                    "function": func, "origin": origin,
                    "kind": kind, "opcode": op,
                    "text": line.strip()[:110],
                })
                break
    return hits


def rank(hits, show_benign):
    by_op = defaultdict(list)
    for h in hits:
        by_op[h["opcode"]].append(h)

    rows = []
    for op, group in by_op.items():
        if not show_benign and op in BENIGN:
            continue
        rows.append((len(group), op, group))
    rows.sort(reverse=True)
    return rows


def print_ranked(rows, hits, show_benign):
    total = sum(n for n, _, _ in rows)
    print("Instructions annotated but not implemented")
    print("=" * 78)
    if not rows:
        print("  none found")
        return
    print("  %-10s %8s  %s" % ("OPCODE", "SITES", "CONSEQUENCE"))
    print("  " + "-" * 74)
    for n, op, group in rows:
        note = CONSEQUENTIAL.get(op) or BY_DESIGN.get(op)
        if note is None:
            note = "benign" if op in BENIGN else "unclassified -- check before assuming harmless"
        print("  %-10s %8d  %s" % (op, n, note))
        fns = sorted({g["function"] for g in group if g["function"]})
        print("  %-10s %8s  spread over %d function(s), e.g. %s"
              % ("", "", len(fns), ", ".join(fns[:3]) or "?"))
    print("  " + "-" * 74)
    print("  %-10s %8d  total%s" % ("", total,
          "" if show_benign else "  (benign opcodes hidden; --all to include)"))

    worst = [(n, op) for n, op, _ in rows if op in CONSEQUENTIAL]
    if worst:
        print()
        print("Consequential classes, largest first:")
        for n, op in worst:
            print("  %6d x %-8s %s" % (n, op, CONSEQUENTIAL[op]))
        print()
        print("A class this size is never a one-off. Fix the emitter, not the site --")
        print("and re-measure, because a branch that starts reading a correct value")
        print("can expose a second defect that the stale value was masking.")


def print_opcode(hits, opcode, limit):
    sel = [h for h in hits if h["opcode"] == opcode]
    if not sel:
        print("no sites for opcode %r" % opcode)
        return
    print("%d site(s) for %r" % (len(sel), opcode))
    note = CONSEQUENTIAL.get(opcode)
    if note:
        print("consequence: %s" % note)
    print()
    by_fn = defaultdict(list)
    for h in sel:
        by_fn[h["function"] or "?"].append(h)
    for fn in sorted(by_fn)[:limit]:
        group = by_fn[fn]
        origin = group[0]["origin"]
        where = "0x%08X" % origin[0] if origin else "?"
        print("  %-30s %s  (%d site%s)"
              % (fn, where, len(group), "" if len(group) == 1 else "s"))
        for h in group[:3]:
            print("      %s:%-6d %s" % (h["file"], h["line"], h["text"]))
    if len(by_fn) > limit:
        print("  ... and %d more function(s); raise --limit to see them"
              % (len(by_fn) - limit))


# ---------------------------------------------------------------------------
# Per-function audit against the XBE
# ---------------------------------------------------------------------------

def xbe_sections(data):
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    table = struct.unpack_from("<I", data, 0x120)[0] - base
    out = []
    for i in range(count):
        off = table + i * 0x38
        _flags, va, vsize, raw, _rsize = struct.unpack_from("<IIIII", data, off)
        name_ptr = struct.unpack_from("<I", data, off + 0x14)[0] - base
        end = data.index(b"\0", name_ptr)
        out.append((data[name_ptr:end].decode("ascii", "replace"), va, vsize, raw))
    return out


def va_to_raw(sections, va):
    for _name, s_va, s_size, s_raw in sections:
        if s_va <= va < s_va + s_size:
            return s_raw + (va - s_va)
    return None


def disassemble(xbe_path, start, end):
    data = io.open(xbe_path, "rb").read()
    raw = va_to_raw(xbe_sections(data), start)
    if raw is None:
        return None, "0x%08X is not inside any XBE section" % start
    blob = data[raw:raw + (end - start)]
    tmp = os.path.join(HERE, ".xverify_tmp.bin")
    io.open(tmp, "wb").write(blob)
    try:
        out = subprocess.run(
            ["objdump", "-D", "-b", "binary", "-m", "i386", "-M", "intel",
             "--adjust-vma=0x%X" % start, tmp],
            capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "objdump failed: %s" % exc
    finally:
        try:
            os.remove(tmp)
        except OSError:
            pass

    insns = []
    for line in out.split("\n"):
        m = re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)(.*)$", line)
        if m:
            insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
    return insns, None


def audit_range(gen_dir, xbe_path, start, end, sites=False):
    """Compare an address range's **push order** against the generated block.

    `--function` needs the `Original:` range comment the recompiler emits, so it
    cannot audit a block written or edited **by hand inside** a larger function
    -- and that is exactly the code most worth auditing. A hand-transcribed
    block at loc_000AFBEE had two pushes the wrong way round for several parts,
    turning `memset(buf, 0, n)` into `memset(NULL, buf, n)`. Nothing could check
    it, because presence-based checks see both pushes and are satisfied.

    Argument order is the failure mode, so that is what this compares: the
    ordered sequence of pushes in the XBE against the ordered sequence of
    PUSH32 statements in the generated block, side by side.
    """
    insns, err = disassemble(xbe_path, start, end)
    if err:
        print(err)
        return 2

    label = "loc_%08X:" % start
    body, path = None, None
    for p in gen_files(gen_dir):
        lines = io.open(p, encoding="utf-8", errors="surrogateescape").read().split(chr(10))
        for i, line in enumerate(lines):
            if line.strip().startswith(label):
                stop = re.compile(r"^\s*loc_([0-9A-F]{8})\s*:")
                j = i + 1
                while j < len(lines):
                    m = stop.match(lines[j])
                    if m and int(m.group(1), 16) >= end:
                        break
                    if lines[j] == "}":
                        break
                    j += 1
                body, path = lines[i:j], p
                break
        if body:
            break

    if body is None:
        print("no generated code carries %s -- give a range that starts at a "
              "label boundary" % label)
        return 2

    xbe_pushes = [(a, args.strip()) for a, op, args in insns if op == "push"]
    gen_pushes = []
    for line in body:
        for m in re.finditer(r"PUSH32\(esp,\s*([^)]*)\)", line):
            arg = m.group(1).strip()
            gen_pushes.append(arg)
    # The generated code pushes a dummy 0 immediately before each call; drop
    # those so the two sequences line up.
    trimmed, k = [], 0
    for i, line in enumerate(body):
        for m in re.finditer(r"PUSH32\(esp,\s*([^)]*)\);\s*(\w+)?", line):
            arg = m.group(1).strip()
            is_dummy = arg == "0" and ("(); /* call" in line or "();" in line)
            if not is_dummy:
                trimmed.append(arg)
    print("range 0x%08X - 0x%08X   (%s)" % (start, end, os.path.basename(path)))
    print("XBE pushes: %d      generated PUSH32 (dummies dropped): %d"
          % (len(xbe_pushes), len(trimmed)))
    print()
    print("  %-12s %-24s %s" % ("addr", "XBE push", "generated push"))
    n = max(len(xbe_pushes), len(trimmed))
    bad = 0
    for i in range(n):
        a, x = xbe_pushes[i] if i < len(xbe_pushes) else ("", "-- none --")
        g = trimmed[i] if i < len(trimmed) else "-- none --"
        flag = ""
        xn = x.lower().lstrip("0x").lstrip("0") or "0"
        gn = g.lower().lstrip("0x").lstrip("0").rstrip("u") or "0"
        if xn != gn and not (x.lower() in g.lower() or g.lower() in x.lower()):
            flag = "   <== differs"
            bad += 1
        print("  %-12s %-24s %s%s"
              % (("0x%08X" % a) if a else "", x, g, flag))
    print()
    if bad:
        print("%d push(es) do not line up. Order is what matters: a swapped pair"
              " changes the callee's arguments and no presence check sees it."
              % bad)
    else:
        print("Push order matches the XBE.")
    return 1 if bad else 0


def audit_function(gen_dir, xbe_path, name, sites=False):
    body, origin, path = None, None, None
    for p in gen_files(gen_dir):
        lines = io.open(p, encoding="utf-8", errors="surrogateescape").read().split("\n")
        for i, line in enumerate(lines):
            if FUNC.match(line) and FUNC.match(line).group("name") == name:
                for k in range(max(0, i - 12), i):
                    m = ORIGIN.match(lines[k])
                    if m:
                        origin = (int(m.group("start"), 16), int(m.group("end"), 16))
                j = i
                while j < len(lines) and lines[j] != "}":
                    j += 1
                body, path = lines[i:j], p
                break
        if body:
            break

    if body is None:
        print("function %r not found under %s" % (name, gen_dir))
        return 2
    if origin is None:
        print("%s has no 'Original:' range comment; cannot audit against the XBE" % name)
        return 2

    start, end = origin
    insns, err = disassemble(xbe_path, start, end)
    if err:
        print(err)
        return 2

    print("%s   0x%08X - 0x%08X   (%s)" % (name, start, end, os.path.basename(path)))
    print("XBE has %d instruction(s); generated body has %d line(s)"
          % (len(insns), len(body)))
    print()

    emitted = "\n".join(body)
    missing, comment_only = [], []
    for addr, op, args in insns:
        if op in BENIGN:
            continue
        hit = False
        for line in body:
            if re.search(r"\b%s\b" % re.escape(op), line) and \
               (DROPPED.match(line) or NOOP.match(line)):
                comment_only.append((addr, op, args))
                hit = True
                break
        if hit:
            continue
        # "Not mentioned" is only meaningful for opcodes the recompiler always
        # annotates.  Ordinary data movement (mov, push, lea, add) is translated
        # straight into C with no comment at all, so its absence from the
        # comments says nothing -- reporting it would bury the real findings
        # under dozens of false positives.
        if op in CONSEQUENTIAL and not re.search(
                r"/\*[^*]*\b%s\b" % re.escape(op), emitted):
            missing.append((addr, op, args))

    if missing:
        print("Consequential instructions with no trace in the translation (%d):" % len(missing))
        for addr, op, args in missing[:20]:
            print("   0x%08X  %-8s %s" % (addr, op, args))
        if len(missing) > 20:
            print("   ... and %d more" % (len(missing) - 20))
        print()
    if comment_only:
        print("Annotated but not implemented (%d):" % len(comment_only))
        # One line per distinct opcode by default -- 23 sites of the same
        # deferred `cmp` are one fact, not 23. The count makes it obvious the
        # list is collapsed, which the first version did not: it printed two
        # lines under a heading that said 23 and read like truncation.
        seen = {}
        order = []
        for addr, op, args in comment_only:
            if op not in seen:
                seen[op] = (addr, args, 0)
                order.append(op)
            a, g, n = seen[op]
            seen[op] = (a, g, n + 1)
        for op in order:
            addr, args, n = seen[op]
            note = CONSEQUENTIAL.get(op, "")
            count = ("x%d" % n) if n > 1 else "  "
            print("   0x%08X  %-8s %-28s %-4s %s"
                  % (addr, op, args[:28], count, note))
        if any(n > 1 for _a, _g, n in seen.values()):
            print("   (one line per opcode; pass --sites for every address)")
        if sites:
            print()
            for addr, op, args in comment_only:
                print("   0x%08X  %-8s %s" % (addr, op, args))
        print()
    if not missing and not comment_only:
        print("Every non-trivial instruction has a corresponding statement.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=DEFAULT_GEN, help="generated sources directory")
    ap.add_argument("--xbe", default=DEFAULT_XBE, help="path to default.xbe")
    ap.add_argument("--opcode", help="list every site for one opcode")
    ap.add_argument("--function", help="audit one function against the XBE")
    ap.add_argument("--range", metavar="START-END",
                    help="audit an explicit address range, for a block written "
                         "by hand inside a larger function (no Original: comment)")
    ap.add_argument("--sites", action="store_true",
                    help="with --function, list every address rather than "
                         "one line per opcode")
    ap.add_argument("--json", help="write the full findings as JSON")
    ap.add_argument("--all", action="store_true", help="include benign opcodes")
    ap.add_argument("--limit", type=int, default=25, help="functions to list per opcode")
    args = ap.parse_args()

    if args.range:
        try:
            lo, hi = [int(x, 16) for x in args.range.replace("0x", "").split("-")]
        except ValueError:
            print("--range wants START-END in hex, e.g. 000AFBEE-000AFC25")
            return 2
        return audit_range(args.gen, args.xbe, lo, hi, args.sites)

    if args.function:
        return audit_function(args.gen, args.xbe, args.function, args.sites)

    if not os.path.isdir(args.gen):
        print("generated sources not found at %s" % args.gen)
        return 2

    hits = scan(args.gen)
    if args.json:
        io.open(args.json, "w", encoding="utf-8").write(json.dumps(hits, indent=1))
        print("wrote %d finding(s) to %s" % (len(hits), args.json))

    if args.opcode:
        print_opcode(hits, args.opcode, args.limit)
        return 0

    print_ranked(rank(hits, args.all), hits, args.all)
    return 0


if __name__ == "__main__":
    sys.exit(main())
