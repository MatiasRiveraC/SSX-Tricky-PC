#!/usr/bin/env python3
"""Save, list and restore whole memory-layout configurations.

The guest memory layout is spread across two files and a dozen interacting
constants -- where the stack sits, how big it is, where the kernel data area
and TIB pool live, how much RAM is mapped, whether the contiguous allocator
reserves a pool or grows down from a ceiling. Changing any of them changes
where the title's 53 MB arena lands, and therefore whether the GPU can reach
the textures inside it.

Several of those configurations boot, allocate cleanly and still render
nothing, so "does it work" cannot be answered from the source alone -- it has
to be measured and remembered. This keeps a labelled copy of both files
together with what that configuration actually did on screen, so going back to
one is a single command instead of a reconstruction from memory. Two
configurations were rebuilt from memory during this session and one of them
came back subtly different; that is the mistake this prevents.

    layout_snap.py list
    layout_snap.py save  <label> [--note "what it does on screen"]
    layout_snap.py restore <label>
    layout_snap.py diff  <label>

Snapshots live in RE_NOTES/layout_snapshots/<label>/ so they sit with the rest
of the project's findings rather than in a scratch directory.
"""

import argparse
import ssxpaths
import difflib
import io
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
KERNEL = os.path.join(ROOT, "xboxrecomp", "src", "kernel")
STORE = os.path.join(ssxpaths.NOTES, "layout_snapshots")
FILES = ["xbox_memory_layout.h", "xbox_memory_layout.c"]
META = "snapshot.json"

# The constants worth showing in a listing, so `list` says what a
# configuration *is* without opening it.
KEYS = ("XBOX_TOTAL_RAM", "XBOX_STACK_BASE", "XBOX_STACK_SIZE",
        "XBOX_KERNEL_DATA_BASE", "XBOX_TIB_POOL_VA", "XBOX_CONTIG_POOL_SIZE")


def summarise(header_text, source_text):
    out = {}
    for line in header_text.split("\n") + source_text.split("\n"):
        s = line.strip()
        if not s.startswith("#define "):
            continue
        parts = s[8:].split(None, 1)
        if len(parts) == 2 and parts[0] in KEYS:
            out[parts[0]] = parts[1].split("/*")[0].strip()
    out["allocator"] = ("both-ends" if "g_contig_next = 0;" in source_text
                        else "reserved-pool")
    out["spill"] = "yes" if "g_heap_spill" in source_text else "no"
    return out


def read_current():
    return [io.open(os.path.join(KERNEL, f), encoding="utf-8",
                    errors="surrogateescape").read() for f in FILES]


def cmd_save(args):
    d = os.path.join(STORE, args.label)
    if os.path.isdir(d) and not args.force:
        sys.exit("snapshot %r already exists (use --force to overwrite)" % args.label)
    os.makedirs(d, exist_ok=True)
    texts = read_current()
    for f, t in zip(FILES, texts):
        io.open(os.path.join(d, f), "w", encoding="utf-8",
                errors="surrogateescape", newline="").write(t)
    meta = {"label": args.label, "saved": time.strftime("%Y-%m-%d %H:%M:%S"),
            "note": args.note or "", "constants": summarise(*texts)}
    io.open(os.path.join(d, META), "w", encoding="utf-8").write(
        json.dumps(meta, indent=2))
    print("saved %r" % args.label)
    for k, v in meta["constants"].items():
        print("    %-22s %s" % (k, v))


def cmd_list(_args):
    if not os.path.isdir(STORE):
        sys.exit("no snapshots yet")
    cur = summarise(*read_current())
    for label in sorted(os.listdir(STORE)):
        p = os.path.join(STORE, label, META)
        if not os.path.isfile(p):
            continue
        m = json.load(io.open(p, encoding="utf-8"))
        same = " <- matches working tree" if m["constants"] == cur else ""
        print("%-18s %s%s" % (label, m["saved"], same))
        if m["note"]:
            print("    %s" % m["note"])
        for k in KEYS + ("allocator", "spill"):
            if k in m["constants"]:
                print("      %-22s %s" % (k, m["constants"][k]))
        print("")


def cmd_restore(args):
    d = os.path.join(STORE, args.label)
    if not os.path.isdir(d):
        sys.exit("no snapshot %r" % args.label)
    for f in FILES:
        shutil.copyfile(os.path.join(d, f), os.path.join(KERNEL, f))
    print("restored %r -- rebuild before measuring" % args.label)


def cmd_diff(args):
    d = os.path.join(STORE, args.label)
    if not os.path.isdir(d):
        sys.exit("no snapshot %r" % args.label)
    n = 0
    for f, cur in zip(FILES, read_current()):
        old = io.open(os.path.join(d, f), encoding="utf-8",
                      errors="surrogateescape").read()
        for line in difflib.unified_diff(old.split("\n"), cur.split("\n"),
                                         "%s/%s" % (args.label, f),
                                         "working/%s" % f, lineterm="", n=1):
            print(line)
            n += 1
    if not n:
        print("identical to %r" % args.label)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("save"); s.add_argument("label")
    s.add_argument("--note", default=""); s.add_argument("--force", action="store_true")
    s.set_defaults(fn=cmd_save)
    s = sub.add_parser("list"); s.set_defaults(fn=cmd_list)
    s = sub.add_parser("restore"); s.add_argument("label"); s.set_defaults(fn=cmd_restore)
    s = sub.add_parser("diff"); s.add_argument("label"); s.set_defaults(fn=cmd_diff)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
