#!/usr/bin/env python3
"""Insert and remove instrumentation in recompiled code, safely and reversibly.

PREFER RUNTIME PROBES FIRST (added part 157).  Every generated label now carries
a RECOMP_LOC() check, so most questions this tool answers can be answered with
no rebuild at all -- 35 s instead of ~90 s, and re-aimable mid-run:

    XBOX_PROBE='0xAA24C|esi==0|esi,[esi+4]|3' ./"SSX Tricky.exe"
    python xbdiag.py --port 7777 'probe 0xAA205||esi,eax|3'

Reach for xprobe.py only for what the runtime evaluator cannot do: a true
per-function `ebp` (it is a C local, so the evaluator falls back to g_seh_ebp),
floating-point values, decoded strings, backtraces, or an arbitrary C condition.
Those still justify the 52 s rebuild; a register or a memory chain does not.

Investigating a defect in recompiled code means printing guest state from inside
generated functions.  Doing that by hand is where the mistakes come from: a
`\\n` written into a Python string that ends up as a real newline splits the C
literal in two, and the resulting build error is three steps away from the
edit that caused it.  It also leaves debris -- probes that survive a session and
quietly change behaviour later.

This tool owns both ends.  Every probe it writes carries a marker, so `clear`
removes exactly what was added and nothing else, and the escaping is handled in
one place that is tested rather than retyped each time.

    xprobe.py add sub_000BE260 --entry --show ecx esi
    xprobe.py add sub_000BE260 --label loc_000BE424 --show 'MEM32(esi + 0x1C)'
    xprobe.py add sub_000BDFD0 --entry --show ebx --when 'ebx != 0' --limit 20
    xprobe.py add sub_0014D850 --entry --backtrace
    xprobe.py list
    xprobe.py clear

Every probe is rate-limited (default 4 hits) because these sites run per frame:
an unlimited print in a draw path produces megabytes and changes the timing of
the thing you are measuring.

`--show` takes any C expression valid at that point -- guest registers (`eax`,
`esi`), memory (`MEM32(esi + 0x1C)`), or a literal.  Strings in guest memory are
awkward to read as numbers, so `--str EXPR` decodes UTF-16 (which is what the
title's localised text is) and `--cstr EXPR` decodes 8-bit.

`--backtrace` prints the host call chain rebased to link addresses, which
`sym.py` maps back to guest functions.  Use it when you need to know *who*
called something: the guest stack cannot tell you, because generated calls push
a dummy return address rather than a real one.
"""

import argparse
import ssxpaths
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_GEN = ssxpaths.GEN

MARK = "/*XPROBE*/"
BEGIN = "/*XPROBE-BEGIN*/"
END = "/*XPROBE-END*/"
FUNC = re.compile(r"^void (?P<name>\w+)\(void\)$")

# Written once, here, instead of being retyped into every ad-hoc edit.
NL = chr(92) + "n"
Q = chr(92) + '"'


def gen_files(gen_dir):
    return [os.path.join(gen_dir, n) for n in sorted(os.listdir(gen_dir))
            if n.endswith(".c")]


def find_function(gen_dir, name):
    """Return (path, lines, start, end) for a generated function body."""
    for path in gen_files(gen_dir):
        lines = io.open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
        for i, line in enumerate(lines):
            m = FUNC.match(line)
            if m and m.group("name") == name:
                j = i
                while j < len(lines) and lines[j] != "}":
                    j += 1
                return path, lines, i, j
    return None, None, None, None


def build_probe(tag, shows, strs, cstrs, backtrace, when, limit):
    """Compose one self-contained probe statement.

    Everything lives on as few lines as possible and every escape is built from
    NL/Q rather than written literally, so the emitted C cannot be broken by
    this file being edited or re-encoded.
    """
    fmt, argv = [], []
    for expr in shows:
        fmt.append("%s=0x%%08X" % expr)
        argv.append("(uint32_t)(%s)" % expr)
    for expr in strs:
        fmt.append("%s=" % expr + Q + "%s" + Q)
        argv.append("_xp_w(%s)" % expr)
    for expr in cstrs:
        fmt.append("%s=" % expr + Q + "%s" + Q)
        argv.append("_xp_c(%s)" % expr)
    if not fmt:
        fmt.append("hit")

    guard = "_xp_n++ < %d" % limit
    if when:
        guard = "(%s) && %s" % (when, guard)

    body = ('fprintf(stderr, "  [XPROBE] %s: %s%s", %s);'
            % (tag, " ".join(fmt), NL, ", ".join(argv)) if argv else
            'fprintf(stderr, "  [XPROBE] %s: %s%s");' % (tag, " ".join(fmt), NL))

    if backtrace:
        body += (' { void *_bt[14]; unsigned short _nf ='
                 ' CaptureStackBackTrace(0, 14, _bt, 0), _q;'
                 ' uintptr_t _b = (uintptr_t)GetModuleHandleW(0);'
                 ' fprintf(stderr, "  [XPROBE]   callers:");'
                 ' for (_q = 1; _q < _nf; _q++) fprintf(stderr, " 0x%%llX",'
                 ' (unsigned long long)((uintptr_t)_bt[_q] - _b + 0x140000000ull));'
                 ' fprintf(stderr, "%s"); }' % NL)

    return ("    { static int _xp_n = 0; if (%s) { %s fflush(stderr); } } %s"
            % (guard, body, MARK))


HELPERS = """
/* xprobe helpers -- decode guest strings for printing.  Static buffers are fine
 * here: probes are rate-limited and only ever run on one thread at a time. */
static const char *_xp_w(uint32_t va)   /* UTF-16, as the title stores text */
{
    static char b[96];
    const unsigned short *w;
    int i;
    if (!va) return "(null)";
    w = (const unsigned short *)XBOX_PTR(va);
    for (i = 0; i < 95 && w[i]; i++) b[i] = (char)(w[i] & 0xFF);
    b[i] = 0;
    return b;
}

static const char *_xp_c(uint32_t va)   /* 8-bit */
{
    static char b[96];
    const char *s;
    int i;
    if (!va) return "(null)";
    s = (const char *)XBOX_PTR(va);
    for (i = 0; i < 95 && s[i]; i++) b[i] = s[i];
    b[i] = 0;
    return b;
}
"""


def ensure_helpers(lines):
    """Add the decode helpers and <windows.h> once per file."""
    text = "\n".join(lines)
    changed = False
    if BEGIN not in text:
        for i, l in enumerate(lines):
            if l.startswith("#include") and "math.h" in l:
                lines[i + 1:i + 1] = [BEGIN, "#include <windows.h>",
                                      "#include <stdio.h>", HELPERS, END]
                changed = True
                break
    return changed


def cmd_add(args):
    path, lines, start, end = find_function(args.gen, args.function)
    if path is None:
        print("function %r not found under %s" % (args.function, args.gen))
        return 2

    probe = build_probe(args.tag or args.function, args.show, args.str_,
                        args.cstr, args.backtrace, args.when, args.limit)

    if args.entry:
        # After the opening brace and any declarations, at the first label.
        idx = None
        for k in range(start, end):
            if re.match(r"^\s*loc_[0-9A-Fa-f]+: ;\s*$", lines[k]):
                idx = k + 1
                break
        if idx is None:
            idx = start + 2
        where = "entry"
    elif args.label:
        idx = None
        for k in range(start, end):
            if lines[k].strip() == "%s: ;" % args.label:
                idx = k + 1
                break
        if idx is None:
            print("label %r not found in %s" % (args.label, args.function))
            return 2
        where = args.label
    elif args.before:
        idx = None
        for k in range(start, end):
            if args.before in lines[k] and MARK not in lines[k]:
                idx = k
                break
        if idx is None:
            print("text %r not found in %s" % (args.before, args.function))
            return 2
        where = "before %r" % args.before
    else:
        print("choose one of --entry, --label LABEL, --before TEXT")
        return 2

    lines.insert(idx, probe)
    ensure_helpers(lines)
    io.open(path, "w", encoding="utf-8", errors="surrogateescape",
            newline="").write("\n".join(lines))
    print("probe added to %s at %s (%s)" % (args.function, where, os.path.basename(path)))
    print("  rebuild, run, then: grep '\\[XPROBE\\]' <log>")
    return 0


def cmd_list(args):
    total = 0
    for path in gen_files(args.gen):
        lines = io.open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
        func = None
        for i, line in enumerate(lines):
            m = FUNC.match(line)
            if m:
                func = m.group("name")
            if MARK in line:
                total += 1
                print("  %-20s:%-6d %-30s %s"
                      % (os.path.basename(path), i + 1, func or "?",
                         line.strip()[:60]))
    print("%d probe(s) active" % total)
    return 0


def cmd_clear(args):
    """Remove exactly what `add` inserted.

    Probe statements carry MARK on their own line; the helper block is fenced by
    BEGIN/END.  Both are removed by matching those markers and nothing else, so
    this cannot touch generated code even if a file has been edited since.
    """
    removed = 0
    for path in gen_files(args.gen):
        lines = io.open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
        out, i, changed = [], 0, False
        while i < len(lines):
            if BEGIN in lines[i]:
                while i < len(lines) and END not in lines[i]:
                    i += 1
                i += 1                      # step past END
                removed += 1
                changed = True
                continue
            if MARK in lines[i]:
                removed += 1
                changed = True
                i += 1
                continue
            out.append(lines[i])
            i += 1
        if changed:
            io.open(path, "w", encoding="utf-8", errors="surrogateescape",
                    newline="").write("\n".join(out))
    print("removed %d probe/helper block(s)" % removed)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=DEFAULT_GEN)
    sub = ap.add_subparsers(dest="cmd", required=True)

    a = sub.add_parser("add", help="insert a probe")
    a.add_argument("function")
    a.add_argument("--entry", action="store_true", help="at the function's first label")
    a.add_argument("--label", help="after a specific loc_XXXXXXXX label")
    a.add_argument("--before", help="immediately before the first line containing TEXT")
    a.add_argument("--show", nargs="*", default=[], metavar="EXPR",
                   help="C expressions to print as hex")
    a.add_argument("--str", dest="str_", nargs="*", default=[], metavar="EXPR",
                   help="guest UTF-16 string pointers to decode")
    a.add_argument("--cstr", nargs="*", default=[], metavar="EXPR",
                   help="guest 8-bit string pointers to decode")
    a.add_argument("--when", help="only fire when this C condition holds")
    a.add_argument("--limit", type=int, default=4, help="max hits (default 4)")
    a.add_argument("--tag", help="label for the log line (default: function name)")
    a.add_argument("--backtrace", action="store_true",
                   help="also print the host call chain, rebased for sym.py")
    a.set_defaults(func=cmd_add)

    l = sub.add_parser("list", help="show active probes")
    l.set_defaults(func=cmd_list)

    c = sub.add_parser("clear", help="remove every probe this tool added")
    c.set_defaults(func=cmd_clear)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
