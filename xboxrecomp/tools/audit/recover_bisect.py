#!/usr/bin/env python3
"""Recover a list of targets, keeping every subset that gates clean.

recover_batch.py is all-or-nothing: one bad function in a batch of 130 reverts
the other 129 (part 178, switch-case batch 2). This drives it divide-and-
conquer: try the whole list; if the gate fails, split in two and recurse, so
the clean parts are kept and the offenders are isolated down to single
addresses, which are reported (and recorded in the findings ledger as
`noted`, never as traps -- a function that breaks the run when recovered is a
lead, not a verdict).

    recover_bisect.py [--runs 3] [--seconds 45] [--note TEXT] 0x... 0x...
    recover_bisect.py --from FILE          # one address per line
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def attempt(addrs, args, depth):
    cmd = [sys.executable, os.path.join(HERE, "recover_batch.py"),
           "--runs", str(args.runs), "--seconds", str(args.seconds),
           "--note", "%s (bisect depth %d, %d targets)" % (args.note, depth, len(addrs))]
    cmd += ["0x%08X" % a for a in addrs]
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    out = (r.stdout or "") + (r.stderr or "")
    kept = "KEPT batch" in out
    runs = [l.strip() for l in out.splitlines() if l.strip().startswith("run")]
    print("%s%s %d targets: %s  [%s]" % ("  " * depth, "KEPT" if kept else "FAIL",
          len(addrs), " ".join("0x%08X" % a for a in addrs[:4]) + (" ..." if len(addrs) > 4 else ""),
          "; ".join(runs)), flush=True)
    if not kept and "nothing lifted" in out:
        return []
    return [] if kept else None


def solve(addrs, args, depth=0):
    if not addrs:
        return []
    res = attempt(addrs, args, depth)
    if res is not None:
        return res
    if len(addrs) == 1:
        return addrs
    mid = len(addrs) // 2
    return solve(addrs[:mid], args, depth + 1) + solve(addrs[mid:], args, depth + 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("addrs", nargs="*")
    ap.add_argument("--from", dest="src")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--seconds", type=int, default=45)
    ap.add_argument("--note", default="recover_bisect")
    args = ap.parse_args()
    addrs = [int(a, 16) for a in args.addrs]
    if args.src:
        addrs += [int(l.strip(), 16) for l in open(args.src) if l.strip()]
    bad = solve(addrs, args)
    print("offenders: %s" % (" ".join("0x%08X" % a for a in bad) or "none"))


if __name__ == "__main__":
    main()
