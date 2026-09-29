#!/usr/bin/env python3
"""Give every generated function an `Original:` range header.

The post-passes that go back to the game's bytes -- fixfpmem.py (x87 memory
operands), xverify.py --function -- find a function's instruction range from
the doc comment the translator writes above it:

    /**
     * sub_00019040
     * Original: 0x00019040 - 0x000190BA (122 bytes, 39 insns)
     */

Bodies added by recover_batch.py were pasted without that comment, so every
one of them was silently skipped by those passes. EA's float-to-PCM stream
converter (sub_00019040) kept the collapsed `fadd` -- a register pop-add in
place of `fadd dword [0x1A9F3C]`, the magic constant that turns a float into
PCM bits -- and every sample came out saturated. See RE_NOTES part 178.

This copies the header from a fresh full translation (tools.recomp --all
--split N --gen-dir DIR) for every function that lacks one, matching by
address so renamed functions are covered.

    add_origin_headers.py FRESH_GEN [--gen GEN] [--file recomp_recovered.c] [--dry-run]
"""
import argparse, io, os, re, sys
import ssxpaths

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
GEN = ssxpaths.GEN
FUNC = re.compile(r"^void (\w+)\(void\)$")
ORIGIN = re.compile(r"^\s*\*\s*Original:\s*0x[0-9A-Fa-f]{8}\s*-\s*0x[0-9A-Fa-f]{8}")
SUB = re.compile(r"^sub_([0-9A-Fa-f]{8})$")


def fresh_headers(d):
    """address -> the Original: line, from a full translation."""
    out = {}
    for f in os.listdir(d):
        if not (f.startswith("recomp_") and f.endswith(".c")):
            continue
        lines = io.open(os.path.join(d, f), encoding="utf-8", errors="replace").read().split("\n")
        origin = None
        for l in lines:
            if ORIGIN.match(l):
                origin = l.strip()
                continue
            m = FUNC.match(l)
            if m:
                s = SUB.match(m.group(1))
                if s and origin:
                    out[int(s.group(1), 16)] = origin
                origin = None
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fresh_gen")
    ap.add_argument("--gen", default=GEN)
    ap.add_argument("--file", default="recomp_recovered.c")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    table = {}
    for l in io.open(os.path.join(args.gen, "recomp_dispatch.c"), encoding="utf-8", errors="replace"):
        m = re.search(r'\{ 0x([0-9A-F]{8})u, \(recomp_func_t\)(\w+) \}', l)
        if m:
            table[m.group(2)] = int(m.group(1), 16)
    hdr = fresh_headers(args.fresh_gen)

    path = os.path.join(args.gen, args.file)
    lines = io.open(path, encoding="utf-8", errors="surrogateescape").read().split("\n")
    out, added, missing = [], 0, []
    has_header = False
    for l in lines:
        if ORIGIN.match(l):
            has_header = True
        m = FUNC.match(l)
        if m:
            name = m.group(1)
            if not has_header:
                s = SUB.match(name)
                addr = int(s.group(1), 16) if s else table.get(name)
                if addr is not None and addr in hdr:
                    out += ["/**", f" * {name}", f" {hdr[addr]}",
                            " * (header restored by add_origin_headers.py)", " */"]
                    added += 1
                else:
                    missing.append(name)
            has_header = False
        elif l.strip() == "}" or (l and not l.startswith((" ", "/", "*", "\t")) and not ORIGIN.match(l)):
            # anything between a header and its function that is not comment
            # text ends the header's scope
            if l.strip() == "}":
                has_header = False
        out.append(l)
    print(f"{args.file}: {added} headers added, {len(missing)} without a fresh range"
          + (": " + ", ".join(missing[:20]) if missing else ""))
    if not args.dry_run:
        io.open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write("\n".join(out))


if __name__ == "__main__":
    main()
