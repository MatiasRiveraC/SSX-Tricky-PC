"""tools_176/: the current recompiler with the part-177 and part-178 flag
changes switched off -- address-order flag propagation, no operand snapshots,
no `_cf =` before sbb/adc. Diffing its output against the current lifter's
gives exactly the hunks those two fixes add."""
import os, shutil, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
src, dst = sys.argv[1], sys.argv[2]
subprocess.check_call([sys.executable, os.path.join(here, "make_baseline.py"), src, dst])
p = os.path.join(dst, "recomp", "lifter.py")
s = open(p, encoding="utf-8").read()
a = "            if _needs_flag_snapshot(insns, i, live_out=live_out):"
b = '        if curr.mnemonic in ("sbb", "adc") and last_flag_setter:'
assert a in s and b in s
s = s.replace(a, "            if False:")
s = s.replace(b, "        if False:")
open(p, "w", encoding="utf-8").write(s)
print("tools_176 ready")
