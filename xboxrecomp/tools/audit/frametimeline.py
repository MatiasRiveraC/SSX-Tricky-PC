#!/usr/bin/env python3
"""frametimeline.py LOG [FROM TO] -- what happened around each frame hitch.

Merges the timestamped diagnostic lines of one run into a single timeline
(ms, relative to each hitch) and prints the 80 ms before and 20 ms after the
chosen hitches -- by default the 40th- and 39th-from-last, safely inside a
race. Run the game with:

    XBOX_FPS_LOG=1     [STALL]  present intervals over 40 ms (with WAIT_LOG)
    XBOX_WAIT_LOG=2    [WAIT]   single-object waits over N ms (1 = 30 ms)
    XBOX_TIMER_LOG=2   [TFIRE]  every one-shot kernel timer fire
    XBOX_FLIP_LOG=1    [FLIP]   render-surface switches (the frame boundary)
                       [PRES]   how long IDXGISwapChain::Present blocked
                       [KICK]   push-buffer kicks + draws pending
                       [BATCH]  every push-buffer batch, wraps included

This is how the part-183 hitches were found: the game thread woke every
16.6 ms as it should, but two frames in a row showed no surface switch --
the ring-wrap tail holding one had been dropped (nv2a_live_pb.c).
"""
import re, sys

PATTERNS = [
    (r"\[TFIRE\] t=([\d.]+) timer (\S+) asked (\d+) after ([\d.]+)",
     lambda m: "fire %s asked %s after %s" % (m.group(2)[-4:], m.group(3), m.group(4))),
    (r"\[STALL\] t=[\d.]+\.\.([\d.]+) ([\d.]+) ms", None),
    (r"\[FLIP\] t=([\d.]+) (\S+) -> (\S+) draws (\d+)",
     lambda m: "    flip %s->%s draws %s" % (m.group(2), m.group(3), m.group(4))),
    (r"\[KICK\] t=([\d.]+) put (\S+) pending draws (\d+)",
     lambda m: "      kick put %s pending %s" % (m.group(2), m.group(3))),
    (r"\[BATCH\] t=([\d.]+) (.*)", lambda m: "      batch %s" % m.group(2)),
    (r"\[PRES\] t=([\d.]+) present ([\d.]+) ms", lambda m: "    Present %s ms" % m.group(2)),
]


def main():
    log = sys.argv[1]
    a, b = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (-40, -38)
    ev = []
    for l in open(log, errors="replace"):
        m = re.match(r"\[WAIT\] t=([\d.]+) (\S+) obj=(\S+) ([\d.]+) ms caller=(\S+) thread (\d+)", l)
        if m:
            t, d = float(m.group(1)), float(m.group(4))
            ev.append((t, "  wait> %s thr %s" % (m.group(3), m.group(6))))
            ev.append((t + d, "  wait< %s %.1f ms" % (m.group(3), d)))
            continue
        for pat, fmt in PATTERNS:
            m = re.match(pat, l)
            if not m:
                continue
            if fmt is None:
                ev.append((float(m.group(1)) + 0.001, "    ** STALL %s ms" % m.group(2)))
            else:
                ev.append((float(m.group(1)), fmt(m)))
            break
    ev.sort()
    stalls = [e[0] for e in ev if "STALL" in e[1]]
    print("%d hitches" % len(stalls))
    for ts in stalls[a:b]:
        print("-----")
        for t, s in ev:
            if ts - 80 <= t <= ts + 20:
                print("%9.1f  %s" % (t - ts, s))


if __name__ == "__main__":
    main()
