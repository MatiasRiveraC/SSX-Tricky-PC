# SSX Tricky RE notes — index

**`RE_NOTES_DECOMP_PROGRESS.md`** is the short dashboard version of this file
— headline metrics, a "what we have" / "what's missing" checklist, updated
after every session that makes progress. Check it first for a fast read on
project state; come here for the full narrative.

Working notes from decompiling `default.xbe.c` (Ghidra's full C export of the Xbox
`default.xbe`) toward the PC-port research described in
`ssx-tricky-pc-port-notes.md`. Read this file first when picking the project back up —
it links every other note file and says what's actually confirmed vs. still a guess.

## Files in this folder

- **`ssx_auto_rename.py`** — the actual deliverable. A Ghidra script (run via Script
  Manager against the live project) that applies every rename/bookmark/comment found
  across all sessions. **617 rename calls + 4 data renames + 44 bookmarks as of the last
  update, syntax-
  verified, zero duplicate target addresses, zero colliding target names.** Re-runnable/
  idempotent — safe to run again if the Ghidra project gets reset. Everything in it is
  reversible. Two stale entries have been found and superseded across sessions
  (`0x0004a5a0`: early guess `New_Script_3` → verified `Script_PlayByName`; `0x0004a730`:
  early guess `New_Script_5` → verified `ScriptVM_DispatchOpcode`) — old lines commented
  out in place rather than deleted, so the file also records *why* a name changed.
- **`default.xbe.c` — refreshed once this session (as of the round-1 GhidraMCP patch), now
  stale again.** `ssx_export_c.py` was run once; the output (`default.xbe.refreshed.c`,
  257,631 lines, verified to contain current names like `Timer_RebuildPlayerRegistry` as a
  real function body, not just a reference) was reviewed and swapped in as `default.xbe.c`.
  The pre-session version is kept as `default.xbe.c.session-backup`. **Rounds 2 and 3**
  (the `GameMode_*`/`Level_PreloadAndCheckCountdown`/`SweepPrune_*` follow-on renames, the
  `search_bytes`/`delete_function` endpoints, `xbox.gdt`) all happened after that export —
  re-run `ssx_export_c.py` again before trusting the text export for anything from this
  point forward.
- **`ssx_export_c.py`** — a third Script Manager script. Re-exports the whole program to C
  via Ghidra's real `CppExporter` (same mechanism as File → Export Program → C/C++),
  writing to `default.xbe.refreshed.c` (not overwriting the original, so it can be
  diffed/reviewed first). Not urgent, not run automatically by anything — run it whenever
  convenient. Full-program decompile of a binary this size will take a while (parallel
  decompiler internally, but still a lot of functions).
- **`ssx_parse_xbox_headers.py`** — a fourth Script Manager script. Builds a proper Ghidra
  data type archive (`xbox.gdt`) from `xbox-includes-master/xbox.h` — the real, open-source
  Xbox SDK headers (xboxkrnl/d3d8/dsound/xacteng/xgraphic/xonline), pre-flattened by that
  project's own Makefile into one self-contained file. Uses `CParserUtils.parseHeaderFiles`,
  the same utility the GUI's Data Type Manager → File → Parse C Source dialog calls.
  **Run this session — succeeded: `xbox.gdt` built with 2084 data types.** Not yet opened
  in Ghidra's Data Type Manager (File → Open File Archive) or applied to any function —
  that's the natural next step, should help firm up the many D3D8-shaped functions already
  found (`BoardMesh_*`, `FX_SpawnTrail*` in `RE_NOTES_ubertrick_fx_cluster.md`) via
  `set_function_prototype`/`set_local_variable_type`. **Ignore `xboxdef_ghidra.h`** in the
  same folder — the user flagged it as ChatGPT-generated and untrusted; not used by this
  script or anything else in the project.
- **`ssx_analyze_gap.py`** — a second, small Ghidra Script Manager script (not part of
  `ssx_auto_rename.py`, run separately) that schedules real Ghidra analysis over the one
  known unanalyzed code region (`0x0002c000`–`0x0002ea00`). **Run this session — result:
  added zero new functions**, proving the region was already fully analyzed (no hidden
  gap). The remaining `Script_PlayByName`-caller-cluster mystery is a different problem
  (zero xrefs to the fragment addresses — they're referenced as raw data somewhere, not
  as code), not something this script could ever have fixed. See "GhidraMCP patched"
  below and open thread 2 in `RE_NOTES_level_script_system.md`.
- **`name_candidates.json`** — raw data behind the Part-1 string-mining pass (self-naming
  debug strings + tagged-allocator call sites). Superseded in usefulness by the
  `RE_NOTES_*.md` files below, kept for reference.
- **`RE_NOTES_xboxrecomp_test.md`** — a real test run (not just reading docs) of
  [xboxrecomp](https://github.com/sp00nznet/xboxrecomp), a static-recompilation
  toolkit (x86→C, directly compilable, not decompilation-for-reading) run by a session
  that had fallen behind this file's actual state — see the note at the top of that
  file. Initial pass ran stages 1-4 (XBE parse → disasm → func_id → recomp): confirmed
  SSX Tricky is not RenderWare (0.0% classified); its independent vtable scanner found
  4,128 previously-undiscovered functions (510 candidate vtables, 9,121 virtual
  methods) purely from walking vtables, worth cross-checking against live Ghidra's
  current function count; mechanically recompiling `Script_DispatchOpcode`
  (`0x00049960`) matched the manual RE in `RE_NOTES_level_script_system.md`
  instruction-for-instruction, independently cross-validating that work via a
  completely different, non-Ghidra method. **Follow-up pushed all the way through
  stages 5-7 to an actual build and run** — found and fixed 2 real upstream MinGW-
  portability bugs (`__debugbreak`/`KernelMode` macro collisions with unrelated Windows
  SDK symbols), translated all 3,815 functions to 343K lines of C (0 failures), built a
  real linked `your_game_recomp.exe`, and **ran it successfully with zero crashes** —
  loaded the real XBE, mapped the full 64MB Xbox address space, resolved all 114 kernel
  thunks, executed the real entry point (`0x00154218`), and (after seeding one
  indirectly-referenced address via `--seed-functions`) the spawned game thread routine
  itself executed and returned cleanly. Also **dynamically confirmed** this project's
  very first open question (the `0x1541A9` thread-context address from session 1) via
  real execution, not static guessing. **Third pass fed this project's own
  `scripts/ssx_auto_rename.py` (1,317 renames) back into the recompiler** —
  `xboxrecomp_output/apply_re_names.py`, a reusable script, injects real names into
  `functions.json`'s `name` field (which `recomp` uses directly). 828 landed
  immediately; the other 489 were known-but-undetected by xboxrecomp's own
  disassembler (same gap class as `0x1541A9`/`0x1543DE`, 489x the scale) — seeding
  those recovered 463 of them, bringing total coverage to 1,292/1,317 (98.1%). Final
  rebuild carries **both** improvements at once: ~30% of the 4,280 generated functions
  now have real names (verified in the actual generated `.c`, not just the injection
  script's own count), and the thread-routine execution result is unchanged (still
  runs, still returns cleanly, zero crashes). Repo cloned to `xboxrecomp/`, the built
  game project is `ssx_recomp/` (both siblings of this folder), outputs in
  `xboxrecomp_output/` including the reusable `apply_re_names.py` and the correct
  complete `seed_functions_final.json` (490 addresses) for next time.
  **Fourth pass found and seeded the actual game entry point** — reading the
  translated CRT thread-bootstrap's own generated C revealed an indirect call through
  the thread's context parameter, which resolves to `0x1541A9` (this project's very
  first open question, from session 1 — now dynamically confirmed as a real function
  pointer, not inert context data). Seeding it (492-address total seed list) got real
  SSX Tricky code executing for the first time: 8 distinct kernel calls (vs. 2-4
  before), reaching a genuine `PsTerminateSystemThread` call before hitting a real,
  root-caused `SIGILL` crash — the runtime's own bridge stub returns instead of
  actually terminating the thread (documented in its source as verified only for
  Burnout 3's specific caller), so SSX Tricky's code falls through into unreachable
  bytes. **Fixed.** Extended `main.c`'s crash handler to also
  catch illegal-instruction faults (it only caught access violations before) and
  report the exact Xbox VA — pinpointed a wild jump to `0x017A8467`, ~24.8MB outside
  the entire ~2MB mapped Xbox image, confirming stack/return-address corruption rather
  than a simple fall-through. Root cause read directly from the runtime's own
  threading model: `PsCreateSystemThreadEx` runs thread routines as nested synchronous
  C calls (documented in its own comment), so `PsTerminateSystemThread` (which never
  returns on real hardware) needs to unwind that whole C call chain, not just return
  one level. Added `setjmp`/`longjmp` to `xboxrecomp/src/kernel/kernel_bridge.c` (a
  small nesting stack + shared helper wrapping both thread-start call sites) — a
  general fix to the runtime itself, not an SSX-specific hack, since the prior
  behavior was only ever verified against Burnout 3's specific caller. **Rebuilt and
  reran: exit code 0, zero crashes**, full coherent execution through everything
  currently translated (thread terminates cleanly via the new unwind path, control
  correctly resumes in `entry()`'s own code, clean program exit). Also linked (NTFS
  junction, not a 2.9GB copy) the real extracted game assets into `ssx_recomp/game/data`
  — not yet exercised, execution doesn't reach file I/O at this depth yet.
  **Confirmed the exact mechanism the fix corrects**: traced the call chain (temporary
  native-stack-scan diagnostic, mapped via `nm` on the built exe, then removed) to
  `sub_00154468`, the CRT thread-exit wrapper — its generated code shows a call
  through a function-pointer table immediately followed by a **deliberate
  `__debugbreak()`**, the textbook MSVC-compiled safety trap for "this call should
  never return." The original bridge's "just return" behavior walked straight into
  that intentional trap; the `setjmp`/`longjmp` fix is exactly what makes this real
  CRT safety mechanism behave as its original author designed.
  **Fifth pass: corrected an over-optimistic read and pushed substantially deeper.**
  The "clean termination" wasn't the real game logic finishing — it was hitting two
  empty auto-generated stub functions almost immediately (confirmed by checking
  `recomp_stubs_unresolved.c`). Extracted all 2,170 stubbed addresses project-wide and
  seeded them in one batch (function count 4,282 → 6,376 → 10,078 after re-
  classification). Translation surfaced a **real bug in the recompiler itself**:
  `tools/recomp/translator.py` decided whether to declare the `ebp` local using a
  hardcoded Burnout-3-specific SEH-helper address pair, silently failing for any other
  game (SSX Tricky's real, correctly-*detected* addresses never matched the hardcoded
  set) — fixed to use the same dynamically-detected addresses the emission code
  already relied on. Rebuilt clean (10,078/10,078 functions, 0 failures) and reran:
  **16 distinct kernel calls now** (up from 8), including a real non-trivial handle
  and a genuine `IoCreateFile` attempt returning `STATUS_OBJECT_PATH_NOT_FOUND` — the
  first point execution has reached real file-I/O-adjacent game logic. Still **exit
  code 0, zero crashes**.
  **Sixth pass diagnosed the file-path failure, first as an apparent architecture
  mismatch, then corrected that read after actually tracing it.** The `IoCreateFile`
  `OBJECT_ATTRIBUTES` pointer resolves into a fixed TLS-derived region (`fs:[0x28]`
  chain) the shared runtime's comments label "RW engine data area" (RenderWare
  phrasing) — first assumed this meant a Burnout-3-specific assumption baked into the
  runtime, breaking non-RenderWare games. **Traced it properly instead of leaving that
  assumption standing**: read `sub_001543DE` (the CRT thread bootstrap) in full for
  the first time, and its opening code is SSX Tricky's own compiled TLS-initialization
  logic — the exact generic MSVC pattern, copying the real TLS template from the
  loaded XBE via the same `fs:[0x28]`→`+0x28` chain. The runtime's mechanism is
  correct and general; the comment was only ever describing what Burnout 3 happens to
  store there. The actual explanation: the specific TLS variable read at `+0x10` is
  very likely just legitimately unset at this point in execution (ordinary program
  state), and SSX Tricky's own code already handles the resulting error gracefully —
  no fix needed, the original "architecture mismatch" framing was corrected once
  traced rather than inferred from a comment. Re-applied the proven seed-everything-
  stubbed technique separately (1,161 more
  addresses, combined seed set 3,734 total; functions 6,376 → 7,449 → 11,144 after
  reclassification; translated clean, 0 failures). Rerun found **20 kernel calls**
  (up from 16), including the first genuine memory allocation
  (`MmAllocateContiguousMemory`) and a flagged missing kernel function
  (`HalRequestSoftwareInterrupt`, ordinal 49) — implemented as a documented no-op
  (correct for this fully-synchronous, non-preemptive execution model) and wired into
  the dispatch table. Verified: warning gone, execution unchanged otherwise. **Still
  exit code 0, zero crashes**, through the deepest and most complete run yet.
  **Eighth pass found the biggest unlock yet, then a fully-diagnosed crash.**
  `xbox_memory_layout.c` hardcoded the Prcb pointer at `fs:[0x20]` to 0, a
  Burnout-3-specific "skip the D3D cache check" hack that silently forced SSX
  Tricky's `sub_001541A9` down a one-line no-op instead of the real branch into
  `Application_ConstructAndInitInput`. **Fixed** (real fake Prcb buffer with a
  non-zero `+0x250` field) — kernel calls jumped from 20 to **200+**, with six full
  `PsCreateSystemThreadEx` worker-thread cycles now running and terminating cleanly.
  Hit a new `EXCEPTION_ILLEGAL_INSTRUCTION` after cycle #6; two more diagnostic bugs
  fixed to see it clearly (the illegal-instruction handler's "Xbox VA of fault" math
  was nonsensical for RIP, and the native-stack-walk filter was hardcoded to the
  link-time image base instead of the ASLR-relocated runtime base). With both fixed,
  traced the crash all the way to a specific, named game assertion: `FILESYS_atomic`
  (`0x0014E770`) rejecting a `FILE_load` call because the target device's priority
  ceiling (`MEM32(0x1FE4A4)`-based array, `+0x70` field) is still at its zeroed
  default — read the actual assert string straight out of `default.xbe`'s `.rdata`
  to confirm. Not yet fixed — the missing piece is whatever's supposed to raise that
  device's priority before the first file load, possibly tied to the earlier
  `IoCreateFile`/`STATUS_OBJECT_PATH_NOT_FOUND` finding. Full detail in
  `RE_NOTES_xboxrecomp_test.md`'s eighth follow-up.
  **Ninth pass corrected that hypothesis with real instrumentation** (temporary
  `fprintf`s added to the generated code, then removed after use): the device-open
  step (`sub_0014E260`) actually runs every time and correctly sets priority to
  `0xFF` — not a missing-init bug after all. The real problem: the *requested*
  priority argument `FILESYS_atomic` reads is garbage on some calls (once a function
  pointer, once a stack address, never a real small integer) — an argument-passing
  bug somewhere in the call chain, not a missing step. Also found the device array's
  base pointer resolves to a suspiciously low address (`0xCE8`, below the entire
  loaded image) — its origin is still untraced. Separately confirmed (3 repeat runs,
  identical up to 423 kernel calls each time) that the illegal-instruction assert
  doesn't reliably terminate the process — it's sometimes silently passed through by
  Windows, and execution can continue into an unrelated, genuinely fatal access
  violation near the entry point. Not a second bug — a corrupted-resumption artifact
  of the same root cause. Fixed the access-violation crash-handler branch to match
  the same ASLR-aware fixes already applied to the illegal-instruction branch.
  **Tenth pass found and fixed the actual root cause.** Traced the corrupted priority
  argument via real x86 disassembly (capstone against `default.xbe`'s own bytes) to a
  genuine code-generator bug: `sub_00154476` ends with `ret 0x18` (pops its own 24
  bytes of args) in the real binary; the translator correctly generated the epilogue
  piece (`sub_001544C6`) but the fall-through predecessor (`sub_001544C3`) never
  called it, silently dropping the cleanup on that path. **Fixed** (added the missing
  call) — rebuilt, ran 3x: the `FILESYS_atomic` assert is completely gone, kernel
  calls up from 423 to 435, crash now deterministic (was flaky before). A codebase-wide
  sweep found ~888 functions (11%) with the same missing-linkage shape — likely a
  systemic translator issue, but most are probably unreached dead paths; only fixed
  the one proven to matter rather than blind-patching all of them. **New crash past
  this point**: the Xbox-side simulated stack pointer has wrapped around (~15MB
  underflow past the 8MB stack region) by the time execution reaches a float-to-int
  conversion helper — the crash site is an innocent bystander, real corruption
  happened earlier in the (now longer) call chain. Not yet traced to source.
  **Eleventh pass eliminated the stack corruption entirely.** Switched to gdb
  (hardware watchpoint on `g_esp`, then just `bt`/`x/i $pc` on the raw SIGSEGV — far
  more reliable than the hand-rolled VEH stack-walker for this project going
  forward) and found/fixed 5 more instances of the same translator bug class,
  verified against real x86 bytes each time: `sub_0015CAC7`'s missing link into its
  real `leave;ret` epilogue, `CRT_ftol_TruncateToInt64`'s own missing fall-through
  for the *common* `eax != 0` case (likely the highest-impact single fix — this CRT
  helper backs nearly every float-to-int conversion in the game), 6 near-identical
  FPU-control-word helper siblings sharing one dropped linkage, and
  `RNG_NextUInt32`'s overflow-carry branch (hit on ~50% of all RNG calls,
  game-wide). Verified incrementally, 3 runs each. **`esp` is now fully healthy**
  at the point of the current crash — the whole stack-corruption saga from the
  eighth follow-up onward appears resolved. Execution now reaches genuinely new
  territory and crashes in `sub_000A8E60` (`Application_RunAndShutdown`) walking
  what looks like an uninitialized callback-object list — not stack corruption this
  time, likely a data-not-yet-populated issue. Not yet traced.
  **Twelfth pass traced it to two genuinely unresolved functions** (`sub_00150BB1`/
  `sub_00150BE1`, part of the heap allocator's free-block search — never
  disassembled at all, not a linkage bug this time) and fixed them the proper way:
  used the project's established seed-and-regenerate pipeline (union of all current
  stubs + seed history, full `disasm→func_id→recomp` rerun), then — after a full
  codebase swap surfaced a different crash elsewhere from re-splitting — reverted and
  surgically transplanted just the newly-resolved functions (plus 2 more found via a
  second targeted seed round) into the existing, well-tested codebase. Rebuilt, ran
  3x: **no crash at all** — execution now settles into a stable, non-progressing loop
  of `RtlEnterCriticalSection`/`RtlLeaveCriticalSection` calls. Read as a legitimate
  environmental limit, not a bug: likely the main thread spin-waiting on a condition a
  concurrently-running thread would set, which structurally can't happen since this
  runtime executes worker threads as fully synchronous nested calls, not real
  concurrency.
  **Thirteenth pass found that theory was wrong** — attached a live debugger
  (`gdb -p`, checking *every* thread via `thread apply all bt`, not just gdb's
  default) to the actually-hung process and found the real game thread genuinely
  stuck in an infinite loop inside `sub_00150BB1` (the free-block search added last
  round), walking a linked list from address 0. The heap's 16 size-class buckets are
  correctly zero per the XBE's own BSS layout, but never get populated for the class
  this allocation needs — the one function that touches that array
  (`sub_001517E0`) turned out to build one named 55MB arena, not populate
  size-classes; where individual classes get filled from that arena is still an open
  question. Added an explicit, acknowledged **workaround** (200,000-iteration cap on
  the search loop, fails gracefully instead of hanging) rather than a real fix.
  With it active, execution passes `Application_ConstructAndInitInput` entirely and
  reaches a new crash in `sub_00151E01`, touching the same Prcb structure from the
  eighth follow-up's major fix. Not yet investigated.
  **Fourteenth pass found the real root cause and reached a fully clean run.**
  Watched the Prcb corruption live and traced it to `sub_0015457F` (the "no
  preferred address" allocation branch — the *normal* path) never calling
  `sub_00154584`, which holds the real kernel ICALL that performs the actual
  memory reservation. The same missing-linkage bug as always, but this time it meant
  the game's entire 55MB heap arena was never allocated at all — explaining every
  symptom since the twelfth follow-up at once. **Fixed** — rebuilt, and the real
  allocation call now fires but returns NULL: a genuine resource limit, not a bug.
  `xbox_memory_layout.h` deliberately caps total RAM at the real Xbox's 64MB "so
  RenderWare's memory probing stops at the right boundary" — a Burnout-3-specific
  constraint that doesn't apply to SSX Tricky (confirmed not RenderWare much
  earlier in this project) and leaves too little room for this ~55.7MB request.
  Verified the mirror-view system scales its stride from the same constant (safe to
  raise), and raised it to 128MB. Rebuilt, ran 3x: **exit code 0, zero crashes, zero
  hangs** — the heap allocates for real, kernel calls reach 373, and the main thread
  terminates and shuts down cleanly. The first completely clean run of the whole
  project.
  **Fifteenth pass fixed the missing-linkage bug class at its actual root, in the
  tool itself** (prompted by a direct question about reusing this tool for another
  game). Every one of the 9 hand-patched functions from earlier follow-ups was really
  one bug, in `disasm.py`'s `build_basic_blocks()`: no handling at all for a basic
  block whose last instruction's natural fall-through reaches or passes the
  function's own end address (a fall-through predecessor of a sibling function,
  structurally a tail call with no explicit `jmp` to trigger the already-correct
  explicit-tail-jmp handling). Fixed generally in `translate_function()`
  (translator.py): detects this exact case and emits the same tail-call pattern
  already used for explicit external jmps. Verified two ways: all 9 previous
  hand-patches now reproduce automatically and byte-for-byte, and a full-codebase
  count found **1,271 total instances** — over 140x what was found by hand. Likely
  the single highest-leverage fix of the whole project. Swapped the regenerated,
  general-fix-verified codebase in and rebuilt: runs stably to a new, precisely
  identified spin — `KeWaitForMultipleObjects` (ordinal 158), which has no kernel
  bridge implementation at all. This is the honest version of the twelfth
  follow-up's original concurrency theory: a real wait this fully-synchronous
  threading model can't satisfy, a materially larger undertaking than any fix so
  far.
  **Sixteenth pass started pushing toward real rendering.** Found the entire D3D8
  driver section of the XBE (36+ functions `Renderer_InitializeD3DDevice` calls
  into) had never been disassembled at all — every prior `disasm` run used
  `--text-only`, which skips non-`.text` sections entirely. Reran with
  `--extra-sections D3D` (dropping `--text-only`): function count 8,343 → 9,235,
  D3D section 0 → 204 translated functions, full clean swap (safe now, post the
  fifteenth follow-up's general fix). Execution now runs real D3D8 device-creation
  code for the first time and hits a new spin: a "kick a queue, wait for it to
  clear" pattern that looked like a hardware register access but resolves (checked
  live) to the driver's own internal command-queue bookkeeping, not real GPU MMIO.
  `nv2a_core.c`'s own comment confirms the GPU command FIFO is explicitly
  "stub for Phase 1... Phase 2-3" — an acknowledged, unfinished, multi-phase piece
  of the tool, not a bug. Same fundamental shape as the `KeWaitForMultipleObjects`
  finding: needs real async/concurrent processing this synchronous runtime doesn't
  have. Real rendering needs a deliberate decision on how deep to go here, not more
  default bug-patching.
  **Seventeenth pass broke through it.** Another D3D-scoped seed round (targeting
  the ~240 stubs still left after the sixteenth's first pass) took the D3D section
  from 204 to 317 translated functions (9,235 → 9,782 total) — and whatever runs now
  services the PFIFO queue, since the hang is gone and real GPU buffer allocations
  start happening. Hit a new crash reading the real NV2A MMIO range
  (`0xFD000000-0xFE000000`) and found two genuine pre-existing bugs in `main.c`'s
  crash handler while diagnosing it: its special-case check for this exact range
  compares the *native* fault address against the raw Xbox-VA constants (can never
  match, since native = xbox_va + offset, always past 4GB), and even a match
  wouldn't have resumed execution (explicit TODO, no instruction-skip implemented).
  Fixed properly: mapped that whole 16MB range to real zeroed memory in
  `xbox_MemoryLayoutInit()` (same pattern as the Prcb/heap-size fixes) so it simply
  never faults. Rebuilt: that crash is gone, execution reaches further into real
  D3D device setup than ever before, now hitting a new access violation
  (`0x80010000`/`0x80000000`, both suspiciously round) inside
  `Renderer_InitializeD3DDevice`'s chain — possibly a bad parameter into the real
  D3D11 call inside the `xbox_d3d8` shared library rather than another generated-code
  gap. Not yet diagnosed.
  **Eighteenth pass: a third D3D seed round (9,782 → 10,078, D3D 317 → 376) got past
  that crash, then hit `sub_00180683`** — `MEM32(esi+0x18)` writing through an
  esi around 2GB, impossible for a bump allocator capped at 128MB. Traced back to a
  live log line: `no bridge for ordinal 14, returning 0` — ordinal 14 is
  `ExAllocatePool`. `kernel_thunks.c`'s canonical, already-verified
  `xbox_resolve_ordinal()` table has it right, but `kernel_bridge.c`'s three
  *separate* ordinal tables (`bridge_for_ordinal`, `stdcall_args_for_ordinal`,
  `kernel_data_va_for_ordinal` — the tables the actual per-title dispatch path uses)
  never got the same correction and had drifted by 1-2 ordinals in multiple places
  (HAL, I/O manager, Ke sync, Rtl, crypto, the Xe/identity DATA-export block).
  Found every mismatch with a small script that diffs the three tables against the
  canonical one by function name rather than by number, fixed all of them, verified
  no duplicate `case` labels. Rebuilt: `ExAllocatePool` now actually allocates
  (previously always returned 0) and the `sub_00180683` crash is gone — a general,
  game-independent kernel-bridge bug, likely fixing silent misbehavior elsewhere too.
  New crash a little further in, inside `sub_0017FF53`: walks a 4-entry static
  object/callback array (confirmed correct at function entry via live `gdb`
  inspection — not a zeroing bug), crashes on `MEM32(eax+4)` with `eax≈0xFFFF0382`,
  a value matching none of the array's real contents. Root-caused by single-stepping
  with address breakpoints (the crash handler's `eax`/`ebx`/`esi` dump is trustworthy —
  those are genuinely global in this codebase — but gdb's native `%rsi`/`%rbx` are
  pure compiler register allocation and misled the first pass): one array entry's
  callback chains into `IoCreateDevice` (ordinal 65), which had **no bridge at all** —
  silently returned 0, which the caller's negative-only failure check let straight
  through, leaving an out-parameter device pointer never written. Wrote a real
  `bridge_IoCreateDevice` (Xbox-VA-space allocated via `xbox_HeapAlloc`, with
  `DeviceExtension` correctly placed at the real Xbox `DEVICE_OBJECT`'s `+0x18`, the
  field the driver actually dereferences after the call — confirmed against the
  disassembly). Also found and fixed two more silent-stack-corruption gaps while
  cross-checking: ordinals registered in the dispatch table but missing their
  stdcall arg-byte-cleanup entry (`IoCreateSymbolicLink` and `NtCreateDirectoryObject`)
  leak however many bytes the real call used, corrupting `g_esp` for everything after.
  Rebuilt: that crash is gone, real ~2MB GPU buffer allocations happen (furthest yet),
  then one more crash at Xbox VA `0xFED00000` — just past the *end* of the 16MB NV2A
  MMIO range mapped in the seventeenth pass. Widened it to 48MB
  (`0xFD000000-0xFFFFFFFF`): that crash is gone too, but now the process runs to a
  60-second timeout with **no further crash and no further kernel-call log output** —
  a genuine hang. The last calls before it (`KeInitializeDpc`, `HalGetInterruptVector`,
  `KeInitializeInterrupt`, `KeConnectInterrupt`) are all unbridged stubs, strongly
  suggesting a GPU interrupt handler gets registered and then spin-waited-on forever
  — the same "needs real interrupt/async delivery this synchronous runtime doesn't
  have" architectural boundary as `KeWaitForMultipleObjects` and the PFIFO queue.
  Needs a deliberate decision, not another isolated fix.
  **Nineteenth pass: made that decision and implemented real interrupt/DPC
  delivery** — `bridge_KeInitializeInterrupt`/`bridge_KeConnectInterrupt`
  (previously unbridged, unconditionally returning FALSE, which is exactly what was
  causing the connect-retry hang) and `bridge_KeInsertQueueDpc` (runs the DPC
  synchronously on the calling thread, reusing `PsCreateSystemThreadEx`'s
  register-save/restore pattern — safe, no cross-thread register race). That hang
  gone, hit three more in the same family, all traced live via `gdb` rather than
  assumed: a PFIFO "kick and spin" register at a confirmed Xbox VA
  (`0xFD100410`), a ring-buffer PUT/GET wait with GET stored in a
  dynamically-allocated software cell, and a GPU fence-completion wait. All three
  are "no real command processor ever consumes/completes anything" wearing
  different clothes; fixed generally with one background thread
  (`xbox_pfifo_pump_thread`) that walks the same fixed GPU-context chain the driver
  itself resolves and makes each of these "instantly complete" every 1ms — touches
  only plain Xbox memory cells, never the shared CPU-register globals or
  recompiled code, so it's safe alongside the single-threaded execution model.
  All three gone; now blocked on a fourth, different-shaped issue: `sub_0016ED57`
  (device miniport init) requests a ~2.13GB contiguous allocation — confirmed via a
  conditional breakpoint on the requested size plus a full backtrace, not guessed —
  which legitimately fails and routes into an error-recovery path. Traced the bad
  size two tail-call hops back to a return value from a function doing D3D
  pixel-format nibble-unpacking (`sub_0016BFB0`); not yet traced further.
  **Twentieth pass: a second unbridged-ordinal audit eliminated the 2.13GB
  allocation entirely.** Cross-checked all eight remaining `no bridge` ordinals
  against the stdcall-cleanup table and found four more missing entries (same
  stack-corruption class as before): `ExFreePool`, `ExQueryNonVolatileSetting`,
  `HalRegisterShutdownNotification`, `HalReturnToFirmware`. Fixed all four (real
  bridges for the two with output parameters, explicit no-ops for the two that are
  genuinely harmless as such). Rebuilt: zero `no bridge` warnings anywhere in the
  run, and the 2.13GB allocation request is completely gone. New crash in the same
  `IoCreateDevice`-adjacent callback-dispatch loop, on a different array entry:
  traced it to a real, general translator bug (`ebp = g_seh_ebp` inheritance was
  gated to exclude any function with its own real `push ebp` prologue, even though
  that exact instruction needs the caller's ebp) and fixed it properly (verified in
  isolation, then a full codebase regeneration + swap). The bug is real and now
  fixed everywhere, but rebuilding proved it wasn't the cause of *this* specific
  crash — byte-for-byte identical crash before and after. Genuinely unresolved;
  next session starts fresh on what `MEM32(ebp+8)` should actually resolve to here.
  **Twenty-first pass found the real bug: a second, bigger ebp gap.** Three
  separate `lifter.py` call sites build "conditional tail call" code
  (`_lift_jcc`'s two cases, and `_emit_cond_goto` used by the far more common fused
  cmp/test+jcc matcher) and none of them emitted `g_seh_ebp = ebp;` the way the
  unconditional-jmp case always had — plus a matching `translator.py` gap
  (`has_tail_jump` only checked `jmp`, not conditional jumps, which would have been
  a compile error for any function whose only external branch was conditional).
  Fixed all four sites. `grep -c "g_seh_ebp = ebp;"` across the generated codebase:
  4,342 before either ebp fix this session → 8,904 after both — likely the
  highest-leverage fix of the session by instance count. Rebuilt: **the
  `sub_00180033` crash is gone**, confirmed via live `g_seh_ebp` inspection to be
  correctly bridged now. Progress moved measurably further into `sub_0016ED57`,
  reaching a second fence-wait (same shape, different field offset) — extended the
  pump thread to cover it, which didn't resolve it. Live tracing showed the value
  involved (`1`) plausibly isn't a bug at all — it may be a legitimate flag from
  `Renderer_InitializeD3DDevice`'s own hardcoded setup, meaning the "this should be
  a pointer" assumption behind the pump extension may itself be wrong. Genuinely
  unresolved; next step is establishing what that stack slot is actually supposed
  to mean at this point, not assuming it's another context pointer.
  **Twenty-second pass resolved it cleanly: `1` was never wrong.** One simultaneous
  `gdb` snapshot of every value in the comparison showed `context+0x2304` (with
  `context=1`) resolves through a real lookup table to `0xFD800000` -- a genuine
  address inside the mapped NV2A MMIO aperture, not garbage. `1` is a real channel
  index; the address resolution was always correct. Same "no real hardware"
  root cause as every other wait this pump handles, just a second channel — added
  it. That hang gone, reached a new one three frames deeper: a real polling loop
  blocking on `MEM32(0xFD000000 + 0x100) & 0x1000000`, confirmed live. Added that
  register to the pump too. Both fixed, reaching `sub_0016F6B0` — three levels
  further into the call chain than any run this session. There it hit something
  categorically different: a real x86 `in al, dx` (I/O port read, port `0x80C0`)
  with **no translation at all** (`/* TODO: in al, dx */`, `eax` silently left
  stale). Not a missing bridge or a pollable memory cell — a whole instruction
  class this tool doesn't emulate. Genuinely unresolved; needs the same kind of
  deliberate design decision the interrupt/DPC gap got in the nineteenth pass,
  not another isolated fix.
  **Twenty-third pass answered it and added real port I/O to the tool.** Port
  `0x8000` is Xbox's ACPI PM I/O base; cross-referenced against xemu's
  `hw/xbox/acpi_xbox.c` (a mature, hardware-accurate emulator, not a guess), `0x80C0`
  is the ACPI GPIO block, and its one implemented register is the TV encoder's
  "field pin" — toggles every read at bit 5, matching exactly what this call site
  does with the value. Added `XBOX_IO_READ*/XBOX_IO_WRITE*` macros +
  `_lift_in`/`_lift_out` in the lifter — a general capability, not a one-off patch —
  and implemented this one confirmed port faithfully (toggle on every read). That
  crash gone, reaching `sub_0016F6B0`: a "kick and wait for busy-to-clear" on the
  *same* register the twenty-second pass's pump fix had permanently forced SET to
  satisfy a *different* waiter's "wait for busy-to-become-SET" check — confirmed
  live both resolve to the same address. Switched the pump from force-setting to
  *toggling* that bit too, satisfying whichever polarity is actually being waited on
  within a tick. Result: **~2,900+ kernel calls before a 90-second cutoff**, up from
  a hard ceiling of ~53 every run this session before this fix — genuine, repeated
  work (`KeSetEvent` firing on real event objects), confirmed via live backtrace to
  be `sub_00170385`'s own outer retry loop re-entering `sub_0016F6B0`, not a dumb
  spin. Doesn't yet terminate naturally — the toggle is a real fix for the
  two-conflicting-waiters problem but only an approximation of the true hardware
  protocol, most likely why the outer loop doesn't converge the way it would on
  real hardware. Next step: find what that outer loop's *actual* exit condition
  gates on (probably one of the other status bytes checked earlier in
  `sub_00170385`, not the busy bit itself).
  **Twenty-fourth pass found it exactly and fixed it at the root.**
  `sub_00170385`'s only return path is gated behind two earlier checks
  (`esi+0x3214`, `esi+0x2400`, both bit `0x10`) that were structurally unreachable
  as *false* with zero-initialized memory — regardless of the busy bit, the function
  could never even reach its real exit check. These read as static capability flags
  a real GPU reports once at detection time, not toggling bits, so fixed with a
  one-time init (not a pump target) setting both bits right after the GPU MMIO
  region is mapped. Rebuilt: **the 2,900-call unbounded loop is gone**, execution
  moves on. Immediately hit a new, precisely-characterized blocker: a
  `MmAllocateContiguousMemoryEx` request for exactly `0x08000000` (`XBOX_TOTAL_RAM`)
  — confirmed live via conditional breakpoint, and confirmed `MmQueryStatistics`
  (the obvious suspect) was never even called. The failure cascades into the exact
  same error-recovery path chased at the *start* of this session (`sub_0016F220`),
  this time on the real persistent context whose fence struct never got populated
  because `sub_0016ED57` failed early. Confirmed via a genuine native run (not
  `gdb`) that this is a real 90+ second hang — a `gdb`-driven test session earlier
  in this pass had looked like a clean exit, which turned out to be an artifact of
  debugger-slowed timing, not real behavior, worth remembering given how
  timing-sensitive this session's toggle-based fixes are. Next step: trace `eax`'s
  origin at this allocation site (same shape as the twentieth pass's still-open
  2.13GB thread) rather than patching the fence-wait symptom again.
  **Twenty-fifth pass found the real root cause and fixed it, superseding the
  twentieth/twenty-fourth passes' "format parameter" theory** (that was a real
  methodology error — gdb native-register reads don't map reliably to C-level
  variables across compiler register allocation; switched to `fprintf`
  instrumentation in the generated C plus the live GhidraMCP HTTP server for
  ground truth against the *original* binary instead). The 128MB request traced
  to a valid `D3DPRESENT_PARAMETERS*` (`Renderer_InitializeD3DDevice`'s own stack
  struct) reading back as NULL ~200 instructions later, due to two stacked bugs
  in the same call chain (`D3D_InitMiniportAndFrameBuffers` → `sub_00170466` →
  `sub_001704A4`): (1) `stdcall_args_for_ordinal`'s entry for ordinal 44
  (`HalGetInterruptVector`) used the textbook 2-arg NT signature (8 bytes) when
  this title's actual call site pushes 3 (12 bytes, confirmed via raw
  disassembly — Ghidra's own decompiler dropped the third arg too), a 4-byte
  stdcall-cleanup shortfall on every call through that ordinal; (2)
  `sub_001704A0`, one of **239 "not detected" stub functions** in
  `recomp_stubs_unresolved.c` (all currently silent no-ops), turned out to be a
  reachable 3-byte orphaned fragment (`XOR EAX,EAX; JMP 0x00170523`) between two
  functions the tool did detect — its empty stub body skipped the real function's
  stack-cleanup epilogue entirely, permanently losing 24 bytes and shifting every
  later stack read in the caller. Fixed both (one-line kernel-table fix; real
  translated code for the stub). **Verified via clean rebuild: the 128MB
  allocation no longer occurs at all.** Immediately hit a new, different-shaped
  blocker: a genuine `KeWaitForMultipleObjects` (unbridged) busy-wait spinning
  hundreds of millions of times — real new work (a proper multi-object wait
  primitive), not a bug fix, left for a future pass. The other 238 unaudited
  stubs are a known, named risk for whoever picks this up next.
  **Twenty-sixth pass did that work and found three real, general fixes plus a
  genuine architectural limit.** Cloned `cxbx-reloaded` and `xemu` into
  `reference/` for future ground-truth lookups (not needed for these specific
  fixes). (1) Wired up the already-implemented `xbox_KeWaitForMultipleObjects`
  to ordinal 158. (2) The busy-loop's real cause: Xbox kernel dispatcher
  objects (`KEVENT` etc.) are structs embedded directly in game memory and
  referenced by address, with no `NtCreateEvent` call to intercept — the
  bridges were naively casting that address to a Win32 `HANDLE`, which Win32
  correctly rejects. Added a lazy VA→HANDLE resolver
  (`xbox_resolve_dispatcher_handle`) shared by `KeSetEvent`/
  `KeWaitForSingleObject`/`KeWaitForMultipleObjects`, guarded so real small
  handle values still pass through unchanged. **Busy-loop gone.** (3) The
  wait that replaced it was `D3DDevice_BlockUntilVerticalBlank` blocking
  forever on a VBlank event nothing ever signals — same root cause as every
  PGRAPH/PFIFO wait this session's pump thread already covers, just via the
  KE event path. Fixed by having the pump thread signal that event ~60×/sec
  through the same resolver (`xbox_signal_dispatcher_event`). **That wait
  resolves too**, ~100 kernel calls further. The *next* blocker turned out
  to be different in kind: attached `gdb -p <pid>` to the hung process and
  pulled `thread apply all bt` (a technique this session hadn't used before —
  return addresses from real `CALL` instructions proved trustworthy where
  register-value reads at breakpoints hadn't been). The backtrace shows a
  `PsCreateSystemThreadEx`-spawned "thread" nested *inside* another spawned
  thread's own C call stack, waiting on objects only a sibling thread could
  signal — a structural deadlock, because this runtime runs Xbox-spawned
  threads as synchronous nested calls on the one real OS thread (a known,
  deliberate earlier design choice), not genuine concurrent threads. Not a
  bug fix in the same sense as everything above — the real fix (genuine
  per-thread execution with per-thread register-global state) is a
  substantial runtime-architecture change, left as an explicit open decision
  rather than started uninvited. All three real fixes are general runtime
  changes in `kernel_bridge.c`/`xbox_memory_layout.c`, none touch
  generated/game-specific code.
  **Twenty-seventh pass implemented that open decision and confirmed it works.**
  Converted Xbox-spawned worker threads to genuine `CreateThread`s instead of
  nested synchronous C calls — made possible cleanly (no changes to the
  ~300K lines of generated code) because the key insight is that
  `g_eax`/`g_esp`/etc. are just plain globals referenced by name everywhere;
  declaring them thread-local (`__thread` — `__declspec(thread)` turned out
  to be silently dropped by this MinGW/GCC toolchain on `extern`
  declarations, a real bug, not just a warning) gives every real thread its
  own register state for free. Also made the `PsTerminateSystemThread`
  unwind stack and the kernel-dispatch-slot handoff thread-local (both were
  real, previously-latent races) and added locking around the heap
  allocator. `bridge_PsCreateSystemThreadEx`'s first call still runs
  synchronously on the main thread (unchanged, proven path); every
  subsequent call now spawns a real thread with its own stack, sized from
  the game's actual requested `KernelStackSize` (found by checking the
  real signature in the freshly-cloned `reference/cxbx-reloaded`). **Verified
  via `gdb -batch` + `thread apply all bt`, not just log output**: one
  thread is genuinely, independently blocked in a real
  `WaitForMultipleObjectsEx` while the main thread runs completely
  separately and reaches `Application_RunAndShutdown` — the actual game
  main loop, several stack frames past where execution used to be stuck
  forever. Immediately hit new territory: a real, *intentional* game
  assertion (not a recompiler bug) — `FILESYS_atomic` aborts because the
  game's own file-system layer believes `FILESYS_init()` was never called,
  with the original "FILE SYSTEM NOT INITIALIZED" message still embedded in
  the binary. Concrete, well-scoped next step: find why `FILESYS_init`
  isn't completing. All changes are in the runtime toolkit only
  (`kernel_bridge.c`, `xbox_memory_layout.c`, `xbox_winnt.h`,
  both `recomp_types.h` copies, `main.c`, `recomp_manual.c`) — zero
  generated/game-specific code touched, and a clean rebuild+run reproduces
  the same `FILESYS_atomic` abort deterministically.
  **Twenty-eighth pass fixed that abort for real** (it turned out to be a
  genuine recompiler bug, not a game-logic gap — the previous pass's Ghidra
  decompile covered the whole original function but didn't confirm which
  specific branch the crash backtrace actually landed in; the real message
  was "CALLED AT PRIORITY LOWER THAN CURRENT DEVICE PRIORITY", not "file
  system not initialized"). Root cause: `sub_00164410` (called with 4
  explicit stack args) only cleaned up its dummy return slot on both exit
  paths, leaking 16 bytes onto the stack — same bug class as two earlier
  fixes this session, this time in generated code rather than the runtime,
  fixed directly in `recomp_0008.c`. **Crash confirmed gone** on a clean
  rebuild. Ghidra went unreachable mid-pass (closed by the user); the rest
  of the investigation ran on generated C plus `gdb -p <pid>` alone, the
  same fallback this project has used before. Execution now proceeds much
  further — several worker threads processing what looks like a real
  resource-loading loop — before hitting a **new infinite loop**, confirmed
  live (not a wait — two gdb snapshots 5 seconds apart show the same PC) in
  `sub_00150BB1`, a tree-walking free-block search inside the game's own
  custom memory allocator (matches the "BXAudioSystem"-tagged pool pattern
  found earlier). Not yet root-caused — flagged as the next step, likely
  the same stdcall/callee-save bug class yet again.
  **Twenty-ninth pass traced it all the way to a single root cause, connecting
  it to the pool-10 hang directly, but didn't land the fix.** The "pool 10"
  tag (`ebx=0x1A`) traced back to `Application_RunAndShutdown` explicitly
  zeroing `ebx` then passing it to `Application_InitSubsystems` (the
  function that constructs every subsystem) — inside which `ebx` reads as
  `0` at 18 checkpoints spanning its entire body, with `esp` provably
  constant throughout, yet comes out as `0x1A` after its own final
  register-restore pop. Not the usual missing-`esp`-cleanup bug shape (no
  stack drift at all) — something writes `0x1A` directly into the *memory
  address* of the saved-`ebx` stack slot. Bisected by reading that exact
  memory location (not just the register) at each subsystem checkpoint:
  clean up through `WorldTriggerManager`'s constructor setup, corrupted
  immediately after — inside the same tagged pool allocator this whole
  investigation started from, this time on its *successful* allocation path
  (finding and carving a block from an existing pool) rather than the
  infinite-loop tree-search path. Both bugs are the same root cause,
  observed at two different times: a stack-corruption bug somewhere in the
  allocator's block-splitting chain (`sub_00150BE1`→`sub_00150C2F`→
  `sub_00150C44`→…) corrupts the caller's saved register during
  `Application_InitSubsystems`; since `ebx` is global and never reset, that
  corruption sits dormant until the main loop reuses it as a pool tag much
  later, hitting the never-constructed pool 10. Root cause narrowed to a
  specific, bounded function chain but the exact write hasn't been found
  yet — next step is the same "print the stack slot at each point"
  technique, one level deeper. No fix applied this pass; all instrumentation
  added and removed; confirmed the twenty-eighth pass's fix still holds and
  the hang still reproduces identically.
  **Thirtieth pass found the twenty-ninth pass's "block-splitting corruption"
  hypothesis was wrong, and landed the real fix.** Address-match instrumentation
  on the entire block-splitting chain found zero hits; re-deriving the watch
  address fresh (rather than reusing one computed earlier) showed the saved-`ebx`
  slot was already wrong *before* `WorldTriggerManager`'s allocation ever ran —
  the "provably constant `esp`" from the twenty-ninth pass was a checkpoint
  artifact, not reality. Per-call `esp` tracking through `Application_
  InitSubsystems` found a clean 0x2C-byte drop during one indirect call:
  `GfxContext_Init` (0x00104880). Reading its generated code end to end found
  two of its own tail-call targets, `sub_00104D36`/`sub_00104D38`, were empty
  `recomp_stubs_unresolved.c` stubs — silently dropping the rest of the
  function *including its own epilogue*, so its stack reservation was never
  unwound. Same stack-cleanup bug class as three earlier fixes this session,
  just at function-body scale instead of one wrong `esp +=` line. Disassembled
  the raw XBE directly (Ghidra still closed) via `objdump`, computing file
  offsets from the XBE's own section table; the gap was ~4000 bytes / ~1025
  instructions ending in a confirmed true epilogue right before the next
  already-correctly-lifted function. Wrote a small Python transpiler
  calibrated against the surrounding already-lifted code's exact idioms
  rather than hand-transcribing, verified all 19 call targets in the gap
  already exist elsewhere, caught and fixed one transpiler bug (`al` register
  alias), then inserted the two real functions into `recomp_0005.c` and
  removed the stubs. **Confirmed via clean rebuild + run: the pool-10 hang is
  gone** — execution now runs far past `Application_InitSubsystems`, into
  real heap allocations for texture/geometry buffers, before hitting a new
  (different, deterministic) blocker at an unresolved vtable target — flagged
  as the next investigation, not yet started.
  **Thirty-first pass fixed a second real hang** (`CDevice_KickOff` busy-waiting
  forever on a GPU register that was never real hardware — stubbed to clear
  instantly, matching this codebase's existing pattern for unbacked pieces),
  which unblocked real further progress into an actual NV2A push-buffer parser
  — but that then crashed on a garbage pointer. Traced the D3D8 device
  bring-up chain in detail via Ghidra's own named-function search
  (`Direct3D_CreateDevice` → `D3D_InitMiniportAndFrameBuffers` →
  `CMiniport_InitHardware`) and confirmed two real gaps: `MmMapIoSpace` is
  never called at all, and `xbox_nv2a`'s own MMIO fault-trap plumbing
  (`nv2a_hook_init`/`nv2a_hook_handle_mmio`) is never wired into `main.c` —
  only referenced in documentation. Assessed as a genuinely separate,
  dedicated GPU/HAL integration task, not a bounded bug — full trace with
  exact addresses left in `RE_NOTES_xboxrecomp_test.md` for whoever picks it
  up. **Thirty-second pass found the garbage-pointer crash was actually a
  second, smaller instance of the exact same missing-lift bug class**
  (`sub_00169516`/`sub_00169522`, two more empty stubs silently dropping a
  function's epilogue, corrupting the caller's `ebx` much later) — not
  related to the D3D8 wiring gap after all. Fixed and verified the crash is
  gone; execution now makes genuine further progress (confirmed via gdb:
  process memory grows from ~75MB to ~324MB, PC keeps moving) before settling
  into a *new* hang that — this time — really is the already-diagnosed GPU
  wiring gap: `D3DDevice_SetRenderState_Simple`'s "wait for command-buffer
  space, kicking the GPU to drain it" loop never exits, since nothing in this
  build actually consumes the buffer. Net progress this session: pool-10 hang
  → KickOff hang → pushbuffer-parser crash, each found and fixed in turn,
  now blocked on the GPU pipeline wiring already scoped above.
  **Thirty-third pass fixed the `CDevice_MakeSpace` ring-buffer hang and a third
  never-lifted fragment (`sub_0016B84E`) the same way as before, then discovered
  a pre-existing background thread (`xbox_pfifo_pump_thread` in
  `xbox_memory_layout.c`, from an earlier session) already solves the GPU/MMIO
  wiring gap at the runtime level — it was running the whole time, making the
  generated-code KickOff/MakeSpace patches redundant defense-in-depth rather
  than the actual fix (left in place, confirmed harmless).** Despite that,
  `D3D_g_pDevice` was still observed reading as 0 partway through
  `GfxContext_Init` — switched from checkpoint bisection to a **gdb hardware
  watchpoint** for a definitive answer and caught a genuine heap-buffer
  overflow: `sub_0016A290` (`D3DDevice_CreateVertexShader`) sizes a buffer from
  a "dry run" pass of the push-buffer opcode parser, then a later fill pass
  overruns it, clobbering `D3D_g_pDevice` several KB away. Root cause of the
  size mismatch not fully isolated; applied a safe 4KB over-allocation margin
  instead of chasing it further. **Result: for the first time this whole
  project, the game's main thread now runs to a clean, deterministic exit**
  ("Game returned. Cleaning up...", exit code 0, reproduced across 4 runs) —
  spawns and cleanly terminates ~9 worker threads first. Four distinct, real
  bugs found and fixed across this session's GPU-wiring investigation (pool-10
  hang, KickOff hang, pushbuffer-parser crash, buffer overflow), each isolated
  with the same toolkit: fprintf checkpoint bisection to narrow down *where*,
  then `gdb -p <pid>` backtraces or hardware watchpoints for a *definitive*
  answer once static reasoning stalled.
  **Thirty-fourth pass found the "clean exit" from the thirty-third pass was actually
  a premature bailout, and traced it to a genuine, systemic lifter bug affecting 13 call
  sites across the whole codebase.** `Application_RunMainLoop`'s quit flag (`this+0x24`)
  read as already-nonzero (`0x40`) at function entry — but `this` itself (`esi`) turned
  out to be corrupted, not a real `Application` pointer. Per-call `esi`/`esp` bisection
  traced the corruption backward through `Application_InitSubsystems` →
  `sub_000AA071` → `sub_000A9A10` → `sub_0014BDA0` → `FILESYS_atomic` → an indirect
  call reading its target from `[esp+0x1C]`. Checked every function in that chain
  against the raw XBE bytes (`objdump`) and all were byte-perfect translations — the
  real bug was one level up: the generated code for **any indirect call whose target is
  itself an esp-relative memory operand** (`call [esp+N]` / `jmp [esp+N]` in the
  original) emits `PUSH32(esp, 0); RECOMP_ICALL_SAFE(MEM32(esp + N), _icall_esp);` —
  two separate C statements, so by the time `MEM32(esp+N)` is evaluated, the preceding
  `PUSH32` has already decremented `esp`, making the read land 4 bytes off from the
  original instruction's operand (real x86 semantics require the memory operand to be
  computed *before* the CPU's own implicit return-address push). Confirmed by accident:
  debug instrumentation reading `MEM32(esp+0x1C)` *before* the push showed a
  plausible-looking function address, while the real macro call (reading it *after*)
  silently resolved to nothing — proving the two reads see different memory. Fixed all
  **13 occurrences** (2 in `recomp_0007.c`, 10 in `recomp_0008.c`, 1 in `recomp_0009.c`)
  by capturing the target into a local *before* the fake return-address push. **Result:**
  the game no longer takes the premature quit-flag exit — it now spawns 25+ worker
  threads (vs. ~9 before) and runs substantially further before hitting a new, different
  crash (illegal instruction) — genuine forward progress, not a regression, since the old
  "clean exit" was masking this deeper territory rather than actually completing a game
  session. **Same pass, diagnosed and fixed that crash too**: it was a literal original
  `INT 3` (`__debugbreak()`, lifted faithfully from real game code, not a lifter bug) hit
  on a "should never happen" file-op assert path, hard-aborting because this toolkit's
  `__debugbreak()` maps to `__builtin_trap()` by default. Changed it to log-once and fall
  through instead (`recomp_types.h` + `recomp_manual.c`, general fix, all 11 call sites
  across the codebase) — matches the existing `RECOMP_ICALL_SAFE` "degrade gracefully"
  convention. **Result: the crash is gone and the game now runs indefinitely instead of
  dying, but settles into a new hang** one worker-thread further in, inside what a gdb
  backtrace + CPU-sampling (~100% sustained, confirmed busy-spin not a blocked wait)
  showed to be the real CRT heap allocator's own free-block-tree search
  (`sub_00150BB1`/`sub_00150BE1`, block-header magic numbers `0x4253`/`0x424F`) —
  looping forever. **Same pass, diagnosed and fixed via the project's live-Ghidra REST
  bridge (`http://127.0.0.1:8080/decompile_function_by_address`) plus gdb memory
  inspection**: this is a real 16-pool size-sorted free-list allocator
  (`DAT_00203be0[16]`), and pool 0's descriptor checks out perfectly (sentinel magic,
  `0x7FFFFFFF` guard size, valid free-list) — but the hanging allocation
  (`Application_RunAndShutdown`'s `"PadCache"`-tagged call, the input subsystem's own
  dedicated pool) requests pool 3, which was **never created** — `DAT_00203be0[3]` is
  still zero. Following the NULL descriptor makes the search wander unrelated low
  memory instead of hitting a sentinel. Fixed with two matching NULL-descriptor
  fallbacks to pool 0 (`recomp_0007.c`), pragmatic rather than chasing why pool 3 was
  never registered. **Result: the biggest milestone of the entire project** — execution
  now reaches and continuously runs `Application_RunMainLoop` for the first time ever
  (confirmed via multiple gdb backtrace samples showing real, changing RIPs). Caveat:
  it's running but not yet progressing — 45 seconds in, the set of unique
  `[ICALL-MISS]` targets stays pinned at exactly 301, meaning the loop cycles through
  one stable state (likely front-end/menu tick or a loading-wait) without advancing.
  **Same pass, resolved the biggest chunk of those 301**: `objdump`-ing the range
  directly showed ~250 tiny, individually-real functions (`fld m32; f{div,mul,add}
  m32; fstp m32; ret`, 32-byte-aligned) that neither Ghidra nor the lifter fully
  discovered — no static xrefs found them. Wrote a Python generator matching this
  codebase's x87 emulation convention and added 225 missing ones (`recomp_0008.c`,
  `recomp_funcs.h`, sorted dispatch-table entries in `recomp_dispatch.c` — binary
  search requires strict order, verified across all 10,303 entries). **Unique
  `[ICALL-MISS]` count dropped from 301 to 76.** While reading a known-good example
  for the macro convention, also found (but didn't fix — far too large a surface)
  a separate, **project-wide systemic bug**: memory-operand x87 instructions in
  *already-lifted* code are sometimes translated with the wrong template (register-
  pair form instead of memory form, silently dividing by garbage) or dropped
  entirely (comment-only, ~403 instances) — likely affecting floating-point math
  throughout the whole game, flagged for a dedicated future pass. Also corrected an
  earlier misread this same pass: catching `ErrorScreen_TriggerDiscErrorFreeze` in a
  backtrace looked like a stuck disc-error state, but direct memory inspection
  confirmed the gating flag reads `0x00` (clear) — the main loop is genuinely cycling
  through varied per-frame logic, not stuck. **A third batch recovered 28 more
  functions** (looser splitter catching tail-call thunks, irregular FPU shapes, a
  `rep movsd` table initializer, and one function outside the cluster entirely) —
  which **caused a regression that turned into the pass's most valuable find**:
  dispatching all 28 crashed the game with `SIGSEGV` at ~200 kernel calls via
  unbounded `sub_00160E72` ↔ `sub_00160EBA` recursion. Bisecting the dispatch table
  entry-by-entry proved all 11 functions calling `sub_0015D044` (`atexit`) crash and
  all 17 others are fine — exposing a **pre-existing, previously-unreachable gap in
  this recomp's SEH/CRT-lock emulation** (`_onexit` at `0x0015D00C` recursing through
  the CRT lock table at `0x1C58D0`). Kept the 17 safe ones dispatched, left the 11
  lifted-but-undispatched with the evidence documented in `recomp_dispatch.c`, and
  deliberately did *not* hack out the `atexit` call — that would hide a real
  toolkit-level defect. Unique `[ICALL-MISS]` count across the pass: **301 → 76 → 60**,
  no crash, main loop still running.
  **Finally, traced exactly why the main loop runs but never advances, finding a fourth
  general lifter bug.** Mapped `Application_RunMainLoop` instruction-by-instruction and
  found its per-frame pacing gate is `MEM32(this+0x2C)`'s `vtable+0xC` — the
  XBoxExecutionMan frame-event wait — which reads **NULL**, degenerating the loop to
  "poll input, no-op, repeat" (and explaining the long-standing mystery `0x00000000` /
  `0x00000014` ICALL misses, which are just VA 0 and VA 0x10 dereferences). A gdb
  hardware watchpoint proved `app+0x2C` is never overwritten — instead **`esi` (the
  Application `this`) is destroyed inside `Application_InitSubsystems` by a stack
  imbalance**. Bisecting fragment-by-fragment found the largest contributor: a
  `VideoPlayer_Construct` loop inside `GfxContext_Init` leaking 8 bytes per iteration
  (180 total). Root cause: **the `_icall_esp` snapshot is scoped to the enclosing basic
  block rather than the call's own arguments**, so `RECOMP_ICALL_SAFE`'s miss-path
  restore discards the prologue's callee-saved register pushes too. Fixed mechanically
  by relocating each capture to just before the contiguous trailing argument-push run —
  **843 of 3,530 ICALL blocks relocated**; verified `GfxContext_Init` now returns
  balanced and the leaking loop's esp is constant, with no regression. **Honest status:
  the main loop still does not advance** — `esi` is still corrupted further down the
  same chain (`sub_000A9A10` → `sub_0014BDA0` → `FILESYS_atomic` → `FILE_size` →
  `sub_0014C440`), where every level verified so far is a faithful translation yet still
  over-pops. This is a *systemic* stack-discipline problem, and the suggested next step
  is a general esp-balance harness (assert `g_esp` at every lifted function's entry/exit
  and log the first offender) rather than further manual drill-down.
  **Thirty-fifth pass ran that harness and disproved the theory.** Built the suggested
  entry/exit `g_esp` guard (GCC `cleanup` attribute, so it fires on every return path).
  Two self-inflicted false starts first: expecting delta 0 when every generated function
  ends `esp += N` (N=4 for `ret`, 4+argbytes for `ret N`), then failing to exclude the
  **5,457 tail-call targets** — fragments jumped into mid-frame that legitimately pop a
  *predecessor's* pushes. The corrected harness guarded 1,012 genuine, internally
  balanced entry points and found **zero mismatches**: stack accounting is sound, so the
  "systemic over-pop" theory is dead and the corruption is a memory write, not an esp
  accounting error. Separately found a real bug: **`NtSetEvent` (ordinal 225) and
  `NtClearEvent` (186) were never bridged** — `xbox_NtSetEvent`/`xbox_NtClearEvent` and
  their thunk entries existed, but no `bridge_*` wrapper did, so `kernel_thunk_dispatch`
  fell through to its silent return-0 stub; events could be created but never signalled
  or cleared. Fixed both plus the missing `case 186` arg-bytes entry. Also bridged
  `NtWaitForMultipleObjectsEx` (235) in the same edit, **found it deadlocks startup** (a
  faithful blocking wait on an async file-I/O completion this layer never signals — gdb
  showed the main thread parked under `sub_0014E260`, never reaching the main loop) and
  deliberately reverted it, recording the reasoning at the dispatch case: a *more
  faithful* bridge is strictly worse until its completion side exists. **Result: the main
  loop now runs ~2,000 iterations/25s, but its `this` is `0x48000001` — a bridge
  *file-handle token*, not the Application object** (cf. the real `0x01510440`;
  `BRIDGE_HANDLE_TAG` is that top byte). Every field read off it is garbage, the frame
  poll returns 0 on all ~5,000 iterations, and **`Application_StateMachineTick` has still
  never executed once** (verified by instrumentation inside it). Two suspects ruled out:
  `DAT_001ba53c` reads **1** (correct — checked in the XBE image at file offset
  `0x1AA5BC` and live via gdb), and `sub_0014B570` is a bounded 16-slot software-timer
  callback scan, not a hang. Next: hardware-watchpoint the saved-`esi` stack slot to
  catch what writes a file handle into it.
  **Part five found a fourth real bug (cdecl vtable slot mistreated as stdcall) and
  advanced `this` from a file handle to a stack address, though not yet to the real
  object.** Inside `sub_00164530`, a vtable slot takes 2 real args (proven by a second
  call to the same slot elsewhere pushing/cleaning exactly 2) but a `RECOMP_ICALL_SAFE`
  miss reset esp as if it were stdcall (the macro's actual design target), corrupting
  unrelated register-preservation pushes sitting below the real args. Fixed by capturing
  `saved_esp` after the explicit args instead of before — call-site-specific, not a
  macro change, since most uses of the macro genuinely are stdcall-shaped. Net result
  across this whole investigation: `this` went `0x48000001` (file handle) ->
  `0x00F7FCF0` (stack address, after the part-two fixes) -> `0x00000000` (NULL, after
  this fix) — real, monotonic progress, but the state machine still hasn't run.
  Exhaustively traced `sub_000A9A10`'s remaining +8-per-call leak through nine more
  functions (`FILESYS_atomic` -> `FILE_size` -> `sub_0014C510` -> `sub_0014E5E0` ->
  `sub_0014E350` and its whole callee set -> `sub_0014E61B` -> `sub_001530DF` ->
  `sub_0014E643` -> `sub_0014E645`), and **every one measured correctly balanced** —
  the discrepancy has no single point along that path. Live probing points at
  `FILESYS_completeop` (an 11-entry jump-table dispatch, called right alongside that
  chain) having at least one more instance of the *same* cdecl/stdcall mismatch — its
  own entry-to-dispatch esp delta varied between calls reporting the identical dispatch
  value, which real control flow can't explain. Not isolated or fixed this pass. Given
  this exact bug class has now hit twice independently, the recommended next step is a
  systematic sweep (every `RECOMP_ICALL_SAFE` site with a follow-up `esp = esp + N`,
  checked for capture-point placement) rather than further one-at-a-time tracing.
  **Continued, same pass — mechanism traced and one real mistranslation fixed.** Probing
  `esi` through `Application_RunAndShutdown` found two corruptions, and direct `esp`
  measurement **corrected the harness result reported above**: that harness's
  `pushes != pops` filter excluded every function that pushes call arguments, so its
  1,012 "entry points" were mostly trivial and its zero-mismatch result proved far less
  than claimed. The stack-imbalance theory is in fact **correct** — `sub_000A9A10` pushes
  and pops `esi` properly, but callees leak `esp`, so its `POP32(esp, esi)` reads the
  wrong slot and restores whatever the FILESYS path left there (hence a file handle as
  `this`). Drilled to `sub_0014E260` and isolated two offsetting offenders:
  `sub_00164410` over-popped 16 bytes and `sub_0014B800` under-popped 12, netting the +4.
  **Fixed `sub_00164410`**: a previous pass had rewritten its epilogues to `esp += 0x14`
  ("ret 0x10"), but `objdump` shows both exits (`0x16443D`, `0x16444D`) are plain `ret`
  (`0xC3`) and its sole caller already cleans all 15 arg dwords via one `add esp,0x3C` —
  so the args were being cleaned twice. Reverted to the faithful `esp += 4`; re-measured
  and the +16 is gone. Remaining on this path: `sub_0014B800`'s −12, which comes from
  `sub_00154476` (must be stdcall `ret 0x18`; its real epilogue is in a tail-call fragment
  `sub_001544C3`/`sub_001544C6` whose cleanup amount needs checking). Main loop still does
  not advance. **Part three fixed two more leaks on the same path**: kernel ordinal 224
  (`NtResumeThread`) had no stdcall arg-size entry, so its 2 args leaked 8 bytes on every
  thread resume (found via a temporary diagnostic logging any thunk dispatched with
  `arg_bytes == 0`, then checking each — 4 of the 5 hit are genuinely 0-arg, only 224 was
  wrong); and **six jump-table arms of `sub_0014B730`** (table at `0x0014B7BC`) were never
  lifted at all, so the indirect jump missed and returned without popping its fake return
  address, leaking 4 bytes per hit — all six generated and dispatched. Each fix confirmed
  by re-measuring the predicted esp delta. **Net: `this` at `Application_RunMainLoop` went
  from `0x48000001` (a file-handle token) to `0x00F7FCF0` (a stack address)** — the handle
  corruption is gone and three real bugs are fixed, but `esi` still isn't preserved end to
  end, so the state machine has still never run. At least one leak remains upstream. **Part four re-measured the whole chain**: `sub_0014E260` is now **balanced end to end**, and the disputed `add esp,0x3C` was confirmed by counting the original's pushes byte-for-byte (exactly 15 arg dwords), independently validating the `sub_00164410` revert. The remaining leak is pinned to the callback ICALL inside `FILESYS_atomic`, which over-pops **40 bytes**; the target resolves correctly to `FILE_size` and `FILE_size`'s own prologue/epilogue are faithful, so the culprit is one of its three callees (`sub_0014C440`, `sub_0014C510`, `sub_0014C4D0`) — the precise next target. Also caught a near-regression: the probe-stripping script deleted a whole line that contained both a probe tag and a real `RECOMP_ICALL_SAFE` call, removing an indirect call while still compiling; restored, and the 13-site `_icall_tgt` count is now a standing post-cleanup invariant. **Part six** ran a systematic sweep for the same cdecl/stdcall `RECOMP_ICALL_SAFE` mismatch across all 10 `recomp_00XX.c` files (45 more sites fixed), which surfaced a real regression from this window's own probe-cleanup work: a bad removal regex had silently deleted six real lines out of `Application_InitSubsystems` (including the entire `GfxContext_ConstructSingleton` call and two `esp = esp + 0xC` cleanups), restored from a pre-probe backup. **Part seven** finally got `this` correct at `Application_RunMainLoop` (`0x01510440`, not NULL) by fixing two more lifted-fragment bugs: `sub_00169522`'s manually-reconstructed epilogue used `esp += 4` instead of the `esp += 8` every other "ret 4" epilogue in the codebase uses, and `sub_00116650` was a completely untranslated "not detected" stub (one of 239 in `recomp_stubs_unresolved.c`) that leaked 4 bytes on every call by cleaning up nothing at all — hand-translated from the XBE bytes. Also bridged `NtWaitForSingleObjectEx` (ordinal 234), the very next kernel call `Application_RunMainLoop`'s body makes; it's real and correct, but the game now spins waiting on a vsync/frame-ready-style event nothing yet signals. **Traced one layer deeper, same pass**: found `bridge_read_handle()` double-dereferences every by-value `HANDLE` argument in this codebase's entire history (`NtSetEvent`, `NtReadFile`, `NtWriteFile`, and 7 more), so every such call has always silently operated on a NULL handle — confirmed live (the fix resolves to real, correct Win32 handles; the current code always resolves to NULL). Fixing it is correct in isolation but exposed a second, deeper problem — `PsCreateSystemThreadEx` stores *raw* native thread `HANDLE`s for a separate resolution path, so two incompatible handle schemes are in flight, and enabling the fix turns "spins safely" into a deterministic hang at worker-thread spawn #35. Reverted rather than ship that regression; full reasoning is a comment on `bridge_read_handle` itself and the detailed writeup is in `RE_NOTES_xboxrecomp_test.md`'s part seven follow-up — this handle-scheme reconciliation is now the single most concrete next task. **Narrowed
further, same session ("continue" pass)**: re-applying the fix with thread-tagged tracing
pinpointed the exact hang to `NtWaitForSingleObjectEx` blocking forever (`INFINITE`
timeout) on token `0x48000001` -- the very first event this build ever creates
(`type=1 init=0`, manual-reset, initially unsignaled) -- which no `NtSetEvent` call
touches anywhere in the entire boot trace up to that point. Traced the worker-thread
dispatch chain (`sub_001543DE`, a generic CRT thread trampoline shared by every worker;
`sub_0014B670`, a thread-pool job dispatcher reading a 4-field job descriptor and calling
a per-job function pointer) far enough to identify this as a thread-pool "ready"/
"job submitted" signal, but stopped short of identifying which specific job or
pool-management path is responsible for setting it -- that requires tracing every reader
of the stack slot `NtCreateEvent` wrote the token to (`handle_ptr=0x00F7FCB4`-equivalent),
which needs either a live watchpoint or a dedicated Ghidra pass. Reverted the fix and
tracing again to preserve the verified working state. **Correction via two gdb
backtraces (same pass)**: the "thread-pool job" lead was a dead end. A backtrace at the
very first `NtCreateEvent` call proves this event is created directly by
`Application_ArmFrameTimer` (the shared `XBoxExecutionMan` frame-sync event, exactly as
`RE_NOTES_application_boot.md` already documented) — not a distinct pool signal. A
backtrace at the wait, with the handle fix briefly re-applied, proves execution sails
straight through the rest of boot into `Application_RunMainLoop` and blocks on exactly
this one event — the earlier "hang at worker-spawn #35" was just where a short log
capture happened to end, not a second blocker. **What's actually missing**: grepping the
whole runtime for `timeSetEvent`/`XAPILIB` returns zero matches — there is no periodic
multimedia-timer subsystem in this recomp at all, so nothing can ever call the
`NtSetEvent` that's supposed to signal this event every tick. Fixing the handle bug alone
is necessary but not sufficient. **Retracted immediately, same pass, on closer reading**:
decompiling `Application_ArmFrameTimer`'s actual call sequence shows there is no missing
kernel/XAPI timer to bridge at all -- `sub_00152230` is the game's *own* fully-translated
software timer queue (a 63-slot critical-section-guarded array), which lazily spawns a
persistent worker thread (confirmed via the literal `0x1543DE` trampoline constant it
pushes as `PsCreateSystemThreadEx`'s `StartRoutine` argument -- the same generic CRT
trampoline seen at the top of every worker backtrace this session) to pump it.
`bridge_KeSetTimer` (ordinals 149/150) is a real stub but is unrelated -- this mechanism
never calls it. **Second correction, verified with live gdb hit-counts, not more
guessing**: the pump thread does not exit either. It's the single, global, lazily-spawned
timer thread (`PsCreateSystemThreadEx #2`, `ctx1=0x00152042`, created at the very start
of boot), and it demonstrably works -- 63 real `KeInitializeTimerEx` calls, then a
genuine `KeWaitForMultipleObjects` cycle that a gdb breakpoint count caught firing 3
times in 30 seconds. The main thread's `NtWaitForSingleObjectEx`, by the same method,
fired once and never again in that same 30s (and separately over a 90s plain run) -- a
real, permanent block, not a timing fluke. Since 3 wakeups in 30s (~10s apart) doesn't
match a 16ms frame-timer period at all, the sharpened conclusion is: the pump thread is
alive but never gets told about the newly-armed frame timer -- point the next pass at
`sub_00152230`'s post-insert notification calls (`0x187370`/`0x18736C`/`0x18732C`) to see
whether the "wake the pump thread early" signal is sent/received correctly, a narrow
three-call check rather than an open-ended mystery. The `bridge_read_handle` fix is still
required regardless (without it the wait never even resolves a valid handle to block on).
Full detail in
`RE_NOTES_xboxrecomp_test.md`'s part seven follow-up (including the retraction).
**Part eight resolved the 3-call plan and found three real bugs, fixing all three**:
`KeQueryInterruptTime` (ordinal 125) and `KeSetTimer`/`KeSetTimerEx` (ordinals 149/150)
both had zero implementation (the latter a deliberate no-op stub, "timer functionality
not needed for basic execution") despite fully-working real implementations already
existing in `kernel_hal.c`/`kernel_sync.c` -- wired both up, reconciling
`KeInitializeTimerEx`'s own event creation with the separate `xbox_resolve_dispatcher_handle`
VA cache so a timer's signal and a waiter's wait resolve to the same native handle. Also
found and fixed a dropped-borrow bug in a 64-bit subtraction (`sub_001521B3`, the
"`_cf` never actually computed" pattern -- confirmed via a live probe showing two
due-times differing by exactly `0x100000000`). **Result: the entire frame-timer signal
chain (`Application_FrameTimerCallback` -> `XBoxExecutionMan_SignalFrameEvent` ->
`NtSetEvent`) fires end-to-end for the first time in this project's history**, confirmed
via `gdb` backtrace -- it's just mistimed (fires continuously instead of ~60/sec), traced
to a *different*, not-yet-found bug in the timer's period-multiply input chain
(`sub_00152230` -> `sub_0015CF30`/`sub_0015CF49`, a correctly-translated `_allmul` fed a
still-unidentified wrong input on the timer's second arm). All three fixes are safe to
keep (verified via repeated `gdb`-backed runs: no crash, no hang, just a mistimed but
architecturally-correct spin). The same "`_cf` never computed" pattern appears 27 times
total across the codebase -- a well-scoped systematic-sweep candidate for a future pass,
same shape as part six's cdecl/stdcall sweep. **Follow-up, same pass -- probably the
single most important open finding in the project, not yet fixed**: fixed two more real
x87 mistranslations while tracing the multiply's wrong input backward (the frame-time
accumulator's `fsubr`/`fadd`/`fmul` memory-operand ops, and a `CRT_ftol_TruncateToInt64`
64-bit `fistp`/`fild` truncated to 32 bits), but neither fixed the observed symptom.
Live bridge-side probing found why: **every translated function's simulated FPU stack
(`_fp_stack`/`_fp_top`) is a local C variable, freshly zeroed on every function entry --
there is no global/`__thread` bridge for it, unlike `ebp` (which has one, `g_seh_ebp`).**
Confirmed directly: a caller computes a real value (`fp_top()=33.333332`) and calls
`CRT_ftol_TruncateToInt64` (the CRT's `_ftol`-style helper, whose whole *purpose* is
converting a value the caller left on the real x87 stack per standard MSVC ABI) -- the
callee's `fp_top()` at entry reads `0.000000`. `CRT_ftol_TruncateToInt64` alone is called
**486 times** across the generated code; if most of those callers also rely on this
cross-call FPU-stack convention (plausible, since that is what the helper is for), most
float-to-int conversions in the entire game could have always silently computed from 0.0
instead of the real value -- physics, timing, scoring, anywhere float math feeds an int
cast. Deliberately **not** fixed broadly this pass: the real fix (promoting the FPU stack
to `__thread` global storage) is a large, project-wide change with real regression risk
for functions that correctly rely on a fresh per-call stack, and needs its own dedicated,
carefully-scoped investigation and prototype rather than a same-session addendum. Full
detail, including the recommended 3-step plan for a future pass, in
`RE_NOTES_xboxrecomp_test.md`'s part eight.
**Part nine implemented the scoped fix and it works.** Added `g_ftol_arg` (single-value
bridge, set by all 491 `CRT_ftol_TruncateToInt64()` call sites from their own local
`fp_top()`) and `g_ftol_fp_stack`/`g_ftol_fp_top` (a shared stack for the 5-function
lifter-split fragment chain, mirroring the `g_seh_ebp` pattern). This alone left 10 call
sites across 4 functions with no local FPU stack at all (leaf functions returning float
via real ST(0), and same-function branch/tail-call fragments) -- rather than hand-tracing
each to its root, added a general `g_x87_st0` thread-local mirror that every one of the
~2032 local `fp_push` macro definitions now also writes through (purely additive, safe
project-wide via mechanical `sed`), and pointed the 10 orphan reads at it. **Verified via
live probes and `gdb` breakpoint-hit counting**: the frame-timer accumulator now ticks
cleanly at ~16.67ms/step (was always `0.0`), `KeSetTimer`'s due-time is now a clean
monotonic ~60fps sequence (was corrupted by `2^32`-scale jumps), and most importantly --
**`bridge_NtWaitForSingleObjectEx`/`bridge_NtSetEvent` now hit 102/127 times over a real
90-second run**, versus part six/seven's "fires exactly once, never again." The frame-sync
wait/signal loop **cycles repeatedly for the first time in this project's history**, not
yet real-time-paced (~1.1/sec vs 60/sec target, an interpretation-overhead question, not a
correctness bug). One side finding not yet root-caused: a short, bit-identical-repeating
sequence of 8 `CRT_ftol_TruncateToInt64` calls with rapidly growing values (up to
`~2.3e19`), most likely a real hash/checksum routine unmasked now that real values flow
through, flagged for a future dedicated look. Full detail in
`RE_NOTES_xboxrecomp_test.md`'s part nine.
- **`RE_NOTES_level_script_system.md`** — **the big one.** The level scripting/trigger
  system: a 24-opcode dispatcher (`Script_DispatchOpcode`, was `FUN_00049960`) that
  creates every interactive/scripted object type in a level (boost pads, trick zones,
  particle emitters, animated textures, crowd boxes, movie triggers, etc.) from a
  data-driven command stream. Contains: the full opcode table (all 24 read, 20 to
  behavioral detail), **the base-class architecture** (every node is inserted into a
  global hash registry keyed by type ID, `NodeRegistry_Insert`/`Remove`, supporting
  both per-placement re-trigger and per-type bulk iteration), a correction to an
  earlier assumption (`param_1[10]` is the raw level-authored command-record pointer,
  not a separate collision-volume object — position/flags live once in the level data,
  not duplicated at runtime), **a second, outer bytecode VM** (`ScriptVM_*`, found this
  session live in Ghidra) that drives menu flow and cutscene sequencing and calls into
  `Script_DispatchOpcode` as just one of its own ~27 opcodes, **`TrickTrigger`'s
  actual runtime behavior** (`TrickTrigger_Update`, also found live this session — the
  "trick complete → fire a payload script" mechanism), a **sweep-and-prune broad-phase
  collision system** (`SweepPrune_*`, shared by every spatial/trigger node type, not just
  TrickTrigger — the likely actual "player enters a trigger volume" detection mechanism
  for the whole engine), **8 more `ScriptVM_*` opcode handlers** (camera shake,
  force-feedback, cutscene camera warp, rail-grind state, HUD callouts) bringing the outer
  VM's opcode survey to ~25/27, a fully-resolved **`GameMode_*` dispatch cluster** —
  a real x86 jump table (`GameMode_PlayStartupScript`) and a matched
  `NoCountdown`/`StartCountdown` pair, which together nail down the game-mode global's
  (`GameMode_Current`, was `DAT_001dec94`) exact value table and correct every earlier
  "gated on mode 3/5 (cutscene/replay)" guess to **ShowoffMode**, the **player/rider
  object itself** (`Rider_ConstructBase`/`Player_Construct`/`OtherRider_Construct`, type
  `0x3ef` — confirmed the exact type `TrickTrigger_Update` checks for), and **the
  complete race/tutorial lifecycle state machine** (`RaceState_SetState`, 11 named
  states spanning both a race flow and a previously-undocumented in-game tutorial flow,
  plus the actual spawn/teardown/ranking functions that drive it). This is the
  highest-value subsystem found so far for a port — reimplementing this dispatcher and
  registry (even with placeholder per-type logic) would let a port correctly place and
  manage every interactive element in a level, and the race state machine alone is close
  to enough to reimplement the entire race flow.
- **`RE_NOTES_frontend_menu_map.md`** — map of the front-end (menu) system: 78 UI-widget
  allocator tags across 39 functions, organized by screen (character/board/track select,
  DVD/jukebox extras menu, rider bio, pause menu, rank/medals, name entry, credits) plus
  the generic widget primitives (buttons/dialogs/lists/sliders) everything else composes
  from. Mostly unverified (tag-name statistics, not read line-by-line) except where noted.
  **Complete (later session)**: the last 4 flagged-as-too-complex widget helpers named
  at tag-confirmed confidence (`UI_BuildTrickSelectPanel`/`UI_BuildHelpOverlay`/
  `UI_BuildTitleHelp`/`UI_BuildDeleteRiderConfirm`) — every function originally flagged
  in this investigation now has a name. **Then mapped `Widget`'s own real vtable
  methods** (11 more renames) — a property-cascade system (setters that propagate to
  named child widgets automatically), a 6-state transition-animation state machine
  (`Widget_UpdateTransitionAnimation`), and a per-frame layout function.
- **`RE_NOTES_results_screen.md`** — one function (`UI_BuildResultsScreen`) read in full;
  also documents a correction (it was originally mis-flagged as a script interpreter due
  to address proximity — the real interpreter is in the level-script-system notes). Also
  has the `TrackTable`/`shortCode` writeup and its resolution (see
  `RE_NOTES_race_hud.md`'s neighbor finding below).
- **`RE_NOTES_loading_screen.md`** — the loading/splash screen system. Decodes the
  `.xsh`/`"SHPX"` EA Gimex texture-bank format (112 files, the most numerous asset
  type), then maps the ~9-class full-screen picture-screen family (vtables
  0x001a76d8..0x001a7a68) sharing a `ScreenBase_*` base, plus the race loading screen
  (mode+track+rider pictures from `xboxload.big`) and the dual-rider/versus loading
  screen. Yields the definitive 12-character roster in index order (resolving Mac/Marty)
  and cross-confirms the GameMode enum.
- **`RE_NOTES_rider_update_chain.md`** — a dedicated deep-dive into `Rider`'s real
  polymorphic vtable (found by tracing `Rider_ConstructBase`'s `*param_1 = ...`
  assignment, distinct from the many per-subsystem tuning-data pointers set earlier in
  the same constructor). 16 renames: the destructor pair, two comparison predicates,
  and — the important part — `Rider_UpdateSubsystems`/`Rider_ResetSubsystemBuffers`
  (the master per-frame update dispatcher, 16 of its 18 subsystem calls are stripped
  no-op profiler markers in this retail build) and `Component_UpdateAll` (a generic
  attached-component-list iterator, called 3x/frame — the one surviving real physics
  call). Checked `OtherRider`/`Player`'s vtable overrides too — same pattern, no extra
  logic found even in the human-input path. **Honest conclusion: the real-time
  trick-scoring logic is confirmed NOT in Rider's C++ class hierarchy** — likely
  either in an attached component object (via `Component_UpdateAll`'s list, `this`
  pointer not resolved) or in the level-script VM instead. **Follow-up: an exhaustive
  `/search_bytes` sweep of the entire binary for the literal `+0x5710` displacement
  (11 hits, every one checked by hand) found zero write instructions anywhere** — a
  rigorous, near-conclusive result that the score field is written through a
  runtime-computed offset (very likely data-driven via the level-script VM's command
  records) rather than any hardcoded C++ line. Checked and ruled out two concrete
  candidates: the `Counter` opcode (0x06, a self-contained countdown timer) and the
  vtable-dispatch opcodes 9/10/0xb (fixed hardcoded vtable slots per opcode, not a
  data-driven generic setter as first hypothesized — disproven with direct evidence).
  The exact write mechanism remains unidentified, but the search methodology is now
  exhausted for this session. **Follow-up (later session): closed out the last 3
  unresolved vtable slots** (2 were still unanalyzed `LAB_` labels never even
  disassembled). Slot 2 → `Rider_UpdateWorldSpaceMarker`/`Rider_DrawWorldSpaceMarker`,
  a substantial floating-nameplate/marker draw function with an AABB visibility
  check. Slots 5/6 are plain getters; slot 5 confirmed as a sibling of the
  `+0x20` comparison-predicate family (that field is the `NodeBase` instance-ID
  field). Every Rider vtable slot is now named. 4 more renames. **Follow-up:
  found this shared slot family (3/4/5/6/10/11/14) is NOT Rider-specific** —
  confirmed byte-identical across `TrickTrigger`/`Boost`/`Fence`'s own vtables
  too (13 of 16 slots shared verbatim). Corrected the earlier `Rider_`-prefixed
  names to a generic `Node_` prefix, and named each class's own distinct
  Update/Destruct slots — including genuine, substantial logic in
  `Boost_Update` (the real overlap-detection + boost-application code) and
  `Fence_Update` (neighbor-segment timer logic). 15 functions touched (11 new,
  4 corrected). **Then extended again to Roller/Cracked/Timer/Debounce — the
  shared footprint holds in all four; named each Update/Destruct pair (8 more
  renames), superseding two old statistical guesses (`New_Script_6`/`7` →
  `Timer_Update`/`Cracked_Update` — the "spawns a Script when it fires" tag
  pattern already seen for `TrickTrigger_Update`). Key cross-class find:
  command-record column 4 is confirmed as the standard "attached script
  handle" field (same `ResourceContext_GetTableField(cmd,4)` →
  `ScriptVM_CreateByHandle` idiom in TrickTrigger, Cracked, AND Timer).**
  **Then swept every remaining node type with a real vtable** (UVScroll/
  TexFlip/Movie/ZBoost/AnimObject family/Particle/cMeshAnim, 18 more
  renames) — essentially every level-script node type from the opcode table
  now has verified Update+Destruct. Side-find: `CrowdBox`'s vtable is
  literally the same address as `CameraAudioPanningMode`'s — two unrelated
  2-slot vtables byte-identical in content, linker-folded into one shared
  location (same phenomenon as the generic no-op stubs, applied to a whole
  vtable this time). **Then closed the last two node types**
  (`AnimCombo`/`UnknownOpcode09`/`UnknownOpcode0d`, 6 more renames) — **every
  one of the 24 node types in `Script_DispatchOpcode`'s opcode table now has
  a verified Update and Destruct method.** 29 renames across this whole
  multi-part vtable-sweep thread, a complete architectural mapping of the
  level-script object system's per-frame lifecycle.
- **`RE_NOTES_application_boot.md`** — traced `InputManager`'s owner all the way to
  the root of the game. **Confirms `DAT_001e3c7c`'s identity** (the "resource
  context" referenced constantly throughout this whole project) — it's the
  `Application` object, proven via a literal `"data/lang/american.loc"` load call at
  `+0x50`. Maps the full chain: `Application` → `Application_StateMachineTick` (the
  top-level Boot/FrontEnd/InGame state machine — **the actual site where
  `GameMode_Current` gets set**, closing another long-standing "where does this get
  initialized" gap) → `InGameState` (confirmed to be a loading/transition manager,
  not the race object) → `AIWorld` (a genuine per-frame system, but for terrain
  streaming, not gameplay). **Then found `WorldTriggerManager` — the first confirmed
  per-frame consumer of a resolved input action code anywhere in this project**:
  `WorldTriggerManager_Update` (called from `InGameState`'s regular per-frame tick)
  walks 40 active trigger-instance slots, each checking its stored action code
  against the exact ASCII-letter codes `Input_ResolveActionCode` produces for
  grab-trick combos, then resolves and plays a trick voice/SFX cue
  (`Trick_ResolveSoundCueID`). Closes the input-pipeline loop with a concrete
  gameplay consequence (audio feedback), even though it's not the score increment.
  Also found `ShapeManager` (tag `"shpMngr"`, 28-byte slot stride exactly matching
  `IconAtlas_GetEntry`'s indexing — very likely the `.xsh` texture-sheet consumer)
  and `AggressionManager` (a 12x12 rider-pair relationship matrix — `Buddy`/`Friend`/
  `Rival`/`Enemy` tiers, tying directly to `constant.loc`'s symbolic names — driving
  what's very likely crowd/commentary reactions). 25 renames total this round.
  Attempted to find `WorldTriggerManager_Update`'s second caller in an unanalyzed
  code region and deliberately backed off after hitting the same false-positive
  class documented project-wide (raw byte scanning mistaking a `CALL`'s displacement
  bytes for a `RET`) — a careful non-decision, not a stall. **Then read
  `InGameState_LoadLevel` in full and resolved `OverlapManager`** — a "good next
  thread" explicitly flagged in an *earlier* session's sweep-and-prune notes: what
  consumes the broad-phase overlap-pair records. Fully mapped the end-to-end
  collision pipeline (`SweepPrune_MaintainAxis` → `SweepPrune_ToggleAxisOverlap` →
  `OverlapManager`'s packed 256-object pairwise flag matrix → each object's own
  `Update()` polling its overlap list — confirmed poll-based, not callback-based).
  7 more renames. **Then, recovering a second unanalyzed code region (carefully —
  verified neighboring functions stayed intact), found `ReplayManager`** (the
  instant-replay recording system, ties to the `"Replay Full"` localized string
  found earlier) and, by following its caller chain, **found and confirmed
  `InGameState_TickFrame` — the actual master per-frame gameplay tick, the closest
  thing to "the top of the per-frame race loop" identified in this entire
  project.** This required correcting an earlier characterization of
  `InGameState_LoadingDispatch` (it does double duty as both the per-frame
  dispatcher *and* the loading/results-screen selector, not just the latter).
  `InGameState_TickFrame` itself doesn't touch rider-array fields directly, and
  **all 5 of its immediate sub-calls were read to completion** — every one turned
  out to be HUD/overlay-state management or a generic utility helper, none of them
  dispatching per-rider gameplay updates. A genuinely valuable negative result: it
  rules out `InGameState_TickFrame`'s own call graph as the score-writer's location.
  The likely explanation: per-rider updates are driven by a separate call chain
  entirely, issued from the low-level game loop rather than through `InGameState`'s
  "UI-flavored" per-frame work — a fresh starting point for a future session rather
  than another layer of "read what X calls." 7 more renames. **Then tried two more
  angles, both confirmed dead ends**: a generic `NodeRegistry_UpdateAllOfType`
  dispatcher exists but is only used for checkpoint/replay node categories, never
  riders (4 renames); and traced `Application_ConstructAndInitInput`'s one caller
  into what's almost certainly MSVC CRT startup code, confirming `Application`
  really is the top of the game-logic hierarchy with nothing further to map above
  it. **Then found the likely real answer to the whole score-writer question**:
  when a race starts, `GameMode_PlayRaceModeScript` calls
  `Script_PlayByName("RaceMode")`, resolved via `ScriptTable_ResolveNameToID`
  against a **runtime-loaded** table of script names (not present in `default.xbe`
  or any `.big`/`.cml` file checked). This strongly suggests trick-scoring is
  **level-script data**, executed generically by the already-documented
  `ScriptVM_DispatchOpcode` interpreter — meaning the exhaustive C++ search never
  found a write instruction because there genuinely isn't one to find; the actual
  next step is decoding the model-archive wrapper format to extract the script
  bytecode itself, a different kind of task entirely from more decompilation.
  **Then a new GhidraMCP endpoint (`/get_symbol_status`, round 4) was added
  specifically to verify renames actually land** — immediately found and fixed a
  real bug in `renameData` (round 5: it silently no-op'd on any address without a
  pre-existing Ghidra-recognized `Data` type, which affected
  `VoiceAssetKeyTable_TrackGari` from earlier this session; fixed, re-applied,
  confirmed `USER_DEFINED`) and a separate gap (`TrackTable`'s rename was correct
  live but had never been added to the deliverable script at all — fixed). Also
  closed out `TransitionEffect`, the last unexplored tagged boot subsystem (the
  screen/menu wipe controller, 4 renames).
- **`RE_NOTES_player_snapshot_system.md`** — a previously-untraced "New_X"
  tagged allocator (`New_PlayerSnapShot`, statistically tagged since an early
  session, never followed up) turned out to be one piece of the game's
  **complete save-game/profile-save pipeline**: `PlayerSnapShot_*` (per-player
  state, tagged-chunk binary format with pointer→index resolution for
  portability) feeds into `SaveGame_TickSerializationStateMachine` (a 17-state
  incremental serializer, 16KB/call, spread across frames to avoid a hitch),
  which `SaveGame_TickAndFlush` drives and `SaveGame_FlushToDevice` writes to
  the actual storage device via vtable dispatch. Found via the classic
  RET+NOP-padding boundary scan for one previously-unanalyzed link in the
  chain. **Follow-up: read all 17 states of the state machine to completion**
  — a full, well-understood Xbox save-file format (header, checksum, a
  bounded data-record section, an object-list chunk type, per-player
  compressed profile/icon chunks, the `PlayerSnapShot` container, padding to
  a fixed ~510KB total size, final checksum). Confirmed a family of 5
  sequential tagged-chunk magic numbers (`0x11111111`-`0x11111115`) making up
  one coherent format. 19 renames total. Partial follow-up: confirmed the
  3620-byte data records (state 3) are embedded inline in the save-context
  object itself, and the state-5 object-list uses a self-referential circular
  list -- neither record's exact contents resolved. **Follow-up: identified
  the save-context object's own class.** Found via `xrefs_to` across the
  vtable's whole byte range (not just the one known slot) that
  `SaveGame_TickAndFlush` is actually slot 4, and traced the real constructor
  (`SaveOverlay_Construct`) to `OverlayManager_Construct` — a previously
  undocumented top-level manager allocating 19 distinct `"Overlays"`-tagged
  HUD/menu panels, called from the already-documented `InGameState_LoadLevel`
  (closing a loose "OverlayNode" tag thread from an earlier session). The
  save-game state machine is the internal state of the "Saving..." progress
  UI overlay, not a freestanding writer. 3 renames. **Follow-up: identified 6
  more of the 19 panels** via `Localization_ResolveString` IDs looked up
  directly in the already-decoded string dump —
  `WorldCircuitNextRaceOverlay`/`WorldCircuitResultsOverlay` (tournament
  bracket), `NameEntryOverlay` (rider name input), `UnlockNotificationOverlay`
  (post-race unlock popups), `ReplayTitleOverlay`, `PauseHudDetailOverlay`. 7
  of 19 panels now named with high confidence (exact string content, not
  guesswork).
- **`RE_NOTES_rider_event_system.md`** — a large, previously entirely
  undocumented rider-lifecycle event dispatch system, found while tracing
  `VenueStaging_EnterNamedState`'s callers. Two sibling ~20-case switch
  statements (`RiderEvent_DispatchTypeA`/`TypeB`) driven by a canonical state
  setter (`RiderEvent_SetState`), connecting directly to already-documented
  code (`Rider_UpdatePhysicsState` — the per-frame rider tick, corrected from
  the earlier mislabel "Rider_TeardownSubobjects"; and `Rider_NotifyLifecycleEvent` — a
  function referenced-but-never-named in an earlier session's
  `GameMode_CheckAndPlayStartCountdown` notes). Two confirmed handlers name
  real states (`RaceStarted`/`RaceStarted2`). Swept all ~35 handlers for the
  score-writer's `+0x5710` offset — zero matches, reinforcing the earlier
  exhaustive negative result. ~30 handlers remain unread — a rich, well-scoped
  vein for a future session. 7 renames. **Follow-up: found the race-finish
  sequence** (`RiderEvent_RaceFinishSequence`, case 6) — reads `rider+0x5720`
  (16 bytes from the score field) and resolves it to a medal tier via fixed
  score thresholds (250000/500000/799999) for one game mode, or per-track
  time thresholds otherwise. Confirmed exactly one static reference to
  `+0x5720` exists anywhere (a read, no write found) — mirrors `+0x5710`'s
  own situation, enriching but not resolving the score-writer question.
  4 more renames.
- **`RE_NOTES_archive_format_decoded.md`** — **decoded the `.big` archive
  container format (`c0fb` magic) and its EA RefPack compression, closing the
  multi-session "where does `Script_PlayByName(\"RaceMode\")` find its data"
  question with physical proof.** Wrote a working RefPack decompressor
  (`refpack.py`) and archive extractor (`extract_big.py`, both project root) —
  the key trick was chaining through payload blobs via actual decompressed-byte
  consumption rather than searching for magic bytes (which false-positives inside
  high-entropy compressed streams). Found `gari.xsf` contains a clean 20-entry
  named script/event table including the literal strings `RaceMode`/
  `ShowoffMode`/`FreerideMode`/`StartCountDown`/`EndCountDown`/`NoCountDown` —
  exactly the names `GameMode_PlayRaceModeScript` and siblings pass to
  `Script_PlayByName`. Checked what's actually at `RaceMode`'s data offset: IEEE
  float parameter data (positions/timings), not new opcode bytecode — refines the
  project's score-writer hypothesis (the opcode *architecture* is fully
  documented already; per-track files hold *configuration data* for it, not new
  code to reverse-engineer).
- **`RE_NOTES_node_base_class.md`** — **the best evidence yet for this project's
  long-standing "shared `Node*` base class the per-frame loop dispatches through
  generically" hypothesis**, repeated informally for 5+ classes across many
  sessions without ever being pinned down. Found via `Camera_ScalarDeletingDestructor`'s
  destructor-chaining reset to a different vtable (`0x00187c20`) — 16 direct
  construct/destruct xrefs from otherwise-unrelated functions confirm it's a
  genuinely shared root class. Read and compared all 16 slots against Camera's own
  derived vtable (Camera overrides 8, inherits 8). One slot
  (`Node_UpdateStub`) is the same "stripped no-op profiler marker, real logic
  compiled out in retail" shape already seen in `Rider_UpdateSubsystems` —
  independent confirmation of the same phenomenon at the root, though not a direct
  match for any of the 5 previously-orphaned Update methods' own vtable offsets.
  Also found a second and third self-referential embedded mini-vtable (the same
  "object pointer == vtable pointer" idiom as `CameraShakeMode`), one within the
  `Node` table itself, one revealed to be `CameraAudioPanningMode` (not actually
  part of Camera's own vtable as first assumed). 10 renames. **Follow-up: chased
  the hierarchy one more level to the true root (`NodeBase`, confirmed identical to
  the already-documented `NodeBase_Construct`/`NodeBase_ConstructRoot` class from
  an earlier session — `NodeBase → Node → Camera`), then did a deep, three-angle
  search for the actual per-frame Update dispatcher and got a definitive negative
  answer: `Rider_UpdateSubsystems` sits at the exact same vtable slot (offset
  `+0x1c`) as `Node`'s own stripped Update stub, proving that's the real dispatch
  slot; `NodeRegistry_UpdateAllOfType` proves the call mechanism directly
  (`(**(*node+0x1c))()`); but exhaustively checking all 6 callers of
  `NodeRegistry_PeekHead` plus every other binary-wide `CALL [reg+0x1c]` site
  (round-6 GhidraMCP patch: `/get_function_containing`) shows every one is either a
  known one-off lifecycle event or the unrelated `Widget` UI hierarchy — no generic
  per-frame walker exists anywhere static analysis can reach. This specific
  question is now genuinely exhausted for this toolset.**
- **`RE_NOTES_camera_system.md`** — **`.cml` CONTAINER SOLVED** (closing section):
  `FUN_00075120` walks each category as a singly linked list of file-relative
  offsets, so the containers ARE statically walkable — this RETRACTS the file's
  earlier "linked via runtime pointers" / "FE camera cannot be extracted"
  claims (marked in place). `0xDEADC0ED` marks unused slots and terminates the
  list. Name hash = `FUN_00074fe0`, verified 2608/2608 across all 13 files.
  The front-end camera is `scripts.cml` scene `VEN_1` slot `V_1`. Ported to
  `port/src/assets/cml.{h,cpp}`.
- **`RE_NOTES_camera_system.md`** — also now includes: the `.cml` camera-script
  format, previously an abandoned dead end, characterized in a later session
  (not fully decoded, but a huge upgrade from opaque). `.cml` is unrelated to
  the `.big`/RefPack container (no compression) — plain byte inspection found a
  ~500-record hierarchical scene graph per track: container nodes (linked via
  `0xDEADC0ED` runtime-pointer placeholders) and leaf/reference nodes (a
  statically-walkable `{size,size,offset,hash}` header whose offset points
  exactly to the first child, confirmed by direct example). Found a clean,
  repeating 6-category template (`Animation Script`/`Camera Regions`/`Cameras`/
  `Director Script`/`Paths`/`Scenes`) under every named track region, and
  cross-validated against a second track (`alaska.cml`) — root tag and several
  leaf hashes are byte-identical across files, confirming a shared,
  track-independent category-naming convention. Exact keyframe float layout
  not reached (deeper levels are sparse runtime-object templates, not dense
  literal data) — needs either the in-binary loader function (not yet found)
  or cross-record statistical analysis to go further. New tool:
  `classify_cml.py` (project root). **Follow-up: cracked the keyframe field
  layout via cross-record statistical analysis** — diffed 6 consecutive
  `Location` records byte-by-byte, found only 15 of ~100 dwords actually vary,
  and decoded them as a clean position (3 large floats, `+0x3c`/`+0x68`/`+0x94`)
  + rotation (3 small floats, `+0x134`/`+0x138`/`+0x140`) pair per record —
  closing most of the format's last open structural question (exact units and
  other record types' equivalent fields not re-verified). **Follow-up: found
  the actual consuming
  code** — searched `default.xbe` for the `.cml` category-name strings
  directly (rather than re-chasing the dead-end `.cml` filename format
  string) and found `"FreeLoaded"` compiled in, part of a real venue/staging
  name table with genuine code xrefs. Traced the full chain:
  `VenueStateTable_ResolveNameByID` (ID→name lookup) ←
  `VenueStaging_TransitionToNamedState` (exit/resolve/enter orchestrator) ←
  `VenueStaging_TickPhaseSequence`/`VenueStaging_TickIndexedPhase` (two
  sibling ~20-state level/venue-load state machines gating on
  `GameMode_Current`, connecting directly to the already-documented
  `InGameState_LoadLevel`/`Level_PreloadAndCheckCountdown` pipeline).
  `VenueStaging_ExitState` contains the literal string `"chase near"` —
  the exact camera-mode name from this project's earliest bookmark note,
  confirming the connection isn't coincidental. 6 renames.

  The rest of `RE_NOTES_camera_system.md` covers the `Camera` class itself,
  previously only touched in passing. Resolved the `Camera_DelegateUpdate`
  (`this+0x34`) open thread: not a
  polymorphic sub-object, but a direct function-pointer record leading to
  `Camera_EvaluateShakeCurves` — a 16-slot keyframed-curve evaluator driven by the
  camera's own behavior timer, feeding `Camera_ApplyShakeOffset`'s
  position+rotation offset composition (via the generic, widely-shared
  `Math_BuildMatrixFromEulerAndPosition`). A data-driven camera-shake/animated-offset
  system, distinct from (but likely layered under) the already-documented
  `Camera_AddShake` scalar accumulator. Confirmed cameras are level-script objects (spawned by
  `ScriptVM_DispatchOpcode` opcode `0x01`), read the real vtable (16 slots):
  destructor, a behavior-timer/dispatch pair, `Camera_UpdateAudioPanning`
  (camera-relative 8-way positional audio panning, iterates every rider) and
  `Camera_UpdateViewTransform` (the actual per-frame view-matrix update via the
  graphics device). Bonus find: `Camera_UpdateAudioPanning` calls
  `Crowd_RandomizeAnimationVariants`, ties directly to the already-documented
  `CrowdBox` opcode — spectator idle-animation variety, throttled by camera
  relevance. 6 renames. Also names and generalizes a pattern seen repeatedly this
  project: several classes' Update methods (`Component_UpdateAll`,
  `Rider_UpdateSubsystems`, now `Camera`'s own update slots) are reachable only via
  vtable, with no traceable caller — almost certainly because the actual per-frame
  loop dispatches through a shared `Node*` base-class pointer, invisible to static
  cross-reference analysis regardless of which class is being searched for.
- **`RE_NOTES_control_scheme.md`** — the trick control scheme, straight from
  `data\config\btnmap0.dat`/`btnmap1.dat` (plain readable text despite the `.dat`
  extension): every digital/analog trick input by name (15 grab-trick variants across
  3 difficulty tiers, prewind mechanics, spin/flip/rail-grind controls). These names
  don't appear in the binary (compiled away at build time), but while checking that,
  **found and fully traced the actual raw-input pipeline for the first time this
  project** — `Input_PollDevice` → `Input_NormalizeGamepadState` →
  `Input_ResolveActionCode` (a 14-entry bitmask→action-code table with a genuine
  repeat/debounce state machine, entry count matching `btnmap0.dat`'s `IS_BOOL`
  section exactly) plus rumble/force-feedback controls. **Then traced the full C++
  class hierarchy behind it**: base `InputDevice` (mostly stub methods) → derived
  `GamepadInputDevice` (the real implementations) → owned by `InputManager`, which
  constructs 4 controller instances (`"XBoxjoypad0"`-`"3"`) and an 8-frame-deep
  `"InputCache"` history buffer sized across all of them — exactly the shape a
  combo/gesture-sequence detector would need. 12 renames total. The final per-frame
  consumer of the cache/resolved action codes wasn't found — `InputManager`'s only
  known construction site reads as a level-transition/reset routine, not an obvious
  persistent singleton.
- **`RE_NOTES_game_data_archives.md`** — a broader sweep of `Game Data\data\` beyond
  `lang\`: dumped the table-of-contents of all 55 `.big` archives (24 use the standard
  EA BIGF format, `parse_all_big.py`/`big_archive_inventory.txt` in the project root).
  Confirms the full 21-song jukebox list and the per-character texture naming scheme.
  Mostly asset cataloging rather than code-behavior insight — lower priority than the
  `.loc` find, but a reusable inventory for future PC-port asset work.
- **`RE_NOTES_race_hud.md`** — the master in-race HUD compositor, `HUD_DrawRaceOverlay`
  (was `FUN_000c6730`), found while tracing what reads the rider stat at `+0x5710`.
  Confirmed panels: rank/position, race time, speed, and score (closes that original
  question — `+0x5710` is drawn directly as the on-screen score). Several more panels
  structurally identified (trick-rating popup text, combo/streak indicator, floating
  score/time-gap popups) but not fully deciphered — good next-step material. Also nets
  4 generic formatting/localization helpers (`HUD_FormatRaceTime`, `HUD_FormatTimeGap`,
  `CRT_FormatString`, `Localization_ResolveString`) and a full 9-function 2D text/glyph
  rendering primitive cluster (`Font_GetGlyphMetrics`, `Text_MeasureStringAnsi`/`Wide`,
  `Text_DrawGlyphBuffer_Numeric`/`String`, `HUD_DrawTextShadowed`(`_Alt`),
  `HUD_DrawNumberBufferShadowed`, `Text_DrawCenteredVertically`), reused throughout HUD
  and menu code, plus the remaining panel-support helpers (`Text_DrawWordWrapped`,
  `Team_GetScoreTier`, `UI_GetPlayerIconSlot`, `HUD_FindActiveOverlaySlot`,
  `HUD_FindComboEventSlot`) — essentially everything reachable from
  `HUD_DrawRaceOverlay` is now named. **Also found and largely decoded its sibling
  function `HUD_DrawWorldSpaceMarkers`** (was `FUN_000c44e0`, immediately adjacent in
  address space) — this turned out to be **the trick/combo score-popup and meter
  system**: a fill meter with sparkle-burst-on-full (plausibly the boost/"Über" meter),
  three stacked floating score-popup callouts graded by `Trick_GetScoreTier` (clean
  point-value thresholds 4000/8000/12000/16000 → tier 2-5 — the actual
  "GOOD/GREAT/AWESOME" trick-landing feedback), and a race-start countdown number. 6
  more renames (`Sprite_DrawIconByIndex`, `Trick_GetScoreTier`, `HUD_DrawNumberStyled`,
  plus `IconAtlas_GetEntry`/`Sprite_DrawAligned` for the marker rendering itself).
  **The localized string content itself is now recovered too** — the user pointed out
  the ISO's extracted `Game Data\data` directory is present in the project root, and
  `data\lang\` holds three `.loc` archives sharing the same 3794-id space:
  `american.loc` (display text), `constant.loc` (**internal symbolic/debug name**, e.g.
  `kHUD_ubertrick`/`kOvCombo1` — unambiguous confirmation), `letter.loc` (an alternate
  text variant). Reverse-engineered the format (LOCH/LOCL header, string count + offset
  table, UTF-16LE strings) and dumped all three side by side — see `parse_loc.py` and
  `american_loc_strings.txt` in the project root. Confirmed every string id referenced
  by this session's HUD work, both display text *and* symbolic name (`"COMBO"`/
  `kOvCombo`, `"KNOCKDOWN!"`/`kHUDKnockdown`, `"BIG AIR BONUS"`/`kHUDBigAirBonus`,
  `"1x COMBO"`.."4+ COMBO"`/`kOvCombo1`-`4`, `"UBER TRICK"`/`kHUD_ubertrick`,
  `"CHECKPOINT"`/`"FINISH"`/`"LAP"`/`kHUD_checkpoint`/`kHUD_finish`/`kHUD_lap`, medal-
  pace labels, etc.) — every "structural, plausible interpretation only" guess in
  `RE_NOTES_race_hud.md`'s panel tables turned out correct, and the `kHUD_`/`kOv`
  symbolic prefixes independently confirm the 2D-HUD vs world-space-marker split
  between the two sibling functions. Full panel table with confirmed text:
  `RE_NOTES_race_hud.md`.
- **`RE_NOTES_ubertrick_fx_cluster.md`** — a particle/vertex-buffer cluster
  (`FX_SpawnTrailQuad`, `FX_SpawnTrailDecal`, `BoardMesh_*`) tied to rail-grind spark
  trails. **The dead end is now resolved** (same session, "read bytes before it" method
  applied a second time, 3 undefined-region boundaries deep): `FX_TrailManager_Tick` →
  `FX_TrailInstance_UpdateTransform` → `FX_TrailSegment_Process` (was the original text-
  export dead end, `FUN_000e08c0` — its guessed-wrong `float` first parameter was also
  fixed to `void *` live). The original "unresolved function-pointer table" guess turned
  out to be wrong, same lesson as the `GameMode_*` clusters — it was mis-placed function
  boundaries, not indirect data references.

## GhidraMCP patched — rounds 2 and 3: 4 more endpoints, all live, both fragment clusters resolved

After round 1 resolved the NodeRegistry thread (below), the `Script_PlayByName` fragment
cluster (`0x0002cdcd`/`cddb`/`cde9`/`d3bf`/`d475`/`ea9e`) needed capabilities no existing
endpoint had:

- **`POST /search_bytes`** (`hex`, optional `limit`) — searches program memory for an
  arbitrary byte pattern, returns one matching address per line (`Memory.findBytes`,
  looped for all matches).
- **`POST /search_address_refs`** (`address`, optional `limit`) — convenience wrapper:
  encodes an address as a little-endian 4-byte pointer and searches for it. **Turned out
  to mostly return false positives** — coincidental `E8`/`E9` (relative `CALL`/`JMP`)
  displacement bytes that happen to numerically equal the target address. Real lesson:
  don't trust an absolute-address byte search without checking what precedes the match.
- **`POST /read_bytes`** (`address`, optional `length` default 16) — reads raw bytes at
  an address as hex. **This was the one that actually solved it** — reading the bytes
  immediately preceding each fragment found real x86 switch/compare-chain dispatchers
  whose case targets landed exactly at each "mysterious" address, just with the wrong
  function boundary (Ghidra had no function there at all; my own earlier
  `/create_function` calls had put the entry 5+ bytes too late, right after the case's
  setup instruction).
- **`POST /delete_function`** (`address`) — removes a function definition
  (`FunctionManager.removeFunction`), needed to clear the mis-placed boundaries before
  `/create_function` could re-derive the correct merged one.

**Result: both fragment clusters fully resolved.** `cdcd`/`cddb`/`cde9` turned out to be
`GameMode_PlayStartupScript`, a real jump table dispatching on the game-mode global
(renamed `GameMode_Current`, was `DAT_001dec94`) to one of three named startup scripts —
which also **corrected every earlier "gated on game mode 3/5 (cutscene/replay)" guess
this session to ShowoffMode** (SSX's trick-attack mode). `d3bf`/`d475`/`ea9e` turned out
to be a matched `NoCountdown`/`StartCountdown` pair gated on the same game-mode value set.
Full writeup: `RE_NOTES_level_script_system.md`, "RESOLVED: cdcd/cddb/cde9 are a real x86
jump table" and "RESOLVED: d3bf/d475/ea9e — countdown gating."

Same install path, same compile-against-real-SDK process, same
`SwingUtilities`/transaction pattern proven safe in round 1. All 4 endpoints confirmed
live and used successfully. Durable copies (source + built jar) updated in `GhidraMCP/`
after each round.

## GhidraMCP patched — round 1: 2 new endpoints (this session)

The "REST plugin can't create functions or disassemble undefined bytes" limitation
documented below is now **fixed at the source**, not just worked around. Fetched the real
upstream source (`github.com/LaurieWired/GhidraMCP`, matches the installed 1.4 build
endpoint-for-endpoint), added two new endpoints mirroring the Listing view's right-click
actions, compiled against the actual Ghidra 11.4.1 SDK found locally in this project
(`ghidra_11.4.1_PUBLIC/`, 200 jars on the classpath, zero compile errors), and installed
the rebuilt jar into the actual loaded extension path:
`C:\Users\MatiasPC\AppData\Roaming\ghidra\ghidra_11.4.1_PUBLIC\Extensions\GhidraMCP\lib\GhidraMCP.jar`.

New endpoints:
- **`POST /disassemble_at`** (`address`) — disassembles raw bytes at an address that isn't
  part of any function yet (`DisassembleCommand`).
- **`POST /create_function`** (`address`, optional `name`) — creates a function at an
  address, disassembling first if needed and auto-determining the function body
  (`CreateFunctionCmd` — the same command the GUI's "Create Function" action uses),
  optionally renaming it in the same call.

Both follow the exact `SwingUtilities.invokeAndWait` + `program.startTransaction()` /
`Command.applyTo(program, monitor)` pattern the file already used for `renameFunction` and
`set_function_prototype` — proven-safe within this codebase, not a novel pattern.

**Update: done.** Ghidra was restarted, the new endpoints confirmed live, and used
immediately to resolve the NodeRegistry bucket-iteration thread (open thread 2 — see
`RE_NOTES_level_script_system.md`'s "Update — the NodeRegistry bucket-iteration thread is
now resolved"). The `Script_PlayByName` caller cluster (open thread 6) turned out to need
more than point-by-point `create_function` calls — several call sites are fragments of
larger functions whose real entry points are still further back in the undefined region —
so `ssx_analyze_gap.py`'s `AutoAnalysisManager` approach (proper control-flow-aware
analysis, not linear disassemble-from-a-guess) is still the right next step for that part.

Artifacts kept for durability, all in the `GhidraMCP/` folder: `GhidraMCPPlugin.patched.java`
(full patched source), `GhidraMCP.jar.patched` (the built jar, in case the extension needs
reinstalling), `GhidraMCP.jar.original-backup` (the untouched original, for rollback —
restore it to the path above and restart if the patched version ever needs reverting).

## Live Ghidra access (GhidraMCP)

`.mcp.json` configures a `ghidra` MCP server (`GhidraMCP/GhidraMCP-release-1-4/
bridge_mcp_ghidra.py`) pointing at `http://127.0.0.1:8080/`, which requires Ghidra
running with its REST-server plugin listening on that port. **This session the MCP tool
itself wasn't connected** (didn't show up in the tool list at all), but the underlying
Ghidra REST server was still reachable directly — worked around it by calling the same
endpoints the bridge script wraps with plain `curl`, e.g.:

```
curl "http://127.0.0.1:8080/decompile_function_by_address?address=0x0004a730"
curl "http://127.0.0.1:8080/xrefs_to?address=0x000aa7a0&limit=50"
curl -X POST "http://127.0.0.1:8080/rename_function_by_address" \
     --data-urlencode "function_address=0x00053740" \
     --data-urlencode "new_name=TrickTrigger_Destruct"
```

Endpoint list is in `bridge_mcp_ghidra.py` (`@mcp.tool()` functions, one REST call each).
Notably `list_functions` (no name/address query, returns the whole `addr: name` table —
5100+ lines) is the easiest way to find "what function actually contains this address"
when `get_function_by_address` comes back empty (i.e. the address falls in an
un-auto-analyzed gap — see open thread 2/4 in this file). If the MCP tool reconnects in a
future session, prefer it over raw curl — same server, just a nicer interface.

**The current Ghidra project was fresh at the start of this session** — none of
`ssx_auto_rename.py`'s renames were applied yet (functions still showed as bare
`FUN_XXXXXXXX`). Fixed by applying the **entire backlog (all 85 rename calls, not just
this session's 8 new ones) live via the REST API**, looping over the script's `rename()`
calls with `curl -X POST .../rename_function_by_address` — confirmed all 85 succeeded
(`searchFunctions?query=Script_` now shows `Script_DispatchOpcode`, `Script_PlayByName`,
etc. instead of `FUN_` addresses). One gotcha hit and fixed along the way: writing the
address/name pairs to a scratch file from Python on Windows produces `\r\n` line endings,
and the stray `\r` silently broke every rename call (generic "Failed to rename function"
with no other clue) until stripped with `tr -d '\r'` — worth remembering if scripting
more batch REST calls this way. **This project no longer needs a Script Manager run to
pick up prior sessions' work** — it's current as of this session. (`ssx_auto_rename.py`
remains the durable source of truth to re-derive everything from scratch if the project
is ever reset again.)

## How the investigation connects (chronological, not file order)

```
Part 1 (string mining across the whole 263k-line file):
  found FUN_0012a250 = tagged pool allocator, ~96 call sites, 41 tags
  found FUN_00049960 flagged (22 tags) but not yet understood       <-- turned out to be the big one
  found FILE_*/ASYNCFILE_*/FILESYS_*/BIG_locateentry (self-naming debug strings)
      |
      v
Part 2 (exUT_/frL_ cluster deep dive):
  corrected: those strings are NOT named pools, just Ghidra's nearest-symbol display fallback
  confirmed FX_SpawnTrailQuad/Decal + BoardMesh_* (rail-grind sparks + swappable board meshes)
  hit a wall: FUN_000e08c0's caller is only reachable via an unresolved fn-pointer table
      |
      v
Part 3 (front-end menu system):
  FUN_0008ff00 misidentified as "Script interpreter" -> corrected to UI_BuildResultsScreen
  discovered the full f3-tag menu system map (78 tags / 39 functions)
      |
      v
Part 4:
  read FUN_0007daa0 in full -> it's FEInit_Boot (corrects the Part-3 "select screen" guess)
  FEInit_Boot calls Script_PlayByName("FEStartScript")
  traced Script_PlayByName -> found the real opcode dispatcher: Script_DispatchOpcode
  Script_DispatchOpcode is FUN_00049960 from Part 1 -- the "22 tags" function, now explained
  documented the complete 24-opcode table + started reading individual constructors (TrickTrigger)
  went on to finish the full opcode survey, base-class architecture (NodeRegistry), across later sessions
      |
      v
Part 5 (this pass -- first live-Ghidra session via GhidraMCP's REST API, not just text-mining):
  confirmed http://127.0.0.1:8080 (Ghidra's REST plugin) reachable even though the MCP tool
      itself wasn't connected this session -- talked to it directly with curl instead
  followed Script_PlayByName's real callees live -> found a whole second layer never seen in the
      text export: ScriptVM_DispatchOpcode/Tick/CreateByHandle/CreateByName/CreateByID
      (opcode 0 of THIS dispatcher calls Script_DispatchOpcode -- the missing link between the
      menu/cutscene event-script layer and the level-object placement layer)
  read TrickTrigger's vtable live (walked xrefs_from on each slot of PTR_FUN_001894f8) ->
      found TrickTrigger_Destruct + TrickTrigger_Update, resolving the long-open
      "TrickTrigger vtable methods" thread. Also corrected a stale guess: TrickTrigger's
      param_1[10] was originally guessed as a collision volume, actually the command-record
      pointer (matches the later NodeBase_Construct correction -- now doubly confirmed)
  applied all new renames live to the Ghidra project via the REST API directly (no Script
      Manager run needed) and mirrored them into ssx_auto_rename.py as the durable record
  decompiled GhidraMCPPlugin.class itself to check for a create-function/run-script REST
      endpoint (there isn't one -- confirmed exhaustively, not assumed) -> wrote
      ssx_analyze_gap.py, a Script Manager script using AutoAnalysisManager to fill the
      one known analysis gap once a human clicks Run
  read the rest of ScriptVM_DispatchOpcode's "opaque" opcodes live -> named 8 more
      (Camera_AddShake, Rumble_TrackMaxA/B, Audio_PlayScaledCue, Camera_WarpToTarget,
      HUD_ShowTimeGapCallout, RailMan_SetSegmentState, CmdTable_LookupByIndex) -- opcode
      survey now ~25/27 resolved
  followed TrickTrigger_Update's tracked-rider-list question further -> found it's not
      TrickTrigger-private at all: it's a shared 3-axis sweep-and-prune broad-phase
      collision system (SweepPrune_*), confirmed via live xrefs across 12 different node
      constructors -- likely the actual engine-wide "what's near what" detection mechanism
      |
      v
Part 6 (same session, continued -- "create as many endpoints as you need, go crazy"):
  user ran ssx_analyze_gap.py, ssx_export_c.py, ssx_parse_xbox_headers.py via Script Manager
      -> ssx_analyze_gap.py added zero new functions (proved the region had no other hidden
         gaps -- useful negative result, not a failure)
      -> ssx_export_c.py's output reviewed and swapped in as the new default.xbe.c
      -> ssx_parse_xbox_headers.py built xbox.gdt (2084 real Xbox SDK types), not yet attached
  patched GhidraMCP a 2nd time (search_bytes/search_address_refs/read_bytes) to chase the
      Script_PlayByName fragment cluster's "zero xrefs" mystery -> the address-search
      endpoint mostly returned FALSE POSITIVES (coincidental E8/E9 relative-branch
      displacement bytes) -- but reading raw bytes preceding 0x0002cdcd (read_bytes) found
      the real answer: a genuine x86 jump table
  patched GhidraMCP a 3rd time (delete_function, to clear mis-placed function boundaries
      before recreating them correctly) -> fully resolved BOTH fragment clusters:
      cdcd/cddb/cde9 = GameMode_PlayStartupScript (dispatches to RaceMode/ShowoffMode/
          FreerideMode startup scripts by name, cross-confirming GameMode_Current's --
          was DAT_001dec94 -- full value table)
      d3bf/d475/ea9e = a matched NoCountdown/StartCountdown pair
          (Level_PreloadAndCheckCountdown/GameMode_CheckAndPlayNoCountdown/
          GameMode_CheckAndPlayStartCountdown)
  corrected every earlier "gated on game mode 3/5 (cutscene/replay)" guess this session to
      ShowoffMode (SSX's trick-attack mode) -- direct consequence of the GameMode_
      discovery, propagated through Camera_AddShake and others
      |
      v
Part 7 (same session, continued -- "keep working, decompile all, go full throttle"):
  resolved the FX-cluster's LOD system (FX_TrailManager_ScanTrackSegments walks 162 track
      segments -> FX_TrailSlot_ClaimByProximity, a 10-slot pool with distance-based
      eviction -- only the 10 closest rail-grind objects ever get a spark trail)
  resolved opcode 8's queue mechanism (ScriptEvent_QueuePush -> RingBuffer_Push, a genuine
      ring buffer with classic wrap/overflow-check idiom)
  resolved the project's OLDEST open thread (present since the first sessions): what walks
      a NodeRegistry bucket each frame. Found NodeRegistry_GetNext (completing the
      iteration primitives) and GameState_ResetAndRebuildTransientNodes, a real
      race-restart/checkpoint routine -- which also solved the separate "_Alt constructor
      family" mystery: they're all called by Script_DispatchOpcode_Alt, a full second
      copy of the main dispatcher used specifically during state-rebuild
  chased the last SweepPrune lead (0x00036490) expecting a cMeshAnim link -> found
      something much bigger: Rider_ConstructBase, the player/rider object's OWN shared
      constructor (type 0x3ef -- confirmed the exact type TrickTrigger_Update checks for;
      yes, the player itself registers with SweepPrune) -> led straight to
      Player_Construct/OtherRider_Construct (human vs AI) -> led to
      Race_SpawnRidersAndLoadAssets, the actual race-setup routine, which reads the SAME
      per-participant array and sign-bit flag as GameMode_*/Timer_RebuildPlayerRegistry/
      Level_PreloadAndCheckCountdown -- the capstone tying the whole session's race-setup
      thread into one coherent subsystem instead of several coincidentally-similar ones
  followed Race_SpawnRidersAndLoadAssets's own address as a DATA reference back to its
      containing table (0x0019a560-0x0019a58c) -> found RaceState_SetState, the master
      state machine for the ENTIRE race and tutorial flow, with debug-name strings for
      all 11 states (PreRace/StartRace/Countdown/Race/EndRace/ReplayEndRace for racing,
      LoadLesson/PreLesson/SelectLesson/RestartLesson/EndLesson for a previously
      undocumented in-game tutorial system) -- plus the actual reset/teardown/ranking
      functions (Race_ResetCountersAndDispatch, Race_ResetPlayerRoster,
      Race_DestroyRidersAndUnloadAssets, Race_ComputeRankings) that implement it
  hit one real mishap along the way: blindly /create_function'd every dword in the
      suspected table without checking overlap first, and one entry (0x0002cbd0) turned
      out to point mid-function into Timer_RebuildPlayerRegistry, briefly truncating its
      stored body -- caught immediately and fixed (delete + disassemble + recreate).
      Lesson recorded for next time: check a candidate table entry doesn't already fall
      inside a known function's range before creating there.
```

## Open threads / good next steps

1. **Opcode table survey: COMPLETE.** All 24 opcodes in `RE_NOTES_level_script_system.md`
   now have at least structural documentation; 20 read in behavioral detail (only
   `Roller` and `cMeshAnim` stopped at structural level due to heavy 3D/matrix math).
   Headline results:
   - `AnimObject` (opcode 0x100) is the shared base for the whole `Anim*` family
     (`AnimDelta`/`AnimCombo`/`AnimTexFlip`) — same parameterized-constructor pattern
     as `Boost` across 0x07/0x10/0x18.
   - **Resolved the recurring "30"/"0x1e" constant** seen across many types
     (`Cracked`, `UVScroll`, `TexFlip`, `AnimCombo`): the engine runs a fixed **30fps**
     simulation rate; frame-count fields are divided by 30 to get seconds.
   - The same randomized-start-offset timing idiom (IEEE-754 mantissa RNG trick, `-2.0`
     sentinel) appears **three separate times**, always inlined, never factored into a
     shared function (`TexFlip`, `UVScrollTexFlip`, `AnimTexFlip`) — a good candidate
     to actually factor into one helper in a clean-room reimplementation.
   - `cMeshAnim` (opcode 0x14, largest object at 1760 bytes) is a skeletal mesh
     animation instance — walks a bone hierarchy, blends bind-pose vs. override
     matrices, combines with parent transforms.
   - Code reuse between opcodes is real but inconsistent — don't assume shape implies
     shared code: `Boost` shares one *function* across 3 opcodes; `UVScrollTexFlip`
     calls into `UVScroll`'s constructor but *inlines* `TexFlip`'s logic; `ZBoost`
     looks like a copy-paste of `Boost` but is a fully separate function. Check every
     time.
   - Every command record shares a **12-byte header** (opcode at `+8`) before its
     type-specific payload — confirmed via `Particle`'s constructor.
   - Node constructors do **not** cleanly split into two families as first guessed —
     "has an embedded collision volume at `param_1[10]`" and "self-registers into the
     owner's `+100` active-node slot" are independent, per-type choices.
   **Assessment: this is very likely enough to reimplement level-trigger *placement*
   in a port** (parse the script, instantiate the right type at the right position with
   the right params) even before each type's runtime *behavior* is separately
   reverse-engineered. **`TrickTrigger`'s runtime behavior is now also read** (see item 4
   below) — a template for reading the rest of the family (`Boost`, `Cracked`, etc.), each
   of which still only has construction documented, not per-frame behavior.
2. **The NodeRegistry bucket-iteration thread is FULLY RESOLVED** — one of the project's
   oldest open questions, dating back to the first sessions. `NodeRegistry_Remove`'s one
   non-destructor caller (`0x0002cc36`) turned out to be inside `Timer_RebuildPlayerRegistry`
   (`0x0002cbcc`, a function that didn't exist in Ghidra's database until created live via
   the new `/create_function` endpoint) — a per-player Timer-node re-registration routine.
   Chasing it one step further found the actual per-type bucket-walker: **completed the
   `NodeRegistry` primitive set** (`Insert`/`Remove`/`PeekHead`/`GetNext`/
   `DestroyAllOfType`) and found **`GameState_ResetAndRebuildTransientNodes`** — a genuine
   race-restart/checkpoint reset routine that walks every live `Debounce`/`DeadNode`/
   `Boost`/`Timer` instance via `PeekHead`/`GetNext` and notifies each (vtable `+0x20`).
   This also resolved the mystery of the `_Alt` constructor family
   (`Boost_Construct_Alt`/`LapBoost_Construct_Alt`/`ZBoost_Construct_Alt`/
   `Roller_Construct_Alt`): they're used by **`Script_DispatchOpcode_Alt`**, a complete
   second copy of `Script_DispatchOpcode`'s type-ID switch used specifically during
   state-rebuild (via `BatchConstruct_NodesOfType`), not random duplicates. **Don't
   confuse any of this with the sweep-and-prune system, item 5 below** — that's a
   *position*-keyed structure (3 sorted axis lists for broad-phase overlap), entirely
   different from this *type*-keyed registry. Full writeup: `RE_NOTES_level_script_system.md`,
   "RESOLVED (live Ghidra, this session): per-type bucket iteration + the `_Alt`
   constructor family's real purpose."
3. **The two unnamed opcode tags are resolved** — not by recovering their (confirmed
   absent, in both the text export and live) tag strings, but by behavior:
   `UnknownOpcode09_TogglePropertyByID` and `UnknownOpcode0d_Construct`. Nothing further
   to do here.
4. **`TrickTrigger`'s vtable methods: DONE this session (live Ghidra).**
   `TrickTrigger_Destruct` (ordinary teardown) and `TrickTrigger_Update` (the real
   "trick complete → fire scripted event" logic — walks a tracked-rider list, checks a
   threshold against a `+0x5710` player stat, then launches a payload script via the
   newly-found `ScriptVM_CreateByHandle`) are both read and renamed. Its "what populates
   the tracked-rider list" follow-on is now also answered — see item 5. Two follow-on
   questions remain: what the `+0x5710` stat actually is; the exact contract of
   `FUN_00141970`. **The FX-cluster dead end (`FUN_000e08c0`) is also now resolved** —
   see `RE_NOTES_ubertrick_fx_cluster.md`, "read bytes before it" method applied a second
   time, 3 undefined-region boundaries deep this time
   (`FX_TrailManager_Tick`→`FX_TrailInstance_UpdateTransform`→`FX_TrailSegment_Process`).
5. **A sweep-and-prune broad-phase collision system** (see
   `RE_NOTES_level_script_system.md` "Broad-phase collision: sweep-and-prune spatial
   system"): `TrickTrigger`'s tracked-rider list turned out to be this object's slot in a
   shared 3-axis sort-and-sweep structure (`SweepPrune_Register`/`Unregister`/
   `InitNode`/`BindAxisBounds`/`MaintainAxis`/`ToggleAxisOverlap`) that essentially every
   spatial/trigger node type registers into at construction — confirmed via live xrefs
   across 12+ constructors. This is very likely the actual engine-wide "what's near what"
   broad-phase detection mechanism, not something TrickTrigger-specific. High confidence
   on the overall algorithm shape, medium on exact field semantics. **All 5 unnamed
   sibling constructors resolved**: `Boost_Construct_Alt`/`LapBoost_Construct_Alt`/
   `ZBoost_Construct_Alt`/`Roller_Construct_Alt` are alternate-overload constructors for
   already-known types, all four called from `Script_DispatchOpcode_Alt` (see item 2 —
   chasing this exact question is what resolved the whole `_Alt`-family/bucket-iteration
   thread). **The last lead (`0x00036490`) turned out to be much bigger than expected —
   not a `cMeshAnim` link, but the player/rider object's own shared base constructor,
   `Rider_ConstructBase`** (type `0x3ef` — confirmed the exact "rider" type ID
   `TrickTrigger_Update` checks for, and yes, the player registers with `SweepPrune` too).
   This led straight to `Player_Construct`/`OtherRider_Construct` (human vs. AI/CPU rider)
   and then to **`Race_SpawnRidersAndLoadAssets`** — the actual race-setup routine that
   spawns every participant, using the *same* per-participant array and sign-bit flag that
   `GameMode_*`/`Timer_RebuildPlayerRegistry`/`Level_PreloadAndCheckCountdown` all
   independently touch. That's the capstone connecting this whole session's `GameMode_*`
   thread to something concrete: one coherent race-setup subsystem, not several unrelated
   mechanisms. Full writeup: `RE_NOTES_level_script_system.md`, "The player/rider object
   itself, and how a race actually starts." Still-open good next step: find what consumes
   the "active pairs" list `SweepPrune_ToggleAxisOverlap` builds — likely where collision
   response actually happens.
6. **The outer script VM: opcode survey ~complete, AND the `Script_PlayByName` cluster is
   now fully RESOLVED.** (see `RE_NOTES_level_script_system.md` "Outer script VM"):
   `Script_PlayByName` doesn't call `Script_DispatchOpcode` directly, it goes through a
   whole second bytecode interpreter (`ScriptVM_DispatchOpcode`/`Tick`/`CreateByHandle`/
   `CreateByName`/`CreateByID`) that handles menu flow, cutscene sequencing, and delegates
   level-object placement as just one of its ~27 opcode types (opcode 0). 25 of ~27
   opcodes now have at least one confirmed behavioral detail — highlights:
   `Camera_AddShake`/`Rumble_TrackMaxA`/`Rumble_TrackMaxB` (force-feedback + camera shake
   accumulators, gated on **ShowoffMode** — corrected from an earlier "cutscene/replay"
   guess), `Camera_WarpToTarget` (cutscene camera positioning via quaternion math),
   `RailMan_SetSegmentState` (rail-grind segment state table), and
   `HUD_ShowTimeGapCallout` (a racing "time gap to opponent" popup). Only two opcodes
   remain genuinely opaque: 0x06/0x0f's paired effect toggle and 0x08's queue mechanism.
   **The `Script_PlayByName` caller cluster** (`0x0002cdcd`/`cddb`/`cde9`/`0x0002d3bf`/
   `d475`/`ea9e`) is now fully explained — see "GhidraMCP patched — rounds 2 and 3" above
   for the endpoints that solved it, and `RE_NOTES_level_script_system.md`'s two
   "RESOLVED" sections for the full writeup. Short version: `cdcd`/`cddb`/`cde9` are a
   real x86 jump table (**`GameMode_PlayStartupScript`**) dispatching on the game-mode
   global (renamed **`GameMode_Current`**, was `DAT_001dec94`) to one of three named
   startup scripts — `RaceMode`/`ShowoffMode`/`FreerideMode` — which is also **the
   discovery that corrected the ShowoffMode misattribution above**. `d3bf`/`d475`/`ea9e`
   are a matched `NoCountdown`/`StartCountdown` pair
   (`Level_PreloadAndCheckCountdown`/`GameMode_CheckAndPlayNoCountdown`/
   `GameMode_CheckAndPlayStartCountdown`) gated on the same game-mode value set. My
   earlier theory ("referenced as raw data, needs a memory-search capability") was
   half-right on the *symptom* (zero xrefs) but wrong on the *cause* — turned out to be
   mis-placed function boundaries (5+ bytes too late) on real, ordinary control-flow
   targets, not indirect data references at all. Nothing left open in this thread.
7. **The track table is fully resolved.** All 7 fields read and confirmed live:
   `displayName`/`displayNameUpper`/`sizeOrFlags`/`shortCode`(×2)/`categoryTag`/
   `trackIndex`. 12 real tracks confirmed (Garibaldi, Merqury City, Aloha Ice Jam,
   Pipedream, Untracked, Tokyo Megaplex, Big Air Dome, Alaska, +4 more), terminator row
   at index 13 exactly matches `UI_BuildResultsScreen`'s own loop bound (`0xd`) — a clean
   cross-confirmation between two independent sessions' findings. Data label renamed
   `TrackTable`. No formal Ghidra struct type defined (no struct-creation endpoint in
   GhidraMCP even after 3 patches — not worth a 4th for a one-off). Full writeup:
   `RE_NOTES_results_screen.md`. **Confirmed NOT the same table**
   `ResourceContext_GetTableField` reads (that one's referenced only through the
   resource-context indirection; `TrackTable` is referenced by hardcoded literal address
   directly, 13+ times, in `UI_BuildResultsScreen` alone) — the earlier "intriguing,
   might be the same table" note in `RE_NOTES_level_script_system.md` is resolved: they
   aren't. **`shortCode` reuse resolved too:** it's the track-code suffix in a
   `<CharacterPrefix>_V<trackCode>` per-character voice-line asset-key naming convention
   (found via `/search_bytes` on the literal `"gari"`), tying directly to the DVD/Jukebox
   talent voice-commentary system. Full writeup: `RE_NOTES_results_screen.md`.
8. **Essentially DONE.** The frontend menu-map is now 26 of ~30 screen-builder functions
   renamed and tag-verified live (every screen category: DVD/jukebox, character/board
   select, rider bio, pause menu, rank/medals/leaderboard, name entry, credits, plus 6
   generic widget helpers read in real construction-pattern detail). The 3 functions
   whose tag couldn't be confirmed live (`FUN_00085ab0`/`FUN_00094a80`/`FUN_000859c0`)
   turned out to not be screen-tag owners at all — they're generic base-`Widget`-class
   internals (destructors + a doubly-linked child-list container system), now fully
   verified and renamed (`Widget_BaseDestructor`, `Widget_ScalarDeletingDestructor`,
   `Widget_UnlinkFromList`, `Widget_ReleaseChildSlotsAndUnlink`,
   `Widget_FindChildByType`, `Widget_GetChildAtIndex`, `Widget_ResetIconTextPairs`,
   `Widget_SetChildVisibilityMask` — 8 renames). Only 4 generic widget helpers remain
   real-but-complex (multiple `unaff_*`/type-propagation decompiler-confusion warnings,
   one checked in detail and genuinely too mangled — 10+ levels of pointer indirection
   — to name with confidence), left un-renamed rather than guessed. Full map:
   `RE_NOTES_frontend_menu_map.md`.
9. **`Race_ComputeRankings`'s remaining sub-helpers: all read and renamed.** Resolved
   into three subsystems: a 5-slot-per-rider rival head-to-head comparison byte grid
   (`Race_RecordRivalComparisonByte`/`Race_CollectRivalComparisonBytes`/
   `Race_GetClampedRoundedTime`/`Race_ComputeRoundedTimeGap`), a 4-slot team-assignment
   system used by Showoff/team modes (`Team_GetRiderTagByte`/`Team_FindSlotByRiderTag`/
   `Team_GetPointerByTag`/`Team_CountActiveTeams`), and a post-race highlight/award
   running-max record tracker (`Race_UpdateHighlightRecords`) — plus a plain
   compiler-generated rounding helper (`CRT_ftol_TruncateToInt64`). 10 renames. Full
   writeup: `RE_NOTES_level_script_system.md`.
10. **New system found and fully vtable-mapped: `AudioSystem` (a.k.a.
    `"BXAudioSystem"` per its own tagged allocation).** Traced the widely-referenced
    global audio handle `DAT_001f82f4` back to its guarded singleton-init and real
    constructor, then read its full vtable (16 dwords, 15 real slots, 8 previously
    unanalyzed `LAB_` labels — batch function-created). **All 15 slots now named**:
    constructor pair, destructor, a tag-confirmed (`"DynamicSoundMem"`) sound-memory
    allocator + its slot-configure sibling, a slot-release function, 2 "play sound"
    variants (`PlaySoundSimple`/`PlaySoundFull` — both reachable only via their own
    vtable slot, no static caller found), a 3D positional-audio calculator, a
    per-frame channel-volume tick, stop-all and stop-by-tag functions, a
    pending-request ticker, a callback-context setter, and one verified no-op stub.
    Surfaced two internal fixed-size arrays (a 64-slot live-channel pool at
    `+0xb44` and a dynamic-sound-buffer pool at `+0xca4`). 15 renames total.
    **Then, tracing a global (`DAT_001f82e4`) referenced from 2 of AudioSystem's own
    vtable slots, found it isn't a sibling system — it's the root of AudioSystem's
    own class hierarchy.** `AudioSystem` turns out to be the leaf of a 9-level
    single-inheritance chain: `AudioBankBase` (root, tag `"BankInstances"`) ->
    `AudioBank` (tags `"mBank"`/`"mPath"`) -> `MidiBankManager` (tag `"MIDIBANKS"`)
    -> `AudioStreamManager` (tag `"StreamArray"`) -> `MusicManager` (reads
    `data\config\music.inf`) -> `SpeechManager` (tag `"mSpeechInstance"`) ->
    `SoundGroupManager` (cross-referenced field pair, no tag) -> `AudioChannelMixer`
    (named after its own 2 real overrides) -> `AudioSystem`. Explains why several of
    AudioSystem's own named vtable slots are actually introduced 1-2 levels below
    it (inherited unchanged, left under the `AudioSystem_` prefix since that's
    still how they're reached). **Follow-up full-vtable comparison across all 9
    levels also found AudioSystem's real vtable is 16 slots, not ~15 as first
    assumed** — 2 missed slots decompiled and named
    (`AudioSystem_PlayLoopedCue`/`AudioSystem_ResolvePriorityTier`), plus
    `AudioBank`'s own genuinely-new slot (`AudioBank_RegisterSoundInstance`,
    inherited 6 levels before being overridden). 13 new renames total this
    thread. **Then swept each level's singleton for outside callers (not just its
    constructor)**, surfacing a real public-API layer: `AudioBank_ResolveCueEntry`/
    `AudioBank_DispatchCueToDevice` (confirms the audio class hierarchy reads
    directly from `.big` archives via the already-named `BIG_locateentry`),
    `AmbientZone_UpdateBlendedPosition` (a 15-emitter weighted position blend,
    plausibly crowd/wind ambience), `MusicTrack_Construct` (a per-track adaptive-music
    loader — beat-grid config, tag-confirmed via `"PathfinderStream"`),
    `SpeechManager_TriggerLineByEventCode` (voice lines beat-synced to music via
    `Rider_ScheduleTimedCallback` — a genuine cross-link to `RiderEvent`), and
    `MidiBankManager`'s destructor + queue method. 7 more renames. **Finished the
    sweep** by reading every remaining referencing function, uncovering two new
    satellite classes that register through the singletons rather than being the
    singletons themselves: `AmbientZone` (7 methods — random weighted sub-sound
    picks, a combined single/array destructor confirming instances live in a
    400-byte-stride record array, up to 15 sub-emitters each) and `SpeechLineSet`
    (a per-instance speech-line holder, tag-confirmed via a refcounted
    `"SpeechConfig"` buffer reading `data\config\speech.inf`, parsed by the newly
    named `SpeechManager_ParseSpeechConfig`). Also found `SoundGroupManager`/
    `SpeechManager`/`MusicManager`/`MusicTrack`'s own destructors, independently
    confirming destructors chain down the inheritance levels in reverse, mirroring
    the constructor chain. 16 more renames (23 total this continuation). Full
    writeup: `RE_NOTES_audio_system.md`.
11. **New system, first rendering-code coverage: `RE_NOTES_rendering_system.md`.**
    The low-level D3D8 Xbox API wrapper layer (~61 `D3DDevice_*`/`D3DResource_*`
    functions) was already auto-labeled by Ghidra before this session; this thread
    found the game's own code built on top of it by tracing callers of the
    per-frame `D3DDevice_Present`/`Clear` calls. Found the engine boot chain
    (`Application_InitPlatformAndDevice` → `Renderer_InitializeD3DDevice`, earlier
    in the sequence than the already-documented `Application_InitSubsystems`),
    a complete disc-read-error fallback screen (content-confirmed via 3
    hardcoded localized strings), a reusable 2D quad-batch sprite renderer
    (`SpriteBatch_FlushAndPresent`/`PushColoredQuad`), a loading-screen image
    blitter, and — via the boot chain — a generic 6-word additive RNG
    (`RNG_Seed`/`NextUInt32`/`SeedGlobal`/`NextGlobalUInt32`) that turned out to
    be the exact same "get a random number" utility already seen called from
    `AmbientZone`'s methods in the audio system. 11 renames. **Then traced `D3DDevice_DrawIndexedVertices`'s
    remaining 5 callers**, finding the actual mesh-drawing layer:
    `BoardMesh_DrawAttachedPatches` (confirms `BoardMesh_LockBuffers`/
    `GetBufferAddr` were already named from an earlier, unrecorded session),
    and 4 `MeshRenderer_*` functions sharing a per-instance shader-cache
    context — including `MeshRenderer_DrawMultiTexturedParts`, which samples
    the previous frame's back buffer as a texture input (a screen-space
    effect, plausibly ice/reflection). Found a 16-slot mesh draw-mode
    dispatch table at `0x001a2a90` (4 of the 5 new functions sit in it) but
    could not find its dispatcher (`/search_address_refs` limitation). 5
    more renames (16 total this thread). **Then found the actual per-frame scene
    pipeline** by tracing `D3DDevice_SetTransform`/`SetViewport` callers:
    `SceneRenderer_RenderAllPasses`/`RenderPassRange` (master frame orchestrators,
    run 6 sub-passes across priority groups), `SceneView_RenderPass` (builds camera
    projection + viewport, then walks a 6-dword-record render-command list — the
    dispatcher the mesh handlers are invoked through), the perspective/orthographic
    `Matrix_Build*Projection` pair, `Renderer_SetDefaultDeviceState`, and the deep
    NV2A `D3D_InitMiniportAndFrameBuffers`. **Also fully mapped the 16-slot mesh
    draw-mode dispatch table** — 6 real draw-mode handlers (`MeshDrawMode_*`:
    decal/quad/visibility-tested/skinned/dual-buffer/line-list), 2 hash-bucket
    enqueue functions (`MeshQueue_Insert*Bucket`, revealing meshes are collected
    into bucketed lists then drained per-bucket), shared no-op stubs, plus
    `BoardMesh_BuildAndUploadGeometry`. 17 more renames (33 total this rendering
    thread). **Then named the render-context helper layer** (7 renames: matrix-slot
    copiers, vertex-register pushers, fog/constant setup, buffer-cycle rotator) plus
    2 resource-registration helpers. **Finally, cracked the top-level frame driver**
    — `SceneRenderer_RenderAllPasses`/`RenderPassRange` had callers only inside a
    never-analyzed blob with no containing function; found the function boundary by
    backward byte-scanning for a ret+padding boundary, created the function
    (`0x00105ce0-0x0010618a`, contains both caller sites), and named it
    `SceneRenderer_RenderFrame` — the master per-frame render function with a
    **reflection pass**, a **split-screen 2-player viewport handler**, and detail-LOD
    selection. Reached only via a graphics-context method table (why it had no direct
    callers). Also named the matrix-stack push/pop and detail-level selector. 4 more
    renames. **The render pipeline is now mapped end to end.** 51 renames total this
    rendering thread. **Then characterized the `GfxContext` class** (the
    render-device object, `param_1` throughout render code): `RenderFrame` is a
    virtual method in its vtable at `0x001a2b38` (with embedded "Grid indicies"/
    "ShadowVolumeData" data-section labels), letting the vtable offsets `RenderFrame`
    calls be mapped to view-dimension accessors, two matrix stacks (world +
    view/proj), and a full render-state command builder (push/pop + blend/alpha/
    texture bit-field setters + commit). 22 more renames. 66 total this rendering
    thread. **Then read `GfxContext_Init` (vtable+0x04)** — the master render-context
    setup, the single most architecturally revealing function in the renderer: it
    allocates a full set of **tagged, typed render-command-list pools**
    (`defPatchList`/`defSpriteList`/`defMeshList`/`defEmitterList`/`defShdVolList`/
    `defCloudNodeList`/etc.), each **pre-seeded with its draw-mode handler** —
    confirming the earlier-named handlers are the per-pool defaults and tying the
    whole command-pool → command-list-walk → draw-handler design together. Also
    builds the full shader table (~24 VS + ~26 PS), 4 `VideoPlayer` objects (FMV/
    replay), an "XBoxBezierMan" tessellation object, and default device state. 4
    more renames. 70 total this rendering thread. **Then fully mapped the
    `VideoPlayer` class** (FMV/replay, surfaced from `GfxContext_Init`): vtable at
    `0x001a83fc`, 6 methods — `Construct` (640×480 surface via the movie subsystem),
    `Open` (1MB `streambuff` + decode stream), `UpdateSkipInput` (controller skip),
    `Tick`, and two state getters. 6 more renames. 76 total this rendering thread.
    **Then mapped the `BezierMan` class** (a.k.a. `XBoxBezierMan`, Xbox Bezier-patch
    hardware tessellation, also from `GfxContext_Init`): vtable `0x001a2a50`,
    tessellation-param defaults, GPU-wait buffer release, U/V tess-factor getters
    (doubled in split-screen), and per-axis tess-enable flag accessors. 9 more
    renames. **Then found GfxContext's real internal name (`XBoxGraphicsMan`) and two
    fresh systems in the data block after its vtable**: `LightManager` (tag
    `"LightMan"`, a full light-object pool — allocate/free/index, ambient + point
    light submission, 10 methods) and `LensFX` (tag `"LensFX"`, ~20.7KB lens-flare
    system, constructor pair). 14 more renames. **Then fully vtable-mapped `LensFX`**
    (occlusion-query-driven flare-intensity fades, fog-gated secondary flare row,
    blend/viewport setup — 8 renames) **and corrected a same-session error caught by
    raw-byte disassembly**: `DAT_001e98c8` is the LightManager instance itself (its
    base-class ctor publishes it), not a separate "device singleton" —
    `LightManagerBase_Construct`/`Destruct`/`ScalarDeletingDestructor` named, all
    dependent annotations fixed (3 more renames). **Then investigated `FUN_00106930`**
    (claims 4 light slots, submits point lights) but found 4 decompiler-unresolved
    `unaff_` registers making the exact parameter mapping unreliable — deliberately
    left unnamed. **Pivoted to LensFX's shared base vtable** (`0x19d1b8`, named
    `FXNode`): confirmed it's `NodeBase`-derived and — importantly — that the
    already-named `FX_TrailInstance_UpdateTransform` (from the rail-grind spark-trail
    system) is actually a generic inherited `FXNode` method, not trail-specific
    (cross-referenced in both this file's rendering writeup and
    `RE_NOTES_ubertrick_fx_cluster.md`). 1 more rename. 111 total this rendering
    thread. Full writeup: `RE_NOTES_rendering_system.md`.
12. **New system: terrain collision (`RE_NOTES_terrain_collision.md`).** The core
    snowboarding mechanic — how the rider collides with the terrain surface. Traced
    the terrain-grid singleton (`DAT_001faf8c` → `TerrainGrid`, a 2D cell array,
    width `+0x2c`/height `+0x30`/cell array `+0x4c`) out from the already-named
    `Rider_ComputeTerrainCellIndex`. Named the query cluster:
    `Terrain_QuerySurfaceContact` (grid broad-phase → AABB reject → per-triangle SSE
    narrow-phase), `Terrain_SampleHeightAt`, `Terrain_GetCellRangeForBounds`, the
    reusable `Collision_TestAABBOverlapWithMargin` primitive, and the rider-side
    contact-cell tracking (`Rider_UpdateTerrainContact`,
    `Rider_CollectTerrainCellsInBounds`, `Rider_MarkTerrainCellTouched`,
    `Rider_RebuildTerrainContactSet`). 12 renames (11 fn + 1 data). Also found the level-asset loader (Level_LoadTrackAssets, revealing the per-track .ltg terrain-geometry file feeds TerrainGrid). Classic grid-
    broad-phase + triangle-narrow-phase design. Open: the `TerrainGrid` builder
    (no direct write xref; `FUN_00142e10`/`FUN_001421b0` are candidates).
    **`.ltg` format now FULLY DECODED** (header→triangle): header maps exactly onto
    the runtime `TerrainGrid` struct (grid W×H/cell size/sub-objs at identical
    offsets), a W*H cell-offset table, then per-cell 76-byte collision-triangle
    records (surface/material id + 3 float3 vertices tiling the 2500-unit sub-cell
    grid). Tool `scripts\classify_ltg.py` (with `tris N` dumper). Gari: 23×31=713
    cells, 243 populated — the decoder's ASCII map traces the run down the mountain.
    2nd fully-closed on-disc→runtime loop in the project. **Then mapped the rider
    physics-mode state machine**: `Rider_DispatchPhysicsMode` (switch on `rider+0x484`)
    selects 1 of 6 models — modes 1-3 terrain/ground (incl.
    `Rider_ResolveTerrainContactPhysics`, the board-on-snow contact-response core),
    mode 4 no-op, modes 5-6 non-terrain. 6 renames. Full writeup:
    `RE_NOTES_terrain_collision.md`.
13. **First real use of `xbox.gdt` (imported project archive) for type-correctness
    work, not naming.** Confirmed `set_local_variable_type`/`set_function_prototype`
    resolve real Xbox SDK struct types across the archive boundary -- applied
    `D3DVIEWPORT8` to 6 confirmed viewport-setup sites across the rendering system
    (`LensFX_SetViewportAndViewMatrix`, the `D3DDevice_SetViewport` thunk's own
    prototype, `SceneView_RenderPass`, `D3DDevice_SetRenderTarget`,
    `SpriteBatch_FlushAndPresent`, `SceneRenderer_RenderFrame` x2), each verified via
    a follow-up decompile showing real struct field access. **Found and documented a
    real correctness gap in GhidraMCP itself**: both endpoints report "successfully"
    even when the requested type name isn't found anywhere and they silently fall
    back to plain `int` -- reproduced with `D3DPRESENT_PARAMETERS` (genuinely absent
    from the built archive, likely an unresolved forward-declared dependency;
    reverted safely once caught). Established a mandatory verify-after-every-call
    discipline going forward. No function/data renames this pass. Full writeup:
    `RE_NOTES_rendering_system.md` and the `reference_ghidra_mcp_connection` memory
    ("Round 7").
14. **Fully characterized `GfxContext`'s method table at `0x001a2b60`** (the same
    table `SceneRenderer_RenderFrame` is reached through). 5 of the 15 other slots
    pointed into never-analyzed code -- created all 5 via `/create_function` (all
    correctly-aligned, no boundary-scan needed this time), named 4 with structural
    confidence: `GfxContext_ApplyUniformGammaRamp` (the screen fade-to-black/white
    effect via hardware gamma ramp), `GfxContext_SetViewRectAndApplyViewport`,
    `GfxContext_GetViewRect`, `GfxContext_SetOrthographicViewAndApply` (2D/UI
    projection setup). 2 left deliberately unnamed (clear location, unclear
    semantics). 4 renames. Full writeup: `RE_NOTES_rendering_system.md`.
15. **Cracked the misfiled "reflection-math cluster"** from the rendering thread --
    turned out to be a shared, emitter-agnostic `FXParticle_*` particle-setup utility
    library (transform/timing init, size-range, color-gradient), reached from 3
    contexts: `Rider_UpdateSnowSprayFX` (rider velocity/terrain-driven snow-spray,
    zero static callers -- likely vtable-dispatched), `FXParticle_SpawnFromDescriptor`
    (a generic config-table spawner), and `SceneRenderer_RenderFrame`'s reflection
    block (feeding the already-named `FX_SpawnTrailDecal`). Ties directly into the
    already-documented `FX_TrailManager`/Ubertrick FX system. 9 renames. Full writeup:
    `RE_NOTES_ubertrick_fx_cluster.md`.
16. **Finished the `FXParticle_*` cluster** (see #15) -- the 6 remaining sibling
    functions are the emission-timing half: `FXParticle_TickEmissionAndAdvance` (the
    master per-period tick, advances a trail position by a delta and an 11-bit
    wraparound phase counter once enough emission time accumulates), backed by
    accumulator set/increment, phase-randomize, delta-clear, and a 2-input
    color-gradient variant. 6 more renames (15 total across the cluster). Full
    writeup: `RE_NOTES_ubertrick_fx_cluster.md`.
17. **Mapped `PixelBlit_ValidateAlignmentAndDispatch`** (the previously-unread pixel
    blitter behind `LoadingScreen_BlitImageToBackBuffer`): validates block-compression
    alignment then dispatches through 5 format-specific handlers (pairwise sharing
    matches real DXT2/3, DXT4/5 equivalence). Handlers created but not individually
    read -- likely generic Xbox library code. 1 rename.
18. **Read `GfxContext`'s full vtable** (120 slots) -- found and bulk-created ~49
    never-analyzed functions (2 already known, confirming the vtable directly
    contains `FX_SpawnTrailDecal`). Characterized a complete texture queue/upload/
    register pipeline and the render-state-record decoder
    (`GfxContext_ApplyRenderStateRecord`) with strong field-offset evidence. 16
    renames; ~27 slots remain unread. Full writeup: `RE_NOTES_rendering_system.md`.
19. **Finished the `0x00103d00-0x00104060` GfxContext-vtable cluster** -- found
    `New_XBoxGridMesh` (closing a previously-flagged unresolved allocation site) plus
    6 getter/setter pairs. 7 renames. Full writeup: `RE_NOTES_rendering_system.md`.
20. **Found a clip-space frustum-test family** in the GfxContext vtable's
    `0x000fb0xx-0x000fb3xx` sub-cluster: a Cohen-Sutherland outcode test, an
    8-corner AABB-vs-frustum visibility classifier (the standard culling
    algorithm), a point-to-screen projector, plus a standalone
    `Matrix_TransformVector4` utility. 7 renames. Full writeup:
    `RE_NOTES_rendering_system.md`.
21. **Found the matrix-stack-2 builder family and `FX_SpawnRadialDecal`** in the
    GfxContext vtable's `0x000fecxx-0x000fedxx` sub-cluster: identity/matrix/
    translation/rotation-apply variants, plus a radial vertex-fan decal spawner
    tying into the `exUT_` submission path. 7 renames. Full writeup:
    `RE_NOTES_rendering_system.md`.
22. **Swept the remaining scattered GfxContext-vtable singles**: found a
    dirty-state-caching render-state variant, the split-screen device-reset
    mechanism, a shared skeletal bone-blend utility, and 6 supporting renames. 9
    renames. Full writeup: `RE_NOTES_rendering_system.md`.
23. **Cracked an earlier session's "candidate Ubertrick/camera/track named-queue
    subsystem" bookmark**: `GfxContext_InitCameraModeRecords` (called from
    `GfxContext_Init`) initializes named camera-mode presets; `Level_GetCurrentTrackNameTag`
    is the short-name companion to `TrackTable`; `GfxContext_SetPerPassCallback`
    confirmed via its consumer. 4 renames. Full writeup:
    `RE_NOTES_rendering_system.md`.
24. **Found `Text_RenderGlyphString`**, the core glyph-by-glyph string
    rasterizer underlying the text system (calls `Font_GetGlyphMetrics`
    directly). Confirmed the `exUT_`/`mesablan` tags seen throughout this
    session's rendering work were never a real named-queue system -- just
    aliased scratch storage. 3 renames. Full writeup:
    `RE_NOTES_rendering_system.md`.
25. **Closed out the entire exUT_-tagged render-submission family**: generic
    quad/rect primitives, a screen-space text variant, fog-aware particle-batch
    submission, all remaining wrappers, plus 5 more GfxContext-vtable getters.
    17 renames. The GfxContext vtable thread is now functionally closed (6
    genuinely stuck singles remain). Full writeup:
    `RE_NOTES_rendering_system.md`.
26. **Extended xbox.gdt type work to the input pipeline**: real signatures on
    `XGetDevices`/`XInputOpen`/`XInputGetCapabilities`, real types applied
    throughout `Input_PollDevice`/`GamepadInputDevice_Construct`. Named
    `GamepadInputDevice_InitVibrationCapabilities`. Confirmed a new nuance:
    the type-set endpoint's response text alone is never a reliable success
    signal, always re-decompile.
27. **Found the `.cml` camera-script runtime interpreter**, closing the last
    open gap in that format's investigation: `VenueStaging_SetActiveCameraMode`,
    `VenueStaging_Tick`, and `VenueStaging_TickCameraScriptCommand` (the
    43-opcode keyframe processor, firing level scripts via opcode 0x25). 4
    renames. Full writeup: `RE_NOTES_camera_system.md`.
28. **Pushed deeper into the camera-script opcode handlers**: found the eased
    camera-mode blend transition, an exit-transition handler, and the
    underlying interpolation implementation. 3 renames. Full writeup:
    `RE_NOTES_camera_system.md`.
29. **Closed the "shader-table management" mystery**: required raw disassembly
    (Ghidra's decompiler hid implicit `this` parameters on 2 functions in the
    chain). Result: no general shader-table class -- it's the disc-error
    screen's own dedicated emergency font resource, triggering the classic
    Xbox "freeze until reset" disc-error behavior. 3 renames + 1 data rename.
    Full writeup: `RE_NOTES_rendering_system.md`.
30. **Closed out `AggressionManager`'s accessor helpers**: decoded the 12x12
    relationship-matrix cell layout, found a relationship-decay/relaxation
    mechanic and the Buddy/Friend/Rival/Enemy category resolver. 6 renames.
    Crossed 1,000 named functions. Full writeup:
    `RE_NOTES_application_boot.md`.
31. **Major structural break on the trick-score-writer mystery**: raw
    disassembly revealed `Component_UpdateAll` operates on 3 fixed component-
    list heads embedded in the Rider object, each a NodeBase-style circular
    list -- definitively answers "whose list is it" for the first time. Full
    trace: `RE_NOTES_rider_update_chain.md`.
32. **Found the component-slot constructor** for the trick-score-writer thread:
    `Rider_ConstructComponentSlots` builds the 3 fixed component slots (generic
    placeholder vtable at construction), `Component_InitEmptyList` confirms the
    exact list field layout. 2 renames. Full trace:
    `RE_NOTES_rider_update_chain.md`.
33. **Found the actual node-insertion function** for the trick-score-writer
    thread: traced the full chain from `RiderAnimation_TriggerByEventCode`
    through `Component_InsertNode`, a genuine doubly-linked-list push-front --
    the first confirmed component-attach operation found in this project. 1
    rename. Full trace: `RE_NOTES_rider_update_chain.md`.
34. **Resolved the `+0x28` vs `+0x50` component-slot ambiguity**: traced a
    second path (`Rider_ResetSubsystemBuffers` -> `ComponentSlot_ResetAndReseedBdrSeq`
    -> `New_BdrSeq_2`) into the same slot struct and reconciled it against
    `Component_InitEmptyList`: each 0x58-byte slot embeds TWO separate lists,
    not one at two bases -- `+0x28` is a BdrSeq animation-event queue,
    `+0x50` is the active-component list `Component_UpdateAll` walks every
    frame. Both insert and traversal sides of this mechanism are now fully
    understood. Also renamed the adjacent `+0x28`-queue utility cluster
    (`BdrSeqQueue_Count`/`RemoveAndDestroyNode`/`DestroyAllAndReset`), which
    independently corroborates the two-lists finding. 4 renames. Full trace:
    `RE_NOTES_rider_update_chain.md`.
35. **Ruled out the HUD side of the trick-score mystery entirely**: after 3
    search angles for the `+0x50` component-list inserter came up empty
    (documented honestly as exhausted), pivoted to a raw-bytes search for
    `rider+0x5780`/`+0x5784` (the tagged-event array the HUD's score-popup
    system reads) and found `HUD_TickRiderDisplayState` — a large,
    previously completely unknown per-frame HUD tick — plus
    `HUD_SelectActivePanelMask`. Both confirmed to only read the array,
    never write it, conclusively closing off the HUD side. 3 renames. Full
    trace: `RE_NOTES_race_hud.md`.
36. **Found the game's actual main-loop/frame-pump mechanism** (different
    subsystem, per user request): traced from the input pipeline's flagged
    "InputCache consumer not found" lead through `InputManager_PollDevicesIntoCache`,
    `Application_TickFrame`, and `Application_FrameTimerCallback` — a
    self-rescheduling `timeSetEvent` timer callback, not a conventional loop
    anywhere in the call graph. This is the first time this project has
    identified how the game's per-frame loop is actually driven. 7 renames
    (2 newly-created function boundaries). Full trace:
    `RE_NOTES_control_scheme.md`, `RE_NOTES_application_boot.md`.
37. **Resolved the previous entry's honest caveat**: confirmed
    `Application_FrameTimerCallback` and `Application_ArmFrameTimer` both
    operate on the `XBoxExecutionMan` singleton consistently, switching to a
    separate `Application` object only for the one call into
    `Application_TickFrame`. Found `XBoxExecutionMan`'s remaining vtable
    slots (`_Shutdown`/`_WaitForFrameEvent`/`_SignalFrameEvent`) and its
    constructor — `_SignalFrameEvent` (slot 4) is confirmed to be
    `Application_TickFrame`'s mystery final per-tick dispatch, a Win32
    `SetEvent` on every frame. 5 renames (2 newly-created function
    boundaries). Full trace: `RE_NOTES_control_scheme.md`.
38. **Different direction again: found the BdrSeq animation-event queue's real
    consumer.** `InGameState_LoadLevel`'s `"PREAI"`/`"PostAI"` tagged
    allocations turned out to be real `NodeRegistry` types 5/0xb whose
    `Update` methods (previously un-analyzed code) drive the entire per-rider
    trick-animation lifecycle: `RiderAnimEvents_ProcessTriggeredQueue`
    (activate) and `RiderAnimEvents_TickActiveQueue` (tick, including real
    stick-direction animation-clip selection), plus a newly-found
    `AudioSystem_DispatchAnimationCueEvent` firing audio cues per animation
    phase. Closes the queue's full insert→activate→tick→destroy lifecycle;
    what calls `PREAI`/`PostAI` every frame remains open. 11 renames, 2-3
    newly-created function boundaries. Full trace:
    `RE_NOTES_rider_update_chain.md`.
39. **Self-correction**: caught a real naming mistake from the entry above —
    the `+0x22` flag is a completion flag (set by the newly-found
    `BdrSeq_TickPlayback`/`AnimTimer_AdvanceAndDetectCompletion`), not a
    trigger/pending flag. Corrected 4 names accordingly
    (`RiderAnimEvents_ProcessCompletedQueue` etc.), old ones kept commented
    out per project convention. Clarified the per-type handlers as a genuine
    multi-phase animation state machine (tear down + chain to next phase).
    6 renames. Full trace: `RE_NOTES_rider_update_chain.md`.
40. **Finished the per-type tick-handler sweep**: read and named all 7
    remaining `BdrSeqEvent_DispatchTickByType` handlers (types 5-11, real
    trick-animation clip selection/blending logic), plus
    `BdrSeq_AdvanceBlendTimersAndDetectCompletion` — a second, independent
    confirmation of where the `+0x22` completion flag gets set, for
    `BdrSeq`'s richer multi-channel case. Every case in the tick dispatcher
    is now named. 8 renames. Full trace: `RE_NOTES_rider_update_chain.md`.
41. **Half-resolved "what calls PREAI/PostAI"**: `GameState_ResetTransientTriggerNodes`
    has a second dispatch mechanism — a raw bucket-walk over a literal
    type-ID array `{2, 5, 7, 8}`, calling the real `Update` slot on each.
    Type 5 is `PREAI`, so it does have a confirmed call site after all.
    Type `0xb` (`PostAI`) still doesn't. No renames. Full trace:
    `RE_NOTES_rider_update_chain.md`.
42. **New direction: mapped `SnowFallMan`**, a previously-unexplored tagged
    object from `InGameState_LoadLevel`'s allocation list — a genuine
    8-emitter falling-snow particle system with camera-relative scroll
    tracking and its own D3D render calls. Confirmed it's `NodeRegistry`
    type 2 from the earlier `{2,5,7,8}` array, closing one more piece of
    that puzzle (though its own Update slot is a no-op; the real logic
    runs through 2 other vtable slots, per-frame caller not found). 8
    renames. New file: `RE_NOTES_weather_effects.md`.
43. **Continued: `LessonMan`, the tutorial system.** Only constructed in
    `GameMode_Current==6`. A genuine lesson-step state machine (enter/exit/
    tick per step) plus its own overlay renderer, with a rich constructor
    that registers 30 real controller-icon textures keyed by the player's
    actual control scheme. Same `NodeRegistry`-type/no-op-Update/no-static-
    caller pattern as `SnowFallMan` — now confirmed as a second instance,
    suggesting a shared dispatcher not yet found. 5 renames. New file:
    `RE_NOTES_tutorial_system.md`.
44. **Continued: `PowerFX Particles`**, a 12-category particle pool — third
    confirmed instance of the same architecture, but this time with a real
    static caller chain. `PowerFXParticles_ActivateCategory`'s only callers
    are inside one of the 15 `Rider_UpdatePhysicsState` sub-calls flagged
    unexplored back at the very start of this session's trick-score-writer
    push. Opened it: **`Rider_UpdateUberTrickGlowFX`** (moderate
    confidence) — activates/deactivates two particle-glow points tracking
    limb/board positions, plausibly the Über Trick charge visual. 7
    renames. New file: `RE_NOTES_powerfx_particles.md`.
45. **Major find: the rider-vs-rider collision system.** Kept pushing on the
    15 flagged `Rider_UpdatePhysicsState` sub-calls (5 turned out to be
    trivial generic math utilities). The largest (303 lines) turned out to
    be a previously completely unexplored, substantial system:
    **`Rider_ProcessCollisionsWithOthers`** — real collision detection (a
    proximity scan + a genuine broad-phase collision-pair binary tree) and
    elastic-collision physics response (`Rider_ApplyCollisionImpulse`).
    Genuine cross-system connection: the collision-eligibility gate reads
    through the exact same shared lookup table this session's `BdrSeqEvent`
    dispatch tables use. 9 renames. Crossed **20%** named functions. Full
    trace: `RE_NOTES_rider_update_chain.md`.
47. **Closed 3 more of the 15-function list**: `Rider_SelectLocomotionAnimState`
    (a clean directional-animation selector tied into `RiderEvent`),
    `Rider_UpdateSpeedIntensityFX`, `Rider_AccumulateCameraShakeInputs`. 11
    of the original 15 flagged functions now opened, 4 remain genuinely
    unconfirmed. 3 renames. Full trace: `RE_NOTES_rider_update_chain.md`.
48. **Fresh direction: `TerrainNode`.** Fourth confirmed instance of the
    "NodeBase-derived InGameState subsystem, no-op generic slots" pattern —
    but this one plugs directly into the already-documented terrain
    system, calling `Terrain_UpdateObjectVisibility`/
    `Terrain_GetCellRangeForBounds`/`TrackSegment_GetByIndex` to drive
    per-frame visibility culling and track-prop LOD. 4 renames. Added to
    `RE_NOTES_terrain_collision.md`.
49. **Fresh direction: `VideoStreamMan`.** Fifth confirmed instance of the
    recurring architecture, but a genuinely new subsystem: a small pool of
    simultaneous in-game video-texture streams, built on the same
    low-level decode API as the already-documented `VideoPlayer` FMV class.
    5 renames. Added to `RE_NOTES_rendering_system.md`.
50. **Followed up the VideoStreamMan side-lead**: the 2 unnamed functions
    next to `VideoPlayer_IsStopped` turned out to be real, heavily-used
    `VideoPlayer` internals — `VideoPlayer_FlushPendingPackets` and
    `VideoPlayer_FindNextChunkByMagic` (a `"MPCh"`-magic packet finder,
    called 6 times total). 2 renames. Added to
    `RE_NOTES_rendering_system.md`.
51. **A real developer debug menu that shipped in retail.** `"DebugMenu"`
    (unconditionally constructed) has real human-readable strings still in
    the binary — "BX Debug Menu", "Instant Replay", "Restart Race",
    "Render/Game/Sound Options", "Exit the Game". Richest instance of this
    session's recurring architecture (6 of 7 vtable slots real logic).
    Bonus: an embedded per-tag memory-usage debug page revealed several
    brand-new tagged-allocator names ("Splinepath"/"MeshAnim"/"AIPaths"/
    "EventPaths"/etc.), checked and ruled out as a `NodeRegistry` type
    table. 8 renames. New file: `RE_NOTES_debug_menu.md`.
52. **Deep-research follow-up, caught a real mistake mid-trace**: traced
    `DebugMenu_CleanupAfterClose`'s true `this` via raw disassembly and
    found it was wrongly named — it's the `AudioSystem` singleton, called
    from 9 unrelated sites. Corrected to `AudioSystem_EndOverlayDucking`,
    found its counterpart `AudioSystem_BeginOverlayDucking` and
    `AudioSystem_PlayUIClickSound`. Bonus: DebugMenu's "Instant Replay"
    trigger code led to a large post-race state machine,
    `ResultsScreen_HandleTransitionState` — confirming DebugMenu hooks into
    real game state machines, not dead debug-only code. 3 new renames + 1
    correction. Full trace: `RE_NOTES_debug_menu.md`.
54. **`SkyNode`/`ModelsNode`, and a nice cross-validation.** `SkyNode` is
    the per-track skybox system — its 12-entry sky-name lookup table,
    read directly from memory, independently cross-validates the
    already-documented `TrackTable` short-codes from a completely
    different system. `ModelsNode` is a genuine companion to `TerrainNode`:
    the actual track-side prop/decoration mesh draw step (confirmed via a
    matching 162-segment loop bound). Also closed `OverlayNode` as nothing
    new (it's the already-documented `OverlayManager`). 8 renames. Full
    trace: `RE_NOTES_terrain_collision.md`.
55. **Major find: cracked 2 of the last 4 unconfirmed `Rider_UpdatePhysicsState`
    sub-calls.** Every rider tracks its own distance-along-track position
    via an embedded spline path (`SplinePath_FindClosestPoint`/
    `EvaluateAtDistance`) and separately queries a scripted-event system
    keyed by track distance (`Rider_UpdateTrackEventTriggers`). Directly
    connects to 2 brand-new tag names `DebugMenu` surfaced earlier
    ("Splinepath"/"EventPaths"). Honest caveat: the computed steering
    angle's consumer (plausibly AI-rider steering) not found. 4 renames.
    13 of 15 flagged functions now opened. Full trace:
    `RE_NOTES_rider_update_chain.md`.
53. **Exhaustive (negative) result on how `DebugMenu` opens**: checked every
    static angle — `InGameState`'s own tick/loading functions, every
    neighboring function in its code region, a targeted binary search for
    the storage-offset reference. Found only construction/teardown, no
    activation path. Consistent with this project's "stripped for retail"
    pattern; a real exhausted search, not an early stop. No renames.
57. **Cracked another: a spatial ambient-zone audio influence system.**
    `Rider_InitAmbientZoneInfluences` (the last fully-unexplored function
    from the original 15) is a genuine spatial-hash-grid query tying
    directly into the already-documented `AudioSystem`/`AmbientZone`
    classes — the real runtime backing store for how a rider's position
    finds and blends nearby ambient zones. 5 renames. 14 of 15 flagged
    functions now opened. Full trace: `RE_NOTES_rider_update_chain.md`,
    cross-referenced in `RE_NOTES_audio_system.md`.
58. **`FogMan`/`FogVolume` — completes the entire tagged-object sweep.**
    The last untouched tag from `InGameState_LoadLevel`'s allocation list
    is a genuine volumetric cloud/fog-volume rendering system (`"cloud"`
    tagged allocator). Bonus: resolved a previously-"stuck" `GfxContext`
    slot sharing a global with FogMan's own lookup
    (`GfxContext_ExtractQuadVertexAttributes`). 8 renames. Every tag from
    the original allocation list is now individually explored. Full trace:
    `RE_NOTES_weather_effects.md`.
59. **`FogVolume`'s 4 cloud-node helper functions named.** Follow-up to
    entry 58: `FogVolume_BuildCloudPuffs`'s real sub-calls —
    `FogVolume_BuildCloudNodeArray` (allocates the `"cloudnodes"`-tagged
    buffer, seeds initial nodes, then procedurally synthesizes extra puffs
    by randomly blending existing node pairs), `FogVolume_ResetCloudNode`/
    `FogVolume_BlendCloudNodes` (trivial reset + the core interpolation
    primitive), and `FogVolume_TransformAndSortCloudNode` (per-node view/
    projection transform via `Matrix_TransformVector4` + depth-sort/fade
    value, called from `FogVolume_DrawCloudPuffs`). 4 renames — 855 total
    renames (850 functions + 5 data), 1,127/5,358 (≈21.04%) live-counted.
    Full trace: `RE_NOTES_weather_effects.md`.
60. **RESOLVED: `FUN_00033790`, the last of the original 15 flagged
    `Rider_UpdatePhysicsState` sub-calls, closing that whole multi-session
    thread.** Found via its own setter/sibling function
    (`Rider_RegisterCollisionGrudge`, called right after every successful
    `Rider_ApplyCollisionImpulse`) — together they form a previously
    undocumented **rider-vs-rider grudge/rivalry-commentary system**: a
    per-partner "grudge intensity" byte that builds up +15 per collision
    (capped per-partner), maps through thresholds into a 3-tier reaction
    level, and on the top tier chance-rolls (`Probability_RollCategoryThreshold`)
    a rivalry commentary/VO event (`Commentary_TryTriggerRivalryReaction` →
    `Commentary_QueueRivalryEvent`); `Rider_DecayTrackedGrudgeAfterCooldown`
    (was `FUN_00033790`) is the delayed fade-out once a rivalry goes cold.
    Very likely tied to the already-documented `AggressionManager`
    Buddy/Friend/Rival/Enemy matrix, exact connection not traced further.
    5 renames. **860 total renames (855 functions + 5 data), 1,132/5,358
    (≈21.13%) live-counted.** Full trace: `RE_NOTES_rider_update_chain.md`.
61. **Bonus find: `Commentary_QueueEvent`, a whole undiscovered voice-line/
    commentary event queue manager.** Found by tracing
    `Commentary_QueueRivalryEvent` one level deeper — it has **~29 distinct
    small wrapper callers**, each stamping its own unique opcode constant,
    structurally unrelated to the already-documented `ScriptVM` opcode
    system (verified directly: `Camera_WarpToTarget`/`HUD_ShowTimeGapCallout`
    don't call it — corrected an over-general claim made in the same pass).
    Named the core mechanism: probability-gated simple events vs.
    category events that claim/evict a playback slot
    (`Commentary_ClaimOrEvictPlaybackSlot`) and get stored into a fixed
    slot table; plus `RNG_ChooseWeightedCandidate`, a likely
    "weighted-pick-avoiding-recent-repeats" helper. Most of the ~29
    wrapper callers and several inner sub-helpers remain unread — a rich,
    low-risk thread for later. 5 renames. **865 total renames (860
    functions + 5 data), 1,137/5,358 (≈21.22%) live-counted.** Full trace:
    `RE_NOTES_rider_update_chain.md`.
62. **Confirmed the grudge system's connection to `AggressionManager`,
    sampling 3 more `Commentary_QueueEvent` wrapper callers.**
    `Rider_CheckGrudgeQualifiesForReaction` (was `FUN_001230b0`) reads the
    exact same grudge-tracking fields as `Rider_RegisterCollisionGrudge`
    directly alongside `AggressionManager_GetRelationshipField3` --
    upgrading the earlier "very likely tied to AggressionManager" note to
    definitively confirmed. Also found `Rider_TriggerOvertakeCommentary`
    (was `FUN_00125d30`, a proximity/overtake-based rivalry-commentary
    trigger, distinct from the collision-triggered path),
    `Character_ResolveVoiceBitmask` (character-index to per-character
    voice bitmask, matching the 11-character roster), and
    `Commentary_TriggerAnimationCueEvent` (was `FUN_00123780`, called from
    the already-named `AudioSystem_DispatchAnimationCueEvent`). 4 renames.
    **869 total renames (864 functions + 5 data), 1,141/5,358 (≈21.30%)
    live-counted.** Full trace: `RE_NOTES_rider_update_chain.md`.
63. **Resolved `SpeechSlot_SetLineParamsAndProcess`/`SetLineID`/
    `ProcessOrExpire` via disassembly-level register tracing (not decompiler
    inference), per explicit "stop guessing, verify deeply" instruction.**
    Traced `Rider_TriggerOvertakeCommentary`'s `this` back through 2 hops to
    a genuine sorted rider-array extraction (confirms Rider), and
    `SpeechManager_StopLine`'s `this` back through its 3-hop construction
    chain to `AudioSystem_Construct` (confirms the global `AudioSystem`
    singleton). Both traces are solid, proving the SAME field
    (`this+0x3988`) is used by two unrelated owner types -- a genuinely
    shared, reusable "speech/reaction slot" sub-component, not a guess.
    Also caught and fixed an overly-narrow first-draft name
    (`Commentary_TryProcessQueuedSlot` -> `SpeechSlot_ProcessOrExpire`) in
    the same pass upon finding the `SpeechManager_UpdateActiveLines`
    connection. 3 renames + 2 corrected script comments. **872 total
    renames (867 functions + 5 data), 1,144/5,358 (≈21.35%) live-counted.**
    Full trace: `RE_NOTES_rider_update_chain.md`.
64. **RESOLVED: the score-writer mystery, this project's single most
    actively-hunted-for question across many sessions.** Found via
    disassembly tracing out from the commentary-wrapper cluster into
    `Rider_UpdateTerrainContact`'s call into an embedded Rider sub-object
    at `rider+0x5630` (a `TrickCombo` scoring tracker). Two independent
    trick-completion paths (`TrickCombo_ScoreRailCompletion`/
    `ScoreAirCompletion`, was `FUN_0005ef70`/`FUN_0005f1c0`) both contain
    `*(int*)(this+0xe0) += bonus` where `this=rider+0x5630` -- **0x5630 +
    0xe0 = 0x5710**, the exact field every earlier session searched for.
    Explains why the earlier exhaustive byte-search found nothing: the
    write is expressed as a local offset off an already-rebased pointer,
    never as a literal `+0x5710` displacement anywhere in the binary. Also
    found `Trick_FormatComboName` (was `FUN_0005d030`, content-confirmed
    via live string-table reads: "50/50 Rail", "Switch 50/50 Rail", "BS
    Rail"/"FS Rail", "180"/"360"/"540"/"1800", "Late", "Double"/"Triple",
    "Front Flip"/"Back Flip"/"Rodeo" -- the actual SSX dynamic trick-name
    generator), `TrickCombo_StartNewSequence`, `TrickCombo_NotifyTrickLanded`,
    and `HUD_DispatchScoreEventCallout`. 6 renames. **878 total renames
    (873 functions + 5 data), 1,151/5,358 (≈21.48%) live-counted.** Full
    trace: `RE_NOTES_trickcombo_scoring_resolved.md`.
65. **The full `TrickCombo` scoring formula, resolved.** Immediate
    follow-up: `TrickCombo_ComputeTrickPoints` (was `FUN_0005d150`) is the
    actual base point-value formula (accumulated difficulty * multiplier *
    scale constant, rounded down to nearest 10); `TrickCombo_GetStreakBonusValue`
    (was `FUN_0005d6b0`) is a combo-streak bonus table (0/4000/8000/12000/
    16000, confirmed as the inverse of the already-documented, unrelated
    `Trick_GetScoreTier`); `TrickCombo_CountRecentRepeats` (was
    `FUN_0005d790`) is a genuine, previously-undocumented anti-farming rule
    -- divides the score by (repeatCount+1) if the same trick was just
    repeated, checked against a 5-entry rolling history;
    `TrickCombo_EncodeTrickRecord` (was `FUN_0005d860`) is the encoder that
    converts raw spin/flip physics state into the packed trick-record
    bitfield `Trick_FormatComboName` decodes. 5 renames (11 total for the
    whole `TrickCombo` investigation). **883 total renames (878 functions
    + 5 data), 1,156/5,358 (≈21.58%) live-counted.** Full trace:
    `RE_NOTES_trickcombo_scoring_resolved.md`.
66. **Confirmed the real SSX Tricky Uber Trick name table by reading live
    string content.** `UberTrickSpinFlipToNameIndex` (was `DAT_001aad48`)
    maps spin+flip combos to an index into `UberTrickNameTable` (was
    `PTR_DAT_001aaa58`) -- the actual name list: real named tricks at low
    indices ("Mc Twist", "Haakon Flip" -- genuine professional snowboard
    tricks) escalating into SSX's signature comedic
    increasingly-dangerous-sounding names as spin+flip count rises
    ("Ambulance Trip", "Hospitalized", "Multiple Fracture", "Roadkill").
    3 data renames. **886 total renames (878 functions + 8 data),
    1,156/5,358 (≈21.58%) live-counted** (function count unchanged, data
    labels only). Full trace: `RE_NOTES_trickcombo_scoring_resolved.md`.
67. **Resolved the trick-type-to-voice-line commentary dispatch, closing
    the `TrickCombo` loop end to end.** `Commentary_DispatchTrickLandedReaction`
    (was `FUN_00123fa0`, `this`=`AudioSystem` singleton) decodes the trick
    record's grab/spin/flip/rail bitfields through two large switch-table
    lookups converting trick-component IDs into specific voice-line IDs,
    dispatching via `Commentary_QueueTrickTypeReaction`/
    `QueueTrickVariantReaction`; `Commentary_DispatchTeamTrickReaction` is
    the simpler team-mode sibling. Confirms `this+0x6e1c`/`+0x6e20` (fields
    `TrickCombo_ScoreRailCompletion` writes on `AudioSystem`) are a
    cooldown pair for this exact path -- the full loop is now mapped: land
    a trick -> compute score -> format name -> resolve trick type -> queue
    the specific voice line. 5 renames. **891 total renames (883 functions
    + 8 data), 1,161/5,358 (≈21.67%) live-counted.** Full trace:
    `RE_NOTES_trickcombo_scoring_resolved.md`.
68. **Swept 5 more `Commentary_QueueEvent` wrapper callers.**
    `Commentary_DispatchRiderEventReaction` (was `FUN_00125140`) ties
    trick-value tiers (same 4000/8000/12000/16000 thresholds as
    `TrickCombo_GetStreakBonusValue`) directly into event-type codes
    matching the already-documented `RiderEvent_DispatchTypeA`/`TypeB`
    system. `Rider_TriggerProximityCommentary` (was `FUN_00123150`) is a
    simpler sibling of `Rider_TriggerOvertakeCommentary` (same-segment
    proximity, not overtake-specific). Plus `Commentary_TryTriggerLimitedReaction`
    (a genuine "max 6 times per race" reaction), `Commentary_TryTriggerFlagReaction`,
    `Commentary_TryTriggerGenericReaction`, and their 6 wrapper targets.
    11 renames. **902 total renames (894 functions + 8 data), 1,172/5,358
    (≈21.87%) live-counted.** Full trace:
    `RE_NOTES_trickcombo_scoring_resolved.md`.
69. **The ENTIRE `Commentary_QueueEvent` wrapper cluster is now closed** --
    every one of the original ~29 callers found via `xrefs_to` is named.
    Final batch: a "solo/single-rider commentary" sub-cluster (filler
    lines with a fixed "announcer" bitmask, speed-threshold and time-gap
    reactions, simple/misc single-line triggers), plus
    `Commentary_DispatchRelationshipTierReaction` -- the direct caller of
    `Commentary_QueueRivalryEvent`, calling `AggressionManager_GetRelationshipField3`
    directly and independently confirming the `AggressionManager`-grudge
    commentary connection via a second code path. 21 renames. **This
    closes the whole `Commentary_QueueEvent` investigation**: the queue
    manager, every wrapper, the trick/team/overtake/proximity/solo
    dispatch paths, and the `AggressionManager`/`SpeechManager`/`TrickCombo`
    tie-ins are all mapped end to end. **923 total renames (915 functions
    + 8 data), 1,193/5,358 (≈22.27%) live-counted.** Full trace:
    `RE_NOTES_trickcombo_scoring_resolved.md`.
70. **New direction: resolved the `OverlayManager` singleton-access chain,
    found the `SaveReplayOverlay` family.** Picked up the standing "who
    writes `SaveOverlay`'s data-record content" open item. Traced
    `InGameState_LoadLevel`'s `OverlayManager_Construct` call via raw
    disassembly to the concrete chain `Application(DAT_001e3c7c) -> +0x72c
    = InGameState -> +0x38 = OverlayManager -> +0x34 (slot 0xd/19) =
    SaveOverlay` -- previously only described as "a field." The record-
    content writer itself remains unfound, but reading the rest of
    `OverlayManager_Construct`'s 19 panels found 3 new ones (slots
    0x13/0x14/0x15) sharing `SaveOverlay`'s size and shape, content-
    confirmed via the `kOVSaveReplay`/"Save Replay" localized string -- a
    whole separate `SaveReplayOverlay` family. 10 of 19 panels now
    identified. 3 renames. **926 total renames (918 functions + 8 data),
    1,196/5,358 (≈22.32%) live-counted.** Full trace:
    `RE_NOTES_player_snapshot_system.md`.
71. **Checked the remaining 9 unidentified `OverlayManager` panels —
    mostly structurally inert.** Sampled both size categories; found the
    same "N of M vtable slots are stripped no-op stubs" pattern documented
    dozens of times elsewhere in this project (trivial identity functions,
    generic destructors, no panel-specific logic). Left unnamed rather
    than forcing weak names. Good stopping point for this sub-thread.
72. **RESOLVED: the model-archive wrapper format's actual level-script
    "bytecode" -- there isn't any new bytecode.** Re-extracted `gari.big`
    and parsed the real record structure behind `gari.xsf`'s 20 named
    scripts (previously only the names were read). Each script's data
    offset turned out to be a small list of `{opcode-ID,
    parameter-block}` pairs using the **exact same opcode vocabulary as
    the already-fully-documented `Script_DispatchOpcode`/
    `ScriptVM_DispatchOpcode` systems** -- confirmed via direct byte
    reads, not assumed. This closes the "is there new bytecode to decode"
    question definitively: no, it's configuration data in an
    already-understood format. No Ghidra renames (pure data-format
    analysis, not code RE). Full trace:
    `RE_NOTES_archive_format_decoded.md`.
73. **Found 5 more `RiderEvent`/ground-physics handlers via a coincidental
    byte-search hit.** Chasing the manual's "Snow Crystal" multiplier
    strings led to a false-positive match on animation event code `0x1ff`
    -- but followed its real callers into fresh territory:
    `RiderEvent_ToggleSwitchStance`/`ResetToRegularStance` (the actual
    code behind the manual's "Switch" stance mechanic -- a nice direct
    cross-reference between the manual and the code),
    `RiderEvent_UpdateGroundSteeringState`, `Rider_EvaluateGroundMovementTransition`
    (called directly from `Rider_PhysicsMode1_GroundRide`), and
    `Rider_SetPhysicsMode` (the physics-mode transition dispatcher backing
    the whole `Rider_PhysicsMode1_GroundRide` family). 5 renames -- more of
    the ~30 still-unread `RiderEvent_DispatchTypeA`/`TypeB` case handlers.
    The original crystal-multiplier lead remains unresolved. **931 total
    renames (923 functions + 8 data), 1,201/5,359 (≈22.41%) live-counted.**
    Full trace: `RE_NOTES_rider_update_chain.md`.
74. **Closed `RiderEvent_DispatchTypeA` completely.** Continued through the
    rest of its switch: several trivial sub-state reset handlers, plus
    `RiderEvent_ProcessGrabInput` (the core grab-input-combo resolver,
    directly matching the manual's "press 2+ grab buttons at once for a
    complex grab" description), `RiderEvent_ResolveJumpTakeoffStance` (a
    substantial regular/switch/fakie takeoff-animation selector), and
    `RiderEvent_ApplyGrabDecayScaling`. 11 renames -- every case in
    `RiderEvent_DispatchTypeA` is now named. **942 total renames (934
    functions + 8 data), 1,212/5,359 (≈22.62%) live-counted.**
    Full trace: `RE_NOTES_rider_update_chain.md`.
75. **`RiderEvent_DispatchTypeB` fully closed -- turns out to be almost
    entirely the rail-riding subsystem.** Directly matches the manual's
    "Rail Riding" section: mode-transition handlers
    (`RiderEvent_EnterRailRide`/`RetryRailModeTransition`/
    `CheckRailModeTransition`/`TransitionToRailMode`/`TransitionToGroundMode`,
    confirming `Rider_SetPhysicsMode(2)` = rail-riding physics vs. the
    already-known mode 1 = ground-ride), balance/lean input processing
    (`ProcessRailLeanInput`/`ProcessRailBalanceInput`/
    `ProcessRailBalanceAndExit`/`SmoothSteeringLean`), a genuine
    `RiderEvent_CheckRailComboTimeout` mechanic (no further rail tricks
    within a decay window ends the grind combo), and
    `RiderEvent_UpdateRailRideMovement` -- by far the largest handler in
    the whole cluster, the master per-frame rail-ride physics tick (named
    on overall role, not every branch). Plus one unrelated handler,
    `RiderEvent_UpdateEndRaceFadeSequence` (the race-end fade-to-results
    sequence). 17 renames. **Both `RiderEvent_DispatchTypeA` and `TypeB`
    are now completely closed** -- every case in both switches is named,
    closing the multi-session "~35 RiderEvent handlers, mostly unread"
    thread for good. **959 total renames (951 functions + 8 data),
    1,229/5,359 (≈22.93%) live-counted.** Full trace:
    `RE_NOTES_rider_update_chain.md`.
76. **Two shared rail-riding helpers, closing the loop to `TrickCombo`.**
    `RiderEvent_CheckRailExitJump` (was `FUN_0001f5b0`, called from
    `RiderEvent_UpdateRailRideMovement`) is the manual's "jump off the end
    of the rail with a trick" trigger -- and it directly calls the
    already-named `TrickCombo_StartNewSequence`/`Rider_UpdateCueTimer`,
    confirming rail tricks and air tricks feed the exact same scoring
    pipeline resolved earlier this session. `RiderEvent_RotateOnRail` (was
    `FUN_00024aa0`) is the "rotate CCW/CW on the rail" mechanic, dispatching
    8 stance x direction rotation animations. 2 renames. **961 total
    renames (953 functions + 8 data), 1,231/5,359 (≈22.97%) live-counted.**
    Full trace: `RE_NOTES_rider_update_chain.md`.
77. **Caught and fixed an overreach: "physics mode 2 = rail-riding" was
    wrong.** While tracing `Math_WrapAngleToRange`'s (was `FUN_0001eac0`,
    a generic angle-wrap math utility, 20 xrefs) other callers, cross-
    checked `Rider_PhysicsMode2_GroundContact`'s own 637-line body
    directly against the confirmed rail-specific fields -- zero overlap.
    Fixed 4 rename comments that had wrongly asserted mode 2 itself was
    "the rail-riding physics mode" (an earlier session had correctly left
    modes 5/6 as "exact identity not resolved" rather than guessing; this
    session's mistake was re-guessing a specific wrong answer). **Likely
    resolved that earlier open question instead**: `Rider_CheckRailAttachmentAlignment`
    (was `FUN_00029ff0`, called from `Rider_PhysicsMode5_NoTerrain`)
    checks angular alignment against a nearby point and transitions into
    `Rider_SetPhysicsMode(6)` -- a strong candidate for the real rail/wall
    attachment scan. The relationship between this and the
    `RiderEvent_DispatchTypeB`/mode-2 rail cluster is honestly flagged as
    unreconciled, not force-unified. 2 new renames + 4 corrected
    comments. **963 total renames (955 functions + 8 data), 1,233/5,359
    (≈23.01%) live-counted.** Full trace: `RE_NOTES_rider_update_chain.md`.
78. **Strengthened the mode-5/6 rail/wall-attachment hypothesis; found the
    airborne physics's predictive terrain gate.** Read
    `Rider_PhysicsMode5_NoTerrain`'s full body: it gates its call to
    `Rider_CheckRailAttachmentAlignment` specifically on
    `ComponentSlot_ResolveCategoryFromType()==0x11` -- a distinct
    component category not seen anywhere else in this project, supporting
    "detect and attach to a nearby grindable/attachable object while
    airborne." Also found `Rider_CheckForwardTerrainClearance` (was
    `FUN_0002a5a0`), a predictive look-ahead ground-clearance check using
    the already-named `Terrain_SampleHeightAt` to decide whether the
    airborne physics tick should keep blending velocity or force a fall
    state. 1 new rename + a strengthened (not force-unified) hypothesis.
    **964 total renames (956 functions + 8 data), 1,234/5,359 (≈23.03%)
    live-counted.** Full trace: `RE_NOTES_rider_update_chain.md`.
79. **NEW SUBSYSTEM, fresh direction: the AI racing-line path file format +
    query API.** Checked `OtherRider` (the CPU racer class) for AI-specific
    virtual methods first -- dead end, every vtable slot was already a
    generic shared `Node_*` method or a plain `Rider` thunk. Found the
    real AI data via the literal `"AIPath\0EventPath\0"` tag string
    (matches the already-extracted-but-never-examined `gari.aip` file and
    the `"AIPaths"`/`"EventPaths"` tag pair from this session's earlier
    `DebugMenu` memory-tag list). Mapped the full load chain:
    `Level_LoadTrackAssets` -> `AIPath_LoadFromFile` -> chunk-type
    dispatch to `AIPathSet_Construct`/`EventPathSet_Construct` (both
    content-confirmed via their literal tag strings) -> per-path parsing
    via `AIPath_ParseFromBuffer`. **`EventPathSet_Construct` is very
    likely the missing constructor for the already-documented
    `Rider_UpdateTrackEventTriggers`'s data source** -- closes that
    earlier open thread's other half. Read `AIPath`'s own 8-slot vtable
    (3 slots in a previously-unanalyzed region -- created functions there)
    to find its query API: `AIPath_QueryZonesInRange` (the core
    distance-range zone query, most likely what CPU AI uses to look ahead
    along its path) plus `AIPath_CheckPointInSpecialZone(FromEnd)` and 3
    parse-time field setters. 11 renames. **Honest gap**: the actual
    external caller of `AIPath_QueryZonesInRange` (the CPU-steering
    decision logic itself) was not found this pass -- a well-scoped next
    thread. **975 total renames (967 functions + 8 data), 1,245/5,365
    (≈23.21%) live-counted** (total function count rose by 6 from the
    newly-created functions). Full trace: `RE_NOTES_ai_path_system.md`.
80. **Found the AI-path race-start assignment site, upgrading an old open
    flag.** The AI-path global turned out to be a fixed-address singleton
    (`0x1e3f38`), so its `xrefs_to` list directly enumerates every
    consumer. Found `Race_ResetPlayerRoster` (already named) assigning
    each rider an AI path per starting-grid slot via the new
    `AIPathSet_GetByIndex`/`Rider_AssignAIPath`. **`Rider_AssignAIPath`
    resets the exact same distance-tracking cache fields
    (`rider+0x374`/`+0x370`/`+0x378`) that the much-earlier-documented
    `Rider_UpdateTrackPathPosition` maintains** -- that function's own
    rename comment had already flagged its look-ahead steering-angle
    output as "a strong, plausible AI-steering candidate... but NOT
    proven"; this significantly strengthens (without fully proving) that
    flag. Also named `EventPathSet_FindIndexByPointer`/
    `AIPathSet_FindIndexByPointer` (debug-telemetry lookup helpers,
    confirming riders track a current-path pointer pair) and
    `AIPathSet_GetCount`. 8 renames. **Still open**: who actually reads
    the steering angle to make a decision. **980 total renames (972
    functions + 8 data), 1,250/5,365 (≈23.30%) live-counted.** Full
    trace: `RE_NOTES_ai_path_system.md`.
81. **Found and named the CPU-rider AI steering triad; caught and fixed two
    self-overclaims while verifying it.** Searched for other literal
    references to the `0x3a0` field offset; `FUN_00035de0` stood out
    (double signal -- also seen earlier via a `rider+0x55e0` search).
    Read three deeply mutually-recursive functions in full:
    `Rider_ComputeAISteering` (was `FUN_00035660`, a 600+ line boids-style
    local rider-avoidance implementation, falls back to path-following
    when avoidance isn't enough), `Rider_FollowAIPath` (was `FUN_00035de0`,
    dynamically re-resolves/reassigns the nearest AI path mid-race and
    **writes the computed look-ahead steering angle directly to
    `rider+0x3a0`** -- a confirmed write site the AI-path thread had been
    missing), and `Rider_ApplyMotionUpdate` (was `FUN_000344a0`, a
    terrain-aware position/orientation commit step). **Two corrections
    caught mid-analysis by checking full xref lists rather than assuming**:
    (1) the commit function was initially named `Rider_ApplySteeringResult`
    but its callers include `Rider_ResetPhysicsState`, `Camera_WarpToTarget`,
    and `ZBoost_Update` -- none AI-related -- so it's a generic commit
    utility, renamed to `Rider_ApplyMotionUpdate`; (2) `Rider_ComputeAISteering`
    was initially called "the master per-tick AI steering function," but its
    only confirmed caller is `RiderEvent_UpdateEndRaceFadeSequence` (the
    post-race fade-out state), with no indirect/vtable references found
    either -- so this triad is a confirmed consumer of the `this+0x3a0`
    field, but only proven to run during end-race coast-down, not general
    mid-race AI. 3 renames. **983 total renames (975 functions + 8 data),
    1,253/5,365 (≈23.36%) live-counted.** Full trace:
    `RE_NOTES_ai_path_system.md`.
82. **Found an actual reader of `rider+0x3a0`, not just a writer -- realized
    entry 81 had answered a different question than the one left open.**
    "Who writes it" (`Rider_FollowAIPath`) and "who reads it to make a
    decision" (the real original question) are different things. Re-ran
    the full `0x3a0`-displacement byte search (all 25 hits, not the
    pre-filtered subset) and checked every containing function. One clean
    hit: the already-named `Rider_TriggerOvertakeCommentary`, which reads
    the *other* rider's `this+0x3a0`, converts it via the newly-named
    **`Math_AngleToSinCos`** (was `FUN_0001e5a0`), and combines it with
    both riders' direction-vector fields to gate rivalry/overtake
    commentary on relative heading. This read runs for ANY nearby rider
    pair, not just AI or end-race state -- supports `this+0x3a0` being a
    general per-rider heading angle maintained for every rider (most
    likely by the already-confirmed-every-frame
    `Rider_UpdateTrackPathPosition`), with `Rider_FollowAIPath` only
    overriding it for AI path-following. 1 rename. **984 total renames
    (976 functions + 8 data), 1,254/5,365 (≈23.38%) live-counted.** Full
    trace: `RE_NOTES_ai_path_system.md`.
83. **Fresh subsystem found as a byproduct: the Career Mode challenge/
    objective record system** (`RE_NOTES_challenge_system.md`). 3 of the 25
    raw hits from the `0x3a0`-displacement search (entry 82) landed in a
    completely unrelated system -- confirmed a coincidental offset
    collision, not rider-related. Read all 3 functions fully: a
    `0x4c`-byte-stride "challenge slot" record array (4 gating flags, a
    score/count value, a name string), driven through a shared polymorphic
    vtable interface. Named `Challenge_RefreshEntryState` (per-slot
    gating/refresh), `Challenge_FormatScoreThresholdText` (progress ->
    localized string), and `Challenge_FormatObjectiveDescription` (the
    ~40-case challenge-type -> description-string switch). **Honestly
    scoped as a tangent, not fully pursued**: most localization string IDs
    undecoded, and the calling UI/menu code (clustered around
    `0x00096xxx`/`0x000e5axx`) isn't yet bounded as functions in Ghidra --
    needs `/create_function` treatment if revisited. 3 renames. **987
    total renames (979 functions + 8 data), 1,257/5,365 (≈23.43%)
    live-counted.** Full trace: `RE_NOTES_challenge_system.md`.
84. **Bounded the challenge-menu's calling UI code, closing the gap flagged
    in entry 83.** Both cluster gaps turned out to be missing function
    boundaries (RET + NOP alignment padding + an un-detected prologue),
    the same class of issue this project has fixed with `/create_function`
    before. Bisected each via raw byte reads and created 5 new function
    boundaries: **`ChallengeMenu_HandleScreenEvent`** (was `FUN_00096630`)
    -- found only via a raw DATA xref (`0x00198d34`), confirming it's
    **vtable-dispatched**, a `Widget`-subclass virtual method driving the
    whole challenge-menu screen's per-challenge-type UI construction;
    **`ChallengeMenu_InitializeScreenState`** (was `FUN_000e5990`) --
    screen entry/setup, registers a state ID; and a 3-function family
    (**`ChallengeMenu_RefreshDescriptionAndRedraw`**/
    **`RefreshDescriptionLabel`**/**`RefreshDescriptionWithTitle`**, were
    `FUN_000e5a30`/`FUN_000e5a70`/`FUN_000e5aa0`) -- small wrappers pushing
    formatted challenge descriptions to display widgets. **Honestly
    scoped**: several more nearby call sites likely form more of the same
    wrapper pattern, not individually bounded this pass. 5 renames. **992
    total renames (984 functions + 8 data), 1,262/5,369 (≈23.50%)
    live-counted** (total function count rose by 4 from the newly-created
    functions). Full trace: `RE_NOTES_challenge_system.md`.
85. **Fixed a stale "zero static callers" claim on `Rider_UpdateSnowSprayFX`,
    found while stirring for a fresh open thread.** That function's rename
    comment (from an earlier session) claimed it had zero static callers
    and was "likely vtable-dispatched on an FXNode-family object" -- never
    re-verified since. A fresh `xrefs_to` check found a perfectly ordinary
    direct-call chain: `Rider_UpdatePhysicsState` -> the already-named
    `Rider_UpdateUberTrickGlowFX` (one of its 15 documented direct
    sub-calls) -> the newly-named **`Rider_UpdateAmbientFXBatch`** (was
    `FUN_00044b40`, runs unconditionally every frame, independent of the
    Uber Trick glow logic in the same caller) -> `Rider_UpdateSnowSprayFX`.
    No vtable dispatch at all -- corrected immediately in both the script
    comment and `RE_NOTES_ubertrick_fx_cluster.md`. `Rider_UpdateAmbientFXBatch`
    batches 5 rider ambient-FX sub-updates (snow spray plus 4 siblings, at
    least 2 sharing its quality-tier gate and one reading the same rider
    velocity fields) -- the 4 siblings not individually named this pass. 1
    rename. **993 total renames (985 functions + 8 data), 1,263/5,369
    (≈23.53%) live-counted.** Full trace: `RE_NOTES_ubertrick_fx_cluster.md`.
86. **Named the 4 sibling functions in `Rider_UpdateAmbientFXBatch`**
    (immediate follow-up, "keep going"). All share `Rider_UpdateSnowSprayFX`'s
    general shape (quality-tier gated, RNG-selected particle ring buffers
    keyed off rider surface/velocity state): `Rider_UpdateSurfaceParticleFX`
    (was `FUN_0003fb70`, gated on a specific surface type or boost state),
    `Rider_UpdateSurfaceFrictionCueFX` (was `FUN_0003f750`, speed+surface
    timer triggering an audio/FX cue via the rider's `+0x58e0` back-pointer
    sub-object), `Rider_UpdateSurfaceSprayFX` (was `FUN_00040060`, projects
    velocity onto the contact-normal-perpendicular plane for a
    surface-skimming spray direction), and `Rider_UpdateAmbientParticleFX`
    (was `FUN_000443e0`, the largest/most complex sibling, named generically
    at structural confidence only). A 6th, conditionally-called function
    (`FUN_00041da0`) was checked but left unnamed -- genuinely more complex,
    not guessed. 4 renames. **997 total renames (989 functions + 8 data),
    1,267/5,369 (≈23.60%) live-counted.** Full trace:
    `RE_NOTES_ubertrick_fx_cluster.md`.
87. **Pushed through the 6th, most complex `Rider_UpdateAmbientFXBatch` call
    too ("keep going hard").** Named **`Rider_UpdateGrindTrailFX`** (was
    `FUN_00041da0`, full 640-line body read) -- distinct in kind from its 4
    burst-particle siblings: maintains a persistent 100-slot fading
    position-history ring buffer (a continuous trail, not a one-shot
    spawn). Gated on the same surface-type field the siblings use, here
    restricted to `{2,5,6}` -- values that coincidentally match the
    still-unresolved rail/wall-attachment physics-mode hypothesis
    (`Rider_PhysicsMode5_NoTerrain`/`PhysicsMode6_NoTerrain`), a plausible
    but not-proven cross-reference suggesting this is a genuine rail-grind
    trail effect. Sources its seed transform either directly from the
    rider (surface 2) or by chasing a pointer at `rider+0x42e8` (surface
    5/6, a plausible "currently-attached rail" pointer, not
    cross-confirmed). 1 rename. **998 total renames (990 functions + 8
    data), 1,268/5,369 (≈23.62%) live-counted.** Full trace:
    `RE_NOTES_ubertrick_fx_cluster.md`.
88. **Refined the `rider+0x42e8` hypothesis from `Rider_UpdateGrindTrailFX`
    with better evidence.** A byte search for other references to that
    field offset found the same null-check-then-deref idiom reused in
    `Rider_ResetPhysicsState` and a new function (`FUN_0003c7e0`, called
    from `Rider_ApplyMotionUpdate`) that uses **both** `rider+0x42e8` AND
    `rider+0x42ec` together -- a confirmed pair of fields, not a single
    "attached rail" pointer as first guessed. Matches
    `Rider_UpdateUberTrickGlowFX`'s own already-documented "two limb/board
    attachment-point positions" far better. Named
    **`Rider_BuildGlowTrailBasisFromAttachment`** (was `FUN_00042f40`) --
    builds a small local reference frame from one attachment point's
    transform for glow-trail positioning. **Caught a false lead**: the
    apparent "mirror" branch call, `FUN_00043340`, turned out to be an
    unrelated GameMode/Team function, not forced into the narrative.
    Corrected the earlier guess in `Rider_UpdateGrindTrailFX`'s comment
    rather than silently editing it. 1 rename. **999 total renames (991
    functions + 8 data), 1,269/5,369 (≈23.64%) live-counted.** Full trace:
    `RE_NOTES_ubertrick_fx_cluster.md`.
89. **Found the confirmed producer of the rider+0x42e8/+0x42ec attachment
    data -- the project's 1,000th total rename.** Full read of
    `FUN_0003c7e0` (called from `Rider_ApplyMotionUpdate`'s tail). Named
    **`Rider_UpdateBoardAttachmentTransforms`**: computes the rider's
    board world-space transform at 2 binding points via a genuine
    board-flex/bend model (skeleton-bone array lookup, rotation-matrix
    composition via `Matrix_Multiply4x4`/`Matrix_TransformVector4`,
    branching on surface-type/`RiderEvent_GetSubState`/
    `ComponentSlot_ResolveCategoryFromType` category `0x10`). **Fully
    closes the write-then-read loop** for the attachment-point pair:
    this function produces the transforms; `Rider_ResetPhysicsState`,
    `Rider_BuildGlowTrailBasisFromAttachment`, and `Rider_UpdateGrindTrailFX`
    all consume them. Confirms (not just suggests) the board bend/
    attachment-point interpretation over the earlier "rail pointer" guess.
    The underlying bone-transform helper cluster wasn't individually named
    -- a well-scoped next thread. 1 rename. **1,000 total renames (992
    functions + 8 data), 1,270/5,369 (≈23.65%) live-counted.** Full trace:
    `RE_NOTES_ubertrick_fx_cluster.md`.
90. **Named 3 of the bone-transform helper cluster** ("cover as much as
    possible" pass): `Matrix3x3_Multiply` (was `FUN_00030990`),
    `Matrix3x3_CopyPackedToPadded` (was `FUN_0003aff0`), and
    `SkeletonBone_GetTransformedPoint` (was `FUN_001093e0`). Left 2 trivial
    forwarders unnamed (deeper unexplored skeleton/animation subsystem, a
    genuine next thread). 3 renames. **1,003 total renames (995 functions
    + 8 data), 1,273/5,369 (≈23.71%) live-counted.**
91. **Closed out the remaining challenge-menu wrapper cluster** ("cover as
    much as possible" pass). Bisected the last 5 boundary gaps, confirming
    the cluster terminates cleanly at the next already-analyzed function.
    Named 4 more `ChallengeMenu_RefreshDescriptionWithTitleAlt2/3/4` string-
    ID variants plus `ChallengeMenu_RefreshDescriptionWithSuffix` (a
    slightly different single-call shape, likely pluralization-aware). 5
    renames. **1,008 total renames (1,000 functions + 8 data), 1,278/5,374
    (≈23.78%) live-counted** (function count rose by 5 from newly-created
    functions). Full trace: `RE_NOTES_challenge_system.md`.
92. **"Make sure not mistake" pass: found and fixed multiple real errors in the
    application-startup/shutdown chain, and found the actual program entry
    point.** Started from `RE_NOTES_control_scheme.md`'s "InputCache history
    consumer" lead, which pointed at `Application_TickFrame`'s final vtable
    dispatch -- verified via raw disassembly that this dispatch is
    `XBoxExecutionMan_SignalFrameEvent` (already resolved elsewhere), a dead
    end for that specific question. While tracing this, found and corrected
    4 real mistakes: (1) **`Application_StateMachineTick` was mislabeled
    "vtable slot 2"** -- it's actually slot 3; slot 2 is a genuinely new,
    tag-confirmed function, **`Application_Purge`** (was `FUN_000a9d70`,
    confirmed via the literal string `"cApplication::Purge\n"`), previously
    dismissed as "not individually traced." (2) **`InputManager_InitOrReset`
    was misnamed** -- its `this` is the Application object itself (proven via
    the same XBoxExecutionMan field offset used elsewhere, plus it nulls the
    global `DAT_001e3c7c` on exit), renamed to **`Application_RunAndShutdown`**.
    (3) **`Application_RunInitialLoadPump` was mischaracterized** as a bounded,
    one-time loading pump -- its body ends in an unconditional `goto` with a
    single quit-flag exit condition, confirming it's actually **the master
    game loop for the entire play session**, renamed to
    **`Application_RunMainLoop`**. (4) **Found the real program entry point**:
    bisected a previously-declined function boundary (an earlier session
    explicitly said "didn't force a function boundary... low expected value")
    at `0x001541a9` -- Ghidra's own auto-analysis immediately recognized it as
    **`XAPILIB::mainXapiStartup`**, closing the entire startup-to-shutdown
    chain: `mainXapiStartup` -> `Application_ConstructAndInitInput` ->
    `Application_RunAndShutdown` -> `Application_RunMainLoop` (the real game
    session) -> shutdown -> `XapiBootToDash`. **Also flagged a real,
    previously-unnoticed deliverable limitation**: `ssx_auto_rename.py`'s
    `rename()` silently skips any address without a pre-existing function,
    meaning re-running the script against a from-scratch `default.xbe`
    reimport would silently no-op every rename at a `/create_function`-only
    address -- documented prominently at the top of the script. 1 new rename
    (`Application_Purge`; the other 3 were name/comment corrections to
    already-counted entries). **1,009 total renames (1,001 functions + 8
    data), 1,280/5,375 (≈23.81%) live-counted.** Full trace:
    `RE_NOTES_application_boot.md`, `RE_NOTES_control_scheme.md`.
93. **"Keep going harder": fully reconciled the sync/async loop relationship
    flagged as an open question in entry 92.** `Application_RunMainLoop`'s
    own `DAT_001ba53c` flag turned out to have a **fixed value of 1 with
    zero writers anywhere** (confirmed dead code in its `==0` branches) --
    following the live branch leads to an unconditional call to the
    already-named `XBoxExecutionMan_WaitForFrameEvent`, a genuine blocking
    `WaitForSingleObject` on the exact same Win32 event
    `Application_TickFrame`'s final dispatch signals via
    `XBoxExecutionMan_SignalFrameEvent`. **This is the complete classic
    architecture**: an OS timer paces `Application_TickFrame` at ~60fps,
    signaling the event; `Application_RunMainLoop` does its own per-tick
    work then blocks on that event until woken, instead of busy-spinning.
    Also chased the `this+0x24` quit-flag lead via `DAT_001df3f4` (read/
    written by `Application_StateMachineTick`) -- turned out to be a
    different, valuable finding (a pending-transition bitmask gating
    FrontEnd/InGame/VideoPlayer-module transitions), not the quit flag,
    which remains genuinely unfound. No new renames (comment/documentation
    corrections only). Still **1,009 total renames, 1,280/5,375 (≈23.81%)
    live-counted.** Full trace: `RE_NOTES_application_boot.md`.
94. **"Go fresh": found and mapped the boot title/cinematic-sequence
    controller** (`RE_NOTES_title_intro_sequence.md`). Noticed the extracted
    `Game Data\data\video\` directory (32 `.mpc` FMV files up to 142MB,
    19 smaller per-track `.xss` files) had never been examined. Traced the
    literal path string `"data/video/eabig.mpc"` to its one reader -- a
    5-slot vtable class tagged `"f3stttl"`. Named all 5 slots:
    `TitleIntroSequence_CheckLoaderReady`/`IsComplete`/`QueueBootVideos`/
    `Tick`/`Render`. `QueueBootVideos` (was `FUN_0007c970`) is the payload:
    gated on `DAT_001df3f4==1` (the same global `Application_StateMachineTick`
    reads as a bitmask -- confirmed shared boot-state variable), it queues
    the EA logo video (`eabig.mpc`) to play first, chained into the SSX
    Tricky opening cinematic (`ssxintro.mpc`) playing next. **Caught and
    fixed a false lead**: a nearby string cluster (`"onTitleOpen"`/
    `"GameLoad"`/`"f3dstvid"`) looked like a hand-built event table but
    turned out to be coincidental data-segment proximity to an unrelated
    `Script_PlayByName` call -- not forced into the narrative. **Also
    caught and fixed an off-by-one function creation** (one byte late,
    caught by cross-checking the vtable's own literal pointer value). 5
    renames + 6 `/create_function` boundary fixes. **1,014 total renames
    (1,006 functions + 8 data), 1,285/5,379 (≈23.89%) live-counted.** Full
    trace: `RE_NOTES_title_intro_sequence.md`.
95. **"Keep going": found the central menu preview-video dispatcher, a
    bigger sibling to `TitleIntroSequence`.** Chased the per-character
    `cv_*.mpc` trigger site; a literal search for `"data/video/cv_brod.mpc"`
    found nothing because the filename is built at runtime via a `printf`-
    style format string `"data/video/cv_%s.mpc"`, found alongside a whole
    cluster of other menu-video path formats (`music`/`audio`/`charactr`/
    `tracks`/`tricks`/`evolve`.mpc). Traced it to its one reader and named
    **`FrontEndVideo_SelectPreviewClip`** (was `FUN_00097230`, needed
    `/create_function`): the central dispatcher choosing which menu-screen
    preview video to queue (via the same `"f3stvid"`-tagged video-widget
    class `TitleIntroSequence` uses), builds the per-character cutscene
    path (`cv_<charactername>.mpc`) at runtime, and also handles 2 special
    promotional videos -- `sled2pr.mpc` ("Sled" being this game's internal
    codename, almost certainly a sequel preview trailer) and `nbapromo.mpc`.
    Its caller vtable slot sits alongside the already-named
    `UI_BuildJukeboxVoicePlayer`, confirming a shared top-level frontend
    controller class (not individually named this pass). 1 rename. **1,015
    total renames (1,007 functions + 8 data), 1,286/5,380 (≈23.90%)
    live-counted.** Full trace: `RE_NOTES_title_intro_sequence.md`.
96. **Continued into the owning class: identified the "DVD Extras" jukebox
    menu screen.** `FrontEndVideo_SelectPreviewClip`/`UI_BuildJukeboxVoicePlayer`'s
    shared vtable (`0x00199348`) sits right after a block of celebrity
    voice-actor credit strings (`"OLIVER PLATT"`, `"DAVID ARQUETTE"`,
    `"DJ PRECISE (aka RYAN WALL)"`, etc.) -- combined with the jukebox
    builder and preview-video music/audio cases, this is unambiguously the
    in-game "DVD Extras" bonus-content menu. Named
    **`DVDExtrasMenu_ScalarDeletingDestructor`** (was `FUN_00096dd0`, slot
    0) to anchor the class; confirmed `Widget`-family inheritance via slot
    2 (`Widget_TickWrapper`); checked 3 more slots, all generic Widget
    cascading setters, not screen-specific. ~24 of ~27 slots left
    unmapped -- class identity established, not chased further. 1 rename.
    **1,016 total renames (1,008 functions + 8 data), 1,288/5,381 (≈23.94%)
    live-counted.** Full trace: `RE_NOTES_title_intro_sequence.md`.
97. **Chased the per-track `.xss` video trigger, found a different
    legitimate subsystem instead.** Found a string/pointer table
    (`0x001b8e00`-`0x001b8e4c`, track-ID short codes + 8 `.xss` path
    pointers) but `/xrefs_to` on it came back empty -- likely computed/
    register-relative indexing, consistent with this project's repeated
    "some tables evade static xref search" pattern. The actual `.xss`
    trigger remains unfound. Checking `TrackInfo_GetRecordByIndex`'s other
    callers instead found **`TrackIntroMusic_SelectAndPlay`** (was
    `FUN_0010eda0`) -- the per-track intro-music selector: looks up the
    current track's name in a `"data/config/intromus.inf"` file and plays
    the matching `"data/audio/<name>"` file with a volume fade-in. Honestly
    documented as a different, tangential finding rather than forced into
    the video-system narrative. 1 rename. **1,017 total renames (1,009
    functions + 8 data), 1,289/5,381 (≈23.95%) live-counted.** Full trace:
    `RE_NOTES_title_intro_sequence.md`.
98. **Decoded the `.mpc`/`.xss` container formats directly from raw file bytes,
    fully exhausting the video-system thread.** `.mpc` confirmed as a real
    MPEG-1/MPEG-2 video stream (literal embedded string "Encoded with MPEG
    Gimex module", standard MPEG start codes) wrapped in an 8-byte `"MPCh"`
    header -- exactly what the already-named
    `VideoPlayer_FindNextChunkByMagic` checks for, closing the loop to the
    already-documented `VideoPlayer` class. `.xss` confirmed as a genuinely
    different, non-MPEG format (`"XSSF"` magic, ADPCM-shaped repeating bit
    patterns, likely audio-only despite living in `data/video/`) -- but a
    literal `/search_bytes` for `"XSSF"` across the ENTIRE binary found
    **zero matches anywhere**, and combined with the earlier empty
    `xrefs_to` results on the path table itself, this is a genuinely
    exhausted dead end (multiple independent techniques, not just
    unchecked) -- likely unused/leftover dev content, or loaded through a
    mechanism outside this binary's reach. 0 renames (file-format analysis
    only). Still **1,017 total renames, 1,289/5,381 (≈23.95%)
    live-counted.** Full trace: `RE_NOTES_title_intro_sequence.md`.
99. **Pushed further into the `DVDExtrasMenu` vtable, then declared this
    whole video/menu investigation genuinely exhausted.** Checked several
    more slots and found a consistent structural pattern: multiple slots
    (input handling, video-cue triggering) all compare `this+0x28` against
    other fields, the same "current sub-screen mode" shape
    `FrontEndVideo_SelectPreviewClip` uses -- confirms one consistent
    state-machine field drives screen-mode-dependent behavior across the
    whole class. Remaining slots are ordinary dispatch cases following
    this same pattern -- diminishing returns, stopped with the class's
    core design confirmed rather than force-naming every generic case.
    Caught and discarded a transcription slip (queried a non-slot address
    by typo, recognized immediately, no bad info recorded). **Status: this
    whole thread -- starting from an unopened video directory, through
    `TitleIntroSequence`, `FrontEndVideo_SelectPreviewClip`,
    `DVDExtrasMenu`, `TrackIntroMusic_SelectAndPlay`, and the `.mpc`/`.xss`
    format decode -- is now genuinely exhausted**: every remaining loose
    end was checked enough to know it's either unreachable via static
    analysis or low-value relative to effort, not simply left unexamined.
    0 renames. Still **1,017 total renames, 1,289/5,381 (≈23.95%)
    live-counted.**
100. **"Go fresh": found and fully mapped `trickdef.dat` and the on-screen
     trick-instruction prompt system.** Picked another untouched extracted
     file, `Game Data\data\tutorial\trickdef.dat` (10080 bytes). Traced its
     one reader forward through the whole display chain: `TrickDef_LoadFile`
     (was `FUN_0005c900`, called from the already-named `InGameState_LoadLevel`),
     `TrickDef_GetRecordByIndex` (was `FUN_0005c930`, revealed the exact
     table shape -- 30 columns x 12 rows of 28-byte records, matching SSX
     Tricky's directional-input+button trick-mapping control scheme),
     `TrickDef_FormatRotationLabel` (was `FUN_0005c980`, builds a compact
     rotation-description string), `TutorialHUD_DrawTrickPrompt`/
     `DrawTrickPromptIcon`/`MeasureTrickPromptWidth`/`ConstructTrickPromptContext`
     (were `FUN_0005cd30`/`FUN_0005ceb0`/`FUN_0005cf40`/`FUN_0005ce80`, 4
     needed `/create_function`) -- the on-screen "do this stick+button
     combo" prompt renderer, using the already-named `HUD_DrawNumberBufferShadowed`/
     `IconAtlas_GetEntry`/`Sprite_DrawAligned`. **Well-anchored, not
     speculative**: `TutorialHUD_ConstructTrickPromptContext` is called
     directly from the already-named `LessonMan_Construct`
     (`RE_NOTES_tutorial_system.md`), confirming the tie-in to the
     tutorial-lesson system. 7 renames (including a self-caught off-by-one
     boundary correction). **1,024 total renames (1,016 functions + 8
     data), 1,296/5,383 (≈24.07%) live-counted.** Full trace:
     `RE_NOTES_tutorial_system.md`.
101. **Closed the trick-prompt widget's vtable entirely, exhausting this
     whole thread.** Read the remaining 2 slots of the widget's small
     vtable (`0x0018a080`): slot 0 = **`TutorialHUD_TrickPromptScalarDeletingDestructor`**
     (was `FUN_00055f40`), slot 2 = **`TutorialHUD_SetCurrentTrick`** (was
     `FUN_0005c950`, the "select which trick to prompt for" setter, both
     needed `/create_function`). All 6 slots now named or accounted for
     (slot 1 is the already-shared `Node_NoOpStub1`). **This is a
     genuinely complete, fully-mapped small widget class** -- the
     `trickdef.dat`/tutorial-prompt thread is exhausted end to end: data
     file -> loader -> table accessor -> label formatter -> the widget
     class that displays it. 2 renames. **1,026 total renames (1,018
     functions + 8 data), 1,298/5,385 (≈24.10%) live-counted.** Full
     trace: `RE_NOTES_tutorial_system.md`.
102. **Found the master per-race audio-config loader, correcting an
     earlier dismissal from this same session.** Picked another untouched
     file, `Game Data\data\config\chant.inf`. Tracing its path string led
     to `FUN_00112870` -- previously dismissed a few entries earlier as
     "tangential, not pursued." Re-examined via raw disassembly and found
     it directly loads `chant.inf`. Found a 7-entry master table of every
     known `.inf` config path, revealing a complete picture: `jukebox.inf`
     -> `AudioSystem_Construct` (already named), `intromus.inf` ->
     `TrackIntroMusic_SelectAndPlay` (already named), `snow.inf` ->
     **`AudioSystem_LoadSnowConfig`** (new, was `FUN_001123f0`), and
     `audio.inf`/`banks.inf`/`crowd.inf`/`chant.inf` ->
     **`AudioSystem_LoadRaceConfigs`** (new, was `FUN_00112870`) -- called
     directly from the already-named `InGameState_LoadLevel`, a
     well-anchored finding. Also named **`AudioSystem_ReloadConfigs`**
     (was `FUN_00111a10`, a sibling entry point, exact distinguishing
     context not fully traced). **Also flagged**: briefly re-investigated
     `btnmap0.dat`/`btnmap1.dat` before realizing they were already fully
     documented in `RE_NOTES_control_scheme.md` -- no new info, noted to
     avoid a repeat detour. 3 renames. **1,029 total renames (1,021
     functions + 8 data), 1,301/5,385 (≈24.16%) live-counted.** Full
     trace: `RE_NOTES_audio_system.md`.
103. **Pinned down `AudioSystem_ReloadConfigs`'s exact context.** Named
     **`FEAudioConfig_Construct`** (was `FUN_00084540`, a tiny wrapper
     immediately calling `AudioSystem_ReloadConfigs`) -- called from the
     already-named `FEInit_Boot`, confirming `AudioSystem_ReloadConfigs`
     is the FrontEnd/menu audio-config entry point, parallel to
     `AudioSystem_LoadRaceConfigs`'s in-race path. Also named
     **`FEAudioConfig_ScalarDeletingDestructor`** (was `FUN_00084cf0`).
     The second caller sits inside a large unanalyzed gap, not bisected
     (low value, primary context already confirmed). 2 renames. **1,031
     total renames (1,023 functions + 8 data), 1,303/5,385 (≈24.20%)
     live-counted.** Full trace: `RE_NOTES_audio_system.md`.
104. **Checked the last untouched `.inf` files.** `speech.inf`/`music.inf`
     were already fully documented; `musicmap.inf` (per-track playlist
     file) was genuinely new. Named **`MusicManager_LoadTrackSongList`**
     (was `FUN_0011ae40`, tag-confirmed) -- called only from
     `AudioSystem_LoadRaceConfigs`, extending that chain one step further
     (5 of the 6 known `.inf` files it touches are now direct calls). 1
     rename. **1,032 total renames (1,024 functions + 8 data), 1,304/5,385
     (≈24.22%) live-counted.** Full trace: `RE_NOTES_audio_system.md`.
105. **Closed the last untraced `.inf` config file: `nascript.inf`, the
     narrator speech-concatenation script.** Named
     **`NarratorSpeech_LoadScriptData`** (was `FUN_00121cc0`, tag-confirmed
     via `"NarrSpeechBreakpoints"`/`"NarrSpeechSamples"` allocation tags):
     parses comma-delimited numeric sample-segment ID lists per track
     section, with a `b`/`B` token marking a synchronization breakpoint
     (`-1` sentinel) -- matching the file's own documented format exactly.
     **Called only from `AudioSystem_LoadRaceConfigs`**, the same central
     loader every other `.inf` file in this thread traces back to. **This
     closes out all 11 known `.inf` config files with a confirmed
     loader** -- a genuinely complete audio-config subsystem map. 1
     rename. **1,033 total renames (1,025 functions + 8 data), 1,305/5,385
     (≈24.24%) live-counted.** Full trace: `RE_NOTES_audio_system.md`.
106. **"Fresh": closed the long-open `snow_sky`/`trick_sky`/`mesa_sky`/
     `elysium_sky` TrackTable-row question with a direct data read.**
     Read `TrackTable`'s raw bytes (all 12 rows) instead of guessing --
     confirmed `snow`->Snowdream (row 1, the previously-unmatched row),
     `trick`->Trick Tutorial (row 10, the tutorial-mode track -- ties
     nicely to this session's `trickdef.dat` work), `mesa`->Mesablanca
     (row 3), `elysium`->Elysium Alps (row 2). **All 12 `TrackTable` rows
     and all 12 `SkyNode` short-codes are now fully cross-confirmed with
     zero remaining ambiguity.** 0 renames (data confirmation + comment
     correction only). Still **1,033 total renames, 1,305/5,385 (≈24.24%)
     live-counted.** Full trace: `RE_NOTES_results_screen.md`.
107. **Checked, genuinely not found: the narrator-commentary consumer.**
     `NarratorSpeech_LoadScriptData` parses `nascript.inf` into fields on
     the `AudioSystem` singleton, but who reads them to actually play a
     line wasn't found -- ruled out `SpeechManager_TriggerLineByEventCode`
     (different mechanism) and `AudioSystem_DispatchAnimationCueEvent` (no
     match), then a raw byte search for `this+0x7dd0` across 6 common
     register encodings found zero matches anywhere. Genuinely exhausted
     via multiple techniques, not simply unchecked -- a dynamic-analysis
     candidate if revisited. 0 renames. Still **1,033 total renames,
     1,305/5,385 (≈24.24%) live-counted.** Full trace:
     `RE_NOTES_audio_system.md`.
108. **Re-swept `InGameState_LoadLevel`'s own call list, found the `.ffn`
     font-loading cluster.** The original enumeration of this function's
     calls predated this session's `TrickDef_LoadFile`/
     `AudioSystem_LoadRaceConfigs` finds and was never actually
     exhaustive. Re-checking the raw call list found
     **`Font_LoadAndParse`** (was `FUN_000c36f0`, called with
     `"data/fonts/title.ffn"`/`"data/fonts/menu.ffn"`) ->
     **`Font_LoadFileData`** (was `FUN_0014bed0`) + **`Font_ParseGlyphTable`**
     (was `FUN_000c2da0`) -- confirmed via matching field offsets to build
     the exact `fontTable+8`, 12-byte-stride glyph array the already-named
     `Font_GetGlyphMetrics` binary-searches over. Closes a real gap
     between the already-decoded `.ffn` file format and its actual loader
     code. 3 renames. **1,036 total renames (1,028 functions + 8 data),
     1,308/5,385 (≈24.29%) live-counted.** Full trace:
     `RE_NOTES_application_boot.md`.
109. **Fixed a stale note: `InGameState_TickFrame`'s 5 sub-calls were
     already ruled out, not "remaining candidates."** An earlier session's
     `InGameState_ApplyHudElementVisibility` comment already confirmed all
     5 of `InGameState_TickFrame`'s immediate sub-calls are HUD/overlay-
     state management (a genuine negative result closing off this path
     for the rider-update-dispatch/score-writer question), but only 2 of
     the 5 had actually been renamed, leaving a separate note in
     `RE_NOTES_application_boot.md` looking open when it wasn't. Named
     the remaining 3: **`InGameState_IsHudOverlayActive`** (was
     `FUN_000b5d90`), **`InGameState_FindActiveOverlaySlotAlt`** (was
     `FUN_000a8fa0`), **`InGameState_TickActiveOverlay`** (was
     `FUN_000ca1b0`). Corrected the stale note explicitly rather than
     silently leaving it. 3 renames. **1,039 total renames (1,031
     functions + 8 data), 1,311/5,385 (≈24.35%) live-counted.** Full
     trace: `RE_NOTES_application_boot.md`.

110. **Closed out the `Commentary_QueueEvent` wrapper-caller sweep — 36
     renames.** The "Bonus find" section in `RE_NOTES_rider_update_chain.md`
     had flagged ~29 unread wrapper callers of the commentary/voice-line
     event queue manager as a future thread; a fresh `xrefs_to` on
     `Commentary_QueueEvent` (`0x00158630`) found 60+ total callers (many
     already named from unrelated between-session work). Named every
     remaining `FUN_`-prefixed caller across 4 batches — confirmed via a
     final `xrefs_to` sweep returning zero unnamed callers. Notable finds:
     `Commentary_QueueScriptedEventA/B` are called directly from
     `ScriptVM_DispatchOpcode` (level-script-triggered commentary, a new
     link); `Commentary_QueueRivalryShowcaseReaction` is called from
     `VenueStaging_TickCameraScriptCommand` and further confirms the
     commentary ↔ `AggressionManager` connection;
     `Commentary_QueueComponentCollisionReaction`/
     `Commentary_QueueRepeatedCollisionReaction` tie into
     `Rider_HandleComponentStateEvent`. 36 renames. **1,075 total renames
     (1,067 functions + 8 data), 1,347/5,385 (≈25.01%) live-counted.** Full
     trace: `RE_NOTES_rider_update_chain.md` ("Closing out the
     `Commentary_QueueEvent` wrapper-caller sweep" section).

111. **Closed the `Commentary_QueueEvent` inner sub-helper cluster — 11
     renames, finishing the subsystem to its natural depth limit.** Named
     the 7 sub-helpers flagged in the original "Bonus find" note plus 3
     more discovered via their own xrefs while tracing them (same tightly-
     coupled cluster). Key finds: `Commentary_LookupCategoryDescriptor`
     (was `FUN_001584a0`) is the shared resolver underneath both
     `Commentary_ResolveEventTableEntry` and `Commentary_ResolveEventCategory`;
     `Commentary_ReleasePlaybackSlot` (was `FUN_00158960`) is
     `Commentary_ClaimOrEvictPlaybackSlot`'s direct eviction counterpart;
     `RNG_NextCommentaryPickUInt32` (was `FUN_001590d0`) turned out to be a
     genuine **second, distinct RNG core** (address-separate from
     `RNG_NextGlobalUInt32`) used only by `RNG_ChooseWeightedCandidate`;
     `Commentary_FindRecentPickInHistory`/`Commentary_RecordRecentPick`
     (were `FUN_001592c0`/`FUN_00159330`) confirm the "avoid recent
     repeats" behavior that function's comment had only speculated about,
     via a real 32-entry circular history buffer. Also closed the one
     remaining leaf, `Commentary_FilterListContainsId` (was
     `FUN_00158d90`) — **zero unnamed functions remain anywhere in the
     `Commentary_QueueEvent` subsystem.** 12 renames. **1,087 total renames
     (1,079 functions + 8 data), 1,359/5,385 (≈25.24%) live-counted.** Full
     trace: `RE_NOTES_rider_update_chain.md` ("Closing the inner sub-helper
     cluster too" section).

112. **Fresh subsystem: the loading/splash screen system (`.xsh` picture
     screens) — 15 renames.** Opened by surveying extracted game assets and
     targeting `.xsh` (112 files, the most numerous asset type). Decoded the
     `.xsh` format = EA Gimex texture bank, magic `"SHPX"`. The filename
     strings led to a **family of ~9 full-screen picture-screen classes**
     (vtables 0x001a76d8..0x001a7a68) sharing a common base
     (`ScreenBase_LoadHudTexture`/`ScreenBase_TickAsyncAssetLoad`/
     `ScreenBase_DrawFrame`). Mapped the **race loading screen** (vtable
     0x001a7780: mode+track+rider pictures from `data/textures/xboxload.big`,
     with `LoadScreen_Format{Mode,Track,Rider}TextureName` builders) and the
     **dual-rider/versus loading screen** (vtable 0x001a7968). The rider
     formatter yields **the game's definitive 12-character roster in index
     order** (eddie/kaori/luther/mac/moby/zoe/jp/elise/psymon/seeiah/brodi/
     marisol) — **index 3 is 'mac', resolving the Mac/Marty roster gap.**
     Also cross-confirmed the GameMode enum (1=freeride, 2/4=race,
     3/5=showoff, 7=time challenge). Then named the 6 shared screen-family
     lifecycle methods (`ScreenBase_ConfigurePulseTimers`/`UpdateFade`/
     `ReleasePictureResources`/`TickFadeOutAndRelease` + 2 loading wrappers)
     and inventoried the full 9-screen vtable family in the notes. 21
     renames. **1,108 total renames (1,100 functions + 8 data), 1,380/5,395
     (≈25.58%) live-counted.** Full trace: `RE_NOTES_loading_screen.md`.

113. **Completed the loading-screen family — all 9 sibling screens now fully
     mapped, 30 more renames.** Continued the previous entry's thread to full
     closure. Named the 3 simple-screen lifecycle twins
     (`Screen_TickFadeOut`/`OnEnterConfigure`/`TickFadeUpdate`), then all 5
     remaining sibling screens: `InfoRowsScreen` (8-row generic content, no
     pictures), `RiderComparisonScreen` (a 2nd, independently-implemented
     2-rider screen), `LoadTipScreen` (single indexed "did you know" tip
     panel), `RulesScreen` (fixed rules/legend text), and `SoloLoadScreen`
     (single-rider variant of `LoadScreen`) — each with its own destructor
     where present (11-slot vtables). **Also resolved the earlier
     `Screen_DrawFullscreenTexture` hedge**: found a real code xref from the
     `data/textures/splash.xsh` string to a small loader function whose
     address sits at vtable `0x001a7730`'s own slot 8, confirming this is
     the game's **`SplashScreen`** — renamed the trio
     `SplashScreen_LoadTexture`/`CreateTextureFromAsset`/`DrawTexture`
     accordingly (deliberately *not* assumed from string proximity alone,
     per this project's standing caution about that trap). 30 renames (29
     new + 1 in-place upgrade). **1,137 total renames (1,129 functions + 8
     data), 1,409/5,406 (≈26.06%) live-counted.** Full trace:
     `RE_NOTES_loading_screen.md` (rewritten with the complete 9-screen
     table).

114. **Traced the loading screen's texture-decode path and found the
     engine's general-purpose Gimex bitmap codec — 9 renames, a bigger find
     than expected.** Followed `ScreenBase_TickAsyncAssetLoad` phase 3's
     decode call chain and discovered it isn't loading-screen-specific: the
     decoder cluster (`GimexBitmap_DecodeDispatch`/`GetDecodedSize` +
     4 per-format decoders `DecodeFormat10`/`18`/`30`/`46Lzw`) sits beneath
     **`FILE_LoadPackedGimexAsset`/`FILE_LoadPackedGimexAssetInto`**, which
     are called directly and solely from the already-named
     **`FILE_loadpack`/`FILE_loadpackat`** — confirming this is the engine's
     general Gimex-format asset-decompression codec, used for any
     compressed pack-file entry. `GimexBitmap_DecodeFormat46Lzw`'s 256-entry
     prefix/suffix tables identify it as a genuine **LZW decompressor**
     (GIF-style), fitting Gimex's known EA-image-library lineage. This
     closes the last open item from the loading-screen thread (the
     `.xsh`->D3D-texture decode path). 9 renames. **1,146 total renames
     (1,138 functions + 8 data), 1,420/5,406 (≈26.27%) live-counted.** Full
     trace: `RE_NOTES_loading_screen.md` ("The Gimex bitmap codec" section).

115. **Resolved a declared `.cml` dead end from an earlier session — the raw
     file-load path, 10 renames.** Initially picked `.cml` via the asset-
     survey technique (untouched-looking `data/camera/` files) and
     mischaracterized it as a fresh, never-traced direction — **wrong**:
     `RE_NOTES_camera_system.md` had already closed the `.cml` container
     format AND its runtime interpreter (`VenueStaging_TickCameraScriptCommand`)
     end-to-end in earlier sessions. Caught and corrected this mid-pass
     (before the writeup was finalized, no renames wasted). What actually
     turned out new: that file's own "the `%s.cml` filename builder has zero
     traceable callers" declared dead end — resolved this pass. Traced the
     loader chain (`CameraScript_LoadFile`/`LoadTrackAndCommonFiles`, exactly
     matching the 11-per-track + 2-shared file count) up to a full
     **NodeBase-derived `CameraScriptManager`** (vtable `0x00195f78`, same
     architecture as `PowerFXParticles`/`SnowFallMan`/`LessonMan`),
     constructed from `CameraController_Construct` (called directly from
     `InGameState_LoadLevel`). **`CameraScriptManager_Tick` calls the
     already-named `VenueStaging_Tick` directly** — the concrete missing
     link between "who loads the `.cml` bytes" and "who drives the runtime
     interpreter found earlier." **Also fixed a real naming mistake**: the
     loader's file-read call had been named `Font_LoadFileData` after its
     first-discovered caller, but it has 8 total callers spanning unrelated
     subsystems — it's a generic raw-file loader, renamed to
     **`FILE_LoadRawFileSync`**. 10 renames (9 new + 1 correction). **1,155
     total renames (1,147 functions + 8 data), 1,429/5,407 (≈26.43%)
     live-counted.** Full trace: `RE_NOTES_camera_system.md` ("resolves the
     declared... dead end" section) — kept in the existing file rather than
     a new one, since it's the same topic. **Immediate follow-up, same
     stretch**: closed 2 of 3 remaining open items —
     `InGameState_IsBlockingOverlayActive` (was `FUN_000ca120`, a broad
     blocking-overlay check shared by several core `InGameState`/HUD
     functions, not camera-specific) and
     `CameraScriptManager_RegisterActiveInstance`/
     `HandleDebugCameraCommand` (were `FUN_0007b5d0`/`FUN_0007baf0` — the
     latter a 10-opcode free-camera pan/zoom/cycle dispatcher, plausibly
     tied to the `"LarryCams"` named set from the `.cml` dumps). 3 more
     renames. **1,158 total renames (1,150 functions + 8 data), 1,432/5,407
     (≈26.48%) live-counted.**

116. **Swept the `RE_NOTES_DECOMP_PROGRESS.md` "what's missing" checklist for
     tractable static-analysis items — 6 renames, 2 stale notes corrected.**
     Checked the index/topic-file first per the new standing rule, confirmed
     `.big`/`.mpc`/`.xss`/etc. asset formats already covered, then picked
     concrete small items off the dashboard's own open-items list instead of
     another asset survey. Named all **5 `PixelBlit_ValidateAlignmentAndDispatch`
     format handlers** (`PixelBlit_ConvertRowsFormat0`/`Format1`/
     `Format2And3`/`Format4And5`/`Format6And7Interlaced` — a shared row-blit-
     loop + ordered-dithering shape, structural confidence; the dashboard had
     said these were "confirmed genuinely not worth naming," corrected in
     place) and found the previously-flagged boundary-overlap bug on
     `0x00149921` **no longer reproduces** (clean, contiguous bodies now).
     Also corrected a real mistake: `FUN_000fa6d0` had been noted as "a
     getter into a different, unidentified table" from
     `GfxContext_GetTextureHandleBySlot` — re-derived the offset arithmetic
     and found it's the **exact same 40-byte texture-slot record, field +4
     instead of +0** — renamed `GfxContext_GetTextureSlotField2`. 6 renames.
     **1,164 total renames (1,156 functions + 8 data), 1,438/5,407 (≈26.60%)
     live-counted.** Full trace: `RE_NOTES_rendering_system.md` (PixelBlit
     update) and `RE_NOTES_DECOMP_PROGRESS.md`'s own checklist (both items
     struck through with corrections in place).

117. **Deep-dive: `ReplayManager`, the instant-replay recording/playback
     system — 11 renames, a genuinely under-mapped subsystem.** Checked
     `RE_NOTES_INDEX.md` first (per entry 115's new discipline) and confirmed
     only 5 functions had ever been named for this system despite its size.
     Traced `ReplayManager_UpdateRecordingState`'s 14-state machine (driven
     directly from `InGameState_TickFrame`) into a genuine time-indexed
     snapshot allocator (`ReplayManager_AllocateAndInsertSnapshotNode`/
     `ReleaseExpiredSnapshotNodes`/`GrowSnapshotFreeList` — free-list-backed,
     time-ordered doubly-linked active list) and the race-end transition
     (state 9: gathers up to 10 racers, resets recording slots, hands off to
     the camera system). **Found the concrete missing link into
     `CameraScriptManager`**: `CameraScriptManager_EnterStagedCameraSequence`
     (was `FUN_0007bd60`) sets the exact flag `CameraScriptManager_Tick`
     checks to invoke `VenueStaging_Tick`, resolving a `.cml`
     `"GATE_CAM"`-tagged camera position — the full chain from "race ends" to
     "the `.cml` runtime interpreter plays the gate-camera sequence" is now
     traced end to end. **Also flagged a real, checkable lead**: `ReplayManager`'s
     4-slot, 3620-byte recording buffers are byte-for-byte identical in size
     to the still-open `SaveGame` "3620-byte record" mystery — not confirmed
     the same memory, but a strong candidate worth checking directly next.
     11 renames. **1,175 total renames (1,167 functions + 8 data), 1,449/5,407
     (≈26.80%) live-counted.** Full trace: `RE_NOTES_replay_system.md` (new
     file). **Immediate follow-up, same stretch**: pulling the thread on
     `ReplayManager_FinalizeRaceEndSnapshot`'s call to a then-unnamed helper
     turned up a whole **per-racer tracking-handle sub-system** — racers
     claim/release small tracking records as they enter/exit the replay
     camera's tracked set, sorted into up to 10 per-camera-slot lists keyed
     by a position/score value (`ReplayManager_ClaimRacerTrackingHandle`/
     `InsertRacerIntoSlotList`/`FindBestCameraSlotForRacer`/
     `ReassignRacerSlot`/`ReleaseRacerTrackingHandle` and 5 more). Reads as
     the mechanism behind instant-replay camera cuts automatically following
     whichever racer scores best for a given angle. 10 more renames.
     **1,186 total renames (1,178 functions + 8 data), 1,461/5,408 (≈27.02%)
     live-counted.** **Closed the loop, same stretch**: traced
     `ReplayManager_TryClaimRacerHandleIfValid`'s own caller into a fresh
     unanalyzed region that turned out to be **`Player`'s own destructor**
     — `Player_Destructor` (was `FUN_0005a890`, the real `~Player()` body)
     releases 2 tracked handles via functions confirmed to search
     `ReplayManager`'s exact `+0x390c`/`+0x391c` racer arrays, **direct
     proof `Player` registers with/unregisters from `ReplayManager`
     itself**. Found a small, clean vtable interface at
     `0x00189ef0`-`0x00189f2c` (adjacent to `Player_ScalarDeletingDestructor`'s
     slot) — 5 adjustor-thunk wrappers
     (`Player_ClaimReplayHandleThunk`/`SetReplayTrackingFieldA`/`FieldB`/
     `ComputeReplayElapsedTimeThunk`/`ReleaseReplayHandleThunk`) around the
     `ReplayManager_*` racer-tracking functions — closes the racer-tracking
     cluster end to end. 8 renames. **1,194 total renames (1,186 functions
     + 8 data), 1,469/5,413 (≈27.14%) live-counted.** Mapped the rest of
     that same 13-slot vtable, same stretch: 2 slots were already-named
     shared `Rider` methods, 2 are shared `Node` no-op stubs, and 3 genuine
     Player-specific overrides all share the exact same current-track
     racer-count gate as the racer-tracking cluster --
     `Player_HandleInputPollOverride`/`RemapInputButtonBit` (button
     aliasing), `Player_TickDirectionalAudioCueOverride`/`TickDirectionalAudioCue`/
     `TriggerDirectionalWindCue` (speed/direction-gated audio cue),
     `Player_TickVibrationGateOverride`/`TickVibrationGate`/
     `TriggerRumbleIfNotReplaying` (plausibly controller rumble, near a
     "Vibration"-tagged string). Confirms this whole vtable is one cohesive
     "how Player behaves differently during an instant-replay sequence"
     interface. 9 renames. **1,203 total renames (1,195 functions + 8
     data), 1,478/5,417 (≈27.29%) live-counted.**

118. **Fresh subsystem: the character animation-set loading system
     (`data/char/anm.big`, `.afl` files) — 6 renames, a third independent
     roster confirmation.** Same asset-survey technique: `anm.big` (one of
     the 4 per-character archives) had never been traced to a specific
     loader. Found the `.afl` animation-list format is loaded through **the
     same Gimex bitmap codec** already documented for `.xsh` textures —
     confirms the codec is a universal compressed-blob decoder, not
     texture-specific. Traced the full loader chain up to
     **`CharacterAnimSet_SelectAndPreload`**, called directly from
     `FEInit_Boot`/`InGameState_LoadLevel`/`Race_ResetPlayerRoster`. The
     150+-entry animation-set name table yields a clean character×venue
     grid with **12 three-letter character codes exactly matching the
     already-established roster** (Edd/Kao/Lut/**Mac**/Mob/Zoe/JP/Eli/Psy/
     See/Bro/Mar) — **the third independent confirmation this session that
     the roster's 4th slot is Mac, not Marty** — plus 10 venue suffixes
     cross-confirming the `TrackTable` track list. 6 renames. **1,209 total
     renames (1,201 functions + 8 data), 1,484/5,417 (≈27.40%) live-counted.**
     Full trace: `RE_NOTES_character_animation_system.md` (new file).
     **Immediate follow-up, same stretch**: resolved the loader's per-slot
     storage structure via raw disassembly (per the standing "verify, don't
     guess" rule) — a genuine per-slot array (24-byte stride) based at
     `+0x1e4` within a **single global singleton object**, confirmed via
     `MOV ECX,0x1cc520` (a fixed immediate) at the call site. Named it
     **`g_CharacterAnimBank`**, referenced from 20+ sites project-wide
     including the already-named `Application_Purge`. Tried to find
     `cMeshAnim`'s direct consumer of this data — checked
     `cMeshAnim_Construct` (no reference) and one candidate function found
     via the singleton's other xrefs, which turned out to be an unrelated
     random-rotation generator — genuinely not found this pass, documented
     honestly with the remaining unchecked reference regions listed for a
     future systematic sweep. 1 data rename. **1,210 total renames (1,201
     functions + 9 data), 1,484/5,417 (≈27.40%) live-counted** (function
     count unchanged; only a data label added). **Immediate follow-up,
     same stretch**: systematically checked the remaining reference sites
     (rather than sampling) and found the real chain —
     `CharacterAnimSet_FinalizeLoadedSlots` (polls all 446 pending loads,
     then calls `CharacterAnimSet_RegisterSlotAnimationIds` to build a
     1412-entry ID-to-slot lookup, `g_AnimationIdTable`/
     `g_AnimationIdToSlotMap`) feeding `AnimationLookup_GetSlotById`/
     `FindSlotIdFallback` and a real keyframe-curve sampler
     (`AnimCurve_SampleKeyframeValue`) used by
     **`Rider_SampleBoneRotationQuaternion`** — the actual per-bone
     rotation query, the real runtime consumer of the loaded animation
     data. **Corrected a mischaracterization along the way**: this last
     function had been sampled earlier and dismissed as "an unrelated
     random-rotation generator" — re-examined once its keyframe-sampler
     callee was properly understood, fixed immediately (a documentation
     fix, not a rename-then-fix, since the wrong name was never actually
     applied). 10 function renames + 2 data renames. **1,222 total renames
     (1,211 functions + 11 data), 1,494/5,417 (≈27.58%) live-counted.**
     Closed the last open item, same stretch: found
     `Rider_SampleBoneRotationQuaternion`'s sole caller --
     `Rider_CheckRailAttachmentAlignment` (already named). Used right after
     `RiderAnimation_TriggerByEventCode` fires a rail-grab animation:
     samples that animation's starting bone pose and uses it to smoothly
     correct the rider's world-space position when snapping onto a rail --
     a confirmed animation-driven pose-snap connecting `RiderAnimation`'s
     event system, this session's `CharacterAnimSet` curve-sampling system,
     and rail-attach physics in one call sequence. Not confirmed as also
     used by `cMeshAnim`'s rendering code specifically. Documentation-only
     update, no new renames.

119. **Fresh subsystem: `SaveContent`, the Xbox save-game content package
     system — 2 renames, plus a real boundary-finding war story.** Asset-
     survey pick: `data/icon/{Replay,Settings}/SaveImage.xbx` had never
     been traced. Decoded the real Microsoft XDK save-content package
     strings this game uses (`Data.ssx` = actual save payload,
     `SaveImage.xbx`/`SaveMeta.xbx` = per-slot icon/metadata,
     `TitleImage.xbx`/`TitleMeta.xbx` = the game's own dashboard icon, and
     the official reserved names `$$XSIMAGE`/`$$XTIMAGE`/`$$XTINFO`) and
     confirmed **2 distinct save-content types** (`Replay` and `Settings`
     saves) from the extracted asset paths. The loader code sits in an
     unusually hard-to-bound ~2KB region (zero `INT3` padding anywhere) —
     several boundary-finding techniques failed or hit a documented false
     positive (`search_address_refs` against a coincidental 16-bit value
     table) before manually walking forward from a confirmed anchor to find
     the real function starts. Found a genuine **~32-slot NodeBase-derived
     vtable** (`0x001a7560`-`0x001a75dc`) — another instance of the
     architecture already seen in `CameraScriptManager`/`ReplayManager`/
     `PowerFXParticles`. Named 2 of its methods:
     `SaveContent_EnumerateSaveSlots` (a save-slot browser scanning the
     Xbox content directory) and `SaveContent_InitializeDataFile` (prepares
     a slot's `Data.ssx` payload file). **Flagged a concrete, plausible
     lead for a future pass**: does this class's `Data.ssx` buffer connect
     to the long-standing `SaveOverlay`/`SaveGame` 3620-byte-record
     mystery? Not checked yet. 2 renames (5 more function boundaries
     created but not named — a well-scoped future thread, ~30 vtable slots
     still unmapped). **1,224 total renames (1,213 functions + 11 data),
     1,496/5,423 (≈27.59%) live-counted.** Full trace:
     `RE_NOTES_savecontent_system.md` (new file).

120. **Full-project audit, user-requested ("CHECK EVERYTHING... logically
     correct").** Mechanically diffed every one of the script's 1,224
     rename() calls against live Ghidra state, address by address: **zero
     missing, zero mismatches** -- the script and the live project were in
     perfect agreement for everything already tracked. Then checked the
     reverse direction: every live `USER_DEFINED` function with no
     corresponding script entry (283 candidates). Classified all of them by
     `SourceType`/body size: 261 were Ghidra's own Function-ID library
     matches (XAPI/D3D/DirectSound/CRT runtime -- never our work), 17 were
     auto-generated 4-byte thunk stubs (inherit their target's name
     automatically, don't need separate entries), 1 was the imported XBE
     entry point -- all expected. **Exactly 4 were genuine gaps**: real,
     correctly-applied `USER_DEFINED` renames from an earlier session that
     never made it into `ssx_auto_rename.py` -- `BdrSeqEvent_SelectDirectionalAnimClip`,
     `RingBuffer_Push`, `ScriptEvent_QueuePush`,
     `AudioVoice_IsSlotActiveAndMixing`. Added all 4 to the script (with
     comments based on direct re-decompile, honestly noted as not
     independently re-derived). **The script and live Ghidra project are
     now in full, verified agreement.** 4 renames (recovered, not new
     discoveries). **1,228 total renames (1,217 functions + 11 data),
     1,496/5,423 (≈27.59%) live-counted -- unchanged since these functions
     were already live, only the script's own bookkeeping was incomplete.**
     Audit continuing: metric consistency, cross-file contradictions, and
     targeted re-verification of hedged renames.

121. **Full-coverage batch audit, user-requested ("do this in multiple
     passes, divide them in multiple batches to check everything").**
     Entry 120's audit above used sampling (15+15 of 1,217 functions); the
     user explicitly asked for full coverage instead. Built a pipeline:
     address-sorted the full function list, split into 31 batches of ~40,
     fetched a condensed decompile snippet (first 14 lines + total line
     count) per function via `/decompile_function`, and read every batch
     checking each function's name against its actual body. **Result: all
     1,217 functions individually reviewed, zero naming or behavioral
     mismatches found.** Notable confirmations reached via full decompiles
     where a batch snippet alone wasn't conclusive: the 30+-function
     `Commentary_Queue*`/`Commentary_TryTrigger*` family (near-identical
     boilerplate) each dispatches a genuinely distinct event via a unique
     constant or `Probability_RollCategoryThreshold` category ID — not
     arbitrary suffixes. The `*Screen_*` family (DualLoadScreen,
     InfoRowsScreen, RiderComparisonScreen, etc.) shares boilerplate but each
     uses its own distinct field offset — genuinely separate classes.
     `New_camnode`/`New_camnode_2` follow an already-documented `New_<Tag>`
     convention, not an error. **Immediate follow-up**: used the audit's
     momentum to check a flagged-but-undone low-effort lead from
     `RE_NOTES_DECOMP_PROGRESS.md` — whether `rider+0x5720` (the unresolved
     medal-tier score field) equals `TrickCombo+0xf0`. Found the only write
     to that offset is a per-tier trick-landing-COUNT histogram
     (`TrickCombo_ScoreAirCompletion`, `this+0xe8+tier*4 += 1`), which is
     incompatible with `+0x5720` being read as a raw score value up to
     799999 — **rules out this hypothesis**, while identifying a genuine new
     function in the process (`Score_ResolveTierAndColor`, was
     `FUN_000c37c0`, a point-value-to-tier+RGBA-color classifier feeding the
     HUD score-popup's tint color, confirmed via its writes into
     `DAT_001baf70`-`7c`). 1 rename. **1,229 total renames (1,218 functions +
     11 data).**

122. **Found a load-side deserializer for the shared record format — and
     caught+fixed my own mis-naming of it via register-level disassembly
     (2026-07-22).** Searched the exported `default.xbe.c` for every literal
     use of `0xe24` (3620, the per-record byte stride) to chase two flagged
     leads: `RE_NOTES_player_snapshot_system.md`'s "no deserialize/load
     counterpart found" gap, and `RE_NOTES_savecontent_system.md`'s
     `Data.ssx`-connection question. Found the deserializer at `0x000bc910`
     (inside the `0x000ba0-0x000bd0` range an earlier session predicted),
     confirmed field-for-field against the write pipeline (same record-count
     chain, same `+0x48`/`0xe24` addressing, same object-list node shape).
     **I first named it `SaveGame_DeserializeFromBuffer` + its 3 callers
     `InGameState_*`, on an un-verified "fastcall this-passthrough"
     inference — then disassembled the call site and found `MOV
     ECX,0x1dbf50; CALL ...`, proving the receiver is a FIXED GLOBAL, not
     InGameState.** `Application_InitSubsystems` calls `AggressionManager_Init`
     on the same `0x1dbf50` (`MOV ECX,0x1dbf50`), so `0x1dbf50` is the
     project's existing "AggressionManager" host object. Corrected all 4
     names to `AggressionManager_DeserializeStateFromBuffer` /
     `LoadPendingStateBuffer` / `ExtractStateBufferHeader` /
     `DiscardPendingStateBuffer`. **Key structural finding**: the SaveGame
     *writer* (`SaveGame_TickAndFlush`, vtable-`0x19d530`) runs on
     **SaveOverlay** — a *different* object than this deserializer's
     `0x1dbf50` host — so writer and reader are two objects sharing one
     record format (working hypothesis: `0x1dbf50` = live career/rivalry
     state, SaveOverlay = write-staging snapshot). Where `0x1dbf50+0x3940`'s
     buffer gets populated wasn't found (no non-zero write to that offset in
     the static image). Does NOT resolve the `rider+0x5710`/`+0x5720`
     content-writer mystery. 4 renames (all corrected in place, net count
     unchanged). **1,233 total renames (1,222 functions + 11 data).**
     Full writeup: `RE_NOTES_player_snapshot_system.md`'s "found a
     deserializer" update (with its IMPORTANT CORRECTION);
     `RE_NOTES_savecontent_system.md` and `RE_NOTES_DECOMP_PROGRESS.md`
     updated too. **Lesson re-logged**: a this-passthrough inference is not
     confirmation — verify ECX at the call site with disassembly.

123. **Traced the pending-buffer source — it's a "Replay load" buffer
     (2026-07-22, immediate follow-up to 122).** The one loose end in 122 was
     "where does `0x1dbf50+0x3940` get populated?" — I'd claimed no write to
     it existed in the static image. That was an artifact of searching the C
     export by literal offset: the writer sits in an **unbounded** code
     region (~`0x93a00`–`0x94010`, a replay save/load handler cluster Ghidra
     never bounded). Querying **live Ghidra's xref engine for the absolute
     data address `0x1df890`** (= `0x1dbf50+0x3940`) found the write at
     `0x00069a91` immediately. Bounded that function via `/create_function`
     (and deleted a bogus zero-length boundary the tool also produced) and
     named it **`AggressionManager_AllocateReplayLoadBuffer`** (was
     `FUN_00069a70`): it allocates a **512KB buffer tagged literally
     "Replay load"** (`s_Replay_load_001919bc`) and stores the pointer into
     `host+0x3940` (size `0x80000` into `+0x3944`). Receiver confirmed
     `this=0x1dbf50` (`MOV ESI,ECX`/`MOV [ESI+0x3940],EAX`, Ghidra resolving
     the absolute target; sole writer of a field the load path reads/frees).
     **This definitively identifies the whole `+0x3940` chain as the
     REPLAY-load path** (not a general save load), tying directly to
     `SaveContent`'s confirmed `Replay` package type. Refined the open
     record-content-writer mystery: since the deserializer restores REPLAY
     data into `0x1dbf50+0x48`, the live-race code that fills `0x1dbf50+0x48`
     is the **replay recorder** — and the unbounded `~0x93a00`–`0x94010`
     handler cluster (calls `AllocateReplayLoadBuffer` at `0x93ffc`, a reader
     at `0x94010`, Extract/Discard at `0x941f3`/`0x9421c`) is the concrete
     next thread (needs boundary work). 1 rename (newly bounded function).
     **1,234 total renames (1,223 functions + 11 data).** Full writeup:
     `RE_NOTES_player_snapshot_system.md`'s "buffer SOURCE found" follow-up.

124. **Bounded the unbounded region around the replay-load buffer allocator
     — it's the Trick/Board-Select screen (2026-07-22, immediate follow-up
     to 123).** Walked backward from `AggressionManager_
     AllocateReplayLoadBuffer`'s call site (`0x93ffc`) using `/read_bytes`-
     verified RET+NOP-padding+prologue detection, one boundary at a time,
     decompile-checked before each next step. **Self-caught mistake**: a
     careless multi-address `create_function` probing loop fragmented an
     existing correctly-analyzed function into 4 bogus pieces — caught
     immediately via decompile, repaired with `/delete_function`. Lesson
     logged: never batch-probe `create_function`; verify one boundary at a
     time. Found the whole `0x93770`-`0x93fab` stretch is the **Trick/
     Board-Select screen**'s tick/input/event code, confirmed via shared
     field offsets (`+0x27`/`+0x4c`) with `UI_BuildBoardSelect`. Named 3:
     **`BoardSelectScreen_HandleTeamCommit`**/`HandleSelectionEvent`/
     `HandleTrickPreviewSelect` (were `FUN_00093770`/`930`/`ae0`) — team
     change + trick-preview-video path/select handling. Confirmed `0x93ffc`
     itself is a genuine auto-thunk (`SourceType DEFAULT`), no separate
     entry needed. Left 2 more bounded functions deliberately UNNAMED
     (`FUN_00093d80`, a button-check dispatcher with no confirmed class
     link; `FUN_00093d6e`, a trivial `return 0xd;` stub) — insufficient
     evidence to attribute a class, per this project's naming discipline.
     **Rich unresolved lead surfaced**: `FUN_00093c70`, a constructor
     installing a still-unidentified vtable (`0x00198c40`, distinct from
     `SaveReplayOverlay`'s family) that allocates either a 512KB or
     10664-byte buffer — sizes matching both the "Replay load" buffer and
     `AggressionManager_ConfigureDefaults`' sub-block — a concrete next
     thread (its own caller sits in yet another unbounded region). 3
     renames. **1,237 total renames (1,226 functions + 11 data).** Full
     writeup: `RE_NOTES_player_snapshot_system.md`'s "bounded part of that
     cluster" update.

125. **Resolved the `FUN_00093c70` lead — it's the Save/Load Memory-Card
     overlay panel (2026-07-22, immediate follow-up to 124).** Traced its
     sole caller (`0x9ea4a`) via the same careful `/read_bytes`-verified
     single-boundary methodology, skipping past a sibling function's own
     jump table along the way. Found the caller allocates a tagged
     **`"cFEStateMCOverlay"`** object (0x3888 bytes) and constructs it via
     `FUN_00093c70` — confirming the mystery constructor's class identity:
     the FrontEnd Save/Load Memory-Card-state overlay panel (matches this
     project's `cXxx`→`Xxx` naming convention). Named 3:
     **`FEStateMCOverlay_ConstructIfActive`** (was `FUN_0009e9d0`, gated on
     a UI state code, resolves a Save/Load/Delete-shaped mode, vtable-
     dispatched with no static caller), **`FEStateMCOverlay_Construct`**
     (was `FUN_00093c70`, installs a vtable distinct from the known
     `SaveReplayOverlay` family, branches on mode: load-mode allocates the
     same 512KB size as the already-found "Replay load" buffer; save-mode
     allocates the same 10664-byte size as `AggressionManager_
     ConfigureDefaults`' sub-block), and **`OptionsMenu_
     HandleDisplaySettingWidgetEvent`** (was `FUN_0009e930`, the setter
     counterpart to the already-documented `OptionsMenu_
     CacheDisplaySettingsFromWidgets` reader — confirmed via shared globals
     `DAT_001dd83c`/`860`/`878`/`874`, with `878` also tying into the
     AggressionManager replay-camera code as a display-quality toggle).
     **Remaining gap**: the matching buffer *sizes* (512KB) don't yet prove
     `FEStateMCOverlay`'s load buffer and `AggressionManager_
     AllocateReplayLoadBuffer`'s buffer are the *same* allocation — could be
     two separate buffers copied between. Not chased further. 3 renames.
     **1,240 total renames (1,229 functions + 11 data).** Full writeup:
     `RE_NOTES_player_snapshot_system.md`'s "resolved the FUN_00093c70 lead"
     update.

126. **Closed out the buffer-identity question and the replay-recorder
     search — one resolved negative, one exhausted via static analysis
     (2026-07-22, same session).** Decompiled `FUN_000e26a0` (the buffer
     call inside `FEStateMCOverlay_Construct`): it dispatches through a
     generic vtable slot (`this+0x650+0x3c`, the same convention already
     documented for the unrelated Challenge system) and never captures a
     buffer pointer — **confirming `FEStateMCOverlay`'s buffer request and
     `AggressionManager_AllocateReplayLoadBuffer`'s tagged pool allocation
     are two separate mechanisms**, not the same buffer (matching sizes
     were coincidental). Then searched for the "replay recorder" (what
     writes gameplay data into the AggressionManager host's `+0x48` record
     array during a live race): `xrefs_to` the record array's absolute
     start address (`0x1dbf98`) returned **zero hits** — consistent with a
     recorder needing variable per-slot index arithmetic, invisible to a
     literal-address search (the same limitation already known for the
     original `rider+0x5710`/`+0x5720` mystery). Enumerated all ~90 xrefs
     to the host global itself — almost all `DATA` reads from UI screens
     (rivalry/relationship display), plus exactly one genuine write, named
     **`AggressionManager_ResetOnRaceFinish`** (was `FUN_000cdf00`) — a
     race-finish/results-transition reset of the rivalry/commentary
     singleton, not a recorder. **Concluding this search avenue for real**:
     finding the actual recorder needs dynamic analysis, the same
     conclusion already reached for the original score-writer mystery. 1
     rename. **1,241 total renames (1,230 functions + 11 data).** Full
     writeup: `RE_NOTES_player_snapshot_system.md`'s "buffer-identity
     resolved" and "searched for the replay recorder" updates.

127. **Mapped `FEStateMCOverlay`'s own vtable and found how
     AggressionManager's persistent state gets RESTORED from a save file
     (2026-07-22, same session).** Read all 32 raw pointer slots of the
     vtable (`0x00198c40`) via `/read_bytes`. Found a mix of genuinely
     shared/linker-folded base-class methods (`Node_NoOpStub1`/`2`,
     `AudioSystem_NoOpStub`, `InputDevice_StubReturnFalse`, plus a run of
     slots identical to several `ChallengeMenu_Refresh*` addresses — a
     shared base-class text-refresh region both classes inherit unmodified,
     not evidence of a deeper relationship) and genuinely unique methods.
     Every address came directly from vtable data, so each was safely
     `/create_function`'d with no risky probing. **Slot 2 resolves the
     class identity of the button-check dispatcher deliberately left
     unnamed in entry 124** — it's `FEStateMCOverlay`'s own input handler,
     not the Board-Select screen's after all; now named
     `FEStateMCOverlay_HandleButtonInput`. **Major finding**: vtable slot
     10, **`FEStateMCOverlay_ValidateAndCommitBuffer`**, is how
     AggressionManager's own persistent state gets *restored* from a save
     file — its save-mode branch checksums a `0x29a8`-byte payload and, on
     match, `CRT_MemCopy`s it directly onto `DAT_001dbf54`
     (`0x1dbf50+4` = the AggressionManager host), then applies audio-volume
     settings and rebuilds the rivalry matrix via `FUN_00069bd0`. Its
     load-mode branch confirms the "Replay load" buffer's exact layout
     (header + payload + trailing checksum footer). This is the
     load/restore side of AggressionManager's persistence — real and
     disassembly-confirmed — but still distinct from the unresolved
     write-during-gameplay "recorder" (entry 126). Named 14: the
     destructor, `EnterActiveState`, `HandleButtonInput`,
     `InitScreenIfSaveMode`, `ApplySettingsIfSaveMode`,
     `BeginBufferAllocationIfLoadMode` (confirmed to tail-call-contain the
     already-named `AggressionManager_AllocateReplayLoadBuffer` thunk, no
     corruption), `BeginBufferStreamRead`, `TickBufferStreamRead`,
     `ValidateAndCommitBuffer`, `DiscardBufferIfLoadMode`,
     `SetOverlayTitleText`, `SetDescriptionVariantA`/`B`, plus
     `Widget_SetListRowsVisibility` (confirmed shared with `ChallengeMenu`,
     named without a class prefix). 14 renames. **1,255 total renames
     (1,244 functions + 11 data).** Full writeup:
     `RE_NOTES_player_snapshot_system.md`'s "mapped FEStateMCOverlay's own
     vtable" update.

128. **Fresh subsystem: the `.xsh` texture-sheet container format —
     DECODED (2026-07-22).** A fresh-direction pick after closing out the
     AggressionManager/replay-load thread — `Game Data\data\textures\*.xsh`
     files had only ever been enumerated by filename before this
     (`RE_NOTES_game_data_archives.md`), never format-decoded. Same
     asset-survey methodology used for `.ltg`/`.cml`: hexdumped real
     extracted files (1/3/16-entry examples), diffed them to isolate the
     fixed structure. Found magic **`"SHPX"`**, a clean header (file size +
     entry count + a 4-byte build tag + an N-entry `{name, offset}` table +
     an 8-byte EA tool-signature string — `"Buy ERTS"`/`"EASports"`, a nod
     to EA's old `ERTS` stock ticker), then a 16-byte per-texture
     sub-header (format code, width, height, mip/flags) before pixel data.
     **Cross-confirmed against live code**: `GfxContext_
     ParseAndQueueTexture`'s own field reads (format-byte switch,
     width/height offsets, mip/flags shift) match the file layout exactly
     — the same "format ↔ runtime struct" correlation done for
     `.ltg`↔`TerrainGrid`. Verified against 3 real files with very clean,
     consistent results (`crowd.xsh`'s 16 crowd-sprite frames match
     byte-for-byte; format-code-to-bits-per-pixel values come out
     suspiciously clean — 4.01/8.00/8.05). Deliverable:
     `scripts\classify_xsh.py`. Open: the exact `D3DFORMAT` mapping, 2
     unknown sub-header byte ranges, the build tag's purpose, and whether
     the magic gets validated at load (searched both byte orders in the
     binary, zero hits). Data-format finding, no function renames. New
     file: `RE_NOTES_xsh_format_decoded.md`.

     **Immediate follow-up, same entry**: traced `GfxContext_
     ParseAndQueueTexture`'s vtable call (`this+0xa8`) to its real target
     (`GfxContext_QueueTextureFromRawData`, already named) and found it
     re-switches on the same format index into a `local_1c` scale factor
     used directly in its own size formula
     (`(w>>mip)*(h>>mip)*local_1c>>1`). **This PROVES the bits-per-pixel
     table, not just measures it**: format `0x60`→exactly 4bpp, `0x61`/
     `0x62`→exactly 8bpp, `0x6d`/`0x78`/`0x7e`→16bpp, `0x7d`→32bpp — an
     exact match to every empirical measurement above. Also found the 3
     direct-copy (4/8bpp) formats get copied verbatim while the other 4
     get swizzled via the real Xbox SDK `XGRAPHC::XGSwizzleRect`. Updated
     `classify_xsh.py` with the confirmed `FORMAT_BPP` lookup table.

     **Second follow-up, same session — self-correction**: traced the
     internal format id further into `GfxContext_RegisterTextureTable`
     (`0x000f9f20`) and found it lands in bits[8:15] of a genuine Xbox
     native `D3DTexture` format DWORD (Format/Width-log2/Height-log2
     packed directly into one header word, the documented Xbox D3D8
     texture layout) — confirming it's a real `D3DFORMAT`-family value,
     not an engine-only invention. **Retracted my own earlier claim**
     that the 3 direct-copy formats were "consistent with paletted/
     indexed-color... than with block compression" — that reasoning is
     wrong: DXT1 (4bpp) and DXT3/DXT5 (8bpp) block compression produce
     numerically identical bits-per-pixel to 4/8-bit palette formats, so
     bpp cannot distinguish the two families. If anything, "no swizzle,
     direct copy" reads *more* consistent with DXT (blocks are already in
     a swizzle-compatible layout on Xbox) than with raw palette data
     (which typically does get swizzled, like the other 4 formats here).
     Documented as genuinely undetermined rather than asserted either
     way — a real self-caught overreach, not a guess dressed up as a
     finding.

     **Third follow-up, same session — settled it empirically**: decoded
     `hud.xsh`'s `"map1"` (format `0x60`) pixel bytes directly as DXT1
     blocks and `"map4"` (format `0x61`) as DXT3/DXT5 blocks. Both
     produce structurally coherent, not-noise results: DXT1's textbook
     "solid-fill" signature (near-identical near-black `color0`/`color1`
     RGB565 pair + all-1s indices) on 34% of blocks (1394/4096), with the
     rest decoding to plausible varied UI-icon colors (light highlights,
     dark shadow/outline tones); and DXT5's **unique** alpha-endpoint-
     plus-3-bit-interpolated-index block structure (e.g.
     `alpha0=0x11,alpha1=0xff` then packed indices) — a shape DXT3 does
     NOT have (DXT3 stores 16 explicit 4-bit alpha values with no
     endpoints at all), ruling DXT3 out for this specific entry. **Strong
     structural evidence, upgraded from "genuinely undetermined"**:
     format `0x60`=**DXT1**, `0x61`=**DXT5**, `0x62`=plausibly **DXT3**
     (untested, no sample file used it). Updated `classify_xsh.py`'s
     `FORMAT_BPP` table and per-entry report accordingly. Not a rendered-
     image proof, but about as close as static analysis gets without one.

     **Fourth follow-up, same session**: censused all 112 extracted
     `.xsh` files (515 texture entries) for format-byte usage. Format
     `0x7d` (32bpp uncompressed) dominates at 88.3%; `0x61` (DXT5) is
     second at 9.7%. **Format `0x60` (DXT1) is used in exactly 2 places in
     the whole game** — both now checked: `hud.xsh`'s original test, plus
     `particle.xsh`'s `"exlm"` flash-particle sprite, which independently
     reproduces the identical DXT1 solid-fill signature (74.3% of blocks)
     with a bright white core exactly where a flash effect's center should
     be — exhausts format `0x60`'s entire real-world usage, a second
     independent confirmation. **Format `0x62` (the DXT3 guess) is used
     zero times anywhere in the shipped game** — confirmed exhaustively,
     not an undersample; permanently untestable from real data. **Found a
     new format byte, `0x7b`** (2 uses, both `lightmap.xsh`/`spot1.xsh`'s
     `"spt1"` spotlight texture), which hits `GfxContext_
     ParseAndQueueTexture`'s pre-switch default (`uVar1=2`, same as
     `0x7e`, predicting 16bpp) — **but its actual measured data (~10bpp,
     5136 of an expected 8192 bytes) genuinely contradicts that
     prediction**, a real, unresolved discrepancy (not explained by
     padding/mips). Most likely a specialized lightmap-texture loader
     using a different code path than the one traced here — **left
     honestly unmapped in `classify_xsh.py`** rather than forced to fit an
     unverified value, a concrete open thread (find the real lightmap
     loader) if this area is revisited.

     **Fifth follow-up, same session — searched for the real lightmap
     loader, genuine dead end**: unlike `"hud.xsh"` (found hardcoded
     directly as an `ASYNCFILE_load_3` argument in
     `ScreenBase_LoadHudTexture`), none of `"lightmap"`, `"spot1"`,
     `"lightmap.xsh"`, `"spot1.xsh"` appear as literal strings anywhere in
     the binary (checked all 4, with and without extension). These files
     are evidently loaded via a dynamic/computed mechanism, not a
     hardcoded path — not chased further via dynamic analysis.

129. **Fresh subsystem: the `.ffn` bitmap font format — DECODED
     (2026-07-22, immediately after 126-128).** `Game Data\data\fonts\`
     (3 files: `menu.ffn`/`smlfont.ffn`/`title.ffn`) had been flagged as
     unexplored. Unlike `.xsh`, had a head start: this project already
     had `Font_LoadAndParse`/`Font_ParseGlyphTable` named from an earlier
     session (their bodies read, but the exact file layout never worked
     out byte-by-byte) — so this was a direct cross-reference against
     already-decompiled code, not a cold-start guess. Found magic
     **`"FNTF"`**: a header (glyph-record-format selector, glyph count, 2
     metric bytes, an offset to the glyph-record table, an offset to the
     bitmap-atlas section) + a 12-byte-per-glyph record table + a packed
     **4-bit-per-pixel alpha-mask glyph bitmap**, unpacked at runtime
     (traced into a newly-named function, see below) into `A4R4G4B4`
     textures — constant white RGB with the 4-bit value as alpha, the
     classic anti-aliased-text-as-mask technique — uploaded via the exact
     same `GfxContext` vtable slot (`+0xa8`) `.xsh` textures use, tying
     the two formats to one shared texture-creation pipeline. **Byte-
     perfect verified against all 3 real files**: every computed bitmap
     size (`width*height/2`) exactly matches the literal remaining file
     bytes with zero residual — `menu.ffn` 128×92→5888B exact,
     `smlfont.ffn` 128×52→3328B exact, `title.ffn` 256×125→16000B exact.
     Named **`Font_UnpackGlyphBitmapTexture`** (was `FUN_000c21a0`, the
     bitmap-section parser `Font_ParseGlyphTable`'s final statement hands
     off to). Deliverable: `scripts\classify_ffn.py`. Open: per-glyph
     sub-field geometry (X/Y/width/kerning not individually pinned down),
     several unread header bytes, and the untested `<200` "compact"
     glyph-record mode (no real sample uses it — all 3 files use the
     `>=200` extended mode). 1 rename. **1,256 total renames (1,245
     functions + 11 data).** New file:
     `RE_NOTES_ffn_font_format_decoded.md`.

130. **Fresh subsystem: the `.inp` tutorial-lesson playback file format —
     DECODED (2026-07-22).** `Game Data\data\tutorial\<char>.big` archives
     each contain 30 files (`psym01.inp`-`psym30.inp` etc.), every one
     exactly 316824 bytes uncompressed. Found the format string
     `"|data\tutorial\%s%02d.inp"` (`0x001aa678`) and traced its 3 xrefs
     — all in completely unbounded code — forward one `/create_function`
     boundary at a time (safe methodology, see 
     `feedback_safe_function_boundary_creation`). **Confirmed this whole
     cluster belongs to `LessonMan`** (the tutorial-mode singleton
     documented in `RE_NOTES_tutorial_system.md`) via an exact field-
     offset match: every new function reads `this+0x4fc` (dword `0x13f`)
     as a character-roster index into `PTR_DAT_001b53f8`, the identical
     field `LessonMan_Construct` itself uses for its own texture-path
     string. Also resolved `LessonMan_TickStepStateMachine`'s previously-
     undocumented `this+0x10` field: a queued "request step N" value
     applied by a generic per-tick helper that looks up each step's
     enter/exit/tick triple from a fixed global table
     (`0x00189d8c + 0xc*(step-1)`) — read directly via `/read_bytes` to
     map all 11 steps before bounding only steps 2-5 (the direct path to
     the loader); steps 6-11 left as known-address, unbounded follow-up.
     Named 9 functions across the step-2→3→4→5 chain, ending in
     **`LessonMan_Step5Enter_LoadLessonInpFile`** (was `FUN_000580f0`) —
     the actual loader: `FILE_loadpackat(path, this+0x458, 0x4d598)`,
     where `0x4d598`=316824 decimal is byte-exact both the real file size
     AND the `"LessonBuffer"` scratch allocation size from
     `LessonMan_Construct`. Then **decoded the file's own internal
     structure** by brute-force header-size search against the real
     extracted `psym01.inp` bytes in Python, verified structurally (not
     just arithmetically): 52-byte header (live-position/total-frame-count
     dwords, `1042` for `psym01.inp`) + 1042×44-byte records (4 constant
     "template" floats + 7 sparsely-varying words reading as a scripted-
     event/cue timeline) + zero padding to the fixed 316824-byte buffer
     capacity — exact byte accounting (`52+1042*44=45900` used,
     remainder verified all-zero). 9 renames. **Immediate follow-up, same
     session**: went back and bounded/named steps 6-10 too (step 11
     still open — its tick address sits far outside this cluster in
     unrelated unbounded code). Found step 5/7 are near-twin `.inp`-load
     paths (one per difficulty choice), step 8 is a distinct "random
     instructor commentary" path with no file load, and steps 6/9/10 are
     "return to menu" variants. **Caught and corrected a mis-attribution
     from the update above**: `LessonMan_InitLessonTypeSubmenu`'s caller
     was guessed as "step 7's enter" from an xref address range before
     step 7 was bounded — now bounded and confirmed the real caller is
     step 9's enter instead; fixed the rename comment immediately per
     this project's "fix old mistakes on discovery" practice. 7 more
     renames (16 total for the `.inp`/`LessonMan`-step thread). Still
     open: step 11, the actual per-frame record consumer (not located),
     a large unnamed reset function (`FUN_000bc170`) called from 2 of the
     steps, and precise per-word/per-field event semantics. **Second
     immediate follow-up, same session**: closed step 11 (a tricky
     boundary — its vtable-referenced address pointed 2 bytes into what
     looked like NOP padding from a linear read; trusted the vtable
     reference over the boundary heuristic and it was correct) —
     **all 11 of LessonMan's steps now bounded and named.** Chased
     `FUN_000bc170` via `xrefs_to` and found it's also called from the
     already-named `ReplayManager_ResetState` — renamed it
     `ReplayManager_ClearRecordedFrames`. Found 2 more callers
     (`FUN_000ac550`/`FUN_000ac610`, generic venue-exit handlers, not
     renamed) sharing the exact same `+0x40` "replay active" flag
     LessonMan's steps check — a genuine cross-system connection.
     Important negative result: `ReplayManager`'s own already-documented
     playback-tick functions do NOT consume LessonMan's `.inp` buffer —
     the systems share a reset utility, not a playback pipeline, so the
     `.inp` per-frame consumer remains genuinely unlocated. 2 more
     renames (18 total for the whole `.inp`/`LessonMan`-step thread).
     **Third immediate follow-up, same session: FOUND the `.inp` per-frame
     consumer.** Went back to `LessonMan_RenderOverlay` (already named)
     and noticed its one special-case extra draw call fires exactly when
     the current step is 5 or 7 — the same 2 steps that load/play a
     `.inp` file. That call, renamed **`LessonMan_DrawRecordedButtonPrompt`**
     (was `FUN_00056760`), reads an 11-dword window from the loaded `.inp`
     buffer keyed by its live position counter and matches individual bits
     against the same button-icon lookup table `LessonMan_Construct`
     populates from the real controller layout, drawing a "press this
     button" icon prompt. **Confirms the `.inp` file's varying per-record
     fields are literally recorded controller button-bitmasks** — matching
     the "`.inp`"="input" filename convention exactly — the mechanism
     behind the game's "watch and repeat" tutorial lessons. 1 more rename
     (19 total for the whole thread). Still open: what actually increments
     the position counter each frame (narrower now, not located). Update
     folded into `RE_NOTES_tutorial_system.md` (no new file).

131. **Fresh subsystem: `BoardSelectScreen` — the character/board/team
     select screen (2026-07-22).** Picked per explicit user request
     after the `.inp`/`LessonMan` thread wrapped up. Only 3 functions had
     ever been named (a byproduct of an unrelated investigation), no
     dedicated file. Found the class's own 14-slot vtable
     (`0x001982b0`-`0x1982e4`) via the 3 known methods' `xrefs_to` data
     references, and — importantly — confirmed 2 near-identical sibling
     vtables immediately adjacent are a **completely different, unrelated
     widget class**, not more `BoardSelectScreen` instances (checked every
     slot's `xrefs_to` individually before naming anything, rather than
     assuming). Named 5 more methods: `BoardSelectScreen_
     CommitTeamSelectionAndStream` (team-switch handler, maps the
     selected character to a per-team asset-load offset), `ExitAndStop
     Preview`, `TickPositionAnimation` (vtable-dispatched only, no static
     caller — needs dynamic analysis), `HandleSecondaryEvent`, and the
     standout: **`BoardSelectScreen_ConfirmSelectionAndEnterLesson`**
     (was `FUN_000887c0`, needed `/create_function`) — sets
     `GameMode_Current=6` unconditionally, **the exact value `LessonMan`
     gates on**, directly tying this screen to the tutorial system:
     the character picked here determines which character's `.inp`
     tutorial files get loaded later, closing the loop between this
     session's two separate investigative threads. Correctly left 2
     vtable slots unnamed after confirming via `xrefs_to` they're shared
     generic `Widget` infrastructure (15+ unrelated callers), not
     board-select-specific. 5 renames (8 total with the pre-existing 3).
     New file: `RE_NOTES_boardselect_screen.md`. **Immediate follow-up,
     same session**: chased the sibling-vtable identity question by
     tracing both classes' constructors via `xrefs_to` on their vtable
     base addresses. Confirmed they're generic `"f3strtgrp"`-tagged
     frontend container widgets, genuinely unrelated to
     `BoardSelectScreen`. Bounding one constructor's caller landed inside
     a switch-case body (`switchD_000895dd::caseD_4`, Ghidra's own
     auto-label) belonging to a different, larger, still-unbounded
     screen-builder switch (5-case jump table at `0x000897a8`) — closes
     the original question, surfaces a genuinely fresh well-anchored
     lead for later. No new renames (bounded function left with Ghidra's
     default switch-case label, owning screen not yet identified).

132. **Fresh subsystem, found as a byproduct: `ProfileEditor` — SSX
     Tricky's Create/Edit Rider Profile screen's per-tab widget builder
     (2026-07-22).** Picked straight back up from entry 131's open
     lead (`switchD_000895dd`). Bounded its remaining 3 cases plus the
     switch's own entry point (`/create_function`, which correctly
     re-merged Ghidra's 4 auto-split case functions into one once the
     entry point existed) — identified unambiguously via allocation tags
     as a 5-tab widget factory: `"f3stoutfit"`/`"f3stboard"`/
     `"f3stprofile"`/`"f3sttkbk"`/`"f3stusrname"` (Outfit/Board/Profile/
     TrickBook/Username). Renamed the dispatcher
     **`ProfileEditor_BuildTabWidget`**, the shared base-widget
     constructor, all 5 tab constructors, and 2 tab destructors (9
     renames) — this **fully closes** the "sibling vtable" thread from
     entry 131: not just "confirmed unrelated to `BoardSelectScreen`" but
     now a properly identified, named subsystem in its own right, with 2
     of its 5 tabs (Profile, TrickBook) directly tracing back to the
     originally-mysterious vtables. Still open: the dispatcher's own
     caller (no static xref found), the other 3 tabs' destructors, each
     tab's remaining vtable slots. New file:
     `RE_NOTES_profile_editor_screen.md`.

133. **Fresh subsystem: `OptionsMenu` — the multi-tab settings screen
     (2026-07-22).** Only 2 functions had ever been named (byproducts of
     the `FEStateMCOverlay` investigation), no dedicated file. Traced the
     already-named `OptionsMenu_CacheDisplaySettingsFromWidgets`'s caller
     to find **`OptionsMenu_HandleTabSwitchEvent`** — a master handler
     managing up to 5 settings tabs, calling each tab's own "cache
     settings from widgets" function on tab-switch. Found and named the
     2 sibling tab-cache functions sitting alongside it:
     `OptionsMenu_CacheAudioSettingsFromWidgets` (7 audio sliders/
     toggles) and `OptionsMenu_CacheControlSettingsFromWidgets` (2
     paired per-player toggles). Also found and named
     **`OptionsMenu_BuildDisplaySettingsWidgets`**, whose sole caller
     (via `xrefs_to`) is the already-named `UI_BuildPauseMenu` —
     confirming the Pause Menu directly embeds this settings screen's
     Display tab, explaining an odd loose end from entry 131
     (`BoardSelectScreen_TickPositionAnimation`'s unexplained tail-call
     into `UI_BuildPauseMenuText`). Also reconfirmed the existing
     `DAT_001dd878` Display-setting ↔ `AggressionManager_
     LoadPendingStateBuffer` connection already documented in
     `RE_NOTES_player_snapshot_system.md`. 4 renames (6 total with the 2
     pre-existing). New file: `RE_NOTES_options_menu.md`.

134. **Fresh direction, per explicit user request to prioritize core
     gameplay/physics over frontend UI for port viability (2026-07-22).**
     Picked up `RE_NOTES_terrain_collision.md`'s longest-standing open
     item: what the rider's cached "surface" value
     (`rider_component+0x2cc`, written by `Terrain_QuerySurfaceContact`/
     `Rider_ResolveTerrainContactPhysics`) actually does. Read both
     functions in full — dense SSE collision math modeling terrain edges
     as **cubic Bezier curves** with iterative closest-point refinement,
     more sophisticated than assumed — and found the cached value traces
     to a **larger enclosing collision-patch object**, not the 76-byte
     triangle record's own leading id (a correction to the earlier `.ltg`
     format notes: this project's own `feedback_fix_old_mistakes_on_
     discovery` in action). Then found the consumer via a *targeted*
     `/search_bytes` for the raw `0x2cc` displacement, specifically
     looking for **clustering** (4 hits in one small function) rather
     than trusting isolated hits — landed on
     **`Rider_ComputeSurfaceCompressionResponse`** (was `FUN_00026da0`),
     which **revises the hypothesis again**: `+0x2cc`/`+0x2d0` are a pair
     of continuous float compression thresholds driving a suspension-
     curve response, not a discrete material id/enum. Found 2 more
     genuine readers via the same search (`Rider_PhysicsMode6_NoTerrain`,
     `RiderEvent_ProcessInputWithComponentDecay`). Also fixed a stale
     "not yet read" note for `FUN_000287a0`
     (`Rider_ResolveTerrainContactPhysics`, already named in an earlier
     session). 1 rename this pass. Substantially closes the file's
     original "surface material semantics" question — the real answer is
     "continuous compression/give," not "categorical snow/ice/rail id."

135. **Stale-note correction: `RE_NOTES_rider_event_system.md`'s "still
     open" section was already fully closed (2026-07-22).** Continuing
     the core-gameplay priority, picked this file back up expecting ~30
     unread `RiderEvent` case handlers — but per this project's "check
     before declaring fresh" discipline, checked each address live
     before touching anything and found **every single one already
     named** (closed in an earlier 2026-07-20 session; this file's own
     "still open" list just never got updated to match). Corrected the
     file rather than re-doing or mis-crediting already-finished work.
     The one genuinely leftover sub-item ("`RiderEvent_SetState`'s other
     8 callers not traced") was also mostly stale, but a fresh
     `xrefs_to` check found 6 genuinely still-unnamed callers — named 5:
     `Rider_FinalizeGroundLandingState`, `Rider_HandleGroundModeEntry`,
     `Rider_CheckLandingRecoveryState`, `RiderEvent_SetStateThunk`,
     `Rider_ResolveLandingOutcome` (the 6th, `FUN_0003c360`, read but
     left unnamed — genuinely complex, not confirmed related to the
     terrain-compression fields from entry 134 despite a nearby offset).
     5 renames.

136. **MAJOR FIND, closing a Tier-1 port-viability gap: the general-racing
     AI steering mechanism (2026-07-22).** Per an explicit "focus on
     Tier 1 until complete" directive, picked up
     `RE_NOTES_ai_path_system.md`'s last open question: whether AI
     steering during normal racing goes through the confirmed
     `Rider_ComputeAISteering`/`Rider_FollowAIPath` pair (proven only for
     the end-race-fade state) via a call site not yet found, or a
     separate mechanism. Rather than searching harder for a call site to
     that pair, read `Rider_UpdateTrackPathPosition` **in full** for the
     first time (previously only its `this+0x3a0` write was known) —
     found its never-read second half conditionally calls the actual
     missing piece: **`Rider_SelectBestAIPathZone`** (was `FUN_00032420`)
     — a probability-gated dynamic lane/path-choice decision that queries
     nearby alternate paths (new: `AIPathSet_QueryNearbyAlternatePaths`,
     `AIPath_ComputeDistanceToBoundingBox`), scores them, and reassigns
     the rider's tracked path when a better one is found. **This runs
     every frame for every rider, unconditionally** — the two mechanisms
     are NOT the same triad as originally assumed; they're two separate,
     both-real AI behaviors (always-on path selection vs. end-race-only
     collision avoidance). Between them, general-racing AI navigation is
     now fully accounted for. 5 renames (28 total for the whole AI-path
     thread, now substantially complete). Also closed
     `RE_NOTES_rider_event_system.md`'s last remaining unread function
     (`Rider_CheckAttachedComponentOrientationLimit`, was `FUN_0003c360`)
     — fully closes every `RiderEvent_SetState` caller.

137. **TIER-1 RESOLUTION: the generic per-frame Update dispatcher, found
     statically (2026-07-22).** The project's oldest systemic mystery —
     what generically ticks registered `NodeBase` subsystems each frame —
     had been declared "needs dynamic analysis" after exhaustive searches.
     Root cause of every failure: the searches hunted callers of vtable
     slot 7 (`+0x1c`, the proven-lifecycle-only `NodeRegistry_
     UpdateAllOfType` path); the real per-frame tick uses **slot 1
     (`+0x4`)** via a different, unnamed dispatcher. Found by reading
     `InGameState_TickFrame`'s unexplored tail call `FUN_000aa830(9)`.
     Named the full machinery: **`NodeRegistry_TickAllOfType`** (slot-1
     walker, self-unregister-safe), **`NodeRegistry_RenderAllOfType`**
     (slot-2 sibling), **`NodeRegistry_IntegratePendingNodesOfType`**
     (pending-list sorted-insert via comparator slots 3/4, with
     replacement), **`InGameState_TickSubsystemsByTypeOrder`** (the master
     11-entry type-ordered loop — **correcting the old
     `InGameState_ApplyHudElementVisibility` misname**, which had misread
     the dispatcher as a HUD-element toggle), and
     **`InGameState_RenderSubsystemsPerViewport`** (split-screen-aware
     render loop, 13-entry order table). +3 data renames for the
     type-order tables. Resolves in one stroke who ticks `LessonMan`
     (type 6), `SnowFallMan`, `PREAI`/`PostAI` (types 5/0xb), and every
     "orphaned Update method" flagged project-wide. Full writeup:
     `RE_NOTES_node_base_class.md`.

138. **TIER-1 RESOLUTION: the `rider+0x5720` score-writer mystery —
     the field was a PHANTOM (2026-07-22).** The longest-running open
     question in the project (the `+0x5710` half was solved earlier;
     `+0x5720`'s writer was never found by any static technique). A fresh
     `/search_bytes` for the *solved* field's displacement (`0x5710`)
     found 2 references in a previously-unbounded function sitting
     between `FUN_0002d800` and `Race_ComputeRankings` — bounding it
     (**`RaceOutcome_EvaluateAndBeginPostRace`**) produced a Rosetta
     stone: identical GameMode-branched medal logic on a **raw rider
     pointer**, with all three field pairs aligned at a uniform `+0x10`
     shift (`0x5720/0x150/0x458` component-frame ≡ `0x5710/0x140/0x448`
     rider-frame; same `MedalTier_ResolveFromValue`+`DAT_001dec90` call,
     same `DAT_001deca0` threshold, same top-half-finish test).
     Conclusive: the lone "`rider+0x5720`" read goes through the
     RiderEvent component's shifted rebase frame and **is
     `rider+0x5710`** — the live trick score whose writer was already
     solved. No second score field exists; no dynamic analysis needed.
     Side finds all named: the per-track career personal-best recorders
     (**`Team_RecordBestRaceTimeIfBetter`**/**`BestShowoffScoreIfBetter`**),
     **`InGameState_FindLeadingViewportPlayer`**,
     **`Race_ComputeRiderStandingsMetric`**, plus `rider+0x140`=placement
     and `rider+0x448`=race-time frame confirmations. 6 renames (1 needed
     `/create_function`). Full writeup:
     `RE_NOTES_trickcombo_scoring_resolved.md`. **With entry 136's AI
     resolution, every Tier-1 port-viability blocker is now closed.**

139. **TIER-2 RESOLUTION: the `.afl` character-animation format — DECODED
     AND FULLY VERIFIED (2026-07-22).** Extracted all 446 `.afl` files
     from `data\char\anm.big` (count exactly matches the 446 pending-load
     flags `CharacterAnimSet_FinalizeLoadedSlots` polls), cross-referenced
     the runtime readers (incl. raw disassembly of the curve-sample path
     to recover register context the decompiler hides), and verified
     **119,498 of 119,498 curve streams parse cleanly** — total, not
     sampled, verification. Layout: 12-byte header (magic `0x114C`,
     entry count, 2 section offsets) + 36-byte entries (animation ID +
     first-curve index + companion count + frame duration; each ID'd
     entry followed by N id=0 companion channel-group entries) + u32
     curve-offset table + compressed piecewise curve streams: modes 0-3 =
     constant/linear/quadratic/cubic polynomials in **3-byte floats**
     (low mantissa byte dropped, reconstructed as 0x80), modes 6/7 =
     u8/u16-quantized keyframes, mode 4 = raw keyframes (defined in code,
     zero uses in shipped data — same pattern as DXT3 in `.xsh`).
     Renamed `AnimCurve_EvaluateSegment` (was `FUN_0005f980`, closing
     that file's open item) and `AnimEntry_SampleRotationCurves` (was
     `FUN_00060c80`). Deliverable: `scripts\classify_afl.py`. 2 renames.

140. **TIER-2 RESOLUTION: the remaining `.cml` record types, decoded
     statistically (2026-07-22).** Extended the cross-record diffing
     technique that solved `Location` to every remaining family, across
     all 12 track files at once. `Moment` (808B) = **two back-to-back
     Location-shaped 404-byte keyframes** (identical field offsets
     repeated at +404); `Transition` (808B) = the same pair + a halving
     blend-duration float at `+0x180` and relative-looking positions;
     staging records (112B) = directory/linkage nodes (ordinal +
     runtime-pointer placeholders + name hash), matching their known
     `VenueStaging_EnterNamedState` role; gate/demo records (916B, the
     majority) = composites of 44-byte named cells — the same cell unit
     underlying the whole format. Also refined `Location`'s rotation
     block to **4 floats** (the 4th plausibly FOV/zoom) — an under-count
     in the earlier 6-record sample, not a wrong offset. Camera keyframe
     data is now decoded for all record types; the in-binary `.cml`
     loader remains unfound (unchanged dead end, accepted). No renames
     (data-format work). **TIER 2 IS NOW COMPLETE** — with entries
     136-138, everything identified as port-gating in the gap analysis
     is resolved.

141. **Tier-3 sweep + full port-viability audit + PC-port plan
     (2026-07-22).** Closed the `.inp` playback loop end to end:
     **`LessonMan_AdvanceInpPlaybackFrame`** (the long-open position-
     counter incrementer) and **`LessonMan_InjectRecordedInputFrame`**
     — proving the tutorial demo drives the rider by replaying recorded
     controller input through the real input pipeline; plus
     `LessonMan_RestartLessonPlayback`, `LessonMan_DestructorFreeBuffers`,
     and 2 race-phase names (`RacePhase_TickStartCountdown`/
     `InitRaceClock`) from the `0x19a490` vtable; bounded one more HUD
     overlay-stack function. 6 renames. Then ran the **full deliverables
     check** (every classify/parse tool re-run against real game data —
     all clean, `.afl` at 119,498/119,498) and wrote
     **`RE_NOTES_PC_PORT_PLAN.md`** (new file): complete per-domain
     audit, the honest gap list (headline: **`.xbd` mesh format is the
     one real remaining decode**, needed by milestone M4 but not by the
     gameplay milestones), platform-replacement map, 7 milestones
     (M0-M6), immediate preparation actions, and risks — including the
     realization that the 360 shipped `.inp` files double as
     deterministic input-replay regression vectors for the port.

142. **Gap-list closure pass: model formats + all residue items
     (2026-07-22, per "handle these two completely").** (1) `.mxf`
     rider/board models: directory level DECODED — header (lodLevelCount/
     version/tableEnd) + 0x18C-byte named entries with contiguous
     data extents, verified across body/head/board files (heads split
     Eyes/Face/Hair per LOD; the board file holds 3 boards x
     regular/goofy stance). Deliverable `scripts\classify_mxf.py`.
     (2) `.xbd` track models: header counts + 4 ascending section
     offsets identified; loader chain traced (`FUN_000b0660` 10-slot
     model bank -> vtable parse -> `FUN_000b03c0` texture-index remap
     fixup, confirming .xbd references the track's .xsh sheets by
     index). Geometry interiors = the port plan's M4 task; directory
     scaffolding done. (3) `.afl` bone mapping resolved structurally:
     no bone names exist — index-based, mapped by skeleton order.
     (4) Audio CLOSED: standard EA `BNKl` v5 banks (PT/EACS headers,
     vgmstream-supported) — confirmed by inspection. (5) `.xsh` 0x7b
     CLOSED: 32bpp + 1 mip + 8-byte terminator, arithmetically exact.
     (6) `.cml` composite cells CLOSED: serialized authoring-tool
     object graph (inline names, stale 0x7E74xxxx tool heap pointers,
     0xDEADC0ED fill). New file: `RE_NOTES_xbd_model_format.md`;
     PORT_PLAN gap list updated — one well-scoped M4 task remains.

143. **PC PORT M0/M1 — builds an .exe that boots into a window
     (2026-07-22).** Started the actual port under `port/`: a C++17 +
     CMake project (MinGW/UCRT g++ 13 verified) producing `ssxtricky.exe`,
     a 640x480 Win32 windowed app presenting a software framebuffer via
     GDI. Ported the verified Python decoders to C++ (RefPack, c0fb +
     BIGF `.big`, `.loc`, `.xsh` incl. DXT1/3/5, `.ffn`) and proved them
     byte-identical to the reference tools via a console self-test
     (`asset_test.exe`): `gari.ltg` extracts to the exact 721068 bytes,
     `american.loc` id 0x66 = "Press START button", etc. Built an
     `Application` main loop + `NodeRegistry` mirroring the original's
     per-frame tick/render subsystem-type-order dispatch. The boot screen
     renders the real `splash.xsh` "Basic Controls" image + localized
     prompt (screenshot-verified). **Correction found during M1**: `.xsh`
     data is stored LINEAR — the RE table's "swizzled" flag is the Xbox
     engine's upload-time `XGSwizzleRect`, not the file layout; the port
     omits swizzling (folded into `RE_NOTES_xsh_format_decoded.md` and
     PORT_PLAN M1 notes). New: `port/` tree + `port/README.md`. No
     Ghidra renames (this is port code, not RE).

144. **BOOT SEQUENCE traced for the faithful port (2026-07-22).** Per the
     user's push to reproduce the real boot flow from the code (not
     guessed), traced the power-on sequence end to end. The boot state is
     `cStartScreenSingle` (0x3e98 bytes, Application state 0, created by
     `StartScreen_Create`); it runs a save-autoload state machine (state
     field +0x3e84, set by `StartScreen_SetState`, rendered by
     `StartScreen_RenderStatusText`) showing the real localized strings
     **0xba7 "Checking hard disk"** (states 0/3/4/5/9-12) and **0xba8
     "Autoloading from hard disk"** (states 6/7/8), white on black in
     menu.ffn (loaded by `StartScreen_Enter`), with code-traced minimum
     frame timings (state 0 = 15 frames, state 8 = 120 frames = 2s);
     terminal state 0x1e sets done-flag +0x48. Fades between phases via
     `TransitionEffect_Update`. Then Basic Controls splash (splash.xsh
     "cont") -> EA logo (eabig.mpc) -> intro (ssxintro.mpc, both queued by
     `TitleIntroSequence_QueueBootVideos`) -> FrontEnd title. 7 renames
     (`StartScreen_Create/Enter/TickExitWhenDone/ResetState/Render/
     RenderStatusText/SetState`). New file
     `RE_NOTES_boot_sequence_and_startscreen.md`. Port
     (`boot_flow.cpp`) now reproduces this faithfully (real strings, real
     font, traced timings, fades) -- only the 2 videos remain placeholder
     pending an MPEG-1 decoder.

145. **`.mpc` AUDIO decoded + port audio subsystem (2026-07-22).** The boot
     videos had no sound because the port had no audio path at all and the
     `.mpc` `SC*l` audio chunks were being discarded. Both fixed. **Codec:
     EA-XA ADPCM, 48 kHz stereo.** Recovered the `SCDl` block framing from
     the game's own chunk dispatcher (`FUN_00013030` /`FUN_00012f60`): a
     per-channel offset table (`chOffset[i]`), channel data at
     `block + chOffset[i] + nch*4 + 0xC` after a 4-byte predictor-history
     header, then EA-XA frames (1 header byte: hi nibble = coefficient
     index, lo nibble = shift; + 14 data bytes = 28 samples, low nibble
     first), with predictor history running **across** blocks. An earlier
     "split the block in half per channel" guess was wrong (negative shift)
     -- corrected via the dispatcher. **Verified**: eabig.mpc decodes to
     168,000 frames / 3.50 s with a real dynamic envelope and 0.090
     zero-crossing rate (tonal, not noise); the Python reference and the
     C++ port decoder are **bit-identical** (0/336,000 samples differ).
     Port additions: `src/assets/ea_adpcm.cpp` (decoder),
     `src/platform/win_audio.cpp` (waveOut streaming output, 8-buffer ring
     + feed thread), wired into `MpcVideo` with ~0.25 s lookahead. Runtime-
     verified: audio device opens 48 kHz stereo and both videos report
     decoded video **and** audio. Full spec appended to
     `RE_NOTES_title_intro_sequence.md`.
- [Live diagnostics tooling](RE_NOTES_diagnostics_tooling.md) — in-process xbox_diag server (memory, heap, icall, page write-watch), xbdiag.py client, xbrun.py run harness

- **AddChild (0x00082D30)** -- widget vtable slot +0xA0, the single function that
  attaches a child widget to its parent's list. Trapped in error part 127,
  recovered part 156. While trapped, every child list in the game was empty.
  See `RE_NOTES_DECOMP_PROGRESS.md` part 156.
- **Widget child-list layout** -- container at `widget+0xF8` (class vtable
  0x00196700), holding a head sentinel at `+0x4` and a tail sentinel at `+0x14`,
  each a 16-byte node `{vtable, -1, prev, next}`. `sub_000A3900(container)`
  returns the first child via `[container+0x10]`, or NULL when empty.
  `sub_000A39E0` appends using `[container+0x1C]` as the tail cursor. Other
  classes put the container elsewhere (the screen class at `+0x100`), so never
  assume +0xF8.

## Part 178 -- audio memory sweep, flags from real predecessors (DECOMP_PROGRESS)
- Stream converter `sub_00019040` wrote PCM from VA 0 over .text/thunks/.data when the APU ran.
- Lifter: flags now from real CFG predecessors (`_incoming_flag_states`, `_finXXXX_jcc`).
- Tools: `apply_lifter_diff.py` (transplant lifter fixes hunk-wise), `add_origin_headers.py`, fixfpmem now covers recomp_recovered.c.

## Part 179 -- audio on, attract movie, controllers (DECOMP_PROGRESS)
- Intro MV decode: sub_00147440 jge fix + split-flag sign branch sub_00146C96 (every motion code was negated).
- Stream codec chain recovered (0x19950/0x1A990/0x12D30/0x12D00); XBOX_APU_RUN defaults on.
- XInput HLE in ssx_recomp/src/xapi_input_hle.c (8 XAPI entry points); keyboard fallback; XBOX_INPUT_AUTOPRESS.
- Lifter: recomp_xmm_t model, XLIFT_NAME_MAP tree names, ftol hand-off. Tools: switchtargets.py, recover_bisect.py.

## Part 180-181 -- float returns across calls, race camera (DECOMP_PROGRESS)
- x87 return hand-off gave callers the callee's last push (pi from angle wraps); fpretcheck.py + X87_RET, XBOX_X87_RET=2 default.
- Hand-written sub_000F9DF0 / sub_000FDDF0 fixed (missing push, double pop). Thin menu ribbons were NaN culling at 0x000F7122.
- xemu reference runs: tools/audit/xemu/, frames in reference/xemu_frames/.

## Part 182 -- NV2A pixel pipeline, memory, flip presents (DECOMP_PROGRESS)
- Register combiners + texture shaders -> HLSL (nv2a_psh.c, port of xemu psh.c); d3d8_nv2a.c draws program output.
- Texture cache validated by sampled signature; 53.1 MB arena vs virtual allocations -> xbox_HeapAllocVirtual above 64 MB.
- Present on SET_SURFACE_COLOR_OFFSET flips (video framebuffer exempt); PFIFO ack order; XBOX_NV2A_DRAWLOG / SURFLOG.
- xemu gdbstub RAM dump (xmem.py): character-select camera node + booth carousel identical to ours.

## Part 183 -- launcher, render resolution, native 16:9, rider facing (DECOMP_PROGRESS)
- cmp16 vs 0xFFFFFFFF never equal: pose sampler root bone never turned (riders "backwards"); lifter narrows every cmp macro.
- XGetVideoFlags 0x15299F -> Video_GetScreenModeFromFlags 0xFE580 -> Renderer_SetScreenMode 0xF9EA0 (was "GfxContext_SetBlendPresetByMode"): 0 4:3, 1 letterbox, 2 anamorphic 16:9; g_screenMode 0x1DD840.
- EEPROM XC_ indices were +1 (video = 8); bridge answers index 8 from the launcher.
- Scene target at render size + host_present scaling; launcher (ssx_recomp/src/launcher.c), "<exe>.ini"; direct mode for redirected output.
- Window on its own UI thread (close = TerminateProcess), menu bar via D3D8HostUiHooks (hostui.c); mip chains uploaded (NV2A filter/bias/clamp/aniso); MSAA + anisotropic options; controls.c bindings; log file. Language: hardcoded American at 0xAE104, not settable.
- Race timer: 19 wsprintf placeholders recovered (wsprintf183). Race 17-27 -> 60 fps: hashed texture cache, GPU vertex programs (nv2a_vsh_hlsl), exact-QPC timer thread + KeTickCount, 59.94 Hz vblank thread, pump idle, vsync off, and the ring-wrap tail (nv2a_live_pb.c live_pb_tail_len) that dropped a surface switch every wrap.
- Frame model: Application_FrameTimerCallback 0xB26B0 acc += T - elapsed, clamp -2T, <=2 ms re-ticks; main thread waits 0x48000001. Perf switches + profsum.py / frametimeline.py in tools/audit/README.md; stale.py never kills the player's game.
