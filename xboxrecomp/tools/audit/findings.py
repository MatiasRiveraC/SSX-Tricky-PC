"""A durable ledger of what is known about specific guest addresses.

WHY THIS EXISTS
---------------
Knowledge about individual addresses keeps being re-derived and then lost in
prose.  The clearest case: `0x000151F0` is registered in the dispatch table but
sits past its sorted region, so the binary search never reaches it and every
call is an ICALL miss.  That reads like a one-line fix, and it is not -- making
it reachable collapses drawing to zero.  Its unreachability was protecting the
build from a bad translation.

Written down in a notes file, that costs a re-discovery every time someone
(including me, later) sees the miss and reaches for the obvious fix.  Written
down here, tools can refuse it.

    findings.py list                       # everything known
    findings.py check 0x000151F0           # exit 1 if the address is a trap
    findings.py add 0x000151F0 trap "..." --evidence "..." --action "..."
    findings.py resolve 0x000151F0 "fixed in part 131 by re-lifting"

STATUSES
--------
  trap      making this reachable/registered makes the build worse.  Do not
            enable without verifying the body against the original bytes.
  bad-body  translated, reachable, and known to misbehave.
  hle       needs a host bridge rather than recovery (statically linked Xbox
            library code -- D3D/XPP/DSOUND sections).
  unliftable the seeder cannot reach it; needs hand transcription.
  noted     an observation with no action attached yet.
  resolved  previously a problem, now fixed.  Kept for the history.
"""
import argparse
import ssxpaths
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PATH = ssxpaths.FINDINGS

BLOCKING = ("trap", "bad-body")
STATUSES = ("trap", "bad-body", "hle", "unliftable", "noted", "resolved")


def load():
    if not os.path.exists(PATH):
        return {}
    with open(PATH, encoding="utf-8") as f:
        try:
            return json.load(f)
        except ValueError:
            return {}


def save(d):
    os.makedirs(os.path.dirname(PATH), exist_ok=True)
    with open(PATH, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=2, sort_keys=True)
        f.write("\n")


def norm(a):
    return "0x%08X" % int(a, 16 if a.lower().startswith("0x") else 16)


def cmd_list(args):
    d = load()
    if not d:
        print("(ledger empty)")
        return
    for k in sorted(d):
        e = d[k]
        if args.status and e["status"] != args.status:
            continue
        print("%s  %-10s %s" % (k, e["status"], e["summary"]))
        for field in ("evidence", "action"):
            if e.get(field):
                print("      %-9s %s" % (field + ":", e[field]))


def cmd_check(args):
    d = load()
    e = d.get(norm(args.addr))
    if not e:
        print("%s: not in the ledger" % norm(args.addr))
        return 0
    print("%s  %s  %s" % (norm(args.addr), e["status"], e["summary"]))
    if e.get("evidence"):
        print("  evidence: %s" % e["evidence"])
    if e.get("action"):
        print("  action:   %s" % e["action"])
    return 1 if e["status"] in BLOCKING else 0


def cmd_add(args):
    d = load()
    k = norm(args.addr)
    d[k] = {
        "status": args.status,
        "summary": args.summary,
        "evidence": args.evidence or "",
        "action": args.action or "",
        "recorded": time.strftime("%Y-%m-%d"),
        "part": args.part or "",
    }
    save(d)
    print("recorded %s as %s" % (k, args.status))


def cmd_resolve(args):
    d = load()
    k = norm(args.addr)
    if k not in d:
        sys.exit("%s is not in the ledger" % k)
    d[k]["status"] = "resolved"
    d[k]["action"] = args.note
    d[k]["resolved"] = time.strftime("%Y-%m-%d")
    save(d)
    print("%s marked resolved" % k)


def blocking_set():
    """Addresses that tools must not enable.  Import this from other passes."""
    return set(k for k, e in load().items() if e["status"] in BLOCKING)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("list"); p.add_argument("--status", choices=STATUSES)
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("check"); p.add_argument("addr")
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser("add")
    p.add_argument("addr"); p.add_argument("status", choices=STATUSES)
    p.add_argument("summary")
    p.add_argument("--evidence"); p.add_argument("--action"); p.add_argument("--part")
    p.set_defaults(fn=cmd_add)

    p = sub.add_parser("resolve"); p.add_argument("addr"); p.add_argument("note")
    p.set_defaults(fn=cmd_resolve)

    args = ap.parse_args()
    sys.exit(args.fn(args) or 0)


if __name__ == "__main__":
    main()
