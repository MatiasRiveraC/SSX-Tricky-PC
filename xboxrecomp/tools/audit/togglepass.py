#!/usr/bin/env python3
"""Enable or disable the floating-point fix in the two render-pass functions.

`SceneView_RenderPass` and `SceneRenderer_RenderAllPasses` decide between a
perspective and an orthographic projection on an x87 compare.  Emitted as
`if (1)` the branch always chose perspective, so the orthographic path -- the
one screen-space text needs -- has never run in this port.

Turning it on is a three-part change (the compare's memory operand and its pop,
the `fnstsw`, and the parity branch), and it has to be turned on and off
repeatedly while the code behind it is brought up.  Doing that by hand is how
partial reverts happen, and a partial revert here measures nothing.

    togglepass.py on|off|status [function-name]

The two are independent and can be switched separately, which matters: the
branch in SceneRenderer_RenderAllPasses gates the *batch cursor reset*
(`view+0x804 = view+0x8C0`), while the one in SceneView_RenderPass chooses
between the perspective and orthographic projections. Turning the second on
without the first hands the render loop a cursor still holding its allocator
poison.
"""
import io, re, sys
import ssxpaths

GEN = os.path.join(ssxpaths.GEN, "recomp_0005.c")
FUNCS = ("SceneView_RenderPass", "SceneRenderer_RenderAllPasses")
BROKEN_CMP = re.compile(
    r"^(\s*)_fpu_cmp = \(fp_top\(\) < fp_st1\(\)\) \? -1 : \(fp_top\(\) > fp_st1\(\)\) \? 1 : 0; "
    r"/\* fcomp (dword|qword) ptr \[([^\]]+)\] \*/$")
FIXED_CMP = re.compile(
    r"^(\s*)\{ double _fc = \(double\)MEMF\(([^)]+)\); _fpu_cmp = .*"
    r"/\* fcomp (dword|qword) ptr \[([^\]]+)\] \*/$")


def spans(lines, only=None):
    for name in (FUNCS if not only else (only,)):
        s = next(i for i, l in enumerate(lines) if l == "void %s(void)" % name)
        e = next(i for i in range(s, len(lines)) if lines[i] == "}")
        yield name, s, e


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    only = sys.argv[2] if len(sys.argv) > 2 else None
    lines = io.open(GEN, encoding="utf-8", errors="surrogateescape").read().split("\n")
    n = 0
    for name, s, e in spans(lines, only):
        for i in range(s, e):
            l, ind = lines[i], re.match(r"^(\s*)", lines[i]).group(1)
            if mode == "on":
                m = BROKEN_CMP.match(l)
                if m:
                    lines[i] = ("%s{ double _fc = (double)MEMF(%s); _fpu_cmp = "
                                "(fp_top() < _fc) ? -1 : (fp_top() > _fc) ? 1 : 0; "
                                "g_fpu_cmp = _fpu_cmp; fp_popp(); }"
                                " /* fcomp %s ptr [%s] */"
                                % (m.group(1), m.group(3), m.group(2), m.group(3)))
                    n += 1
                elif l.strip() == "/* fnstsw ax - store FPU status word */":
                    lines[i] = "%sg_fpu_cmp = _fpu_cmp; FNSTSW_AX(eax); /* fnstsw ax */" % ind
                    n += 1
                elif "after test - parity */)" in l:
                    # The *marker* mnemonic is unreliable -- the site in
                    # SceneRenderer_RenderAllPasses is spelled "jp after test"
                    # while the trailing annotation, which comes from the
                    # instruction itself, correctly says `jnp`. Key off that.
                    # Keyed off the enclosing function, not the marker text.
                    # The marker in SceneRenderer_RenderAllPasses says "jp"
                    # while the instruction is `jnp` with mask 0x05, and the
                    # trailing annotation that would reveal that does not
                    # survive a round trip through `off`.
                    jnp = (name == "SceneRenderer_RenderAllPasses")
                    cond = ("!FPU_PARITY(0x05)) /* test ah,0x05 ; jnp */" if jnp
                            else "FPU_PARITY(0x44)) /* test ah,0x44 ; jp */")
                    head = l[:l.index("if (")]
                    tail = l[l.index("goto"):] if "goto" in l else l[l.index(")") + 1:]
                    lines[i] = "%sif (%s %s" % (head, cond, tail)
                    n += 1
            elif mode == "off":
                m = FIXED_CMP.match(l)
                if m:
                    lines[i] = ("%s_fpu_cmp = (fp_top() < fp_st1()) ? -1 : "
                                "(fp_top() > fp_st1()) ? 1 : 0; /* fcomp %s ptr [%s] */"
                                % (m.group(1), m.group(3), m.group(4)))
                    n += 1
                elif "FNSTSW_AX(eax); /* fnstsw ax */" in l:
                    lines[i] = "%s/* fnstsw ax - store FPU status word */" % ind
                    n += 1
                elif "if (FPU_PARITY(0x44))" in l:
                    # Two spellings reach here: this tool's own, and the one
                    # fixfpbranch.py writes when it sweeps the tree. Held back
                    # means held back either way -- a parity branch left in
                    # while the fcomp beside it is still the broken register
                    # form reads g_fpu_cmp from some unrelated comparison,
                    # which is worse than the hardcoded `if (1)` it replaced.
                    head = l[:l.index("if (FPU_PARITY(0x44))")]
                    tail = l[l.index("goto"):] if "goto" in l else ""
                    tail = tail.split("/*")[0].strip()
                    lines[i] = "%sif (1 /* jp after test - parity */) %s" % (head, tail)
                    n += 1
                elif "if (!FPU_PARITY(0x05)) /* test ah,0x05 ; jnp */" in l:
                    lines[i] = l.replace("if (!FPU_PARITY(0x05)) /* test ah,0x05 ; jnp */",
                                         "if (1 /* jnp after test - parity */)")
                    n += 1
            else:
                if "FPU_PARITY" in l or "FNSTSW_AX" in l or "_fc = (double)MEMF" in l:
                    n += 1
    if mode in ("on", "off"):
        io.open(GEN, "w", encoding="utf-8", errors="surrogateescape",
                newline="").write("\n".join(lines))
        print("turned %s%s: %d line(s) changed"
              % (mode, (" [" + only + "]") if only else "", n))
    else:
        print("fix is %s (%d fixed line(s) present)" % ("ON" if n else "OFF", n))


if __name__ == "__main__":
    main()
