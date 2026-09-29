#!/usr/bin/env python3
"""Sample the title's memory while it runs, and line it up with its own log.

WHY THIS EXISTS
---------------
Task Manager showed the process jump from ~20 MB to ~180-200 MB a few seconds
after the splash screen goes black. That is a real signal and a precise one --
the whole guest mapping is 140 MB, so a jump of that size means something is
touching essentially all of guest memory at once, which is the signature of a
fill or copy with a corrupted length rather than of ordinary allocation.

But "a few seconds after" is not a place in the code. This samples the working
set every 250 ms alongside timestamped stderr, so the jump can be read against
the last thing the title logged before it -- which is what turns it into an
address to look at.

    memwatch.py                              # 60 s, default env
    memwatch.py --seconds 30 --interval 0.1
    memwatch.py --env XBOX_DISPATCH_DENY=000151F0,00179411
    memwatch.py --jump 20                    # report deltas over 20 MB

Working set is what Task Manager's "Memory" column shows: pages actually
resident. Private bytes (also reported) is committed private memory, which does
not fall back when pages are trimmed, so a rise there is allocation and a rise
only in working set is *touching* what was already reserved. The difference
matters here: the guest arena is reserved up front, so a 160 MB working-set
rise with flat private bytes means a write sweep, not a leak.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GAME = os.path.join(ROOT, "ssx_recomp", "build", "SSX Tricky.exe")


class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD),
                ("PageFaultCount", wt.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
                ("PrivateUsage", ctypes.c_size_t)]


PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010


def safe(text):
    """Console-safe: this console is cp1252 and the title logs UTF-8."""
    enc = sys.stdout.encoding or "utf-8"
    return text.encode(enc, "replace").decode(enc, "replace")


class MEMORY_BASIC_INFORMATION64(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_ulonglong),
                ("AllocationBase", ctypes.c_ulonglong),
                ("AllocationProtect", wt.DWORD),
                ("__alignment1", wt.DWORD),
                ("RegionSize", ctypes.c_ulonglong),
                ("State", wt.DWORD),
                ("Protect", wt.DWORD),
                ("Type", wt.DWORD),
                ("__alignment2", wt.DWORD)]


MEM_COMMIT, MEM_RESERVE, MEM_FREE = 0x1000, 0x2000, 0x10000
MEM_PRIVATE, MEM_MAPPED, MEM_IMAGE = 0x20000, 0x40000, 0x1000000
TYPE_NAME = {MEM_PRIVATE: "private", MEM_MAPPED: "mapped", MEM_IMAGE: "image"}


def regions(handle):
    """Committed regions, keyed by allocation base: (type, total bytes, count).

    Working set alone cannot distinguish "touching pages that were already
    reserved" from "reserving more"; private bytes cannot see file-backed
    views at all. Walking the address space answers both, and says which of
    the three kinds is growing.
    """
    out = {}
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION64()
    while addr < (1 << 47):
        n = ctypes.windll.kernel32.VirtualQueryEx(
            handle, ctypes.c_void_p(addr), ctypes.byref(mbi),
            ctypes.sizeof(mbi))
        if not n:
            break
        if mbi.State == MEM_COMMIT:
            k = (TYPE_NAME.get(mbi.Type, hex(mbi.Type)), mbi.AllocationBase)
            t, c = out.get(k, (0, 0))
            out[k] = (t + mbi.RegionSize, c + 1)
        nxt = mbi.BaseAddress + mbi.RegionSize
        if nxt <= addr:
            break
        addr = nxt
    return out


def sample(handle):
    c = PROCESS_MEMORY_COUNTERS_EX()
    c.cb = ctypes.sizeof(c)
    ok = ctypes.windll.psapi.GetProcessMemoryInfo(
        handle, ctypes.byref(c), c.cb)
    if not ok:
        return None
    return c.WorkingSetSize, c.PrivateUsage, c.PageFaultCount


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--interval", type=float, default=0.25)
    ap.add_argument("--jump", type=float, default=8.0,
                    help="report a sample when working set rises this many MB")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    ap.add_argument("--regions", action="store_true",
                    help="also walk the address space and report which "
                         "allocation grew (private / mapped / image)")
    ap.add_argument("--log", help="write the merged timeline here")
    a = ap.parse_args()

    if not os.path.exists(GAME):
        sys.exit("no game binary at %s -- build first" % GAME)

    env = dict(os.environ)
    env.update(dict(kv.split("=", 1) for kv in a.env if "=" in kv))

    __import__("stale").kill_test_instances()
    p = subprocess.Popen([GAME], stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE, env=env,
                         cwd=os.path.dirname(GAME))

    t0 = time.time()
    lines = []          # (t, kind, text)
    lock = threading.Lock()

    def reader():
        for raw in p.stderr:
            t = time.time() - t0
            with lock:
                lines.append((t, "log", raw.decode("utf-8", "replace").rstrip()))
    th = threading.Thread(target=reader, daemon=True)
    th.start()

    h = ctypes.windll.kernel32.OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, p.pid)
    if not h:
        sys.exit("could not open the process for querying")

    prev_ws = 0
    peak = 0
    reg_t = -99.0
    first_regions = None
    last_regions = None
    while time.time() - t0 < a.seconds and p.poll() is None:
        s = sample(h)
        if s:
            ws, pv, pf = s
            t = time.time() - t0
            wsm, pvm = ws / 1048576.0, pv / 1048576.0
            peak = max(peak, wsm)
            if a.regions and (first_regions is None or (t - reg_t) > 2.0):
                last_regions = regions(h)
                reg_t = t
                if first_regions is None:
                    first_regions = last_regions
            if wsm - prev_ws >= a.jump:
                with lock:
                    lines.append((t, "mem",
                                  "working set %7.1f MB (+%.1f)   private %7.1f MB"
                                  " faults %d" % (wsm, wsm - prev_ws, pvm, pf)))
                prev_ws = wsm
        time.sleep(a.interval)

    try:
        p.kill()
    except OSError:
        pass
    th.join(timeout=2)

    with lock:
        lines.sort(key=lambda e: e[0])
        out = []
        for t, kind, text in lines:
            if kind == "mem":
                out.append("%7.2fs  >>>>  %s" % (t, text))
            else:
                out.append("%7.2fs        %s" % (t, text))
    text = "\n".join(out)
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            f.write(text + "\n")
        print("wrote %s (%d lines)" % (a.log, len(out)))
    print("peak working set %.1f MB" % peak)

    if first_regions and last_regions:
        print()
        print("address-space growth (committed bytes, by allocation base):")
        keys = set(first_regions) | set(last_regions)
        rows = []
        for k in keys:
            a0 = first_regions.get(k, (0, 0))[0]
            a1, n1 = last_regions.get(k, (0, 0))
            if a1 - a0 > 1048576 or (a0 == 0 and a1 > 4 * 1048576):
                rows.append((a1 - a0, k, a0, a1, n1))
        for delta, (kind, base), a0, a1, n1 in sorted(rows, reverse=True)[:12]:
            print("  %-8s base 0x%012X  %8.1f -> %8.1f MB  (+%.1f, %d regions)"
                  % (kind, base, a0 / 1048576.0, a1 / 1048576.0,
                     delta / 1048576.0, n1))
        if not rows:
            print("  nothing grew by more than 1 MB -- the working-set rise is "
                  "pages being touched inside allocations that already existed")
    # Always echo the memory samples with a little context around each.
    for i, (t, kind, txt) in enumerate(lines):
        if kind != "mem":
            continue
        print("\n%7.2fs  >>>> %s" % (t, txt))
        for j in range(max(0, i - 3), i):
            if lines[j][1] == "log":
                # The title's log is UTF-8 and this console is cp1252, so a
                # single arrow in a heap line used to abort the whole report.
                print("        last log: " + safe(lines[j][2][:150]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
