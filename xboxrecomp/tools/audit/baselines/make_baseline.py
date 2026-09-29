"""Build tools_old/: a copy of xboxrecomp/tools with the part-178 flag changes
reverted, so full-program output can be diffed old vs new."""
import os, re, shutil, sys

src = sys.argv[1]     # xboxrecomp/tools
dst = sys.argv[2]     # scratch/.../tools_old/tools
if os.path.exists(dst):
    shutil.rmtree(dst)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns("__pycache__", "output", "*.json"))

# ---- lifter: undo the four edits ----
p = os.path.join(dst, "recomp", "lifter.py")
s = open(p, encoding="utf-8").read()
s = s.replace('''    return clobbered and ops is None and live_out
''', '''    return False
''')
s = s.replace('''    # Flags materialised by each predecessor (translator._materialise_flags):
    # the block is reached from setters that disagree, so the condition was
    # evaluated at the end of every predecessor into a named local.
    if flag_setter == FIN_SETTER:
        return f"{flag_ops[0]}_{jcc}", desc
''', '')
i = s.index("    # Flags that arrived from a predecessor are rebuilt from the setter's")
j = s.index("        stmts.extend(snap_stmts)\n", i) + len("        stmts.extend(snap_stmts)\n")
s = s[:i] + s[j:]
open(p, "w", encoding="utf-8").write(s)

# ---- translator: old address-order propagation ----
p = os.path.join(dst, "recomp", "translator.py")
s = open(p, encoding="utf-8").read()
s = s.replace("        in_states, fin, flags_live = _incoming_flag_states(self.lifter, blocks, start)\n",
              "        flag_state = None\n")
i = s.index("            stmts, _ = lift_basic_block(\n                self.lifter, bb, flag_state=in_states.get(bb.start),\n")
j = s.index("                stmts = stmts[:at] + fin[bb.start] + stmts[at:]\n", i) + \
    len("                stmts = stmts[:at] + fin[bb.start] + stmts[at:]\n")
s = s[:i] + ("            stmts, flag_state = lift_basic_block(\n"
             "                self.lifter, bb, flag_state=flag_state)\n") + s[j:]
i = s.index("        fin_names = sorted(")
j = s.index("+ \"; /* conditions from disagreeing predecessors */\")\n", i) + \
    len("+ \"; /* conditions from disagreeing predecessors */\")\n")
s = s[:i] + s[j:]
open(p, "w", encoding="utf-8").write(s)
print("baseline tools at", dst)
