#!/usr/bin/env python3
"""Recover a batch of guest functions, splice them in, and keep the batch only
if the build still measures clean.

The recovery frontier recedes: every batch of recovered functions unlocks code
that surfaces the next set of unresolved targets, so batches come in a long
series. Two things went wrong doing that by hand (RE_NOTES part 120):

  * A batch was rolled back and took the previous, *good* batch with it,
    because there was no backup between them.
  * A batch linked cleanly, ran to exit 124, and was still writing over guest
    `.text`. Crash count and draw count both looked fine.

So this takes a backup per batch, and gates on three signals together:
`exit == 124`, `crash == 0`, `textcorrupt == 0`, plus draws inside a band.
A batch that fails any of them is reverted automatically and reported.

    recover_batch.py 0x0004C140 0x0007BEA0 ...
    recover_batch.py --from-miss run.log        # take targets from ICALL-MISS lines
    recover_batch.py --keep-going 0x...         # splice even if the gate fails
    recover_batch.py --runs 3 --seconds 70 0x...

Closure is driven off the **linker**, not a source grep: a source scan over
`gen/*.c` cannot see how definitions are spread across the split translation
units and once produced thousands of false positives. Both `undefined
reference` and `multiple definition` are matched -- a loop that greps only for
the former reports LINK CLEAN while the build is failing.
"""

import argparse
import ssxpaths
import io
import os
import re
import shutil
import subprocess
import sys
import time

import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
XBOX = os.path.join(ROOT, "xboxrecomp")
XBE = ssxpaths.XBE
ANALYSIS = ssxpaths.ANALYSIS
GEN = ssxpaths.GEN
# Lift call targets under the names the tree already uses (lifter._tree_names).
os.environ.setdefault("XLIFT_NAME_MAP", os.path.join(GEN, "recomp_dispatch.c"))
RECOMP_DIR = ssxpaths.RECOMP
BUILD = ssxpaths.BUILD
EXE = os.path.join(BUILD, "SSX Tricky.exe")
BACKUPS = ssxpaths.BACKUPS

FILES = [(GEN, "recomp_recovered.c"), (GEN, "recomp_dispatch.c"),
         (GEN, "recomp_stubs_unresolved.c"), (RECOMP_DIR, "recomp_funcs.h"),
         (GEN, "recomp_recovered.h")]

ENTRY = re.compile(r"^\s*\{ 0x([0-9A-Fa-f]{8})u, \(recomp_func_t\)"
                   r"([A-Za-z_][A-Za-z0-9_]*)")
BODY = re.compile(r"^void (sub_[0-9A-F]{8})\(void\)$", re.M)


def run(cmd, cwd=None, timeout=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                       errors="replace", timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def backup(tag):
    d = os.path.join(BACKUPS, tag)
    os.makedirs(d, exist_ok=True)
    for src, name in FILES:
        shutil.copyfile(os.path.join(src, name), os.path.join(d, name))
    return d


def restore(d):
    for src, name in FILES:
        shutil.copyfile(os.path.join(d, name), os.path.join(src, name))


def seed_and_lift(work, addrs):
    """Run disasm + func_id once for the whole seed set, then lift each."""
    os.makedirs(work, exist_ok=True)
    seeds = os.path.join(work, "seeds.json")
    io.open(seeds, "w", encoding="utf-8").write(
        "[" + ",".join('{"start":"0x%08X"}' % a for a in addrs) + "]")
    rc, out = run([sys.executable, "-m", "tools.disasm", XBE, "-o",
                   os.path.join(work, "disasm"), "--text-only", "--force",
                   "--analysis-json", ANALYSIS, "--seed-functions", seeds],
                  cwd=XBOX, timeout=900)
    if rc != 0:
        sys.exit("disasm failed:\n" + out[-2000:])
    run([sys.executable, "-m", "tools.func_id", XBE,
         "--functions", os.path.join(work, "disasm", "functions.json"),
         "--strings", os.path.join(work, "disasm", "strings.json"),
         "--xrefs", os.path.join(work, "disasm", "xrefs.json"),
         "-o", os.path.join(work, "funcid")], cwd=XBOX, timeout=900)
    bodies = []
    for a in addrs:
        rc, out = run([sys.executable, "-m", "tools.recomp", XBE, "-f",
                       "0x%08X" % a, "-o", os.path.join(work, "o"),
                       "--disasm-dir", os.path.join(work, "disasm"),
                       "--func-id-dir", os.path.join(work, "funcid"),
                       "--skip-binary-check"], cwd=XBOX, timeout=900)
        # Keep the doc comment above each body: its `Original:` range is how
        # fixfpmem.py and xverify.py find the instructions to check a body
        # against, and without it every recovered function was skipped by
        # them (RE_NOTES part 178).
        keep, on, doc = [], False, []
        for line in out.split("\n"):
            line = line.rstrip("\r")
            if not on:
                if line.startswith("/**"):
                    doc = [line]
                    continue
                if doc and not doc[-1].rstrip().endswith("*/"):
                    doc.append(line)
                    continue
            if BODY.match(line):
                on = True
                keep.extend(doc)
                doc = []
            elif not on and line.strip():
                doc = []
            if on:
                keep.append(line)
            if on and line == "}":
                on = False
        if keep:
            bodies.append("\n".join(keep))
    return fix_scalar_simd("\n\n".join(bodies))


def fix_scalar_simd(text):
    """Rewrite `movaps`/`movups` emitted as a scalar float copy.

    The lifter usually emits a 128-bit move correctly (`recomp_xmm_t` plus
    `MEMX`), but on a rare path it emits `float xmm0; xmm0 = MEMF(a);
    MEMF(b) = xmm0;` -- which moves 4 of the 16 bytes and leaves the other 12
    stale. That is worse than the call being dropped, which is what it was
    doing before recovery: `sub_0007FF70` is a bare 16-byte copy, and enabling
    the scalar version took draws from ~89,000 to ~7,400 (RE_NOTES part 121).

    Only 1 site in the tree had this against 2,960 correct ones, so it is a
    corner of the lifter rather than a systemic gap -- but a newly lifted
    function can land on it, so every batch gets checked.
    """
    # Per function: switching the declaration to the 128-bit union means every
    # scalar use in that function has to name its lane too. Converting only
    # the movaps lines (as this used to) left `xmm0 = MEMF(...); /* movss */`
    # assigning a float to a union -- a compile error that reverted a whole
    # batch in part 178.
    out, n = [], 0
    for chunk in re.split(r"(?m)(?=^void \w+\(void\)$)", text):
        if not re.search(r"xmm\d = MEMF\([^;]+\); /\* mov[au]ps \*/"
                         r"|MEMF\([^;]+\) = xmm\d; /\* mov[au]ps \*/", chunk):
            out.append(chunk)
            continue
        lines = []
        for line in chunk.split("\n"):
            if line.startswith("    float xmm"):
                line = line.replace("    float xmm", "    recomp_xmm_t xmm", 1)
            elif re.search(r"/\* mov[au]ps \*/", line):
                if re.search(r"xmm\d = MEMF\(", line) or re.search(r"MEMF\([^;]+\) = xmm\d;", line):
                    n += 1
                line = re.sub(r"xmm(\d) = MEMF\(([^;]+)\); /\* (mov[au]ps) \*/",
                              r"xmm\1.x = MEMX(\2); /* \3 -- 128-bit, not a scalar float */", line)
                line = re.sub(r"MEMF\(([^;]+)\) = xmm(\d); /\* (mov[au]ps) \*/",
                              r"MEMX(\1) = xmm\2.x; /* \3 -- 128-bit, not a scalar float */", line)
                # register-register movaps stays a whole-union copy
            elif "xmm" in line and "memcpy(" not in line:
                mc = re.search(r"/\* (\w+)", line)
                lane = "d" if mc and mc.group(1).endswith("sd") else "f"
                code, sep, comment = line.partition("/*")
                code = re.sub(r"\bxmm([0-7])\b(?!\.)", r"xmm\1." + lane, code)
                line = code + sep + comment
            lines.append(line)
        out.append("\n".join(lines))
    if n:
        print("  corrected %d scalar-float movaps site(s)" % n)
    return "".join(out)


def split_bodies(text):
    """{name: full text} for each `void sub_X(void) { ... }` in `text`.

    Splitting on blank lines does not work: generated bodies contain a blank
    line before every `loc_` label, so it shreds every function into fragments
    and splicing those produces `expected declaration or statement at end of
    input`. Scan from the header to the line that is exactly `}` instead.
    """
    out = {}
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        m = BODY.match(lines[i])
        if not m:
            i += 1
            continue
        e = i + 1
        while e < len(lines) and lines[e] != "}":
            e += 1
        s = i   # include the doc comment directly above (see seed_and_lift)
        while s > 0 and lines[s - 1].lstrip().startswith(("/*", "*", "*/")):
            s -= 1
        out[m.group(1)] = "\n".join(lines[s:e + 1])
        i = e + 1
    return out


def drop_body(text, name):
    """Remove one `void name(void) { ... }` definition, by line.

    A regex over the whole file is not safe here: bodies contain braces inside
    strings and comments, and a non-greedy match happily stops at the first
    line that merely looks like a closing brace, leaving half a function
    behind -- which produced `expected declaration or statement at end of
    input` the first time this ran.
    """
    lines = text.split("\n")
    head = "void %s(void)" % name
    try:
        s = lines.index(head)
    except ValueError:
        return text
    e = s + 1
    while e < len(lines) and lines[e] != "}":
        e += 1
    while s > 0 and (lines[s - 1].strip() == ""
                     or lines[s - 1].lstrip().startswith(("/*", "*", "*/"))):
        s -= 1
    del lines[s:e + 1]
    return "\n".join(lines)


def splice(body, note):
    """Add bodies, declarations and dispatch entries; drop superseded stubs."""
    names = set(BODY.findall(body))
    addrs = set(int(n[4:], 16) for n in names)
    if not names:
        return set()

    p = os.path.join(GEN, "recomp_dispatch.c")
    lines = io.open(p, encoding="utf-8", errors="surrogateescape").read().split("\n")
    existing = {}
    for l in lines:
        m = ENTRY.match(l.rstrip("\r"))
        if m:
            existing[int(m.group(1), 16)] = m.group(2)

    # An address already registered under a *different* symbol means this
    # recovery duplicates a function that was given a real name -- the part-60
    # bug. Registering it again also puts a duplicate key in a binary-searched
    # table. Drop those from the batch rather than shadowing them.
    dupes = {a for a in addrs
             if a in existing and existing[a] != ("sub_%08X" % a)}
    for a in sorted(dupes):
        print("  skipping 0x%08X: already registered as %s" % (a, existing[a]))
        body = drop_body(body, "sub_%08X" % a)
        # Match every call form, not just `/* call 0x... */`: the lifter also
        # emits tail jumps (`g_seh_ebp = ebp; sub_X(); return; /* tail jmp */`)
        # and fall-throughs. Rewriting only one form left the others behind and
        # closure looped forever on a symbol it had decided not to add.
        old = "sub_%08X();" % a
        new = "%s();" % existing[a]
        body = body.replace(old, new)
        # Call sites spliced by an earlier round of this same batch are
        # already in the file, and the linker will keep asking for the
        # dropped symbol until they are rewritten too -- otherwise closure
        # loops forever on an address it has decided not to add.
        for tgt in ("recomp_recovered.c",):
            q = os.path.join(GEN, tgt)
            t = io.open(q, encoding="utf-8", errors="surrogateescape").read()
            if old in t:
                io.open(q, "w", encoding="utf-8", errors="surrogateescape",
                        newline="").write(t.replace(old, new))
                print("    rewrote %d existing call site(s) in %s"
                      % (t.count(old), tgt))
    names -= set("sub_%08X" % a for a in dupes)
    addrs -= dupes

    # A recovered body supersedes an undetected stub for the same address.
    p = os.path.join(GEN, "recomp_stubs_unresolved.c")
    sl = io.open(p, encoding="utf-8", errors="surrogateescape").read().split("\n")
    out = [l for l in sl
           if not any(("void %s(void) { " % n) in l and "not detected" in l
                      for n in names)]
    if len(out) != len(sl):
        io.open(p, "w", encoding="utf-8", errors="surrogateescape",
                newline="").write("\n".join(out))
        print("  superseded %d undetected stub(s)" % (len(sl) - len(out)))

    p = os.path.join(GEN, "recomp_recovered.c")
    s = io.open(p, encoding="utf-8", errors="surrogateescape").read()
    io.open(p, "w", encoding="utf-8", errors="surrogateescape",
            newline="").write(s + "\n/* " + note + " */\n\n" + body + "\n")

    # Declarations go to recomp_recovered.h, which only recomp_recovered.c and
    # recomp_dispatch.c include. Appending them to recomp_funcs.h -- included
    # by every generated file -- made each splice recompile all of gen/: the
    # part-180 sweep spent ~16 minutes per closure round doing that.
    funcs = io.open(os.path.join(RECOMP_DIR, "recomp_funcs.h"), encoding="utf-8",
                    errors="surrogateescape").read()
    p = os.path.join(GEN, "recomp_recovered.h")
    h = io.open(p, encoding="utf-8", errors="surrogateescape").read()
    add = "".join("void %s(void);\n" % n for n in sorted(names)
                  if ("void %s(void);" % n) not in h
                  and ("void %s(void);" % n) not in funcs)
    if add:
        head, sep, tail = h.rpartition("#endif")
        io.open(p, "w", encoding="utf-8", errors="surrogateescape",
                newline="").write(head.rstrip("\n") + "\n" + add + "\n" + sep + tail)

    p = os.path.join(GEN, "recomp_dispatch.c")
    lines = io.open(p, encoding="utf-8", errors="surrogateescape").read().split("\n")
    n = 0
    for a in sorted(addrs, reverse=True):
        if a in existing:
            continue
        for i, l in enumerate(lines):
            m = ENTRY.match(l.rstrip("\r"))
            if m and int(m.group(1), 16) > a:
                lines.insert(i, "    { 0x%08Xu, (recomp_func_t)sub_%08X },"
                             % (a, a))
                n += 1
                break
    io.open(p, "w", encoding="utf-8", errors="surrogateescape",
            newline="").write("\n".join(lines))
    print("  spliced %d bodies, %d dispatch entries" % (len(names), n))
    instrument_new_labels()
    # The lifter still emits x87 memory arithmetic in the collapsed register
    # form; fixfpmem.py restores it from the XBE bytes. Recovered bodies used
    # to miss that pass entirely.
    r = subprocess.run([sys.executable, os.path.join(HERE, "fixfpmem.py")],
                       capture_output=True, text=True)
    tail = (r.stdout or "").strip().split("\n")[-1:]
    print("  fixfpmem: %s" % (tail[0] if tail else "no output"))
    return addrs


def instrument_new_labels():
    """Put RECOMP_LOC() on the labels a splice just added.

    Without this a recovered function is invisible to runtime probes, which is
    exactly when its labels matter most.  instrument_labels.py is idempotent,
    so this is cheap and safe to call after every splice.
    """
    rc, out = run([sys.executable,
                   os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "instrument_labels.py")],
                  cwd=ROOT, timeout=300)
    tail = [l for l in out.splitlines() if l.strip().startswith("total")]
    if rc == 0 and tail:
        added = tail[-1].rsplit(":", 1)[-1].strip()
        if added != "0":
            print("  instrumented %s new label(s) for runtime probes" % added)
    elif rc != 0:
        print("  WARNING: label instrumentation failed -- runtime probes will "
              "miss the new bodies:\n%s" % out[-400:])


def build():
    rc, out = run(["mingw32-make", "-j8"], cwd=BUILD, timeout=1800)
    undef = sorted(set(re.findall(r"undefined reference to `(sub_[0-9A-F]{8})'",
                                  out)))
    multi = len(re.findall(r"multiple definition", out))
    # Linker detail first: "ld returned 1" alone hid a duplicate runtime
    # symbol that failed every attempt of a bisection in part 179.
    errs = ([l for l in out.split("\n")
             if "multiple definition" in l or "undefined reference" in l]
            + [l for l in out.split("\n") if " error:" in l])
    return undef, multi, errs


def measure(runs, seconds, env_extra=None, logdir=None):
    """exit / crash / draws / textcorrupt -- all three gates plus the band.

    `env_extra` exists so the gate measures the configuration the tree is
    actually shipped in. Without it every batch was scored against a run with
    two known-defective addresses live, so a good batch reported a crash that
    had nothing to do with it.
    """
    env = dict(os.environ)
    env.setdefault("XBOX_INPUT_HOST", "0")   # a player at the pad must not steer the measurement
    env.update(env_extra or {})
    res = []
    for i in range(runs):
        __import__("stale").kill_test_instances()
        try:
            p = subprocess.run(["timeout", str(seconds), EXE],
                               capture_output=True, text=True, env=env,
                               errors="replace", timeout=seconds + 60)
            log = (p.stdout or "") + (p.stderr or "")
            code = p.returncode
        except subprocess.TimeoutExpired:
            log, code = "", -1
        if logdir:
            # Kept so a failed gate can be read afterwards; the intermittent
            # draws=0 boot reverted a good batch in part 180 and left nothing
            # to diagnose it from.
            with io.open(os.path.join(logdir, "gate_run%d.log" % (i + 1)), "w",
                         encoding="utf-8", errors="replace") as f:
                f.write(log)
        draws = re.findall(r"draws=(\d+)", log)
        res.append({"exit": code,
                    "crash": log.count("CRASH"),
                    "textcorrupt": log.count("[TEXT] .text"),
                    "draws": int(draws[-1]) if draws else 0})
    return res


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addrs", nargs="*", help="hex addresses to recover")
    ap.add_argument("--from-miss", help="read ICALL-MISS targets from a log")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--seconds", type=int, default=70)
    # Baseline is ~8,150 draws per 55 s run while the title still stalls in the
    # intro; the old 50,000 default predates that measurement and silently
    # reverted clean batches.  Set a little under baseline so a real
    # regression still trips it.
    # Re-baselined in part 145 alongside xbrun.py: the healthy band moved to
    # ~7,300-7,700 once the title advanced past the stalled loading screen.
    ap.add_argument("--min-draws", type=int, default=6000)
    ap.add_argument("--ignore-findings", action="store_true",
                    help="attempt targets recorded as traps in findings.py anyway")
    ap.add_argument("--keep-going", action="store_true",
                    help="splice even if the measurement gate fails")
    ap.add_argument("--env", action="append", default=[],
                    metavar="K=V", help="environment for the gate runs")
    ap.add_argument("--note", default="")
    ap.add_argument("--tag", default=None)
    ap.add_argument("--no-menu-check", action="store_true",
                    help="skip the frontend walk (menucheck.py) after the boot gate")
    args = ap.parse_args()

    addrs = [int(a, 16) for a in args.addrs]
    if args.from_miss:
        log = io.open(args.from_miss, encoding="utf-8", errors="replace").read()
        for m in sorted(set(re.findall(r"unresolved target 0x([0-9A-F]{8})",
                                       log))):
            v = int(m, 16)
            if 0x11000 <= v < 0x166F80 and v not in addrs:
                addrs.append(v)
    if not addrs:
        ap.error("no addresses (give them, or --from-miss)")

    # Some addresses are known to make the build worse when enabled -- see
    # findings.py.  They surface as ICALL misses run after run, so --from-miss
    # keeps offering them; without this check the same trap gets re-attempted,
    # measured, and reverted every few parts.
    try:
        import findings
        blocked = findings.blocking_set()
    except Exception:
        blocked = set()
    trapped = [a for a in addrs if ("0x%08X" % a) in blocked]
    if trapped:
        for a in trapped:
            print("  SKIP 0x%08X -- recorded in findings.py as a known trap" % a)
        print("  (override with --ignore-findings once the body is verified)")
        if not args.ignore_findings:
            addrs = [a for a in addrs if a not in trapped]
        if not addrs:
            sys.exit("every target is a recorded trap; nothing to do")

    tag = args.tag or time.strftime("batch-%Y%m%d-%H%M%S")
    print("batch %s: %d target(s)" % (tag, len(addrs)))
    saved = backup(tag)
    print("  backup -> %s" % saved)

    work = os.path.join(BACKUPS, tag, "work")
    body = seed_and_lift(work, addrs)
    got = set(BODY.findall(body))
    print("  lifted %d of %d" % (len(got), len(addrs)))
    missed = [a for a in addrs if ("sub_%08X" % a) not in got]
    if missed:
        print("  could not lift: %s"
              % " ".join("0x%08X" % a for a in missed))
    if not got:
        sys.exit("nothing lifted; nothing to do")

    splice(body, args.note or ("Recovery batch %s." % tag))

    def revert():
        # Restoring the sources alone left the executable built from the
        # batch: after a part-180 revert the next walk still ran the reverted
        # functions. Rebuild so the binary matches the tree again.
        restore(saved)
        build()

    for rnd in range(1, 10):
        undef, multi, errs = build()
        if errs and not undef:
            print("  compile errors:\n    " + "\n    ".join(errs[:5]))
            revert()
            sys.exit("reverted: compile error")
        if not undef:
            print("  link clean (multiple-definition=%d)" % multi)
            break
        print("  closure round %d: %s" % (rnd, " ".join(undef)))
        extra = [int(n[4:], 16) for n in undef]
        body = seed_and_lift(work, sorted(set(addrs) | set(extra)))
        found = split_bodies(body)
        keep = "\n\n".join(found[n] for n in undef if n in found)
        if not keep:
            print("  closure stalled; reverting")
            revert()
            sys.exit("reverted: could not lift %s" % " ".join(undef))
        splice(keep, "Closure for %s." % tag)
    else:
        revert()
        sys.exit("reverted: closure did not converge")

    res = measure(args.runs, args.seconds,
                  dict(kv.split('=', 1) for kv in args.env if '=' in kv), logdir=saved)
    ok = all(r["exit"] == 124 and r["crash"] == 0 and r["textcorrupt"] == 0
             and r["draws"] >= args.min_draws for r in res)
    for i, r in enumerate(res, 1):
        print("  run%d exit=%-4s crash=%d draws=%-8d textcorrupt=%d"
              % (i, r["exit"], r["crash"], r["draws"], r["textcorrupt"]))
    if ok and not args.no_menu_check:
        # The boot gate never presses a button, so it cannot see the
        # frontend. menucheck.py walks into the menus and fails when they
        # draw blank -- the part-180 regression the gate passed 3/3.
        r = subprocess.run([sys.executable, os.path.join(HERE, "menucheck.py")],
                           capture_output=True, text=True)
        last = ((r.stdout or "").strip().splitlines() or [""])[-1]
        print("  " + last)
        ok = r.returncode == 0
    if ok:
        print("KEPT %s" % tag)
        return
    if args.keep_going:
        print("GATE FAILED but --keep-going: %s left in place" % tag)
        return
    revert()
    print("REVERTED %s -- restore point %s" % (tag, saved))
    sys.exit(2)


if __name__ == "__main__":
    main()
