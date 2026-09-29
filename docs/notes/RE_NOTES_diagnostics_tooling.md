# Live diagnostics tooling

Built because the diagnose loop had become the bottleneck: edit a generated
`.c` to add an `fprintf`, rebuild (2-5 minutes), run, read, edit it back out.
That loop is slow and it is *unsafe* -- probe removal has damaged real code
twice (a stripped closing brace in `recomp_0003.c`/`recomp_0007.c`, and a stray
`}` in `xbox_HeapAlloc` that let a whole 20-run batch report "clean" against a
stale binary). Almost every probe was asking a question that can be answered
live.

## 1. `xbox_diag` -- in-process server

`xboxrecomp/src/kernel/xbox_diag.c` / `.h`.

Off unless `XBOX_DIAG_PORT` is set; costs nothing when off (verified: 12/12
clean runs with identical `opens=22 / pb=33117` metrics). Listens on
**127.0.0.1 only**. Line-based text protocol -- one command per line, reply is
result lines then a single `OK` or `ERR <reason>` -- so it drives from bash,
python or netcat with no client library.

In-process on purpose: an external inspector would have to marshal symbols,
defeat ASLR and re-derive the guest layout through `ReadProcessMemory`. In here
`g_xbox_mem_offset`, the pool descriptors, the dispatch table and the ICALL
miss list are just variables.

```
info                  runtime layout (offset, RAM size, heap base, pool table)
mem <va> [len]        hex + ascii dump
d32 <va> [count]      dword dump
str <va> [max]        read a C string
pools                 CRT pool descriptor table at 0x00203BE0
freelist <slot> [max] walk one pool's free list, flagging non-'FB' nodes
blocks <va> [max]     walk the block chain from a header VA
heapcheck             validate every pool's free list, report only breaks
icall                 unresolved indirect-call targets, first-seen order
func <va>             is this VA in the dispatch table?
watch <va>            report who writes this page (to stderr)
unwatch               clear all watches
owner <va>            which allocation owns this VA, and who asked for it
files                 every file opened, with bytes actually read
```

### `watch` -- who wrote this address

The most-asked question in this port, and the one gdb answered *wrongly* more
than once (part forty-six: it blamed `ntdll!RtlAllocateHeap` for a write that a
`VirtualQuery` sweep proved could not have come from there).

Mechanism: write-protect the page; the VEH catches the access violation, prints
the guest registers and the native return addresses, unprotects, and sets the
trap flag so the instruction completes; the following single-step re-arms the
guard. `xbox_diag_handle_fault()` must be called **first** from the VEH, for
both `EXCEPTION_ACCESS_VIOLATION` and `EXCEPTION_SINGLE_STEP`.

Page-granular, so neighbouring addresses also report -- the exact faulting VA
is printed. Resolve the frames with `addr2line` on a `-g` build, or via `nm -n`
plus a nearest-symbol lookup on a release build.

## 2. `xbdiag.py` -- client

`python3 xbdiag.py <port> "<cmd>" ["<cmd>" ...]`. Retries the connect (so it can
be launched alongside the title), prints each reply, exits non-zero if any
command answered `ERR`.

## 3. `xbrun.py` -- run harness

`python3 xbrun.py [-n 20] [-t 12] [--env K=V]`. Runs the title N times,
groups crashes by signature, and reports `opens` / `icall-miss` /
push-buffer dwords for the last clean run. Exit status is the crash count, so
it drops straight into a bisect.

Intermittent faults here sit around 1-in-6 to 1-in-20; three runs prove
essentially nothing, which is how an intermittent fault went unattributed for
two whole passes. **12 seconds per run is enough** -- full state (22 opens,
33,117 dwords) is reached by then, so a 20-run sample costs four minutes.

## `owner` -- who asked for this memory

Added after the first investigation went wrong. `xbox_HeapAlloc` now keeps a
4096-entry ledger of every block with the native return addresses of the
caller; `owner <va>` finds the block containing an address and prints them.

"Which subsystem owns this address" decides whether a stray write is a bad
pointer or a genuine double-allocation, and it cannot be answered from the
address alone. It took one command to settle a question I had been reasoning
about incorrectly for several steps.

## First investigation -- and the correction

`heapcheck` reported pool 0's free list broken, and `d32` showed the descriptor
at `0x01614010` holding an ascending pointer table (stride `0x100`) instead of a
descriptor. It looked like heap corruption.

Arming a page watch during init is useless -- the pool block is 53 MB and its
zero-fill generates millions of faults -- so `armwatch.py` polls `freelist`
until the pool is genuinely valid and only then arms. That caught the writer:

```
Application_RunMainLoop -> sub_000AE16F -> SceneRenderer_RenderFrame +0x1892
  -> sub_001043B0 +0xF8D -> sub_00169A00 +0x245        (D3D section)
```

and `owner 0x01614010` identified the block:

```
allocation #15: 0x01614000..0x04B2D998 (55,679,384 bytes)
  requested by XBoxExecutionMan_Construct -> Application_InitPlatformAndDevice
               -> sub_00151380 -> sub_00154584 -> MmAllocateContiguousMemoryEx
```

**The conclusion was that this is not corruption.** SSX builds a CRT pool inside
that 53 MB block during init, uses it briefly -- the watch caught it valid, one
`FB` node with 51 MB free -- then retires it, and D3D reuses the same memory for
a vertex pointer table. The stale pool-table entry at `0x00203BE0` still points
there, so a naive walk reports a broken list for the rest of the run. The
supporting evidence is that the state is identical at 2 s and 9 s and the title
reaches exactly the same place every run (22 opens, 33,117 dwords) -- a live
heap being corrupted would not be deterministic.

`walk_freelist` now checks the sentinel's `0x7FFFFFFF` size first and reports a
retired pool as such, with 0 violations, rather than as a break. Two false
leads' worth of time, fixed in the tool so it cannot recur.

**The lesson worth keeping**: the tool made the wrong answer *fast*, which is
still a win -- but a diagnostic that cannot tell "retired" from "corrupt" will
manufacture bugs. Encode the invariant, not just the walk.

## vtaudit.py -- guest vtable vs. dispatch table

`xboxrecomp/tools/audit/vtaudit.py`

    vtaudit.py 0x0019A724          # audit one vtable
    vtaudit.py 0x00196960          # the frontend / TitleIntroSequence class
    vtaudit.py --find-screens      # candidate screen-class vtables
    vtaudit.py 0x0019A724 --slots 24

Exits non-zero if any slot is unregistered or unreachable, so it works in a
check loop.

Four blockers have had the same shape: a method table points at an address with
no translated body, or with one that is not registered, and the indirect call is
dropped silently -- the call returns whatever was in `eax`, and the title waits
forever on an answer nothing computed. This makes that a one-command check.

Per slot it prints the target, the registered symbol, and whether
`recomp_lookup`'s **binary search** can reach it. That last column matters
independently: an entry present but out of sort order is unreachable no matter
how correct its body is (part 113).

Three traps it encodes, each of which produced a confident wrong answer when the
check was done by hand:

* The generated sources are **CRLF**, so a grep anchored `^void name(void)$`
  never matches. It once reported every slot of a fully-translated vtable as
  missing.
* A placeholder written on **one line** is still a definition, so "no body" from
  a line-oriented search can mean "there is a stub here that draws nothing".
* Walking back over `.data` to find a vtable base runs into unrelated words that
  merely look like code addresses. `0x001C72C0` audited as an 18-slot vtable
  with every slot unregistered; nothing in `.text` stores that address and its
  "methods" start mid-instruction. The tool now warns when a base is never
  referenced from `.text` -- a real vtable is always stored into an object.

The dispatch table is the oracle rather than a source grep: an entry there has
to resolve to a defined symbol or the link fails.

## recover_batch.py -- gated function recovery

`xboxrecomp/tools/audit/recover_batch.py`

    recover_batch.py 0x0004C140 0x0007BEA0 ...
    recover_batch.py --from-miss run.log       # targets from ICALL-MISS lines
    recover_batch.py --runs 3 --seconds 70 --min-draws 60000 0x...
    recover_batch.py --keep-going 0x...        # splice even if the gate fails

One batch end to end: back up the four files it touches, seed the
disassembler, lift each target, splice bodies + declarations + dispatch
entries in sorted position, close the call graph off the **linker**, build,
measure, and **revert automatically unless the batch passes**.

Exits 0 on KEPT, 2 on REVERTED, so it can drive a bisect loop. Restore points
live in `RE_NOTES/recover_batches/<tag>/`.

### The gate

    exit == 124   crash == 0   textcorrupt == 0   draws >= --min-draws

All four, because each alone lies. A batch can link cleanly, exit 124 and
still be writing over guest `.text` (part 120). A draw-count drop can be
*progress* -- the title leaving a busy screen for a quiet one -- so when a
batch fails the band, check frames before believing either reading; in part
121 that check confirmed a real regression (black screen, splash never
appears).

### What it encodes

* **Backup per batch.** Part 120 rolled back a bad batch and lost the good one
  before it.
* **Closure matches `undefined reference` *and* `multiple definition`.** A loop
  greping only the first reports LINK CLEAN while the build fails.
* **Skip an address already registered under a different symbol** -- the
  part-60 duplicate bug -- and rewrite its call sites, including ones spliced
  by an earlier round of the same batch, or closure loops forever on a symbol
  it has decided not to add.
* **Split bodies by scanning to a lone `}`.** Generated bodies contain a blank
  line before every `loc_` label, so splitting on blank lines shreds functions
  and yields `expected declaration or statement at end of input`.
* **Correct scalar-float `movaps`.** The lifter occasionally emits a 128-bit
  move as `float xmm0; xmm0 = MEMF(a); MEMF(b) = xmm0;` -- 4 of 16 bytes.
  1 site in the tree against 2,960 correct ones, but a fresh lift can land on
  it, and it is worse than a dropped call: the dropped call leaves the old
  value intact, this leaves three quarters of it stale.
