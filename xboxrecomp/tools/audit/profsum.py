#!/usr/bin/env python3
"""profsum.py LOG [TOP] -- name the hot functions in an XBOX_PROFILE report.

The in-process sampling profiler (xboxrecomp/src/kernel/xbox_profile.c)
prints raw module offsets:

    XBOX_PROFILE=START,SECONDS[,viaN][,all][,stall]

      viaN   attribute time inside Windows DLLs to the Nth executable frame
             up the stack (RtlVirtualUnwind), e.g. via2 names the kernel
             bridge's guest caller instead of "ntdll.dll"
      all    report idle threads too
      stall  count only samples inside the present intervals over 40 ms that
             the renderer marks (needs XBOX_FPS_LOG=1): what runs during a
             hitch, not on average

This resolves each offset with addr2line (Release is built with -g) and
prints, per thread, the time by outermost function (inlined frames folded
into their caller) and by innermost function. Resolve a log against the
executable that wrote it: after a rebuild the offsets name other functions.
"""
import collections, os, re, subprocess, sys
import ssxpaths

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = ssxpaths.GAME_EXE
A2L = os.environ.get("ADDR2LINE", r"C:/Program Files/msys64/ucrt64/bin/addr2line.exe")
IMAGE_BASE = 0x140000000


def main():
    log, top = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 25
    threads, cur = [], None
    for l in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"\[PROFILE\] thread (\d+)\s+cpu ([\d.]+)%", l)
        if m:
            cur = {"tid": m.group(1), "cpu": m.group(2), "rows": []}
            threads.append(cur)
            continue
        m = re.search(r"\[PROFILE\]\s+([\d.]+)%\s+system via (exe\+\S+)", l)
        if m and cur is not None:
            cur["rows"].append((float(m.group(1)), "via:" + m.group(2)))
            continue
        m = re.search(r"\[PROFILE\]\s+([\d.]+)%\s+(\S+)", l)
        if m and cur is not None:
            cur["rows"].append((float(m.group(1)), m.group(2)))

    res = {}
    for a in sorted({r[1].replace("via:", "") for t in threads for r in t["rows"] if "exe+" in r[1]}):
        o = subprocess.run([A2L, "-f", "-i", "-e", EXE, "0x%X" % (IMAGE_BASE + int(a[4:], 16))],
                           capture_output=True, text=True).stdout.strip().split("\n")
        funcs = o[0::2]
        res[a] = (funcs[0], funcs[-1])          # innermost, outermost

    for t in threads:
        inner, outer = collections.Counter(), collections.Counter()
        for pct, s in t["rows"]:
            if s.startswith("via:") and s[4:] in res:
                k = "[sys<-" + res[s[4:]][0] + "]"
                inner[k] += pct
                outer[k] += pct
            elif s in res:
                inner[res[s][0]] += pct
                outer[res[s][1]] += pct
            else:
                inner["[" + s + "]"] += pct
                outer["[" + s + "]"] += pct
        print("thread %s cpu %s%%  (listed sites cover %.0f%%)"
              % (t["tid"], t["cpu"], sum(p for p, _ in t["rows"])))
        print("  by function:", ", ".join("%s %.1f" % kv for kv in outer.most_common(top)))
        print("  innermost:  ", ", ".join("%s %.1f" % kv for kv in inner.most_common(top)))


if __name__ == "__main__":
    main()
