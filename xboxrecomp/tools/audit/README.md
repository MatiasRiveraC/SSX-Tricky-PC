# Audit tools

Two tools for the problem that dominates work on this recompilation: code that
*looks* translated, builds clean, and silently does not do what the original
instruction did.

## Why these exist

Recompiled code fails differently from code you write. There is no crash and no
warning — a function is simply missing an instruction, and the effect surfaces
somewhere unrelated, several frames later. Three real examples from this repo:

- `fnstsw ax` emitted as a comment, so every `test ah, 0x41` after a
  floating-point compare read a **stale register**. 2,074 sites.
- A `bsf` dropped, which corrupted every mid-sized heap allocation.
- `RECOMP_ICALL_SAFE` rewinding the stack on a missed call, at a call site whose
  convention did not want that — 8 bytes leaked per hit, corrupting an object
  pointer five frames up.

None of those are findable by reading one function. They are findable by
counting, and by comparing against the original bytes.

## `xverify.py` — what was annotated but never implemented

Scans the generated sources for instructions the recompiler described but did
not emit code for, groups them by opcode, and says what each class costs.

```
xverify.py                     rank every dropped-instruction class
xverify.py --opcode fnstsw     every site for one opcode, with functions
xverify.py --function sub_X    audit one function against the XBE itself
xverify.py --json out.json     machine-readable, for diffing across runs
```

The bare scan is the survey: it tells you which classes exist and how large they
are. Size is the argument — a defect at 2,000 sites is a property of the
emitter, not of a function, and fixing it one site at a time is wasted work.

`--function` is the strong check. It disassembles the real bytes from the XBE
and lines them up against the generated statements, so it also catches
instructions dropped *without* leaving an annotation, which the scan cannot see.
Reach for it when one function misbehaves; use the scan to decide which
functions deserve that attention.

Opcodes are classified into three buckets: **consequential** (behaviour changes),
**by design** (the recompiler defers `cmp`/`test` to the consuming branch on
purpose), and **benign** (`nop`, `wait`). Anything unclassified is reported as
unclassified rather than assumed harmless.

## `xprobe.py` — instrumentation you can take back out

Adds and removes print statements inside generated functions.

```
xprobe.py add sub_000BE260 --entry --show ecx esi
xprobe.py add sub_000BE260 --label loc_000BE424 --show 'MEM32(esi + 0x1C)'
xprobe.py add sub_000BDFD0 --entry --show ebx --when 'ebx != 0' --limit 20
xprobe.py add sub_0014D850 --entry --backtrace
xprobe.py list
xprobe.py clear
```

Three things it handles that hand-editing keeps getting wrong:

**Escaping.** A `\n` written into a Python string that reaches the C source as a
real newline splits the string literal, and the build error points three steps
away from the edit. The escape is built once, here, instead of being retyped.

**Removal.** Probes carry a marker and the helper block is fenced, so `clear`
removes exactly what was added — verified to leave zero residue and an unchanged
line count. Probes that outlive a session change behaviour silently.

**Rate limiting.** Every probe stops after `--limit` hits (default 4). These
sites run per frame; an unlimited print in a draw path produces megabytes and
changes the timing of whatever you are measuring.

`--str` and `--cstr` decode guest strings (UTF-16 and 8-bit) so text reads as
text. `--backtrace` prints the host call chain rebased to link addresses for
`sym.py` — necessary because the guest stack cannot tell you who called
something: generated calls push a dummy return address, not a real one.

## Working order

1. `xverify.py` to see which classes exist and how big they are.
2. `xverify.py --function` on the function you suspect, against the XBE.
3. `xprobe.py add` to watch values, `clear` when done.
4. Re-measure. A branch that starts reading a *correct* value can expose a
   second defect the stale value was masking — this happened with the `fnstsw`
   fix, which regressed file opens 344 → 25 until the x87 stack model behind it
   is fixed too.

That last point is the one worth internalising: in recompiled code, a correct
fix on top of a broken foundation can measure worse than the bug. Always
re-measure, and bisect when it regresses.

## Frame rate: `profsum.py`, `frametimeline.py`

Performance questions have their own switches (part 183), all off by
default and all printing to stderr:

| Variable | Prints |
| --- | --- |
| `XBOX_FPS_LOG=1` | `[FPS]` rate every 2 s, a present-interval histogram, shader compiles and texture uploads |
| `XBOX_PROFILE=START,SECS[,viaN][,all][,stall]` | `[PROFILE]` per-thread sampled CPU; `stall` keeps only samples inside >40 ms present intervals |
| `XBOX_WAIT_LOG=1` or `=N` | `[WAIT]` single-object waits longer than 30 ms / N ms, timestamped |
| `XBOX_TIMER_LOG=1` or `=2` | `[TIMER]` lateness summary / `[TFIRE]` every one-shot fire |
| `XBOX_FLIP_LOG=1` | `[FLIP]` surface switches, `[PRES]` Present time, `[KICK]`, `[BATCH]` push-buffer batches |
| `XBOX_READ_STATS=1` | `[READS]` disc reads every 2 s |

`profsum.py LOG` names the `[PROFILE]` offsets with addr2line;
`frametimeline.py LOG` merges the timestamped lines into one timeline around
each hitch. Work from the average first (`XBOX_FPS_LOG` + `XBOX_PROFILE`),
then from the hitches (`,stall` + the timeline): in part 183 the average
said "CPU-bound vertex programs" and the hitches said "a dropped ring tail",
and both were true.

