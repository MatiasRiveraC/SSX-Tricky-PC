"""Run the recompiled title N times, apply the gate, and record every result.

WHY THIS EXISTS
---------------
Every part of this port ends the same way: run the build a few times, grep the
log for exit code, crash count, .text corruption and draw count, and eyeball
whether it got better or worse.  Doing that by hand has gone wrong twice --
once by reading a stale binary after a failed compile, and once by calling a
batch clean when a `multiple definition` error meant the build never relinked.

So this does the whole loop, and -- the part that matters -- **appends every
run to a persistent log**.  A regression is then attributable: the log says
what the numbers were before the change, not just that they look bad now.

    xbrun.py                          # 3 runs, print the gate
    xbrun.py -n 5 --label "icall fix" # 5 runs, recorded under that label
    xbrun.py --compare                # this label vs the last different one
    xbrun.py --history 20             # last 20 recorded measurements

THE GATE
--------
A run is clean when all four hold together.  Three of them have individually
been true while the build was badly broken, which is why they are checked as a
set:

  exit == 124     the 60 s timeout fired, i.e. it did not die on its own.
                  127 masks a segfault; 139 is a segfault.
  crash == 0      no CRASH line from the exception filter in main.c.
  textcorrupt==0  nothing wrote over guest .text.  A build can link cleanly,
                  exit 124 and still be corrupting its own code.
  draws >= floor  it is still submitting GPU work.  Zero draws with everything
                  else green is the signature of a dispatch-table regression.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GAME = os.path.join(ROOT, "ssx_recomp", "build", "SSX Tricky.exe")
LOGDIR = os.path.join(ROOT, "RE_NOTES")
LEDGER = os.path.join(LOGDIR, "measurements.jsonl")

RE_DRAWS = re.compile(r"draws=(\d+)")
RE_CLEARS = re.compile(r"clears=(\d+)")
RE_TEXT = re.compile(r"textcorrupt|\[TEXT\] \.text", re.I)
RE_CRASH = re.compile(r"^CRASH", re.M)
RE_MISS = re.compile(r"unresolved target (0x[0-9A-Fa-f]{8})")
RE_ITEM = re.compile(r"\[ITEM\] done")



def check_fresh(auto_build):
    """Refuse to measure a binary older than the sources it came from.

    Measuring a stale executable produces a perfectly clean, perfectly
    meaningless result: probes that were added print nothing, and a fix that
    was made looks like it did not work.  It has cost this project a full
    investigation more than once -- most recently when `make` was simply not
    on the PATH and its failure was hidden behind a grep.  So the check is
    here, in the one place every measurement passes through, rather than in
    the discipline of whoever is running it.
    """
    if not os.path.exists(GAME):
        sys.exit("no game binary at %s -- build first" % GAME)
    exe = os.path.getmtime(GAME)
    src_root = os.path.join(ROOT, "ssx_recomp", "src")
    newest, newest_t = None, 0.0
    for base, _dirs, files in os.walk(src_root):
        if os.path.basename(base).startswith("gen.bak"):
            continue
        for n in files:
            if not n.endswith((".c", ".h")):
                continue
            f = os.path.join(base, n)
            t = os.path.getmtime(f)
            if t > newest_t:
                newest, newest_t = f, t
    if newest_t <= exe:
        return
    rel = os.path.relpath(newest, ROOT)
    if not auto_build:
        sys.exit("STALE BINARY: %s is newer than the executable. "
                 "Rebuild first, or pass --build to do it here." % rel)
    print("stale: %s is newer than the binary -- rebuilding" % rel)
    rc = subprocess.run(["mingw32-make", "-j8"],
                        cwd=os.path.join(ROOT, "ssx_recomp", "build"),
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                        text=True, errors="replace")
    bad = [l for l in rc.stdout.splitlines()
           if " error" in l.lower() or "Error " in l]
    if rc.returncode != 0 or bad:
        print(chr(10).join(bad[-15:]))
        sys.exit("build failed -- not measuring")
    warn = [l for l in rc.stdout.splitlines()
            if "implicit declaration" in l or "int-conversion" in l]
    for l in warn[:10]:
        print("  warning: " + l.strip())
    print("rebuilt")


def one_run(seconds, env_extra):
    """Run once under a timeout; return the parsed result dict.

    The timeout is enforced here rather than by timeout(1), which is not on
    the PATH of a native Windows Python.  Surviving to the deadline is the
    healthy outcome, so it is reported as 124 to match what timeout(1) would
    have returned and to keep older recorded measurements comparable.
    """
    env = dict(os.environ)
    env.setdefault("XBOX_INPUT_HOST", "0")   # a player at the pad must not steer the measurement
    env.update(env_extra or {})
    t0 = time.time()
    if not os.path.exists(GAME):
        sys.exit("no game binary at %s -- build first" % GAME)
    p = subprocess.Popen([GAME], stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE, env=env,
                         cwd=os.path.dirname(GAME))
    try:
        log = p.communicate(timeout=seconds)[1].decode("utf-8", "replace")
        rc = p.returncode
    except subprocess.TimeoutExpired:
        p.kill()
        log = (p.communicate()[1] or b"").decode("utf-8", "replace")
        rc = 124
    draws = [int(m) for m in RE_DRAWS.findall(log)]
    clears = [int(m) for m in RE_CLEARS.findall(log)]
    return {
        "exit": rc,
        "crash": len(RE_CRASH.findall(log)),
        "textcorrupt": len(RE_TEXT.findall(log)),
        "draws": max(draws) if draws else 0,
        "clears": max(clears) if clears else 0,
        "items": len(RE_ITEM.findall(log)),
        "misses": sorted(set(RE_MISS.findall(log))),
        "wall": round(time.time() - t0, 1),
        "log": log,
    }


def gate(r, floor):
    return (r["exit"] == 124 and r["crash"] == 0
            and r["textcorrupt"] == 0 and r["draws"] >= floor)


def record(label, runs, floor):
    os.makedirs(LOGDIR, exist_ok=True)
    entry = {
        "when": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "label": label,
        "floor": floor,
        "runs": [{k: v for k, v in r.items() if k != "log"} for r in runs],
        "clean": sum(1 for r in runs if gate(r, floor)),
        "total": len(runs),
        "draws_median": sorted(r["draws"] for r in runs)[len(runs) // 2],
        "items_max": max(r["items"] for r in runs),
    }
    with open(LEDGER, "a", encoding="utf-8") as f:
        f.write(json.dumps(entry) + "\n")
    return entry


def load_history():
    if not os.path.exists(LEDGER):
        return []
    out = []
    with open(LEDGER, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--runs", type=int, default=3)
    ap.add_argument("-s", "--seconds", type=int, default=55)
    # Re-baselined in part 145. The old 7,500 was set against a stalled
    # loading screen redrawn forever at high per-frame cost; once the title
    # advanced past it the healthy band moved to ~7,300-7,700, so the old
    # floor sat inside the noise and failed clean runs. A genuinely broken
    # run measures 0 or a few hundred, so 6,000 separates them with room.
    ap.add_argument("--min-draws", type=int, default=6000,
                    help="draw floor; baseline is ~8,150 while the intro still stalls")
    ap.add_argument("--build", action="store_true",
                    help="rebuild first if any source is newer than the binary")
    ap.add_argument("--label", default="(unlabelled)")
    ap.add_argument("--env", action="append", default=[],
                    help="VAR=VAL passed to the game, repeatable")
    ap.add_argument("--history", type=int, metavar="N",
                    help="print the last N recorded measurements and exit")
    ap.add_argument("--compare", action="store_true",
                    help="compare the last measurement against the previous different label")
    ap.add_argument("--keep-logs", metavar="DIR",
                    help="write each run's stderr to DIR")
    args = ap.parse_args()

    if args.history:
        for e in load_history()[-args.history:]:
            print("%s  %-28s %d/%d clean  draws~%-6d items=%d"
                  % (e["when"], e["label"][:28], e["clean"], e["total"],
                     e["draws_median"], e.get("items_max", 0)))
        return

    if args.compare:
        h = load_history()
        if len(h) < 2:
            sys.exit("need at least two recorded measurements")
        cur = h[-1]
        prev = next((e for e in reversed(h[:-1]) if e["label"] != cur["label"]), h[-2])
        print("current : %-28s %d/%d clean  draws~%d  items=%d"
              % (cur["label"][:28], cur["clean"], cur["total"],
                 cur["draws_median"], cur.get("items_max", 0)))
        print("previous: %-28s %d/%d clean  draws~%d  items=%d"
              % (prev["label"][:28], prev["clean"], prev["total"],
                 prev["draws_median"], prev.get("items_max", 0)))
        d = cur["draws_median"] - prev["draws_median"]
        di = cur.get("items_max", 0) - prev.get("items_max", 0)
        print("delta   : draws %+d, items %+d, clean %+d"
              % (d, di, cur["clean"] - prev["clean"]))
        if cur["clean"] < prev["clean"] or d < -1000:
            print("REGRESSION")
        return

    check_fresh(args.build)

    env_extra = dict(kv.split("=", 1) for kv in args.env if "=" in kv)
    runs = []
    for i in range(args.runs):
        __import__("stale").kill_test_instances()
        r = one_run(args.seconds, env_extra)
        runs.append(r)
        if args.keep_logs:
            os.makedirs(args.keep_logs, exist_ok=True)
            with open(os.path.join(args.keep_logs, "run%d.log" % (i + 1)),
                      "w", encoding="utf-8") as f:
                f.write(r["log"])
        print("run%d exit=%-4d crash=%d textcorrupt=%d draws=%-6d clears=%-4d items=%d %s"
              % (i + 1, r["exit"], r["crash"], r["textcorrupt"], r["draws"],
                 r["clears"], r["items"],
                 "OK" if gate(r, args.min_draws) else "FAIL"))

    e = record(args.label, runs, args.min_draws)
    print("-" * 72)
    print("%d/%d clean   draws median %d   items max %d   recorded as %r"
          % (e["clean"], e["total"], e["draws_median"], e["items_max"], args.label))
    allmiss = sorted(set(m for r in runs for m in r["misses"]))
    if allmiss:
        print("unresolved indirect targets: " + " ".join(allmiss))
    sys.exit(0 if e["clean"] == e["total"] else 1)


if __name__ == "__main__":
    main()
