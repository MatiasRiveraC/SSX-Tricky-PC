"""Client for the in-process live diagnostics server (src/kernel/xbox_diag.c).

WHY THIS EXISTS
---------------
The server has been in the tree for a while and answers most of the questions
that were previously asked by editing a generated .c file, rebuilding for
minutes, running, reading, and editing the probe back out -- a loop that has
twice damaged real code on the way out.  It had no client script, so it went
unused.  This is that client.

    xbdiag.py info                          # one command against a running game
    xbdiag.py d32 0x001F8A88 8              # dump 8 dwords
    xbdiag.py watch 0x001F8A88              # report writes to that page
    xbdiag.py --launch --watch 0x001F8A88 --seconds 40
                                            # start the game, arm the watch,
                                            # and capture the writer

THE WATCH IS THE POINT
----------------------
`watch <va>` protects the containing guest page and reports every write through
the process VEH, naming the guest location doing it.  Twice now the question
"what writes this field?" has been answered instead by scanning the whole .text
for the address displacement -- which finds only absolute references and misses
every write made through a register-held pointer.  The watch finds all of them.

Requires XBOX_DIAG_PORT to be set for the game process; --launch does that.
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GAME = os.path.join(ROOT, "ssx_recomp", "build", "SSX Tricky.exe")
DEFAULT_PORT = 4501


def send(port, line, timeout=5.0, retries=1):
    """Send one command; return the reply text (without the trailing OK/ERR)."""
    last = None
    for _ in range(retries + 1):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        except OSError as e:
            last = e
            time.sleep(0.4)
            continue
        try:
            s.settimeout(timeout)
            # The server greets on connect with a banner that itself ends in
            # "OK", so it must be consumed before the command is sent -- reading
            # straight through would return the greeting as the reply.
            greeting = b""
            while b"\n" not in greeting or not greeting.decode(
                    "utf-8", "replace").rstrip().endswith(("OK", "commands")):
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                greeting += chunk
                if greeting.decode("utf-8", "replace").rstrip().split("\n")[-1] == "OK":
                    break
            s.sendall((line + "\n").encode())
            buf = b""
            while True:
                try:
                    chunk = s.recv(4096)
                except socket.timeout:
                    break
                if not chunk:
                    break
                buf += chunk
                tail = buf.decode("utf-8", "replace").rstrip().rsplit("\n", 1)[-1]
                if tail == "OK" or tail.startswith("ERR"):
                    break
            return buf.decode("utf-8", "replace")
        finally:
            s.close()
    raise SystemExit("could not reach the diag server on port %d (%s).\n"
                     "Is the game running with XBOX_DIAG_PORT=%d set?"
                     % (port, last, port))


def launch(port, seconds, env_extra):
    env = dict(os.environ)
    env["XBOX_DIAG_PORT"] = str(port)
    env.update(env_extra)
    __import__("stale").kill_test_instances()
    p = subprocess.Popen([GAME], stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE, env=env,
                         cwd=os.path.dirname(GAME))
    out = []

    def drain():
        for raw in p.stderr:
            out.append(raw.decode("utf-8", "replace"))
    threading.Thread(target=drain, daemon=True).start()
    # timeout(1) is not on a native Windows Python's PATH, so hold the deadline here
    threading.Timer(seconds, p.kill).start()
    return p, out


WRITE_RE = re.compile(r"\[WATCH\][^\n]*", re.I)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="*", help="command to send, e.g. d32 0x1F8A88 8")
    ap.add_argument("-p", "--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--launch", action="store_true",
                    help="start the game with the diag port set, then act")
    ap.add_argument("--seconds", type=int, default=40, help="run length under --launch")
    ap.add_argument("--watch", action="append", default=[], metavar="VA",
                    help="arm a write-watch on this guest VA, repeatable")
    ap.add_argument("--arm-after", type=float, default=6.0,
                    help="seconds to wait before arming, so boot allocation settles")
    ap.add_argument("--env", action="append", default=[], metavar="VAR=VAL")
    args = ap.parse_args()

    env_extra = dict(kv.split("=", 1) for kv in args.env if "=" in kv)
    proc, out = (None, None)
    if args.launch:
        proc, out = launch(args.port, args.seconds, env_extra)
        time.sleep(args.arm_after)

    try:
        for va in args.watch:
            print(send(args.port, "watch " + va, retries=6).rstrip())
        if args.command:
            print(send(args.port, " ".join(args.command), retries=6).rstrip())
        if proc:
            proc.wait()
            text = "".join(out)
            hits = WRITE_RE.findall(text)
            print("-" * 72)
            if hits:
                print("%d watch report(s):" % len(hits))
                seen = {}
                for h in hits:
                    seen[h] = seen.get(h, 0) + 1
                for h, n in sorted(seen.items(), key=lambda kv: -kv[1])[:20]:
                    print("  %4dx %s" % (n, h.strip()))
            else:
                print("no writes reported to the watched page(s)")
            for line in text.splitlines():
                if line.startswith("CRASH"):
                    print(line)
    finally:
        if proc and proc.poll() is None:
            proc.kill()


if __name__ == "__main__":
    main()
