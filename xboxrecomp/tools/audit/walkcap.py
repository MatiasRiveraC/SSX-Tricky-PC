#!/usr/bin/env python3
"""walkcap.py OUTDIR SECONDS [GAME ARGS...] -- walk into a race and dump frames.

Starts the game with scripted controller presses (START to skip the intro,
then A through the default menu choices into a Garibaldi race) and writes
every EVERY-th presented frame, from frame FROM, as OUTDIR/fNNN_fFFFFF.bmp.
The player's own controller is ignored (XBOX_INPUT_HOST=0), so it is safe to
run while someone is playing.

    walkcap.py cap 90 --direct                   # frames 560.. every 40
    FROM=2700 EVERY=60 COUNT=12 walkcap.py race 90 --direct
    XBOX_FPS_LOG=1 walkcap.py fps 90 --direct    # any XBOX_* switch passes through

Environment: FROM (560), EVERY (40), COUNT (30), NOKILL=1 to leave other test
instances running (parallel runs). The log goes to OUTDIR/run.log.

Frame numbers drift between runs: the presses are timed in seconds, so how
far a given frame is into the race depends on load speed. Compare by content
(xemucompare.py) or by the race clock, not by frame number.
"""
import glob
import os
import subprocess
import sys

import ssxpaths
import stale


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, secs, args = os.path.abspath(sys.argv[1]), float(sys.argv[2]), sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(out, "*.bmp")):
        os.remove(f)
    env = dict(os.environ,
               XBOX_INPUT_HOST="0",
               XBOX_INPUT_AUTOPRESS=os.environ.get("PRESSES", "START@11,START@14,A@18+1.5x16"),
               XBOX_D3D_DUMP=os.path.join(out, "f"),
               XBOX_D3D_DUMP_FROM=os.environ.get("FROM", "560"),
               XBOX_D3D_DUMP_EVERY=os.environ.get("EVERY", "40"),
               XBOX_D3D_DUMP_MAX=os.environ.get("COUNT", "30"))
    if not os.environ.get("NOKILL"):
        stale.kill_test_instances()
    with open(os.path.join(out, "run.log"), "wb") as log:
        p = subprocess.Popen([ssxpaths.GAME_EXE] + args, cwd=ssxpaths.BUILD, env=env,
                             stdout=log, stderr=subprocess.STDOUT)
        try:
            p.wait(timeout=secs)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
    print(len(glob.glob(os.path.join(out, "*.bmp"))), "frames")


if __name__ == "__main__":
    main()
