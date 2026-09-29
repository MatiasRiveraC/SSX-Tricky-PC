#!/usr/bin/env python3
"""Find translated functions that leave the guest stack unbalanced.

The disassembler splits one machine function into several translated ones
wherever a branch target looked like an entry point, so a single original
function becomes a chain:

    sub_00169EA0   push ecx,ebx,ebp,esi,edi   -> tail jmp sub_00169ED5
    sub_00169ED5                              -> tail jmp sub_00169EF3
                                              -> tail jmp sub_00169F46
    ...                                       -> pop edi,esi,ebp,ebx ; ret 8

Every node in that chain is a separate C function, and the pushes and the pops
end up in *different* ones. That is fine while the chain is intact. It is not
fine when a link is missing, stubbed, or ends without the pops: the caller then
returns with `esp` low by however much the chain pushed, and every later read of
a stack argument in that thread is off by that amount.

The damage is silent and delayed. A 32-byte leak of exactly this shape --
5 register pushes plus 2 arguments plus a return address -- was what corrupted
`esi` in the scene-view render loop three call levels above, long after the
function that caused it had returned.

This walks each chain from its entry and reports the net effect:

    stackbalance.py                 leaks only
    stackbalance.py --all           every chain
    stackbalance.py --from NAME     one chain, showing each node

A chain is reported when a path reaches a `return` having pushed more registers
than it popped. Argument pushes immediately before a call are not counted --
they belong to the callee's `ret N`.
"""

import argparse
import glob
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_GEN = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "ssx_recomp", "src", "recomp", "gen"))

FUNC = re.compile(r"^void (?P<name>[A-Za-z_][A-Za-z0-9_]*)\(void\)$")
PUSH = re.compile(r"^\s*PUSH32\(esp, (?P<what>[^)]*)\);\s*$")
POP = re.compile(r"^\s*POP32\(esp, (?P<what>[^)]*)\);\s*$")
CALL = re.compile(r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\(\); /\* call")
TAIL = re.compile(r"g_seh_ebp = ebp; (?P<name>[A-Za-z_][A-Za-z0-9_]*)\(\); return;")
RET = re.compile(r"^\s*esp \+= (?P<n>\d+); return;")
REGS = ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp")


def load(gen):
    bodies = {}
    for path in sorted(glob.glob(os.path.join(gen, "*.c"))):
        lines = io.open(path, encoding="utf-8",
                        errors="surrogateescape").read().split("\n")
        i = 0
        while i < len(lines):
            m = FUNC.match(lines[i])
            if not m:
                i += 1
                continue
            j = i
            while j < len(lines) and lines[j] != "}":
                j += 1
            bodies[m.group("name")] = (os.path.basename(path), lines[i:j])
            i = j
    return bodies


def analyse(body):
    """(net register pushes, tail-jump targets, has a plain return)."""
    net, tails, rets = 0, [], False
    for k, line in enumerate(body):
        m = PUSH.match(line)
        if m:
            # An argument push is one that a call consumes; a register save is
            # not immediately followed by a call. Look ahead a few lines.
            nxt = "\n".join(body[k + 1:k + 4])
            if CALL.search(nxt) or "RECOMP_ICALL_SAFE" in nxt:
                continue
            if m.group("what") in REGS:
                net += 1
            continue
        m = POP.match(line)
        if m:
            if m.group("what") in REGS:
                net -= 1
            continue
        m = TAIL.search(line)
        if m:
            tails.append(m.group("name"))
            continue
        if RET.match(line):
            rets = True
    return net, tails, rets


def walk(bodies, name, seen=None, depth=0, verbose=False, out=None):
    """Net register imbalance along the worst path from `name`."""
    if seen is None:
        seen = set()
    if name in seen or name not in bodies:
        return 0, (name not in bodies)
    seen = seen | {name}
    _f, body = bodies[name]
    net, tails, rets = analyse(body)
    if verbose:
        out.append("%s%-34s net=%+d tails=%s%s"
                   % ("  " * depth, name, net,
                      ",".join(tails) if tails else "-",
                      " ret" if rets else ""))
    worst, missing = 0, False
    if rets:
        worst = net
    for t in tails:
        sub, miss = walk(bodies, t, seen, depth + 1, verbose, out)
        missing = missing or miss
        if abs(net + sub) > abs(worst):
            worst = net + sub
    if not rets and not tails:
        worst = net
    return worst, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gen", default=DEFAULT_GEN)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--from", dest="start", default="")
    args = ap.parse_args()

    bodies = load(args.gen)
    if not bodies:
        sys.exit("no generated sources under %s" % args.gen)

    if args.start:
        out = []
        worst, missing = walk(bodies, args.start, verbose=True, out=out)
        print("\n".join(out))
        print("\nworst-path register imbalance: %+d dword(s)%s"
              % (worst, "  (chain has a missing link)" if missing else ""))
        return

    bad = []
    for name in sorted(bodies):
        worst, missing = walk(bodies, name)
        if worst != 0 or missing:
            bad.append((name, worst, missing))
    print("chains analysed: %d" % len(bodies))
    print("unbalanced or broken: %d" % len(bad))
    for name, worst, missing in bad[:40] if not args.all else bad:
        print("    %-40s %+d dword(s)%s"
              % (name, worst, "  missing link" if missing else ""))
    if not args.all and len(bad) > 40:
        print("    ... and %d more (--all)" % (len(bad) - 40))


if __name__ == "__main__":
    main()
