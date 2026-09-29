# SSX Tricky decomp progress tracker

**Last updated:** 2026-08-24 (session note, continued same day — **THE GAME LOADS
ITS FIRST REAL ASSETS.** `D:\data\langmerican.loc`, `constant.loc` and
`letter.loc` all open with STATUS_SUCCESS — SSX Tricky's localisation archives,
whose format this project decoded long ago. **Root cause was the CRT's fast
memcpy.** `CRT_MemCopy` (0x00151120) branches on destination alignment; a
32-byte-aligned destination takes an SSE fast path whose 32-byte bulk loop the
lifter emitted with the stores left as literal `/* TODO: movntps ... */`
comments — it **loaded and never stored**, with the 128-bit XMM registers
modelled as 4-byte `float`s. The correctly-translated <32-byte tail still ran,
which is why it was so quiet: short copies worked, long ones lost their bulk and
kept only the remainder. The async loader's filename buffer was 32-byte aligned
and the name >32 bytes, so it came out **empty** — every asset open failed.
Found by probing the async state machine one level at a time (queue has items →
states 0→1→2 reached → state 2 reports ERROR_INVALID_HANDLE → state 0's open got
an empty filename → the enqueue side's CRT_MemCopy). **This is far bigger than
file I/O**: CRT_MemCopy is *the* CRT memcpy, so any struct/string/buffer of 32+
bytes copied to an aligned destination has been silently losing its contents for
this project's entire history — expect other 'impossible' corruption to share
this cause. **Systemic gap**: `grep -rn "TODO: mov"` reports **74** unimplemented
SIMD instructions across the generated code, all memory *stores* (`movntps`,
`movntq`) plus `movhlps`/`movlhps` — 2 fixed here, 72 remain (48 in
recomp_0009.c, 15 in recomp_0008.c). Each is a silent data-loss bug of exactly
this shape; worth a deliberate sweep rather than waiting for each to surface.
New frontier crash (intermittent, ~2/3 runs) in a list search `sub_00164600`
reached via FILESYS_atomic — not a regression, the loader was previously idle.
Full trail: `RE_NOTES_xboxrecomp_test.md` part thirty-four.)

**Last updated:** 2026-08-24 (session note, continued same day — **the async-file
completion never signalled because I deleted an instruction while inserting a
debug probe two parts earlier.** A CaptureStackBackTrace at the offending
`NtSetEvent(NULL)`, resolved with addr2line, led straight to `sub_0014D850`,
whose entry must read the completion event handle out of the request object
(`eax = MEM32(ebx + 0x68)`). In part 30 I inserted a `[FIOOBJ]` probe there with
an Edit whose replacement text simply omitted that line; removing the probe
later restored everything around it but not the dropped instruction, so every
completion signalled a NULL handle and the worker waited forever. Two innocent
candidates were checked and cleared first (`PUSH32` evaluates its value before
decrementing `sp`, so `MEM32(esp+8)` correctly resolves to arg0 — that
translation is right). **Lesson — second self-inflicted regression this session:
when inserting a probe into generated code, the replacement must reproduce every
original line verbatim.** Prefer inserting after a complete statement rather than
rewriting a block containing one. **Verified: NULL signals 1→0; file-I/O worker
kernel calls 9→272 (blocked→working); successful file opens 14→16; 60s run exit
124, 0 crashes; unresolved targets 59→58.** Still open: the worker is active but
has not yet opened a real asset file. **Also answered "how far from rendering?"**
— and the answer wasn't what it looked like: **neither display path is connected**.
`d3d8_CreateDevice` is never called by anything, and `pgraph_d3d11_init()` is
reachable only from `nv2a_pb_replay.c`, a standalone offline tool; the PFIFO pump
makes no `nv2a_*` calls at all. Xbox D3D8 is *statically linked into the title*,
so the recompiled code **is** the graphics driver and drives the GPU through
memory-mapped registers — the display path is GPU-command emulation, not an API
shim. Closed one piece: DXGI needs a real HWND but Xbox has no window system, so
`d3d8_device.c` now creates an output window on demand (groundwork; nothing
reaches it yet). Status artifact brought current. Full trail:
`RE_NOTES_xboxrecomp_test.md` part thirty-three.)

**Last updated:** 2026-08-24 (session note, continued same day — **fixed a real
bug in `bridge_NtWaitForMultipleObjectsEx`: it never read the handle array**. It
called `bridge_read_handle(handles_va + i*4)` — passing the array element's
*address* to a function that takes a handle *value* (compare the sibling
`bridge_NtWaitForSingleObjectEx`, which correctly passes `STACK_ARG(0)`). So the
array was never dereferenced and the array's own Xbox VA was handed to
`WaitForMultipleObjects`, failing instantly with STATUS_UNSUCCESSFUL every time;
the file-I/O thread spun on it ~8,300 times per 25s run. Found by making the
kernel-call trace limit tunable (**new `XBOX_KCALL_LOG` env var**, 0 = unlimited)
— the old hard cap of 200 calls is useless for a hang tens of thousands of calls
in; with it raised, the ordinal histogram made the loop obvious. Fixed to read
the entry then resolve it (tagged token via the handle table, otherwise via
`xbox_resolve_dispatcher_handle`, matching `bridge_KeWaitForMultipleObjects`).
**Result: kernel calls 120,394 → 34,714 and the mix became healthy** — timers
arming, events signalling, rather than one thread burning CPU on a failing call.
0 crashes, exit 124. Build gotcha worth remembering: `BRIDGE_HANDLE_TAG` was
defined ~600 lines *below* the function, so the first attempt failed to compile
and the test silently ran the **stale exe** — which looks exactly like 'the fix
did nothing'; macros now hoisted. **Remaining blocker is now pinned precisely**:
the file-I/O thread waits on tagged tokens 0x48000002 and 0x48000003 — the two
event handles stored at +0x48/+0x68 of the async request object dumped earlier —
but the only NtSetEvent in the entire run targets 0x48000001, so the request's
own completion events are never signalled. Also spotted: one NtSetEvent called
with a NULL handle, a plausible candidate for the completion path failing to
find its event. Full trail: `RE_NOTES_xboxrecomp_test.md` part thirty-two.)

**Last updated:** 2026-08-24 (session note, continued same day — **the game
disc can now be read straight from an Xbox .iso**; verified end-to-end with the
extracted `game\` directory moved away entirely: XBE and all `D:\` reads come out
of the image, 0 crashes, 17 successful opens. Added a read-only XDVDFS reader
(`xbox_xdvdfs.c`), validated against the real SSX Tricky USA image with a
throwaway Python parser and then a standalone C unit test *before* wiring
anything in. Cheap to add because `kernel_file.c` already had two swappable
backends and everything funnels through ~12 `xbox_Nt*File` entry points; also
because read-only turned out to be sufficient — verified, not assumed: every
`D:\` open uses `disposition=0x1` and saves already go to the emulated hard disk,
so disc writes are refused with `STATUS_MEDIA_WRITE_PROTECTED` as real hardware
does. Which paths go to the image is decided by a new
`xbox_path_split_game_disc()` that matches only `to_save == 0` rules, so
TDATA/UDATA keep going to the real filesystem. Tree walks are iterative with an
explicit visit bound (a corrupt image must not blow the stack); lookups are
case-insensitive, accept `\` or `/`, and tolerate the trailing blank the title
genuinely passes. **Bug worth remembering**: the first ISO run crashed at startup
with *every Xbox register zero* and fault addresses that were sign-extended
32-bit values — the signature of a pointer truncated to `int`. Cause:
`CommandLineToArgvW` without `#include <shellapi.h>`. The compiler had said so
(`makes pointer from integer`) but the build grep filtered for `error` only.
**Grep builds for `implicit declaration` and `int-conversion` too — on Win64
those are pointer-truncation crashes waiting to happen.** Usage: an image beside
the exe or one level up is auto-found, or pass any `.iso` on the command line;
with no image it runs from `game\` exactly as before. **Scope caveat**: this
makes the *disc* generic, not the executable — the binary contains SSX Tricky's
10,383 translated functions and a hardcoded entry point, so another title still
needs its own recompile. Full trail: `RE_NOTES_xboxrecomp_test.md` part
thirty-one.)

**Last updated:** 2026-08-24 (session note, continued same day — **found and
fixed a regression I introduced myself two parts earlier, which had been
silently corrupting memory on every call since**. Part 24 replaced
`sub_0015DB00`'s body with a real `memmove` but kept reading arguments as
`MEM32(ebp + 8/0xC/0x10)` **after deleting the `push ebp; mov ebp,esp` prologue
that made `ebp` meaningful** — in this codebase a generated function's `ebp` is
seeded from `g_seh_ebp`, i.e. the *caller's* frame. So every call read dest/src/
size from wild addresses and copied accordingly. It looked like progress (pool
constructed, 53fps frame loop) while quietly scribbling over memory. Caught with
a gdb hardware watchpoint on Xbox VA 0x28 (the fake TIB's TLS pointer, seeded to
0x760000): `Old value = 7733248, New value = 0` inside that memmove. Fixed by
reading args relative to `esp` (+4/+8/+0xC). **Lesson: when replacing a
translated function's body wholesale, the prologue is part of the argument
addressing — deleting `push ebp; mov ebp,esp` silently redefines every
ebp-relative access that remains. Prefer esp-relative reads in hand-written
replacements.** That single fix eliminated the `"(null)\(null)"` blocker chased
through the two entries below — it was never a missing function, just this
corruption destroying the string data — and the title's **real** save paths
appeared with its genuine title ID (`TDATA\45410004`, `UDATA\45410004`, plus
real `TitleImage.xbx`/`TitleMeta.xbx` metadata), all STATUS_SUCCESS. Also
translated `sub_0014C090` (last undetected function in the async file-I/O range;
notably it was *not* the cause of a crash that appeared when enabling it — an
addr2line backtrace proved it merely let execution reach the corrupted TIB
path). Second fix: **directory opens** — the game reached its asset root and
failed on `D:\` and `D:\data ` because Xbox opens directories with no special
flag while Win32's CreateFileW needs `FILE_FLAG_BACKUP_SEMANTICS` (and rejects
the trailing blank the title genuinely passes). `xbox_NtCreateFile` now probes
with trailing spaces/dots trimmed and retries with that flag **only when the
target really is a directory**. **Result: exit 124, 0 crashes, 0 "no bridge"
warnings, 0 `(null)` paths, successful file opens 0 → 16, misses 60 → 59.** The
game now reaches and opens its actual asset root. Next: it loops opening `D:\`
and `D:\data ` without progressing, so trace what it does with those handles
(`NtQueryDirectoryFile` is already bridged, ordinal 207). Full trail:
`RE_NOTES_xboxrecomp_test.md` part thirty.)

**Last updated:** 2026-08-24 (session note, continued same day — **translated
three undetected functions straight from raw XBE bytes, without Ghidra**.
`0x0014B5E0`, `0x0014D700`, `0x0014D720` are undetected by *Ghidra as well*
(`get_function_by_address` returns nothing), so the usual read-the-bytes-from-
Ghidra route was unavailable. **New reusable technique**: the XBE section table
is already documented, so compute `file_offset = VA - section_VA +
section_raw_offset` (.text: `VA - 0x10000`), extract with Python, and
`objdump -D -b binary -m i386 --adjust-vma=<VA>`. That yields an authoritative
disassembly of any gap with no Ghidra round-trip and no risky `create_function`
call — now the preferred route for undetected code. Biggest find:
**`sub_0014B5E0` is the game's periodic timer tick** (registered by
`sub_0014B62F` with the software timer queue at 1000/rate ms, default 10 ms) —
it bumps two global counters and calls **up to eight callbacks** from a table at
`0x203BA0..0x203BC0`. Undetected, so the SoftTimer dispatch's indirect call
missed every tick and *the entire periodic-callback chain never ran*. The other
two are leaf accessors on the async file-I/O request object (`0x0014D720` is
stored in the object itself at `+0x20`, confirmed by a live dump). Also learned
a useful triage distinction: unlike parts 24/27 (stdcall mid-function targets
whose empty stubs returned and leaked 36 bytes, corrupting memory), these are
**cdecl leaves** — a miss leaked no stack and silently returned a *wrong value*
(0) instead, which is why the symptom was a never-resolving file request rather
than a crash. **Check the callee's calling convention before assuming a miss
means corruption.** Dispatch entries inserted in sorted order (`recomp_lookup`
binary-searches; ordering re-verified programmatically, 0 out-of-order across
10,382 entries). Result: unresolved ICALL targets **65 → 60**; 45s run still
exit 124, 0 crashes, 0 "no bridge" warnings, 5 successful file opens. Remaining
target in this subsystem: **`0x0014C090`** — disassembled and understood in
outline (clamps a limit from `obj->[8]`, then calls `0x0014C440`/`0x0014C4A0`/
`0x0014C4D0`), but it is a medium function with three sub-calls rather than a
leaf, so deliberately not rushed; it is the prime suspect for the NULL filename
behind `path="(null)\(null)"`. Full trail: `RE_NOTES_xboxrecomp_test.md` part
twenty-nine.)

**Last updated:** 2026-08-24 (session note, continued same day — **kernel-bridge
audit: eleven ordinals were declared, implemented and arg-sized but never wired
into the dispatch table**, so every call silently returned 0. The two that were
doing real damage: `RtlInitAnsiString` (289) — which fills in a counted
`ANSI_STRING`'s Length/MaximumLength/Buffer — was a no-op, so the descriptor
handed to `NtCreateFile` kept stack garbage (observed `Length=29345` with
`MaximumLength=24`, impossible, beside a perfectly valid Buffer pointing at
`"00000000"`, a title-ID save folder); and `RtlEqualString` (279) returned 0 =
"not equal" for **every** string comparison in the title (~200 calls during boot
alone), so no asset/device/save lookup could ever match. Also wired:
KeDelayExecutionThread (the title's Sleep, which had become a busy spin),
KeStallExecutionProcessor, KeSetBasePriorityThread, MmLockUnlockBufferPages,
ObfDereferenceObject (**fastcall** — arg in `ecx`, not the stack),
RtlTimeToTimeFields, NtResumeThread (handle needed table resolution, not a raw
cast), AvGetSavedDataAddress, AvSendTVEncoderOption. The two string ones needed
real care rather than pass-through: `RtlInitAnsiString` must write its fields
back into Xbox memory **as Xbox VAs**, and `RtlEqualString` must compare exactly
`Length` bytes since these are counted, *not* NUL-terminated, strings.
**Result: "no bridge" warnings 11 → 0; save-game file I/O works for the first
time (successful opens 0 → 5, with well-formed
`\Device\Harddisk0\partition1\TDATA\00000000` and `UDATA` paths returning
STATUS_SUCCESS); 40-second run exit 124 + zero crashes.** The reusable technique:
`kernel_thunk_dispatch` already prints "no bridge for ordinal N" once per slot —
collect those from a run and cross-reference each against the arg-size table's
name comments and whether an `xbox_<Name>` impl exists, and the whole gap list
falls out in one pass. **Remaining blocker is now precisely located**: one open
still fails repeatedly as `path="(null)\(null)"`, built at `sub_0014C7AE`'s
`loc_0014C7D0` by `sprintf(buf,"%s\\%s", MEM32(0x1C4800), esi)` where *both* are
NULL — `0x1C4800` (the CRT current directory) is never written anywhere in the
generated code, and the filename arrives NULL from a caller chain sitting in the
same address range as four still-unresolved ICALL targets (`0x0014B5E0`,
`0x0014C090`, `0x0014D700`, `0x0014D720`), none of which Ghidra or the lifter
recognizes as functions — same undetected-code class as the entry below. Not
started deliberately: function-boundary creation needs the careful
one-at-a-time verification this project has a rule about. Of the 65 unresolved
ICALL targets overall, **60 are real code in loaded XBE sections** (.text 36,
D3D 20, XPP 4) and only 5 are garbage — but note the recompiler must **not** be
re-run wholesale, since the generated files carry many sessions of hand-applied
fixes. Full trail: `RE_NOTES_xboxrecomp_test.md` part twenty-eight.)

**Last updated:** 2026-08-24 (session note, continued same day — **the game's
main loop now runs at ~53 fps and a 45-second run is crash-free**, up from an
infinite non-returning spin). First, a correction: the entry just below claimed
a "120-second run with no crash" — that was wrong. Those runs used `timeout N`
and returned exit code **127**, which was misread as benign; a real clean
timeout is **124**. Run in the background without the wrapper, it still
segfaulted at ~25s. (Verify "no crash" on exit 124 **plus** `grep -c CRASH` = 0,
never on a missing message.) The borrow fix below is still real, just not the
whole story. Root-caused the rest properly: rate probes showed
`Application_FrameTimerCallback` was entered **exactly once and never
returned** — its frame-pacing catch-up loop (`keep ticking until the next-frame
delay exceeds 2ms`) could never exit because the frame-time target field read
**0.000000** instead of 16.667. `Application_ArmFrameTimer` *did* seed it, on
the same object, so a **gdb hardware watchpoint** on that field's native
address caught the writer exactly: a `memmove` under
`D3DDevice_CreateVertexShader` copying **7,345,856 dwords (~29 MB)** into a
**5,188-byte** buffer. Measuring stack balance around one call pinned the
cause: `esp` leaked exactly **36 bytes**, because `sub_00169FE1` and
`sub_0016A116` — two mid-function jump targets inside that scanner's loop —
were never detected by the lifter and were auto-generated as **empty stubs**,
which `return` and unwind the whole frame instead of continuing the loop. That
36-byte shift made the caller read a *pointer* where an element count belonged.
Same bug class as the `memmove` fix below: code the recompiler never emitted,
reached through an edge it did see. Fixed by translating both faithfully from
Ghidra's real bytes — which also allowed **removing a previous session's
admitted workaround** (a `+0x1000` "safety margin" whose own comment said the
root cause was never isolated; it could never have covered a 29 MB overrun).
Then found a second, independent bug: the frame accumulator still grew
unbounded because `KeTickCount` — a kernel **data** export, which titles read
directly from memory — was written once at init under a comment asserting "a
background thread in main.c updates this every ~1ms". **That thread was never
written.** The game's millisecond clock was frozen for the entire run. Added
`xbox_tick_count_thread` (1 ms cadence, matching KeTickCount's real
resolution). Result: frame callback **1281 calls in 24.1s ≈ 53 fps**,
accumulator bounded, unresolved-ICALL cascade down from 265+ to **65**, `g_esp`
stable, thunk-table corruption gone. Next lead: those remaining 65 misses are
untranslated D3D vtable entries (`0x00166F80`+) reached from
`Renderer_SetDefaultDeviceState` during device init. Full trail:
`RE_NOTES_xboxrecomp_test.md` part twenty-seven.)

**Last updated:** 2026-08-24 (session note, continued same day: found and
fixed the actual root cause of the worker-thread crash from the entry
just below -- a second, unfixed instance of an *already-documented* bug
class (RE_NOTES part eight's "dropped borrow on a 64-bit subtraction,"
previously fixed in `sub_001521B3`) sitting in a sibling 64-bit
subtraction in `sub_00152230`, the software timer queue's registration
function. Rather than keep hand-tracing the intricate due-time arithmetic
(error-prone, as the entry below's own notes show), settled it
empirically: this codebase already has a filterable `xbox_log` facility
with an `XBOX_LOG_LEVEL` env-var override, and `xbox_KeSetTimerEx` already
logs every real timer arm's computed `due_ms` at debug level -- no new
instrumentation needed. Running with `XBOX_LOG_LEVEL=3` showed the frame
timer's first two arms correctly computing `due=16ms`, then its third arm
(a re-registration) computing `due=429497ms` -- off by almost exactly
`0x100000000` (100ns units), the exact signature of the known bug class.
`sub_00152230` never assigned its carry flag before an analogous
subtraction (still at its declaration-time `0`), the identical bug already
fixed elsewhere but never propagated here -- data/timing-dependent, which
is why 2 prior arms didn't trip it. Fixed with the same one-line pattern
already proven in the sibling function. Verified live: the same re-arm now
computes `due=1ms` (sane), and more importantly, a 120-second run that
previously crashed reliably within 40-60 seconds every time now runs the
full duration with **no crash at all**. A separate, non-fatal stall
remains after the fix (the process doesn't crash but also doesn't keep
progressing) -- flagged as next session's starting point, not chased
further. Full trail: `RE_NOTES_xboxrecomp_test.md` part twenty-six.)

**Last updated:** 2026-08-24 (session note, continued same day: kept going
on the worker-thread stack-overflow crash from the entry just below. Ruled
out three hypotheses live (a stack leak in `sub_001520CB`'s own polling
loop -- proven balanced via before/after-ICALL probes; `_chkstk`
(`sub_0015CDF0`) as a second `memmove`-class translation bug -- hand-traced
and confirmed correct; `KeInsertQueueDpc`/the thread-notify list being
uninvolved -- proven wrong by re-testing, since this whole area turned out
to be genuinely timing-sensitive and single fixed-probe-count diagnostics
kept missing the actual event on different runs). Switched to a
range-based "is g_esp physically possible" sanity check instead of a
count-capped probe, which finally caught it: `g_esp` starting at
`0xFFFFFFF4` (underflowed) and counting down 24 bytes per iteration,
confirming a real infinite loop. A second diagnostic (one `CaptureStackBackTrace`
hooked into the shared `recomp_icall_miss_log_once` choke-point) resolved
the actual native call chain: `xbox_worker_thread_trampoline` ->
`bridge_KeInsertQueueDpc` -> `sub_001543DE` -> `sub_00152161` ->
`Application_FrameTimerCallback` (a function this project already knew
about and documented: the game's entire frame-pacing mechanism, a
self-rescheduling callback with no `while(1)` anywhere in its call graph).
**Root cause**: `bridge_KeInsertQueueDpc` runs a DPC's routine
synchronously/immediately by design (correct for a one-shot DPC) -- but if
`Application_FrameTimerCallback`'s frame re-arm path routes through it
instead of a real waited-on timer (via the software-timer-queue machinery
in `sub_00152230`/`sub_00152042`/`sub_001520CB`), every "next frame" fires
the instant the current one finishes scheduling it, with zero real-world
delay -- an unbounded recursive loop, each level consuming real native C
stack and simulated Xbox stack, until one overflows some 40-60 real
seconds later. This is a whole-subsystem gap (the software timer queue's
due-time gating), not a single-line bug, so it was fully characterized but
deliberately **not patched** this session -- a rushed fix risked being a
depth-limit workaround rather than the real thing. All diagnostic
instrumentation added and removed cleanly; binary confirmed back to the
prior entry's state (memmove fix + RAM-mirror-gap fix intact) before
stopping. Full trail and the concrete next-session starting point:
`RE_NOTES_xboxrecomp_test.md` part twenty-five.)

**Last updated:** 2026-08-24 (session note: continued straight from the
prior entry's open lead — the `sub_0015663A`→`sub_00155DE5` null-pool
crash — and root-caused it all the way down via live probes rather than
guessing: `sub_0015663A`'s arena arg traced to global `0x203CD4`, set by
`sub_00154D34` from `sub_00156216` (RenderWare's video-memory pool
constructor), which was returning failure on its one and only call.
Hand-tracing that ~400-line function's control flow gave a wrong
prediction (a good reminder this doesn't always work past a few
branches), so switched to instrumenting the actual failure convergence
point and working backward — found `esi` held a stray stack address
where it should still have been the `0` it was xor'd to at function
entry, right after a call to `sub_0015DB00`. **Root cause**: `sub_0015DB00`
is the MSVC CRT's `memmove()` helper (confirmed via calling convention +
the real disassembly's overlap check), whose original jump-table-based
remainder-byte trampolines (`0x0015DC58` etc.) were never recognized as
function starts by Ghidra *or* the recompiler (`get_function_by_address`
returns nothing there) — every dispatch through it silently misses,
corrupting the caller's `esi`/`edi` on **every call, at all 6 of its
project-wide call sites**, not just this one. Fixed by replacing the
whole function with a real `memmove()` call — its only job on real
hardware, not a workaround. This alone fixed the null-pool crash
immediately (the pool's real 1MB allocation now succeeds every run).
Progressing further exposed a follow-on: both the main thread and a
worker thread independently faulted reading Xbox VA exactly `0xF5000000`
— precisely where the prior session's mirror-loop fix stops mapping to
avoid the GPU-MMIO collision, confirming RenderWare's memory-wrap walker
does read into that gap. Fixed by backing the gap with real zeroed memory
(same strategy already used for the GPU MMIO aperture), sized dynamically
from `XBOX_TOTAL_RAM` so it's correct at any heap size. With both fixed, a
longer run surfaces a **new, distinct, not-yet-fixed** bug: the Xbox stack
pointer itself goes wild inside a worker thread (`PsCreateSystemThreadEx
#2`, routine `0x001543DE`) sometime after a clean start — thread-local
`g_esp` is confirmed correctly seeded and stable early on, so the
corruption happens during execution; a stack-leak hypothesis in the
`sub_001520CB`/`sub_001520FE` polling loop was tested and ruled out.
Given this session's dominant pattern (missing-jump-table-style CRT/engine
helper gaps), checking for another mistranslated runtime helper in that
call chain is the recommended first move next session. Full trail: `RE_NOTES_xboxrecomp_test.md` part twenty-four.)

**Last updated:** 2026-08-23 (session note: user pushed back on accepting the
~53MB allocation as "legitimate" at face value (real Xbox retail RAM is 64MB) —
right call. Re-investigated with a reliable technique (`CaptureStackBackTrace`
+ `nm -n` symbol resolution, since gdb's frame-pointer `bt` lies on this
codebase's tail-call-heavy code) and found the earlier `sub_0016A290`
conclusion was wrong; the real chain is `bridge_MmAllocateContiguousMemoryEx`
← `sub_00151380`, whose caller in `recomp_0003.c` pushes a literal immediate
`0x3519998` (55,679,384) right after `Renderer_InitializeD3DDevice()` —
genuine shipped SSX Tricky behavior, confirmed live via probe, not a bug.
Fixed a real, separate bug found along the way (`xbox_MmQueryStatistics`
reported fake host-PC-derived stats instead of real heap accounting; added
`xbox_HeapGetStats()`). Raised `XBOX_TOTAL_RAM` 128→140MB (genuinely needed,
~135MB) and found/fixed three more real infrastructure bugs this exposed: (1)
mirror-view #28 vs. the fixed GPU-MMIO aperture (0xFD000000) address-range
collision — mirror loop now stops before it would reach the aperture, for
any heap size; (2) a dead "let OS choose" sentinel in the base-address search
plus too-narrow candidate spacing — added address-space validation
(`xbox_probe_layout_free`) and a wider stepped search; (3) the real root
cause of a persistent Mirror-14 failure: a vestigial `VirtualAlloc` for a
fake kernel-PE-header page at a fixed native address that's been dead code
since an earlier session's `xbox_resolve_uncached_alias` masking superseded
it — removed, replaced with ordinary Xbox-VA-space storage plus a narrow
redirect in the alias resolver. Result: 27/27 mirrors + GPU MMIO now map
cleanly, and the process gets much further (718 vs. 213 log lines) before a
new, deeper crash in what looks like RenderWare's own video-memory pool
allocator (`sub_0015663A`→`sub_00155DE5`) reading a garbage/out-of-range
arena-limit field. Not yet root-caused — flagged as the next concrete lead.
Full trail, all four fixes, and the open lead: `RE_NOTES_xboxrecomp_test.md`
part twenty-three.)

**Last updated:** 2026-08-18 (session note: this update was written by a session that
had fallen behind the project's actual state — see the note at the top of
`RE_NOTES_xboxrecomp_test.md`. Ran a real test of the external tool
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) — static recompilation
(x86→compilable-C, not decompilation), not previously evaluated in this project —
against the real `default.xbe`, then pushed a follow-up all the way through an actual
build and run. No renames from this; it's an independent-tooling cross-check, not RE
work via Ghidra. Headline result: **a real `your_game_recomp.exe` built from all 3,815
recompiled functions and ran with zero crashes** — full Xbox memory-space mapping, all
114 kernel thunks resolved, the real entry point (`0x00154218`) executed, and (after
seeding one indirectly-referenced address via the tool's `--seed-functions`) the
spawned game thread routine itself executed and returned cleanly. Found/fixed 2 real
upstream MinGW-portability bugs along the way (macro collisions between the tool's own
Windows compat shims and unrelated real Windows SDK symbols:
`__debugbreak`/`KernelMode`). Also dynamically confirmed, via real execution, this
project's very first open question from session 1 (the `0x1541A9` thread-context
address) — not just another static guess. Also confirmed not-RenderWare and found
4,128 vtable-thunk functions via the tool's own vtable scanner (worth diffing against
live Ghidra's current ~5,443-function count for anything still missing), and
mechanically recompiling `Script_DispatchOpcode` matched this project's own manual RE
of that function instruction-for-instruction. **Then closed the loop the other
direction**: fed `scripts/ssx_auto_rename.py`'s 1,317 renames into the recompiler
(`xboxrecomp_output/apply_re_names.py`, reusable) so the generated C carries real names
instead of `sub_XXXXXXXX` — 828 applied immediately, 489 more recovered by seeding
xboxrecomp's disassembler with addresses this project's live-Ghidra work already knew
about but the automated heuristics didn't (98.1% total coverage, 1,292/1,317), rebuilt
with both the naming improvement and the deep thread-routine execution result intact
(zero crashes, zero regressions). ~30% of the 4,280 generated functions are now
human-readable. See `RE_NOTES_xboxrecomp_test.md`. Setup artifacts in sibling folders
`xboxrecomp/`, `ssx_recomp/` (the buildable game project), and `xboxrecomp_output/`
(including `apply_re_names.py` and `seed_functions_final.json`, both reusable
as-is for a future pass once more renames accumulate). **Then found and seeded the
actual game entry point**: reading the translated CRT thread-bootstrap's generated C
revealed an indirect call through the thread's own context parameter, resolving to
`0x1541A9` — this project's very first open question (session 1), now dynamically
confirmed as a real called function pointer rather than inert context data. Seeding it
got genuine SSX Tricky code executing for the first time (8 distinct kernel calls vs.
2-4 before, several syscalls never seen in any prior run), reaching an actual
`PsTerminateSystemThread` call before a real `SIGILL` crash with a confirmed root
cause: the runtime's bridge stub returns instead of terminating (verified in its own
source comment as tested only against Burnout 3's specific caller), so SSX Tricky code
falls through into unreachable bytes when it does. **Fixed**: extended the crash handler to
catch illegal-instruction faults (was access-violation only) and report the exact
Xbox VA, pinpointing a wild jump ~24.8MB outside the entire ~2MB mapped image (stack/
return-address corruption, not a simple fall-through). Root cause read directly from
`PsCreateSystemThreadEx`'s own documented threading model (thread routines run as
nested synchronous C calls, not real OS threads) — added `setjmp`/`longjmp` to
`xboxrecomp/src/kernel/kernel_bridge.c` so `PsTerminateSystemThread` (never returns on
real hardware) unwinds the whole C call chain back to the thread-start site instead of
falling through. A general runtime fix, not an SSX-specific hack. **Rebuilt and
reran: exit code 0, zero crashes**, clean coherent execution through everything
currently translated. **Then corrected an over-optimistic read**: the clean
termination wasn't real logic finishing, it was hitting 2 empty auto-generated stub
functions almost immediately. Seeded all 2,170 project-wide stubbed addresses in one
batch (4,282 → 10,078 functions after reclassification), which surfaced a **real bug
in the recompiler**: `ebp` declaration logic keyed off Burnout 3's hardcoded SEH-helper
addresses instead of the correctly auto-detected ones for this game — fixed in
`tools/recomp/translator.py`. Rebuilt clean (10,078/10,078, 0 failures), reran: **16
kernel calls now (up from 8)**, including a real `IoCreateFile` attempt (genuine
NTSTATUS failure, not a no-op) — first real file-I/O-adjacent game logic reached.
Still exit 0, zero crashes. **Diagnosed the file-path failure — first as an apparent
Burnout-3/RenderWare architecture mismatch in a fixed TLS region (`fs:[0x28]`), then
corrected that read after actually tracing it**: `sub_001543DE`'s full body (only its
tail had been read before) shows SSX Tricky's own compiled code correctly
initializing that exact region via the same chain, copying the real TLS template from
the XBE — the runtime's mechanism is correct and generic, not RenderWare-specific.
The failure is very likely just an ordinarily-unset per-thread TLS variable, already
handled gracefully by the game's own code. No fix needed; corrected the earlier
over-attribution once traced instead of leaving it standing. **Seventh pass**: seeded
the remaining stub gap once more (8,024 functions translated, 0 failed, 11,716 total
after reclassification, stubs down to 415 from 2,170 at campaign start). **Confirmed
by direct diff: execution is byte-for-byte identical to the prior run** — nearly
doubling the compiled/linked codebase (3,815 → 8,024 functions) introduced zero
regressions. Running scorecard: real linked/running exe, zero crashes across every
scale-up, 20 real kernel operations (incl. genuine memory allocation and file I/O),
3 general (non-SSX-specific) bugs found and fixed in the shared tool, ~1,300 of this
project's own function names flowing into the generated C. Re-seeded everything still stubbed
project-wide again (11,144 functions after reclassification, translated clean).
**Result: 20 kernel calls** (up from 16), including the first genuine memory
allocation and a missing kernel function (`HalRequestSoftwareInterrupt`) found and
properly implemented (documented no-op, correct for this synchronous execution
model) rather than left as a silent gap. Still exit 0, zero crashes — deepest, most
complete run yet. Also linked the real extracted game assets in via NTFS junction
(not a 2.9GB copy) — not yet exercised, execution doesn't reach file I/O at this depth
yet. **Eighth pass: the biggest unlock yet.** `xbox_memory_layout.c` hardcoded the
Prcb pointer at `fs:[0x20]` to 0 (a Burnout-3-specific hack), silently forcing SSX
Tricky's real app-init branch (`Application_ConstructAndInitInput`) down a dead no-op
path instead. **Fixed** (real fake Prcb buffer, non-zero `+0x250`) — kernel calls
jumped from 20 to **200+**, with six full worker-thread (`PsCreateSystemThreadEx`)
cycles now running and terminating cleanly. Hit a new `SIGILL` after cycle #6; fixed
two more diagnostic bugs to see it clearly (bogus RIP-based "Xbox VA" math for
illegal-instruction faults, and an ASLR-blind native-stack-walk filter, both in
`main.c`), then traced the crash all the way to a specific, understood game
assertion: `FILESYS_atomic` (`0x0014E770`) rejecting a `FILE_load` call because the
target device's priority ceiling is still at its zeroed default — confirmed by
reading the assert string directly out of `default.xbe`'s own `.rdata`. Root cause
not yet fixed (need to find what's supposed to raise that device's priority before
first file load; likely connected to the earlier `IoCreateFile`/path-not-found
finding). **Ninth pass corrected that hypothesis with real instrumentation**
(temporary `fprintf`s in the generated code, removed after use): the device-open step
actually runs every time and correctly sets priority to `0xFF` — the real bug is that
the *requested* priority argument `FILESYS_atomic` reads back is garbage (a function
pointer once, a stack address once, never a real integer), an argument-passing bug,
not a missing init step. Also found the device array's base pointer is a suspiciously
low, likely-bogus address (`0xCE8`, below the whole loaded image) — origin not yet
traced. Separately confirmed the illegal-instruction assert doesn't reliably terminate
the process (3 repeat runs, identical up to 423 kernel calls each time; 2 stopped
cleanly, 1 continued into an unrelated fatal access violation near the entry point —
a corrupted-resumption artifact of the same root cause, not a second bug). See
`RE_NOTES_xboxrecomp_test.md`'s ninth follow-up. **Tenth pass: found and fixed the
real root cause.** Traced via real x86 disassembly (capstone) to a genuine
code-generator bug — `sub_00154476` ends with `ret 0x18` (callee cleans its own 24
bytes of args) in the real binary; the translator generated the correct epilogue
piece (`sub_001544C6`) but the fall-through predecessor (`sub_001544C3`) never called
it, dropping the cleanup silently. **Fixed** — rebuilt, ran 3x: the `FILESYS_atomic`
assert is gone entirely, kernel calls up 423 → 435, crash now deterministic (was
flaky before, strong confirmation). Codebase-wide sweep found ~888 functions (11%)
with the same missing-linkage shape, likely a systemic translator issue — fixed only
the one proven to matter rather than blind-patching all of them (most are probably
unreached dead paths this deep into the game). **New crash past this point**: the
Xbox-side stack pointer has wrapped around (~15MB underflow) by the time execution
reaches a float-to-int helper; that function is an innocent bystander, the real
corruption is earlier in the now-longer call chain and not yet traced. **Eleventh pass eliminated the stack corruption entirely**: switched to gdb (watchpoint
on `g_esp`, then `bt`/`x/i $pc` on the raw SIGSEGV — much more reliable than the
custom VEH stack-walker), found and fixed 5 more instances of the same
missing-fall-through-linkage bug class: `sub_0015CAC7`'s missing epilogue link,
`CRT_ftol_TruncateToInt64`'s own missing fall-through for the *common* `eax != 0`
case (likely the biggest single fix, given this CRT helper backs nearly every
float-to-int conversion in the game), 6 sibling FPU-control-word helpers sharing one
dropped linkage, and `RNG_NextUInt32`'s overflow-carry branch (~50% of all RNG calls,
game-wide). Verified 3x per fix. `esp` is now fully healthy at the crash point —
the whole stack-corruption saga is resolved. Execution reaches new territory and
crashes in `sub_000A8E60` walking what looks like an uninitialized callback-object
list (not corruption this time). Not yet traced. **Twelfth pass traced the InputManager crash to two genuinely unresolved functions**
(`sub_00150BB1`/`sub_00150BE1`, the heap allocator's free-block search — never
disassembled, not a linkage bug this time) and fixed them properly: ran the
project's established seed-and-regenerate pipeline, then, after a full codebase swap
surfaced a different crash from re-splitting elsewhere, reverted and surgically
transplanted just the newly-resolved functions (plus 2 more via a second targeted
seed) into the existing tested codebase. Rebuilt, ran 3x: no crash at all — execution
now settles into a stable loop of `RtlEnterCriticalSection`/`RtlLeaveCriticalSection`
calls. Read as a legitimate environmental limit (main thread spin-waiting on a
condition a concurrent thread would set, impossible since worker threads run as
synchronous nested calls here, not real concurrency), not a new bug. **Thirteenth pass found the twelfth's "concurrency wait" theory was wrong**: attached
gdb to the actually-hung process (checking every thread, not just gdb's default) and
found the game thread genuinely stuck in an infinite loop inside `sub_00150BB1`,
walking a linked list from address 0 because the heap's size-class bucket for this
allocation was never populated (the one function touching that array turned out to
build one named 55MB arena, not fill size-classes — where classes get populated from
it is still open). Added an explicit, acknowledged workaround (200K-iteration search
cap, fails gracefully) rather than a real fix. With it active, execution passes
`Application_ConstructAndInitInput` entirely and reaches a new crash in
`sub_00151E01` touching the same Prcb structure from the eighth follow-up. Not yet
investigated. **Fourteenth pass found the real root cause and reached a fully clean run.** Watched
the Prcb corruption live, traced it to `sub_0015457F` (the *normal* "no preferred
address" allocation path) never calling `sub_00154584`, which holds the real kernel
ICALL performing the actual memory reservation — the same missing-linkage bug as
always, but this time meaning the game's entire 55MB heap arena was never allocated,
explaining every symptom since the twelfth follow-up. **Fixed.** The real allocation
call then fired but returned NULL — a genuine resource limit: `xbox_memory_layout.h`
deliberately caps total RAM at the real Xbox's 64MB for a documented
RenderWare-specific reason that doesn't apply to SSX Tricky (confirmed non-RenderWare
earlier in this project). Verified the mirror-view wraparound system scales safely
with this constant and raised it to 128MB. Rebuilt, ran 3x: **exit code 0, zero
crashes, zero hangs** — heap allocates for real, kernel calls reach 373, main thread
terminates and shuts down cleanly. First completely clean run of the whole project.
**Fifteenth pass fixed the missing-linkage bug class at its root, in the tool
itself.** All 9 hand-patched functions from earlier rounds were one bug in
`disasm.py`'s `build_basic_blocks()`: no handling for a block whose last instruction
falls through past the function's own end into a sibling function (a tail call with
no explicit `jmp`). Fixed generally in `translator.py`, reusing the same machinery
already used for explicit tail jumps. Verified: all 9 prior hand-patches now
reproduce automatically, and a full-codebase count found 1,271 total instances —
over 140x what was caught by hand. Likely the single highest-leverage fix of the
project. Swapped the regenerated codebase in, rebuilt: runs stably to a precisely
identified new spin (`KeWaitForMultipleObjects`, no kernel bridge implementation at
all) — a real concurrency wait this synchronous threading model can't satisfy, a
materially larger task than any fix so far. See `RE_NOTES_xboxrecomp_test.md`'s
fifteenth follow-up.) **Sixteenth pass (parts six-nine of RE_NOTES_xboxrecomp_test.md,
several sessions): from the KeWaitForMultipleObjects spin to a genuinely cycling
frame-sync loop.** Part six's systematic cdecl/stdcall capture-point sweep plus fixing
two more missing-fall-through cases got `this` finally resolving correctly at
`Application_RunMainLoop` (part seven), narrowing the remaining blocker to one specific
never-signaled event via gdb backtracing (retracting two wrong hypotheses along the way:
a thread-pool dispatch red herring, and a "pump thread exits early" theory, both
disproven by gdb breakpoint hit-counting over real time windows -- now the established
reliable method over log-reading). Part eight then found and fixed three more real bugs:
`KeQueryInterruptTime`/`KeSetTimer` had zero implementation despite working real backends
already existing (wired up, reconciling the two separate handle-identity schemes in this
bridge layer so a timer's signal and a waiter's wait resolve to the same native handle),
a dropped-borrow bug in a 64-bit subtraction, and -- the headline finding -- that every
translated function's simulated x87 FPU stack is a fresh local C variable with no bridge
across a call boundary (unlike `ebp`, which has one: `g_seh_ebp`), meaning most
float-to-int conversions game-wide had likely always silently computed from 0.0. Part
nine implemented the fix: a scoped `g_ftol_arg` bridge for `CRT_ftol_TruncateToInt64`'s
own 491 call sites plus its 5-function lifter-split fragment chain, and -- after 10 more
call sites turned out to have no local FPU stack at all (leaf functions returning float
via real ST(0), and same-function branch fragments) -- a general `g_x87_st0`
thread-local mirror, added to all ~2032 local `fp_push` macro definitions project-wide
(purely additive, safe without auditing every site individually). Verified via live
probes and gdb hit-counting: the frame-timer accumulator now ticks cleanly at
~16.67ms/step (was always 0), `KeSetTimer`'s due-time is a clean monotonic ~60fps
sequence (was corrupted by 2^32-scale jumps from a related 64-bit `fistp`/`fild`
truncation bug also fixed this pass), and -- `NtWaitForSingleObjectEx`/`NtSetEvent` (the
frame-sync event) now hit 102/127 times over a real 90-second run, versus the prior
"fires exactly once, never again." **The frame-sync wait/signal loop cycles repeatedly
for the first time in this investigation's history** -- not yet real-time-paced, a
separate interpretation-overhead question. One side finding flagged but not yet
root-caused: a short, bit-identical-repeating sequence of `CRT_ftol_TruncateToInt64`
calls with rapidly growing values (up to ~2.3e19), plausibly a real hash/checksum
routine unmasked now that real values flow through instead of always truncating to 0.
See `RE_NOTES_xboxrecomp_test.md`'s parts six through nine.) **Seventeenth pass (part
ten): the real state machine was dead on arrival, found and fixed.** Continued from the
sixteenth pass's cycling wait/signal loop by tracing why `Application_TickFrame` never
did real per-frame work despite firing regularly. Chased a state field stuck at 0 back
through 4 levels (`Application+0x10` <- `Application+8` <- `Application+4` <-
`Application_StateMachineTick`'s return value) to the actual root cause:
`Application_StateMachineTick` dispatches through a 4-entry jump table keyed on a state
index, and slot 0's real target (`0x000A9BAB`) was **never translated by the lifter at
all** -- only reachable via a computed jump table, invisible to static disassembly
(the same bug class as the earlier-documented "6 jump-table arms of `sub_0014B730` never
lifted" finding). Worse, `RECOMP_ITAIL` (the tail-call dispatch macro, unlike
`RECOMP_ICALL_SAFE`) had no miss-log fallback at all, so the failed lookup silently did
nothing and the caller's stale register value leaked through disguised as a real return
value -- completely invisible without live probing. **Fixed**: disassembled the real 16
bytes via `objdump` against the actual XBE, hand-translated the function (advances the
state index 0->1, tail-calls into `StartScreen_Create` -- already a correctly-translated
function matching the documented real first boot state exactly), and added a general
miss-log fallback to `RECOMP_ITAIL` itself so this bug class can't hide silently again.
Verified via `gdb` breakpoints: both the new function and `StartScreen_Create` now fire,
confirmed on a clean rebuild after probe cleanup. Every symptom traced across the
sixteenth and seventeenth passes (dead per-frame ticks, unreachable state-check branches,
no graphics after a 7-minute blind wait) shares this one root cause. Not yet confirmed
whether this alone reaches graphics -- a 4.5-minute post-fix soak still showed none,
plausibly just needing more real time given the documented boot sequence's frame counts
against this environment's current ~1-1.5Hz tick rate. See `RE_NOTES_xboxrecomp_test.md`'s
part ten.) **Eighteenth pass (parts eleven-twelve): two more real hangs found and fixed
in the boot chain, one more precisely diagnosed.** Continued past the fixed state
machine by tracing why the game still never reaches graphics. Found
Application_RunMainLoop calls StartScreen_Enter (the state's "enter" hook), which never
returned even after a 3.5-minute soak with a direct before/after probe -- traced 6 more
levels deep (Font_LoadAndParse -> FILE_LoadRawFileSync -> FILESYS_atomic -> FILE_load ->
sub_0014C4A0 -> sub_0014C3A0) to a genuine infinite loop, confirmed live at 140,000,000+
iterations in 15 seconds. Root cause via objdump against the real XBE: a `jne` branches
on flags from a `test` whose operand gets overwritten by an intervening `mov` before the
original translation's branch check (real x86 flags survive the mov; the translation
used the wrong, reloaded operand) -- fixed to match real semantics, though that alone
didn't end the loop, since both tested values are genuinely constant for this call: the
loop's real job is calling a device-completion-flag check that nothing in this
synchronous execution model ever signals, an ordinary busy-wait-for-async-completion
pattern. Applied the same kind of bounded-iteration workaround this project already used
successfully for an identical situation (the heap allocator's free-list search,
seventeenth pass's predecessor). Also found, while reading the stuck function, a real
systemic bug not fixed this pass: some functions legitimately repurpose `ebp` as a
scratch register, but this codebase's `g_seh_ebp` frame-pointer bridge assumes `ebp` is
always the real frame pointer, so those functions corrupt the shared frame chain when
they propagate it forward -- flagged for a future project-wide pass. Verified the font
load's whole call chain now completes and `StartScreen_Enter` finally returns to
`Application_RunMainLoop` for the first time. This immediately surfaced (and the same
pass fully diagnosed) a third bug: `esi` -- `Application_RunMainLoop`'s own `this`,
expected stable for the whole function -- gets clobbered to the exact heap buffer address
the font load allocated, the signature of a missing `esi` save/restore somewhere in the
newly-reached font-load call chain (never exercised before this session's fixes).
Everything observed downstream (a null current-state pointer, a garbage vtable read, an
icall to a bogus target) is a fully explained consequence of this one corrupted register,
not a new mystery. Not fixed this pass; documented precisely enough (exact corrupted
value, exact origin, exact candidate function list) to fix directly next time. Three
consecutive previously-invisible boot-chain hangs found and resolved or precisely
diagnosed across these two passes -- a strong, consistent signal that the remaining path
to graphics is a finite chain of comprehensible bugs. See
`RE_NOTES_xboxrecomp_test.md`'s parts eleven-twelve.) **Nineteenth pass (parts
thirteen-fifteen): traced a register-clobber five call levels deep, ruled out a
plausible-looking fix, and found + fixed a second missing-jump-table-arm gap (7
functions this time).** Continued past the fixed font-load hang by chasing exactly why
`Application_RunMainLoop`'s own `esi` (its `this` pointer) kept reading back as an
unrelated heap-buffer address. Used the same live-probe bisection method throughout:
ruled out the previous pass's workaround loop and the FILESYS_atomic/FILE_load boundary
(both esp/esi-clean across every call, live-verified), briefly applied and then
correctly reverted a plausible-looking "duplicate push" fix in `StartScreen_Enter`
after rebuilding showed it changed nothing (root-caused via the real callee's own `ret 4`
convention -- the original two-push translation was actually correct). Swept drift
probes across `StartScreen_Enter`'s three remaining calls in one run and found it
precisely: `Font_LoadAndParse`'s call site shows `esi` going from a valid object
pointer to NULL and `esp` drifting 76 bytes. Bisected two more levels deep (entry
snapshot + exit checkpoint in `Font_ParseGlyphTable`, then the same in
`Font_UnpackGlyphBitmapTexture`) and localized an 84-byte leak to one specific 9-argument
icall. Verified the call site's real bytes via `objdump` match the translation exactly
(ruling out a second missing-arguments hypothesis), then found the true mechanism: the
icall's target, `GfxContext_QueueTextureFromRawData`, itself tail-jumps through its own
11-entry jump table -- and slot 3's real target (the one this specific texture load
actually hits, confirmed via a live index probe showing a legitimate in-range value, not
garbage) was **completely unresolved** -- caught immediately by the `RECOMP_ITAIL`
miss-log added two passes ago, the toolkit fix paying for itself directly. Reading the
whole table found **7 of its 11 entries missing** (only the 4 pointing to an
already-translated shared handler existed) -- the same "lifter never found it, only
reachable via computed jump" bug class as this thread's very first fix, seven instances
at once. Hand-translated all 7 from real XBE bytes (`sub_000FA2C5` through
`sub_000FA333`), verified via `gdb` that the new functions fire and the previously-dead
shared continuation now genuinely executes, and confirmed the specific miss-log entry is
gone on rebuild. **The original `esi` corruption is unchanged even with this fix
verified working** -- the 503-instruction shared continuation itself
(`sub_000FA34A`) most likely has its own separate, not-yet-audited stack-balance issue.
Documented precisely enough (the exact function, the exact drift-probe pattern that
resolved every prior hop) to continue directly next time rather than starting over. See
`RE_NOTES_xboxrecomp_test.md`'s parts thirteen through fifteen.) **Twentieth pass (part
sixteen): the register-clobber is fixed -- root cause was one missing instruction.**
Bisected one more level with a single drift probe around the 654-instruction real
function `sub_000FA34A` calls into (8 stack arguments) rather than auditing it by hand:
pre/post esp differed by exactly -36, the entire call's arguments plus fake
return-address never recovered. Root cause: that function's real epilogue is
`leave; jmp 0x1788b2`, a tail-call into a sibling fragment that turned out to be one of
the ~239 "not detected" empty stubs -- doing nothing at all. The real bytes there, via
objdump (after first computing the wrong file offset against the wrong XBE section and
getting garbage, then recomputing against the correct one), are a single instruction:
`ret $0x20`. Hand-translated it, and the whole chain snapped into place immediately: a
live probe at `Application_RunMainLoop`'s own state-tick dispatch -- the exact site that
showed the corrupted `esi` at the end of the nineteenth pass -- now reads the correct
Application object, the correct StartScreen instance, and the correct dispatch target,
confirmed via gdb that `StartScreen_ResetState` and its own callee both now fire. The
register-clobber that took five function-levels of live tracing across four consecutive
passes to run down is fixed. Checked for immediate downstream effect: a field frozen at
exactly 0 for the entire investigation (the "pending sub-object" `Application_TickFrame`
checks) was observed to actually change to a real pointer for the first time -- the
underlying data this whole chain operates on is now demonstrably live, not permanently
frozen. Its sibling field (the state enum gating real per-frame work) is still 0 at the
checked point, so that's the next concrete target -- find what's supposed to update it
in response. See `RE_NOTES_xboxrecomp_test.md`'s part sixteen.) **Twenty-first pass (part
seventeen): found and fixed the `Application+0x10` blocker, then found a fourth,
deeper bug still open.** Traced `Application+8`'s writer (`sub_000AA150`,
"SetPendingState") by static reading, confirming `Application_RunMainLoop`'s poll
loop now calls the new state's "IsReady" check for the first time -- which always
returned false. Root cause: `ScreenBase_TickAsyncAssetLoad`'s own 5-entry jump table
was missing from the dispatch table, same lifter blind spot as parts thirteen-sixteen
(computed-jump targets invisible to static disassembly); ground-truthed via `objdump`
and found the lifter had actually emitted 4 of the 5 slots' code already, just
unbounded, PLUS silently dropped each slot's true opening instructions (9-14 bytes)
after the preceding slot's `ret` -- a new variant of the same bug class. Extracted 5
slots + a shared epilogue into named functions. In passing, also fixed a stale
`g_recomp_table_size` constant (10078 vs an actual 10341 entries) that was silently
making the dispatch table's last 263 entries unreachable via its own binary search,
pre-existing and unrelated to today's edits. This unblocked real forward progress
(new never-before-seen `ICALL-MISS` targets appeared), which then surfaced a
**self-inflicted** stack leak in my own hand-translated `sub_000AF200` trampoline
(missing the codebase's universal fake-retaddr push/pop pair around a call) -- found
via a new, reusable diagnostic added directly to the shared `RECOMP_ICALL_SAFE` macro
(tracks `esi` across every indirect call system-wide plus a recent-icall-target
ring-buffer dump). Fixing that surfaced a **third** missing jump table
(`StartScreen_RenderStatusText`'s own 17-entry state dispatch), worse than the first
since a miss leaked its entire 2184-byte local frame -- traced a divide-by-zero crash
in an entirely unrelated function (`InputManager_PollDevicesIntoCache`) two call-levels
and several ticks away back to this one missing table. Fixed the one state reachable
this early (state 0) properly; the other 14 unreached states get safe
frame-preserving placeholders (matching this codebase's existing "not detected" stub
convention) rather than full translation, clearly marked as a follow-up. The game now
runs measurably further (through async asset load, several input-poll ticks, into
rendering the status-text screen) before hitting a **fourth**, deeper `esi` corruption
inside `StartScreen_Render`'s own render call chain -- diagnosed but not yet fixed;
strongest lead is an argument-count mismatch between a vtable call site and
`Text_DrawGlyphStringWide`'s `ret 40` calling convention. See
`RE_NOTES_xboxrecomp_test.md`'s part seventeen.) **Twenty-second pass (part
eighteen): found bug #4's actual root cause and fixed it -- a fifth instance of the
same "not detected" empty-stub bug class (`sub_000FDDF0`, called from
`GfxContext_WaitGPUAndCheckIdle`, `StartScreen_Render`'s very first icall) --
leaking its caller's fake-retaddr push exactly like `sub_001788B2` (part sixteen)
and this session's own `sub_000AF200` (part seventeen). Unlike those two tiny
trampolines, this one was a genuinely substantial function (a 4-channel byte
fade/pulse-easing loop ending in a real tail-jump into an already-translated D3D
import-thunk flag setter) that the original lifter's static analysis simply never
found a call-graph path to; hand-translated and registered it faithfully via
`objdump` ground truth. Argument-count mismatch theory from part seventeen was a
red herring -- the actual cause was one level higher. Then traced a *sixth*,
still-open corruption one level deeper: with bug #4 fixed, the same `esi=0` crash
pattern recurs later, now isolated to a genuine (non-garbage) nested vtable call
inside `StartScreen_Render`'s own body back into `StartScreen_RenderStatusText`
itself -- `ebx`/`esi` are provably correct going in and become the literal integer
`1` by the time it returns, but an exhaustive push/pop-vs-every-exit-path audit of
12 functions in that specific call tree (re-verifying `sub_000AF272` byte-for-byte
against `objdump` again in the process) found all of them correctly balanced.
Checked 4 more candidates immediately after (also clean), then switched from
stack-*balance* auditing to direct register-*value* tracking around
`sub_000AF272`'s own 3 calls -- conclusive within one rebuild: `esi`/`ebx` come
back wrong specifically after `sub_000AA5C0` returns, holding a literal constant
address hardcoded inside its callee `sub_001502D0` (the sprintf-style
localized-string formatter). One level deeper: `sub_001502D0` calls
`sub_001507F0` **twice, unconditionally, before either of its own two internal
jump-table dispatches even run** -- and `sub_001507F0` was **yet another "not
detected" empty stub** (a simple ANSI-to-wide string-copy helper), leaking 8
bytes on every single localized-string render regardless of whether the string
has a `%` specifier at all. This -- not either jump-table gap -- is the actual
root cause of the whole bug #4/#5 chain, since the string here ("Checking hard
disk...") never reaches either dispatch. Hand-translated the real function and
registered it. **Verified with a 90-second soak test: zero crashes** -- the
first stable multi-minute run in the entire parts-twelve-through-eighteen
investigation arc. Also fixed in passing, not itself the cause but a genuine
separate bug: `sub_001502D0`'s own two jump tables (0x150754/6 entries,
0x150780/14 entries) had 19 of 20 combined targets missing from the dispatch
table -- the single largest gap found this session -- fixed with the same
safe-placeholder-frame-unwind approach used for bug #3's unreached states. Five
real bugs found and fixed total across parts seventeen-eighteen. See
`RE_NOTES_xboxrecomp_test.md`'s part eighteen.) **Twenty-third pass (part
eighteen, continued): translated the full ~950-byte async-I/O worker function
rather than deferring it to a follow-up.** Mapped its complete 11-entry
internal jump table (a real async-file state machine: open, set-flags,
read-header, read-data, seek, probe-eof, read-remaining, close, error)
against `objdump` ground truth and translated it as one self-contained
function using `switch`/`goto` (not `RECOMP_ITAIL`, since every target is a
local label, unlike every other jump-table fix this session) -- all its
callees were already correctly translated, only this top-level glue was
missing. Caught and fixed one real push-order bug during a second adversarial
pass before integrating. Registered `sub_0014D850`. Result: dramatically more
kernel activity -- previously-unseen ordinals firing for the first time
(`NtWaitForMultipleObjectsEx`, `KeSetBasePriorityThread`,
`ObfDereferenceObject`, `NtResumeThread`) -- with no crash and no change to
`Application_TickFrame`'s normal pace. Found and re-enabled a directly related,
already-diagnosed gap in `xboxrecomp/src/kernel/kernel_bridge.c`:
`NtWaitForMultipleObjectsEx` (ordinal 235) had been explicitly disabled by a
prior session with a comment stating exactly why -- the async-I/O completion
path it waits on didn't exist -- which is precisely what this pass's fix now
provides. Re-enabled it; verified no deadlock across multiple runs (main loop
still ticks normally). `StartScreen_SetState` still doesn't fire yet -- tracing
the new function's own runtime-registered completion callback forward is the
next concrete, well-scoped target. Continued further same pass: found the
true enqueue site (`sub_0014E4E0`, not the initially-suspected
`sub_0014E200` which is never even called) and tested the shard-mismatch
hypothesis live -- **refuted**: both the worker's queue index and newly
enqueued items' own index consistently resolve to the same shard (0), and
real items are genuinely created. The remaining gap is most likely a
worker-thread lifecycle/timing question (does a new worker get (re-)spawned
once items exist, or does the one that already ran exit for good) --
precisely scoped for a follow-up session running both enqueue- and
dequeue-side probes simultaneously. Seven real bugs fixed total this part,
one kernel-bridge gap re-enabled, stability verified up to a 5-minute soak.
See `RE_NOTES_xboxrecomp_test.md`'s part eighteen (continued).) **Twenty-fourth
pass (part nineteen): found and fixed the real root cause of the "always
empty" queue pop -- refuting last pass's own timing/lifecycle hypothesis.**
Live dual-side probes (queue base pointer + head/tail/count, printed from
both the insert and pop call sites in the same run) showed the worker's
queue pointer (`ebx`) was correct on its very first loop iteration, then
became `0x00000000` (then garbage) on every iteration after -- while the
real queue at the original, untouched address kept filling up. Bisected to
one call chain (`sub_0014D850` → `sub_001647E0` → `sub_00151C35` →
`sub_00151C55`, the real `NtWaitForMultipleObjectsEx`-calling wrapper) and
ground-truthed the real call site (Xbox VA `0x00151C7D`) via objdump: 6 real
arguments are pushed before `call [0x187314]` with no caller-side cleanup
after, meaning the callee must self-clean all 24 bytes. Root cause was
native, not lifted code: `xboxrecomp/src/kernel/kernel_bridge.c`'s
`bridge_NtWaitForMultipleObjectsEx` (ordinal 235) was missing the real NT
prototype's `WaitMode` parameter entirely (only reading 5 of the real 6
args), and `stdcall_args_for_ordinal` declared only 20 of the real 24 bytes
for it -- leaving 1 stack word permanently unpopped on every call, which
then poisoned the caller's next register-restore pops. Since the registers
are `__thread` globals, the corruption was permanent for that thread's
whole life, explaining why only the *first* pop (before any wait) ever
worked. Fixed both the missing argument read and the declared byte count
(also fixing a latent semantic bug where `Alertable`/`Timeout` were reading
the wrong stack slots even on a hit). Verified: 20-second soak, no crash,
kernel call counts climbing into the hundreds of millions, and
`\Device\Harddisk0\partition1\` -- the exact check the boot state machine
had been stuck on all session -- now opens with `status=0x00000000`. A
separate, narrower bug remains (a follow-on file open with an empty path
string fails with `STATUS_OBJECT_PATH_NOT_FOUND`); `StartScreen_SetState`
itself wasn't yet observed firing in a short gdb session, worth a longer
dedicated pass next time. See `RE_NOTES_xboxrecomp_test.md`'s part
nineteen.) **Twenty-fifth pass (part twenty): found and translated the
actual missing boot-state driver, `sub_000AF7B0` (cStartScreenSingle
vtable slot 5, the per-frame Tick that calls `StartScreen_SetState`) --
completely absent from the codebase, same "only reachable via a runtime
vtable slot" gap class as every other gap this session.** Read the real
vtable bytes off the XBE to confirm slot 5's address, ground-truthed the
full ~2834-byte function plus its 30-entry jump table via objdump, and
hand-translated it whole (all ~22 distinct states, the dispatch prologue
including one previously-missed sub-cluster, the FPU fade-timer clamp
reimplemented with plain float comparisons). Caught and fixed two real
bugs during self-review: a shared-epilogue label reached from two paths
with different numbers of registers pushed (would have corrupted the
stack), and two inverted je/jne translations on the same test-mask idiom
in different states. Registered in the dispatch table (10378->10379).
Verified live: the function IS reached with the correct initial state and
correctly transitions state 0->1, returning cleanly with no stack
imbalance -- a real, working translation of previously-nonexistent code.
Traced why it's only called once: the real caller is
`Application_RunMainLoop` (not `Application_StateMachineTick`, a red
herring that only handles one-time top-level object creation; not
`Application_TickFrame`, a separate timer-driven mechanism independently
confirmed via direct printf counter to be ticking correctly and
continuously -- an earlier gdb breakpoint-based "only 2 hits" count for
both was itself a red herring, contradicted by the authoritative counter;
gdb's breakpoint continue-counting is unreliable on this codebase's
heavily multi-threaded runtime). `Application_RunMainLoop` has a real,
correct internal loop gating cStartScreenSingle's Tick call, controlled by
a shutdown flag on the Application object's own `+0x24` -- ruled out two
hypotheses (a red-herring global always-1 flag, and a hypothetical
register-corruption bug in the wait helper it calls, both live-probed and
excluded) and narrowed the flag's flip to a precisely bounded ~6-call
stretch between the input-poll/tick inner loop and a rendering-detail-level
call cluster, ready for direct instrumentation next session. All debug
probes removed (verified via grep); clean rebuild, 30-second soak, no
crash, sustained kernel activity throughout. See
`RE_NOTES_xboxrecomp_test.md`'s part twenty.) **Twenty-sixth pass (part
twenty-one): found and fixed the real hang, then reached actual
`SceneRenderer_RenderFrame` execution for the first time in this project's
history.** The "only called once" mystery from the previous part wasn't a
shutdown-flag/lifecycle issue at all -- attached gdb to the live, genuinely
hung process (60+ seconds, zero progress, confirmed via active PC sampling
that it was spinning, not blocked) and found the main thread deep inside
real rendering code for the first time ever traced: a GPU ring/frame-throttle
wait (`sub_001688D0`, comparing a producer count against a consumer count
at fixed offsets on the same GPU channel-context pointer, `0x1776C0`, that
`xboxrecomp`'s existing PFIFO pump thread already services for two other
wait channels -- its own comment explicitly anticipated needing a third).
Added the missing fourth sync channel to `xbox_pfifo_pump_thread` in
`xbox_memory_layout.c`, same style as the existing two. Verified live: the
render call now completes and the main loop genuinely cycles multiple
frames in a row. Immediately hit a new, different blocker at the very next
real-work step: a genuine heap exhaustion (~53 MB single allocation against
a 112 MB budget with 66 MB already used) followed by an apparent
null-deref segfault. Tried bumping the heap budget 128->256 MB (already
documented as "safe to raise") -- made things *worse* (a new, earlier hang
appeared) and was reverted; growing the budget isn't safe in isolation.
Real next step is narrowly scoped: find what computes the ~53 MB request
and whether it's genuine or a sizing bug. All debug instrumentation
removed; clean, reproducible rebuild -- boots, ticks the state machine,
renders several real frames, then hits the heap crash at a consistent
point every run. See `RE_NOTES_xboxrecomp_test.md`'s part twenty-one.)
**Twenty-seventh pass (part twenty-two): traced the ~53MB allocation to
`D3D8::D3DDevice_CreateVertexShader` (`sub_0016A290`), found it already had
prior-session history (this exact size was already reached and already
succeeded once, before a "clean exit" run), then empirically tested
raising `XBOX_TOTAL_RAM` beyond 128MB and found it's not a safe, simple
fix.** Tested 129/140/160/256MB systematically: each produces a
*different*, unrelated crash or hang (a clean-crash-handler report at
129MB still short of budget; the allocation succeeding but crashing
moments later at an unmapped address `0xFED10000` at 140MB; an entirely
different, much earlier hang at both 160MB and 256MB) -- the signature of
several latent, memory-layout-dependent bugs, not one bug getting worse
with size. Reverted to the well-understood 128MB baseline. All three
failure modes documented precisely for a future session to chase
individually (the 140MB case -- clean allocation success immediately
followed by a crash at a fixed unrelated address -- is flagged as the most
tractable starting point, since it isolates a second bug from the
allocation-budget question entirely). See `RE_NOTES_xboxrecomp_test.md`'s
part twenty-two.)
Earlier: decoded the `.mpc` **audio** stream -- EA-XA ADPCM 48kHz stereo in SCDl blocks, framing taken from the game's own chunk dispatcher; Python and C++ decoders verified bit-identical -- and built the port's audio subsystem (waveOut streaming output + EA-XA decoder), so the boot videos now play **with sound**. See INDEX 145. Earlier: traced the real BOOT SEQUENCE for a faithful port: `cStartScreenSingle` boot state showing 0xba7 'Checking hard disk' / 0xba8 'Autoloading from hard disk' via `StartScreen_SetState`/`RenderStatusText` with code-traced frame timings (state 0=15f, state 8=120f), menu.ffn font, fades, then splash->EA logo->intro->title. 7 renames. New file `RE_NOTES_boot_sequence_and_startscreen.md`; port `boot_flow.cpp` reproduces it faithfully. Earlier: gap-list closure: `.mxf` model directory level decoded (`classify_mxf.py`, verified on all model kinds), `.xbd` header/loader traced (texture-index remap confirmed), `.afl` bone mapping resolved as index-based, audio confirmed standard EA `BNKl` v5 (vgmstream-supported), `.xsh` 0x7b closed (32bpp+1mip+terminator, exact), `.cml` cells closed (serialized tool object graph). One well-scoped task remains for the port's M4 milestone: geometry interiors. New file `RE_NOTES_xbd_model_format.md`. Earlier same session: **Tier-3 sweep + full port-viability audit + PC-port plan.** Closed the `.inp` playback loop end to end (`LessonMan_AdvanceInpPlaybackFrame` — the long-open position-counter writer — and `LessonMan_InjectRecordedInputFrame`: the tutorial demo replays recorded controller input through the real input pipeline), named the race-phase vtable functions, ran the full deliverables check (every format decoder re-verified against real data, `.afl` 119,498/119,498), and wrote **`RE_NOTES_PC_PORT_PLAN.md`** — the complete audit + gap list (headline: `.xbd` mesh format is the one real remaining decode, gating visuals but not gameplay milestones) + the 7-milestone port process plan. 6 renames. Earlier same session: **TIER 2 COMPLETE**, immediately after the Tier-1 resolutions: (1) the `.afl` character-animation format DECODED AND FULLY VERIFIED — extracted all 446 files from `anm.big` (count exactly matches the runtime's 446 pending-load flags), cross-referenced the runtime readers incl. raw disassembly of the curve-sample path, and verified **119,498 of 119,498 curve streams parse cleanly** with the documented spec: 12-byte header + 36-byte ID/companion entry table + u32 curve-offset table + compressed piecewise curve streams (constant/linear/quadratic/cubic polynomial segments in 3-byte floats with implicit 0x80 low mantissa byte, plus u8/u16-quantized keyframe modes; raw-keyframe mode 4 defined but unused in shipped data, like DXT3). Deliverable `scripts\classify_afl.py`; renamed `AnimCurve_EvaluateSegment` + `AnimEntry_SampleRotationCurves`. (2) the remaining `.cml` record types decoded statistically across all 12 track files: `Moment` = two back-to-back Location-shaped 404-byte keyframes, `Transition` = same pair + a halving blend-duration float, staging records = 112-byte directory/linkage nodes, gate/demo records = composites of 44-byte named cells; also refined `Location`'s rotation block to 4 floats (the 4th plausibly FOV). See `RE_NOTES_character_animation_system.md` / `RE_NOTES_camera_system.md`. Earlier same session: **BOTH REMAINING TIER-1 BLOCKERS RESOLVED STATICALLY**, per an explicit "deep research and resolve them now" directive — both had been declared "needs dynamic analysis." (1) The generic per-frame Update dispatcher: prior searches hunted vtable slot 7 (`+0x1c`) callers; the real per-frame tick uses slot 1 via `NodeRegistry_TickAllOfType` (was `FUN_000aa830`), driven by a master type-ordered loop (`InGameState_TickSubsystemsByTypeOrder` — correcting the old `InGameState_ApplyHudElementVisibility` misname), plus render sibling + pending-node integrator + 3 type-order data tables. Resolves who ticks LessonMan/SnowFallMan/PREAI/PostAI and every "orphaned Update method" project-wide. (2) The `rider+0x5720` score-writer: the field is a PHANTOM — bounding an unbounded race-results function (`RaceOutcome_EvaluateAndBeginPostRace`) proved via a 3-field-pair uniform `+0x10` alignment that the lone `0x5720` read is `rider+0x5710` (the already-solved live trick score) through the RiderEvent component's shifted rebase frame. No second field, no missing writer. Side finds: per-track personal-best recorders (`Team_RecordBestRaceTimeIfBetter`/`BestShowoffScoreIfBetter`), the standings comparator (`Race_ComputeRiderStandingsMetric`), the per-viewport leader pick, and `rider+0x140`=placement / `rider+0x448`=race-time confirmations. 10 function renames + 3 data renames. Earlier same session: closed the AI-steering Tier-1 gap in `RE_NOTES_ai_path_system.md` — whether AI steering during normal racing has a confirmed mechanism. Read `Rider_UpdateTrackPathPosition` in full for the first time and found its unread second half calls `Rider_SelectBestAIPathZone` (was `FUN_00032420`) — a probability-gated dynamic lane/path-choice decision running every frame for every rider, unconditionally. This is NOT the same mechanism as the previously-found `Rider_ComputeAISteering`/`Rider_FollowAIPath` pair (which only runs during end-race fade) — they're two separate, both-real AI behaviors. Between them, general-racing AI navigation is now fully accounted for. 5 renames. Also closed the last remaining function in `RE_NOTES_rider_event_system.md`'s `RiderEvent_SetState`-caller cluster (`Rider_CheckAttachedComponentOrientationLimit`). Earlier same session: stale-note correction: `RE_NOTES_rider_event_system.md`'s "still open" section claimed ~30 unread `RiderEvent` case handlers, but checking each live (per "check before declaring fresh") found every one already named from an earlier 2026-07-20 session — corrected the file. The one real leftover sub-item ("`RiderEvent_SetState`'s other 8 callers") was also mostly stale, but found and named 5 genuinely unnamed callers: `Rider_FinalizeGroundLandingState`, `Rider_HandleGroundModeEntry`, `Rider_CheckLandingRecoveryState`, `RiderEvent_SetStateThunk`, `Rider_ResolveLandingOutcome`. 5 renames. Earlier same session: found the consumer of `rider_component+0x2cc` via a *targeted* `/search_bytes` for the raw displacement, specifically looking for clustering (4 hits in one small function) rather than trusting isolated hits — a technique that correctly avoided this project's documented false-positive trap for small displacements. Landed on `Rider_ComputeSurfaceCompressionResponse` (was `FUN_00026da0`, sole caller `Rider_PhysicsMode2_GroundContact`), which **revises the hypothesis again**: `+0x2cc`/`+0x2d0` are a pair of continuous float compression thresholds driving a suspension-curve physics response, not a discrete surface-material id/enum as suspected in the immediately preceding pass. Found 2 more genuine readers via the same search. 1 rename. This substantially closes `RE_NOTES_terrain_collision.md`'s longest-standing open question: the real answer to "what does surface material affect" is "board compression/give," not "friction category." Earlier same session: re-prioritized to core gameplay/port-viability per explicit user request and did the initial investigation identifying `rider_component+0x2cc`'s write site + correcting the `.ltg` format assumption (see that file for the full trail). Earlier still: opened `OptionsMenu` — the multi-tab settings screen. Only 2 functions had ever been named, no dedicated file. Traced the existing `OptionsMenu_CacheDisplaySettingsFromWidgets`'s caller to find the tab-switch master handler (`OptionsMenu_HandleTabSwitchEvent`) plus 2 sibling tab-cache functions (Audio, Controls), and found `OptionsMenu_BuildDisplaySettingsWidgets` is called directly from `UI_BuildPauseMenu` — confirming the Pause Menu embeds this settings screen, which also explains an odd loose end from the `BoardSelectScreen` thread. 4 renames (6 total with the 2 pre-existing). New file `RE_NOTES_options_menu.md`. Earlier same session: `ProfileEditor` — SSX Tricky's Create/Edit Rider Profile screen's per-tab widget builder. Picked up the open lead from the `BoardSelectScreen` sibling-vtable investigation, bounded the remaining switch cases and the dispatcher's own entry point, and identified — via unambiguous allocation tags — a 5-tab widget factory: Outfit/Board/Profile/TrickBook/Username. Renamed the dispatcher (`ProfileEditor_BuildTabWidget`), the shared base-widget constructor, all 5 tab constructors, and 2 destructors. 9 renames. New file `RE_NOTES_profile_editor_screen.md`. This fully closes the sibling-vtable thread: 2 of ProfileEditor's 5 tabs (Profile, TrickBook) were the originally-mysterious vtables. Earlier same session: opened `BoardSelectScreen` — the character/board/team select screen. Only 3 functions had ever been named (a byproduct of an earlier unrelated investigation), no dedicated file. Read the class's own 14-slot vtable directly via `/read_bytes`, confirmed 2 near-identical sibling vtables immediately adjacent are a completely different, unrelated widget class (not more instances of this one — checked every slot's `xrefs_to` individually rather than assuming), and named 5 more methods. The standout: `BoardSelectScreen_ConfirmSelectionAndEnterLesson` sets `GameMode_Current=6` unconditionally — the exact value `LessonMan` gates on — directly tying this screen to the tutorial system investigated earlier this session: the character picked here determines which character's `.inp` files `LessonMan` later loads. 5 renames. New file `RE_NOTES_boardselect_screen.md`. Earlier same session: closed out the entire `.inp`/`LessonMan` tutorial-playback investigation end to end — decoded the `.inp` file format (52-byte header + 1042×44-byte records), bounded and named all 11 of `LessonMan`'s step-machine steps, and found the actual per-frame consumer (`LessonMan_DrawRecordedButtonPrompt`, confirming the file's varying fields are recorded controller button-bitmasks — the real "watch and repeat" tutorial mechanism). 19 renames across that whole thread — see `RE_NOTES_tutorial_system.md` for the full multi-part trail. Earlier still: decoded `.ffn` and `.xsh` texture/font formats (see `RE_NOTES_ffn_format_decoded.md`/`RE_NOTES_xsh_format_decoded.md`) and mapped `FEStateMCOverlay`'s save/load vtable (see `RE_NOTES_player_snapshot_system.md`) — full narrative history in those files, not repeated here.)
**Update this file after every session/prompt that makes decomp progress** —
new renames, a newly-mapped system, or a newly-closed (or newly-opened)
investigation thread. Keep it short: metrics + two checklists. Full narrative
detail always lives in the relevant `RE_NOTES_*.md` file and project memory —
this file is a dashboard, not a replacement for those.

## Headline metrics

- **1,331** total renames applied (`ssx_auto_rename.py`, now in `scripts\`) — 1,317
  functions + 14 data labels, zero duplicate addresses/names (integrity-checked
  every session). **1,000-rename milestone reached 2026-07-21. Full script/live
  parity confirmed 2026-07-21** (mechanical diff of every rename against live
  Ghidra: 0 missing, 0 mismatched, 0 unexplained extras). **Full-coverage
  function-by-function audit completed 2026-07-21**: all 1,217 functions
  individually re-checked against fresh decompiles in 31 batches — 0 issues
  found. **1 more rename immediately after** (`Score_ResolveTierAndColor`),
  from checking a flagged low-effort follow-up on the `rider+0x5720`
  score-writer mystery (see "open" section) — result was a negative/ruling-out
  finding plus a genuine new function identification.
- **≈1,566 of ≈5,443** functions in the binary (**≈28.77%**) — incremented from
  the last live count (+66 named this session, +20 from newly
  `/create_function`-bounded functions: 12 in the `.inp`/`LessonMan`
  step-machine thread, 1 in the `BoardSelectScreen` thread (`0x887c0`), and
  5 in the `ProfileEditor` thread (`0x89644`/`0x896e0`/`0x89692`/`0x895e4`/
  `0x895d4` — the last being the dispatcher's own entry point, which
  absorbed/re-merged the 4 case functions Ghidra had auto-split); the 4
  `OptionsMenu` renames this round were all already-bounded functions, no
  new boundaries needed. Not independently re-counted live this pass. This
  undersells true
  coverage — most of the unnamed ~85% is generic engine/math/D3D plumbing that
  doesn't need individual attention; the named ~15% covers essentially every
  architecturally significant system (see below). Note: ~61 low-level D3D8
  API wrapper functions are also named but were auto-identified by Ghidra
  (function-ID matching), not manual work from any session.
- **39** `RE_NOTES_*.md` files (31 subsystem writeups + INDEX + this dashboard).
- **The score-writer mystery is RESOLVED, formula included** (see
  `RE_NOTES_trickcombo_scoring_resolved.md`): `rider+0x5710` is written by
  `TrickCombo_ScoreRailCompletion`/`ScoreAirCompletion` as
  `*(int*)(rider+0x5630+0xe0) += bonus` — a locally-rebased offset that's why
  byte-level searches for the literal `+0x5710` displacement never found it.
  The full formula is now named too: base points
  (`TrickCombo_ComputeTrickPoints`), combo-streak bonus
  (`TrickCombo_GetStreakBonusValue`), and a genuine repeat-trick anti-farming
  penalty (`TrickCombo_CountRecentRepeats`, divides score by repeatCount+1).
- GhidraMCP plugin at **round 6** (custom-patched endpoints beyond stock:
  disassemble/create/delete function, byte search, symbol status, function
  containment, etc. — see `reference_ghidra_mcp_connection.md` in memory).

## What we have — fully or substantially mapped

- **The `.afl` character-animation format — DECODED AND FULLY VERIFIED**
  (2026-07-22, `RE_NOTES_character_animation_system.md`): all 446 real
  files parse with 0 failures across 119,498 curve streams. Compressed
  piecewise curves (polynomial segments in 3-byte floats + quantized
  keyframe modes), 36-byte ID/companion entry table, u32 curve-offset
  table. Deliverable `scripts\classify_afl.py`. 2 renames.
- **The remaining `.cml` record types — decoded statistically**
  (2026-07-22, `RE_NOTES_camera_system.md`): `Moment` = paired Location
  keyframes, `Transition` = pair + blend duration, staging = directory
  nodes, gate/demo = 44-byte named-cell composites; `Location` rotation
  refined to 4 floats. Camera keyframe data is now covered for all
  record types.
- **Terrain surface-material physics — substantially resolved**
  (2026-07-22, update to `RE_NOTES_terrain_collision.md`): traced the
  rider's cached "current surface" value (`rider_component+0x2cc`) from
  its write site (`Terrain_QuerySurfaceContact`/`Rider_
  ResolveTerrainContactPhysics`) to its consumer
  (`Rider_ComputeSurfaceCompressionResponse`, found via a targeted
  `/search_bytes` clustering search). The real answer to "what does
  surface material affect": a continuous compression/give range driving a
  suspension-curve physics response, not a discrete snow/ice/rail
  category as originally assumed — corrected across 2 hypothesis
  revisions in the same investigation. 1 rename.
- **`OptionsMenu` — the multi-tab settings screen** (2026-07-22,
  `RE_NOTES_options_menu.md`, new file): found the tab-switch master
  handler (`OptionsMenu_HandleTabSwitchEvent`) and 2 sibling tab-cache
  functions (Audio, Controls) alongside the pre-existing Display tab
  functions. Found `OptionsMenu_BuildDisplaySettingsWidgets` is called
  directly from `UI_BuildPauseMenu` — the Pause Menu embeds this settings
  screen, explaining an odd `UI_BuildPauseMenuText` tail-call noticed in
  the `BoardSelectScreen` work. 4 renames (6 total with 2 pre-existing).
- **`ProfileEditor` — the Create/Edit Rider Profile screen's per-tab widget
  builder** (2026-07-22, `RE_NOTES_profile_editor_screen.md`, new file,
  found as a byproduct of the `BoardSelectScreen` sibling-vtable
  investigation): a 5-way tab dispatcher (`ProfileEditor_BuildTabWidget`)
  building Outfit/Board/Profile/TrickBook/Username tab widgets, identified
  unambiguously via allocation tags. All 5 tab constructors + shared base +
  2 destructors named (9 renames). Closes the loop on the 2 originally-
  mysterious "sibling vtables" near `BoardSelectScreen` — they're the
  Profile and TrickBook tabs.
- **`BoardSelectScreen` — the character/board/team select screen** (2026-07-22,
  `RE_NOTES_boardselect_screen.md`, new file): read the class's own 14-slot
  vtable via `/read_bytes`, confirmed 2 near-identical sibling vtables found
  adjacent are a different, unrelated class (checked every slot's `xrefs_to`
  individually rather than assuming), and named 5 more methods (8 total).
  Standout finding: `BoardSelectScreen_ConfirmSelectionAndEnterLesson` sets
  `GameMode_Current=6` — the exact tutorial-mode trigger — directly
  connecting this screen's character choice to which `.inp` files `LessonMan`
  later loads. 5 renames.
- **The `.inp` tutorial-lesson playback file format — substantially decoded**
  (2026-07-22, folded into `RE_NOTES_tutorial_system.md`): `Game Data\data\
  tutorial\<char>.big` archives each hold 30 files (`psym01.inp`-`psym30.inp`
  etc.), every one exactly 316824 bytes. Traced the format string
  `"|data\tutorial\%s%02d.inp"`'s 3 xrefs forward through completely
  unbounded code, confirmed the whole cluster is `LessonMan`'s own step
  state machine (exact field-offset match on the character-roster table
  vs. `LessonMan_Construct`), mapped all 11 steps' enter/exit/tick addresses
  via `/read_bytes`, bounded+named steps 2-5 (the menu → loader path) ending
  in the actual file loader (`LessonMan_Step5Enter_LoadLessonInpFile`,
  byte-exact size match to real files and to the `"LessonBuffer"`
  allocation). Decoded the file's own structure against real bytes: 52-byte
  header + 1042×44-byte records (a sparse scripted-event/cue timeline, not
  continuous motion capture) + zero padding, exact byte accounting verified.
  9 renames, **+7 more** bounding/naming steps 6-10, **+2 more** closing step
  11 (all 11 steps now done) and identifying the shared reset utility
  (`ReplayManager_ClearRecordedFrames`) steps 7/9 call, **+1 more** finding
  the actual per-frame consumer (`LessonMan_DrawRecordedButtonPrompt`) via
  `LessonMan_RenderOverlay`'s special-case draw call — confirms the `.inp`
  file's per-record varying fields are recorded controller button-bitmasks,
  the real mechanism behind the "watch and repeat" tutorial lessons.
  19 renames total for this thread. Open: what increments the position
  counter each frame (narrower now, not located), exact bit-to-button
  mapping.
- **The `.ffn` bitmap font format — DECODED** (2026-07-22,
  `RE_NOTES_ffn_font_format_decoded.md`, new file): a fresh-direction pick
  immediately after the `.xsh` decode (fonts had been flagged as
  unexplored). Magic `"FNTF"`. Unlike `.xsh`, had a head start — this
  project already had `Font_LoadAndParse`/`Font_ParseGlyphTable` named
  from an earlier session, so this was a direct cross-reference against
  their already-decompiled bodies (a header + a glyph-record table +
  a packed 4-bit-per-pixel alpha-mask glyph bitmap, unpacked to
  `A4R4G4B4` textures via the classic anti-aliased-text-as-alpha-mask
  technique). **Byte-perfect verified against all 3 real extracted `.ffn`
  files** — every computed bitmap size exactly matches the literal
  remaining file bytes with zero residual. Named 1 new function,
  `Font_UnpackGlyphBitmapTexture` (was `FUN_000c21a0`, the bitmap-section
  parser, uploads via the SAME `GfxContext` vtable slot `.xsh` textures
  use). Deliverable: `scripts\classify_ffn.py`. Open: per-glyph sub-field
  geometry (X/Y/width/kerning?), several unknown header bytes, and the
  untested `<200` "compact" glyph-record mode (no real sample uses it).

- **The `.xsh` texture-sheet container format — DECODED** (2026-07-22,
  `RE_NOTES_xsh_format_decoded.md`, new file): a fresh-direction pick after
  closing out the AggressionManager/replay-load thread. `.xsh` files had
  only ever been enumerated by filename before this; the actual container
  format (magic `"SHPX"`) was never decoded. Fully mapped: 12-byte header +
  4-byte build tag + an N-entry `{name, offset}` table + an 8-byte EA
  tool-signature string (`"Buy ERTS"`/`"EASports"` — a nod to EA's old
  `ERTS` stock ticker), then a 16-byte per-texture sub-header (format code,
  width, height, mip/flags) before pixel data. **Cross-confirmed against
  live code**: `GfxContext_ParseAndQueueTexture`'s own field reads (format
  byte -> D3DFORMAT-index switch, width/height offsets, mip/flags shift)
  match the file layout exactly, the same "format ↔ runtime struct"
  correlation done for `.ltg`/`TerrainGrid`. Deliverable:
  `scripts\classify_xsh.py`. Verified against 3 real files (1/3/16 texture
  entries) — clean, consistent results (`crowd.xsh`'s 16 crowd-sprite
  frames all match byte-for-byte). **Immediate follow-up, same session**:
  traced the format code further through `GfxContext_
  QueueTextureFromRawData` and PROVED the bits-per-pixel table exactly
  (was only measured before) — `0x60`=4bpp, `0x61`/`0x62`=8bpp,
  `0x6d`/`0x78`/`0x7e`=16bpp, `0x7d`=32bpp (the latter 4 swizzled via
  `XGRAPHC::XGSwizzleRect`, the former 3 copied directly). **Second
  follow-up, same session**: traced the internal format id further into
  `GfxContext_RegisterTextureTable` — it lands in bits[8:15] of a genuine
  Xbox native `D3DTexture` format DWORD, confirming it's a real
  `D3DFORMAT`-family value, not an engine-only id. **Self-correction**: my
  first pass had asserted the 3 direct-copy formats were "paletted... not
  DXT1/DXT5" — that reasoning was wrong (DXT1/DXT3/DXT5 block compression
  produces numerically identical bpp to 4/8-bit palette formats, so bpp
  alone can't distinguish them; if anything, the "no swizzle" behavior
  now reads *more* consistent with DXT than with raw palette data).
  Retracted, documented as genuinely undetermined. **Third follow-up, same
  session — settled it**: decoded `map1`'s data directly as DXT1 blocks
  and `map4`'s as DXT3/DXT5 blocks. Both produce structurally coherent
  results — DXT1's textbook "near-black color0/color1 pair + all-1s
  indices" solid-fill signature on 34% of blocks plus plausible varied
  RGB565 colors on the rest; DXT5's unique alpha-endpoint+interpolated-
  index structure (which DXT3 lacks entirely). **Strong structural
  evidence** (not a rendered-image proof): format `0x60`=DXT1,
  `0x61`=DXT5. **Fourth follow-up, same session**: censused every one of
  112 extracted `.xsh` files (515 entries) for format-byte usage. Found
  format `0x60` is used in only 2 places total — both now checked, a
  second file (`particle.xsh`'s "exlm" flash-particle sprite) reproduces
  the exact same DXT1 signature independently, exhausting its whole
  real-world usage. Found format `0x62` (the DXT3 guess) is used **zero**
  times anywhere in the shipped game — untestable from real data. Found a
  previously-unseen format byte `0x7b` (2 uses, both a "spt1"/spotlight
  texture) that hits the switch's default case — but its measured data
  (~10bpp) genuinely **contradicts** the 16bpp its default-case treatment
  would predict, a real unresolved discrepancy (likely a different,
  untraced lightmap-specific loader, not a math error — left honestly
  unmapped in `classify_xsh.py` rather than forced to fit). Open: the
  real lightmap-texture loader, `0x62`/DXT3 (untestable), 2 unknown
  sub-header byte ranges, the build tag's purpose, and magic validation
  at load time. Data-format finding, no new function renames.
- **The full program entry-point-to-shutdown chain, including the real `WinMain`-
  equivalent** (2026-07-21, `RE_NOTES_application_boot.md`): `XAPILIB::mainXapiStartup`
  (a real Xbox-SDK symbol, found by bisecting a boundary an earlier session had
  explicitly declined to force) -> `Application_ConstructAndInitInput` ->
  `Application_RunAndShutdown` (corrected name/ownership, was misnamed
  `InputManager_InitOrReset`) -> `Application_RunMainLoop` (corrected
  characterization, was mischaracterized as a bounded loading pump but is
  actually the master game loop for the whole session) -> shutdown ->
  `XapiBootToDash`. Also corrected Application's own vtable slot numbering and
  named the previously-undocumented `Application_Purge` (tag-confirmed). **Also
  fully reconciled** the sync main loop with the async timer-driven
  `Application_TickFrame` chain: `Application_RunMainLoop` blocks on
  `XBoxExecutionMan_WaitForFrameEvent` (`WaitForSingleObject`), woken each tick
  by `Application_TickFrame`'s `XBoxExecutionMan_SignalFrameEvent` (`SetEvent`)
  -- a classic OS-timer-paced game loop, not two unrelated mechanisms.
- **All 11 known `.inf` audio config files now have a confirmed loader**
  (`RE_NOTES_audio_system.md`, 2026-07-21): `AudioSystem_LoadRaceConfigs` loads
  audio.inf/banks.inf/crowd.inf/chant.inf/musicmap.inf/nascript.inf directly,
  `AudioSystem_LoadSnowConfig` loads snow.inf, tying together with the already-named
  `AudioSystem_Construct` (jukebox.inf), `TrackIntroMusic_SelectAndPlay`
  (intromus.inf), `MusicManager_Construct` (music.inf), and `SpeechLineSet_Construct`
  (speech.inf). `AudioSystem_LoadRaceConfigs` is called from `InGameState_LoadLevel`;
  its FrontEnd/menu counterpart `AudioSystem_ReloadConfigs` is called from
  `FEInit_Boot`. Corrected an earlier same-session dismissal of
  `AudioSystem_LoadRaceConfigs` as "tangential." A genuinely complete subsystem map.
- **`trickdef.dat` + on-screen trick-instruction prompt** (`RE_NOTES_tutorial_system.md`,
  2026-07-21): a 30-column x 12-row table of trick definitions (directional-
  input + button trick mapping), plus the full render chain that displays "do
  this stick+button combo" prompts during tutorial lessons. Well-anchored via
  a direct call from the already-named `LessonMan_Construct`. **The trick-prompt
  widget's own small vtable is now fully mapped, all 6 slots named** — this
  thread is exhausted end to end.
- **`TitleIntroSequence` boot cinematic controller** (`RE_NOTES_title_intro_sequence.md`,
  2026-07-21): a 5-slot vtable class (tagged `"f3stttl"`) that queues and ticks the
  EA logo (`eabig.mpc`) chained into the SSX Tricky opening cinematic
  (`ssxintro.mpc`), gated on the same `DAT_001df3f4` global
  `Application_StateMachineTick` reads as a boot-sequence state. **Update**: the
  menu-screen preview-video trigger site WAS found —
  `FrontEndVideo_SelectPreviewClip`, the central dispatcher for every menu
  preview video (evolve/tricks/tracks/charactr/music/audio.mpc, plus
  per-character `cv_<name>.mpc` built at runtime, plus `sled2pr.mpc`/
  `nbapromo.mpc`). The owning class is the in-game "DVD Extras" bonus-content
  menu (celebrity DJ voice credits + jukebox), confirmed via
  `DVDExtrasMenu_ScalarDeletingDestructor`. The `.mpc`/`.xss` container
  format itself and the per-track video trigger weren't decoded — a clean
  next thread. **Update — thread fully exhausted**: decoded both container
  formats directly from raw file bytes. `.mpc` confirmed as a real MPEG-1/2
  video stream (Gimex middleware) matching the already-named
  `VideoPlayer_FindNextChunkByMagic`'s `"MPCh"` check. `.xss` confirmed a
  different, non-MPEG (likely audio) `"XSSF"`-tagged format with **zero**
  code references anywhere in the binary (multiple independent search
  techniques) — a genuine, confirmed dead end, not merely unchecked. Also
  found the per-track intro-music selector (`TrackIntroMusic_SelectAndPlay`,
  reads an `.inf` config file) and identified the owning "DVD Extras"
  jukebox menu class (`DVDExtrasMenu_ScalarDeletingDestructor` + confirmed
  a consistent `this+0x28` screen-mode field used across its vtable).
- **Career Mode challenge/objective record system** (`RE_NOTES_challenge_system.md`,
  2026-07-21, found as a byproduct of the AI-steering byte search): a `0x4c`-byte-stride
  "challenge slot" record array (4 gating flags, score/count value, name string) driven
  through a shared polymorphic vtable, plus the localized description-text formatter
  (`Challenge_FormatObjectiveDescription`, ~40 challenge-type cases) and per-slot
  refresh (`Challenge_RefreshEntryState`). **Follow-up (same day)**: bounded the
  calling UI/menu code via `/create_function` (it was a missing-function-boundary gap,
  not raw data) — found `ChallengeMenu_HandleScreenEvent`, a **vtable-dispatched**
  `Widget`-subclass virtual method (found only via a data xref, never a direct call)
  driving the whole screen, plus screen-init and description-refresh helpers.
  **Correction (2026-07-22): the "sibling wrapper call sites" gap this line
  used to flag as open was actually already closed** in the same file's own
  "Closed out the remaining wrapper cluster" section — all 5 remaining gaps
  were bisected and named (`ChallengeMenu_RefreshDescriptionWithTitleAlt`/
  `Alt2`/`Alt3`/`Alt4`/`WithSuffix`), 13 renames total for the whole thread,
  "no more open gaps in this specific area." This progress-tracker summary
  simply hadn't been updated after that later pass — fixed on discovery
  while picking a next thread to work on, no new RE work was needed. Most
  individual localization string-ID constants remain undecoded (data
  analysis, not code RE — low value).
- **AI racing-line path file format + query API + race-start assignment**
  (`RE_NOTES_ai_path_system.md`, 2026-07-20): the `.aip` file's load chain
  (`Level_LoadTrackAssets` -> `AIPath_LoadFromFile` ->
  `AIPathSet_Construct`/`EventPathSet_Construct`, content-confirmed via the
  `"AIPaths"`/`"EventPaths"` tags), the per-path binary parser
  (`AIPath_ParseFromBuffer`), the runtime query API (`AIPath_QueryZonesInRange`
  and siblings), and the race-start assignment site (`Race_ResetPlayerRoster`
  -> `Rider_AssignAIPath`, one path per starting-grid slot). Closes the missing
  constructor for the already-known `Rider_UpdateTrackEventTriggers` consumer,
  and significantly strengthens (does not fully prove) the old
  `Rider_UpdateTrackPathPosition` "plausible AI-steering candidate" flag --
  `Rider_AssignAIPath` resets the exact same distance-tracking cache fields
  that function maintains. **PARTIALLY RESOLVED (same session, immediate
  follow-up)**: found and named a CPU-rider steering triad --
  `Rider_ComputeAISteering` (boids-style local avoidance) <->
  `Rider_FollowAIPath` (path-following fallback, mutually recursive,
  **confirmed writer of `rider+0x3a0`**) -> `Rider_ApplyMotionUpdate`
  (generic terrain-aware position/orientation commit, also used by
  non-AI callers -- renamed from an initial AI-specific name after
  checking its full xref list). Confirms the write site for the
  steering-angle field, but the only confirmed *trigger* found is the
  end-race fade-out RiderEvent state, not general mid-race AI -- caught
  via the same xref-checking discipline before it was overclaimed as
  "the master per-tick AI brain." **Further resolved (same session)**:
  found an actual READER of `rider+0x3a0` (distinct from the writer above)
  in `Rider_TriggerOvertakeCommentary`, which reads it for ANY nearby
  rider pair via the new `Math_AngleToSinCos` helper to gate rivalry
  commentary on relative heading -- supports `this+0x3a0` being a
  general per-rider heading angle maintained for every rider (likely by
  `Rider_UpdateTrackPathPosition`, confirmed to run every frame for every
  rider), not an AI-only field. **FULLY RESOLVED (2026-07-22, port-
  priority pass)**: read `Rider_UpdateTrackPathPosition` in full for the
  first time (previously only its `this+0x3a0` write was known) and found
  its unread second half calls `Rider_SelectBestAIPathZone` (was
  `FUN_00032420`) — a probability-gated dynamic lane/path-choice decision
  (queries nearby alternate paths, scores them, reassigns the rider's
  tracked path) that runs every frame for every rider, unconditionally —
  **not** the same mechanism as the `Rider_ComputeAISteering`/
  `Rider_FollowAIPath` pair (confirmed end-race-fade-only). These are 2
  separate, both-real AI behaviors; between them, general-racing AI
  navigation is fully accounted for. **This closes the AI-steering Tier-1
  port-viability gap.** See `RE_NOTES_ai_path_system.md` for the full
  writeup and corrections.
- **Level-script system**: `Script_DispatchOpcode` (24 level-object types) +
  `ScriptVM_DispatchOpcode` (~27 outer-VM opcodes), fully documented.
- **`NodeRegistry`/`NodeBase`/`Node` class hierarchy**: confirmed
  `NodeBase -> Node -> {Camera, TrickTrigger, Boost, ...}`, the shared
  root-object system, its intrusive doubly-linked bucket lists, and the exact
  per-frame "Update" vtable-dispatch mechanism (proven via
  `NodeRegistry_UpdateAllOfType`, though never found wired to a genuine
  every-frame caller — see "open" below).
- **Sweep-and-prune broad-phase collision** + **`OverlapManager`** poll-based
  overlap bookkeeping, full pipeline sweep→toggle→poll.
- **Terrain collision** (core snowboarding mechanic): the `TerrainGrid`
  singleton (2D cell array), `Terrain_QuerySurfaceContact` (grid broad-phase →
  AABB → per-triangle SSE narrow-phase), `Terrain_SampleHeightAt`, and the
  rider-side contact-cell tracking. Grid-broad + triangle-narrow design.
  **The `.ltg` (level terrain geometry) file format is DECODED** — its header
  maps directly onto the runtime `TerrainGrid` struct (grid W×H, cell size,
  offset table); tool `scripts\classify_ltg.py`. Loaded by
  `Level_LoadTrackAssets`. Per-cell triangle encoding still open. See
  `RE_NOTES_terrain_collision.md`.
- **Game-mode/race lifecycle**: `GameMode_Current` states (Race/Showoff/
  Freeride), the full `RaceState_SetState` 11-state race+tutorial flow,
  `Application_StateMachineTick` boot chain, `InGameState_TickFrame`.
- **`Camera` class**: real vtable, shake-curve system
  (`Camera_EvaluateShakeCurves`/`Camera_ApplyShakeOffset`), audio panning,
  view-transform update, `CameraShakeMode`/`CameraAudioPanningMode` embedded
  mini-vtables.
- **`LightManager`** (new, tag `"LightMan"`): a full light-object pool —
  allocate/free/index (`LightManager_AllocateLightPool`/`FreeLightArray`/
  `GetLightByIndex`), slot claiming with a high-water mark, and ambient/point
  light submission (`LightManager_AddAmbientLight`/`AddPointLight`). Its
  base-class ctor publishes the instance as the global `DAT_001e98c8`
  singleton (corrected from an initial "separate device object" theory via
  raw-byte disassembly). Also confirmed `GfxContext`'s real internal class
  name (`XBoxGraphicsMan`).
- **`LensFX`** (new, tag `"LensFX"`): the lens-flare system, fully
  vtable-mapped — Xbox occlusion-query readback
  (`Render_ReadVisibilityTestResult`) drives flare-sprite intensity
  (`LensFX_UpdateCoreSpriteIntensity`/`UpdateFlareArrayIntensity`, fog-gated),
  plus blend-state/viewport setup methods. Shares vtable slots with the
  FX-trail system (common base). See `RE_NOTES_rendering_system.md`.
- **`.big` archive container format** (`c0fb` magic) + **EA RefPack**
  decompression — working tools (`refpack.py`, `extract_big.py`). Found
  `RaceMode`/`ShowoffMode`/`FreerideMode`/etc. as literal per-track script
  names inside `.xsf` files, closing the multi-session "where do level
  scripts live" question.
- **`.cml` camera-script format — COMPLETE end to end**: container/leaf
  record shapes, a 6-category per-region template (tool: `classify_cml.py`),
  keyframe field layout (position + rotation per `Location` record, via
  cross-record statistical diffing), the `VenueStaging` name-lookup →
  transition orchestrator → level-load state machines, **and now the actual
  runtime interpreter** (`VenueStaging_TickCameraScriptCommand`, a 43-opcode
  keyframe/command processor) that walks the format frame-by-frame and
  drives the active camera mode (`VenueStaging_SetActiveCameraMode`/
  `VenueStaging_Tick`). On-disc format → runtime execution, fully closed.
- **`Rider`'s own vtable**: all 11 slots named (destructor, world-space
  marker/nameplate draw, an instance-ID compare/get family tying into
  `NodeBase`, the master per-frame update dispatcher and its buffer-reset
  companion). A long-open thread from early in the project, now fully closed.
  **Extended cross-class**: confirmed the same shared slot family (13 of 16
  slots) is byte-identical across `TrickTrigger`/`Boost`/`Fence`/`Roller`/
  `Cracked`/`Timer`/`Debounce` — a genuine Node-level virtual-method
  footprint. Each class's own Update/Destruct named, including real per-frame
  logic in `Boost_Update` (overlap detection + boost application),
  `Fence_Update` (neighbor-segment timers), `Roller_Update` (obstacle
  motion), and the Cracked/Timer script-firing pair. Command-record column 4
  confirmed as the standard "attached script handle" field.
  **COMPLETE**: every one of the 24 node types in `Script_DispatchOpcode`'s
  opcode table (UVScroll/TexFlip/Movie/ZBoost/AnimObject family/Particle/
  cMeshAnim/both unknown-opcode types included) now has verified
  Update+Destruct — a full architectural mapping of the level-script
  object system's per-frame lifecycle, not a partial sweep.
- **`RiderEvent` lifecycle dispatch**: two ~20-case state-machine switches
  driving rider/board **animation** triggers (`BdrSeq_*`, confirmed via
  `New_BdrSeq`), connected to `Rider_UpdatePhysicsState` (the per-frame rider
  tick — corrected from the earlier mislabel "Rider_TeardownSubobjects") and
  the per-active-entity race-start notification chain. Includes a fully-mapped
  **race-finish sequence** (`RiderEvent_RaceFinishSequence`) that resolves
  `rider+0x5720` to a medal tier (`MedalTier_ResolveFromValue`/
  `MedalTier_LookupTrackThresholds`) — a concrete consumer of the score-field
  region, see "open" below.
- **`OverlayManager`**: a previously undocumented top-level HUD/menu overlay
  panel manager (19 distinct panels), called from the already-documented
  `InGameState_LoadLevel`. 7 of 19 panels identified by name via exact
  localization-string lookups: `SaveOverlay` (hosts the `SaveGame` state
  machine), `WorldCircuitNextRaceOverlay`/`WorldCircuitResultsOverlay`
  (tournament bracket), `NameEntryOverlay`, `UnlockNotificationOverlay`,
  `ReplayTitleOverlay`, `PauseHudDetailOverlay`.
- **`PlayerSnapShot`/`SaveGame` system**: the complete Xbox save-file format,
  all 17 states mapped — header, checksum, data records, object-list chunks,
  per-player profile/icon chunks, padding to a fixed ~510KB block, final
  checksum. 5-member tagged-chunk magic-number family confirmed
  (`0x11111111`-`0x11111115`).
- **In-race HUD**: `HUD_DrawRaceOverlay`/`HUD_DrawWorldSpaceMarkers`, the
  trick/combo score-popup system, glyph/text rendering primitives, all
  localized strings resolved via the fully-reverse-engineered `.loc` format.
- **Frontend menu system**: COMPLETE — 26 of ~30 originally-mapped
  screen-builder functions plus all 4 last-remaining "too complex" widget
  helpers (named at tag-confirmed confidence despite decompiler mangling).
  `Widget` base class fully mapped: destructor family, doubly-linked
  child-list container, and now its full real vtable (a property-cascade
  system propagating setters to named child widgets, a transition-animation
  state machine, per-frame layout).
- **Input pipeline**: raw `XInputGetState` through action-code resolution,
  `InputDevice`/`GamepadInputDevice` class hierarchy, `InputManager`'s 4-pad +
  8-frame input-history cache. **Real Xbox SDK types now applied throughout**
  the gamepad-open/poll chain (`XGetDevices`/`XInputOpen`/`XInputGetCapabilities`/
  `XInputGetState`/`XInputSetState` all have accurate signatures;
  `PXINPUT_STATE`/`PXINPUT_FEEDBACK`/`XINPUT_CAPABILITIES` applied to the real
  variables that hold them) -- `Input_PollDevice` and
  `GamepadInputDevice_Construct`/`InitVibrationCapabilities` decompile with
  genuine struct field access end to end, not raw offsets.
- **Results screen / track table**: `TrackTable` (12 tracks, 7 fields/row),
  `Race_ComputeRankings` and its full sub-helper cluster (rival comparison,
  team assignment, highlight records).
- **Ubertrick FX cluster**: rail-grind spark trails, LOD slot-claiming, full
  pipeline from `FX_TrailManager_Tick` down to quad-spawning primitives.
- **`SnowFallMan`/`FogMan`**: self-contained weather-effect systems (falling
  snow with 8 fixed emitter slots; volumetric cloud/fog-volumes). Own Update
  slots are no-ops, real logic runs through 2-3 other vtable slots each,
  per-frame caller not found. See `RE_NOTES_weather_effects.md`. **Every
  tagged object from `InGameState_LoadLevel`'s allocation list is now
  individually explored** (`SnowFallMan`/`LessonMan`/`PowerFX Particles`/
  `TerrainNode`/`VideoStreamMan`/`DebugMenu`/`SkyNode`/`ModelsNode`/
  `OverlayNode`/`FogMan`) -- see each system's own RE_NOTES file.
- **`AudioSystem`** (a.k.a. `"BXAudioSystem"`): singleton audio engine class,
  newly traced this session from its global handle (`DAT_001f82f4`) through
  its guarded singleton-init and real constructor. **Vtable fully mapped —
  15 of 15 real slots named**: constructor pair, destructor, a tag-confirmed
  sound-memory allocator + its slot-configure sibling, a slot-release
  function, two "play sound" variants (both vtable-dispatch-only, no static
  caller — the project's recurring pattern), a 3D positional-audio
  calculator, a per-frame channel-volume tick, stop-all/stop-by-tag
  functions, a pending-request ticker, a callback-context setter, and one
  verified no-op stub. Surfaced two internal channel arrays (64-slot live
  pool + dynamic-sound-buffer pool). **Then found `AudioSystem` is actually
  the leaf of a 9-level single-inheritance chain**: `AudioBankBase` (root,
  tag `"BankInstances"`) → `AudioBank` (tags `"mBank"`/`"mPath"`) →
  `MidiBankManager` (tag `"MIDIBANKS"`) → `AudioStreamManager` (tag
  `"StreamArray"`) → `MusicManager` (reads `data\config\music.inf`) →
  `SpeechManager` (tag `"mSpeechInstance"`) → `SoundGroupManager` →
  `AudioChannelMixer` → `AudioSystem`. Explains why several AudioSystem
  vtable slots are byte-identical to `AudioBankBase`'s raw slots (genuinely
  inherited unmodified). **Full 9-level vtable comparison also found
  AudioSystem's real vtable is 16 slots (not ~15)** — 2 missed slots named
  (`PlayLoopedCue`/`ResolvePriorityTier`), plus `AudioBank`'s own new slot
  (`AudioBank_RegisterSoundInstance`). 13 renames from the follow-up pass.
  **Then a public-API sweep** (xrefs to each level's singleton, not just its
  constructor) found real cross-system links: `AudioBank`'s cue system reads
  straight from `.big` archives (`AudioBank_ResolveCueEntry`/
  `DispatchCueToDevice`), a per-track adaptive-music loader
  (`MusicTrack_Construct`, tag `"PathfinderStream"` + full beat-grid config),
  and beat-synced voice-line triggering tied to `RiderEvent`
  (`SpeechManager_TriggerLineByEventCode`). 7 more renames. **Then finished
  the sweep**, uncovering 2 new satellite classes: `AmbientZone` (7 methods
  — random weighted sub-sound picks, a combined single/array destructor
  confirming a 400-byte-stride instance array, up to 15 sub-emitters each)
  and `SpeechLineSet` (tag-confirmed via a refcounted `"SpeechConfig"`
  buffer reading `data\config\speech.inf`, parsed by
  `SpeechManager_ParseSpeechConfig`). Also independently confirmed the
  **destructor chain mirrors the constructor chain** in reverse
  (`SoundGroupManager_Destruct` → `SpeechManager_Destruct` →
  `MusicManager_Destruct`). 16 more renames.
- **`Rendering system`**: new — the game's own rendering code, found by
  tracing callers of `D3DDevice_Present`/`Clear` (the ~61 low-level D3D8
  wrappers were already auto-labeled by Ghidra before this session). Found
  the true engine boot chain (`Application_InitPlatformAndDevice` →
  `Renderer_InitializeD3DDevice`, earlier than the already-documented
  `Application_InitSubsystems`), a complete content-confirmed disc-read-error
  fallback screen, a reusable 2D sprite-batch renderer, a loading-screen
  image blitter, and a generic RNG (found via the boot chain, turned out to
  be the same "random number" utility already seen in the audio system's
  `AmbientZone` code). 11 renames. **Then traced the mesh-drawing layer**
  (`D3DDevice_DrawIndexedVertices`'s remaining 5 callers):
  `BoardMesh_DrawAttachedPatches` (confirms `BoardMesh_LockBuffers`/
  `GetBufferAddr` existed from an earlier session) plus 4 `MeshRenderer_*`
  functions sharing a shader-cache context — one of which
  (`MeshRenderer_DrawMultiTexturedParts`) samples the previous frame's back
  buffer as a texture input, a screen-space effect. Found a 16-slot mesh
  draw-mode dispatch table (4 slots filled, dispatcher not found). 5 more
  renames. **Then found the actual per-frame scene pipeline**:
  `SceneRenderer_RenderAllPasses`/`RenderPassRange` (master frame
  orchestrators) driving `SceneView_RenderPass` (builds camera projection +
  viewport, walks a render-command list — the dispatcher for mesh handlers),
  the `Matrix_Build*Projection` pair, `Renderer_SetDefaultDeviceState`, and
  NV2A `D3D_InitMiniportAndFrameBuffers`. Fully mapped the 16-slot draw-mode
  table (6 handlers + 2 hash-bucket enqueue functions + shared stubs) and
  named `BoardMesh_BuildAndUploadGeometry`. 17 more renames (33 total this
  rendering thread). **Then cracked the top-level frame driver**
  (`SceneRenderer_RenderFrame` — reflection pass + split-screen + LOD),
  **characterized the `GfxContext` render-device class** (its vtable at
  `0x001a2b38`, view array, two matrix stacks, render-state command builder),
  read **`GfxContext_Init`** (revealing the tagged typed render-command-pool
  architecture + the shader table), and mapped the **`VideoPlayer`** FMV class.
  76 renames total this rendering thread — the render pipeline is now mapped
  from NV2A hardware init down to individual draw handlers. See
  `RE_NOTES_rendering_system.md`.

## What's missing / genuinely open

- ~~`xbox.gdt` missing `D3DPRESENT_PARAMETERS`~~ **RESOLVED (was a
  misdiagnosis)**. The type was never actually missing -- a diagnostic script
  proved it's right there in the original archive. The real cause:
  `set_local_variable_type` only searches the program's own DataTypeManager,
  never other open archives directly; a type must first be "seeded" into the
  program's DTM via a `set_function_prototype` call (which IS cross-archive-
  capable) before `set_local_variable_type` can find it. Fixed by applying the
  real, accurate `Direct3D_CreateDevice` signature (added to `ssx_auto_rename.py`).
  The `xbox_fixed.gdt` rebuild effort was unnecessary (harmless, but solved
  nothing) -- see `RE_NOTES_rendering_system.md`'s correction note and the
  `reference_ghidra_mcp_connection` memory's "Round 7 CORRECTION" for the full
  story, including the general-purpose fix for this class of problem.
- ~~The top-level per-frame frame driver~~ **DONE** — cracked as
  `SceneRenderer_RenderFrame` (found the never-analyzed blob's boundary by
  backward byte-scanning). The render pipeline is now mapped end to end.
  ~~The `FUN_000e64xx` reflection-math cluster~~ and ~~the graphics-context
  method table at `0x001a2b60`~~ are also **DONE** (see below and
  `RE_NOTES_rendering_system.md`). Still open: the register-relative
  dispatcher that indexes the 16-slot draw-mode table (every entry is named
  regardless).
- **Consolidated "stuck" list from the 2026-07-20 `GfxContext` vtable/xbox.gdt
  session** (per direct user request — document what's stuck before
  redirecting, so it's traceable rather than silently dropped):
  - **`GfxContext`'s vtable: ~6 of 120 slots still genuinely stuck** (down
    from 46 across 5 follow-up passes this session — the thread is now
    functionally closed, real diminishing returns). Remaining:
    `FUN_000fb080` (plain 4-float setter on GfxContext's own `+0x50..+0x5c`,
    unclear target — NOT the same field as the now-named
    `GfxContext_GetMatrixStack1Bounds`, which reads the nested `+0x220`
    object's own `+0x50..+0x5c`, a confusing offset coincidence between 2
    different objects), `FUN_000fb3f0` (reads a *global*, `DAT_001faf90`,
    not `this` — possibly unrelated to GfxContext despite living in its
    vtable; resembles quad/vertex-index extraction, not confirmed connected
    to terrain), `FUN_000fb560` (double vtable indirection through
    `FUN_000f98e0`'s result), `FUN_000f98e0` (trivial getter for `this+0x34`,
    unclear target), `FUN_00104760`/`FUN_001033d0` (both large and complex —
    `001033d0` looks like a full board-mesh skin-and-submit-draw function,
    tag-confirmed via `"frL_ARMS"`, genuinely deferred rather than guessed).
    Not worth further effort without a different angle (e.g. finding what
    else references `this+0x34` or `DAT_001faf90`).
  - ~~`exUT_`/`exTW_` "named-queue subsystem" bookmark~~ **RESOLVED** — it
    was never a real named-queue system. `FUN_00102850` turned out to be
    `Text_RenderGlyphString` (the core glyph rasterizer, confirmed via a
    direct `Font_GetGlyphMetrics` call), and it reuses `"mesablan"+this+8`
    as a plain scratch counter — confirming the tags are just aliased
    static-storage offsets, not a queue mechanism. 2 of its 8 wrapper
    variants named precisely (byte/wide string mode); the other 6
    (`FUN_00103dc0`/`e70`/`eb0`/`ef0`/`f90`/`104020`, calling
    `FUN_00102360`/`00102180`/`001025b0`/`00101a90`/`00101d00`) are almost
    certainly more text-rendering variants (one, `FUN_001025b0`, has an
    almost identical signature to `Text_RenderGlyphString`) but weren't
    individually confirmed — low-value to force further, thread is
    functionally closed.
  - **Several D3D-semantic gaps named at "structural confidence" only**
    (behavior/field-shape confirmed live, exact D3D concept not pinned down):
    `GfxContext_SetStateExtraFlags`/`GetStateExtraFlags` (a 5-bit field),
    `GfxContext_SetStateModeAndColor24` (2-bit mode + 24-bit likely-RGB
    value), `GfxContext_SetStageFilterMode` (per-stage 2-bit field),
    `GfxContext_SetBlendPresetByMode` (3 float-constant presets, exact
    preset meaning unknown), `GfxContext_SetTailFlagByte` (a flag near the
    tail of GfxContext's ~2.2MB allocation). Would need either a matching
    real Xbox SDK D3D8 constant table to cross-reference bit patterns
    against, or a dynamic trace, to firm up.
  - ~~**`Rider_UpdateSnowSprayFX`** has zero static callers~~ **RESOLVED
    (2026-07-21)** — the "zero callers, likely vtable-dispatched" claim was
    stale and wrong. A fresh check found an ordinary direct-call chain:
    `Rider_UpdatePhysicsState` -> `Rider_UpdateUberTrickGlowFX` -> the
    newly-named `Rider_UpdateAmbientFXBatch` (runs unconditionally every
    frame) -> `Rider_UpdateSnowSprayFX`. No vtable dispatch involved. See
    `RE_NOTES_ubertrick_fx_cluster.md`.
  - **`FXParticle_AccumulateEmissionTime`**'s caller loop at the very top of
    `Rider_UpdateSnowSprayFX` has a `this` receiver that isn't reliably
    recoverable from the decompile (thiscall-with-optimized-out-ECX
    ambiguity) — likely a catch-up mechanism for multiple elapsed periods,
    mechanism unconfirmed.
  - ~~`PixelBlit_ValidateAlignmentAndDispatch`'s 5 format handlers~~
    **NAMED (2026-07-21)**: `PixelBlit_ConvertRowsFormat0`/`Format1`/
    `Format2And3`/`Format4And5`/`Format6And7Interlaced` — the shared row-
    blit-loop + ordered-dithering shape is now documented (structural
    confidence; exact per-format pixel math still not decoded, likely a
    texture bit-depth down-conversion library). Also found the previously-
    noted boundary-overlap bug is **no longer present** — all 5 bodies are
    clean and contiguous now. See `RE_NOTES_rendering_system.md`.
  - ~~`FUN_000fa6d0`~~ **NAMED and CORRECTED (2026-07-21)**: not a
    "different, unidentified table" as previously noted — re-derived the
    offset arithmetic and found it's the exact same 40-byte per-slot
    texture record `GfxContext_GetTextureHandleBySlot` uses, just field +4
    instead of +0. Renamed `GfxContext_GetTextureSlotField2` (exact semantic
    meaning of the field itself still undetermined).
- ~~**The trick-score writer** (`rider+0x5710`, who increments it during
  gameplay)~~ **RESOLVED (2026-07-20, later session)** — see
  `RE_NOTES_trickcombo_scoring_resolved.md`. The single longest-running open
  question in this project is closed: every `Rider` embeds a `TrickCombo`
  scoring sub-object at `rider+0x5630`; `TrickCombo_ScoreRailCompletion`/
  `ScoreAirCompletion` both write `*(int*)(this+0xe0) += bonus` where
  `this=rider+0x5630` — `0x5630+0xe0=0x5710`. The write was never found by
  byte-level searches because it's expressed as a local offset off an
  already-rebased pointer, never as a literal `+0x5710` displacement. The
  full formula, a genuine repeat-trick anti-farming penalty, the real Uber
  Trick name table (`McTwist`, `Haakon Flip`, escalating into `Roadkill`),
  and the entire trick-type-to-voice-line commentary dispatch are all
  mapped too. `rider+0x5720`'s own writer (see the old note below, kept for
  history) was NOT specifically re-checked against the `TrickCombo` struct
  offset (`0x5720-0x5630=0xf0`) — a quick, low-effort follow-up worth doing
  if this area is revisited, though `+0x5710` was the actual target all
  along. **Follow-up done (2026-07-21, post-audit)**: checked it. Decompiled
  all 9 known `TrickCombo_*` methods and found the ONLY write anywhere to
  `TrickCombo+0xf0` is `*(int*)(this+0xe8+iVar5*4) += 1` inside
  `TrickCombo_ScoreAirCompletion`, where `iVar5` is a tier index (0-5) from
  a newly-found free function (renamed `Score_ResolveTierAndColor`, was
  `FUN_000c37c0`). This is a per-tier trick-landing-count histogram
  (`this+0xe8..+0xfc`, 6 ints), incremented by exactly 1 per landed trick —
  which **cannot** be what `rider+0x5720` holds, since that's read as a raw
  score value compared against 250000/500000/799999 (a +1 counter would
  need hundreds of thousands of landed tricks in one race to reach that).
  **This rules out the `TrickCombo+0xf0` hypothesis for the `+0x5720`
  writer** — the score-writer mystery for `+0x5720` remains genuinely open.
  Real side-finding kept from the check: `Score_ResolveTierAndColor` sweeps
  a 6-row float threshold table and returns both a tier index and an RGBA
  color; confirmed via its `HUD_DrawWorldSpaceMarkers` call sites (both
  write its color out-param directly into `DAT_001baf70/74/78/7c`, the same
  globals set immediately before every `HUD_DrawNumberStyled` call
  elsewhere in this codebase) that it's the score-popup number's
  point-value-to-tint-color classifier — distinct from the already-named
  `Trick_GetScoreTier` (matches exact streak-bonus literals for "1x/2x/3x/4+"
  combo-multiplier text, a different tier concept). 1 rename.
- ~~**The `rider+0x5720` writer**~~ **RESOLVED (2026-07-22, Tier-1 push):
  the field is a PHANTOM.** Bounding a previously-unbounded race-results
  function (`RaceOutcome_EvaluateAndBeginPostRace`, found via a fresh
  `0x5710`-displacement search) revealed the identical GameMode-branched
  medal logic on a raw rider pointer — and all three field pairs align at
  a uniform `+0x10` shift (`0x5720/0x150/0x458` component-frame ==
  `0x5710/0x140/0x448` rider-frame). The lone "`rider+0x5720`" read in
  `RiderEvent_RaceFinishSequence` goes through the RiderEvent component's
  rebase frame and **is `rider+0x5710`** — the live trick score whose
  writer was already solved. No second field, no missing writer, no
  dynamic analysis needed. Bonus: `rider+0x140`=placement,
  `rider+0x448`=race time confirmed. See
  `RE_NOTES_trickcombo_scoring_resolved.md`'s closing section.
- (Historical, pre-resolution notes below, kept for the investigation
  trail) **New lead, not yet resolved**: `rider+0x5720` (16 bytes away) is
  consumed by the race-finish sequence and resolved to a medal tier against
  score-shaped thresholds (250000/500000/799999) — confirmed score-field
  *region*, but its own write site is equally unfound (exactly one static
  reference to `+0x5720` exists anywhere, a read). Current best theory:
  either genuinely-uncompiled-yet C++ not yet located, or driven by a
  runtime-computed offset invisible to static search. Needs dynamic analysis
  or a fundamentally different search angle to resolve. **Checked one more
  angle**: `Rider_TeardownSubobjects` (`0x00036990`) turned out this session to
  actually be a dense per-frame decay/physics-lerp update function (not a real
  destructor body, despite being reached via Rider's genuine destructor
  chain — see `RE_NOTES_terrain_collision.md` for the full ambiguity) —
  grepped it specifically for `+0x5710`/`+0x5720` references since it's a
  genuine per-frame gameplay tick not previously checked under that framing.
  Zero hits. Another angle closed without success.
  **Major structural lead (2026-07-20)**: raw disassembly (not decompiled C,
  which hid the true `this`) revealed `Component_UpdateAll` operates on 3
  fixed, inline-embedded component slots at `rider_this+0x28/+0x80/+0xd8`
  (`0x58` bytes apart), each the sentinel head of its own intrusive circular
  linked list (the same `NodeBase` idiom used throughout this project) --
  answering "whose list is it" definitively for the first time across every
  session that's touched this thread.
  **Insertion side found and fully reconciled (2026-07-20, same day)**: each
  0x58-byte component slot actually embeds TWO independent doubly-linked
  lists, not one — `+0x28` (a BdrSeq animation-event queue, populated by
  `Component_InsertNode` from `RiderAnimation_TriggerByEventCode`'s chain and
  by `ComponentSlot_ResetAndReseedBdrSeq`) and `+0x50` (the active-component
  list `Component_UpdateAll` walks every frame). Proven via
  `Component_InitEmptyList` setting both off one identical base in a single
  call. Both the insert and traversal mechanics of this thread are now fully
  understood and reconciled — no more ambiguity there. **Still not found,
  and now the single narrowest remaining piece of this whole mystery**: what
  real object type gets attached to the `+0x50` active-component list (what
  vtable+0xc's real implementation does per attached node) — that object's
  code is the most likely home for the actual trick-score write. Needs
  either a search for whatever allocates/attaches to `+0x50` specifically
  (not `+0x28`, which is a red herring for this specific question — that's
  the animation queue), or dynamic analysis.
  See `RE_NOTES_rider_update_chain.md` for the full trace.
  **The `+0x50` inserter search (three angles: the `+0x28` utility cluster,
  tag-string guessing, an opcode sweep) came up empty this session** --
  documented honestly in `RE_NOTES_rider_update_chain.md` as exhausted for
  now, with a real possibility `+0x50` is simply never populated in retail.
  **Pivoted to the HUD side of the same mystery instead** (who pushes into
  `rider+0x5784`'s tagged-event array, read by the HUD's score-popup system)
  and fully closed that angle: found and named `HUD_TickRiderDisplayState`
  (the master per-frame HUD tick, previously completely unknown) plus
  `HUD_SelectActivePanelMask` -- both confirmed to only READ `+0x5784`, never
  write it. **The entire HUD side is now ruled out as the writer.** The real
  answer must be in the gameplay/physics tick or a dedicated trick-detection
  subsystem not yet located. Also found `OptionsMenu_CacheDisplaySettingsFromWidgets`
  (unrelated, a false-positive lead kept for its own value). 3 renames.
  **740 renames** (735 functions + 5 data). Live metric: 1,010/5,340
  (≈18.91%). Full trace in `RE_NOTES_race_hud.md`.
- ~~**The generic per-frame `Node` Update dispatcher.**~~ **RESOLVED
  STATICALLY (2026-07-22, Tier-1 push)** — the prior searches failed
  because they hunted callers of vtable slot 7 (`+0x1c`); the real
  per-frame tick uses **slot 1 (`+0x4`)** via a different, then-unnamed
  dispatcher: `NodeRegistry_TickAllOfType` (was `FUN_000aa830`), driven by
  `InGameState_TickSubsystemsByTypeOrder` (was misnamed
  `InGameState_ApplyHudElementVisibility` — corrected) walking an 11-entry
  type-order table, plus a render sibling (`NodeRegistry_RenderAllOfType`,
  slot 2, per-viewport 13-entry table) and a pending-node integrator
  (`NodeRegistry_IntegratePendingNodesOfType`, comparator slots 3/4).
  Resolves who ticks LessonMan/SnowFallMan/PREAI/PostAI and every other
  "orphaned Update method" project-wide. Full writeup:
  `RE_NOTES_node_base_class.md`.
- ~~`.cml` keyframe field layout~~ **RESOLVED** — cross-record statistical
  diffing of 6 consecutive `Location` records found position (3 large floats,
  `+0x3c`/`+0x68`/`+0x94`) + rotation (3 small floats, `+0x134`/`+0x138`/
  `+0x140`) per record, out of an otherwise-constant placeholder template.
  Rests on internal statistical evidence only (no external file had raw
  binary coordinates to cross-check against). Exact units and the equivalent
  fields for `Moment`/`Transition`/`Staging` record types not individually
  re-verified — same technique would apply directly if needed.
- **`SaveGame` record internals.** The container format (17-state pipeline,
  5 chunk types) is fully mapped, and the context object's own class is now
  identified (`SaveOverlay` — a UI overlay panel owned by the newly-found
  `OverlayManager` system, not a freestanding writer). Confirmed the
  3620-byte data records (state 3) are embedded inline in the save-context
  object itself (`+0x48+i*0xe24`) and the state-5 object list is a
  **Checked a new lead (2026-07-21)**: `ReplayManager_Construct` initializes
  its own 4-slot recording buffer at the *exact same* base offset and
  stride (`this+0x48`, `0xe24`=3620-byte slots) as `SaveOverlay`'s record
  storage — see `RE_NOTES_replay_system.md`. Traced `SaveGame_WriteDataRecords`
  directly: it checksums `param_1+0x48+i*0xe24` where `param_1` is
  `SaveOverlay`'s own object, **not** a live `ReplayManager` pointer — so
  they're separate, independently-allocated buffers with an identical
  layout, not the same memory. **Sharper lead**: both are very likely
  populated by the same underlying "build a checkpoint/snapshot record"
  routine (parameterized by destination pointer) — finding a function that
  takes a `this+0x48+i*0xe24`-shaped destination as a *parameter* (rather
  than being hardcoded to either object) would likely be the actual shared
  record-content writer. Not found yet, but a more concrete search target
  than before.
  self-referential circular list serializing `NodeBase`-family objects by
  their save-portable instance ID (`+0x20`, confirmed against
  `NodeBase_ConstructRoot`) — but exact field layout *inside* either record
  type still wasn't traced, the specific `NodeBase` subtype in the list is
  unidentified. ~~No deserialize/load counterpart has been found~~
  **A load-side reader for the shared record format was FOUND (2026-07-22)**,
  but it targets a DIFFERENT object than the writer:
  `AggressionManager_DeserializeStateFromBuffer` (was `FUN_000bc910`),
  reached from `InGameState_LoadLevel` via 3 more new functions
  (`AggressionManager_LoadPendingStateBuffer`/`ExtractStateBufferHeader`/
  `DiscardPendingStateBuffer`) gated on a raw loaded-buffer field
  (`host+0x3940`; **source now FOUND**:
  `AggressionManager_AllocateReplayLoadBuffer` allocates a 512KB
  **"Replay load"**-tagged buffer there — so this whole chain is the
  REPLAY-load path, not a general save load. The writer was invisible to the
  C export because it sat in an unbounded region; live Ghidra's xref engine
  on the absolute `0x1df890` found it). Confirmed field-for-field against the write side
  (same record-count chain, same `+0x48`/`0xe24` addressing, same
  object-list node shape). **Register-level disassembly caveat**: I first
  mis-named these `SaveGame_`/`InGameState_`, then proved via `MOV
  ECX,0x1dbf50` at the call site that the receiver is the AggressionManager
  host global `0x1dbf50`, NOT InGameState — corrected all 4 names to
  `AggressionManager_`. The **writer** pipeline (`SaveGame_TickAndFlush`, a
  vtable-`0x19d530` method) runs on **SaveOverlay** (a heap panel), a
  *separate* object sharing this record format — so writer and this reader
  are two objects, not one. Working hypothesis (unproven): `0x1dbf50` is the
  live career/rivalry state, `SaveOverlay` a write-staging snapshot copied
  from it — which would make the record-*content*-writer mystery below
  actually about what populates `0x1dbf50+0x48` during gameplay. See
  `RE_NOTES_player_snapshot_system.md`'s "found a deserializer" update
  (with its IMPORTANT CORRECTION) for the full writeup. Does **not** resolve
  the record-content-writer mystery below (checked earlier candidate,
  `New_Dead_Object` — a related but distinct checkpoint-restore system, not
  the save-file loader).
  **2026-07-20 partial progress**: named `SaveGame_StageChunkAndAccumulateChecksum`
  (state 3's actual worker — confirmed it only stages+checksums each record,
  doesn't build its content) and found the record *count* comes from
  `DAT_001e3c7c+0x72c` (current level/track object) `+0x1c+0x7c` — a real
  lead that the records are likely one-per-checkpoint/segment. The record
  *content*'s populating code wasn't found — it must be written somewhere
  outside this whole state-machine family (live during gameplay, into the
  singleton instance), a search of similar difficulty to the long-standing
  trick-score-writer mystery (RESOLVED, see above). **2026-07-20, later
  session**: traced the full concrete access chain via raw disassembly --
  `Application(DAT_001e3c7c) -> +0x72c = InGameState -> +0x38 =
  OverlayManager -> +0x34 (slot 0xd/19) = SaveOverlay` -- previously only
  described as "a field," now pinned down exactly. The record-content
  writer still wasn't found (needs a way to search for other code
  computing this same chain), but reading the rest of
  `OverlayManager_Construct`'s 19 panels in full found 3 new ones (slots
  0x13/0x14/0x15, content-confirmed via the `kOVSaveReplay`/"Save Replay"
  string) forming a whole separate `SaveReplayOverlay` family -- see
  `RE_NOTES_player_snapshot_system.md`. Not chased further this pass. Also re-read
  the already-named `SaveGame_WriteObjectListChunk`/`InitObjectListIterator`
  (state 5) for more field-level detail: confirmed each per-object header
  chunk (magic `0x11111113`, part of the already-documented tagged-chunk
  family) maps `+0xc`/`+0x18`/`+0x1c`/`+0x20` from the current `NodeBase`-
  family list entry (`+0x20` = the already-confirmed save-portable instance
  ID) -- the exact `NodeBase` subtype being walked still isn't identified,
  but the chunk's own field mapping is now clear.
- ~~`RiderEvent` case handlers unread~~ **FULLY NAMED (2026-07-20, later
  session)** — this entry previously said "read" prematurely; most were
  still bare `FUN_` addresses. Both `DispatchTypeA` and `DispatchTypeB`'s
  entire switches are now individually named, not just read — turned out to
  be almost entirely the rail-riding subsystem (mode transitions, balance/
  lean input, combo timeout, the master rail-physics tick) plus the core
  grab-input-combo resolver and a race-end fade sequence. See
  `RE_NOTES_rider_update_chain.md`. No further `+0x5710`/`+0x5720`
  references found beyond the race-finish sequence already documented.
  Individual per-event-code semantic vocabulary still not exhaustively
  catalogued for every constant, but nothing left unread structurally.
- ~~**The model-archive wrapper format's actual level-script bytecode.**~~
  **RESOLVED (2026-07-20, later session)** — walked `gari.xsf`'s per-script
  data offsets beyond `RaceMode` and confirmed they're `{opcode-ID,
  parameter-block}` command lists using the exact same already-documented
  `Script_DispatchOpcode`/`ScriptVM_DispatchOpcode` opcode vocabulary, not a
  separate bytecode format. **There is no new bytecode to reverse-engineer
  here** — what remains (which numeric opcode ID maps to which specific
  case, and per-track tuning values) is data analysis, not code RE. See
  `RE_NOTES_archive_format_decoded.md`.
- ~~`xbox.gdt` never opened in Ghidra's Data Type Manager~~ **DONE** (user
  action, 2026-07-19) — imported as a project-level archive (visible in the
  Active Project tree alongside `default.xbe`, not just a transient file
  reference), confirmed expanded with real type entries under an `xbox.h`
  folder plus base scalar typedefs. The 2,084 real Xbox SDK types are now
  browsable/referenceable. **Now actively being applied** (2026-07-20): 6
  `D3DVIEWPORT8` applications across the rendering system, all verified live
  (see `RE_NOTES_rendering_system.md`). **Important caveat discovered**:
  `set_local_variable_type`/`set_function_prototype` report "successfully"
  even when the requested type isn't found and they silently fall back to
  `int` — every application must be re-decompiled and eyeballed afterward,
  never trusted from the response text alone (see "Round 7" in the
  `reference_ghidra_mcp_connection` memory). `D3DPRESENT_PARAMETERS` tried
  and confirmed genuinely absent from the built archive (a dependency on an
  unresolved forward-declared type), reverted safely rather than left
  silently mistyped. This was previously a hard blocker (GUI-only action
  this toolset can't perform) — now cleared and in active use.
- ~~`default.xbe.c` very stale~~ **DONE** (user action, 2026-07-19) — ran
  `ssx_export_c.py` in the `default.xbe` CodeBrowser's Script Manager,
  producing `default.xbe.refreshed.c` (7,450,938 bytes vs. the old file's
  7,248,358). Spot-checked: contains 39 hits for functions renamed in this
  very session (`LensFX_Construct`, `LightManager_AddPointLight`, etc.) that
  the old file has zero of — confirms it's a genuine fresh snapshot, not a
  cache. Promoted to the canonical `default.xbe.c` filename; the old version
  preserved as `default.xbe.c.stale-2026-07-17` (an even older
  `default.xbe.c.session-backup` also still sits alongside it from a prior
  session). Both GUI-only blockers from this list are now cleared.

## Progress log (milestones only, not every session)

- Early sessions: level-script system, node registry, sweep-prune, game
  modes, race lifecycle, HUD, frontend menus, input pipeline mapped.
  ~297 renames.
- `.loc` localization format fully reverse-engineered; `GhidraMCP` patched
  through round 5 (disassemble/create/delete function, byte search, symbol
  status). Full rename-script integrity verified exhaustively (304/304).
  ~307 renames.
- Camera shake system, `Node`/`NodeBase` hierarchy discovered and the
  per-frame Update-dispatch question exhaustively investigated (definitive
  negative result). `.big`/RefPack archive format decoded, `RaceMode` found.
  `.cml` format characterized, `VenueStaging` system found. `RiderEvent`
  animation-trigger system found. `PlayerSnapShot`/`SaveGame` complete
  format mapped. GhidraMCP patched to round 6. **362 renames.**
- Found `RiderEvent_RaceFinishSequence` and the medal-tier resolution chain —
  a new, concrete lead on the score-field region (`rider+0x5720`, score-shaped
  thresholds) though its write site remains as unfound as `+0x5710`'s.
  Confirmed `SaveGame`'s two remaining record types' container mechanics
  (inline embedding, self-referential circular list) without resolving their
  contents. **366 renames.**
- Identified `SaveGame` state 5's serialized object type as `NodeBase`
  instances (by save-portable instance ID). Checked `New_Dead_Object` as a
  deserializer candidate — related checkpoint-restore system, not it. Named
  `CRT_MemCopy` (the generic memcpy underlying both). Cross-checked
  `elysium.xsf` against `gari.xsf` — same core script vocabulary confirmed.
  **367 renames.**
- Cracked the `.cml` keyframe field layout via cross-record statistical
  diffing — position + rotation fields identified per `Location` record. No
  new renames (data-format finding, not code). **367 renames.**
- Finished the `RiderEvent` handler sweep (no more score leads found) and
  closed out `Rider`'s own vtable — the last 3 slots (2 were still
  unanalyzed `LAB_` labels) now named, including a substantial world-space
  marker/nameplate draw function. Every Rider vtable slot is now named.
  **371 renames.**
- Extended the vtable sweep to `TrickTrigger`/`Boost`/`Fence` — found 13 of 16
  slots shared byte-identically across all 4 classes, corrected 4
  mis-attributed `Rider_`-prefixed names to generic `Node_` names, and named
  each class's own distinct slots (including real logic in `Boost_Update`/
  `Fence_Update`). **382 renames.**
- Completed the sweep across `Roller`/`Cracked`/`Timer`/`Debounce` — every
  simple level-script node type now has verified Update+Destruct. Superseded
  `New_Script_6`/`7` → `Timer_Update`/`Cracked_Update`. Confirmed
  command-record column 4 = the standard "attached script handle" field
  across three node types. **388 renames.**
- Finished the vtable sweep: UVScroll/TexFlip/Movie/ZBoost/AnimObject
  family/Particle/cMeshAnim (18 renames). Essentially every level-script node
  type now has verified Update+Destruct. Side-find: `CrowdBox` and
  `CameraAudioPanningMode` share one linker-folded vtable. **406 renames.**
- Closed the last two node types (`AnimCombo`/`UnknownOpcode09`/`0d`) — every
  one of the 24 level-script node types now has verified Update+Destruct.
  Vtable sweep thread complete (29 renames total across the whole thread).
  **412 renames.**
- Named the last 4 `Widget` helpers (tag-confirmed, decompiler-mangled).
  Frontend menu-map thread now genuinely complete. **416 renames.**
- Mapped `Widget`'s own full real vtable (11 renames) — a property-cascade
  system, a transition-animation state machine, per-frame layout. Brief
  unproductive `SaveGame`-vtable-boundary detour abandoned quickly, no loss.
  **427 renames.**
- Retried the `SaveGame` context-object identification with a cleaner
  technique — found the real vtable boundary, traced the constructor to a
  brand-new `OverlayManager` system (19 HUD/menu panels). The save-game state
  machine is the "Saving..." progress overlay's own internal state, not a
  freestanding writer. **430 renames.**
- Identified 6 more `OverlayManager` panels via exact `Localization_ResolveString`
  ID lookups (World Circuit bracket, name entry, unlock notifications, replay
  title, pause HUD detail). 7 of 19 panels now named with high confidence.
  **436 renames.**
- Found and fully vtable-mapped the `AudioSystem`/`BXAudioSystem` singleton
  class (new system, not previously traced past one function) — all 15 real
  vtable slots named across two passes: constructor pair, destructor,
  tag-confirmed sound-memory allocator + slot-configure sibling, slot-release,
  2 vtable-dispatch-only "play sound" variants, a 3D positional-audio
  calculator, a per-frame channel-volume tick, stop-all/stop-by-tag, a
  pending-request ticker, a callback-context setter, and a verified no-op
  stub. Surfaced two internal channel arrays (64-slot live pool +
  dynamic-sound-buffer pool). **451 renames** (448 functions + 3 data).
- **Same session, major follow-on find**: `AudioSystem` is the leaf of a
  9-level single-inheritance chain, not standalone. Traced by following a
  global (`DAT_001f82e4`) read from 2 of AudioSystem's own vtable slots back
  to its constructor — the true root of the hierarchy, not a sibling system
  as first assumed. Named all 8 intervening constructors: `AudioBankBase`
  (root, tag `"BankInstances"`) → `AudioBank` (tags `"mBank"`/`"mPath"`) →
  `MidiBankManager` (tag `"MIDIBANKS"`) → `AudioStreamManager` (tag
  `"StreamArray"`) → `MusicManager` (reads `data\config\music.inf`) →
  `SpeechManager` (tag `"mSpeechInstance"`) → `SoundGroupManager`
  (cross-reference confidence) → 1 layer left unnamed (too little evidence)
  → `AudioSystem`. Retroactively explains the byte-identical vtable slots
  found earlier (genuinely inherited, unmodified across 8 levels) — same
  shared-vtable-slot idiom as the Node-family sweep, confirmed in a new
  subsystem. **460 renames** (457 functions + 3 data).
- **Same session, immediate follow-up**: read all 9 levels' vtables in full
  (not just constructors), finding each level's genuinely-new methods.
  `SoundGroupManager` (level 7) and the previously-unnamed level 8 (now
  `AudioChannelMixer`) turned out to be where `StopSoundsMatchingTag`/
  `StopAllSounds`/`TickPendingRequests`/`UpdateChannelVolumes` are truly
  introduced (all 4 left under their existing `AudioSystem_` names —
  correction documented, not force-renamed). Also found AudioSystem's real
  vtable is **16 slots, not ~15** — 2 missed slots decompiled and named
  (`AudioSystem_PlayLoopedCue`, a loop-aware play variant;
  `AudioSystem_ResolvePriorityTier`, a threshold-bucketing function matching
  `MedalTier_ResolveFromValue`'s shape). Named `AudioBank`'s own new slot
  too (`AudioBank_RegisterSoundInstance`). **464 renames** (461 functions +
  3 data).
- **Same session, second follow-up**: moved every `RE_NOTES_*.md` file into a
  new `RE_NOTES\` subfolder and every `.py` tool into a new `scripts\`
  subfolder (user-requested reorg) — project root now holds only non-doc,
  non-script files. Then swept each of the 9 audio inheritance levels'
  singleton globals for outside callers (not just constructors), finding a
  real public-API layer: `.big`-archive-backed cue dispatch
  (`AudioBank_ResolveCueEntry`/`DispatchCueToDevice`), an ambient
  position-blending system (`AmbientZone_UpdateBlendedPosition`), a
  per-track adaptive-music loader (`MusicTrack_Construct`), beat-synced
  voice-line triggering tied into `RiderEvent`
  (`SpeechManager_TriggerLineByEventCode`), and `MidiBankManager`'s
  destructor + queue method. **471 renames** (468 functions + 3 data).
- **Same session, third follow-up**: finished the public-API sweep — read
  every remaining referencing function for `SoundGroupManager`/
  `SpeechManager`/`AudioStreamManager`'s singletons. Found 2 new satellite
  classes (`AmbientZone`, 7 methods; `SpeechLineSet`, tag-confirmed via
  `data\config\speech.inf`), the `speech.inf` parser itself
  (`SpeechManager_ParseSpeechConfig`), 4 more real singleton-level methods,
  and independently confirmed destructors chain in reverse through all 9
  inheritance levels, mirroring the constructor-chain discovery. One
  function (`FUN_001170c9`) skipped — known wrong-function-boundary
  decompiler issue, not fixed this pass. **487 renames** (484 functions + 3
  data). Live-recounted function-naming metric: 757/5,212 (≈14.5%).
- **Pivoted to the rendering system** (user-requested — "pivot to new
  system, go to audio later if needed"), genuinely untouched before this
  session. Found the ~61 low-level D3D8 API wrappers were already
  auto-labeled by Ghidra (not manual work), then traced game-side code from
  `D3DDevice_Present`/`Clear` callers: the true engine boot chain
  (`Application_InitPlatformAndDevice` → `Renderer_InitializeD3DDevice`,
  earlier than `Application_InitSubsystems`), a content-confirmed
  disc-read-error screen, a reusable 2D sprite-batch renderer, a
  loading-screen blitter, and a generic RNG (found via the boot chain,
  turned out to be the same utility already used in the audio system's
  `AmbientZone` code). **11 renames. 498 renames** (495 functions + 3 data).
  Live-recounted function-naming metric: 769/5,212 (≈14.75%). Main 3D
  render loop still open — see "what's missing" above.
- **Continued the rendering thread**: traced `D3DDevice_DrawIndexedVertices`'s
  remaining 5 callers, finding the real mesh-drawing layer —
  `BoardMesh_DrawAttachedPatches` (confirmed `BoardMesh_LockBuffers`/
  `GetBufferAddr` already existed from an earlier session) and 4
  `MeshRenderer_*` functions sharing a shader-cache context, including one
  that samples the back buffer as a texture (screen-space effect). Found a
  16-slot mesh draw-mode dispatch table (4 slots filled; dispatcher not
  found — `/search_address_refs` limitation). **5 more renames. 503
  renames** (500 functions + 3 data). Live-recounted function-naming
  metric: 774/5,212 (≈14.85%).
- **Full-throttle continuation (opus)**: found the complete per-frame scene
  render pipeline — `SceneRenderer_RenderAllPasses`/`RenderPassRange` (master
  frame orchestrators, 6 sub-passes × priority groups) → `SceneView_RenderPass`
  (builds camera projection via `Matrix_BuildPerspectiveProjection`/
  `Orthographic`, sets viewport, walks a 6-dword render-command list — the
  dispatcher that ultimately invokes the mesh draw handlers). Named
  `Renderer_SetDefaultDeviceState` and the deep NV2A
  `D3D_InitMiniportAndFrameBuffers`. Fully mapped the 16-slot mesh draw-mode
  dispatch table: 6 `MeshDrawMode_*` handlers, 2 `MeshQueue_Insert*Bucket`
  enqueue functions (revealing a collect-into-buckets-then-drain design),
  shared no-op stubs, plus `BoardMesh_BuildAndUploadGeometry`. **17 more
  renames. 520 renames** (517 functions + 3 data). Live-recounted
  function-naming metric: 791/5,215 (≈15.2%) — crossed 15%. Only the
  outermost frame driver (never-analyzed blob) remains open.
- **Same continuation**: named the render-context helper layer (the small
  `FUN_000fbxxx`/`FUN_000fcxxx` calls appearing as noise in every render
  decompile) — 3 matrix-slot copiers (`RenderContext_SetMatrixSlot*`), 2
  vertex-register pushers, the fog/shader-constant setup, and the
  multi-buffer index rotator, plus 2 resource-registration helpers. 9 more
  renames. **529 renames** (526 functions + 3 data). Metric: 800/5,215
  (≈15.3%).
- **Cracked the top-level frame driver** (the last render-pipeline gap):
  `SceneRenderer_RenderAllPasses`/`RenderPassRange`'s callers lived in a
  never-analyzed blob with no containing function. Found the boundary by
  backward byte-scanning for a ret+padding pattern, created the function
  (`0x00105ce0-0x0010618a`, contains both caller sites), named it
  `SceneRenderer_RenderFrame` — the master per-frame render function with a
  reflection pass, a **split-screen 2-player viewport handler**, and
  detail-LOD selection; reached only via a graphics-context method table.
  Also named the matrix-stack push/pop + detail selector. 4 more renames.
  **The render pipeline is now mapped end to end** (frame table →
  RenderFrame → passes → SceneView command-list walk → mesh draw handlers →
  GPU). **533 renames** (530 functions + 3 data). Live metric: 804/5,216
  (≈15.4%).
- **Characterized the `GfxContext` class** — the central render-device object
  (`param_1` throughout render code). `SceneRenderer_RenderFrame` is a virtual
  method in its vtable at `0x001a2b38` (with embedded "Grid indicies"/
  "ShadowVolumeData" data-section labels). Mapped its view-dimension accessors,
  two matrix stacks (world + view/projection), and full render-state command
  builder (push/pop + blend/alpha/texture bit-field setters + commit), plus
  destructor and GPU-wait. Also mapped the view-array accessors, pinning the
  view layout (base +0x38, count +0x3c, active index +0x40, 0x94-byte stride).
  22 more renames (66 total this rendering thread). **555 renames** (552
  functions + 3 data). Live metric: 826/5,236 (≈15.8%).
- **Read `GfxContext_Init`** (vtable+0x04) — the most architecturally
  revealing render function. It allocates a full set of **tagged, typed
  render-command-list pools** (`defPatchList`/`defSpriteList`/`defMeshList`/
  `defEmitterList`/`defShdVolList`/`defCloudNodeList`/etc.), each pre-seeded
  with its draw-mode handler — confirming the render architecture:
  game systems append typed records to per-category pools, `SceneView_RenderPass`
  walks the resulting list, each record dispatches to its pool's handler. Also
  builds the full shader table (~24 VS + ~26 PS), 4 `VideoPlayer` objects (new
  FMV/replay class), an "XBoxBezierMan" tessellation object, and default device
  state. 4 more renames (70 total this rendering thread). **559 renames** (556
  functions + 3 data). Live metric: 830/5,236 (≈15.9%).
- **Fully mapped the `VideoPlayer` class** (FMV/replay, surfaced from
  `GfxContext_Init` which builds 4 instances): vtable at `0x001a83fc`, 6
  methods — `Construct` (640×480 surface via the movie subsystem), `Open`
  (tag-confirmed 1MB `streambuff` + decode stream), `UpdateSkipInput`
  (controller skip-button poll), `Tick`, `IsFinished`/`IsStopped`. The system
  behind SSX's intro/attract FMVs. 6 more renames (76 total this rendering
  thread). **565 renames** (562 functions + 3 data). Live metric: 836/5,241
  (≈16.0%) — crossed 16%.
- **Mapped the `BezierMan` class** (a.k.a. `XBoxBezierMan`, Xbox Bezier-patch
  hardware tessellation for smooth curved surfaces, also from `GfxContext_Init`):
  vtable `0x001a2a50`, tessellation-param defaults, GPU-wait buffer release,
  U/V tessellation-factor getters (doubled in split-screen), and per-axis
  tess-enable flag accessors. 9 more renames (85 total this rendering thread).
  **574 renames** (571 functions + 3 data). Live metric: 845/5,249 (≈16.1%).
- **Pivoted to a fresh system: terrain collision** (the core snowboarding
  mechanic). Traced the `TerrainGrid` singleton out from
  `Rider_ComputeTerrainCellIndex` and named the query cluster
  (`Terrain_QuerySurfaceContact` grid-broad→AABB→triangle-narrow phase,
  `Terrain_SampleHeightAt`, `Terrain_GetCellRangeForBounds`,
  `Collision_TestAABBOverlapWithMargin`) plus the rider-side contact-cell
  tracking (4 functions) and the `TerrainGrid` data label. 9 renames (8 fn +
  1 data). **583 renames** (579 functions + 4 data). Live metric: 853/5,249
  (≈16.25%). New notes file `RE_NOTES_terrain_collision.md`.
- **Chased the TerrainGrid builder → found the level-asset loader**:
  `Level_LoadTrackAssets` (called from `FEInit_Boot` + `InGameState_LoadLevel`)
  builds `|data/models/<track><suffix>` paths and loads the track's full asset
  set — the suffixes reveal the file types, including **`.ltg` (level terrain
  geometry, the collision-mesh source for `TerrainGrid`)**. Also named
  `TrackInfo_GetRecordByIndex` (0x84-stride track table) and
  `Terrain_UpdateObjectVisibility` (per-frame terrain render culling). 3 more
  renames. **586 renames** (582 functions + 4 data). Live metric: 856/5,249
  (≈16.31%).
- **Decoded the `.ltg` terrain-geometry file format** (data-format finding, no
  renames) — extracted `gari.ltg` from `gari.big` and confirmed its header maps
  *exactly* onto the runtime `TerrainGrid` struct (file `+0x2c`/`+0x30`/`+0x48`
  = grid width/height/sub-objs, same offsets). Format: 0x4c header (world bbox,
  cell size, W×H grid, sub-division) + W*H cell-offset table + per-cell triangle
  data. For gari: 23×31=713 cells, 243 populated tracing the run down the
  mountain. New tool `scripts\classify_ltg.py`. Closes the terrain-collision
  loop end-to-end (on-disc `.ltg` → `Level_LoadTrackAssets` → `TerrainGrid` →
  query functions). **586 renames** unchanged; **19 RE_NOTES files, 12 tools (added classify_ltg.py).**
- **Decoded the `.ltg` per-cell triangle records** (completing the format): each
  cell is a sequence of 76-byte collision-triangle records — u32 surface/material
  id (steps by 8 per patch) + 3 float3 vertices tiling the 2500-unit sub-cell
  grid + a flags field. Extended `classify_ltg.py` with a triangle dumper
  (`... tris N`). The `.ltg` format is now fully decoded from header to
  individual collision triangles. Open follow-up: mapping surface-id ranges to
  snow/ice/rail surface types (a link to rider surface physics). **586 renames**
  unchanged.
- **Mapped the rider physics-mode state machine** (the terrain-contact
  consumer): `Rider_DispatchPhysicsMode` switches on `rider+0x484` to select 1
  of 6 movement/collision models — modes 1-3 are terrain/ground states
  (`Rider_PhysicsMode1_GroundRide`, `Mode2_GroundContact`,
  `Rider_ResolveTerrainContactPhysics` = the board-on-snow contact-response
  core), mode 4 no-op, modes 5-6 non-terrain (airborne/rail — exact identity
  left un-claimed). 6 renames. **592 renames** (588 functions + 4 data). Live
  metric: 862/5,249 (≈16.42%).
- **Corrected a real mis-naming error from an earlier session** (opus,
  careful pass). The earlier-"verified" claim that `Rider_ScalarDeletingDestructor`
  calls `Rider_BaseDestructor`→`Rider_TeardownSubobjects` was wrong: the
  genuine destructor (`0x36920`) actually calls `Rider_DestructUnlinkFromRegistry`
  (`0x36970`) + the real `Rider_TeardownSubobjects` (`0x31f10`). The function
  formerly called "Rider_TeardownSubobjects" (`0x36990`) is a 233-line
  per-frame physics/state update (zero teardown calls) reached via a vtable
  adjustor thunk and shared across ~28 component classes — renamed
  `Rider_UpdatePhysicsState`; its thunk (`0x370f0`) and Player's equivalent
  (`0x5be60`) were the same mislabel, renamed `*_Thunk` /
  `Player_UpdatePhysicsState`. Independently confirmed by
  `Player_ScalarDeletingDestructor` calling the real `0x31f10` teardown. 2 net
  new names + 3 corrections, old entries preserved commented-out per
  convention; all 7 verified live; cross-references fixed across 4 RE_NOTES
  files. **594 renames** (590 functions + 4 data). Live metric: 864/5,249
  (≈16.46%). A reminder that "verified" in an earlier note isn't a guarantee —
  reading each function's actual body caught it.
- **Mapped the rider physics-component vtable** (the `0x00187c8c` family
  `Rider_UpdatePhysicsState` sits in): confirmed it's one shared vtable
  embedded per rider slot (byte-identical function pointers across all ~28
  table entries, only tuning constants differ). Named `Rider_ResetPhysicsState`
  (construct/reset, slot 1) and `Rider_HandleComponentStateEvent` (slot 3,
  confirmed as the exact function `Component_UpdateAll` calls at vtable+0xc —
  an event-code dispatcher with edge-triggered state-bitmask reactions). 2
  renames. **596 renames** (592 functions + 4 data). Live metric: 866/5,262
  (≈16.46%).
- **Investigated `Rider_HandleComponentStateEvent`'s handler cluster** (event
  codes 0xc/0x13/0x14/0x15) — found a real probability-gated message-post
  pattern, formed a "commentary system" hypothesis from an adjacent
  `"WorldTriggerManager"` tag string, then **checked and ruled it out**
  (the message-post function doesn't touch `WorldTriggerManager`'s singleton
  or any of its methods). Deliberately left unnamed rather than guess — full
  investigation documented in `RE_NOTES_terrain_collision.md`. No renames.
- **Found two fresh rendering systems** by reading the data block after
  `GfxContext`'s vtable: confirmed `GfxContext`'s real class name is
  `XBoxGraphicsMan` (tag-traced), and found **`LightManager`** (tag
  `"LightMan"`, a full light-object pool: allocate/free/index + ambient/point
  light submission, 10 methods) and **`LensFX`** (tag `"LensFX"`, lens-flare
  system, constructor pair). 14 renames. **610 renames** (606 functions + 4
  data). Live metric: 880/5,271 (≈16.70%).
- **Fully vtable-mapped `LensFX`** (8 renames): the classic lens-flare design —
  Xbox occlusion-query readback (`Render_ReadVisibilityTestResult`, with
  blocking-poll mode) → visible-pixel-ratio intensity fades
  (`LensFX_UpdateCoreSpriteIntensity` for the 3 core sprites,
  `UpdateFlareArrayIntensity` for the secondary flare row, fog-gated) + blend
  state/viewport setup. **Also caught and corrected a same-session error
  within minutes**: `DAT_001e98c8` briefly mis-read as a separate "device
  singleton" — raw-byte disassembly of `LightManager_Construct` (`mov ecx,
  esi` before the base-ctor call) proved it's the LightManager instance
  itself → `LightManagerBase_Construct`/`Destruct`/`ScalarDeletingDestructor`
  named (+2 net), all dependent annotations fixed (incl.
  `GfxContext_WaitGPUAndCheckIdle`'s "device flush" → actually
  `LightManager_SaveLightCursor`). **620 renames** (616 functions + 4 data).
  Live metric: 890/5,278 (≈16.86%).
- **Investigated `FUN_00106930`** (5 reads of the LightManager singleton) —
  found it claims 4 light slots and submits point lights, but the decompiler
  shows 4 `unaff_` (unresolved) registers, meaning the exact parameter/light
  mapping can't be reliably determined from decompiled output. **Deliberately
  left unnamed** rather than guess. Pivoted to `LensFX`'s base vtable
  (`0x19d1b8`) instead: named **`FXNode_ScalarDeletingDestructor`** and
  confirmed the base class is `NodeBase`-derived. This also revealed that
  `FX_TrailInstance_UpdateTransform` (named in an earlier session for the
  rail-grind spark-trail system) is actually a **generic inherited `FXNode`
  method**, not trail-specific — cross-referenced in both
  `RE_NOTES_rendering_system.md` and `RE_NOTES_ubertrick_fx_cluster.md` so
  the connection isn't lost. 1 rename. **621 renames** (617 functions + 4
  data). Live metric: 891/5,278 (≈16.88%).
- **Started applying real `xbox.gdt` struct types**, following the user's
  direct instruction to work on the newly-unblocked capability. Confirmed
  live that `set_local_variable_type`/`set_function_prototype` resolve types
  across archive boundaries (retested `D3DVIEWPORT8` on
  `LensFX_SetViewportAndViewMatrix`'s param after a connection hiccup, and on
  the `D3DDevice_SetViewport` thunk's own prototype). **Found a real
  correctness gap**: reading the plugin's Java source showed `resolveDataType`
  silently falls back to `int` on an unresolved type name while still
  reporting "successfully" -- reproduced live with `D3DPRESENT_PARAMETERS`
  (genuinely missing from the built `xbox.gdt`, likely due to an unresolved
  forward-declared dependency; a local var was silently retyped to `int`,
  caught by re-decompiling, reverted to `undefined4`). Then swept every other
  `D3DDevice_SetViewport` call site and applied verified `D3DVIEWPORT8` to all
  5 remaining stack-local instances (`SceneView_RenderPass`,
  `D3DDevice_SetRenderTarget`, `SpriteBatch_FlushAndPresent`,
  `SceneRenderer_RenderFrame` x2) -- each confirmed via a follow-up decompile
  showing real struct field access (e.g. `uStack_30.X`). Checked for
  `D3DLIGHT8`/`D3DMATERIAL8`/`D3DCAPS8` opportunities -- none exist in the
  codebase, confirming lighting is entirely the custom `LightManager` system.
  Added a `set_var_type` helper to `ssx_auto_rename.py` so these type
  applications are re-runnable like the renames. No function/data renames
  this pass (type-correctness work, not naming). **621 renames unchanged.**
  Live metric: 891/5,278 (~16.88%, unchanged from before this pass). See
  `RE_NOTES_rendering_system.md` and the `reference_ghidra_mcp_connection`
  memory ("Round 7") for full detail.
- **Fully characterized `GfxContext`'s method table at `0x001a2b60`** (16
  entries, the same table `SceneRenderer_RenderFrame` is reached through).
  Found 5 of the 15 other slots pointed into never-analyzed code (no
  containing `Function` object) -- same situation as the original
  `SceneRenderer_RenderFrame` gap, but this time every address was a
  correctly-aligned function start, no boundary-scanning needed. Created all
  5 via `/create_function`, named 4 of them (plus one pre-existing-but-unnamed
  neighbor) with structural confidence: `GfxContext_ApplyUniformGammaRamp`
  (a uniform R=G=B hardware-gamma fade -- the screen fade-to-black/white
  transition effect), `GfxContext_SetViewRectAndApplyViewport`,
  `GfxContext_GetViewRect`, `GfxContext_SetOrthographicViewAndApply` (the
  2D/UI orthographic projection setup, split-screen-aware). Left 2 slots
  created-but-unnamed (`FUN_000f9b80`/`FUN_000f9df0`) -- clear table role,
  unclear semantic meaning, not guessed. Added a `createFunction(...)`
  pre-step + the 4 `rename()` calls to `ssx_auto_rename.py`, following the
  established documented-not-automated convention from the earlier
  `SceneRenderer_RenderFrame` creation. **4 renames. 625 renames** (621
  functions + 4 data). Live metric: 895/5,283 (≈16.94%). Full detail in
  `RE_NOTES_rendering_system.md`'s new "method table at 0x001a2b60, fully
  characterized" section.
- **Corrected a same-day misdiagnosis about `xbox.gdt`**. What looked like a
  missing type (`D3DPRESENT_PARAMETERS`) turned out to be a completely different
  problem: `set_local_variable_type` only ever searches the program's own
  DataTypeManager, never other open archives -- a type has to be seeded into the
  program's own DTM via a `set_function_prototype` call first (genuinely
  cross-archive) before `set_local_variable_type` can find it. Proved this with a
  diagnostic Jython script (`scripts/ssx_diag_list_open_archives.py`) that calls
  the exact same `DataTypeManagerService` GhidraMCP uses, confirming
  `D3DPRESENT_PARAMETERS` was in the original `xbox.gdt` all along. Fixed by
  applying the real, accurate `Direct3D_CreateDevice` signature (verified against
  the actual call site) -- this seeded the type and made Ghidra auto-infer real
  enum types (`D3DFORMAT`/`D3DSWAPEFFECT_DISCARD`/`BOOL`) at the call site. The
  earlier `xbox_fixed.gdt` rebuild effort (this same day, header reorder +
  archive rebuild) is now confirmed unnecessary -- harmless, but didn't fix
  anything, since there was nothing to fix in the archive. All prior docs
  claiming the type was "missing from the archive" corrected in place rather
  than silently left wrong. 1 prototype set (not counted in the renames total --
  a signature/type annotation, not a name). **625 renames unchanged.** Live
  metric unchanged. See `RE_NOTES_rendering_system.md`'s correction note and the
  `reference_ghidra_mcp_connection` memory's "Round 7 CORRECTION" for full detail.
- **Input pipeline: applied real xbox.gdt types**, the first subsystem outside
  rendering to get this treatment, using the now-corrected workflow (seed via
  `set_function_prototype` before `set_local_variable_type`). Applied real
  Xbox SDK signatures to `XInputGetState` (`HANDLE, PXINPUT_STATE`) and
  `XInputSetState` (`HANDLE, PXINPUT_FEEDBACK`) -- both verified field-by-field
  against the real struct layouts (`pState->dwPacketNumber` lands exactly on
  `XINPUT_STATE`'s real first field; `pFeedback->Header.Unknown2[0x30]`/`[0x31]`
  land exactly on `XINPUT_FEEDBACK_HEADER`'s real trailing bytes). `PXINPUT_STATE`
  auto-propagated into the caller, `Input_PollDevice`, with zero extra work.
  Checked `XInputGetCapabilities` too -- skipped, not called from any mapped
  game-side code path and its locals are a raw IOCTL buffer, not directly
  `XINPUT_CAPABILITIES`. **Confirmed operational hazard**: `set_function_prototype`
  on a library-ID-matched function (like `XInputGetState`) can pop a Ghidra
  confirmation dialog that silently blocks the HTTP call for minutes -- user
  confirmed this directly by checking the Ghidra window. 3 `set_prototype` calls
  total this session (not counted in the renames total). **625 renames unchanged.**
  See `reference_ghidra_mcp_connection` memory for the confirmed hang mechanism.
- **Cracked the "reflection-math cluster"** that `RE_NOTES_rendering_system.md`
  had left open -- turned out to be misfiled entirely. It's a shared,
  emitter-agnostic `FXParticle_*` utility library (particle transform/timing
  init, size-range setup, color-gradient setup) reached from 3 distinct
  contexts, not reflection-specific at all: `Rider_UpdateSnowSprayFX` (rider
  velocity/terrain-driven snow-spray emitter, field offsets cross-verified
  against already-documented Rider vec4s), `FXParticle_SpawnFromDescriptor`
  (a generic config-table-driven spawner, no Rider involved), and
  `SceneRenderer_RenderFrame`'s reflection block (feeding the already-named
  `FX_SpawnTrailDecal`). Ties directly into the already-documented
  `FX_TrailManager`/Ubertrick FX system. 9 renames. **634 renames** (630
  functions + 4 data). Live metric: 904/5,283 (≈17.11%). Full writeup in
  `RE_NOTES_ubertrick_fx_cluster.md`'s new "shared FXParticle_* utility
  library" section.
- **Finished the `FXParticle_*` cluster** -- the 6 remaining sibling functions
  called alongside it inside `Rider_UpdateSnowSprayFX` turned out to be the
  emission-timing half: `FXParticle_TickEmissionAndAdvance` is the master
  per-period tick (accumulate rate*duration, and once a threshold's crossed,
  step a trail position by a delta and advance an 11-bit wraparound phase
  counter), backed by accumulator set/increment, phase-randomize, delta-clear,
  and a 2-input color-gradient variant. Field-offset cross-referencing against
  the 9 functions named earlier this session pinned the whole mechanism down
  precisely -- a genuine emit-over-time trail-segment system, not a one-shot
  spawn. 6 more renames (15 total across the whole cluster). **640 renames**
  (636 functions + 4 data). Live metric: 910/5,283 (≈17.23%). Full writeup in
  `RE_NOTES_ubertrick_fx_cluster.md`.
- **Checked `Rider_UpdateSnowSprayFX`'s missing static callers** (both call and
  data-reference searches came back empty) -- consistent with this project's
  recurring vtable-dispatched-only pattern, not pursued further given
  diminishing returns. **Mapped `PixelBlit_ValidateAlignmentAndDispatch`**
  (the pixel blitter behind `LoadingScreen_BlitImageToBackBuffer`, previously
  unread): a block-compression alignment validator dispatching through 5
  format-specific handlers (formats pairwise share handlers matching real
  DXT2/3 and DXT4/5 equivalence) -- handlers created as functions but not
  individually read (likely generic Xbox texture-blit library, not game
  logic); one has a boundary-detection overlap bug not fixed this pass. 1
  rename. **641 renames** (637 functions + 4 data). Live metric: 911/5,288
  (≈17.23%).
- **Read `GfxContext`'s full vtable** (120 slots, 0x1e0 bytes) -- the largest
  never-analyzed batch found in the project so far: ~49 slots pointed into
  never-analyzed code. Bulk-created all of them (2 turned out to already be
  known functions -- `FX_SpawnTrailDecal`/`thunk_FUN_00101290`, confirming
  `GfxContext`'s vtable directly contains the already-documented
  `FX_SpawnTrailDecal`). Characterized 2 coherent clusters with strong
  field-offset evidence: a complete **texture queue/upload pipeline**
  (parse record → queue raw data + swizzle mips → register as real D3D
  resources) and the **render-state-record decoder** (`GfxContext_ApplyRenderStateRecord`,
  unpacking a compact bitfield record into the individual setter-slot calls
  already named from an earlier pass). 16 renames. ~27 slots remain unread
  (mostly a `0x00103d00-0x00104060` init-adjacent cluster). **657 renames**
  (653 functions + 4 data). Live metric: 928/5,337 (≈17.39%). Full writeup in
  `RE_NOTES_rendering_system.md`.
- **Finished the `0x00103d00-0x00104060` GfxContext-vtable cluster** -- found
  **`New_XBoxGridMesh`** (tag-confirmed "cXBoxGridMesh"), closing a
  previously-flagged open item where `/create_function` had failed at this
  address in isolation (resolved by bulk-creating the whole surrounding
  cluster together). Also named 6 getter/setter pairs at structural
  confidence. 7 renames. Left 8 sibling forwarding-wrapper functions unnamed
  but cross-referenced to the already-documented `exUT_`/`exTW_` named-record
  -queue ubertrick system from `RE_NOTES_ubertrick_fx_cluster.md`. **664
  renames** (660 functions + 4 data). Live metric: 935/5,337 (≈17.52%). Full
  writeup in `RE_NOTES_rendering_system.md`.
- **Found a genuine clip-space frustum-test family** in the
  `0x000fb0xx-0x000fb3xx` GfxContext-vtable sub-cluster:
  `GfxContext_ComputeClipOutcode` (a textbook Cohen-Sutherland outcode test),
  `GfxContext_TestAABBVisibility`/`ClassifyAABBAgainstFrustum` (8-corner AABB
  visibility classification, the standard culling algorithm),
  `GfxContext_ProjectPointToScreen` (perspective divide + screen remap), plus
  a standalone `Matrix_TransformVector4` math utility used throughout. 7
  renames. **671 renames** (667 functions + 4 data). Live metric: 942/5,337
  (≈17.65%). Full writeup in `RE_NOTES_rendering_system.md`.
- **Found the matrix-stack-2 builder family and `FX_SpawnRadialDecal`** in
  the `0x000fecxx-0x000fedxx` GfxContext-vtable sub-cluster: identity-reset/
  apply-matrix/apply-translation/apply-rotation variants (all built on a
  newly-named `Matrix_Multiply4x4` primitive), plus a large radial vertex-fan
  decal spawner tying into the `exUT_` submission path (named to match the
  already-known `FX_SpawnTrailDecal`, which sits in the same vtable). 7
  renames. **678 renames** (674 functions + 4 data). Live metric: 949/5,337
  (≈17.78%). Full writeup in `RE_NOTES_rendering_system.md`.
- **Swept the remaining scattered GfxContext-vtable singles**: found
  `GfxContext_ApplyRenderStateDelta` (a dirty-state-caching XOR-diff variant
  of the earlier render-state decoder), `GfxContext_SetActiveViewAndResetDevice`
  (calls `D3D8::D3DDevice_Reset` when switching views -- the split-screen
  view-switch mechanism), `Skin_AccumulateWeightedBoneMatrices` (a shared
  skeletal-blend utility), plus 6 more supporting renames. 9 renames. **687
  renames** (683 functions + 4 data). Live metric: 958/5,337 (≈17.95%). Full
  writeup in `RE_NOTES_rendering_system.md`.
- **Cracked an earlier session's "candidate Ubertrick/camera/track named-
  queue subsystem" bookmark**, redirecting away from the GfxContext-vtable
  sweep once it hit diminishing returns. Confirmed the bookmark's own
  flagged "top candidate" (`0x001041a0`) is `GfxContext_InitCameraModeRecords`
  -- called from `GfxContext_Init`, initializes named camera-mode presets
  (chase near/board/eyes/reverse, replay cam, scripts) via a new
  `CameraMode_InitRecord` helper. Also found `Level_GetCurrentTrackNameTag`
  (the 12-track short-name lookup companion to `TrackTable`) and confirmed
  `GfxContext_SetPerPassCallback` by tracing its consumer
  (`SceneRenderer_RenderPassRange`'s conditional per-pass callback). Crossed
  18%. 4 renames. **691 renames** (687 functions + 4 data). Live metric:
  962/5,337 (≈18.03%). Full writeup in `RE_NOTES_rendering_system.md`.
- **Pushed into the deferred `FUN_00102850`** rather than leaving it open --
  found `Text_RenderGlyphString`, the core glyph-by-glyph string rasterizer
  (calls `Font_GetGlyphMetrics` directly, builds a perspective-projected
  textured quad per character). This retroactively confirms the `exUT_`/
  `mesablan`-tagged fields seen throughout this whole investigation were
  never a real named-queue system -- just fixed-offset scratch storage
  aliasing unrelated string literals. Named 2 of its wrapper variants
  precisely (byte/wide string mode). 3 renames. **694 renames** (690
  functions + 4 data). Live metric: 965/5,337 (≈18.08%). Full writeup in
  `RE_NOTES_rendering_system.md`.
- **Closed out the entire exUT_-tagged render-submission family** (17
  renames in one pass): `GfxContext_SubmitColoredQuad`/`SubmitScreenRect`
  (generic 2D primitives), `Text_RenderGlyphStringScreenSpace` (the
  screen-space sibling of `Text_RenderGlyphString`), `FX_SubmitParticleBatch`/
  `WithFog` (batch vertex submission via a second growing buffer), all 6
  remaining forwarding wrappers, plus 5 more scattered `GfxContext`-vtable
  getters (`GfxContext_InitDefaultRenderStateRecord` confirmed as the
  constructor for the canned state block `ApplyDefaultRenderStateBatch`
  reads). Checked the 4 remaining `PixelBlit_ValidateAlignmentAndDispatch`
  handlers -- confirmed genuinely not worth naming (unreliable register-
  based parameter routing, same class of case as `FUN_00106930`). The
  `GfxContext` vtable thread is functionally closed -- 6 genuinely stuck
  singles remain, real diminishing returns. **711 renames** (707 functions +
  4 data). Live metric: 982/5,337 (≈18.40%). Full writeup in
  `RE_NOTES_rendering_system.md`.
- **Extended the input-pipeline xbox.gdt type work** (started earlier this
  session with `XInputGetState`/`SetState`): applied real signatures to
  `XGetDevices`/`XInputOpen`/`XInputGetCapabilities`, and real types
  (`PXINPUT_FEEDBACK`/`XINPUT_CAPABILITIES`) to the specific variables that
  hold them. Named `GamepadInputDevice_InitVibrationCapabilities` (tag-
  confirmed via a 0x46-byte "XInputFeedback" allocation and a 25-byte
  `XINPUT_CAPABILITIES`-sized probe buffer). `Input_PollDevice` and
  `GamepadInputDevice_Construct` now decompile with genuine struct field
  access throughout. **New nuance confirmed**: the endpoint's "Type not
  found directly" message is not a reliable success/failure signal by
  itself in either direction -- one application succeeded genuinely despite
  that message; only re-decompiling ever tells the truth. Also traced
  `FX_SpawnRadialDecal`'s callers -- only a data (vtable) reference exists,
  no static dispatch site found; left as a documented dead end rather than
  chased further. 1 rename + 4 prototype/type applications. **712 renames**
  (708 functions + 4 data). Live metric: 983/5,337 (≈18.42%).
- **Found the `.cml` camera-script runtime interpreter**, closing the last
  gap an earlier session explicitly left open. Traced forward from this
  session's own `GfxContext_InitCameraModeRecords` find (the "chase near"
  camera-mode name records) into the already-documented `VenueStaging`
  system: `VenueStaging_SetActiveCameraMode` writes the exact field
  `VenueStaging_ExitState` compares against, `VenueStaging_Tick` is the
  master per-frame driver, and `VenueStaging_TickCameraScriptCommand` is the
  actual 43-opcode `.cml` keyframe interpreter (opcode 0x25 fires
  `CameraScript_%d` level scripts via the already-named `Script_PlayByName`).
  On-disc format -> field layout -> runtime execution is now a fully closed
  loop. 4 renames. **716 renames** (712 functions + 4 data). Live metric:
  987/5,337 (≈18.49%). Full writeup in `RE_NOTES_camera_system.md`.
- **Pushed one level deeper into the camera-script opcode handlers**: found
  `VenueStaging_BlendToCameraModeEased` (a proper eased transition, not an
  instant cut, selecting ease-in/out/in-out curves), `VenueStaging_SetPendingExitCameraState`
  (opcode 4's exit-transition handler), and `VenueStaging_InterpolateCameraModes`
  (the underlying blend implementation). 3 renames. **719 renames** (715
  functions + 4 data). Live metric: 990/5,337 (≈18.55%). Full writeup in
  `RE_NOTES_camera_system.md`.
- **Made partial progress on `SaveGame` record internals** (a long-standing
  open item): confirmed state 3's actual worker function only stages+
  checksums each 3620-byte record rather than building it, and found the
  record count's source (the current level/track object) -- a real lead
  that records are likely per-checkpoint/segment. The record content's
  actual populating code wasn't found (genuinely hard -- similar difficulty
  to the trick-score-writer search), documented honestly rather than
  guessed. 1 rename. **720 renames** (716 functions + 4 data). Live metric:
  991/5,337 (≈18.57%).
- **Attempted the remaining 12 unidentified `OverlayManager` panels** (7 of
  19 were named in an earlier session via `Localization_ResolveString` ID
  lookups). Read `OverlayManager_Construct` fully and identified exactly
  which of the 19 allocation slots are still unnamed and grouped them
  structurally: 3 large, near-identical sibling constructors
  (`FUN_000ceb60`/`cecd0`/`cee50`, likely a common panel family sharing a
  fixed `0x1b9` setup constant), 4 small simple panels sharing an identical
  construction pattern (`param_1[8]`/`[0xb]`/`[0xc]`/`[0x10]`, reading the
  same 3 fields off the current-level object -- likely simple screen-space
  icons/indicators, not full menu screens), and one large 20-item grid-
  layout generator (`FUN_000c7f20`, 0x5f0-byte panel). Checked for embedded
  tag strings after their vtables (the pattern that worked for `GfxContext`/
  `LightManager`/`LensFX` earlier this session) -- none found, just more
  vtable pointer data. Confirming their exact identity would need tracing
  each panel's Draw/Tick vtable slot (not construction) for
  `Localization_ResolveString` ID calls -- a real, tractable path in
  principle, but a much larger per-panel effort than the earlier 6 that
  were found this way. Not pursued further this pass; no renames applied
  (correctly left unnamed rather than guessed). Checked one of the 4 small panels' vtable directly (7 slots: 1 real destructor, 6 falling back to the shared RaceState_NullHandler no-op) -- confirmed these panels have essentially no unique per-frame logic to extract signal from, genuinely exhausting this angle rather than a search gap.
- **Closed the "shader-table management" mystery** -- a genuinely old open
  item (`this+0xc0` shader-handle pointer read by `SpriteBatch_FlushAndPresent`,
  never traced to its owning class). Required raw disassembly, not
  decompiled C: `ErrorScreen_RenderDiscReadError` and its caller both take an
  implicit `this` parameter Ghidra's decompiler completely hid, showing them
  as parameterless. Found the real caller in a never-analyzed region via a
  backward RET+NOP-padding boundary scan. **Result: no general shader-table
  class exists** -- it's the disc-error screen's own dedicated, self-
  contained emergency font resource (`"ABORTFONT"`/`"Excpt Glyph"`/
  `"FontTemp"` tags), triggered by an infinite freeze loop matching real
  Xbox retail disc-error behavior. 3 renames + 1 data rename. **724 renames**
  (719 functions + 5 data). Live metric: 994/5,339 (≈18.62%). Full writeup
  in `RE_NOTES_rendering_system.md`.
- **Closed the last remaining rendering "Next steps" item**: the mesh
  draw-mode table's "missing per-frame dispatcher" turned out to never have
  existed as a separate function. Traced `SceneView_RenderPass`'s actual
  dispatch and `MeshQueue_InsertPrimaryBucket`'s enqueue step -- neither
  does a runtime `table[id]` lookup. The real mechanism was already sitting
  in the existing `GfxContext_Init` documentation: each render-command pool
  is pre-seeded with its own dedicated default handler *once*, at startup.
  The original open item's premise (assuming a hidden per-frame dispatcher
  must exist) was the actual mistake. No renames -- a documentation
  correction, not a new discovery, but closes `RE_NOTES_rendering_system.md`'s
  "Next steps" list completely. Live metric unchanged: 994/5,339 (≈18.62%).
- **Closed out the `AggressionManager` accessor helpers** (a flagged
  next-step item from `RE_NOTES_application_boot.md`): the 12x12 relationship
  matrix's 16-byte cells decode to a level byte + a decay-mode byte + 2
  fields with unpinned semantics; found a genuine relaxation mechanic
  (`AggressionManager_DecayRelationshipLevels`, relationships drift back to
  baseline over time) and the category-index resolver
  (`AggressionManager_FindCategoryIndex`, Buddy/Friend/Rival/Enemy tiers).
  Confirmed the whole system refreshes once per race start, not per-frame.
  **Crossed 1,000 named functions.** The actual banter-trigger consumer
  wasn't found this pass (plausible lead: `SpeechManager`, not confirmed).
  6 renames. **730 renames** (725 functions + 5 data). Live metric:
  1,000/5,339 (≈18.73%). Full writeup in `RE_NOTES_application_boot.md`.
- **Major structural break on the trick-score-writer mystery**: raw
  disassembly tracing (the same technique that cracked the shader-table
  mystery) revealed `Component_UpdateAll` operates on 3 fixed component-list
  heads embedded directly in the Rider object (`+0x28`/`+0x80`/`+0xd8`,
  `0x58` bytes apart), each a `NodeBase`-style circular list sentinel --
  definitively answering "whose list is it" for the first time across every
  session that's investigated this. The actual component types (and thus,
  potentially, the score-writer itself) weren't found this pass -- the
  insertion call sites for the 3 specific list-head addresses are now the
  concrete next step. No renames (a structural finding, not new code
  identification). Live metric unchanged: 1,000/5,339 (≈18.73%). Full trace
  in `RE_NOTES_rider_update_chain.md`.
- **Found the component-slot constructor**, narrowing the trick-score-writer
  search further: `Rider_ConstructComponentSlots` builds the 3 fixed
  component slots (each set to the shared `Node_NoOpStub1` placeholder
  vtable -- inert at construction, not specialized), and
  `Component_InitEmptyList` confirms the exact field layout (`+0x50` =
  first-element pointer, `+0x24` = each node's own "next"). The actual
  insertion call sites (which would reveal the real component types) still
  weren't found, but the search is now precisely targeted instead of a
  blind offset sweep. A real tie-in to `BdrSeq_InitEventCodeTable`/`RiderEvent`
  was found but turned out to be an unrelated global table, not the
  insertion mechanism itself. 2 renames. **732 renames** (727 functions + 5
  data). Live metric: 1,002/5,339 (≈18.77%). Full trace in
  `RE_NOTES_rider_update_chain.md`.
- **Found the actual node-insertion function for the trick-score-writer
  thread**: traced `RiderAnimation_TriggerByEventCode` -> `New_BdrSeq` ->
  `BdrSeq_ConfigurePlaybackFromEventCode` -> `Component_InsertNode` (was
  `FUN_00107ac0`, confirmed via raw disassembly), a genuine doubly-linked-
  list push-front -- the first confirmed "attach a real gameplay object to
  a Rider's component list" operation in this project. Honest caveat: its
  sentinel field is `+0x28`, not the `+0x50` field found earlier for the
  same slots -- not yet reconciled, documented precisely rather than
  papered over. 1 rename. **733 renames** (728 functions + 5 data). Live
  metric: 1,003/5,340 (≈18.78%). Full trace in
  `RE_NOTES_rider_update_chain.md`.
- **`+0x28` vs `+0x50` fully RESOLVED**: traced a second path into the same
  slot struct via `Rider_ResetSubsystemBuffers` -> `ComponentSlot_ResetAndReseedBdrSeq`
  (was `FUN_00066c80`) -> `New_BdrSeq_2`, whose insertion logic is byte-for-byte
  identical to `Component_InsertNode`'s. Reconciled by re-reading
  `Component_InitEmptyList` (already verified live): it sets FOUR fields off
  one identical `this` in a single call -- `+0x24`/`+0x28` (a dummy sentinel
  at `+0x2c`) AND `+0x50`/`+0x54` (self-pointing) -- proving these are two
  genuinely separate, independently-maintained lists per 0x58-byte slot, not
  one list at two relative bases. `+0x28` = the BdrSeq animation-event queue
  (insert side); `+0x50` = the active-component list `Component_UpdateAll`
  walks every frame (traversal side). Both sides of the mechanism are now
  fully understood; the remaining open question is narrower: what real
  object type gets attached to the `+0x50` list and what vtable+0xc does.
  1 rename. **734 renames** (729 functions + 5 data). Live metric:
  1,004/5,340 (≈18.80%). Full trace in `RE_NOTES_rider_update_chain.md`.
- **Corroborating find, same thread**: the small utility cluster right next to
  `Component_InsertNode`/`Component_InitEmptyList` (`0x00107b00`-`0x00107b50`)
  turned out to be entirely a `+0x28`-queue API — `BdrSeqQueue_Count` (whose
  only caller, confirmed via `xrefs_to`, is `Component_UpdateAll` itself —
  purely for a debug-profiler count marker, unrelated to its own separate
  `+0x50` walk), `BdrSeqQueue_RemoveAndDestroyNode` (9 callers, per-event-type
  handlers, not individually traced), and `BdrSeqQueue_DestroyAllAndReset`
  (walks+destroys the `+0x28` queue, then resets BOTH sub-lists in one call —
  direct proof both lists share a single reset entry point despite being
  independently maintained). Further confirms `+0x28`/`+0x50` are genuinely
  separate mechanisms, not a naming coincidence. 3 renames. **737 renames**
  (732 functions + 5 data). Live metric: 1,007/5,340 (≈18.86%). Full trace in
  `RE_NOTES_rider_update_chain.md`.
- **Three more search angles for the `+0x50` inserter (utility-cluster
  proximity, tag-string guessing, an opcode sweep for `MOV [reg+0x50],reg`)
  all came up empty** — checked and documented honestly as exhausted for
  this session, with a real possibility `+0x50` is simply never populated
  in this retail build (consistent with this project's repeated finding of
  inert extension points). **Pivoted to the HUD side of the same mystery**
  (who pushes into `rider+0x5784`'s tagged-event array, read by the HUD's
  score-popup system) and closed it fully via a much higher-signal
  raw-bytes search for the literal `0x5780`/`0x5784` displacement (only 9
  hits total in the whole binary): found and named
  **`HUD_TickRiderDisplayState`** (was `FUN_000c3da0`) — a large,
  previously completely unknown per-frame function that ticks all 4
  riders' HUD display state (score-tier bucketing, timer/position
  smoothing) and reads `+0x5784`'s tagged events, but **never writes to
  it** — and its panel-mask selector **`HUD_SelectActivePanelMask`** (was
  `FUN_000c38f0`). This conclusively rules out the entire HUD side as the
  writer. Also found the unrelated **`OptionsMenu_CacheDisplaySettingsFromWidgets`**
  (was `FUN_0009e1d0`, a false-positive lead from the same search, kept for
  its own value — confirms the km/h/mph units flag's source). 3 renames.
  **740 renames** (735 functions + 5 data). Live metric: 1,010/5,340
  (≈18.91%). Full trace in `RE_NOTES_race_hud.md`.
- **Different subsystem (per explicit user request): found the game's actual
  main-loop/frame-pump mechanism for the first time in this project's
  history.** Picked up `RE_NOTES_control_scheme.md`'s flagged "InputCache
  per-frame consumer not located" lead. Traced the full chain:
  `InputManager_PollDevicesIntoCache` (the per-frame device-poll-into-ring-
  buffer producer, confirmed via multiple real callers including the
  already-named `VideoPlayer_UpdateSkipInput`), `Input_CatchUpPollAndTick`,
  `Application_RunInitialLoadPump` (a one-time boot pump, not the perpetual
  loop), `Application_TickFrame` (the real per-frame update), and
  **`Application_FrameTimerCallback`** — confirmed to be a self-rescheduling
  `XAPILIB::timeSetEvent` callback, not a `while(1)` loop anywhere in the
  call graph. Created 2 new function boundaries
  (`Application_FrameTimerCallback_StdcallThunk`/`Application_ArmFrameTimer`)
  where none existed. Honest caveat preserved: `Application_ArmFrameTimer`'s
  vtable-slot identity (`XBoxExecutionMan`) doesn't fully reconcile with
  `Application_TickFrame`'s traced `this` (`DAT_001e3c7c`, confirmed = the
  `Application` object) despite matching field layouts — documented
  precisely rather than forced. 7 renames. **747 renames** (742 functions +
  5 data). Live metric: 1,017/5,342 (≈19.04%, denominator +2 from the 2
  newly-created functions). Full trace in `RE_NOTES_control_scheme.md` and
  `RE_NOTES_application_boot.md`.
- **Resolved the honest caveat from the entry above, same session**: re-read
  `Application_FrameTimerCallback`'s raw disassembly with full precision and
  found there's no real conflict -- its own `this` is consistently the
  `XBoxExecutionMan` singleton throughout (exit flag, last-tick, accumulator,
  frame-time-target fields all confirmed), and it only switches to the
  separate `Application` object (`[DAT_001e3c7c]`) for the single call into
  `Application_TickFrame`, switching straight back afterward. Two objects,
  two cleanly separated responsibilities, no aliasing needed. Found and
  named `XBoxExecutionMan`'s remaining vtable slots (2/3/4 -- previously
  un-analyzed code with no `Function` object at all): `XBoxExecutionMan_Shutdown`,
  `XBoxExecutionMan_WaitForFrameEvent`, and **`XBoxExecutionMan_SignalFrameEvent`**
  (the latter is exactly what `Application_TickFrame`'s mystery final per-tick
  dispatch calls, closing that loop too -- every game frame ends with a
  `SetEvent` on a Win32 event handle, plausibly a frame-sync primitive for a
  secondary thread). Also named `XBoxExecutionMan_Construct`. 5 renames (2
  more newly-created function boundaries). **751 renames** (746 functions +
  5 data). Live metric: 1,021/5,344 (≈19.10%). Full trace in
  `RE_NOTES_control_scheme.md`.
- **Different subsystem again (per "go another direction, full throttle"):
  found the real per-frame consumer of the BdrSeq animation-event queue**
  (the insert side was found earlier this session, the read/consume side was
  not). Traced from `InGameState_LoadLevel`'s `"PREAI"`/`"PostAI"` tagged
  allocations (real `NodeRegistry` types 5/0xb, previously unexplored) to
  their own `Update` methods (found in completely un-analyzed code) --
  **`RiderAnimEvents_ProcessTriggeredQueue`** (activates newly-triggered
  events per rider, dispatching to an 8-case per-type handler) and
  **`RiderAnimEvents_TickActiveQueue`** (ticks every active event per rider
  every frame, dispatching to a 12-case per-type handler, one of which --
  `BdrSeqEvent_SelectDirectionalAnimClip` -- does real stick-direction-based
  trick-animation selection). Also found a genuinely new discovery:
  **`AudioSystem_DispatchAnimationCueEvent`**, fired per-phase-flag during
  animation playback (finer-grained than the already-known per-trick-
  completion audio system). This closes the BdrSeq queue's full lifecycle
  (insert/activate/tick/destroy) but leaves one honest open thread: what
  calls `PREAI`/`PostAI`'s own `Update` every frame is still not found
  (`NodeRegistry_UpdateAllOfType`'s only callers are for unrelated types
  3/4). 11 renames, 2-3 newly-created function boundaries. **761 renames**
  (756 functions + 5 data). Live metric: 1,032/5,347 (≈19.30%). Full trace
  in `RE_NOTES_rider_update_chain.md`.
- **Half-resolved the "what calls PREAI/PostAI" open thread**: read
  `GameState_ResetTransientTriggerNodes` again and found it has a SECOND
  dispatch mechanism besides `NodeRegistry_UpdateAllOfType` -- a raw
  bucket-walk loop over `DAT_0019b994`, which turned out to be a flat
  array of 4 literal type IDs (`{2, 5, 7, 8}`), calling the real `Update`
  vtable slot on every node of each. **Type 5 is `PREAI`** -- so it does
  have a confirmed call site after all, just not through the generic
  dispatcher. Type `0xb` (`PostAI`) is still not in any confirmed dispatch
  list found so far. No renames (a structural clarification, not new code
  identification). Full trace in `RE_NOTES_rider_update_chain.md`.
- **Exhaustively closed the PostAI question**: found and checked every
  single bucket-walk call site in the entire binary (9 sites, 5 distinct
  functions, via `xrefs_to` on `NodeRegistry_PeekHead` -- no more exist).
  Type `0xb` appears in none of them. `RiderAnimEvents_TickActiveQueue`
  (PostAI) is either reached exclusively via a computed call or genuinely
  dead in retail -- a complete, exhaustive negative result, not a partial
  one. Static analysis is maxed out on this specific question. Full trace
  in `RE_NOTES_rider_update_chain.md`.
- **Self-correction, same session ("keep going")**: caught and fixed a real
  naming mistake from the entry above. Traced where the `+0x22` flag
  (checked by the "activation" cluster) actually gets *set* -- BdrSeq's own
  real per-frame tick method (its vtable slot 2, previously un-analyzed,
  named `BdrSeq_TickPlayback`) calls a generic playback-timer utility
  (`AnimTimer_AdvanceAndDetectCompletion`, decompiled earlier this session
  but not renamed at the time) that sets `+0x22=1` when an animation
  *finishes* playing -- not when one is newly queued. So the whole cluster
  is a completion/phase-transition dispatcher, not an activation one.
  Corrected 4 names (`RiderAnimEvents_ProcessCompletedQueue`/
  `Rider_TriggerAnimCompletionPass`/`RiderAnimQueue_ProcessCompletedEntries`/
  `BdrSeqEvent_DispatchCompletionByType`), old ones kept commented out per
  project convention. Also clarified the 8 per-type handlers: each tears
  down the finished node then (all but type 0) chains immediately to a new
  animation phase via `RiderAnimation_TriggerByEventCode` -- a genuine
  multi-phase animation state machine. 6 renames (2 new + 4 corrected).
  **763 renames** (758 functions + 5 data). Live metric: 1,034/5,347
  (≈19.34%). Full trace in `RE_NOTES_rider_update_chain.md`.
- **Finished the per-type tick-handler sweep**: read all 7 remaining
  `BdrSeqEvent_DispatchTickByType` handlers (types 5-11), each doing
  real trick-animation clip selection/blending from rider-state fields
  (angle, tilt, 2D stick vectors via an atan2-shaped helper). Named by
  verified input shape rather than guessed trick name, per this project's
  `UnknownOpcode09`-style honesty precedent. Also found
  **`BdrSeq_AdvanceBlendTimersAndDetectCompletion`** (was `FUN_000604b0`),
  an even more central completion-detector than the one found in the
  correction above -- advances every active blend channel on a node and
  sets the same `+0x22` flag when the last channel's timer completes,
  independently confirming the corrected understanding. This fully
  completes the per-type tick-handler sweep -- every case in
  `BdrSeqEvent_DispatchTickByType` is now named. 8 renames. **771 renames**
  (766 functions + 5 data). Live metric: 1,042/5,347 (≈19.49%). Full trace
  in `RE_NOTES_rider_update_chain.md`.
- **New direction: `SnowFallMan`, the falling-snow weather-effect system**
  (new `RE_NOTES_weather_effects.md`). Previously only mentioned in passing
  as one tag string in `InGameState_LoadLevel`'s allocation list -- never
  itself explored. Turned out to be a genuinely substantial, self-contained
  8-emitter snow-particle system with camera-relative scroll-position
  tracking and its own D3D render-state-managed draw call. Also closed a
  small piece of the earlier `{2,5,7,8}` type-ID investigation: **type 2 is
  `SnowFallMan`** (confirmed via the same `NodeRegistry_Insert` wrapper
  `PREAI`/`PostAI` use) -- but its own `NodeBase` Update slot resolves to
  the shared no-op stub, so the real logic runs through 2 other vtable
  slots invoked some other way, per-frame caller not found (same honest
  state as several other systems this project has mapped). 8 renames.
  **779 renames** (774 functions + 5 data). Live metric: 1,050/5,347
  (≈19.64%). Full trace in `RE_NOTES_weather_effects.md`.
- **Continued the same direction: `LessonMan`, the tutorial/lesson-mode
  system** (new `RE_NOTES_tutorial_system.md`). Only constructed when
  `GameMode_Current==6`. A genuine lesson-step state machine (enter/exit/
  tick per numbered lesson step) plus its own fullscreen overlay renderer,
  with a rich constructor that loads 30 real controller-button icon
  textures and registers them keyed by the player's actual current control
  scheme (tying into the input-pipeline bitmasks from
  `RE_NOTES_control_scheme.md`). Same honest pattern as `SnowFallMan`:
  `NodeRegistry` type 6, generic Update slot is a no-op, real logic in 2
  custom vtable slots with no static caller found -- now confirmed as a
  **second instance** of this exact architecture, suggesting a shared,
  not-yet-found "tick+render optional InGameState subsystems" loop. 5
  renames. **784 renames** (779 functions + 5 data). Live metric:
  1,055/5,349 (≈19.72%). Full trace in `RE_NOTES_tutorial_system.md`.
- **Continued the same direction: `PowerFX Particles`** (new
  `RE_NOTES_powerfx_particles.md`) -- a 12-category particle pool, third
  confirmed instance of the same architecture. This time found a genuine
  static caller chain, and it connects directly back to this session's very
  first thread: `PowerFXParticles_ActivateCategory`'s only 2 callers are
  inside one of the 15 `Rider_UpdatePhysicsState` sub-calls flagged
  unexplored during the original trick-score-writer push. Opened it and
  named it **`Rider_UpdateUberTrickGlowFX`** (moderate confidence) --
  activates/deactivates two particle-glow points tracking limb/board
  positions, very plausibly the Uber Trick charge visual. No score-field
  reference found in it either, but this closes one item off the 15-function
  unexplored list (14 remain). 7 renames. **791 renames** (786 functions + 5
  data). Live metric: 1,062/5,350 (≈19.85%). Full trace in
  `RE_NOTES_powerfx_particles.md`.
- **Major find: rider-vs-rider collision system.** Kept pushing on the 15
  flagged `Rider_UpdatePhysicsState` sub-calls. 5 were trivial generic math
  utilities (`Math_Atan2`/`Vector4_DotProduct`/`Math_ClampTowardTargetWithMargin`/
  `Vector4_Scale`/`Rider_AccumulateStateTimer`) plus a shared pause/loading
  gate (`GameState_ShouldSkipGameplayTick`). The largest one (303 lines)
  turned out to be a previously **completely unexplored, substantial
  system**: **`Rider_ProcessCollisionsWithOthers`** -- real rider-vs-rider
  collision detection (a proximity scan + a genuine broad-phase collision-
  pair binary tree) and elastic-collision-shaped physics response
  (`Rider_ApplyCollisionImpulse`), with a collision counter that ties into
  the already-named `Rider_UpdateCueTimer`. **Genuine cross-system
  connection**: the collision-eligibility gate
  (`ComponentSlot_ResolveCategoryFromType`/`ComponentSlot_CheckCategoryFlag`)
  reads through the exact same shared lookup table this session's
  `BdrSeqEvent` dispatch tables use -- collision eligibility is gated by the
  same per-slot component-category system the trick-animation thread
  mapped. Still not the score writer, but the most substantial single find
  in this whole 15-function sweep. 9 renames. 8 of 15 flagged functions now
  opened (7 remain). **801 renames** (796 functions + 5 data). Live metric:
  1,072/5,350 (≈20.04% -- crossed 20%). Full trace in
  `RE_NOTES_rider_update_chain.md`.
- **Closed 3 more of the 15-function list**: `Rider_SelectLocomotionAnimState`
  (high confidence -- a clean directional-animation selector tied to the
  already-named `RiderEvent_GetSubState`/`RiderAnimation_TriggerByEventCode`),
  `Rider_UpdateSpeedIntensityFX` and `Rider_AccumulateCameraShakeInputs`
  (both moderate confidence). **11 of the original 15 flagged functions are
  now opened, 4 remain** (checked but not confidently understood: a
  decay-countdown pattern, two terrain/path-query-shaped functions, and a
  one-time checkpoint-cache-build loop). 3 renames. **804 renames** (799
  functions + 5 data). Live metric: 1,075/5,350 (≈20.09%). Full trace in
  `RE_NOTES_rider_update_chain.md`.
- **Fresh direction again: `TerrainNode`** (added to
  `RE_NOTES_terrain_collision.md`) -- fourth confirmed instance of the
  "NodeBase-derived InGameState subsystem, no-op generic slots, real logic
  elsewhere" architecture, but this one plugs directly into the
  already-documented terrain-collision system: its real per-frame tick
  calls the already-named `Terrain_UpdateObjectVisibility`/
  `Terrain_GetCellRangeForBounds`/`TrackSegment_GetByIndex` to drive
  per-frame visibility culling and track-side prop LOD. Same "no static
  caller found" status as the other 3 instances. 4 renames. **808 renames**
  (803 functions + 5 data). Live metric: 1,079/5,351 (≈20.17%). Full trace
  in `RE_NOTES_terrain_collision.md`.
- **Fresh direction: `VideoStreamMan`** (added to `RE_NOTES_rendering_system.md`,
  next to the existing `VideoPlayer` section) -- fifth confirmed instance of
  the recurring "NodeBase-derived InGameState subsystem" architecture, but a
  genuinely new subsystem: a small pool of simultaneous in-game video-texture
  streams (arena jumbotron/billboard style), built on the exact same
  low-level decode API as the already-documented full-screen `VideoPlayer`
  FMV class. Found 2 more unnamed `VideoPlayer`-adjacent functions as a
  side lead, not chased further. 5 renames. **813 renames** (808 functions
  + 5 data). Live metric: 1,084/5,353 (≈20.25%). Full trace in
  `RE_NOTES_rendering_system.md`.
- **Followed up the VideoStreamMan side-lead**: the 2 unnamed functions next
  to `VideoPlayer_IsStopped` turned out to be real, heavily-used internal
  `VideoPlayer` helpers -- confirmed via `xrefs_to` from the already-named
  `VideoPlayer_Tick`/`VideoPlayer_Open`. `VideoPlayer_FlushPendingPackets`
  (drains/discards pending decode packets) and
  `VideoPlayer_FindNextChunkByMagic` (finds the next packet matching a 4-byte
  `"MPCh"` magic marker, discarding the rest -- called 6 times total, a core
  primitive not a rare edge case). 2 renames. **815 renames** (810 functions
  + 5 data). Live metric: 1,086/5,353 (≈20.29%). Added to
  `RE_NOTES_rendering_system.md`.
- **New: a real developer debug menu that shipped in retail** (new
  `RE_NOTES_debug_menu.md`). `"DebugMenu"` -- unconditionally constructed,
  0x3864 bytes, with real human-readable strings still in the binary
  ("BX Debug Menu", "Instant Replay", "Restart Race", "Render/Game/Sound
  Options", "Exit the Game"). The richest instance of this session's
  recurring architecture -- 6 of 7 vtable slots hold real logic (menu
  navigation, top-level action dispatch for Exit/Restart/Instant-Replay,
  scrollable-list layout math, render, and an auto-replay-trigger check).
  How the menu actually OPENS wasn't found. Bonus: an embedded per-tag
  memory-usage debug page revealed several new tagged-allocator names
  never seen before in this project ("Splinepath"/"MeshAnim"/"AIPaths"/
  "EventPaths"/"ReplayCache"/etc.) -- checked and ruled out as a
  NodeRegistry type table (cross-referenced "Dead Object" against the
  already-named New_Dead_Object's own tag string). 8 renames. **823
  renames** (818 functions + 5 data). Live metric: 1,095/5,357 (≈20.44%).
  Full trace in `RE_NOTES_debug_menu.md`.
- **"Take your time, deep research" pass on DebugMenu's audio-ducking calls,
  and a real mistake caught mid-trace**: found `DebugMenu_CleanupAfterClose`
  was WRONGLY named -- its true `this` (traced via raw disassembly, not the
  decompiler's generic `param_1`) is `DAT_001f82f4`, the already-known
  `AudioSystem` singleton, confirmed via 9 `xrefs_to` call sites spanning
  unrelated systems (`ReplayManager_UpdateSequenceState`, a large
  results-screen state machine). Corrected to `AudioSystem_EndOverlayDucking`
  (old name kept commented out per convention) and found its counterpart
  `AudioSystem_BeginOverlayDucking` plus a one-line wrapper
  `AudioSystem_PlayUIClickSound` (confirmed via the already-documented
  `AudioSystem` vtable to call the already-named `AudioSystem_PlaySoundSimple`).
  **Bonus**: tracing DebugMenu's "Instant Replay" trigger code led to a large,
  substantial post-race state machine, **`ResultsScreen_HandleTransitionState`**
  (was `FUN_000e8a00`) -- confirming DebugMenu's actions hook into real, live
  game state machines rather than an isolated debug-only path. 3 new renames
  + 1 in-place correction. **826 renames** (821 functions + 5 data). Live
  metric: 1,098/5,357 (≈20.50%). Full trace in `RE_NOTES_debug_menu.md`.
- **`SkyNode`/`ModelsNode`** (added to `RE_NOTES_terrain_collision.md`) --
  continued deep-research pass. `SkyNode` is the per-track skybox system;
  reading its 12-entry sky-name lookup table directly from memory
  independently cross-validates the already-documented `TrackTable`
  short-codes from a completely different angle (`gari`->Garibaldi,
  `pipe`->Pipedream, etc.) -- two unrelated systems agreeing on the same
  short-code convention. `ModelsNode` turned out to be a genuine companion
  to `TerrainNode`: the actual track-side prop/decoration mesh DRAW step
  (confirmed via a matching 162-segment loop bound with
  `TerrainNode_BuildVisibleCellList`). Also closed `OverlayNode` as
  "nothing new" -- it's just the already-documented `OverlayManager` under
  a different tag. 8 renames. **834 renames** (829 functions + 5 data).
  Live metric: 1,106/5,358 (≈20.64%). Full trace in
  `RE_NOTES_terrain_collision.md`.
- **Major find: cracked 2 of the last 4 unconfirmed `Rider_UpdatePhysicsState`
  sub-calls, revealing a whole new track-distance system.** Traced the
  shared helper calls fully instead of stopping at "terrain/geometry-query-
  shaped" -- every rider tracks its own distance-along-track position via
  an embedded spline path (`SplinePath_FindClosestPoint`/`EvaluateAtDistance`,
  with incremental frame-to-frame caching) and separately queries an
  external, vtable-dispatched object for scripted events keyed by track
  distance (`Rider_UpdateTrackEventTriggers` -- checkpoints, camera cuts,
  etc.). **Directly connects to 2 of the brand-new tag names DebugMenu's
  memory-usage page surfaced earlier** ("Splinepath"/"EventPaths" now have
  real confirmed homes). Honest caveat: the computed look-ahead steering
  angle's consumer (plausibly AI-rider steering, not proven) wasn't found.
  4 renames. 13 of the original 15 flagged functions now opened, 2 remain.
  **838 renames** (833 functions + 5 data). Live metric: 1,110/5,358
  (≈20.72%). Full trace in `RE_NOTES_rider_update_chain.md`.
- **Cracked another: a spatial ambient-zone audio influence system.**
  `Rider_InitAmbientZoneInfluences` (was `FUN_000308d0`, the last remaining
  fully-unexplored function from the original 15) turned out to be a
  genuine spatial-hash-grid query tying directly into the already-
  documented `AudioSystem`/`AmbientZone` classes -- closes the gap between
  "here's `AmbientZone`'s own methods" and "here's how a rider's position "
  actually finds and blends nearby zones every frame." 5 renames
  (`AmbientZone_QueryInfluencesNearPosition`, `AudioSystem_RegisterActiveZoneInfluence`/
  `FindActiveZoneInfluence`, `Math_EvaluateFalloffCurve`). **14 of the
  original 15 flagged functions now opened -- only `FUN_00033790` remains**
  (re-attempted with fresh context, genuinely exhausted for this pass, not
  an early stop). **843 renames** (838 functions + 5 data). Live metric:
  1,115/5,358 (≈20.81%). Full trace in `RE_NOTES_rider_update_chain.md`,
  cross-referenced in `RE_NOTES_audio_system.md`.
- **`FogMan`/`FogVolume` -- completes the entire tagged-object sweep.** The
  last untouched tag from the original `InGameState_LoadLevel` list turned
  out to be a genuine volumetric cloud/fog-volume rendering system
  (confirmed via the `"cloud"` tagged allocator). Bonus: resolved a
  previously-"stuck" `GfxContext` vtable slot that shares a global with
  FogMan's own volume-count lookup
  (`GfxContext_ExtractQuadVertexAttributes`). 8 renames total. **Every tag
  from the original `InGameState_LoadLevel` allocation list is now
  individually explored** (`SnowFallMan`, `LessonMan`, `PowerFX Particles`,
  `TerrainNode`, `VideoStreamMan`, `DebugMenu`, `SkyNode`, `ModelsNode`,
  `OverlayNode`, `FogMan`). **851 renames** (846 functions + 5 data). Live
  metric: 1,123/5,358 (≈20.96%). Full trace in
  `RE_NOTES_weather_effects.md`.
- **Exhaustive (negative) result on "how does DebugMenu actually open"**:
  checked every static angle without dynamic analysis -- `InGameState_TickFrame`/
  `InGameState_LoadingDispatch` in full, all 5 other functions in
  `InGameState`'s own code neighborhood, and a targeted binary search for the
  `+0x2a0` storage-offset reference (found only the construction and
  destructor-teardown sites, no activation). Conclusion: `DebugMenu` is
  genuinely constructed/destroyed every level load, but nothing statically
  reachable ever ticks or renders it -- consistent with this project's
  repeated "compiled in but stripped/disabled for retail" pattern. Not
  proven, but a genuine multi-angle exhausted search, not an early stop. No
  renames. Full trace in `RE_NOTES_debug_menu.md`.


### Title screen corrected from a measured capture (was guesswork)
The front-end title layout had been placed by eye and was wrong in every
dimension. Rebuilt from a capture of the original: art panel inset with black
above/below (not full-screen), menu items in gold **title.ffn** inside the
panel, info lines and "select (A)" in white **menu.ffn** below it, everything
at scale 1. Exact fractions in `RE_NOTES_boot_sequence_and_startscreen.md`.

Added a frame-capture harness (`SSX_START_PHASE=title`, `SSX_DUMP_FRAME=*.bmp`)
so front-end work is diffed against real captures rather than eyeballed.

Open gap: the panel's snowflake backdrop + three rider characters are a 3D
front-end scene (`f3bigmod`/`f3bigtex`/`f3biglod` in `models/mdlxbx.big`), not
a texture — blocked on the `.mxf`/`.xbd` geometry decode (M4). `dvd1` stands in
for the backdrop; the logo is genuine.

### Front end re-derived from `FEInit_Boot` instead of from screenshots
Per user direction: boot sequence is game code, so it comes from the decompile
or the notes — not from eyeballing captures. Read `FEInit_Boot` (:81736) and
`TitleIntroSequence_*` (:80861-81641). Results in
`RE_NOTES_boot_sequence_and_startscreen.md`:

* Title backdrop **proven** to be a 3D level (`data/models/ssxfe.big` via
  `Level_LoadTrackAssets`), extracted: 131+42 DXT1 textures + `ssxfe.xbd`.
  No 2D shortcut exists — blocked on M4.
* Real font scales recovered (`title.ffn` 1.4/1.3, `menu.ffn` 1.8/1.4) and the
  `effective = callerScale x fontScale` rule from `Text_MeasureStringAnsi`.
  Port now applies the font half; non-uniform scaled text renderer added.
* 640x480 ortho confirmed from the code.
* Boot video chaining confirmed correct.
* **Attract mode found and still missing from the port**: 60 s idle on the
  title screen replays `ssxintro.mpc`.
* Corrected `.xsh` TOC record size to 8 bytes (was parsed as 16 in an ad-hoc
  script; the port's own loader was already right).

### Title screen layout fully extracted from code (no fitted constants left)
Traced `f3stttl` -> vtable `PTR_FUN_00199498` slot 0x2c -> `UI_BuildTitleHelp`
(`0x00099690`). Recovered the complete layout despite a badly-mangled decompile
by (a) reinterpreting denormal floats back into the integer string ids they
really are, and (b) scanning the function's raw bytes for the coordinate
immediates the decompiler dropped.

Results: menu list (20,330) title.ffn scale 1.0; "Press START button" (38,410)
menu.ffn scale 0.8; copyright (20,428) menu.ffn scale 0.8; buttons y=428.
Widget scale constant is `0x3f4ccccd` = 0.8f at `+0xf8`.

**Found a real bug this way**: the copyright line is localized string `0xddb`
(`kFECopyright`), not a hardcoded literal as the port had it — that would have
broken every non-English build. Now read from the `.loc` table like the rest.

Attract mode implemented (60 s idle replays the intro).

Remaining fudges on this screen, both marked in source: the list widget's row
leading (in `UI_BuildListWidget_Alt`, not yet read) and the art-panel rect,
which stands in for the 3D viewport until M4.

### Title screen: zero invented text values remain
Read `UI_BuildListWidget_Alt` and `UI_BuildButtonGroup` plus the widget measure
pass `FUN_000a78b0`. Row pitch is item height + 4.0 on a vertical axis (fields
`+0x130`/`+0x138`/`+0x13c`), replacing the fitted 0.62 factor. Button group is
driven by a 17-entry `{mask, glyph, stringId}` table at `DAT_001b9d00`; the
title's `UI_BuildButtonGroup(2, 0)` selects glyph 62 + `kSTRSelect` (0x218),
icon 20x22, label scale 0.7.

**Second hardcoded-string bug caught**: "select" was a C literal, same class of
bug as the copyright line. Both now read from the `.loc` table.

Remaining non-code values on this screen are both blocked, not guessed:
the art-panel rect (stands in for the 3D ssxfe viewport, M4) and the A-button
sprite (needs the `fe_1.xsh` "xbox" atlas sprite table decoded).

### Hard-disk boot phase verified — 2 of 3 durations were wrong
Check1 0.60 -> **1.50 s**, Check2 0.55 -> **1.25 s**; Autoload 2.00 s was
already correct. Real frame counts come from `StartScreen_SetState`
(state 0 = 15f, 1 = 75f, 8 = 120f, 9 = 75f) and the state->string mapping from
`StartScreen_RenderStatusText`'s switch on `this+0x3e84`.

**Important discovery: `default.xbe.c` is stale.** The entire `StartScreen_*`
family postdates the export and does not appear in it at all. Found via
`/search_bytes` on the string ids as raw data, then `/get_function_containing`.
Treat an empty grep of the export as "re-query live Ghidra", not "not present".

Left undone deliberately: the state-machine driver at 0x000afae3-0x000b018a is
an undefined region; defining a function there risks fragmenting neighbours
(prior-session lesson), so the exact state path stays documented-but-inferred.

### Splash + status screens verified — 9 corrections
Splash backdrop was letterboxed; the original does a fullscreen 640x480 stretch
(`SplashScreen_DrawTexture` -> `FUN_000c1ed0(0,0,640,480)`), untinted.
"loading..." was at (14,454) in dark grey at integer scale 2; it is really
**white with a black drop shadow at (28,430)** via `HUD_DrawTextShadowed`
(call convention: colour, x, y, string), gated on the screen's `+0x30 == 4`.
The "Checking/Autoloading" status text was vertically centred at y=232; it is
really centred in a **416-wide box with its top at y=96**, from
`FUN_000bddd0`'s screen-fraction globals (0.5/0.65/0.2 of 640x480).

Also learned how fades are done natively: a fullscreen quad with per-screen
colour + alpha, not a framebuffer multiply.

Added `text::drawShadowed` and extended the capture harness
(`SSX_START_PHASE=splash|check|autoload|title`).

### Two text-rendering bugs fixed (found by capture comparison)
1. **Baseline wobble** in the scaled text renderer — rounded glyph offset and
   size independently, so glyphs sharing a baseline drifted a pixel apart. Now
   rounds destination edges and derives size from them. Verified by measuring
   the framebuffer: all 22 letter groups bottom at y=112 exactly (only the `g`
   descender differs).
2. **Drop-shadow offset was 1, should be 2.0** — `DAT_0018766c`, recovered from
   `HUD_DrawTextShadowed`'s disassembly since the decompile drops the args. At
   1px the shadow was invisible behind the glyph.

Also pinned the draw-colour global layout: `DAT_001baf70` = alpha,
`0x001baf74/78/7c` = R/G/B.

Capture harness now holds `fade_ = 1.0` when `SSX_START_PHASE` is set, so dumps
are never taken mid-fade.

### Button-glyph system identified; sprite rect still open
The button icon lookup is the **ShapeManager** (`"shpMngr"`, 0x1614, at
`[DAT_001e3c7c + 0x724]`), 200 slots x 28 bytes at +8. Renamed the misnamed
`IconAtlas_GetEntry` -> `ShapeManager_GetShape` (0x000f24e0) now that the two
are provably the same system.

Proved the title screen's `select` glyph (index 62) binds to the **`xbox`**
`.xsh` entry: `FUN_000f2550` writes `+0x6d0` from the `+0x15fc` cache slot, and
`(0x6d0-8)/0x1c = 62`.

**Open**: both initializers write only field [0] (texture handle) and only two
ShapeManager functions exist, so nothing found yet sets the per-shape UV
transform -- yet shapes 61+ share one 256x256 texture and must differ somehow.
The sub-rect source is unlocated; A-button pip stays a placeholder rather than
being guessed.

### Shape system decoded; real A-button sprite now drawn
Proved the shape record layout from `Sprite_DrawAligned`/`Sprite_DrawIconByIndex`:
`[0]` texture, `[1]`/`[2]` w/h, `[3]`=v0, `[4]`=u0, `[5]`=u1, `[6]`=v1.
Traced the full button-glyph draw path: `f3icon` widget stores the shape index
at `+0xf0`, vtable slot 11 (`FUN_000a56a0`) fetches and calls
`Sprite_DrawAligned`.

Dumped the `xbox` atlas and located the four face buttons (y=230, 17x22 each);
the height matches `FUN_000a41c0`'s 22.0 exactly. The port now draws the real A
sprite instead of a coloured rectangle.

**Still open**: nothing writes the per-shape UV fields -- verified across all 7
binders (only field [0], 270 writes) and all ~45 GetShape callers (zero writes).
The rect used is measured from the atlas and flagged as such in the source.

### `.ltg` load chain fully decoded (M2a-pre complete)
Traced the world singleton (`0x001faf88`, vtable `0x001a7ae8`) and its terrain
load chain, creating 3 functions from undefined regions (one boundary at a time,
each verified via `/read_bytes` first):

* `Terrain_BuildFromLtgAndXbd` (`0x0013a7f0`, slot 0x14) — **created**
* `Ltg_RelocateHeaderPointers` (`0x0013a910`, slot 0x18) — **created**
* `Ltg_RelocateCellTable` (`0x0013a930`, slot 0x1c)
* `Ltg_FixupCellAndResolveIndices` (`0x0013af90`, slot 0x20)
* `Ltg_RelocateSubObjectPointers` (`0x0013a970`, slot 0x24) — **created**
* `Resource_RelocateBlockPointers` (`0x0013b130`, slot 0x28)

`TerrainGrid` is the raw `.ltg` buffer (`this+4`); `this+8` is the `.xbd`.
Full corrected format in `RE_NOTES_terrain_collision.md` — four separate errors
in the old file-only decode are now fixed, all confirmed arithmetically against
`gari.ltg` (cell bbox spans exactly 10000, sub-cell exactly 2500, cell `+0x38`
== `0x54` == the sub-object array start, table end == data base).

**Scope change for M2**: the `.ltg` contains no geometry, only a spatial index;
collision surfaces live in `.xbd` tables (strides 0x2d0/0x90/0x70/0x80/0x5c), so
part of the M4 `.xbd` work is a prerequisite for M2 collision.

### `.xbd` table directory decoded (M2a-pre3, partial)
Five tables recovered from the `.ltg` index-resolution code and verified
arithmetically against `gari.xbd` (base + count*stride == next section offset,
exact, all five): A `0x2d0`/3885, B `0x90`/3393, C `0x70`/10, D `0x80`/542,
E `0x5c`/942. Full table in `RE_NOTES_xbd_model_format.md`.

**Interior not decoded, and confirmed not decodable from the file alone**: the
fields `Terrain_QuerySurfaceContact` reads (`p[0x16]` pointer, AABB at
`p[0x17]`/`p[0x1a]`) do not validate on disk — an exhaustive scan of a 720-byte
record found no offset where min<=max holds consistently. Those are built at
runtime. That build step is the M4 `.xbd` task.

### M2a done: `.ltg` C++ loader, validated on all 10 tracks
`port/src/assets/ltg.{h,cpp}` + a self-test in `asset_test`. Grid, cell table,
cell headers and the 16 sub-cell descriptors all parse; verified against every
track's `.ltg` (1404 populated cells, 22464 sub-objects, zero invariant
violations).

**Own error caught by the test**: an earlier note claimed every cell bbox spans
exactly one cell size. False — only 32 of gari's 243 do; the rest are tight
content bounds that may overhang neighbouring cells. Generalised from a single
sample. Note corrected, test now asserts only the three real invariants.

### M2b done: TerrainGrid queries ported and verified
`port/src/game/terrain.{h,cpp}` — AABB-overlap-with-margin, cell-range-for-bounds
and position->cell-index, all from live Ghidra. Cell-index round-trip is exact
over all 713 cells; clamping and broad phase verified.

**Found a naming bug in our own Ghidra DB**: `0x0015ca68` was
`CRT_RoundFloatToInt64` but is really MSVC `_ftol` (truncates toward zero).
Using rounding broke the cell round-trip on 521/713 cells; truncation gives 0.
Renamed `CRT_ftol_TruncateToInt64`; fixed 10 refs in `ssx_auto_rename.py` and 2
in the notes, resynced to `ghidra_scripts`. Would have silently mis-ported every
future function using that helper.

Also established from the disassembly: **the ground plane is XY, Z is up**
(rider pos.x/pos.y at rider+0x170/+0x174 feed the grid's worldMin.x/.y).

### M2c in progress: rider physics
Decoded `Rider_UpdateTerrainContact` (0x00037e70) by resolving its 14 float
globals. It is the **landing** path, not continuous ground-following: it
detects a landing, clamps landing speed, normalises a landing vector into
rider+0x180, and classifies the landing angle into `rider+0x208`:
1 = clean (+/-30 deg), 3 / 4 = off-axis, 2 = reversed (switch/fakie) - bands
exact via Math_PI / Math_DegreesPerHalfTurn. Result feeds
TrickCombo_ScoreAirCompletion, then Rider_SetPhysicsMode(3).

Renamed 4 constants in Ghidra: `Math_PI` (0x00188850), `Math_DegreesPerHalfTurn`
(0x00187514), `Const_NegOneF` (0x001a9f38), `Rider_MinLandingSpeed` (0x001877d8).

Next for M2c: physics modes 1/2 (continuous ground contact) and mode 3
`Rider_ResolveTerrainContactPhysics` - the actual board-on-snow response.

### M2c: mode 3 constants resolved; world unit still unknown
`Rider_ResolveTerrainContactPhysics` (0x000287a0, 514 lines) -- all 26 float
globals resolved. Key find: `DAT_00187558` = 1/60, the **fixed physics
timestep**. Also 1/30, 1/80, damping 0.92, and a second PI.

`DAT_00187544` = 277.778 = 1000/3.6 exactly (km/h conversion), and
`Rider_MinLandingSpeed` = 555.556 = 2x that. **But the world unit is still
undetermined** -- mm gives gari a 280 m drop over 300 m, cm gives a realistic
2.3x3.1 km run; plausibility favours cm but that is not evidence. Searched the
constant regions for a gravity value (981/9810/9.81): no match (the one near-hit
is used by a board-transform function). Recorded as open; the port keeps raw
world units rather than assuming a scale.

### Airborne modes 5/6 read; unit scale now strongly indicated (not proven)
Mode 6 uses only 1/60, +/-8.33333 and 0.983333 -- `8.33333 = 500/60` (a +/-500
per-second rate clamp, likely spin rate) and `0.983333 = 1 - 1/60` (per-frame
decay). No gravity constant there either.

Every speed constant is an exact multiple of 277.778 (=1000/3.6): 1, 2, 2.5, 3,
8. Under mm those are 1-8 km/h (absurd); under **cm** they are 10-80 km/h with a
20 km/h min landing speed -- so **1 unit = 1 cm is strongly indicated**, though
still not proven absent a gravity constant. Port stays in raw world units.

### M2d done: debug wireframe renderer
`port/src/game/debug_draw.{h,cpp}` + `terrain_debug_node.{h,cpp}`.
`SSX_DEBUG_TERRAIN=<track>` draws the real .ltg grid (nominal cells, per-cell
content AABBs, sub-cell AABBs) and sweeps a probe through the ported broad
phase each frame. Top-down shows Garibaldi's run winding across the grid; side
XZ shows a clean continuous descent -- a strong visual confirmation that the
corrected .ltg parse is right, since bad offsets would scatter the boxes.

### M2c groundwork: rider field map extracted
36 distinct component-frame rider fields used across the six physics functions,
extracted mechanically (pattern `componentBase + 0xOFF + param_1`, not plain
`param_1 + 0xOFF` -- the naive scan finds only unrelated locals). Includes
`0x5628`, the same field `RiderEvent_UpdateRailRideMovement` uses, tying the
rail system to the airborne modes.

Deliberately NOT transcribing the ~1600 lines of SSE in modes 1/2/3 yet: doing
it before the fields are named would produce plausible-but-unverifiable physics.
Order is field map (done) -> name fields by usage around the terrain queries ->
transcribe per mode against the debug sandbox.

### Rider world transform identified: `rider+0x4870` is a 4x4 matrix
Rows at +0x4870 (X), +0x4880 (Y), +0x4890 (Z), **+0x48a0 (world position)**.
Corroborated by two independent subsystems: the whole matrix is used by
`Rider_UpdateBoardAttachmentTransforms` (needs the rider's world matrix), and
row 3 alone by `Rider_UpdateSurfaceParticleFX` / `Rider_UpdateSnowSprayFX`
(spray spawns at the board's ground position). This is the key field for the
mode 1/2/3 transcription.

### CORRECTION (same session): rider field map needs (slot, offset) pairs
The field map recorded earlier today conflated component frames. Each physics
mode reads through a **different** component slot -- mode1 `rider+0x20`, mode2
`rider+0x0c`, mode3 `rider+0x10`, dispatcher `rider+0x30` -- so the same numeric
offset is different memory per mode. Four offsets (0x17c, 0x18c, 0x2dc, 0x2e4)
appear under multiple slots and were silently merged.

Concrete instance: `rider+0x19c` **direct** is an int terrain-cell index
(written by `Rider_ComputeTerrainCellIndex`), while `componentBase+0x19c` in
modes 1/2/3 is a float being integrated. Same offset, different type and
meaning -- the same "phantom field" trap seen earlier with 0x5720 vs 0x5710.

Rule adopted: never record a rider field as a bare offset; always `(slot,
offset)`. Corrected per-slot table is in RE_NOTES_terrain_collision.md.

### SOLVED: rider "component slots" are C++ multiple-inheritance descriptors
`Rider_ConstructBase` stores 12 pointers to 8-byte records at
0x00188798..0x001887f0, each `{i32 thisAdjust, u32 subObjectOffset}` -- the
standard MSVC MI layout (the adjusts are small negatives: -0x14, -0x10, -0xc,
-0x60, -0x20).

Addressing rule: `fieldAddr = rider + descriptor[slot][+4] + offset`.
So the dispatcher's physics-mode selector is `rider + 0x840 + 0x484` =
**rider+0xCC4**, not rider+0x484 as previously written.

This is the root cause of the recurring "phantom field" trap: two modes using
the same numeric offset address *different base sub-objects*. Any rider offset
recorded without its slot is meaningless and needs re-checking.

Remaining: the descriptors for slots +0x0c/+0x10/+0x20 (modes 2/3/1) are set
outside Rider_ConstructBase's visible body; finding them names the ground-mode
fields.

### CORRECTION: physics modes don't share a `this`
Found a contradiction: `NodeBase_ConstructRoot` sets byte 0x20 to a sequential
instance ID (an int), but `Rider_PhysicsMode1_GroundRide` dereferences
`param_1 + 0x20` as a pointer. Resolution: **`param_1` is a different object in
each mode** -- they are methods of different base classes, each receiving its
own adjusted `this`. Only the dispatcher's `param_1` is the complete rider
(its +0x30 descriptor matches Rider_ConstructBase exactly).

The constructor tail corroborates: sub-objects store back-pointers to the
complete rider (at +0x5910 and +0x7e4 within the sub-object) plus a negative
delta (offset - 0x840) at +0x2c -- standard MI bookkeeping.

Consequence: the per-slot offset table is correct *relative to each mode's own
this*, but those are NOT rider offsets. No physics-mode field can be named until
each mode's base sub-object is identified (needs disassembly - Ghidra drops the
ECX adjustments). The five direct rider fields are unaffected.

### SOLVED: rider field addressing (the M2c blocker is cleared)
`Rider_DispatchPhysicsMode`'s jump table gives each mode's `this` adjustment
(ADD ECX,0x4c0/0x4f0/0x500/0x520/0x530/0x630) -- Ghidra drops these entirely.
Chaining through each mode's MI descriptor:

  fieldAddr = rider + delta + descriptor[+4] + offset

giving mode1 base **0x850**, mode2 **0x864**, mode3 **0x860**. All three
descriptor pointers land exactly on `Rider_ConstructBase` assignments
(param_1[0x138]/[0x13f]/[0x144]) -- three independent confirmations.

The offset-only scan was wrong both ways: it **merged** different fields
(offset 0x17c = rider+0x9cc in mode1 but rider+0x9e0 in mode2) and **split**
identical ones -- 7 rider addresses are shared across modes under different
per-mode offsets (e.g. rider+0x9cc = mode1+0x17c = mode2+0x168). Those 7 are
the shared physics state and the top naming targets.

Also corrected: the physics-mode selector is **rider+0xCC4**, not rider+0x484.
M2c can now proceed to field naming and transcription.

### First reads on the shared physics fields
Using the solved addressing: `rider+0x09cc` is a **per-second rate** (both mode1
and mode2 multiply it by the 1/60 timestep before use); `rider+0x0b40` is a
**vector base** (taken as float* by both, touched at 4 sites in mode2);
`rider+0x0b48` is a **normalised scalar** (compared against 0.7 and 0.0).
Four of the seven still to characterise.

Noted a live instance of the old trap: mode2 also reads `iVar3 + 0x2e4`, but
`iVar3` is a different base than `iVar11`, so that is NOT rider+0x0b48 despite
the identical offset.

### All seven shared physics fields characterised by type
`+0x09cc` per-second rate (x1/60 in both modes); `+0x09e0`, `+0x09f0`, `+0x0b40`
**vectors** (taken as float*); `+0x0ab0` scalar (read in m2, zeroed in m3);
`+0x0b48` normalised scalar (0.7/0.0 thresholds); `+0x0d04` a **pointer to
another object** (deref'd to +0x58 char in m1, +0x94 int compared !=1 in m2).
Each corroborated by 2-3 modes reaching it through different per-mode offsets.

Also: `rider+0x0898` (mode1 only) is an int counter wrapping mod 4
(`+1 & 0x80000003`) - a phase index, not a physical quantity.

M2c: address model solved and verified, shared fields typed; remaining work is
naming them to specific quantities and transcribing modes 1/2/3.

### Three shared vectors separated by arithmetic shape
`rider+0x09f0` is **integrated state** - mode2 does `v += delta * dt` in place
(explicit Euler), which also confirms `+0x09cc` is a per-second rate since it
supplies the dt. `rider+0x09e0` and `rider+0x0b40` are **dot-product operands**
(read-only, fed into SSE horizontal-add pairs) - the basis the integrated vector
is projected onto.

Bonus: `rider+0x0a5c` is a scalar accumulator of a dot-product difference,
exponentially damped at **4.684/s** (`x *= 1 - 4.684*dt`) - the shape of a
lean/spin accumulator.

Identifications come from arithmetic shape, not name guessing: in-place `+= d*dt`
= integrated state; read-only horizontal-add = dot operand; `*= (1-k*dt)` =
damping.

### `rider+0x09f0` is VELOCITY; a gravity-shaped term found
Mode 2's integration delta is a weighted sum of basis vectors (an
**acceleration**), so `*(+0x09f0) += delta*dt` makes `+0x09f0` the **velocity**.

Found `_DAT_001aa420 = -1000` applied conditionally along `rider+0x0b40`:
`accel += dir * -1000`. As cm that is -10 m/s^2 (game-rounded gravity); as mm
it is -1 m/s^2 (implausible). **Second independent line of evidence for the
centimetre reading**, after the speed ladder.

Still not proof: that constant has exactly one xref in the binary and is gated,
and the airborne modes contain no gravity constant at all - so it may be a
mode-2 "stick to surface" force. Unit scale remains strongly indicated, not
proven.

### `rider+0x0b40` characterised; older note corrected via the addressing rule
Mode 1 conditionally **stores** a direction into `rider+0x0b40`, then
immediately dots it with the velocity (`rider+0x09f0`); mode 2 applies the
-1000 acceleration along the same direction. It is the axis the ground modes
decompose motion along.

**Correction applied to an older note**: `Rider_ResolveTerrainContactPhysics`
was documented as storing "the contact normal (rider+0x310)". Under mode 3's
base (0x860) that is really **rider+0x0B70** - a different field from 0x0b40.
The old bare offset is itself an instance of the offset-without-slot error.

Not claiming whether 0x0b40 is world-up or surface-normal: gravity along up and
a sticking force along the normal have identical arithmetic shape here.

### RESOLVED: `rider+0x0b40` is the terrain surface normal
Mode 1 stores it **only after a successful terrain query** - it builds a probe
segment from `P + dir*(-100)` to `P + dir*(+200)`, runs `FUN_001412b0` +
`Terrain_SampleHeightAt`, and writes `rider+0x0b40` only if the sample
succeeds. A world up-axis would be constant and ungated, so this is
terrain-derived: the **ground normal under the rider**.

Consequence: mode 2's `-1000` along it is a **surface-sticking force, not
gravity** - which weakens that line of unit evidence. But the probe range
(-100..+200 = -1m..+2m in cm, vs -10cm..+20cm in mm) is a **third** independent
line pointing at centimetres, alongside the speed ladder.

### M2c transcription started: `port/src/game/rider.{h,cpp}`
First real physics code, carrying only what is verified:
* the rider state struct with **true** rider offsets (post-adjustment chain),
  and a header note that fields are `(mode, offset)` pairs, never bare offsets;
* `classifyLanding()` -- the fully decoded landing classifier, bands formed as
  `(PI/180) * degrees` exactly as the original does. **9 of 9 band cases pass**
  in `asset_test`;
* the fixed 60 Hz timestep, `dt = rate * 1/60`, taken from the rider's own
  per-second rate the way modes 1 and 2 do it;
* the ground probe geometry (-100..+200 along `rider+0x48f0`);
* velocity integrated in place (`v += accel * dt`), the surface-stick term,
  the lean accumulator's 4.684/s damping, and the mod-4 phase counter;
* `dot4()` kept in the engine's SSE pair-sum association so rounding matches.

**Explicitly NOT transcribed, and marked so in the source**: the acceleration
weights, the mode transition rules, and the real surface normal (which needs the
.xbd narrow phase = M4). The `grounded` branch deliberately leaves the stored
normal alone rather than fabricating one.

### Mode 2's acceleration decoded: a suspension + carve model
The four weights resolve into the core riding mechanic:
* `Rider_ComputeSurfaceCompressionResponse` (0x00026da0) gives a **suspension
  force**, clamped non-negative (a spring that pushes, never pulls);
* an **edge angle** is built via `PI/180` from `rider+0x0ab0` and cached at
  `rider+0x0a74`;
* `sin`/`cos` of it rotate a basis vector into the **board's edge direction**;
* the suspension force is **split** between the surface normal and that edge
  (`edge = force/cos`, `normal = force - edge`), conserving the total.

That is a board-on-snow carving model: tilting the board redistributes the
reaction force from "up out of the slope" toward "along the edge" - how a
snowboard actually turns. Accel = normal*w1 + rotatedEdge*w2 + vec(0x0BC0)*w3 +
vec(0x0BD0)*w4, plus the gated surface-stick term.

### Mode 2's full force model identified
accel = normal*suspension + rotatedEdge*carve + tangentA*(F1+F2) + tangentB*F3,
plus the gated -1000 surface stick. Basis: `rider+0x0b40` normal,
`rider+0x0BC0` tangent A, `rider+0x0BD0` tangent B; the edge direction is
`normal*cos + tangentB*sin`.

`F2 = FUN_00027230(..., (30/30 * normalForce) / limit)` is **scaled by the
normal force** - the friction-circle signature (tangential force proportional
to load). `F3 = FUN_000274b0(..., rotatedEdge, ...)` consumes the edge
direction, making it the lateral/grip term. So: suspension + carve +
longitudinal + lateral, a contact-patch model with load-dependent friction.

Confirmed `rider+0x0d04`: it gates the surface stick via `ptr->[0x94] != 1`
alongside a global flag `DAT_001c6bc8`.

Held back from naming the three force functions - their roles in the sum are
clear but their internal formulas are unread.

### `Rider_ComputeThrustForce` (0x00026ef0) decoded and renamed
Mode 2's first tangential term is the rider's **thrust**:
`force = characterStat x speedHeadroom x headingAlignment`.

* `DAT_00187504` = **1/255** normalises a byte from `*(rider+0x0d04) + 0xe`,
  proving that pointer targets the **character/stat record** (it also gates the
  surface stick via +0x94).
* Alignment falloff is exact: full thrust within **30 deg** of travel, zero at
  **60 deg**.
* Speed limiter: headroom below max, capped at 1111.11 (= 4 x 277.778).
* `frame+0x45c == 2` selects a higher stat range (1.205-1.510 vs 0.738-1.015) -
  a boost state.
* Re-confirms `rider+0x09f0` is velocity (squared + sqrt'd to get speed).

Remaining in mode 2: FUN_00027230 (load-scaled friction) and FUN_000274b0
(lateral term).

### Mode 2 force model COMPLETE; unit scale settled as centimetres
Named `Rider_ComputeDragForce` (0x00027230, quadratic drag `-(a v^2 + b v + c) v`
with 3 character stats) and `Rider_ComputeLateralGripForce` (0x000274b0, a
piecewise-linear speed/grip curve + grip stat).

**Grip curve breakpoints are exact multiples of 277.778 (0, 2, 5, 8) with equal
widths of 3** -> in the cm reading: **0, 20, 50, 80 km/h**, peak grip at 50, mild
falloff to 80. Round authored numbers plus a physically correct shape.

That is the **fourth** line of evidence; three survive independently (speed
ladder, ground probe -1m..+2m, grip curve) - the `-1000` "gravity" argument was
withdrawn when +0x0b40 proved to be the surface normal. **Verdict: 1 unit = 1 cm.**

Both force functions share a **mismatch penalty**: when `frame+0x1f0` !=
`statRec+0x58`, coefficients scale by 1-penalty (0.0 / 0.150 / 0.300 by mode) -
a stance/switch handicap.

Complete model: normal*suspension + edge*carve + tangentA*(thrust+drag) +
tangentB*lateralGrip, plus the gated surface stick. Every term is driven by
per-character stats from the record at rider+0x0d04.

### Force model transcribed into the port and verified
`port/src/game/rider.{h,cpp}` now carries the decoded model:
* a `Stats` struct mirroring the character record at `rider+0x0d04`
  (+0x0e accel, +0x11/+0x19 drag, +0x13 grip, +0x58 stance, +0x94 stick gate);
* `statLerp()` -- the shared `lo + (hi-lo) * stat/255` every force term uses;
* `lateralGripCurve()` -- piecewise linear through the four authored
  breakpoints;
* `thrustAlignment()` -- the 30/60 degree falloff, *without* clamping the low
  side, because the original relies on a later `if (0 <= align)` to discard
  negatives (clamping here would silently change behaviour);
* `mismatchScale()` -- the stance-mismatch penalty (0 / 0.150 / 0.300 by mode);
* the unit constant, now that cm is settled.

`asset_test` verifies all of it:
```
[ridr] landing classifier: 9 of 9 bands correct
[ridr] grip curve: 4 of 4 breakpoints exact, peak at 50 km/h yes
[ridr] thrust alignment: 5 of 5 checks pass (1.0 at 30deg, 0.5 at 45, 0.0 at 60)
[ridr] stat lerp (1/255 normalisation): 2 of 2 endpoints exact
ALL OK (0 failures)
```

### OPEN thread: the tangent basis producer
`rider+0x0BC0` / `+0x0BD0` (mode 2's tangential axes) are written by **none** of
the three ground modes - verified at each mode's correct offsets (m1 0x370/0x380,
m2 0x35c/0x36c, m3 0x360/0x370). Ruled out `Rider_UpdatePhysicsState` and
`FUN_00026eb0` (a debug marker). Most likely among the ~18 helpers
`Rider_UpdateSubsystems` (0x00030da0) dispatches before the modes; look for one
reading the transform row 1 (`rider+0x4880`) *and* the surface normal
(`rider+0x0b40`) and writing a vector pair.

This is the last input `step()` needs to assemble the full acceleration.

### Tangent-basis trace stops at dynamic dispatch
`Rider_UpdateSubsystems`'s 18 calls are almost all **instrumentation stubs**
(`0xdeadbfXX` tags via `DebugBuffer_Write`). The one with real logic,
`Rider_UpdatePhysicsComponents`, calls `Component_UpdateAll` 3x (a 3-substep
integration) but reads no transform and writes no vectors.

So the tangent basis is built inside an **attached component's** update, reached
through a runtime linked list - dynamic dispatch, not a followable static call.
Recorded as its own thread: enumerate component types and find which update
writes a vector pair.

Does not block the sandbox: all force terms are transcribed and tested; the
basis can be derived conventionally (forward projected onto the surface, crossed
with the normal) as a marked stand-in, like the surface normal pending M4.

### M2 sandbox: a rider now moves under the transcribed physics
`SSX_DEBUG_TERRAIN=gari` steps a `rider::State` through the decoded force model
each frame and draws it on the real `.ltg` grid. After ~15 s it reads:

    rider (-12398, 1090, -2771)  airborne  67.8 km/h  trail 24

It accelerates from rest to **67.8 km/h** — below the 80 km/h cap and consistent
with the thrust model (`headroom x characterStat x alignment`), which is a
meaningful check: a wrong stat normalisation or a wrong unit scale would land
far off that number.

**What is real vs. stand-in, stated plainly:**
* real: the grid, the broad phase, the force *terms* (thrust / drag / lateral
  grip / stick), the 1/60 step, the stat normalisation, all constants;
* stand-in: the surface (from `.ltg` sub-cell AABBs, pending M4's `.xbd`), and
  the tangent basis (derived conventionally, since the engine builds it in a
  component reached by dynamic dispatch);
* omitted rather than faked: the suspension/carve split, because
  `Rider_ComputeSurfaceCompressionResponse` needs a penetration depth the
  stand-in surface cannot supply.

**Behaviour note**: the rider slides off the track edge and falls. That is
expected — the stand-in surface only exists over populated cells, and there is
no track-following. Note also that the downward pull is the **surface-stick**
term (`-1000` along the normal), not gravity: no global gravity constant has
ever been located in this codebase, which remains an open curiosity.

### FINDING: the game has no constant gravity term
Exhaustive search - no `Gravity` function; all constants of modes 1/2/3/5/6 and
the contact function resolved; a sweep of ten 2 KB data blocks for floats in
900..1100 found **seven** candidates, every one accounted for (AI steering,
collision impulse, spray FX, a mode-5 *torque*, and the -1000 surface stick).

Instead SSX uses two mechanisms: **on the ground**, -1000 along the *surface
normal* (the rider is pulled into the mountain, not toward world-down - which is
why riders hug terrain); **airborne**, mode 5's velocity update is asymmetric on
Z (`(fVar21 - fVar15)` on the Z lane only), a *computed* descent term, not a
fixed g.

Port consequence: adding a conventional `v.z -= 9.81*dt` would be wrong in both
regimes. rider.cpp implements the ground mechanism; the airborne one needs
fVar15's provenance.

### CORRECTION: mode 5's Z-asymmetry is substate-gated, not general descent
Last entry called it "the descent term". Wrong - the block is gated on
`RiderEvent_GetSubState() == 0x2dc || 0x2dd`, and its values come from a
100-byte-stride table indexed by `frame+0x31c` off a manager object. A
per-substate data-driven effect, not gravity.

Gravity remains **not found**, and the general airborne descent is still
unlocated. Second time this session a striking asymmetric term turned out to be
conditional (the first was the -1000 read as gravity). Reading the gate above an
expression matters as much as the expression.

### Component dispatch pinned exactly; insertion still unfound
`Component_UpdateAll`: circular list, head `this+0x50`, next `node+0x24`,
dispatch **vtable slot 0xc**. That is now exact (the prior note had it partly
open).

Insertion site still not found - searched for functions writing both +0x24 and
+0x50; all 16 hits are false positives or known init/teardown. The real inserter
takes the head as a parameter, so a `param_1`-only scan cannot see it.

**This is the highest-value remaining unknown in the rider physics**: both the
tangent-basis producer AND the general airborne descent terminate here.
Suggested attack: enumerate vtables whose slot 0xc points into 0x00025000-
0x0004a000 (the rider physics range) - the component *types* are findable from
their vtables even without the attach site.

### Component hypothesis disproved; matrix hypothesis replaces it
The vtable scan worked - found the component family (stride 0x58, matching
`Rider_ConstructComponentSlots`). But slot 0xc is
**`Rider_HandleComponentStateEvent`**, an *event dispatcher*, so
`Component_UpdateAll` does not run physics. Both threads that pointed at the
components (tangent basis, airborne descent) need redirecting.
`Rider_UpdatePhysicsState` (slot 0) also writes no vectors - it does dt-scaled
decay over ~15 field pairs and dispatches the mode.

**New hypothesis**: `rider+0x0b40` and `+0x0BC0` are the bases of two **4x4
matrices** (rows 0x10 apart), not loose vectors. Supporting: mode2 reads rows 0
and 2 of the first and rows 0 and 1 of the second, and `rider+0x0B70` (already
identified as the contact normal) is exactly **row 3 of the 0x0b40 matrix**.

That reframes the search - look for a function writing a whole 4x4 at 0x0b40 or
0x0BC0, not two separate vectors. Explains why every search so far missed it.

### `rider+0x0b40` RESOLVED: a contact record, not a matrix
Mode 3 writes two of the four candidate rows, both gated on
`Terrain_QuerySurfaceContact`: row 3 (`rider+0x0B70`) gets the **contact
normal**, row 0 (`rider+0x0B40`) gets a **vector difference** (penetration /
relative delta). Mode 1 writes `0x0B40` after its own terrain sample too.

So `0x0b40..0x0b70` is a **contact record** (delta at +0x00, normal at +0x30),
not a transform - superseding the 4x4 guess for that base. Consistent with every
earlier observation (terrain-gated, dotted with velocity, carries the stick
direction).

`rider+0x0BC0`/`+0x0BD0` remain unfound after **five** eliminations: the three
ground modes, the UpdateSubsystems stubs, the component dispatch, and
Rider_UpdatePhysicsState.

### `Rider_EvaluateGroundMovementTransition` = the switch-stance flip
Fixed the 4-float-write scanner to respect variable lifetime (Ghidra recycles
`pfVar10`; the first version matched a declaration against a later unrelated
write and produced a false "found it"). The corrected scan gives 13 true hits.

`Rider_EvaluateGroundMovementTransition` (0x00025bd0, slot 0x20 = mode 1's
frame) writes `rider+0x0BC0` and `+0x0BD0` -- but **negates** them
(`DAT_001fae10..1c` are all 0, so `v = 0 - v`). With the PI heading rotation,
the `+0x204` stance toggle, and an animation code chosen by comparing against
`statRec+0x58`, this is **riding switch/fakie**.

Ties the model together: `statRec+0x58` is the same field the drag and grip
functions use for their mismatch penalty - riding switch costs you coefficients
*and* plays a different animation.

Producer of 0x0BC0/0x0BD0 still unfound (this is a modifier, not a constructor);
the other 12 scan hits are all already-identified fields.

### Two more angles closed on the 0x0BC0 producer; likely a stale-export artefact
Helper-fill **ruled out** (zero sites pass the address to a callee under any
known frame base). Reset-time init **inconclusive**: `Rider_ResetPhysicsState`
sets two vec4s to `(0,0,0,1)` - identity *quaternions*, not a basis - and never
touches 0x370; also its `this` is a component object, not provably mode 1's
frame.

Seven approaches now exhausted. Since the export is known-stale (it hid the
whole `StartScreen_*` family earlier), the most likely explanation is that the
producer simply **isn't in `default.xbe.c`**. Next step: re-run the
lifetime-correct 4-float-write scan against a fresh export or live Ghidra.

## M3 STARTED (behavioural-fidelity milestone)

### M3a done: button-map parser ported
`port/src/assets/btnmap.{h,cpp}` parses both `btnmap0.dat`/`btnmap1.dat`.
Verified in `asset_test`: 14 digital / 15 grabs / 16 analog in both files,
matching the documented shape (15 grabs = 4+6+5), plus specific bindings
(`Boost`/`Tweak`=B|X, `Reset`=BACK, `Antic`=A, `LateSpinMode`=BLACK,
`easyA`=LSHIFT, `easyB`=Y, `Brake`=PAD_NEG_Y|JOY_L_NEG_Y negated) and the
firing logic (`fires(Boost, B|X)`=1, `fires(Boost, B)`=0).

Recorded a semantic decision: `(ALL)` in the *not-pressed* column means **no
exclusions**, not "exclude everything" - the literal reading would make almost
every row unfireable. Likewise `(NONE)` in the *pressed* column means "no held
requirement".

### M3b done: TrickCombo scoring ported
`port/src/game/trickcombo.{h,cpp}`. Constants read from the binary:
scale **0.678654** (`DAT_0018a0d0`), big-air threshold **4.0 s**
(`DAT_001878c4` - a constant already resolved during the M2 landing work),
**1000 points per whole second** of big air.

Formula: `points = trickPoints/(repeats+1)`, plus `streakBonus(streak)` when
streak>1 (then streak resets), plus `ftol(airSec)*1000` when airSec >= 4.0.
`trickPoints = ftol(difficulty * multiplier * 0.678654)` floored to a multiple
of 10.

Two corrections to the older prose in that notes file: the zero-guard is a plain
`< 0` (DAT_001a9f34 == 0.0), not an epsilon; and CountRecentRepeats' `'e'`/`,`
comparisons are **byte values 0x65/0x2c in the packed record**, not characters.

Verified: streak table exact, repeat divisor exactly /1 /2 /3 (6780 -> 3390 ->
2260), big air 3.0s -> 0 and 4.5s -> 4000. Hand-computed point values match,
independently confirming the scale constant and the floor-to-10.

### M3c done: RiderEvent state machine ported (+ a correction)
**Correction**: the notes said `RiderEvent_SetState` "validates the new value via
FUN_000313f0". It does not - that function switches on the state field *before*
the write, so it is the **EXIT handler**. Renamed
`RiderEvent_DispatchStateExit`. The machine is a textbook enter/exit:
exit(cur) -> write -> enter(new), with `DispatchTypeB` as the per-frame tick.
Three tables: 14 exit / 16 enter / 22 tick cases.

Also pinned the state field's true address: **rider+0xCC8** (component slot 0x30,
descriptor 0x840, +0x488) - adjacent to the physics-mode selector at rider+0xCC4.

`port/src/game/riderevent.{h,cpp}` verified by asserting the transition trace
`X0;E7;X7;E15;T15;X15;E21;` - ordering, no-op repeats, and transition count.

State 4 = `RiderEvent_ToggleSwitchStance` is the same switch/fakie mechanic found
from the physics side in `Rider_EvaluateGroundMovementTransition`.

### M3 LOOP CLOSED: input -> physics -> events -> scoring
`port/src/game/input.{h,cpp}` bridges platform buttons into the file's button
vocabulary, and the sandbox now runs the full chain each frame. Live readout
after ~20 s on gari:

    score 3010  last +3010  air 12.00s  best 1.855s  evt 15  action medC

Each field comes from a different decoded subsystem:
* `action medC` -- a named grab resolved through the real `btnmap0.dat` table
  scan, not a hardcoded button check (the engine is data-driven here, so the
  port is too);
* `evt 15` -- RiderEvent state `0xf`, set on takeoff via the enter/exit machine;
* `last +3010` -- the transcribed `TrickCombo` air formula;
* `best 1.855s` -- airtime from the 1/60 frame counter.

Boost is wired the faithful way: holding it sets `rider.modeField = 2`, which is
the flag `Rider_ComputeThrustForce` reads (`frame+0x45c == 2`) to select the
*higher* stat range (1.205-1.510 instead of 0.738-1.015). So boost works by
changing which authored constant pair the thrust lerps between -- not by
multiplying a force.

### Two stand-ins in the loop, both flagged in the source
1. **Trick difficulty** is `airFrames * 40` -- a placeholder. The engine
   accumulates real difficulty into `this+0x14` from spin/flip/grab state,
   which is `TrickCombo_EncodeTrickRecord`'s territory and not yet transcribed.
2. **Shoulder/BLACK/WHITE buttons** don't exist in the port's platform layer
   yet, so the d-pad stands in for them to make the grab tiers reachable from a
   keyboard. A mapping convenience, not engine behaviour.

### Difficulty model decoded and ported (3 renames)
`this+0x14` is fed by three accumulators, now named:
`TrickCombo_AccumulateSpinDifficulty` (0x0005dd60, **+0.0699543 per new 180 deg**),
`TrickCombo_AccumulateFlipDifficulty` (0x0005ddc0, **+0.2499898 per new full 360**),
`TrickCombo_AccumulateElementDifficulty` (0x0005d220, **(count+1) x 0.0424975**),
plus two flat bonuses in ScoreAirCompletion (+0.1999773 / +0.1800110).

Spin and flip share the 180-snap preamble but differ in the gate - flip requires
`a % 360 == 0`. Both keep a **high-water mark** (+0x38 / +0x3c) so rocking back
and forth cannot farm difficulty. **A flip is worth 3.57x a spin.**

Preserved an asymmetric rounding boundary: the snap uses `rem < 0x5b` (< 91), so
90 rounds DOWN. My first test asserted the mathematically-nearest values and
failed - the implementation was right, the expectation was the guess. Test now
asserts the engine's answers.

This retires the port's biggest scoring stand-in (difficulty was `airFrames*40`).

### `.aip` container ported and validated on all 10 tracks
From `AIPath_LoadFromFile` (0x000bf8d0): magic `0x0A0A0A0A`, chunk count, then
`{type,size}` chunks (0 = AIPaths, 1 = EventPaths). The walk lands **exactly**
on the file end for every track, so the reader rejects a mismatch rather than
accepting a short read.

All 10 parse with both chunk types present; sizes track complexity sensibly
(pipe smallest at 11664/1248, elysium/untrack largest). Container level only -
the per-path interior is still `AIPath_ParseFromBuffer`'s territory.

Also wired the **real difficulty accumulators** into the sandbox loop, replacing
the `airFrames*40` stand-in: the HUD now reads e.g.
`diff 0.40  spin 720  flip 360` with the engine's high-water-mark rules live.

### `.aip` per-path records fully decoded (AIPath_ParseFromBuffer)
Container walk was already exact; this pass decoded the record interior and
found that the obvious reading was wrong. The 16-byte segment record is NOT
`{vec3 pos, u32}` -- it is `{float dirX, dirY (unit in xy only), float slope
(rise/run), float length (horizontal cm)}`.

Proof rather than plausibility:
- worst `|1-|(dirX,dirY)||` = 2.13e-07 over 27,642 segments
- integrating the segments from `start` reproduces the stored bbox to
  0.00195 cm worst-case over all 1,020 paths -- a closed identity
- the 8 `|slope|>100` outliers all have `length` at the 1e-5 floor and yield a
  clean 5.1-5.5 m rise: vertical drops on Mega Plex, confirming the model

Also settled: the EventPath "vtable" is a **shifted secondary view starting at
slot 4** of the AIPath vtable, which predicts the 0x40/0x3c object sizes and
the tag split -- and the files match exactly (932/932 AI paths carry tags
{100,101}, 88/88 event paths carry {0}).

Event-path tag 0 is the **distance to the finish**, not the path's own length
(first hypothesis, failed on all 88). Cross-checked against independently
decoded geometry: 27 of 29 endpoint-to-startpoint joins telescope correctly;
the 2 exceptions are branch points.

Zones are intervals `[rangeStart, rangeEnd]` in cm: all 4,501 lie inside
`[0, pathLength]`, and all 4,276 zone ids resolve in the DAT_001bae60 table
with none falling through to the default.

Still open: `param100`/`param101` roles. Read and preserved, named neutrally.

### Race lifecycle ported + a documented claim CORRECTED
Ported `port/src/game/race.{h,cpp}`: the track table, the showoff medal
targets, and the 11-state race/tutorial state machine.

**Correction to `RE_NOTES_level_script_system.md`.** That file described
`Race_ComputeRankings`' constant table as "medal **time** targets ...
(milliseconds -- 55s/40s/25s)" keyed by a "difficulty flag". The constants are
right; the interpretation was wrong on both counts.

They are **showoff trick SCORES**:
1. the destination is `rider+0x5710`, which this project separately established
   is the running trick score (`0x5630+0xe0`) and which the HUD draws as the
   on-screen score -- while the race *time* is `rider+0x448`, a field the same
   function uses in its other branch;
2. the writes are gated on `GameMode_Current` 3 and 5, both ShowoffMode;
3. Pipedream -- the halfpipe -- carries the highest targets
   (800000/500000/250000), absurd as par times, exactly right as trick scores.

And **`DAT_001dec90` is the current track index**, not a difficulty:
`Level_GetCurrentTrackNameTag` (0x0007b800) switches on it to produce the asset
tag (garibald/snowdrea/elysium/mesablan/merqury/aloha/pipedrea/untracke/tokyo/
trick/alaska; slots 6 and 9 share `pipedrea`). Cross-check: the 10 tracks that
ship a `.aip` are exactly those tags minus `trick`.

Two Ghidra mistypings cleared up in passing: `&DAT_0001c138` is the integer
115,000, and `(param_2 != 2) - 1 & X` is a branchless "X if medal==2 else 0".

The internal inconsistency that exposed this was already sitting in the notes:
the same section called `+0x5710` the "Showoff sort stat" two paragraphs after
calling constants written to it "time targets".

### M1' (headless core) FINISHED
`NodeRegistry` existed but was untested and incomplete. Closed both.

**Order tables re-verified by reading the binary**, not by trusting the prose:
```
0x001ba548  g_SubsystemTickOrder_Main            0,1,2,3,5,6,7,8,4,0a,0b
0x001ba574  g_SubsystemTickOrder_OverlayActive   8,0a,0c
0x001ba580  the IntegratePendingNodesOfType arg  = 3   (a global, not an immediate)
0x001bb264  g_SubsystemRenderOrder               0,1,2,3,5,7,8,4,9,0a,0b,6,0c + ffffffff
```
All three matched what the port already had. Worth noting `InGameState_Tick-
SubsystemsByTypeOrder` passes `DAT_001ba580` rather than a literal 3, so the
port names it instead of inlining.

**Two genuine gaps fixed:**
1. `tickFrame` skipped the pending-node integration entirely (the old code had
   a "future addition" comment). Now: main table -> `integratePending(3)` ->
   tick type 12, the engine's exact sequence.
2. `NodeRegistry_IntegratePendingNodesOfType` (0x000aaa80) is now ported
   properly: drain the per-type pending list, scan the active list for the
   first node where slot 3 (`insertBefore`) says yes, then slot 4 (`replaces`)
   decides insert-in-front vs displace-and-destroy. The engine's displace path
   ends in `(**(code**)*other)(1)` -- the scalar-deleting destructor.

**One deliberate divergence, documented at the call site:** `NodeRegistry_Tick-
AllOfType` saves only the immediate next-pointer before each tick, so if a tick
removed its SUCCESSOR the engine would follow freed memory. The shipped game
never does this (nodes only remove themselves). The port stops instead of
chasing the dangling pointer.

**Type 9 is correctly absent from the tick path** -- `InGameState_TickFrame`
ticks it separately under its own gating -- but present in the render order.
Both directions are now asserted.

9 new assertions in `asset_test`, all passing: the three tables driven through
the public API, self-unregister-mid-tick, pending invisibility before
integration, sorted insert, the replace path, and integrate scoping to type 3
only.

### M4 part 1: the .xbd collision chain DECODED + a prior conclusion corrected
The earlier session concluded the `.xbd` collision record was "populated at
runtime, not read from disk". **It had tested the wrong table** — table A
(stride 0x2d0) rather than table D (stride 0x80), which is what
`Terrain_QuerySurfaceContact` actually walks.

Traced properly: the query reads `subObject+0x3c`/count `+0x2a` and derefs the
entries as pointers, but `Ltg_RelocateSubObjectPointers` never resolves
indices — so the array must sit inside a CELL array that
`Ltg_FixupCellAndResolveIndices` did resolve. Measured: **all 238 sub-object
arrays lie inside the cell's `+0x44` array = table D**. (The table-A guess
scored 0/238 and was dropped.)

Table D then validates completely against the file: AABB min<=max **542/542**
(the old table-A scan managed 12 of 40), `w` components `(1,0,0,0)`
**542/542**, and the cubic curve inside its own AABB **542/542**. The record is
`c3,c2,c1,c0` float4s + group index + AABB; the query evaluates
`p(t)=c0+c1t+c2t^2+c3t^3` 4-wide at 4 samples.

**New table F found** (base `xbd+0x70`, count `xbd+0x28`, stride 0x28) via
`Resource_RelocateBlockPointers` -> vtable slot 0x6c. It is the collision
group: AABB + `firstD`. Both linkage directions are exact (169/169, 542/542),
groups are contiguous runs starting at `firstD` (169/169), and F's AABB is the
union of its members' (167/169).

**Exactly one field is genuinely runtime-populated**: `F+0x18` bit 0, which
gates the test and is 0 in all shipped records. A port defaults it to enabled.

**Ground surface identified as table A**: reference density shows A, B and D
are each referenced exactly once (partitioned across cells) while E is shared
92,728 times over 942 records (a palette). A is **exactly 16.0 refs per cell** =
one record per `.ltg` sub-object (243x16 = 3,888 vs 3,885 records). Decoding
A's 720-byte interior is the remaining M4 step and what unblocks M2's ground
stand-in.

### M4 part 2: edge-collision system PORTED and verified on all 10 tracks
Found the D-record fixup (world vtable slot 0x70, `FUN_0013b490`, created after
checking the boundary). It resolves `+0x50`/`+0x54` as **neighbour indices**
(-1 = null) and `+0x58` as the group index.

**Self-correction from earlier this session:** I had dumped D as floats and
recorded `+0x50`/`+0x54` as "NaN", reading them as unset runtime data. They are
`-1` sentinels in an int field. Checking the fixup before typing the field
would have caught it immediately.

So a group is a chain of cubic segments — a rail/lip/edge. Ported as
`port/src/assets/xbd.{h,cpp}`, validated across all 10 tracks: **5,476 edges in
1,531 chains**, with w-homogeneity 5476/5476, AABB order 5476/5476, curve
inside AABB 5476/5476 (worst excess 0 cm), link symmetry 3945/3945, group
contiguity 1531/1531, and the chain arithmetic closing exactly
(5476 - 1531 = 3945 internal links). Worst end-to-start gap across all 3,945
links: 0.035 cm.

Remaining for M4: table A (the ground surface, one record per .ltg sub-object).
Its 720-byte shape is visible but its reader has not been found, and after the
NaN mistake above I am not committing to a field layout from a dump alone.

### M4 part 3: ground sampler located; table A confirmed, its geometry still open
Mapped **every** `.ltg` sub-object pointer field to its table by testing which
cell array each points into (243 cells, 100% unambiguous): `+0x34`->A,
`+0x38`->B, `+0x3c`->D, `+0x44`->E, `+0x48`->C.

Found `Terrain_SampleHeightAt` (0x0013f480), the real ground sampler. It walks
16 sub-objects per cell and runs two inner loops: `+0x34`->table A and
`+0x38`->table B.

**Table A is the file-resident ground surface.** AABB at `+0x150`/`+0x15c`
validates **3885/3885**; `+0x168` is the surface type id (9 distinct values),
returned to the caller as part of the contact result and skipped when it equals
0x11.

**Table B is runtime-registered, not file geometry**: its path needs
`(rec+0x68 & 0x20)` set and `rec+0x6c` non-null; in gari those are **0/3393**
and **null in all 3393**. (Mid-session I briefly called B the ground after
seeing the sampler read `+0x38` — the gate values corrected that.)

**A's geometry payload is still open.** `FUN_0013a280` reads `+0x50..+0x14f` as
exactly 16 vec3s (48 float loads, w discarded; w == 1.0 on all 62,160) and
memoises a 7,376-byte derived structure per record — a tessellation cache.
Four structural hypotheses were tested and all failed decisively (world points
in the AABB 0/3885; Bezier convex-hull property 110/3885, worst breach 137 m;
corner interpolation 0/3885; the +0x180 quad matching control points 0/3885).
The 16 vectors are not in the record's world space. Next step is reading
FUN_0013a280's arithmetic rather than fitting further models.

### M4 part 4: GROUND SURFACE DECODED — bicubic patches in the power basis
Read `FUN_0013a280`'s arithmetic instead of fitting models to the dump, and it
settled immediately. Its lazily-built basis table decodes to **exactly
`(u^3, u^2, u, 1)` for `u = i/9`** (worst deviation 3.3e-08), so table A's 16
vectors are **polynomial coefficients**, not control points:

```
S(u,v) = sum_j ( sum_i P[j][i] * u^(3-i) ) * v^(3-j)
```

That is why every Bezier hypothesis failed last pass — a power basis has no
convex-hull property and does not interpolate corners.

Tessellation resolution is certain from the memo-cache arithmetic, which closes
exactly on 0x734 dwords: 4 + 100*4 = 0x194 (10x10 points), + 81*16 = 0x6a4
(9x9 quads), + 9*16 = 0x734 (the stride).

**Validated on all 10 tracks, 28,484 patches**: AABB order 28484/28484;
patch evaluated on the engine's own 10x10 grid lies inside its stored AABB
28312/28484 (99.4%), worst breach 4.8 cm — against patches tens of metres
across, and against 137 m for the discarded hypotheses.

Surface type at `+0x168` has 18 distinct values; the sampler's skip value 0x11
is carried by exactly 80 patches, so that branch is real.

Ported as `xbd::Patch` with `eval(u,v)`, asserted in asset_test.
**This retires M2's ground-surface stand-in.**

### M2 GROUND STAND-IN RETIRED — the physics now runs on the real surface
`rider::step` takes an optional `xbd::File`; with one, `terrain::sampleGround`
walks the engine's own path: cell -> sub-objects -> the sub-object's table-A
patch list -> evaluate the bicubic patch. It tessellates to the SAME 10x10 grid
the engine uses and does point-in-triangle on the 9x9 quads, so the
discretisation matches rather than approximating.

**A pairing correction found while wiring it.** Sub-object `counts[]` and
`ptrs[]` are NOT positionally paired. Established:
* `+0x34` <-> `+0x24` — read directly in `Terrain_SampleHeightAt`, and the
  references sum to exactly **3885 == table A's count**
* `+0x3c` <-> `+0x2a` — read directly in `Terrain_QuerySurfaceContact`, and
  they sum to exactly **542 == table D's count**
* `+0x38` <-> `+0x28` — inferred only, from the sum landing on **3393 ==
  table B's count**

The exact-partition argument works because A, B and D are each referenced
exactly once across all cells. It does NOT apply to table E (92,728 references
over 942 records), so the `+0x44`/`+0x48` pairings are left unestablished and
`subIndices()` returns 0 for them rather than guessing.

**Validated on real track data** (probing the centre of every populated cell):

| Track | cells | hits | z range (cm) | inside patch bbox | bad normals |
|---|---|---|---|---|---|
| gari | 243 | 208 (86%) | -269473 .. 1926 | 208 / 208 | 0 |
| alaska | 150 | 111 (74%) | -244587 .. 6565 | 111 / 111 | 0 |
| pipe | 46 | 41 (89%) | -22723 .. -678 | 41 / 41 | 0 |
| untrack | 216 | 185 (86%) | -265117 .. 28636 | 185 / 185 | 0 |

Every hit lands inside its own patch's bounding box and every normal is unit
length and up-facing. Misses are cell centres that fall in gaps between
patches — the track is a ribbon through a rectangular grid, so that is
expected, not a failure.

The rider now also carries `surfaceType` (patch `+0x168`), the field the engine
feeds to its contact result.

**M2's remaining stand-in is the tangent basis only** — independent of M4, tied
to the still-open `rider+0x0BC0`/`+0x0BD0` question.

### M4 part 5: table A's render fields — atlas UVs, cell back-reference, corner cache
Three more fields resolved by closed tests:
* `+0x00..+0x0f` is a **16x16 texture atlas** mapping -- `uvOffset` is always a
  multiple of 1/16 (3885/3885) and `uvScale` is a single distinct value,
  exactly (1/16, 1/16). The 4 constant corner UVs carry a flipped V axis.
* `+0x17c` is the **owning .ltg cell index**: all 209 distinct values are
  populated cell indices, and every patch's AABB lies inside its claimed cell's
  AABB in XY -- **3885/3885, worst overhang 0 cm**.
* `+0x180..+0x1bf` is a **cached copy of the four patch corners**, exactly
  `S(u,v)` at (0,0)(0,1)(1,0)(1,1) -- **3885/3885, worst error 0.023 cm**,
  found by testing all 24 slot permutations.

The `+0x1c0` block is a 4-entry header (two correlated indices, the sub-object
index 0..15, and an `0xffff` sentinel present in all 3885) followed by **two
8x8 grids of 12-bit fixed point** (both top out at exactly 4096). Their meaning
is NOT established -- and a monotonic pattern that looked convincing in record 0
holds in only ~406/3885, so it was noise.

Table A is now **716/720 bytes structurally mapped**, ~452/720 semantically
resolved.

### M4 part 6: render fields ported + two gari-only claims caught by the test
Ported the decoded render-side patch fields (`uvOffset`/`uvScale`, `cellIndex`,
the corner cache, the raw 8x8 grids) and validated across all **28,484
patches** in the 10 shipped tracks. Two of my own claims from the previous
pass failed and were corrected:

* **`+0x1c4` is NOT the sub-object index.** Gari spans exactly 0..15, which is
  why it looked like one; **elysium spans 0..16 (17 distinct)**. Reverted to
  `unk1c4`.
* **The 12-bit grid reading survives, but needs a sentinel.** The raw max
  across tracks is 65535; excluding `0xffff` it is **exactly 4096 everywhere**.
  `0xffff` = "no value" (gari has none, which is why the first pass missed it).

Held on all 28,484: `uvOffset` a multiple of 1/16, `uvScale` exactly
(1/16,1/16), cached corners == `eval()` (worst 0.031 cm), and every patch's
AABB inside its claimed cell in XY.

Lesson worth keeping: gari has been the single-file sample for most of this
decode, and it is unrepresentative in at least two ways (no grid sentinels, a
sub-index range that coincidentally fills 0..15). Cross-track validation is not
a formality here.

### M4 part 7: table E field map; two fields verified, two hypotheses rejected
Byte-searching for the 8x8 grids' reader produced only false positives again
(`FUN_0013b720`, which keeps surfacing in these searches, is a **constants
initialiser** -- its "hits" are float literals, not displacements). Switched to
table E, which is higher value: it is the shared palette every sub-object
references.

Built a per-dword distinct-value census over all 942 gari records, then
verified the two structural readings across 4 tracks (1,579 records):
* `+0x1c..+0x24` is a **unit vector**: 1579/1579, zero exceptions.
* the AABB is `+0x28..+0x30` (min) vs `+0x40..+0x48` (max): **1579/1579**;
  the competing pairing scores 0/1579.

Rejected and recorded so they are not retried: `+0x34..+0x3c` as the AABB
centre (0/1579, worst 2.8e5 cm), and the unit vector as a plane normal with
`+0x4c` as its constant (0/1579).

Table E is now a clean field map with 2 of 13 fields semantically resolved.

### M4 part 8: THE RENDER MESH FOUND; header fully regular; materials decoded
Three linked breakthroughs.

**1. The `.xbd` header is regular** -- every table's base field is
`countField + 0x48`, giving **15 table slots** (14 populated), not the 7
previously known. Strides recovered by solving contiguity, exact on every
track. Two of the slots are large *single blobs* (count == 1).

**2. Table G = materials.** `FUN_000b03c0` (the texture remap) walks
`xbd[0x1c]` records at `xbd[0x64]`, stride `0x48`, rewriting the four leading
shorts through a remap array and skipping negatives -- so they are `.xsh`
texture indices with -1 = unused. 1,159 materials across the 10 tracks; slot 3
is unused in all of them.

**3. The `+0x54` blob is the RENDER MESH**: exactly `patchCount * 320` bytes on
every track, 16 vertices of `{float3 pos, float u, float v}` per patch -- a 4x4
**Bezier control net** with texture coordinates.

It is the *same surface* as the collision patch: the cubic Bezier -> power
conversion reproduces the stored `+0x50` coefficients for **28,484 / 28,484**
patches (worst 1.5 cm), and the net's corner control points equal the cached
corners with **worst error 0 cm**.

This also explains the earlier failed Bezier tests: `+0x50` really is the power
basis, and the control net simply lived in another table. Both representations
ship -- net for rendering, power form for fast evaluation.

Ported to `xbd::Patch::net`/`netU`/`netV` and `xbd::Material`, asserted in
asset_test. One test bug caught and fixed along the way: `net` is indexed
`[v][u]`, so the corner lookup needed the transpose (it read 2.08e4 cm before
the fix, 0 after).

### M4 part 9: the object vertex buffer decoded
The `xbd+0x58` blob is `u32 count` then `count` x 32-byte vertices:
`{float2 uv, float3 pos, float3 normal}`.

Found by scanning every (stride, start, field-offset) combination for the one
that makes a vec3 unit length -- **stride 32, start +4, normal at +0x14 gives
100.0% on all four tracks probed**, and the size then fits `count*32 + 4` with
exactly 12 bytes of alignment slack every time.

Positions are **model space** (roughly +/-70 m) against tracks spanning
hundreds of thousands of cm, so these are prop/object meshes that something
else instances -- the terrain is the Bezier net in the `+0x54` blob. UVs are
tiled, ranging well outside [0,1].

Ported as `xbd::ObjVertex`; asserted across all 10 tracks.

### M4 part 10: table B decoded as the OBJECT INSTANCE table
`+0x00..+0x3f` is a row-major 4x4 affine transform (row 3 = world
translation), `+0x40` an object index (duplicated at `+0x70`), `+0x4c`/`+0x58`
an AABB.

Across all 10 tracks, **24,563 instances**: w column exactly (0,0,0,1) in
24563/24563, rows orthonormal in 24535/24563 (the rest carry a scale), AABB
ordered 24563/24563. The object index's range matches the `xbd+0x68` table's
count *exactly* on every track (gari 0..647 vs 648, etc.).

**Refines an earlier claim**: I had written that table B "is NOT file geometry,
populated by runtime registration". That applies only to its *collision* path
-- the gate at `rec+0x68 & 0x20` is zero in every shipped file. The instance
data itself is entirely on disk.

Rejected and recorded: the `xbd+0x68` stride-8 records as
`{firstVertex, vertexCount}` -- the counts sum to 13,957 against 29,506
vertices and do not tile.

The render chain so far: **object instance (B) -> object index -> [xbd+0x68
table, unresolved] -> object vertices (xbd+0x58 blob)**, with terrain on a
separate path: **patch (A) -> Bezier net (xbd+0x54 blob) + material (G)**.

### M4 part 11: material lists decoded; a table-B claim CORRECTED
`FUN_0013b3e0` (vtable slot 0x48) shows `xbd+0x68` is **variable-length**
records `{u32 n; u32 materialIndex[n]}`, not a stride-8 table -- the old
reading came from dividing the region by its count. The walk lands EXACTLY on
the region end on all 4 tracks probed, with zero out-of-range indices and a
maximum index of exactly `materialCount-1` every time. All 10 tracks: **7,203
lists, 7,915 refs, 0 dangling.**

**Correction to part 10.** I claimed table B's `+0x40` indexes the `xbd+0x68`
material lists because its range matched that count exactly on every track.
Not decisive -- `xbd+0x68` and `xbd+0x7c` have the *same* count. Table B's own
fixup (`FUN_0013b320`, vtable slot 0x2c) shows the index is resolved through
**`xbd+0x7c`**, and additionally that `+0x44` is a self-reference to another
table-B instance. Both corrected in the port; `Object::link` added.

Lesson: a range match against a count is not identification when two candidate
tables share that count. The fixup function is the authority.

### M4 part 12: the object MESH blob chain decoded
`xbd+0x7c`'s values are **byte offsets into the `xbd+0x80` blob**, and each
mesh record is self-describing: size at `+0x00`, its own index at `+0x0c`,
AABB max at `+0x1c` and min at `+0x58`.

Three self-checks all close: offsets are exactly the running sum of the sizes
(216/216, 819/819, 648/648, 178/178), `+0x0c` equals the record index in 100%
of records, and on 3 of 4 tracks the sizes sum to the blob span with zero
slack. The port treats a break in either invariant as a parse failure.

**A guess corrected by the test**: min@`+0x58`/max@`+0x64` gave 625/648 on
gari; the real pairing is max@`+0x1c`/min@`+0x58` at 648/648. Across 10 tracks
7,201/7,203 are ordered -- the 2 stragglers are a real layout variant (a size
class whose `+0x58` holds integers reading as denormals), so the assertion
bounds the outliers instead of claiming 100%.

Mesh interior still undecoded: no field sums to the object vertex count on any
track, so meshes share the vertex buffer rather than owning disjoint ranges.

**The object render chain is now end to end:**
instance (B) -> `+0x7c` -> mesh record (`+0x80` blob) ; instance -> material
list (`+0x68`) -> materials (G) -> `.xsh` texture indices ; plus the shared
vertex blob (`+0x58`).

### M4 visualisation: the sandbox now draws the REAL decoded geometry
The M2d debug renderer previously drew only the `.ltg` index -- cell and
sub-cell AABBs. It now also draws, from the decoded `.xbd`:

* **bicubic ground patches**, each evaluated on a 4x4 grid from its power-basis
  coefficients and wireframed, tinted by `surfaceType` so material variation is
  visible directly;
* **edge curves** (rails/lips), each cubic segment sampled 8 ways;
* **placed object instances** as their world-space AABB footprints.

Toggles in the sandbox: `X` patches, `Y` edges, `BACK` objects, `RIGHT` colour-
by-surface-type, on top of the existing `A`/`B` bounds and `UP`/`DOWN`/`LEFT`
views. The `.xbd` load is optional -- the sandbox still runs on the `.ltg`
index alone if it is missing.

This means **M3 and M4 progress is visible now, without M5**. M5 is the
front-end (HUD, menus, cameras); it is not a prerequisite for seeing the
gameplay and geometry work, and treating it as one would mean building the
front-end before the thing it presents.

### M4 part 13: `.mxf` and `.afl` PORTED -- M4's named formats now all have modules
Both were decoded in the notes but had **no port module at all**. Closed.

**`.mxf`** (`port/src/assets/mxf.{h,cpp}`): header + `0x18C`-byte entry table.
The reader *enforces* what the notes recorded rather than assuming it -- the
entry table must exactly fill the gap to the data, and each entry's offset must
equal the previous offset + size. 25 model files, 168 entries, 0 unnamed. The
LOD ladder reads out as documented:
`Body3000(196352) Body1500(121400) Body750(75848) BodyShdw750(0)` -- including
the size-0 shadow mesh -- and the board file's regular/goofy pairs
(`Al AlGoofy Bx BxGoofy Fr FrGoofy shdwAl shdwALGoofy`).

**`.afl`** (`port/src/assets/afl.{h,cpp}`): the full curve-stream format --
12-byte header, 36-byte entry table, u32 curve-offset table, and the
mode-tagged segment streams including the 3-byte float (implicit `0x80` low
mantissa byte).

Validated across **every one of the 446 files in `anm.big`**:
`119,498 of 119,498 curve streams size cleanly, 0 parse failures` --
**exactly** the figure `scripts/classify_afl.py` verified, so the C++ port and
the Python reference agree file-for-file.

Mode census over the top-level headers: `const 36608, multi 46506, key8 21386,
cubic 8161, quad 3322, linear 3217, key16 298` -- and **zero** `rawkey`,
confirming the notes' observation that mode 4 is defined but unused in shipped
data. 6,498 entries, 1,523 carrying the redirect flag.

### M4 scope check
| M4 deliverable | State |
|---|---|
| `.xsh` textures | done (M1) |
| `.ffn` text | done (M1) |
| `.afl` skeletal animation | **done** |
| `.mxf` rider/board models | **directory done**; geometry interior open |
| `.xbd` -> real meshes | terrain done end to end; objects done to the mesh record; **index-buffer interior open** |

Still open in `.xbd`: the mesh record interior, table C (10 records), table E's
remaining 11 fields, and the two 8x8 grids in table A.

### M5 STARTED: the Widget base class read and ported
Began M5 at the foundation rather than a screen, since every front-end screen
composes from the same base class.

From `Widget_FindChildByType`, `Widget_GetChildAtIndex`, `UI_BuildIcon` and the
Widget constructor (0x000858a0): type tag at `+0x04`, prev/next at
`+0x08`/`+0x0c`, child list at `+0x10`, `+0xf0 = -1` and `+0xf4 = 0` at
construction, and a default 10x10 size. Widgets are allocated **0x100 bytes**
with a string tag and poisoned with `0xdeadc0de` first.

The child list is a **circular doubly-linked list with a sentinel** -- both
walkers stop when a node's next or prev points at itself. The port reproduces
that rather than substituting a vector, since the traversal semantics are
observable behaviour.

Ported `port/src/ui/widget.{h,cpp}`; asserted first-match ordering, the index
walk, past-the-end, the visibility bitmask (bit N = Nth child), and the
constructor defaults.

**M5 sizing still open**: this is the base class, not the screens. The ~20
screen builders remain tag-confirmed only, and reading them is what determines
the milestone's size.

### M5b: the outer ScriptVM ported -- the front end's sequencer
Found the real shape of M5: the front end is **script-driven**. `FEInit_Boot`
calls `Script_PlayByName("FEStartScript")`, which runs on the outer ScriptVM --
so menus need the sequencer, not just widgets.

Ported `port/src/game/scriptvm.{h,cpp}` from `ScriptVM_Tick` (0x00049890) and
`ScriptVM_DispatchOpcode` (0x0004a730): a 0xf0-byte object with a command
index, byte cursor and wait timer; commands carry their own length at `+0x04`,
which is how the cursor advances; the dispatcher returns 1 continue / 0 yield /
-1 terminate; 27 opcodes recorded, including op 4 = Wait and op 0x1a = spawn a
sub-script.

**A real behavioural subtlety, and a test that was wrong about it**: the wait
guard is `0 <= wait - delta`, so a timer landing *exactly* on zero still costs
a frame. `wait = 2` with `dt = 1` blocks frames 2 and 3 and only resumes on
frame 4. My first test asserted the intuitive behaviour and failed against a
correct port -- the header comment had already described the rule I then
contradicted in the test.

Only the sequencer is ported; opcode behaviour comes from a host callback,
since most handlers reach into systems that do not exist in the port yet.

### M4 close-out: mesh `blockCount` decoded, and a false "variant class" withdrawn
**Correction first.** I had recorded that 26 of gari's 648 mesh records "hold
integers at +0x58 that read as denormals, so the layout is not uniform". That
was an artefact of the WRONG AABB offsets. With the corrected pairing
(max@+0x1c / min@+0x58) the AABB is ordered in 216/216, 819/819, 648/648 and
177/178 records -- one outlier in total, not a class. Withdrawn from the notes
and from the code comment.

**Real finding**: `+0x04` is a block count (1 mostly; also 2/3/5/21/24), and it
is a perfect discriminator. Single-block records carry one ascending run of
section offsets at +0x2c/+0x34/+0x54; multi-block records never do --
**6,896/6,896 versus 0/307** across all 10 tracks. So multi-block records hold
one section set per block. `+0x08` is 60 on every record of every track.

**Where I stopped, and why.** The per-block section sets and the u16 index
buffer need the GPU draw path. Three inference attempts failed outright: no
field sums to the object vertex count, no field linearly predicts the record
size, and the 60-byte-block hypothesis does not hold. This format has punished
shape-fitting repeatedly (the Bezier-vs-power mixup, the stride-8 material
lists, the AABB offsets, this false variant class) and rewarded finding the
consumer every time. The next attempt should start at the draw call.

### M4: the GPU draw path found; vertex format independently confirmed
Pulled the right lever this time -- started at `D3DDevice_DrawIndexedVertices`
and read its callers instead of scanning bytes.

`MeshRenderer_DrawPartsList` (0x000ffdc0) yields three hard facts:
* `SetStreamSource(0, ..., 0x20)` -- the vertex stride is **32 bytes**, an
  independent confirmation of the `ObjVertex` layout previously derived from a
  unit-normal scan. Two unrelated routes, same answer.
* primitive type **6** = `D3DPT_TRIANGLEFAN`.
* index blocks are `{.., u16 count at +2, u16 indices at +4}`, straight from
  the draw call's arguments.

**And a structural result**: `Mesh_RegisterVertexBuffers` is called with the
`.xbd` base itself, reading `+0x10`/`+0x34`/`+0x58`/`+0x7c` -- the header's
CountV/CountO/BaseV/BaseO. Verified on 4 tracks: `xbd[0x10] == 1` everywhere
(one vertex buffer), `xbd[0x34]` is the mesh count, and every mesh record's
`+0x10` is **0**, the slot index the registration rewrites. **The `.xbd` image
IS the runtime mesh-set object**, which is why its header is so regular.

Section `+0x54` is the index data: its offset is even in **100%** of records
(a byte offset would be ~50%), the values show strip/fan repeats, and they
climb across records -- confirming meshes index a SHARED vertex buffer, which
is precisely why no field ever summed to the vertex count.

**Still open**: the in-file index layout. Walking the tail as
`{u16, u16 count, u16 idx[count]}` blocks consumes the record exactly in only
6 of 648, so the part objects are BUILT from the record, not mapped onto it --
the same pattern the D-records showed. Next step is the record -> part
constructor, which is a far narrower target than "the interior".

### M4 final: skinned stride found; part constructor NOT found after five angles
New: **`MeshRenderer_DrawSkinnedMesh` uses `SetStreamSource(0, ..., 0x40)`** --
skinned rider/board vertices are **64 bytes**, against 32 for static props.
That is the first hard number for the `.mxf` interior.

Also pinned the graphics-device vtable base at **0x001a2b38** (slot `+0x194` =
`Mesh_RegisterVertexBuffers`, called from `FUN_000b0660` with the `.xbd` base),
and traced the load chain `Level_LoadTrackAssets -> FUN_000b07a0 ->
FUN_000b0660`, which calls the model-slot object's **own vtable slot `+0x10`**
to parse the loaded pack.

**The part constructor was not found.** Five angles tried: draw-call xrefs, the
register-buffers xref (a lone DATA ref through the device vtable), the load
chain (stops at an unlocated slot vtable), tail-walking as index blocks (6 of
648), and displacement byte-searches (constants initialisers again).

The remaining question is now exactly one sentence: **what writes the vtable
into the 0x14-byte model-slot objects at `modelBank + 4 + i*0x14`?** That
yields the parse method -> the part layout -> the index buffers.

M4's milestone deliverables are complete -- every named format loads and
validates against real data, and terrain renders. This interior is a research
item, not a missing deliverable, and it is filed with a far sharper target than
when the milestone opened.

### M5a continued: widget property cascade + transition animation ported
**Correction**: my first M5a pass recorded Widget `+0xf0`/`+0xf4` as an `id`
and a `flags` word (from the constructor writing -1 and 0). They are the two
**named child references** behind the engine's property cascade --
`Widget_SetFlagA`/`SetFlagB`/`SetRect`/`AddOffset`/`PlaySound` each write a
local field then forward the identical call to `+0xf0` (unconditionally) and
`+0xf4` (if non-null). The -1/0 just means unset. Fixed in the port and tests.

Also ported `Widget_UpdateTransitionAnimation`: state 2 adds the four deltas
and drops to 1; state 4 multiplies **only the first three** (the fourth is left
alone) and drops to 3; states 0/1/3/5 fall through; anything else returns early
without the common tail. That first-three asymmetry is exactly the sort of
thing that drifts silently in a reimplementation, so it is asserted.

New field map: `+0x10` flagA, `+0x14` transition state, `+0x30` deltas,
`+0x40` animated values, `+0x50` rect.

M5 now has: the Widget base class with its real cascade and animation
semantics, and the ScriptVM sequencer. Next: the ~20 screen builders.

### M5b: the button-prompt bar decoded and ported
`UI_BuildButtonGroup` (0x00086170) + its table at `DAT_001b9d00`: 17 rows of
12 bytes (bit, icon group, string id), one `"f3dBUTTON"` widget per set bit
into an `"f3buttns"` container. Buttons emit in **table order**, and the
builder **clears each bit as it consumes it**.

All 16 real rows resolved against `american.loc`: next, select, play video,
previous, yes, no, continue, retry, cancel, save, load, delete, format,
options, accept, tutorial. Icon field takes exactly three values (0x3e/0x3f/
0x40).

**A closing cross-check**: earlier boot-sequence work had recorded that
`UI_BuildTitleHelp` calls `UI_BuildButtonGroup(2, 0)`. Mask 2 resolves to
exactly one button -- **"select"** -- which is what the title screen displays.
Two separate investigations, months apart in project time, agreeing without
being fitted to each other.

Ported as `port/src/ui/buttongroup.{h,cpp}`; asset_test re-resolves all 16 ids
against the real .loc (16/16), verifies the title mask, and verifies table
ordering for a multi-bit mask.

### M5c: widget catalogue (78 types) + character roster ported
Swept every `FUN_00150d70("<tag>", <size>, 0)` call site: **78 distinct widget
types with their allocation sizes**, 0x10 to 0x1580. Two tags legitimately
carry two sizes (`f3DVDtxt`, `f3wsngltxt`) -- consistent with this file's
earlier note that `f3wsngltxt` has at least 7 call sites. Cross-checked against
six builders read directly: 6/6 agree. Ported as `ui/widgettypes.{h,cpp}`.

Read `UI_BuildCharacterSelect` (0x0007e680): 4 player slots at `screen+0x64`,
gated on the same `DAT_001df3f4` boot-state global the title sequence uses.

Ported the character roster in the engine's index order from
`LoadScreen_FormatRiderTextureName` -- 0 eddie ... 11 marisol. **Did not
re-derive it**: the notes already settled this (including Mac vs Marty), so I
checked the index first per the standing rule. The port then re-verifies it
against shipped assets -- all 12 riders have both `_body` and `_head` .mxf in
`char/mdlxbx.big` (12/12), and all 12 names resolve in `american.loc`, with
`marty` absent, exactly as the NTSC-build explanation predicts.

### M5d: the title screen's button bar is now TABLE-DRIVEN
`boot_flow.cpp` previously hardcoded the single "select" prompt with a literal
string id and a literal sprite rect. It now calls
`ui::expandButtonMask(Btn_Select, ...)` and renders whatever the decoded table
returns -- so **any** screen's mask will draw correctly, not just the title's.

Added the icon-group -> face-button mapping: group `0x3e` is the A-button set
(next / select / play video / accept), `0x3f` the B-button set (previous /
cancel), `0x40` the rest (delete / format / options / tutorial). Sprite rects
still carry their existing caveat -- measured from the `fe_1.xsh` atlas, not
read from the engine's own per-shape UV table, whose writer is unlocated.

Removed the now-dead `kSTRSelect` constant rather than leaving it behind.

### BREAKTHROUGH: the `.map` files are the toolchain's own manifest
Chasing `FEStartScript`'s bytecode led somewhere better. Every track's `.big`
ships a `<track>.map` next to the `.xbd`, and it is **plain text** -- the
ColdFusion toolchain's manifest, stamped `Compiled (Oct 29 2001)` with the
original art path `D:\ssxdvd\art\FrontEnd\NorthAmerica\SSXFE`.

**It matches the binary table-for-table**: PATCHES 3885 = table A, INTERNAL
INSTANCES 3393 = table B, MODELS 648 = the mesh table, CONTEXT BLOCKS 648 =
the material lists, SPLINES 169 = table F, LIGHTS 942 = table E, PARTICLE
MODELS 10 = table C. Seven exact matches.

**The decisive one**: the MODELS section's Ref column is each model's instance
count. It sums to **3393, exactly table B's record count**, and per-model
**648 of 648 agree**. That is the original tool's 2001 output validating a
decode derived years later from the binary alone, with nothing fitted to it.

**Two tables identified outright:**
* **Table E = LIGHTS.** Artist names encode the type (`_Am_`, `_Di_`, `_sp_`,
  `_pt_`); correlating them against the `+0x00` enum gives 0 directional,
  1 spot, 2 point, 3 ambient. This retro-explains every structurally-known
  field: the unit vector is the light DIRECTION, the AABB its influence
  volume, the 0.70711 cosine the SPOT CONE (cos 45 deg), and the flags
  0/0x100/0x200 the type. Ported as `xbd::Light`; across 10 tracks **4,026
  lights**, all with unit direction, ordered AABB and type in 0..3.
* **Table C = PARTICLE MODELS** (10 rows, `Fog_Sphere_A_*`).

**SPLINES independently confirms the edge decode** -- the names are
`Spline_FenceRail_*`, exactly what the D/F cubic chains were deduced to be from
geometry alone.

One mismatch, recorded honestly: MATERIALS has 219 rows against table G's 125,
so G is not simply "the material list". Still open.

Side find: `tricky.ser` inside `ssxfe.big` is a **big-endian BIGF holding PS2
memory-card icon data** (`icon.sys`, `nhl.ico`, `pstats.ps2`) shipped unused in
the Xbox build. Not script data -- recorded so it is not chased again.

### `.map` cross-check completed -- and it corrected a fresh claim
Ran every `.xbd` header count against every `.map` section row count on four
tracks. Confirmed: `+0x08` PATCHES, `+0x14` INTERNAL INSTANCES, `+0x20`/`+0x34`
MODELS and CONTEXT BLOCKS, `+0x24` LIGHTS, `+0x28` SPLINES.

**Correction to the entry I wrote two turns ago**: table C (`+0x18`) is
**PARTICLE INSTANCES**, not PARTICLE MODELS. On gari both sections have 10
rows, so that track cannot distinguish them; alaska (27 vs 17) and elysium
(59 vs 19) do. Models live at `+0x38`.

That is the **fourth** time gari specifically has been unrepresentative -- no
grid sentinels, a sub-index coincidentally filling 0..15, zero AABB outliers,
and now equal particle counts. Noted in the format file: cross-track checks are
mandatory for this format, not a formality.

**MATERIALS resolved as a mismatch that makes sense**: 219/222/176/81 rows with
no table of that size, because table G is a per-object material *binding* of 4
texture indices, and those indices' maxima (120/193/107/72) fall inside the
corresponding MATERIALS counts on every track. G is the binding, MATERIALS the
palette -- and the palette is not a `.xbd` table.

### TASK #31 SOLVED: the mesh build chain, end to end
The blocker was "what writes the model-slot vtable". It is **`FUN_000b05d0`**,
the model-bank constructor, looping 10 slots and writing `PTR_FUN_0019aac0`.
Slot vtable `+0x10` is the parse method (`FUN_0013a8a0`), which just stores the
pack and calls `Resource_RelocateBlockPointers` -- so there is no separate
"part constructor"; **the relocation IS the build**, which is why five earlier
searches for a constructor found nothing.

Chain: `FUN_0013aa20` walks mesh records by their size field ->
`FUN_0013aa50` per record (sub-block array at `+0x08`, index at `+0x0c`) ->
`FUN_0013aa80` sub-blocks of **stride 0x18** (three group pointers that
frequently alias, a secondary pointer, and a matrix where **-1 = the default at
DAT_001fadd0**) -> `FUN_0013ab20` per group (**count at +0x20, part array at
+0x28**) -> `FUN_0013ab70`, a no-op stub.

The group's count/array offsets are **exactly** what `MeshRenderer_DrawPartsList`
reads as `part[0x54][0x20]`/`[0x28]` -- build side and draw side agreeing from
opposite directions.

**Three corrections to my own record layout**: `+0x04` is the sub-block count
(I had the idea right), `+0x08` is the **sub-block array offset** and not a
constant (I had recorded "constant 0x3c"), and `+0x0c` is an index the loader
resolves through a table, not a self-id -- it merely happens to equal the
record ordinal.

Ported and verified on all 10 tracks: **7,203 mesh records, subBlockOffset ==
0x3c in 7203/7203, 8,615 sub-blocks, 8,405 geometry groups, all 8,405 with a
resolvable part array.**

One link left: the sub-part records. `vtable[0x44]` is a stub so they are not
relocated, yet the draw path treats `subpart[+0x08]` as an index-block pointer.
That is now a single well-posed question instead of "the interior".

### The last `.xbd` link: located precisely, and it is a RUNTIME step
Decoded every remaining relocation callback -- `Resource_RelocateBlockPointers`
calls 13 vtable slots and **none touches a sub-part**; the per-sub-part hook
(`vtable[0x44]` = `FUN_0013ab70`) is a no-op stub.

The reason is now clear rather than mysterious: **the draw-time "part" is not
the file record.** `MeshRenderer_DrawPartsList` reads `+0x04`, `+0x50`, `+0x54`,
`+0x58`, `+0x5c`, but a file sub-block is 0x18 bytes -- the offsets cannot fit.
`GfxContext_Init` (0x00104c40) confirms it: each draw mode is an allocated
**object** with the draw function in vtable slot 0, so what the draw path walks
is a **render-queue node** populated at submission time.

**The file side of `.xbd` is therefore complete.** Every table, every record
layout, and the whole load/relocate chain are decoded and asserted. What
remains is a render-submission step that copies a mesh group into a queue node
-- a different subsystem, and the only thing between the decoded data and drawn
prop meshes.

This is a much better answer than "the interior is undecoded": the boundary is
now known to be at the file/runtime seam, not inside the format.

### 3D IS NOW ON SCREEN -- perspective camera added to the sandbox
The block on "seeing 3D" was never the mesh interior: the terrain is fully
decoded and was only ever drawn ORTHOGRAPHICALLY. Added a real perspective
look-at camera (`dbg::View::Persp3D` + `dbg::fitPerspective`) with near-plane
clipping, and wired an orbit control into the sandbox.

`ssxtricky.exe` now renders, in true 3D:
* the **bicubic ground patches**, evaluated from their power-basis
  coefficients and tinted by surface type;
* the **edge/rail splines**, sampled along their cubic segments;
* the **placed object instances** as world-space AABBs (24,563 of them).

Controls: `START` toggles 2D/3D; in 3D the D-pad orbits and A/B zoom; `X`/`Y`/
`BACK` toggle the patch, edge and object layers in either mode.

**Two bugs caught while wiring it**, both mine and both from adding bindings to
an already-loaded key set:
1. `START` briefly did double duty (toggle 3D *and* reset the rider). Bindings
   are now **mode-scoped** -- the D-pad and A/B mean different things in 2D and
   3D and must not both fire.
2. Even after scoping, leaving 3D would still have triggered the 2D-only rider
   reset in the same frame, because the toggle ran before the check. Fixed by
   capturing the mode into `wasPersp` before toggling.

What is still NOT drawn is prop/rider **meshes** -- that needs the render
submission step (task #32), not the file format, which is complete.

---

## `.cml` container SOLVED -- the front-end camera is now the game's own

Deep-research pass on the `.cml` loader's linking step. Result: **the container
is statically walkable**, and two earlier claims in
`RE_NOTES_camera_system.md` were wrong and are now retracted in place (see the
`RETRACTED` markers at lines ~141 and ~575 there, plus the new closing section).

**What was wrong.** The notes said `.cml` container children are "linked via
runtime pointers the game's loader fills in" and that "the FE camera position
cannot be extracted from the file by static walking alone". The mistake was
reading record `+0x00` as a size field and treating the neighbouring
`0xDEADC0ED` words as unresolved links. `0xDEADC0ED` is allocator poison in
**unused slots** -- it is the list terminator, not a pointer.

**What is true.** `FUN_00075120` (reached from `CameraScript_LoadFile` ->
`FUN_000753a0` -> `FUN_000752e0`) walks a per-category singly linked list whose
every link is a file-relative offset, made absolute by adding the file base.
`CameraScript_LoadFile` performs no relocation at all, which is why it works
directly on the raw file. Node: `+0x00` next, `+0x04` size, `+0x08` data
offset, `+0x0c` name hash, `+0x10` name. Heads at `base + 4 + category*4`.

The name hash is `FUN_00074fe0`:
`h = (h << 23) ^ ((int)h >> 7) ^ (int)c`, arithmetic shift, signed char.

**Verified on all 13 shipped `.cml` files**, and the strongest check is the
hash: a function reproduced from the disassembly agrees with **2608 / 2608**
stored hashes, which confirms the hash and the walk independently of each other.
Also: 977/977 camera records parse (895 with commands, 82 empty stubs), 3241
commands with 0 non-monotonic times, `endTime == next.time` 2346/2346, and
259/259 scene slots with heading always within +/-pi.

**Deliverable.** `port/src/assets/cml.{h,cpp}` + a test block in
`port/tests/asset_test.cpp`. The title screen's camera is no longer invented:
it is `scripts.cml` scene `VEN_1` slot `V_1`,
`pos (-703.6, -104.4, 279.8) cm, heading -1.56 rad`, confirmed loading at
runtime in `port_log.txt`. Those placements land inside `ssxfe.xbd`'s own object
bounds at Z ~= 280 cm, i.e. a camera height, which is an independent sanity
check that the numbers are placements and that Z is up.

**One correction to my own earlier count in this session:** an intermediate run
reported 2591 commands. That figure came from a first pass that used a guessed
terminator heuristic; with the real poison terminator the count is **3241**, and
the two agree with the per-length histogram. The 2591 is superseded.

**Still open here:** pitch and FOV for that camera. They are not in the Scene
record -- they live in the 404-byte **Animation Script** records (category 0),
which hold angle/damping-looking values (`225.0`, `30.0`, `1000.0`, `0.2`,
`0.6`, `-0.03`) that are not decoded yet. The port renders level with a default
FOV and labels that on screen rather than passing it off as original.

---

## Correction: `.cml` Scenes are rider placements (same session)

I shipped the title-screen camera as `scripts.cml` scene `VEN_1` slot `V_1`.
That was wrong and is now reverted. `FUN_0007a150` -- the only consumer of the
cat-5 Scene object -- reads slot `+0x14` as a **participant selector**
(0 = user, 10/11/12 = roles, 13 = any remaining rider, 14..25 = character
`v - 0x0e`, matching the 12-rider roster) and walks 6 slots of 0x30 bytes. The
slots place RIDERS on a staging set; `VEN_1` is the player plus five fillers.

The container/hash/command decode and every verification count from the
previous section are unaffected. Only the Scene semantics changed. Full
write-up and evidence in `RE_NOTES_camera_system.md`.

The port now uses the placements only to locate the set and frame the view on
it, labelled `"framed on the VEN_1 rider stage (camera ours)"`. The authored
front-end camera remains **unrecovered**, blocked on the category-0 Animation
Script records.

## `.bnk` sound-bank container decoded (format only)

Chasing the front-end navigation sound. `data/audio/zbxfe.bnk` (inside
`audio.big`) is the front-end bank.

```
+0x00  'BNKl'
+0x04  u16 version (5)
+0x06  u16 slot count (65)
+0x08  u32 offset of the sample data (0x420)
+0x10  u32 sparse slot table
```

Sound descriptors are EA **`PT`** records: after the 4-byte `PT` tag, a stream
of `[key][len][big-endian value]` fields ending at key `0xFF`; keys `0xfd`/
`0xfe` are standalone markers with no length. Keys seen: `0x82` channels,
`0x84` sample rate, `0x85` sample count, `0x88` data offset, `0x89`, `0x8a`,
`0x8b`, `0x8c`, plus `0x06`, `0x0b`, `0x0e` (volume -- two records share a data
offset and differ only here), `0xa0`, `0x11`.

Scanning for `PT` yields **13** records (the header's 65 is slot capacity, not
record count). All stereo 32 kHz except one at 44.1 kHz, durations 1.5-7.4 s --
so this bank is front-end music/stings, **not** UI blips. Sample data is raw
EA-XA with no SCHl/SCDl framing, i.e. `eaxa::decodeChannel` should apply
directly once a reader is written.

**NOT done:** no `.bnk` reader in the port yet, and the cue the front end plays
on menu navigation is still unidentified -- `Widget_PlaySound` turned out not to
be an audio function at all (see below), `Audio_PlayScaledCue` has only a
ScriptVM caller, and the FE input handlers
(`FEStateMCOverlay_HandleButtonInput`) only return event bitmasks.

## Rename to fix: `Widget_PlaySound` (0x000a4bf0) is not a sound function

It reads `DAT_001e3c7c + 0x720`, which `RE_NOTES_application_boot.md` records as
the **graphics device**, and calls its vtable `+0xec` (get) / `+0xe8` (set)
around cascading `+0x2c` to the named children -- the same `+0xe8` slot
`TitleIntroSequence_Tick` uses to select render passes (1, 2, 5, 8). So it
pushes a render-layer offset (`widget+0x60`), draws the subtree, and restores.
`UI_BuildTitleHelp` setting `+0x60 = 2` on the button group fits.
Suggested name: `Widget_RenderSubtreeWithLayerOffset`. Not yet renamed in
Ghidra.

## Camera modes decoded -- the front-end camera is now the game's own

Category 0 ("Animation Script") is the **camera mode** table. Anchored on
`VenueStaging_SetActiveCameraMode` (record `+0x168`/`+0x170`, resolved by
disassembly because Ghidra conflated the accessor and the record) and
`ReplayCamera_ApplyViewParams` (writes a distance clamped to [200,1200] into
the live camera at `+0xcc` -- the same offset the file record uses, so record
and live camera share a layout).

**945 of 1049 records carry a world position** at `+0x3c/+0x68/+0x94`.
`scripts.cml`'s `STG_VEN_1` reads as: set scene `VEN_1`, camera to `VEN_Est1`,
blend to `VEN_Est1a`, hand over to follow-cam `VEN_char1` at t=2.5, end at 7s.
So **op 14 = set scene, op 0 = set camera mode, op 1 = blend**.

Verified: all four `VEN_Est*` shots land inside `ssxfe.xbd`'s object bounds.
The port plays the script positionally; the eye is the game's, the look-at is
still ours (orientation field undecoded). A tempting "flag then value" field
pairing was tested and **cleanly rejected**, so it is not recorded as fact.

Full detail in `RE_NOTES_camera_system.md`.

**Second misnamed function found:** `Audio_PlayScaledCue` (0x00078430) scales
camera `+0xcc`, i.e. camera DISTANCE, not audio. Together with
`Widget_PlaySound` (really a render-layer scope), two "audio" names in this
project turned out to be non-audio. Neither renamed in Ghidra yet.

## Sound manager entry points recovered from undetected code (runtime bring-up)

Three functions the lifter never detected were recovered from the raw XBE
bytes and translated, all reached only through indirect calls:

- **0x0011D7A0** -- the sound manager's heap creation. Allocates a
  **0x4B000-byte (307200) named heap `"mSoundHeap"`** (the literal lives at
  `.rdata` 0x001A6334), fills it with the debug pattern `0xDEADC0ED`, stores it
  at `this+4`, calls its own virtual init through `[vtbl+4]`, then hands the
  block to `sub_00013DA0(block, 0x4B000, 0x71A00)`. `ret 4`.
- **0x0011D6F0** -- that virtual init. Builds a 0x108-byte audio configuration
  block, stamps in the queried channel count at `+0x26` and a fixed budget
  `0xB6140` at `+0xF8`, then picks one of two voice layouts from its argument
  (arg == 2 -> 0x480/0x420/0x24; arg <= 1 -> 0x420/0x24). Returns 2.
- **0x0014C300** -- the chunked-file-read continuation. Object layout recovered:
  `+0x00` context, `+0x04` handle, `+0x08` file offset, `+0x0C` bytes left,
  `+0x10` running total, `+0x14` current chunk size, `+0x18` destination cursor,
  `+0x1C` read function pointer, `+0x20` last result. Re-issues the next chunk
  capped at **0x2000 bytes** and re-registers itself as the callback via
  `sub_0014E130`.

Also decoded in passing: **`sub_000A8EE0`** is a ring-buffer advance over a
multi-buffer writer -- `offset = base + stride * index`, iterate `this->[0x14]`
children calling `[vtbl+0x24]` then accumulating `[vtbl+0x20]`, then
`index = (index + 1) % this->[8]`. Its `this` comes from
`Application_TickFrame`'s `+0x28`.

The `.rdata` descriptor table at **0x001A6330** holds `{ fn 0x0011D890,
"mSoundHeap" }` followed by `"XboxSoundManager"` at 0x001A6340 -- useful
anchors for naming this class.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part thirty-six.

## D3D8 render-state family recovered — the title's GPU command emitters

Twenty functions in the XBE's D3D section (0x00166F80–0x001775D8), all reached
only through the device vtable and none previously translated. They are
Microsoft's inline `D3DDevice_SetRenderState_*` family: each reserves space in
the push buffer via `sub_0016B920`, writes an NV2A method header plus its value,
commits the write pointer, and caches the value in a shadow global for
redundancy filtering.

**Push-buffer context** lives at `0x001776C0`; the buffer itself was observed at
Xbox VA `0x0108A000` with a limit of `0x010A9DFC` (~128 KB).

**Method headers decoded** — `(count << 18) | method`:

| function | header | method | count | shadow |
|---|---|---|---|---|
| 0x001670C0 | 0x80320 | 0x320 | 2 | 0x001748BC |
| 0x001670F0 | 0x41E6C | 0x1E6C | 1 | 0x001748CC |
| 0x00167120 | 0x402A8 | 0x2A8 | 1 | 0x00174888 |
| 0x001671E0 | 0x403A0 | 0x3A0 | 1 | 0x001748A8 |
| 0x00167220 | 0x403A4 | 0x3A4 | 1 | 0x00174898 |
| 0x001672A0 | 0x40380 | 0x380 | 1 | 0x001748D0 |
| 0x00167400 | 0x417BC / 0x817BC | 0x17BC | 1 or 2 | 0x001748B8 |
| 0x00167490 / 0x001674E0 | 0x8038C | 0x38C | 2 | 0x0017488C |
| 0x00167540 | 0x40328 | 0x328 | 1 | 0x00174884 |
| 0x00167E20 / 0x00167E80 | 0x41D84 | 0x1D84 | 1 | 0x001748DC / 0x001748E0 |
| 0x00167F40 / 0x00167F90 | 0x41D7C | 0x1D7C | 1 | 0x001748C0 / 0x001748C4 |

Also identified but not yet translated: **~15 C++ static initialisers** at
0x001652E0–0x00165FA0 (`mov ecx,obj; call ctor; push dtor; call atexit
0x0015D044`) — global constructors that have never run — and 11 identical float
helpers at 0x00166CB0–0x00166DF0 computing `dst = src / [0x001878C4]` over
src 0x001A7A9C+4i, dst 0x001FAE40+4i.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part thirty-nine.

## Per-thread TIBs, and nine more functions recovered

The runtime now gives every guest thread its own Thread Information Block
(pool at Xbox VA 0x00750000, 64 slots of 0x100 bytes) instead of one shared
block at VA 0. `fs:` is genuinely per-thread on hardware and the title relies
on it — the SEH exception-chain head at `fs:[0x00]` and the CRT's per-thread
TLS pointer at `fs:[0x28]`.

That unblocked the CRT lock table (`_mtinitlocks`, now run at startup) and with
it the eleven C++ static-object constructors at 0x001652E0–0x00165FA0 plus
`sub_00166DB0`, all of which had been out of the dispatch table since the
thirty-fourth pass.

**Functions recovered this pass** (all translated from the original bytes):

| address | what it does |
|---|---|
| 0x000F95C0 | copies a 5-dword template from 0x001EAD20 into `this+4` |
| 0x000F9B80 | copies three dwords from arg0 into `this+0x44` |
| 0x0013A780 | zeroes `this+4` … `this+0x10` — the vtable[2] entry reached from `sub_000A7F20` |
| 0x0015C819 | CRT float-state reset (`fnclex`) |
| 0x001629FD | the CRT's real `InitializeCriticalSectionAndSpinCount` (5 instructions) |
| 0x001805B2 | XPP: publishes a descriptor through kernel thunk slot 15 |
| 0x00182765 | XPP: stamps a 4-byte `FF 80 80 80` marker at `this` |

**Held back, deliberately** — faithful translations that stop the title loading
its `data\lang\*.loc` archives when dispatched: `sub_0015CFE4` (CRT 0x80-byte
control block → 0x0020500C/0x00205008) and `sub_0016414C` (one-shot init guarded
by 0x00205010, calls `sub_0016402B`). The downstream consumer of those globals
is the thing to fix.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part forty-four.

---

## Pass forty-six — the boot stall was an HLE gap, not a recompilation bug

The `Application_InitSubsystems` stall (pass forty-five's "corrupt CRT free
list") is fixed, and no lifted code was at fault. The allocator, the block
splitter, `CRT_MemCopy`, and the whole `fpo_leaf` `esp` chain were each verified
against `objdump` of the original XBE and cleared.

**Root cause** — `xbox_NtQueryInformationFile` did not implement
`FileNetworkOpenInformation` (class 34) for handles on the mounted ISO, only for
files on the emulated hard disk. SSX's `FILE_size` uses that class to size a file
before reading it, so every `D:\data\lang\*.loc` query returned
`STATUS_NOT_IMPLEMENTED`; the title then called `NtReadFile` with the length it
never got back (`0xFFFFFFFF`) and read the whole archive over a 23-byte filename
buffer, wrecking the CRT pool's free-block headers just past it.

**Kernel/HLE fixes this pass**

| file | fix |
|---|---|
| `kernel_file.c` | implement `XboxFileNetworkOpenInformation` for ISO handles; log unhandled classes instead of failing silently |
| `kernel_bridge.c` | `bridge_KeCancelTimer` no longer overlays the host `XBOX_KTIMER` on guest RAM (it wrote an 8-byte host pointer through a guest VA) |
| `kernel_bridge.c` | the VA→timer map recycles instead of saturating at 64 and leaking; the old host timer is cancelled before its slot is reused |
| `xbox_memory_layout.c/.h` | new `xbox_VerifyViewIntegrity(tag)` — `VirtualQuery` sweep of the guest view plus a `HeapValidate` of the process heap |

**Still open, unchanged**: `sub_0015CFE4` / `sub_0016414C` remain held back (see
pass forty-four); `PXBOX_KDPC` and `PXBOX_KINTERRUPT` are still cast from guest
VAs at ordinals 137 / 98 / 152 and need the same side-map treatment
`KeCancelTimer` just got; 70 unimplemented SIMD stores remain.

**Status**: 3/3 runs exit 124, zero crashes, 0 unresolved ICALL targets, live
push buffer feeding 411 methods per frame, 0 draws yet.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part forty-six.

---

## Pass forty-seven — SSE was never implemented, and the blocker is DirectSound

**The file-read failure is fully fixed.** The last malformed path came from
`CRT_MemCopy`'s unaligned leg (`sub_001511AA`) copying 4 of 16 bytes. Reads now
stream correctly: 25 sequential 8 KB requests totalling exactly `american.loc`'s
197,712 bytes, against `0xFFFFFFFF` before.

**The cause was systemic.** The lifter declares every XMM register as a scalar
`float`, so `MEMF` (4 bytes) backed 16-byte moves and packed arithmetic was
emitted as comments. Fixed by mechanical transform of the generated sources
(not by regenerating -- the part-38 carry-flag fixes live in the output, not in
`lifter.py`):

| what | sites |
|---|---|
| `float xmmN` → `recomp_xmm_t` union declarations | 601 |
| 16-byte `movaps`/`movups` loads / stores | 2,960 / 2,306 |
| scalar (`movss`, `addss`, cvt, …) → `.f` | 1,398 |
| packed ops implemented (`addps` `mulps` `subps` `shufps` `minps` `maxps` `unpcklps` `unpckhps` `cmpltps`) | 4,240 |

**The remaining blocker is identified precisely.** Attaching to the live process
puts the main thread inside translated DSOUND, spinning on AC'97 channel-1
`CR.RR` at `0xFEC0011B` waiting for hardware that we back with plain RAM. The
loop reads the register once and spins on the cached byte, so a polling pump
cannot fix it — AC'97 clears RR as a side effect of the *write*.

**Three pieces of infrastructure exist but were never wired up**:
`src/audio/dsound_device.c` (a 384-line DirectSound HLE) is in no CMakeLists;
`recomp_lookup_manual()` in `recomp_manual.c` still returns NULL for everything;
`apu_hook_handle_mmio()` is a finished VEH MMIO decoder with no caller.

**Next**: route the game's DSOUND entry (`sub_001798D5`, called from
`sub_00014CCD`) through `recomp_lookup_manual` into the HLE, and add `audio/` to
the build.

**Status**: 3/3 runs exit 124, zero crashes, 15 file opens, archive loads in
full, push buffer at 411 methods/frame, still `draws=0`.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part forty-seven.

---

## Pass forty-eight — audio hardware emulated; the title reaches its main loop

The DSOUND wall is down. Two MMIO apertures now get device behaviour instead of
behaving as plain RAM, and the main thread runs `Application_RunMainLoop`.

| signal | before | after |
|---|---|---|
| file opens per run | 15 | **22** |
| push-buffer dwords | 608 | **33,117** |
| NV2A methods per batch | 411 | **1,238** |
| main thread | parked in DSOUND | **`Application_RunMainLoop`** |

**What was done**

| file | change |
|---|---|
| `src/apu/aci_mmio.c` / `.h` | new — AC'97 ACI device at `0xFEC00000`; `CR.RR` self-clears on write, as hardware does |
| `src/apu/apu_mmio_hook.c` | new `apu_mmio_install()` — brings up the xemu APU port and guards `0xFE800000` (512 KB) |
| `src/apu/apu_xaudio2.c` | resolve `XAudio2Create` via `LoadLibrary` (MinGW has the header, no import lib) |
| `ssx_recomp/src/main.c` | install both apertures; route VEH access violations to the two hooks |

The earlier "wire in the DirectSound HLE" plan was revised after measuring the
boundary: the game reaches DSOUND through 16 wrappers that do their own vtable
dispatch, so an HLE needs guest-memory objects with guest vtables, whereas the
hardware route reused two components that were already written but never called.
`src/audio/dsound_device.c` *is* compiled (pass forty-seven said otherwise —
that check looked at the wrong CMakeLists); it is simply never invoked.

**The title now loads frontend assets**: `hud.xsh`, `fe_1.xsh`, `menu.ffn`,
`music.inf`, `jukebox.inf`, and programs DMA contexts, surface format, register
combiners, texgen, fog and transform-program state.

**Next**: still `draws=0` — neither `SET_BEGIN_END` nor `DRAW_ARRAYS` is ever
emitted. 27 unresolved indirect-call targets (`[ICALL-MISS]`) are the prime
suspect; the 10 game-code ones (`0x000151F0`, `0x0012A720`, `0x00014080`,
`0x0014CD60`, `0x0012C900`, `0x0012AA60`, `0x0012B040`, `0x0012BEB0`,
`0x000F9DF0`, `0x000F98E0`) are the frontend batch and need hand translation —
`analyze_unresolved.py` has another title's section table and cannot be used.

**Status**: 3/3 runs exit 124, zero crashes, 22 opens, deterministic counters.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part forty-eight.

---

## Pass forty-nine — seven frontend functions recovered; the render path is live

Worked the `[ICALL-MISS]` list from pass forty-eight. Unresolved targets **27 → 20**.

**Landed**: `0x000F98E0`, `0x000F9DF0`, `0x0012A720`, `0x0012AA60`, `0x0012B040`
(small accessors and predicates), `0x00014080` (shutdown-callback table), and
`0x0014CD60` — the streaming-read continuation, which installs *itself* as its
own completion callback, which is exactly why nothing detected it.

**Held back, deliberately** (translated, verified as regressions, not registered):

* `0x000151F0` — the frame-pacing worker loop. Enabling it drops opens 22 → 17
  and push-buffer dwords 33,117 → 608; once the loop runs it starves the
  progress that happens without it.
* `0x0014CD40` — the close-path completion callback. Enabling it makes the close
  path execute and turns a bit-identical run into a non-deterministic one
  (segfault / 23 opens / 608 dwords across three runs).

Each function was verified on its own rather than as a batch — that is what
caught both.

**New**: a Release+`-g` crash backtrace put `SceneRenderer_RenderFrame` and
`SceneRenderer_SelectDetailLevel` on the stack, so **the title now reaches its
render path**, a consequence of the pass forty-eight audio unlock.

**Open**: a low-rate intermittent fault (roughly 1 in 6 to 1 in 20; 10/10 clean
at the end) in the CRT small-block allocator's free-list unlink
(`sub_0015663A`, `recomp_0007.c:47218`), reached from the render path. Not
bisected against the seven recoveries — the pre-recovery build only had six
clean runs behind it, which at this rate proves nothing.

**Status**: 10/10 runs exit 124, zero crashes, 22 opens, 33,117 dwords.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part forty-nine.

---

## Pass fifty — the intermittent fault is DSOUND refcounting

**Bisected the part-49 intermittent properly** (20-run arms, 12 s each): 2/20
with the seven recoveries, 3/20 without. Not caused by them — it came in with
the pass-48 audio unlock.

**Root cause**: DSOUND's COM reference counting was missing from the dispatch
table. `[ICALL-MISS]` returns 0 and carries on, so AddRef/Release were no-ops
and voice objects arrived as small integers (`0x17C`, `0x502`), which then fed a
zero channel count into a divide.

**Landed**: `0x001789A5` (AddRef) and its two 5-byte vtable thunks
`0x0017918D` / `0x00179192`. Unresolved targets **20 → 13**, and all five
implausible targets (`0xFFEE9230` etc.) disappeared — they were reads through
already-corrupted objects.

**Held back**: `0x00179411` (Release) — **18 crashes / 20** when registered,
opens 22 → 15, dwords 33,117 → 608. With both AddRef and Release missing nothing
was ever destroyed; enabling Release alone frees objects whose counts are still
too low because other AddRef sites remain unresolved. It goes in once the rest
of the refcount paths exist.

**Status**: 18/20 clean, 22 opens, 33,117 dwords, 13 unresolved targets.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part fifty.

---

## Pass fifty-one — `fst` was popping the x87 stack; 183 sites, all wrong

**Why nothing draws**: `Application_RunMainLoop` spins in state 3 (loading) at
`loc_000AA1D0` and never clears it, so `SceneRenderer_RenderFrame` runs
**exactly once** — that single pass is where all 33,117 push-buffer dwords come
from. Loading completes through `sub_0014CD40`, held back in pass 49, so it is
on the critical path.

**The bug**: enabling it exposed a 396 MB allocation loop in DSOUND buffer
creation. The descriptor the game passes was uninitialised — because the
zero-fill that prepares it (`sub_00157AA0` → `sub_00157AB9`) fills with x87
`fst`, and the lifter emitted `fp_pop()` after every one. `fst` stores st(0)
*without* popping; only `fstp` pops. After the first store every subsequent one
wrote uninitialised `_fp_stack[]` garbage.

**183 `fst` sites in the tree, every one wrong, none correct.** Fixed
mechanically. Descriptor `bytes` 1440874496 → 0, `fmt` → 0, OOM loop gone.

| configuration | clean | opens | dwords |
|---|---|---|---|
| before | 18/20 | 22 | 33,117 |
| `fst` fix, CD40 off | **19/20** | 22 | 33,117 |
| `fst` fix, CD40 on | crashes | 23 | 484 |

**Next blocker** (with CD40 on): D3D vertex-stream setup faults —
`sub_0016D990` / `recomp_0009.c:4655`, a 0x28-stride table walked with a garbage
index, reading unmapped `0xFC178218`.

**Status**: 18/20 clean, 22 opens, 33,117 dwords, `-O3 -DNDEBUG`, no probes.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part fifty-one.

---

## Pass fifty-two — close callback safe, title font loads

`sub_0014CD40` (held back in pass 49) is **re-enabled**: the pass-51 `fst` fix
removed the regression that made it unusable. With both in place the title now
loads **`D:\data\fonts\title.ffn`** — the title-screen font — at 23 opens and
the full 33,117 push-buffer dwords.

Stability, stated honestly: one 20-run batch gave 16/20, two later batches on
identical code gave 20/20 and 19/20 (39/40 combined). The fault is
timing-sensitive; a single batch is not the rate.

**Remaining render fault diagnosed**: `sub_0016D990` faults because
`MEM32(device + 0x470)` is garbage. The device is a static at `0x00174B30`, so
the field sits at a fixed `0x00174FA0` and could be watched with no probe. The
writer is `sub_0016A79D` (`device->0x470 = edi`), called from `sub_001043B0`,
and it is handed `0x21202808` then `0x00174B2F` — both nonsense. **The bad
vertex-stream pointer originates in game code**, not in D3D.

**Status**: 39/40 clean, 23 opens, 33,117 dwords, 13 unresolved targets.

Full runtime detail in `RE_NOTES_xboxrecomp_test.md` part fifty-two.

---

## Pass fifty-three/fifty-four — the draw question is closed

**The title does emit geometry.** `sub_00169A00` writes the NV2A header
`0x000417FC` = `NV097_SET_BEGIN_END`, and a `find` scan of the real push buffer
locates it at `0x0108C310`, immediately followed by method `0x1810` =
`NV097_DRAW_ARRAYS` and its payload.

**Two fixes got us there**:
* The push-buffer extent is `device+0x10`/`+0x14` (allocation #12,
  `0x0108B000..0x0128D000`, 2 MB), not `+0x04` which is only a segment limit.
* `nv2a_live_pb_tick` started parsing at the current write pointer, discarding
  everything the title buffered during device init and its first frame. It now
  replays from the real base: **33,117 → 524,766 dwords parsed**.

**Why `draws=0` anyway**: `submit_draw()` bails unless `inline_count != 0`, and
`NV097_DRAW_ARRAYS` has *zero* references in the translator. It implements only
the `INLINE_ARRAY` path; SSX binds vertex buffers via `SetStreamSource` and
draws with `DRAW_ARRAYS`. Not a fault — an unimplemented path.

**Next, and well-defined**: implement in `nv2a_pgraph_d3d11.c` —
`SET_VERTEX_DATA_ARRAY_OFFSET` (0x1720), `SET_VERTEX_DATA_ARRAY_FORMAT`
(0x1760), `DRAW_ARRAYS` (0x1810), and build a D3D11 vertex buffer from guest
memory.

**Corrections**: "the title never submits geometry" was wrong; and the
stride-`0x100` data is push-buffer payload, not a corrupt pointer table, which
retires the "D3D scribbles over the CRT pool" theory.

**Status**: 14/14 clean, 23 opens, 524,766 dwords.

Full detail in `RE_NOTES_xboxrecomp_test.md` parts fifty-three and fifty-four.

## Pass fifty-five — the reason nothing renders: one undetected epilogue

Two earlier conclusions corrected, one root cause found, one bug class opened.

**Corrected.** The "524,766 push-buffer dwords" from pass fifty-three were an
artifact: bring-up used the ring's *extent end* as the wrap point instead of the
producer's `limit` field, so one backwards pointer move replayed 2 MB of
never-written memory as commands. And the backwards move was not a wrap at all
but a segment reset (the driver rewinds once the GPU drains; our pump drains
instantly). Both fixed; the parse is now 1,214 dwords, 100% recognised. The
title emits **881 methods of device init and no draws whatsoever**, so
"the title does emit draws" was wrong.

**Root cause.** `Application_RunMainLoop` spins 3.8 M times/sec on an indirect
call to `0x0002108A`, which is not even an instruction boundary. The chain:

- `sub_0016C9FD` / `sub_0016CA04` are empty "not detected" stubs.
- They are the tail of `sub_0016C8A0`, a four-iteration texture-stage flush, and
  hold its entire epilogue (`pop edi/esi/ebp/ebx; add esp,0x1c; ret 4`).
- Returning early leaves `esp` 52 bytes low, through `sub_0016D920` and
  `sub_0016D990` into `sub_00169A00`, the DRAW_ARRAYS emitter.
- It reads its vertex count from the wrong slot: `0x21202808` instead of 4,
  reserves 2,170,926 dwords and writes DRAW_ARRAYS commands 8.7 MB past the end
  of the 2 MB ring, over the game heap and the Application object's vtable.
- The main loop then calls through that vtable. Hence the spin, and hence no
  draws ever reaching the push buffer.

**Fixed.** Translated `sub_0016C8D5`, `sub_0016C9FD`, `sub_0016CA04` from XBE
bytes. `sub_0016C8A0` balances and `DrawArrays` reaches D3D for the first time.

**Opened.** 225 reachable "not detected" stubs exist; each now self-reports on
execution. Only 10 actually run, nine of them in D3D:
`0x00171CCA 0x0016FFD5 0x0016A826 0x001690DA 0x001692FF 0x001677A0
0x00169F2B 0x0016D234 0x0016D0FC` and `0x0017E6FB` (DSOUND). A second 0x4C leak
via `sub_0016D410`/`sub_0016D730` is already confirmed. Draw parameters stay
garbage until these are translated.

**Also this pass.** Vertex-array draw path implemented in
`nv2a_pgraph_d3d11.c` (0x1720 offsets, 0x1760 formats, 0x1810 DRAW_ARRAYS,
guest-memory access, which the file previously lacked entirely; the
0x1680–0x1780 range had been on the ignore list).

**Tooling.** Push-buffer dword accounting + method histogram; kernel per-ordinal
rate histogram with generated names; ICALL-MISS hit counts, rate report and
`__FILE__`/`__LINE__` call sites; `XBOX_ICALL_TRACE` for a chosen target;
`sym.py` (link address → guest function); `ringwatch.py`; undetected-stub
reporting.

## Pass fifty-six — x87 memory operands recovered; 5,821 sites

- **Five more undisassembled fragments** recovered (0x0016D061, 0x0016D0FC,
  0x0016D123, 0x0016D234, 0x0016D2C3), closing the second stack leak.
  `sub_00169A00` now receives its real arguments: prim=6, start=0, count=4.
- **Tried and reverted** the project-wide x87 stack promotion. It is the right
  model but is blocked on the defect below; measured 30% -> 80% crash.
- **Found and largely fixed the largest remaining correctness defect**: every
  fadd/fsub/fmul/fdiv with a memory source was emitted as the popping *register*
  form, dropping the operand and corrupting stack depth. 8,309 sites; 5,821
  rewritten with operands re-derived from the XBE and verified by mnemonic
  agreement, 165 functions skipped as unsafe, 231 register-source sites left.
  Crash rate 80% -> 50% on this change alone.
- **Current blocker**: `Font_UnpackGlyphBitmapTexture` reads a garbage dimension
  (0x63F0) from the second font's header, asks for 1.3 GB, gets NULL, and
  memsets through it from guest address 0 — wiping `.rdata`, the NV2A context
  and the heap. Confirmed the sole writer into `.rdata` by page watch.
- Push-buffer consumer now refuses a write pointer outside the ring.

Net: 2/10 clean, probe-free. Lower than pass fifty-four's 14/14, which was
clean only because the title was doing nothing.

## Pass fifty-seven — re-seed the lifter instead of hand-writing

Stopped hand-recovering undisassembled functions and used the project's own
pipeline: `tools/disasm --seed-functions`, `tools/func_id`, then
`tools/recomp -f <addr>` per function, into separate output directories so the
manual fixes in `gen/` were not regenerated over. Seeding is iterative — the
recovered functions tail-call interior addresses that also need seeding — and
closed after four rounds at 25 functions.

Spliced into `gen/recomp_recovered.c` + `.h`, superseded stubs removed, 23
dispatch entries inserted in ascending order (the lookup binary-searches).

**2/10 -> 9/10 clean. 23 -> 191 files opened. ~14,700 -> 19 unresolved indirect
calls. 484 -> ~700,000 push-buffer dwords.**

The 1.3 GB memset from pass fifty-six is gone: the font path's vtable methods
were untranslated, so the indirect call returned zero and the caller ran on
garbage. The x87 drift counter reads top = 0, drift +0, independently confirming
pass fifty-six's operand fix balanced the stack.

The title now emits real per-frame work: 465 `DRAW_ARRAYS` and 465
`SET_BEGIN_END` pairs per five seconds. **Remaining gap**: it binds attribute 0's
buffer address (0x01132000) every draw but never writes any
`SET_VERTEX_DATA_ARRAY_FORMAT`, so the translator has no layout to read the
buffer with. Also fixed: VTXFMT stride is 8 bits, not 24 (cxbx reference).

## Pass fifty-eight — the missing vertex format, traced to its source

Full chain from "no geometry" to a single bad value:

1. The translator never sees `SET_VERTEX_DATA_ARRAY_FORMAT`.
2. The emit exists (`MEM32(eax) = 0x401760` at 0x0016DAB8 in `sub_0016D990`) and
   its gate passes -- `device+8 = 0x5FF`, bit 7 set.
3. It then tests `MEM8(MEM32(device+0x470) + 4) & 4`, and `device+0x470` holds
   `0x21202808`, not a valid guest pointer.
4. A page watch names the writer: `sub_0016A79D`, the tail of SetVertexShader.
   `sub_0016A770` shows bit 0 of the handle is a tag -- the pointer is
   `handle - 1`. The game passed `0x21202809`.
5. That handle comes from `renderobj + 0x15740`, set from the out-parameter of
   `sub_0016A290` = `CreateVertexShader`.

`CreateVertexShader` returns E_OUTOFMEMORY without writing the out-parameter when
its allocation fails -- but measured, **the allocation succeeds** and the sizes
are sane (696/1092/1156/596 bytes). What it returns is not: `0x00000028` once,
and `0x00F80388` twice for two different allocations. So `sub_00154E60` is
handing out an invalid address and a duplicate.

Next: separate whether the bad handle originates in that allocator or in
`sub_0016A2F9`, the success path that writes the handle out.

Housekeeping: removed ~493 MB of this session's logs, the scratch disassembly
binaries, and 12 stale `.bak` files. 8/8 clean, probe-free.

## Pass fifty-nine — the handle traced to a large-block allocation failure

Separated the two candidates from pass fifty-eight by measurement.

`sub_0016A2F9` writes the handle correctly (`MEM32(esp+0x20)` resolves to arg3,
`pHandle = 0x00F7FEBC`, the caller's own slot), and `esi` is intact from the
allocation to the write -- an earlier reading that suggested otherwise was
mistaking the `esi | 1` tag for corruption.

The allocation is the fault, and selectively so: **small blocks are healthy**
(flags=0x40, size 0xC -> cleanly incrementing pointers 0x20 apart), while
**large blocks** (flags=0, size 0x1E0-0x484) return addresses outside the heap
(`0x00000008`, `0x00000028`, `0x21202808`, `0x20702808`) and hand out
`0x00F80388` three times for three different sizes. `0x21202808` is simply one
of those returns.

Ruled out by measurement: the heap handle is valid (`0x00203CD4 = 0x00F80000`);
`heapcheck` shows 0 violations; the allocator's 32-function call graph has no
undetected stubs; and the SEH frame is correct (`ebp` stable, all three
arguments align with what the caller passed).

Next: `sub_0015663A`'s large-block path, entered with valid arguments.

Housekeeping: build directory cleared of 275 stale `.txt` captures (~21 MB),
15 gdb scripts and 3 empty files. 8/8 clean, probe-free.

## Pass sixty — second recovery batch, held back with measurements

Recovered 32 more never-discovered functions (19 unresolved indirect-call
targets plus their closure). Registering them dropped the title from 191 file
opens to 8 and from ~694,000 push-buffer dwords to 644, so they are **held back**
-- definitions in the tree, none registered.

Real defect found and fixed along the way: **four of the recovered functions were
duplicates of already-translated ones** that carry RE'd names --
`FX_TrailManager_Tick`, `Pool_FreeSlot`, `Localization_ResolveString`,
`Heap_Free`. The closure check had matched callees by `sub_XXXXXXXX` name instead
of by address, so it never saw them. Two divergent copies of one function, with
indirect calls reaching one and direct calls the other. Duplicates removed, the
eight call sites pointed at the existing definitions.

`sub_0015CFE4` and `sub_0016414C` remain harmful when registered, now confirmed
against the lifter's own translation rather than the earlier hand-written one --
so the hand translation was never the problem.

Added `scratchpad/try_reg.py` to register/unregister single dispatch entries, so
recovered functions can be enabled one at a time instead of as a batch.

## Pass sixty-one — `bsf` restored; the heap allocator is correct

`sub_00157A70` is two instructions, `bsf eax,ecx` and `ret`, and the lifter
emitted the `bsf` as a TODO comment. It is the bit scan `RtlAllocateHeap` uses to
find a larger free list when the exact-size one is empty, so the list index was a
stale register — which is why small allocations were fine (exact list populated,
scan never reached) and mid-sized ones returned wild pointers.

Implemented with `__builtin_ctz`. The allocator now returns clean monotonic
in-heap pointers with no duplicates, no out-of-heap values and no NULLs.
`0x21202808`, chased since pass fifty-six, was a wrong-list block.

**Trade-off, stated plainly**: memory now initialises differently and the title
stops early — 13/14 clean and 191 opens becomes 3/8 and 19 opens.
`GfxContext_ConstructSingleton` asks the **pool** allocator (`sub_00150AC0`,
table `0x203BE0`) for 2.3 MB, gets NULL, and the app unwinds through a NULL
vtable and terminates cleanly. The pool table is empty a second in and the 55 MB
block never appears. Fix kept: the longer run was built on a heap handing out
overlapping blocks.

Next: the pool allocator. Ruled out already — heap is growable
(`heap+0x14 = 2`, MaximumBlockSize 0xFF00), `NtAllocateVirtualMemory` is bridged
and working, and nothing over 1 MB reaches the CRT heap.

Also counted: **212 instructions across 42 opcodes** remain as `/* TODO */`
no-ops. Mostly SIMD, but this one was a single scalar `bsf`.

## Pass sixty-two — `__SEH_epilog` restored the wrong frame

`__SEH_epilog` (`sub_0015DEF5`) unwinds with `esp = ebp`, reading `ebp` from the
**global** `g_seh_ebp` rather than from the frame it is unwinding. Every nested
call between the SEH prolog and epilogue overwrites that global with its own
frame, so the epilogue restored esp to an unrelated function's frame.
**24 call sites, none of which published its own `ebp` first.**

Measured as 472 bytes lost per `CreateVertexShader` call, which walked
`GfxContext_Init`'s stack down 0xB14 by the time its epilogue ran — so its pops
read the wrong slots and `esi` (the Application object) came back as `0x105`.
That produced the NULL vtable call that ended the run.

Fixed at all 24 sites (`g_seh_ebp = ebp;` before the call). Verified: the three
allocator calls now measure +8/+8/+8 (was +8/+8/-464), `esi` survives at
`0x01614440`, and the vtable slot holds a real function pointer instead of NULL.
The title no longer terminates early — full twelve-second runs, opens 19 -> 22.

Same shape as the `bsf` bug and the earlier stack leaks: a per-invocation value
modelled as a global, silently wrong only when something nested touches it.

**Still open**: 4/10 clean. The remainder is an access violation in
`sub_0014BAC0`, a record walker that is handed `0xFFFFFFFF`; the arithmetic shift
makes its stride -1 so it walks backwards out of the mapped view. Reached from
`sub_0014D4DB -> sub_0014B9C0`. The `0x0014D6xx` recovered family does not fix it.

## Pass sixty-three — a branch tested the wrong value; every async load returned null

`ASYNCFILE_release` at `0x0014D533`:

    mov eax,[esi+0x10] / test eax,eax / mov eax,[esp+0x14] / je

The lifter defers flag-setting to the consuming branch, and here emitted the
comparison *after* the reload — so it tested the out-pointer (never null) instead
of the pending field (zero). The branch was never taken, and the fall-through
writes null through that out-pointer. **Every completed async file load handed its
caller a null buffer.** One site: 4/10 clean -> 8/8, opens 22 -> 63.

Ruled out first, each measured: the record walker's `sar` is faithful; the walk
terminates in 1–2 steps on the real file; and the in-memory buffer matches the
file byte-for-byte at seven offsets. The async entry was healthy too — right
handle, right buffer, right size, not pending.

**Swept the class**: 4,234 deferred-flag sites, **318 with an operand clobbered
before the branch**, 241 repaired by snapshotting the operands where the flags
were produced (35 skipped for an unrecognised branch shape). Many are the
`fnstsw ax` float-comparison idiom, so this was corrupting float comparisons
across the game as well. Verified against original bytes.

**8/8 clean. Push-buffer traffic 542 -> 132,665 dwords a run, with real
`SET_BEGIN_END` and `DRAW_ARRAYS` from the title's own render path.**

Next: 122,880 of those dwords still do not decode as commands — the write pointer
is crossing a region the producer did not fill, same shape as pass fifty-five.

## Pass sixty-four — clean command stream; one undetected stub was the last leak

The 122,880 "unrecognised" dwords were not garbage: they were `DRAW_ARRAYS`
parameters in a run longer than the 2047 an 11-bit method count can express, so
the emitted header overflowed. The count driving it was `0x00F7FE70` — a stack
address — from `esp` being 0x18 low after `sub_0016D990`, intermittently.

Reporting only the calls that move `esp` found it immediately: every state-flush
call is a clean +4 except `sub_0016C520` at −20. Its chain tail-jumps into
**`sub_0016C605`, an undetected stub** — skipping four pops and a `ret 4`, exactly
24 bytes. Recovering it exposed three more in the same chain, then two more;
six functions lifted and audited by address (no clashes).

**Result: the push buffer parses 100% clean — 837,796 dwords, 0 unrecognised,
624,519 methods. 191 file opens. 577 DRAW_ARRAYS and 577 SET_BEGIN_END pairs a
run.**

Remaining: `SET_VERTEX_DATA_ARRAY_FORMAT` still never written. The state now
looks correct (shader handle `0x00F81D00`, dirty bit set, `[handle+4] = 4`), but a
probe on the emit itself never fires — so the path is gated out when
`sub_0016D990` actually runs. Next step.

## Pass sixty-five — byte sign tests always false; the title renders

`test al, al` sets SF from bit 7 of AL, but `LO8` is `(uint8_t)`, so
`(int32_t)(LO8(x) & LO8(y))` is never negative and every `jns`/`js` on a byte
operand took the wrong path. **34 sites** (30 `LO8`, 4 `HI8`), rewritten to widen
through the signed narrow type.

One of them was the gate in front of the vertex-format emit, which is why
`SET_VERTEX_DATA_ARRAY_FORMAT` had never once been written despite every value
it depends on being correct.

**The translator now draws: 574 `DrawPrimitiveUP` calls and 2,296 vertices a run,
attribute 0 arriving as FLOAT/size 4/stride 64.** Push buffer still parses 100%
clean; 8/8 runs clean; 191 file opens.

Next: vertex payloads read as zero because the bound offset (`0x01132000`) is
inside the push-buffer ring — the title uses the inline "UP" path, so the data
must be captured when its DRAW_ARRAYS is parsed rather than dereferenced later
from a ring the producer has already reused.

## Pass sixty-six — geometry confirmed correct, failure isolated to the device path

Raw command-stream dump proves the decode: attr 0 FLOAT/size 4/stride 64, offset
0x01132000, TRIANGLE_STRIP, DRAW_ARRAYS start 0 count 4 — and the four vertices
are `(0,0) (640,0) (0,480) (640,480)`, a full-screen splash quad. Earlier readings
of a degenerate draw were a logging artefact (only v0 was printed).

Fixed: the vertex **stride mask was 8 bits** where cxbx defines 24
(`0xFFFFFF00`), truncating any vertex wider than 255 bytes; and **no swap signal
was handled** — SSX emits no `FLIP_STALL`, it moves the CRTC scanout base, so
`NV_PCRTC_START` now drives Present.

Recorded: the **GPU MMIO aperture is plain RAM**, so no `nv2a_core.c` register
handler ever runs; the push-buffer consumer works only by reading the channel
context out of memory directly.

**A hardcoded magenta quad drawn through the same device also renders black**, so
the title's data is exonerated and the fault is in the D3D11 submission path —
now reproducible without the title in the loop. Validation layer reports no
API misuse. 8/8 clean, 191 file opens.

## Pass sixty-seven — first pixels on screen

Bisected the D3D11 path: the clear reaches the back buffer, the RTV is the back
buffer, the GPU holds the right vertices, the VS constants are right — and the
**PS constant buffer had `COLORARG1 = D3DTA_TEXTURE` where `D3DTA_DIFFUSE` was
set**.

Cause: `tss[D3DTSS_COLORARG1] ? tss[...] : D3DTA_TEXTURE` — and **`D3DTA_DIFFUSE`
is 0**, so the "default if unset" idiom silently replaced it. The stage sampled an
unbound texture (transparent black) and every draw was discarded in the pixel
shader. Root cause one layer back: the texture-stage-state table was never
seeded with D3D8's documented defaults, which is what forced the guessing.

Fixed both ends — seed the defaults at device creation, read the table directly.

**The back buffer went from `000000` to `FFFFFF`: the title's full-screen splash
quad now rasterises.** White rather than the splash art because no texture is
bound yet — that is the next piece. 9/10 clean, 191 opens, ~1,475 draws a run.

## Pass sixty-eight — wrong method numbers, and the wrong present clock

`nv2a_pgraph_d3d11.c` carried its own copies of 23 NV2A method constants; **six
were wrong**, including all four clear methods (`CLEAR_SURFACE` was 0x01D0, not
0x1D94) plus `DEPTH_TEST_ENABLE`, `CULL_FACE_ENABLE` and `TEXTURE_CONTROL0`.
They shadowed the correct definitions and the compiler had been warning on every
one. That is why `clears=0` while `SET_COLOR_CLEAR_VALUE` sat in the histogram.
All 23 deleted in favour of `nv2a_regs.h`.

Then the screen went black again, and sampling the buffer at three points showed
the draw landing every frame (`after-title-draw: FFFFFF`) while **Present ran on
a 16 ms timer that kept firing between the frame's clear and its draw**. The
title offers no flip signal, so the clear that begins the next frame is now the
frame boundary: it presents the finished frame before wiping. Message pumping
split into `d3d8_PumpMessages()`; the timer is a 500 ms safety net only.

**Every presented frame is now white — the splash quad is on screen continuously.
10/10 clean, 191 opens, 846,553 push-buffer dwords, ~1,475 draws/clears a run.**

Also added texture upload (format decode, xemu swizzle masks, all uncompressed
formats to A8R8G8B8, DXT passthrough, per-stage cache) — inert for now, since the
title emits no texture methods during the splash.

## Pass sixty-nine — three executed stubs recovered; the title is stuck

A 60 s run matches a 12 s one exactly and **never emits a texture method**: the
title is stuck drawing the splash quad, not loading slowly.

Correcting pass 68: it uploads vertex microcode during init but never writes
`SET_TRANSFORM_EXECUTION_MODE`, which defaults to FIXED — so shader translation
is *not* the current blocker.

Recovered the three `/* not detected */` stubs that were actually executing.
`sub_0005FDB0` ends in **`ret 4`** (stdcall, one arg) while the empty stub popped
nothing, leaking 4 bytes of stack on dozens of call sites; it formats a filename
via `CRT_FormatString` and calls `FILE_LoadRawFileSync` — **an asset loader doing
nothing**. Three of its five callees already existed under RE names; the
address-based check caught them again.

Result: the `D:\(null)` open is gone, duplicate font opens gone, icall misses
19→18, no stub executes. **10/10 clean, 186 opens, 875,744 dwords.**

Registering the 14 unregistered indirect-call targets gives 12 misses and 10/10
clean but **blacks the screen** — frame structure changes and the boundary present
catches a cleared buffer. Reverted; rendering returned immediately. They stay
held back for one-at-a-time bisection.

## Pass seventy — the stall localised to one failing kernel wait

Kernel profile: **319,107 `NtWaitForMultipleObjectsEx` + 319,044
`RtlNtStatusToDosError` in twelve seconds** (14.7M in twenty-five) — a hot spin
on a wait that keeps failing.

Early waits pass tagged handles and succeed; the spinning ones pass raw
`0x0000AEA2`, which is not a Win32 handle, so the wait returns
`STATUS_UNSUCCESSFUL` instantly and the caller retries forever. Ruled out by
measurement: not an event (only 3 exist), not a thread handle (0x450/0x638/0x9A0),
not table exhaustion, and stable across runs.

**The tell is the array address**: the working wait on that worker stack reads
its handle from `0x04CCDE3C`, the failing one from `0x04CCDE44` — eight bytes
higher in the same frame. A stack-layout defect in the recompiled caller, same
class as the `ret 4`/stub mismatch.

The spin is *new*: before the stub recovery the profile was 3.02M
`KeDelayExecutionThread` (a sleeping poll). The title was stuck either way; the
recovery moved it into a loop that names a specific defect. Probe gated behind
`XBOX_WAIT_TRACE=1`. 8/8 clean, 186 opens, rendering unchanged.

## Pass seventy-one — the stall is fixed

Traced the 14.7M-iteration wait spin end to end: host backtrace → `sub_0014D850`
holds its request object in `ebx` and never reassigns it → per-call-site probing
named `sub_0014D7B0` as the clobberer → that chain is byte-faithful to the XBE
but returned with **`esp` 8 bytes high** → the divergence is an **indirect call in
`sub_001646C0`** where `RECOMP_ICALL_SAFE` (the *stdcall* variant, which rewinds
`g_esp` past the pushed arguments on a miss) is used at a **cdecl** site that
pops them itself — **a double-pop of 8 bytes**.

The missed target, `0x0014D650`, was **already translated in
`recomp_recovered.c` and simply never registered in the dispatch table**.
Registering that one function fixes everything downstream.

**`NtWaitForMultipleObjectsEx`: 319,107 → 156 a run. Kernel calls per window:
1,921,648 → 5,716. Opens 186 → 190**, with `ssxfe.big` and `particle.xsh` loading
again. 10/10 clean, rendering unchanged.

Note: `RECOMP_ICALL_SAFE` remains wrong at cdecl sites in general — a miss
anywhere else corrupts the stack the same way. 19 misses remain; several may
also be translated-but-unregistered.

## Pass seventy-two — dropped indirect calls audited, 19 → 7

Of the nineteen misses, **seventeen were already translated and merely
unregistered**; two were genuinely absent. Registering all seventeen gave 191
opens but a black screen, so they went in by bisection: **fifteen are safe**.

Two are held back with a documented reason rather than a workaround:
`sub_000151F0` collapses loading (190 opens → 15; it drives the critical-section
import slots and a stored callback), and `sub_00179411` blanks the screen (a
DSOUND cleanup path that releases something the renderer still holds — the
vertex-stream offset goes to 0). Their calling conventions were verified correct
against objdump, so these are real dependencies, not translation defects.

**19 → 7 unresolved calls, 190 opens, 10/10 clean, rendering unchanged.** The
remaining five all need lifting; the cheap unregistered case is exhausted.

## Pass seventy-three — dropped calls 7 → 4

Lifted the five that genuinely needed it. `0x0013A7F0` needed a four-fragment
closure chain; `0x0015C7E0` really is a one-byte `ret` (verified against the XBE,
not a bad boundary); `0x0017FCEC` remains unliftable because it sits in the XPP
section the disassembler does not cover.

Four registered cleanly. The fifth, `sub_0012AB10`, regresses loading (190 opens
→ 178) and surfaces six more misses in the same 0x0012Axx/0x0012Bxx cluster —
that cluster is a **partially translated subsystem** that wants completing as a
unit, not one function at a time.

**19 → 4 unresolved calls across parts 72–73. 190 opens, 10/10 clean, rendering
unchanged.** All four remaining are held back with a documented reason.

## Pass seventy-four — cluster completed, and pass 73 corrected

**Correction:** pass 73 called the 0x0012Axxx cluster "full of holes". It is not —
all **79 detected functions in the range are translated, none are stubs, 77 were
already registered.** What was missing were functions the disassembler *never
detected*, which no range scan of detected functions can reveal. Enabling
`sub_0012AB10` exposed four; closure pulled in three more.

`sub_0012AB10` alone regresses (190 opens → 178); with its seven dependencies it
does not (190 → 344). It is a faithful six-instruction predicate — enabling it
just makes the title take the correct branch, which needs the rest.

**Being precise about the 344:** the file *set* is unchanged. All 154 extra opens
are `U:\` polls (300 → 608, ~24/s = once per frame). This is the **save-device
enumeration** path now running instead of being skipped — correct behaviour
restored, not new content reached.

**19 → 3 unresolved calls across passes 72–74; 84 recovered functions; 10/10
clean; rendering unchanged.**

## Pass seventy-five — why only a white quad

**The title emits exactly one draw per frame** (1,502 draws / ~1,500 frames;
BEGIN_END x3004 = 2x1502; vertex format set **once**). The push buffer genuinely
contains one full-screen untextured quad per frame — this is not a translator gap.

**The texture state is emitted but unobservable.** `sub_001695C0` does run and
does write `0x1B0C/0x1B4C/0x1B8C` into the ring at base+0xA1C — *once, during D3D
init*, ~1,100 log lines before the consumer can connect. Connecting earlier is
impossible: it needs a D3D11 device, which needs the title's own D3D init to
finish. By then the frame loop has recycled the buffer.

Three capture attempts all failed and were reverted: replay-at-bring-up (region
already recycled), snapshot-at-first-context (taken before the emit), and
watermarked snapshots (**never fire — during init the title writes into the ring
without publishing the context write pointer, and the context's base/limit fields
are not populated either**).

The workable route is to capture from the *emitting* side — hook the push-buffer
allocator `sub_0016B920` or `sub_001695C0` — rather than reading back a buffer
recycled before anyone listens. State unchanged: 344 opens, 3 dropped calls,
10/10 clean.

## Pass seventy-six — init-time GPU state captured; pass 75 corrected

Built the capture pass 75 said was needed. Three parts: the ring extent comes
from `MmAllocateContiguousMemoryEx` (the context publishes nothing usable during
init), sampling runs on the **guest thread** from `kernel_thunk_dispatch` (the
1 ms pump cannot see init at all), and the delta is replayed at bring-up. Verified:
821 dwords captured, `0x1B0C` header at index 647 exactly where the emitter wrote it.

**The result contradicts pass 75.** The captured state reads
`TEX CONTROL0 = 0x00000000` on all four stages — bit 30 clear, **textures
explicitly disabled** — and `SET_TEXTURE_OFFSET`/`FORMAT` are never sent. The
emitter confirms it: the texture-object argument is zero, i.e. `SetTexture(stage,
NULL)` ×4. The title binds no texture **because it does not want one yet**; the
white quad is what it is deliberately drawing.

So the blocker is not the renderer at all — the title is still not advancing to a
state that draws content. The capture is kept regardless: init-time GPU state was
previously invisible and is now visible, at no measurable cost.

10/10 clean, 344 opens, 3 dropped calls, rendering unchanged.

## Pass seventy-seven — it is the hard-disk check; text is one field away

Following the user's recollection of SSX's boot sequence paid off immediately.
The StartScreen state machine runs correctly — `Enter`, `SetState`, `Render`,
`TickExitWhenDone`, `RenderStatusText` all fire, **status state = 1** — and the
localisation lookup for string 0xBA7 returns, in UTF-16, **"Checking hard disk"**.
The title is on exactly the right screen with exactly the right message.

The text dies at the glyph renderer: `sub_000BDFD0` reads its character count
from `text_object+0x1C` and finds **0**, so it iterates nothing and emits no
geometry. That also explains the absent texture binding — with no glyphs there is
nothing to texture, so pass 76's "it does not want a texture" was true but
incomplete; it is the same defect from the other end.

The count is 0 before and after every call in `sub_000AED60`, so the
string-to-glyph conversion is missing further up, where the text object at
screen+0x4C is prepared. **Next question, now single and well-defined: what
should write the count at text_object+0x1C?**

344 opens, 3 dropped calls, rendering unchanged, build probe-free.

## Pass seventy-eight — text traced end to end; it dies in x87 layout

Followed "what writes text_object+0x1C" to the bottom. Localisation → format →
copy into obj+0xA6C → working copy at +0x26C → glyph loop walking every character
all **work with real data** ("Checking hard disk" verified at each stage). What
never happens is the commit: the glyph index `edi` stays 0, so the count stays 0
and `sub_000BDFD0` emits nothing.

Glyphs are emitted **per word** — normal characters jump to the loop tail, and
only a space/backslash/null reaches `loc_000BE2F6`, which calls
`Text_MeasureStringWide` and then does the word-wrap decision in **x87 floating
point**. That is this build's largest known defect surface (2,488 unfixed
memory-operand sites, shared stack still disabled), and it is exactly the kind of
code it breaks.

Corrected: `sub_000BE480` "bailing" on an empty buffer is the *normal* set-string
path (it falls through to the copy), not the defect. And pass 76's "does not want
a texture" is fully explained — it wants one for the glyphs.

**The x87 memory-operand work, previously deferred as a broad cleanup, is now the
thing directly between here and text on screen.** 8/8 clean, 344 opens, 3 dropped
calls, probe-free.

## Pass seventy-nine — 2,074-site x87 bug found; blocked on the stack model

Found a real, systematic defect: **`fnstsw ax` is emitted as a comment only**
(2,074 sites, so every `test ah,0x41` reads a stale AH), and **`fcomp <mem>`
compares `fp_top()` against `fp_st1()`** rather than the memory operand its own
comment names (2,050 sites). Verified against objdump at 0x000BE345.

Fixed generally — and it **regressed hard**: opens 344 → 25, black screen.
Bisection shows the `fnstsw` half alone does it, which is the explanation rather
than a mystery: stale AH gave every FP branch a consistent survivable path;
reading the comparison faithfully only helps if the comparison is right, and with
the 8,309 popping-form memory-operand sites still unfixed, `fp_top()` is often not
ST(0). **The fix is correct but gated on the x87 stack model and must land with
it, not before.** Reverted.

**Correction to pass 78:** `text_object+0x1C` is *not* a glyph count — in
`sub_000BDFD0` it is `edi = esi + ebx*4 + 0x6C`, a starting index. Zero is
correct. The word *is* committed (`commit word[0] = 0x01A624A8`), so the pipeline
reaches the draw loop with valid data; the failure is further inside that loop.

8/8 clean, 344 opens, probe-free, rendering unchanged.

## Pass eighty — tooling for this defect class

Added `xboxrecomp/tools/audit/`:

**`xverify.py`** finds instructions the recompiler annotated but never
implemented, grouped by opcode and classified consequential / by-design /
benign. Current tree: **fnstsw 2082, fstp 152, fld 119**, pand 17, fldcw 15,
fnstcw 14, sahf 9. The `fstp`/`fld` counts are *new findings* — dropped FP loads
and stores, which would explain stack drift independently of the compare bug.
`--function` diffs one function against the XBE and reports the `fnstsw` at
0x000BE349 that previously took a dozen manual cycles to find.

**`xprobe.py`** adds/removes instrumentation in generated code: escaping handled
once (the thing that repeatedly broke hand-written probes this session), fenced
markers so `clear` leaves zero residue, rate limiting by default, guest UTF-16/
8-bit string decoding, and host backtraces rebased for `sym.py`.

One correction during development: `--function`'s first version reported 86
ordinary `mov`/`push` as "missing" because they carry no annotation. Restricted
to opcodes that are always annotated — a tool with false positives is worse than
none. 8/8 clean, 344 opens, no probes left active.

## Pass eighty-one — 271 dropped x87 stack ops fixed

`xverify.py`'s first report found two unexamined classes: **`fstp st(0)` x136**
(pop and discard — the pop never emitted), **`fld st(N)` x119**, plus 16
`fstp st(N>0)`. All pure stack bookkeeping, all dropped whole. Fixed
mechanically; both classes now absent from the report. **271 sites, 10/10 clean,
344 opens, rendering unchanged.**

Retried the 2,074-site `fnstsw` fix on the corrected stack, with and without the
`fcomp` pop — **both still regress to 25 opens and a black screen**. So stack
depth was not the only defect feeding the comparison. The likely remainder is the
memory-operand form: **9,498 `fp_popp()` calls** across the sources is far more
popping than this program's FP shape justifies. Reverted; the stack fix stays.

## Pass eighty-two — the FP compare idiom is four instructions; three are dropped

**Correcting pass 81:** the "9,498 pops is too many" reasoning was wrong. 7,051
are legitimate `fstp m32`, 255 `fstp m64`, 2,036 are macro *definitions*. The
popping is fine.

The real defect: an x87 comparison is `fcomp` / `fnstsw ax` / `test ah,MASK` /
`jp`, and the recompiler drops three of the four. The `test` is absent entirely
and **780 parity branches are emitted as `if (1)`** — in both directions. That is
why fixing `fnstsw` alone regressed: the branch stayed unconditional while the
status word started steering everything else.

Built **`fixfpbranch.py`**, which disassembles each function from the XBE, pairs
every `jp`/`jnp` with its `test ah,IMM`, and rewrites through a new
`FPU_PARITY(mask)` macro — **1,125 of 1,159 sites resolved**, 34 skipped rather
than guessed.

The complete idiom still regresses (344 → 25 opens, now failing just after
`title.ffn`, with FP-inexact exceptions). Reverted. The fix is now one command
away whenever the remaining obstacle is understood, and the font path is where to
look. 10/10 clean, 344 opens, rendering unchanged.

## Pass eighty-three — the x87 comparison idiom lands (5,249 sites)

**`fnstsw` is gone from the defect report.** Landed: 2,050 corrected compare
operands, 2,017 restored pops, 2,074 `fnstsw` implementations, 1,125 parity
branches, on top of part 81's 271 stack ops. **10/10 clean, 344 opens, rendering
unchanged.**

Two things had to be fixed before the program could be: **the measurement** (a
single 25 s run reported a phantom "8 opens" that sent the bisect down a wrong
branch — everything is now run three times), and **the harness** (the function
list had Windows line endings, so every name reached the picker with a trailing
`\r` and nothing was ever applied, while it reported success — when a bisect says
a change has no effect, verify it was applied).

Excluded exactly two functions, `SceneView_RenderPass` and
`SceneRenderer_RenderAllPasses`. Each alone drops opens 344 → 25. Their site is
`test ah,0x44` / `jp` — the `!=` idiom — which as `if (1)` always took the
not-equal path; the fix makes the *equal* path reachable for the first time and
something there fails. The fix is correct; the exclusions hold back a correct
change that uncovers an older defect on a never-executed path.

The glyph builder is unchanged — still one word, 1,202 draws, no text. The FP
work was necessary but is not the last link.

## Pass eighty-four — save paths, executable name, and proof of what is on screen

**`UserData\` was a bug.** `T:`/`U:` mapped to `TitleData`/`UserData` while the
title's own `\Device\Harddisk0\Partition1\TDATA\<id>` paths went elsewhere — the
same storage under two names, with the empty directory created by the U:\ device
poll (~24/s) added in pass 74. Now `T:`/`U:` resolve to `\TDATA\<id>` /
`\UDATA\<id>`, ID read from the XBE certificate. The hard disk contains exactly
`TDATA/45410004` and `UDATA/45410004`.

**The executable is named after the game.** `cmake/XbeTitleName.cmake` reads the
certificate at configure time and sets `OUTPUT_NAME` — the build now produces
**`SSX Tricky.exe`**, falling back to the project name when no XBE is present.
`main.c` prints the name and ID at startup.

**Proof of what is on screen.** The "no text" claim rested on a draw count and a
*two-pixel* sample. `XBOX_D3D_DUMP=<prefix>` now writes whole frames as .bmp; a
captured 640×480 frame has **one distinct colour across all 76,800 sampled
pixels**. Measured, not inferred. 10/10 clean, 344 opens.

## Parts 85-86 — the white screen, and 4,025 x87 operand holes

**Reference established.** A xemu screenshot of the same ISO at boot: black
screen, white "Checking hard disk". The black is correct; the white this port
was showing was the defect.

**The white screen.** Every frame capture so far had been gated to the first six
presents, so the instrumentation could only ever report "white". Spread across
the run it reproduced immediately: white through present 60, black from 120.
Cause: the title binds **only** vertex attribute 0 (position) as an array -- the
other 48 bytes of its 64-byte vertex are zero -- and feeds diffuse and texcoords
from the NV2A's per-attribute *constant registers*, which this translator did
not implement. `va_read_color()` ended in a hardcoded `return 0xFFFFFFFFu`, so
the title's full-screen backdrop quad was painted white over a correct black
clear. Implemented all five constant-attribute write paths
(`SET_VERTEX_DATA4UB/2F/4F/2S/4S`) with semantics from cxbx-reloaded. Backdrop
is now `000000` across all 307,200 pixels.

Same defect class as the `D3DTA_DIFFUSE` bug: **a guessed default standing in
for state that was never tracked.**

**`fixfpmem.py` — 3,626 sites.** Every `fadd`/`fsub`/`fmul`/`fdiv` was emitted as
the popping two-register form whatever the instruction was, losing the operand
and, for the 2,488 non-popping mnemonics, shortening the x87 stack. Recovered by
disassembling each function from the XBE and pairing instruction sequences;
rewrites only when length and mnemonic agree at every position. All 3,626
paired.

**`fixfpudrop.py` — 399 sites** emitted as bare `/* FPU: ... */` comments:
`fsubr` x226, `fdivr` x60, `fiadd` x21, and the transcendentals. 3 `fxam` sites
reported and left alone rather than guessed.

**`sub_000F9560` recovered** — a qsort comparator reached only through the
orthographic-projection path, whose absence caused a 108-million-call indirect
spin.

**Verified: 5/5 clean, exit 124, zero crashes, 344 opens, ~1,200 draws, 3
dropped indirect calls, probe-free.**

**Still open:** the text. `SceneView_RenderPass`'s FP branch selects the
orthographic projection that screen-space text needs; landing it now requires
finishing the batch-sort chain behind it (the comparator is recovered, but the
element pointers reaching it are still bad).

## Part 87 — two more instruction classes closed

**32 parity branches** landed by fixing the *tool*, not the code. The pairing
between `test ah,IMM` and its `jp` looked back a fixed three instructions; the
compiler schedules SSE work into that gap, so 34 sites never paired and were
recorded as unsafe to touch. Replaced with a walk over an allow-list of
mnemonics that provably leave EFLAGS alone. 15 of the 32 are in
`Rider_ResolveTerrainContactPhysics` — core physics, not frontend.
Tree-wide hardcoded parity branches: 34 → 4.

**16 repeated string instructions** (`repe cmpsb`/`cmpsd`, `repne scasb`) were
comment-only with their branches pinned — 7 to "matched", 3 to "differed".
Worse, they never advanced `esi`/`edi` or counted `ecx` down, and the four
`repne scasb` sites are the CRT `strlen` idiom (`not ecx; dec ecx`), so every
length computed that way came from -1. All 16 implemented via `fixrepstr.py`,
all 10 branches repointed at the real result.

**PFIFO pump hardened**: it dereferenced guest-published pointers unchecked, and
the mapping has a real hole at `0xEC400000-0xFD000000`, so a bad pointer killed
the host process in a thread unrelated to the guest code that caused it.

**Verified: 5/5 clean, 344 opens, ~1,202 draws, 3 dropped indirect calls,
screen black across all 307,200 pixels (matches xemu).** Comment-only
instructions remaining: 3 (`fxam`, deliberate). Collapsed x87 arithmetic: 0.

**Still open:** the orthographic render path. The sort behind it is verified
correct and the crashing function is fully translated — what it needs is state
that nothing on a currently-reachable path initialises. That is a body of
never-executed code, and the text lives on the far side of it.

## Part 88 — the instruction-coverage backlog, closed

**155 SIMD sites** (`fixsimd.py`). 54 of them were **stores** — `movntps` x39 and
`movntq` x15 — writing nothing at all, the same failure mode that broke the CRT
`memcpy`. `sub_00178164` is a 4x4 transpose whose eight instructions were *all*
comments, so the routine was a no-op returning whatever was already in the
destination. MMX needed a lane model, added to `recomp_types.h` as typed inline
functions rather than macros because the semantics (saturation, signed
multiply-accumulate, lane-flushing shifts) are too easy to get subtly wrong in a
macro.

**46 scalar sites** (`fixscalar.py`), split three ways because the
classification matters as much as the fix: 20 real (`bsf` x2 — the same shape
that once corrupted every mid-sized heap allocation; `rcr` x8 with the carry its
`shr` discarded; `xlatb`; `pushal`/`popal`; `cmpxchg` x2 where both the store
*and* its branch were wrong), 23 correct as no-ops and now marked so, and 13
that are not code at all — one region where the disassembler walked into a float
table.

**Backlog: 3,626 collapsed x87 → 0; 402 x87 comments → 3; 16 rep-string → 0;
155 SIMD → 0; 46 scalar → 13; 34 parity branches → 4; 10 string branches → 0.**
Total instructions still unimplemented: **16**, of which 3 are deliberate and 13
are not code.

**Verified: 5/5 clean, 344 opens, ~1,202 draws, screen black across all 307,200
pixels.**

The instruction-level backlog is effectively closed. What stands between here
and the boot message is not a missing opcode — it is the orthographic render
path's initialisation.

## Part 89 — why nothing is drawn: the batch cursor was never initialised

**The answer to the question the port has been stuck on.** `SceneView_RenderPass`
ends in the loop that draws a scene view's 24-byte batches, bounded by a cursor
at `view+0x804` and an end at `view+0x808`. Measured:

    MEM32(view + 0x804) = 0xDEADC0DE     <- allocator poison
    MEM32(view + 0x808) = 0x017AB5D8
    view + 0x8C0        = 0x017AB590     <- where the cursor should point

Poison ≥ end, so the loop exits on its first test every frame and writes the
poison back. **It has never executed a single iteration.** That is why exactly
one draw — the backdrop — reaches the GPU per frame, and it has nothing to do
with the projection.

**Cause:** the cursor is written in exactly one place, `SceneRenderer_RenderAllPasses`,
guarded by a `jnp` that was emitted as `if (1)`. That made the view-count scan
exit immediately with count 0, so the reset loop never ran. The store itself
translated fine; only the branch was wrong. Found by scanning the *XBE* for every
access to `[reg+0x804]` — grepping the generated source misses computed addresses.

**Enabling that one branch** gives `cursor = 0x017AB590` (exactly the array
base), and the first batch then has a valid object pointer (`0x01950DB0`) and a
sensible pass index. The loop runs for the first time.

Not yet stable: `RenderContext_CycleFrameBuffers` double-buffers the view arrays,
and the per-frame walk in `sub_001046D0`/`sub_001046E0` (24 passes × 6 views)
does not reset cursors and runs away past its six slots into texture memory.
Ruled out along the way: those two functions are fully translated, the render
pass's two exits are correctly balanced, and the qsort behind it is correct.

**Baseline restored and verified: zero crashes, 344 opens, ~1,202 draws,
probe-free.** `togglepass.py` now switches the two held-back branches
independently — which is what isolated the cause.

## Part 90 — the render loop runs: 1,202 draws → 27,131

Both branches held back since part 79 are enabled, stable, and the title is
submitting real geometry. What had blocked them for eleven parts was never the
branches — it was **one undetected function**.

Enabling the cursor reset made the batch loop run, then crash. Measured across
the first virtual draw, `esp` dropped exactly **32 bytes**. Bisected to
`sub_0016A460` → `sub_00169EA0`, which pushes five registers and tail-jumps into
a chain. A new tool, `stackbalance.py`, walks these chains and found it balances
+5/−5 **but with a missing link**: `sub_00169ED1` was an undetected stub that
returned immediately, skipping the epilogue. 5 pushes (20) + 2 args (8) + return
address (4) = exactly the 32 bytes. Recovered it — four bytes,
`mov esi,[esp+0x10]`, falling through into the next function.

**draws 1,202 → 27,131, vertices 4,808 → 108,524, zero crashes, 344 opens.**

**The title is drawing glyphs.** Quads of ~11×17 pixels at y 97–114 marching
left to right — text. A second bug was hiding them: the translator read diffuse
from slot 3 and texcoord0 from slot 9 (the fixed-function assignment), but this
geometry uses a vertex *declaration* binding them at slots 1 and 2. Every glyph
got the constant colour (opaque black) and texcoords of 0,0 — black quads with
one texel, on black. Added type-based slot resolution; they now decode as
`c=FFFFFFFF` with real atlas UVs.

**Correction:** while chasing this I disassembled `0x00169xxx` with the `.text`
shortcut (`va − 0x10000`), but that region is in the **D3D section** — every
dump was off by `0x80`, which produced a confident and wrong conclusion about
phantom boundaries. All VA→file conversion now goes through the section table.

**Remaining blocker:** `z` is `+inf` in the guest's own vertex data (and `−nan`
on the perspective path), so the rasteriser rejects every glyph. One more
computation producing a non-finite result — this one surviving into memory
rather than into a branch.

## Part 91 — the infinite depth, traced to its source

The glyph quads are right in every respect but one: `z = +inf`, so the
rasteriser discards them. Followed that value back with the diagnostic server's
page write-watch (a real `CaptureStackBackTrace`, unlike `xprobe --backtrace`,
whose stack scan misattributed a caller earlier).

`find` located the value at `0x00B3E088` and every `0x20` after — the z field of
every vertex from index 4 on, while the backdrop's vertices 0–3 hold a correct
`0.01`. `watch` named the writer: `Text_RenderGlyphStringScreenSpace`.

The computation is `z = nearScreenZ * view[0x868]`, and measured:

    nearScreenZ    = 0x3C23D70A   (0.01, correct)
    view[0x868]    = 0xDEADC0DE

**The same poison that was in the batch cursor** — and it is the *title's own*
debug fill (13 occurrences in the XBE), not something this port invents.

Checked all eight XBE sites touching `[reg+0x868]`: three belong to other
objects that share the offset, and the real setter (`sub_0004727C`) is guarded
to write only when the field is already `0.0` — a guard that is correctly
translated and rightly declines to touch poison. Its caller never runs.

**Blocker, stated precisely:** the scene view is only partially constructed.
`sub_000FF8C0` clears `+0x004`–`+0x800` and sets `+0x808`/`+0x80C`, stopping
well short of `+0x868`. Either a constructor that would zero the rest never
runs, or the allocation is expected to arrive zeroed and does not.

**5/5 clean, zero crashes, 344 opens, 27,128 draws, 108,512 vertices,
probe-free, both branches ON.**

## Part 92 — "Checking hard disk" renders

**The title's first screen is on screen and readable**, matching the xemu
reference. Two defects stood in the way.

**1. Float returns were dropped across every call (620 sites).** A startup
write-watch named the writer of the poisoned field: it is not a scene-view
member but the projection matrix's `m22`, `1/(zfar − znear)`. Both clip planes
were zero because they come from camera getters that return a float **in
ST(0)** — and every translated function has its own `_fp_stack`, torn down on
return, so the caller read its own stale local stack. `recomp_types.h` already
mirrors pushes into `g_x87_st0`, but only *orphan* sites had been changed to
read it; every function that uses the FPU elsewhere still read the wrong value.
`fixfpret.py` fixes all 620, only where the call→consumer path is unambiguous.
`z` went from `+inf` to `0.000` and **pixels appeared for the first time**.

**2. Every texture was unswizzled twice.** The glyphs were placed correctly but
shredded. Ruled out in order: the uploaded atlas is a clean font sheet (so the
NV2A swizzle decode and A4R4G4B4 conversion are right); the UVs are clean
integer rects, and cutting those 18 cells out of the atlas spells "Checking hard
disk"; layout, stride and shaders all check out. The fault: `tex_upload` decodes
the swizzle and writes linear pixels, then declares the texture `A8R8G8B8` —
a *swizzled* Xbox format code — so the shim's `UnlockRect` unswizzled it again
on upload. Invisible to inspection because `LockRect` returns the CPU copy taken
before that pass. One-value fix: declare it `LIN_A8R8G8B8`. **This was
corrupting every texture in the title, not just the font.**

**Correction:** an earlier "no texture is bound" reading came from `GetTexture`,
which is `E_NOTIMPL` in the shim and never writes its out-parameter.

**5/5 clean, zero crashes, 344 opens, ~27,100 draws, ~108,400 vertices,
probe-free.**

## Part 93 — past the disk check

The message holds ~180 frames, then the screen goes black — but the title does
not stop. It settles into a steady **23 draws a frame**, and they are legible:
a full-screen quad with a **512×512 splash texture** bound and UVs (0,0)–(1,1);
a untextured full-screen quad whose alpha ramps `5C → 62 → 66` (a fade); and a
shadowed line of text bottom-left. The title has reached a splash/attract screen
and is compositing it correctly.

**Why none of it appears:** the splash texture's VRAM is empty. A startup
write-watch on that page records **zero writes** for a whole run, and the entire
`0x00900000`–`0x00C00000` region the textures point into is untouched. Not a
loading failure — the `files` command shows every archive read in full
(`splash.xsh` 1,048,704 bytes in 129 reads; `fe_1.xsh` 1,049,104; `hud.xsh`
164,256). The font is the control: it comes from `.ffn`, its data *is* present,
and it renders.

**Next blocker, precisely: `.xsh` texture archives are read from disc but never
installed into video memory.**

**Also fixed:** `tex_upload` cached one texture per stage, and this title
alternates two every frame — so it missed on *every draw*: release, create,
swizzle-decode, upload, ~27,000 times a run, and it released textures already
bound to a shader-resource view. Replaced with a 16-entry LRU keyed on
(offset, format). **27,000 texture uploads a run became 4.**

**5/5 clean, zero crashes, 344 opens, ~27,000 draws, probe-free; the boot
message still renders.**

## Part 94 — the splash screen, and the white flash

Classifying every full-screen quad in a run: a textured opaque-white blit of the
splash art (×30), under a fade whose alpha ramps one step per frame (`B2 → D4`)
to opaque black (×76). The composition is correct.

**The white flash** is that blit rendering *untextured*: when `tex_upload`
returns NULL, `apply_draw_state` calls `SetTexture(0, NULL)`, and the shim sets
`COLOROP = DISABLE`, so the quad emits its diffuse colour — `FFFFFFFF`,
full-screen. Once the (empty) texture binds, the sampled texel is `(0,0,0,0)`,
alpha zero, and it turns invisible — the black that follows.

**Why the texture is missing.** The install path works, for four textures.
`GfxContext_ParseAndQueueTexture` → jump table (all 11 targets registered) →
`GfxContext_QueueTextureFromRawData` → per-format handlers → `sub_00178164`,
the XG swizzle whose 39 non-temporal stores were dead until part 88. Its
arguments give the destinations:

    dst 0x01A66600 128×128   ← the font atlas, renders
    dst 0x01A6E680 256×256   ← renders
    dst 0x01AAE700 / 0x01AEE780 / 0x01B2E800  256×256

The pixel data is real (`0x04A2CDD0` holds `FFC28E47 FFC18E47 …`, A8R8G8B8).
But the splash quad's texture offset is `0x009E1C80` and the overlay's is
`0x00AE1D00` — **neither is ever a swizzle destination**, and the whole
`0x00900000`–`0x00C00000` region stays zero for an entire run.

**Next thread:** only four or five textures are installed, while `splash.xsh`
and `fe_1.xsh` are each read in full (1 MB, 129 reads) and hold many more. The
archive iteration stops after the first entries, or never starts for those
files.

**5/5 clean, zero crashes, 344 opens, ~27,100 draws, probe-free.**

## Part 95 — the GPU could not reach the textures

**Root cause of the blank splash, in the kernel bridge.** Comparing every
destination the XG swizzle writes against every offset the NV2A is given:

    swizzle wrote to:  0x049E1C80    GPU was told:  0x009E1C80
    difference:        0x04000000  — exactly 64 MB

The title computes the GPU address as `va & 0x03FFFFFF` (correct on a 64 MB
console) and guarantees that works by *asking* for memory in that range —
`low=0x00000000 high=0x03FFB000`. **We returned `0x04B3E000`.**
`bridge_MmAllocateContiguousMemoryEx` read the range only to print it, then
called the ordinary bump allocator.

**A wrong first attempt, worth recording.** Serving unconstrained requests from
the top of the heap satisfied the constraint but **broke the boot message** —
the 53 MB arena is unconstrained and the font atlas lives *inside* it, so moving
the arena above 64 MB gave the font exactly the splash's bug. The correct shape
is to reserve, not relocate: a 16 MB pool at the bottom of the heap for
range-constrained requests only, everything else above it as before.

**Result:** the splash's address now resolves and carries pixels (a 40×30 patch
reaches the screen); the 256×256 texture is now a real image (260 colours, 51%
non-black). It is still only 0.3% filled — the archive iteration from part 94
remains open.

**Also fixed: 7,848 exceptions a run.** `fnstcw`/`fldcw` were bare comments (29
sites), so the CRT's save/modify/restore of the x87 control word read
uninitialised memory, concluded a float exception was pending, and called
`RtlRaiseException` with `STATUS_FLOAT_INEXACT_RESULT`. Modelled as storage
(`g_x87_cw`, init 0x027F). **7,848 → 0.**

**The save file:** the title still does not autoload it, and the save is not at
fault — `NtQueryDirectoryFile` is *never called*, so the directory is never
enumerated. Separate thread.

**4/4 clean, zero crashes, 344 opens, ~26,500 draws, probe-free.**

## Part 96 — why the save is never found

A real save was installed at `UDATA\45410004\201120EF6C64\Data.ssx`. It is not
autoloaded, and neither the save nor the file system is at fault.

**The title never asks.** `NtQueryDirectoryFile` is called **zero times** in a
run. It opens `UDATA`, `UDATA\45410004`, `TitleMeta.xbx`, `TitleImage.xbx` — all
succeed — so it registers its save area and stops. No enumeration, so nothing
to find the save with.

**Why: the boot state machine stalls.** `StartScreen_SetState` (0x000AEF50) is
called **exactly once, with state 2**, and never again — where states 6/7/8 are
the "Autoloading from hard disk" phase. That is why the port jumps from the
disk-check message straight to the splash, and why the message sits ~180 frames
instead of the 15 the code specifies.

The driver `sub_000AF7B0` does a readiness check through a vtable slot, and on
false with the countdown expired falls to state 2. Measured: the check returns
**0** every frame while the countdown runs down to zero. It resolves to
`0x0012AA60`, a hand-written stub that is *faithful* — it carries the original
`mov eax,[ecx+0x78] ; test eax,eax ; setne al ; ret` in its comments. It returns
false because the field at `+0x78` is **never written** — the same shape as the
batch cursor and the depth scale: a field whose writer sits on an unreached
path.

**The splash art** is identified from the user's xemu capture as the "BASIC
CONTROLS" loading screen; the reddish patch this port shows is its tan pixels,
the texture still only 0.3% filled (part 94's archive-iteration thread).

**5/5 clean, zero crashes, 344 opens, ~27,000 draws, probe-free.**

## Part 97 — the save-device manager is never started

Followed the readiness flag to its writer. The object the boot screen checks has
vtable `0x001A7558`; auditing all eighteen slots: **11 translated, 1 stub, and
6 that do not exist at all** (no definition, no stub, no dispatch entry).

Every one verified against the XBE bytes first — two detected extents were
wrong (`0x0012B080` stopped one instruction short of its `xor al,al ; ret`;
`0x0012AE90` runs past its detected end). **Five recovered and spliced.**

**The chain:** scanning the class's code for `+0x78` accesses found the writer —
`0x0012C6B0` (vtable slot `+0x24`, one of the missing six) sets `[ecx+0x18]`
then jumps to `0x0012B3F0`, which does `mov [ebx+0x78],eax` and dispatches. The
state machine itself was translated all along; only its entry point was missing.

**It still does not advance, and the reason is one level up.** With the entry
point restored, neither it nor the state machine is ever called.
`0x0012C6B0` appears as a dword exactly once in the image — its vtable slot —
and the boot driver's ICALLs are slots 0x28/0x30/0x54/0x40/0x14/0x70/0x20/0x10/
0x74, **never 0x24**. So the manager is not stalled: it is **never started**.
Something in boot initialisation should call slot `+0x24` once, and that path
does not run.

Same shape as the last three blockers: a field with no writer because the
writer's caller is unreached.

**5/5 clean, zero crashes, 344 opens, ~27,100 draws, 3 dropped indirect calls,
probe-free.**

## Splash texture: cause found, fix blocked on memory layout (part 98)

`splash.xsh` = one entry `cont`, 512x512, linear A8R8G8B8 — decoded offline it
is exactly the "BASIC CONTROLS" screen. The install path is **correct**:
`GfxContext_ParseAndQueueTexture` (`0x000fa1d0`) → `QueueTextureFromRawData`
(`0x000fa2a0`) → `XGSwizzleRect` (`0x00178164`), called with the right
arguments (`src=0x025E48C0 pitch=2048 w=512 h=512 bpp=4`). All of
`XGSwizzleRect`'s helpers (`0x00177920`, `0x0017793F` mask builder,
`0x001779A0`/`0x001779C9` offset seeds) are present and faithfully translated.

The failure is addressing, not code: the swizzle writes `0x05457C80`, the GPU
is told `0x01457C80` — 64 MB apart, because `XBOX_TOTAL_RAM` is 140 MB and the
title's 53 MB arena straddles the line the title's own `va & 0x03FFFFFF`
assumes. Rebuilding the layout to a true 64 MB model fits arithmetically but
stops the boot; bisect shows the stack relocation, not the RAM size, is what
breaks it. Reverted; baseline restored (1068 allocations, exit 124, no crash).

New tooling: `XBOX_READ_LOG` (NtReadFile placement + the dropped Event/APC
args), `XBOX_SWZ_LOG` (XGSwizzleRect arguments).

Also noted: `sub_00178164`'s detected extent overruns into `sub_001788B2` at
the XGRPH section's very end (`0x001788B8`), returning without popping the
guest return address on that path.

## Splash: second defect found — the quad is 40x30 (part 99)

Frame capture (`XBOX_D3D_DUMP` + contact sheet) instead of inference. Current
build: draws 130,376, boot message renders frames 15–150, then the splash quad
appears as a **40x30 patch at x160–199, y360–389** — 1/16th scale, lower-left,
filled with garbage texels. So the splash has *two* independent bugs: the
texture address (arena above 64 MB) and the quad geometry. The geometry one is
cheaper and is wrong regardless of the texture.

Two memory layouts tried and reverted: moving the kernel block + TIB pool +
stack down as a unit puts the arena below 64 MB and swizzles the splash to a
GPU-reachable address, but renders nothing (draws → 0). Adding a spill region
above 64 MB for non-GPU allocations is sound and also works allocation-wise,
but inherits the same breakage. Shrinking the stack 8 MB → 1 MB *in place* is
safe and kept (7 MB reclaimed, draws 126,751).

Also: the 4096-byte heap floor is load-bearing — `ExAllocatePool` is called
~1028×/run with 96–400 byte requests, and giving each its own page costs
4.2 MB, but any smaller floor (even 512, which still shares pages) collapses
rendering. Something reads a neighbouring pool object; latent bug, own thread.

## Splash: one bug, not two — the quad was always correct (part 100)

`XBOX_D3D_VTXLOG` shows the splash draw's real geometry: a full-screen
640x480 quad, UVs 0..1, white vertex colour, correct texture bound. The
"40x30 quad" claimed in part 99 was wrong — that patch is the texture's single
populated 32x32 Morton tile (4,12) mapped across a correct full-screen quad,
landing at exactly x160-200/y360-390. **Fixing the texture address finishes the
splash; there is nothing else behind it.**

New tool: `tools/audit/layout_snap.py` (save/list/restore/diff) storing whole
memory-layout configurations under `RE_NOTES/layout_snapshots/` with what each
does on screen. Three saved: `baseline-A` (renders, wrong texels, draws ~130k),
`spill-B` (correct addresses, black screen), `high-C` (kernel block+stack above
64 MB — correct addresses, full 1051-allocation init, draws=0 then a fault in
`Mesh_RegisterVertexBuffers` with a -18 count).

## THE SPLASH RENDERS (part 101)

Root cause of every "renders nothing" layout: `recomp_types.h:381` hardcoded
`0x00741000` for the synthetic kernel PE header instead of following
`XBOX_FAKE_KERNEL_HEADER_VA`, so relocated layouts sent RenderWare's cache-line
reads into the title's arena. Now `g_xbox_fake_hdr_va`, defined in
`kernel_bridge.c`.

Also fixed: `XBOX_LOW_REGION_MIN_ALLOC` keeps small allocations out of the
GPU-visible low region (they were starving `MmAllocateContiguousMemoryEx` into
returning 0, which became a null `.xbd` mesh table and a crash in
`Mesh_RegisterVertexBuffers`); and **32 of 91 recovered functions were defined
but never registered in `recomp_dispatch.c`** — every indirect call to them
dropped silently.

Snapshot `high-E` renders "Checking hard disk" then the real BASIC CONTROLS
splash, full-screen, correct art and colours. Remaining: the title stalls at
frame ~165 calling through an uninitialised vtable (misses are `0xDEADC0DE`
fragments), which also freezes the fade-in at ~5% brightness.

## .text corruption traced to a NULL the title never checks (part 102)

`GfxContext_ApplyRenderStateDelta`'s jump table at Xbox VA 0x000FB030 reads
correct at load and zero at use. Cause: `sub_001423C0` (mesh remap fixup) gets
a NULL table from `FUN_00141d20`, does not check it, and writes
`*(0 + index*4)` for ~256K indices through the uncached alias — straight across
guest .text from VA 0.

Fixed along the way: the diag write-watch now covers RAM mirrors (independent
views carry their own protections) and its fault handler folds mirror addresses
back before matching (it truncated to uint32_t, so every mirror fault went
unhandled and killed the process). **Correction to part 99:** the 4096-byte
heap floor is NOT load-bearing — that test predated the fake-kernel-header fix.
Removing it returns 3.75 MB, and with a real free list plus
`XBOX_HEAP_BASE=0x00214000` nothing spills above 64 MB any more. That matters
because the title forms `0x80000000|(addr & 0x03FFFFFF)` for *any* pointer, so
nothing it can see may live above the line.

Snapshot `high-F`: boot text then the BASIC CONTROLS splash, then the crash above.

## Crash vs hang answered; ssxfe.big barely read (part 103)

Crash/hang is **not** the free list. Three runs each: reuse ON crashes 3/3,
reuse OFF 2/3. Reuse only makes it deterministic (kept on for that reason). The
earlier hang-vs-crash readings were run-to-run variance, from one run each.

The crash: `sub_001423C0` reads a NULL mesh index-list pointer (`psVar2+0x22`),
takes a huge count from it, and writes `*(0 + index*4)` upward from Xbox VA 0 —
`0x3EBFE*4 = 0xFAFF8`, which is why the jump table at 0x000FB030 dies first.

Upstream cause: `ssxfe.big` is 4,160,813 bytes and only **223** are ever read
(16-byte header + 207-byte directory start) before the corruption. Header is
`C0 FB 00 DB` followed by a path table (`data/models/ssxfE_L.`). `FILE_loadpack`
stops after the header and one short block — that is the real next target.

## ssxfe.big: entry resolves to the wrong 207 bytes (part 104)

Traced upstream of the part-103 NULL. The kernel size query is **correct**
(`eof=4160813`), and the requested names are well-formed
(`|data/models/ssxfe.{ltg,xbd,xsf,xsh}`, `|data/models/ssxfe_L.xsh`). Two
earlier readings corrected: the QINFO structure was not all-zero (the probe
printed CreationTime, not EndOfFile at +40), and the fifth pack name is not
empty (the probe caught the stack buffer mid-build).

The actual defect: the pack layer resolves every entry to **offset 16,
length 207** — the archive's path table, not the entry. Everything after that
is the title behaving correctly on wrong input: the buffer starts `"/m"`, so
`GetDecodedSize` returns 0, `FILE_LoadPackedGimexAsset` skips the decode and
returns the raw bytes, the caller reads an entry count out of ASCII path text,
and `sub_001423C0` walks a NULL list from VA 0.

Next: `FUN_0014c510` (size) / `FUN_0014c440` (open) under `FILE_load_2`.
New tooling: `[QINFO]` logging and read hexdumps in `XBOX_READ_LOG`,
`XBOX_PACK_LOG` for pack requests.

## ssxfe.big: the short read is correct — correcting part 104 (part 105)

Decoded the `.big` format: `C0 FB`, BE16 size-4, then
`{path'\0', BE24 offset, BE24 size}` from +0x0C. Entries are contiguous and the
last ends exactly at EOF.

`FUN_0014fae0`/`FUN_0014fb40` compute `BE16(hdr+2)+4 = 223` — so the 16+207
bytes are the pack **index**, loaded correctly and in full. Part 104's "the
entry resolves to offset 16 size 207" was wrong; nothing is short-reading.

Also cleared: the `|` prefix is a literal in `Level_LoadTrackAssets`'s
`"|data/models/%s%s"`; the kernel size query is right; and `sub_0014D850`'s
hand translation matches the XBE byte-for-byte through the pack-registry call
(`push 0x001FE4E8; call 0x001645C0`).

Remaining: after the index loads, nothing reads the entry — there is no third
read on the handle. The entry-lookup/read phase of `sub_0014D850`'s state
machine never runs for a `|` path. New tooling: `[SEEK]` logging (the run makes
zero seeks) and `XBOX_READ_TRACE` backtraces on small reads.

## Root cause: no pack is ever mounted (part 106)

`sub_0014D850`'s op state machine only ever runs **phase 0** for `|` paths.
Phases 6, 7 (read-remaining) and 10 (pack registration) never execute in a
whole run. Type-10 ops come from `sub_0014E720`, called only by `sub_0014C5B0`
(mount pack) — **zero calls measured**. Its callers `sub_00129630`/
`sub_00129E00` are never entered, and their call sites are in `sub_0007DCC4`,
which *is* reached — so the mount is gated off by a failing condition.

With an empty pack registry, `|data/models/ssxfe.*` resolves to the archive at
offset 0, so the 223-byte pack index is returned as entry data → count parsed
from ASCII path text → NULL index list → writes from VA 0 across .text → the
jump table at 0x000FB030 dies → crash. One defect, everything downstream is the
title behaving correctly on wrong input.

Next: the gate in `sub_0007DCC4`. New tooling: `XBOX_FSOP_LOG`.

## 29 dead branches; the pack lookup was one (part 107)

**Correcting part 106:** packs *are* mounted — the mount is phase 9, which I had
not logged. All eight register cleanly. The failure is the directory *lookup*.

Root cause: the disassembler splits a machine function at branch targets, and
flags are modelled per-function, so a compare in one piece and its `jcc` in the
next lose the condition — the lifter emits `int _flags = 0;` and the branch is
**never taken**. 29 such branches across 23 functions. One is `FUN_0014fba0`'s
entry-name `stricmp` (tested in `sub_0014FC8B`, branched in `sub_0014FC9A`), so
no pack entry name ever matched.

Fix: carry the operands, not the flags — `SPLIT_CMP` / `SPLIT_J*` in
`recomp_types.h` (thread-locals in `kernel_bridge.c`), applied by the new
`tools/audit/fixsplitflags.py`. Measured: crashes 3/3 → 1/3, frames 165 → 170,
and `texxbx.big`/`music.big` now open and decode.

Remaining: the title now produces pointers above the 140 MB mapped RAM
(`0x08CFB000`), which the mirror folds onto `.text`; and 26 dead branches whose
producer comparison isn't in the parser's recognised form.

## Pack system fully working; decoder overrun is what's left (part 108)

Crashes **3/3 → 0/3** after the split-flag fix. Seven archives now mount
(`anm/brdxbx/mdlxbx/texxbx/audio/music/speech.big`), both `C0FB` and `BIGF`
container types, entry handles are the negated table indices `FUN_0014c6d0`
hands out, and entry reads seek and chunk properly (55 seeks/run).

**Correcting part 104:** the "zero seeks" reading predated the fix — nothing
resolved, so nothing seeked. `off=0x0` in the read log means a NULL ByteOffset
(read from the seeked position), which is correct.

Remaining: `GimexBitmap_DecodeFormat10` doesn't terminate. The inputs are all
verified good — `src=10 FB 07 D1 A7` (valid header, size 512,423 matching
`GetDecodedSize`), sane destination `0x00768200` — yet it writes ~140 MB,
reaching `0x08CFB000`, which the RAM mirror folds onto guest `.text`. That is
the remaining `.text` corruption and why the splash's "loading..." draws as a
missing-glyph box.

## FIXED: the splash renders correctly (part 109)

Root cause: `TEST_S` (and `CMP_L/GE/LE/G`) evaluated signed conditions at 32
bits, but `LO8(r)` yields a `uint8_t` — so `test al,al ; js` asked whether a
0..255 value was negative. **Always false.** 49 `TEST_S` sites and 83 signed
compares use byte operands; every one of those branches was dead.

RefPack splits its opcode classes on bit 7, so opcode `0xE3` (a 16-byte literal
run) fell into the short-form branch. `GimexBitmap_DecodeFormat10` never
terminated — 2.4 billion iterations, ~140 MB into a 512 KB buffer, past mapped
RAM, mirror-folded onto guest `.text`.

Fixed with `RECOMP_SEXT`/`RECOMP_SIGNBIT` (switch on `sizeof`, which doesn't
promote, so it reads the width the lifter encoded). Verified against a
reference RefPack decode of `feanim.afl`: 37,591 ops, 512,423 bytes — the
runtime trace now matches op for op.

**Result (3 runs): 3/3 clean, 0 `.text` corruptions (was 21–37), 60 frames (was
18), draws ~155,000 (was ~2,562), and the BASIC CONTROLS splash rendering at
full brightness with correct colours and working "loading..." text.**

## Save enumeration FIXED: NtQueryDirectoryFile arity (part 110)

Xbox's `NtQueryDirectoryFile` takes **ten** arguments — there is a
`FILE_INFORMATION_CLASS` at index 7 between `Length` and `FileName`. Our bridge
declared nine (`case 207: return 36`) and read `FileName`/`RestartScan` one slot
early, so the pattern pointer was the literal `1` and every query returned
STATUS_NO_MORE_FILES. Fixed to 40 bytes with indices 7/8/9.

The title now enumerates the save device and opens **`U:\201120EF6C64\`** and
its `SaveMeta.xbx` — the real save on the HDD, never touched before.

Trap worth remembering: "never called" readings for `NtQueryDirectoryFile`,
`ExAllocatePool` and file seeks were all artefacts of the kernel call log's
200-entry cap. Zero from that log means "not in the first 200 calls".

Now stays on "Checking hard disk" instead of reaching the splash — it is doing
the save check properly but the device manager's state machine does not
advance, which is part 97's open thread (state setter = vtable slot `+0x24`).

## Dead-branch class closed at 4 of 29 (part 111)

Widened `fixsplitflags.py` (scan back 14 lines; stop at calls/compares; refuse
join points). 4 revived, up from 3. No regression, no unstick — 3/3 clean,
~118,000 draws, zero `.text` corruption.

The other 25 are **not** parser gaps: 10 have their flags set inside a called
function (7 of those are FPU transcendental argument reduction via
`fnstsw`/`sahf` in `sub_0015F07D`), 2 have no comment at all, 2 are in the known
misdisassembled float table, and 2 need OF/PF. Fixing the "call clobbers" group
needs callee-published flags — a separate, larger change with low expected value.

## Autoload chain mapped; one untranslated span left (part 112)

Boot driver `sub_000AF7B0` advances 0→1→3→5 and parks. State 5 gates on the
save-device manager's vtable slot `+0x6C` (`sub_0012AA50`:
`cmp [ecx+0x78],0x12 ; sete al`) — autoload needs manager **state 18**.

Re-audited all 32 manager vtable slots: **10 were absent**, not the 6 part 97
recorded. Recovered five, including the gate itself (`0x0012AA50`, which sat
between two functions part 97 had recovered) and `0x0012C8F0` (slot `+0x70`,
`push 0x14 ; call 0x12b3f0`).

Remaining blocker: of 38 calls to the state machine across `.text`, exactly one
passes 18 — `0x0012CB90` — and it is **inside no translated function**.
`sub_0012CAAA` was recovered ending at its first `ret` (`0x0012CB0B`) but the
code continues; `0x0012CB0B-0x0012CDA0` is untranslated and holds nine
state-machine calls. Needs the re-seed pipeline, not a hand-write.

State: 3/3 clean, ~117,000 draws, zero `.text` corruption.

### Part 113 -- save-manager state machine

* Decoded the manager's two-level state switch at `sub_0012C978`: byte index
  map at `0x0012CD88` (19 entries) selecting into a 7-entry jump table at
  `0x0012CD6C`, dispatching on **state - 2**, i.e. states 2..20. State 18
  goes to `sub_0012CAA3`.
* Recovered the three switch targets that had no body at all
  (`0x0012C9B7`, `0x0012C995`, `0x0012C9D8`) plus their callees
  (`0x0012CA34`, `0x00150950`). All seven targets now exist.
* `sub_0012B3F0` identified as the state setter (`MEM32(this + 0x78) = arg`).
* Found `0x0012CD69` is not code -- it is the jump table plus alignment
  padding, wrongly seeded as a function. Unreachable; documented.
* Found 48 dispatch-table entries unreachable by `recomp_lookup`'s binary
  search because they were appended out of order. Moved the 11 save-manager
  entries into sorted position; the other 37 deliberately left alone.
* Verified 3/3 clean, exit 124, crash 0, draws 119334/119355/119378,
  textcorrupt 0.
* Open: nothing ever calls `sub_0012B3F0`, so the machine never leaves state
  0 and boot state 5 waits forever for state 18.

### Part 114 -- save autoload works

* Recovered manager vtable slot **+0x64 = `sub_0012C820`**, the method that
  starts the state machine (`sub_0012B3F0(this, 16)`), and **+0xE4 =
  `sub_0012AD40`**, the autoload gate. Neither had a body, so both calls were
  dropped silently.
* Fixed a join point in the save-record check where two `cmp` instructions
  share one `je` and the lifter kept only the second -- a record **size**
  check had become a `0x8080` magic check. Carried operands with `SPLIT_CMP`
  and `SPLIT_JE`.
* Fixed `NtQueryDirectoryFile`: `DIR_CONTEXT` slots keyed on a reusable
  `HANDLE` were only freed on scan exhaustion, so a new directory open could
  inherit an abandoned scan and its pattern. Added
  `xbox_dir_context_release()` on `NtClose`, and made a pattern change restart
  the scan.
* Result: the boot driver runs 0->1->3->5->6->7->8->9->25 and the manager
  steps 16->17->19->18->20->21 -- the xemu sequence, including "Autoloading
  from hard disk". Splash verified by frame capture; draws ~127,700.
* Open: one run in eight segfaults on the newly-live autoload path.

### Part 115 -- the boot text sequence is complete

* Found why the middle phase was blank: `StartScreen_RenderStatusText`
  dispatches per boot state through a byte remap at `0x000AF788` into a
  17-entry jump table at `0x000AF744`, and **14 of the 17 handlers had no body
  at all**. State 6 selects `0x000AF2AE`, the "Autoloading from hard disk"
  handler (string id `0xBA8`).
* Recovered all 14 through the seed pipeline, plus 9 closure fragments.
* The same miss also leaked 2,184 bytes of stack per call, because
  `RECOMP_ITAIL`'s miss path skips the handler epilogue that unwinds
  `StartScreen_RenderStatusText`'s `0x888` frame. That is almost certainly the
  intermittent segfault from part 114 — 11 consecutive clean runs since.
* Removed a duplicate `sub_0014FEC0` that closure created for an address
  already present as `Localization_ResolveString`, and redirected its 17 call
  sites.
* Verified by frame capture: text width 203 px, 293 px, 203 px, then the
  splash — Checking hard disk, Autoloading from hard disk, Checking hard disk,
  splash. Matches the reference emulator.
* 11/11 clean, draws ~131,000.

### Part 116 -- boot completes; the frontend screen is what waits

* Disproved the assumption that boot state 25 is a dead end. Probed live: the
  handler at `loc_000B0289` sees `f3e88=1, f3e8c=1` and advances to state 30 in
  the same frame, setting the screen's done flag `MEM8(this + 0x48)`.
* `StartScreen_TickExitWhenDone` then runs the teardown `sub_000ADFD0` — fade,
  wait, destroy the save manager, return 1. A call counter shows
  `StartScreen_Render` stops being called while draws continue at ~1,300/s, so
  **the next screen is already up** and drawing the same loading art.
* Confirmed the frontend gets everything it asks for: 718 opens including
  `ssxfe.big` (streaming correctly as a `C0 FB` pack of RefPack members),
  `fe_1.xsh` in full, character archives, audio, fonts, and the save data.
* Audited the StartScreen vtable at `0x0019A724`: three slots had no dispatch
  entry. Recovered `0x000AED10` (destructor) and `0x000AED00` (the getter for
  the screen's own finished flag at `+0x3E74`), plus `0x0012A2B0`. Held back
  `0x0015CF26`, which calls the still-unliftable `0x0015FE32`.
* 4/4 clean, draws unchanged in rate.
* Open: identify the screen object now on top and the condition it polls.

### Part 117 -- frontend constructed, never driven

* Wrote `xboxrecomp/tools/audit/vtaudit.py`: audits a guest vtable against the
  dispatch table, reporting per slot whether it is registered and whether
  `recomp_lookup`'s binary search can reach it. Encodes the three traps that
  produced wrong answers by hand (CRLF anchors, one-line placeholders, and a
  `.data` walk-back that invents vtables out of unrelated words).
* That last trap caught a false positive of my own: `0x001C72C0` audited as an
  18-slot vtable with every slot missing, but nothing in `.text` stores it and
  its "methods" start mid-instruction. The tool now warns on it.
* Recovered the genuinely unregistered slots: 3 on **ScreenBase**
  (`0x0019654C`, inherited by every screen) and 5 on screen class
  `0x0019AB88`, plus 4 closure functions. Both now resolve completely.
* Located the frontend class vtable at **`0x00196960`**: `FEInit_Boot` plus the
  five `TitleIntroSequence_*` methods, all translated and registered.
* Probed live: `FEInit_Boot` runs exactly once, storing the intro object on the
  app at `+0x730`, and **no intro method ever runs** -- not Tick, not Render,
  not IsComplete. Nor does `sub_0007C780`, the frontend render driver.
* So the blocker is now one layer past a missing body: the object is built and
  registered, and nothing drives it each frame.
* 3/3 clean, draws unchanged.

### Part 118 -- the switch works; the main loop stops

* **Correction to part 117:** the StartScreen vtable base is `0x0019A744`, not
  `0x0019A724` -- the live object holds it, and at that base every slot
  resolves. The "unregistered slot" reported in parts 116/117 was an artifact
  of auditing 8 slots too early.
* **`0x0015CF26` is MSVC `_purecall`** (`_amsg_exit(25)` = R6025 "pure virtual
  function call", then `exit(255)`). Registering it would terminate the
  process; it must stay unregistered. Seeing it in an audit means the abstract
  base vtable is being read rather than the concrete one.
* **The fade works.** Frame capture across the transition shows the splash
  fading in over ~60 frames, mean (4,6,7) to (83,124,153). The missing
  fade-out is not a separate bug -- nothing renders after the splash, so the
  last frame persists.
* **The screen switch works.** `[app_vtable+0xC]` builds the next screen and
  the app installs it: the new current screen is `vt=0x00196960`, the frontend
  / TitleIntroSequence class. `FEInit_Boot` is its `[vt+0x04]` and runs once.
* **The main loop stops.** The three per-frame sites that update the current
  screen (`[vt+0x14]` render, `[vt+0x18]` tick x2) are never reached again
  after the switch, and the loop runs fewer than 600 iterations in 60 seconds
  before ceasing to advance. Drawing continues at ~1,300/s from elsewhere,
  which is why it looked healthy.
* Recovered the genuinely missing slots: 3 on ScreenBase (`0x0019654C`,
  inherited by every screen) and 5 on screen class `0x0019AB88`, plus 4
  closure functions. Both now fully resolve.
* 4/4 clean on the unprobed build.
* Next: instrument the callback table at `0x001FE2F0` walked by
  `sub_0014B570`, the last call before the loop stops.

### Part 119 -- frozen splash traced to one unset pointer

Full chain, every step measured:

1. Timestamped heartbeat: the guest main loop runs at 60 Hz to iteration 300
   (t=5.76 s) and then stops dead. Drawing continues from the GPU pump, which
   is why it kept looking healthy.
2. A single global sequence number across all checkpoints puts the last event
   at `loc_000AA1B0` calling `[frontend->vtable + 4]` = `FEInit_Boot`, which
   never returns.
3. Chain: `FEInit_Boot` -> ... -> `Level_LoadTrackAssets(0x1FAF88, -1)` ->
   `sub_001423C0` -> `sub_00141D20`, the pack-directory walk (131 entries,
   allocation and bounds all sane).
4. Measuring esp/esi/edi across each entry: 51 calls drift 0, **one drifts
   -84**, and `esi` comes back 51 -> 1,715,000 with `edi` -> 0. The loop index
   and the pointer its bound is read through are both destroyed, so it runs
   unbounded.
5. The drift is an **ITAIL miss to 0x10010000** in
   `GfxContext_QueueTextureFromRawData`. `RECOMP_ITAIL` has no `saved_esp` to
   restore, unlike `RECOMP_ICALL_SAFE` -- so every ITAIL miss skips the
   epilogue *and* unbalances the caller. Worth hardening as a class.
6. The target was garbage because the jump table at `0x000FA544` is
   **corrupted in guest .text**: entries [9] and [10] hold `10010000` and
   `00873C70` instead of `000FA2D8` and `000FA2EB`.
7. `XBOX_DIAG_WATCH=000FA568` named the writer immediately: a loop at
   `loc_001428D6` writing two dwords per element with **stride 0x90**, walking
   from `0x000FA058` straight up through `.text`.
8. Root cause: `element = MEM32(table + 0x5C) + index * 0x90`, and that base
   field holds `0x000F5D00` -- a `.text` address. The array base was never
   populated. Every function in the chain is a faithful translation; one
   structure field is unset.

Probably also the long-standing "archive iteration stops after a handful of
textures" blocker -- the texture install dies at entry 51 because this same
init sequence destroyed the table it dispatches through.

3/3 clean; the 1,230 tracing probe lines were stripped afterwards.
Next: find what should write `+0x5C` on the object at `0x001FAF88`.

### Part 120 -- the .text corruption is fixed

* Identified `MEM32(table + 0x5C)` as a **file-relative offset** awaiting
  relocation, not an uninitialised pointer: the table at `0x00917580` is a read
  buffer for `ssxfe.big`, and `0x000F5D90` is ~1 MB into a 4 MB archive.
* Found why relocation never ran, using `vtaudit.py`: the **CmdTable** class
  (vtable `0x001A7AE8`) had **27 of 35 slots unregistered**, and the loader
  class (`0x0019AAC0`) was missing slot **+0x28 = `sub_001435E0`** -- the
  504-byte method that walks loaded records and relocates them. That was the
  unresolved ICALL target that had been in the miss list since part 113.
* Recovered all of them. Both vtables now resolve fully (except `0x000B0340`,
  which the seeder cannot reach).
* **Verified fixed:** `+0x58/+0x5C/+0x60` now hold heap pointers
  (`0x009A2040 / 0x00A0D310 / 0x00A19BE0`) instead of `.text` addresses; zero
  `0x10010000` ICALL misses; **zero `.text` writes**.
* Second blocker on the same path: `sub_0007DFC5` was an undetected stub the
  frontend init tail-jumps into. Recovered it plus ~20 closure functions in the
  construction chain; the trace now runs ~25,000 checkpoints deep.
* 3/3 clean, exit 124, draws ~89,700.
* **Reverted:** a later batch of nine recoveries reintroduced `.text`
  corruption (exit 127, textcorrupt=1, draws ~7,200). The batch before it had
  measured clean at 158,602 draws but was rolled back with it for lack of an
  intermediate backup -- re-apply that one separately.
* Open: the splash still holds; the frontend init reaches unprobed code. The
  frontier recedes with each batch rather than being a single wall.

### Part 121 -- gated recovery loop; a movaps that moved 4 of 16 bytes

* Built `xboxrecomp/tools/audit/recover_batch.py`: one batch end to end --
  backup, seed, lift, splice, linker-driven closure, build, measure, and
  **auto-revert unless it passes**. Gate is `exit==124`, `crash==0`,
  `textcorrupt==0`, draws in band; any one alone lies.
* It encodes the traps that each cost a cycle: backup per batch; closure
  matching *both* `undefined reference` and `multiple definition`; skipping an
  address already registered under a different symbol (and rewriting its call
  sites, or closure loops forever); and splitting bodies by scanning to a lone
  `}` rather than on blank lines, which shreds generated functions.
* **Real lifter bug fixed:** `movaps` emitted as a scalar `float` copy --
  4 of 16 bytes. Only 1 site in the tree against 2,960 correct ones, but that
  site was `sub_0007E189`, which normalises a vector and was storing a quarter
  of the result. Corrected, and the tool now corrects it on every lift. Note
  this is worse than a dropped call: the dropped call left the old value
  intact; the scalar write leaves three quarters stale.
* **Kept, each gated:** 13 functions -- batch A (7 + closure), then
  `0x0007FFB0`, `0x00084D80`, `0x00085820`, `0x00013150`, `0x000A3A60`,
  `0x000A9AE0`.
* **Held back with evidence:** `0x0007FF70` and `0x000A5320`, both collapsing
  draws ~89,000 to ~7,300. Checked against frames rather than the counter --
  with `0x000A5320` enabled the screen is black all run and the splash never
  appears, so it is a real regression, not progress. `0x000FB080` cannot be
  lifted.
* 3/3 clean, draws ~89,780, zero corruption.

### Part 122 -- the held-back functions were building the title screen

* The two functions part 121 rejected are the ones that build the **title
  screen**: probing them showed labels resolving to `kFEStartGame` "Start
  Game", `kFEDVDContent` "DVD Content", `kFE_TitleScreen` "Press START
  button", `kFECopyright`. The draw-count drop was the frontend leaving the
  busy splash for an empty menu, not a regression. Label count is now **12**,
  from none.
* Traced `TitleIntroSequence_QueueBootVideos`: it spins waiting on 12 items.
  The items looked like garbage (`vt=3`) but a direct dump showed all 12 valid
  -- `esi` was being destroyed, `0x006E331C` -> `0`, across one iteration.
* **New general defect:** an `ICALL_SAFE` **miss leaks its pushed arguments**
  when the lifter captures `saved_esp` *after* the pushes (a branch hoists
  them). Two identical `[vt+0x3C]` calls in one function differed for exactly
  this reason -- one balanced, one leaking 16 bytes, which clobbered `esi` and
  hung the intro. An unresolved ICALL is therefore not "a call that does
  nothing": it can corrupt the caller's frame. Cure is to remove the miss.
* Hand-transcribed `0x000FB080` (16-byte field copy, `ret 4`): the seeder
  cannot reach it because a jump table sits immediately before it. Registering
  it silenced the wait loop and took draws 7,478 -> 55,391 in that run.
* The ICALL miss frontier is now **exhausted** -- no real targets left.
* **Screen is black** after the splash flash: worse to look at than part 121,
  and the menu is not reached. 4/4 clean, draws ~7,950, zero corruption; one
  intermittent segfault seen.
* Next: why the title screen draws without presenting -- 55,391 draws against
  12 presents in 100 s.

### Part 123 -- the two boot videos located; tracing overhead was skewing measurements

* Confirmed the console sequence the user described. The two boot videos are
  `data/video/eabig.mpc` (VA `0x001966B8`, referenced from `0x0007CB65`) and
  `data/video/ssxintro.mpc` (VA `0x001966D0`, from `0x0007CADD`), both inside
  `TitleIntroSequence`, copied into an item buffer at `+0x4C` by two byte-copy
  loops.
* **Neither is ever opened**, and probes on `TitleIntroSequence_Tick`,
  `_CheckLoaderReady`, `_IsComplete` and both filename copies fire zero times.
  So reaching the menu does not require an MPC decoder first -- the intro
  sequence has to run before decoding is even asked for.
* **Measurement caveat found:** parts 121-122 took several readings with ~4,200
  label probes active, each `fprintf` + `fflush`. That is enormously slow and
  conflated a real stall with the instrument. Stripped all 4,265 probe lines
  and re-measured clean: **5/5, exit 124, zero crashes, zero corruption, draws
  ~7,950**. The segfault seen during frame capture does not reproduce without
  the dump pressure. Rule: confirm any stall with tracing removed.
* Where time actually goes: `UI_BuildButtonGroup` (0x00086170), walking a
  bounded 17-entry table at `0x001B9D00` and allocating per match -- menu
  construction, not a spin.
* Screen still black past the splash. 12 title-screen labels build.
* Next: why the built UI never presents (one run: 55,391 draws vs 12 presents
  in 100 s), and the `0x00082D30` widget-tree recursion.

### Part 124 -- the menu text is drawn every frame; it is invisible, not absent

* The frontend **does** present: ~353 clears and ~8,300 draws per 60 s run, so
  about 6 fps at ~23 draws a frame -- the same per-frame count the splash had.
* The draws are **menu glyphs**: 4-vertex white quads marching along `y ~ 101`
  (`x` 257 -> 269 -> 282 ... 444), the signature part 90 identified as text.
  So the menu is submitted, in the right place, every frame, and simply not
  visible.
* Ruled out, each by measurement:
  - **Full-screen black quad.** The frame does end with an opaque
    `(0,0)-(640,480)` `FF000000` quad under SRC_ALPHA blending. Added an
    env-gated filter to drop exactly that draw -- **screen stayed black**.
  - **Depth clipping.** Pre-transformed vertices clip on z outside [0,1]
    regardless of the depth test, and some early draws carry `-6.26e16`.
    Counted: **36 of 6,000**, not rising. Not the cause.
  - **Depth test.** `enable=0`, discards nothing.
* Remaining hypothesis: the glyphs sample an **empty font texture**. With
  SRC_ALPHA/INV_SRC_ALPHA an all-zero-alpha texture makes a quad invisible
  while still counting as a draw, which fits every measurement. Uploads do
  happen (128x128, 256x256, 512x512 all seen), so the question is which
  texture is bound for the glyph draws and whether it has data -- the old
  "archive iteration stops after a handful of textures" blocker.
* 5/5 clean, zero corruption. Diagnostics left in tree, env-gated and off:
  `XBOX_SKIP_BLACK_QUAD`, `XBOX_ZCLIP_LOG`.

### Part 125 -- splash timed at 1.5 s; drawing stops dead after it

* Timed the frames with a wall-clock stamp per dump:
  - `t=0.00..4.81s` boot text, presents 0-290, about **60 presents a second**
  - `t=4.98..5.48s` the splash art
  - `t=7.09s` onward black, and presents drop to **2 a second**
  So the splash is up ~1.5 s. Some of that shortening is legitimate -- assets
  come off a host filesystem with no DVD seek and the save is a local read --
  but the thirty-fold frame-rate collapse at the same instant is not.
* **Corrected part 124.** A draws-per-second counter shows all **8,178 draws
  happen in the first ~5 s** and then stop completely; the push-buffer counter
  freezes at 701,513 dwords in 336 batches. The glyph quads sampled last part
  were the boot message and splash, not the menu. So the menu is *not* drawn
  and invisible -- **nothing is drawn at all** once the frontend takes over.
* This kills the part-124 hypotheses (empty font texture, black full-screen
  quad, depth clipping): all were about draws being hidden, and after t=7s
  there are no draws to hide.
* The guest stays alive throughout -- ~5,400 kernel calls per report window,
  mostly critical sections, IRQL, waits and timers -- so it is running and
  simply not submitting GPU work. Matches parts 117-118 from the other side:
  `TitleIntroSequence_Render` has never been observed to run.
* Diagnostics added, env-gated and off: `XBOX_PRESENT_TIME`,
  `XBOX_DRAWRATE_LOG` (plus `XBOX_SKIP_BLACK_QUAD`, `XBOX_ZCLIP_LOG`).
* 3/4 clean, ~8,200 draws, ~350 clears, zero corruption.

### Part 126 -- watchdog pins the stuck thread at the intro wait loop

* **Presentation is already capped**: `Present(swap_chain, 1, 0)` is vsync
  interval 1, locked to the display refresh. The 2 presents/s seen later is not
  a cap, it is the guest not producing frames.
* **The main loop runs 60/s for 5 s then stops dead** (iteration counter), with
  15-16 ms per iteration while alive and exactly one >100 ms stall per run. The
  ~5,400 kernel calls per report window that continue are other threads.
* The tick gate `InputManager_PollDevicesIntoCache` is a ring (head `+0xC`,
  tail `+0x10`) that reads empty from t=6s -- but its producer `sub_000A8EE0`
  keeps enqueueing at 60/s all run. With the consumer stopped the tail laps the
  head, so a full ring looks empty. **Consequence, not cause.**
* **Built a watchdog**: one global store per guest label (`g_last_loc`, 9,929
  stamps, no I/O) reported from a probe on the still-running enqueue thread.
  Measured free -- draws ~8,150 and clears ~350 unchanged. It names the stuck
  location directly: **`lastloc=0x0007CA02`**, the intro's wait-for-video loop
  in `TitleIntroSequence_QueueBootVideos`. An earlier reading that this loop
  "no longer runs" was wrong; its probes had been stripped, not its code.
* **Blocker stated exactly:** readiness is `MEM8(item->[0x98] + 0x4504)`; the
  only writer in the image is at `0x001071EF` inside `sub_00106E20`, which
  never runs, nor do its callers `sub_00107200` / `sub_00107453`. Next question
  is well-formed: what should call those each frame.
* 3/4 clean, ~8,150 draws, zero corruption; the intermittent segfault persists
  at roughly one run in four.

### Part 127 -- intro wait loop cleared; four chained bugs fixed

* **General lifter bug found and swept**: `RECOMP_ICALL_SAFE` captured
  `_icall_esp` before a function's callee-saved register pushes, so an ICALL
  miss rewound `esp` past the frame saves and the epilogue popped stack
  garbage. Measured as a +12 drift with `esi` becoming `0x61746164` ("data").
  New pass `tools/audit/fixicallsaves.py` keys on a verified prologue-save /
  epilogue-pop LIFO match (reading the pop order as a sequence, since the
  lifter interleaves stores between pops). **86 sites fixed.**
* **Video-stream handler vtable `0x001A7328` was 15/32 unregistered**,
  including `+0x2C`, the async-read completion query. Recovered 15 (+3 by
  closure) in one gated batch.
* **The completion callback `0x0012A170` did not exist at all** -- registered
  via `ASYNCFILE_setcallback` from `0x0012A1D0`. Nothing in the image wrote the
  "read complete" state 3; scanning all four `.text` references of that field
  proved it. Recovering it made the readiness flip **ready for the first time**.
* **`0x00104760`** -- unresolved ICALL target called from inside `sub_0010A4E0`,
  the decode function that then crashed on first execution. Recovered; crash
  gone.
* **Net effect:** the intro wait loop exits. `sub_00106E20` now reaches
  `0x001071EF`, the `+0x4504` flag writer that part 126 proved had never run.
  The stall moved from `0x0007CA02` to the **second** boot video, consistent
  with the console sequence of two videos before the menu.
* **Watchdog corrected**: `g_last_loc` is shared across guest threads and its
  reporter's own caller stamps it, giving a bogus reading. Added `g_main_loc`,
  stamped only in `Application_RunMainLoop`; it pins the block at
  `loc_000AA1F1`.
* **Permanent diagnostics**: crash reporter in `main.c` (code, module RVA,
  guest loc) plus `-g` in Release flags, so `addr2line` resolves a segfault to
  an exact generated line.
* **Reverted**: sorting the dispatch table to make `0x000151F0` reachable and
  registering 8 more definitions collapsed draws to 0 and segfaulted 2/4 runs.
  Same failure as the earlier 48-entry attempt; needs per-entry bisection.
  7 unregistered definitions live in `recomp_stubs_unresolved.c` and must never
  be registered -- they are the empty esp-corrupting stubs.
* **Tooling**: `recover_batch.py --min-draws` default was a stale 50,000 and
  silently reverted a clean batch; corrected to 7,500 against the ~8,150
  baseline.
* 3/3 clean, exit 124, zero crashes, zero `.text` corruption, draws ~8,300-8,700.
* **Intro progress is now measurable**: 4 of the 12 boot-video items complete (0 before this part); stalls on item 5. Probes gated behind `XBOX_TICK_LOG`.
* **`0x000151F0` is a trap**: registered but past the sorted region, so unreachable. Moving *only that one entry* into place collapses draws to 0 and completes 0 items -- its unreachability was protective and the body must be verified against the original bytes first. Reverted. Treat "registered but unreachable" as a finding about the function, not a table bug.
* 4/4 clean at end of part, draws ~8,100-8,300.

### Part 128 -- 15.6 ms scheduler tick fixed; durable tooling added

* **Correction to part 127**: the intro was never stalled at item 5, it was *slow*. An 80 s run reaches 8 of 12 items and keeps going; a 55 s sample caught it mid-way. Sampling a slow process early and calling it hung repeats part 123.
* **`timeBeginPeriod` was never called.** The bridge is written for the Xbox kernel millisecond timers (`KeDelayExecutionThread` clamps to `Sleep(1)`, the `KeTickCount` thread runs a "1 ms cadence"), but Windows defaults to a ~15.6 ms scheduler tick, so every short wait slept 15x too long. The intro polls async reads through those waits, so boot ran at roughly a fifteenth speed. Fixed in `main.c`, released via `atexit`.
* **Measured**: intro items 1-3 complete in **0.03 s** (previously seconds), and **5/5 clean runs** against 2/3 immediately before -- the best stability of the session.
* **The remaining intro blocker is a race**, not a missing function: runs finish 4, 5 or 8 items, one spinning 16.6M ticks. Traced to the real object this time -- `sub_00106BD0`s `this` is `MEM32(MEM32(MEM32(0x1E3C7C)+0x730)+0x14)`, a global stream manager, not the item context at `+0x98` as part 127 assumed. Slot 0 sits in state 2 where the gate wants 1.
* **Assets identified**: `data/char/eddie_body.mxf`, `eddie_head.mxf`, `board.mxf` plus `|data/char/*.xsh` shaders. Shaders reach state 3; the `.mxf` models sit at state 1 -- this is character-model loading, connecting to the rider/prop mesh gap.
* **Verified against the XBE**: the model class vtable really has a bare `ret` at `+0x18`, so it genuinely does not load through the shader classs pump. Not a translation gap.
* **New tools** in `xboxrecomp/tools/audit/`: `xbrun.py` (gated N-run harness that appends every measurement to `RE_NOTES/measurements.jsonl`, with `--history`/`--compare`), `findings.py` (address ledger; `recover_batch.py` now refuses recorded traps), `xbdiag.py` (client for the live diag server).
* **Write-watch made usable**: it reported every write to the whole 4 KB page, burying the one that mattered. Now takes an exact range (`XBOX_DIAG_WATCH=<va>[:<len>]`) and prints `g_main_loc`/`g_last_loc` with the frames -- two reports instead of hundreds, and `addr2line` named the writer in one step. Limitation: the page is unprotected between the fault and the re-arming single step, so a concurrent write from another thread is missed.
* 5/5 clean, draws ~8,200, zero crashes, zero `.text` corruption.

### Part 129 -- eight bytes: the BASIC CONTROLS screen renders

* **The intro chain is sound.** Instrumenting `edge -> gate -> flag -> done` across five runs: `edges == gates` always, `flags` trails by one only when the run ends mid-item. One run completed **all twelve items** in 32 s and the main loop **resumed** (iters 269 at t=5s to 291 at t=39s), moving the watchdog from the intro wait to `loc_000AA244`, the Render call.
* **Two part-127 corrections.** `sub_00106BD0`s `this` is `MEM32(MEM32(MEM32(0x1E3C7C)+0x730)+0x14)`, a global stream manager -- not the item context at `+0x98`. And `sub_00106BD0` sets state 2 **itself** on success, so it is a one-shot edge; state 2 at a stall is the aftermath, not the fault.
* **The intermittent segfault, found.** It resolved every time to `UI_BuildButtonGroup` (`recomp_0002.c:64678`), the menu construction that only runs on runs that get far enough. The object there is allocated, memset with the games own `0xDEADC0DE` poison, then constructed -- and `esi` went from a valid `0x021C8F60` to `0x0000003E` across one call, `sub_000A4410`. An esp trace showed it 16 bytes low at the epilogue, so the pops landed garbage in `esi`.
* **Cause: `sub_000A3E60`, an empty "not detected" stub.** Each of its two call sites pushes one argument plus the conv slot; an empty body consumes neither, leaking 8 bytes a call. Seeder could not lift it, so transcribed by hand from the XBE -- three instructions ending in a **tail jump** to `IconAtlas_GetEntry`, whose `ret 4` balances the stack on its behalf. Call sites are direct, so no dispatch entry was added.
* **Result: draws ~8,216 -> ~62,355 (7.6x), frames ~350 -> ~2,707, 6/6 clean**, and four unresolved indirect targets disappeared with it.
* **Verified by frame capture**: the **BASIC CONTROLS** screen renders in full -- logo, controller diagram with every label, snowflake background, "loading...", and a fully textured, lit **character model**. The `.mxf` model path works end to end. Static across 3,000 frames, so the next question is what the loading screen still waits on.
* Note `XBOX_D3D_DUMP` takes a **path prefix**, not a directory.

### Part 130 -- the loading screen waits on an interrupt that never fires

* **Ruled out by measurement, not assumption**: file I/O (ledger byte-identical at t=16s and t=36s), pending async requests (all 64 slots idle), the screens own loader (`ScreenBase_TickAsyncAssetLoad` reaches terminal phase 4 in 7 calls; all 5 jump-table handlers registered; `obj+0xC0 == 4`), and the track loader (527 stamps across `0x00142xxx` never appear).
* **Watchdog reading corrected**: 466/533 samples sat on a 15-instruction leaf (`loc_0007D360`). That is the last *stamped* label before a long unstamped stretch, not a hot spot -- `g_main_loc` keeps the stale value. It has no direct callers; `__builtin_return_address(0)` relative to `&__ImageBase` named them (`GetModuleHandleA(0)` returns NULL there and yields nonsense).
* **The blocker**: `loc_000AA20C` is reached exactly **twice** a run, so the main loop is not cycling. `loc_000AA296` resolves to **`XBoxExecutionMan_WaitForFrameEvent`** (`0x000B2750`, vtable `0x0019AE84` slot `+0x0C`) -- an INFINITE wait entered four times and never returning from the fourth.
* **`XBoxExecutionMan_SignalFrameEvent` (`0x000B2760`) is called by nothing in the entire translated image.** It exists only as vtable slot `+0x10`. The main thread waits forever on an event no code sets; the static picture is the last frame re-presented.
* **Root cause is a gap the tree already documents**: `kernel_bridge.c`s own comment on `KeInitializeInterrupt`/`KeConnectInterrupt` says it stores the ServiceRoutine but "does not yet simulate periodic VBLANK-style delivery" to it. On hardware that ISR sets this event. Fix: deliver the connected KINTERRUPTs ServiceRoutine at ~60 Hz, as `xbox_memory_layout.c` already pulses `XBOX_VBLANK_EVENT_VA` on a 16 ms ticker -- but this one calls **guest** code, so it needs a thread with proper guest register state.
* `0x000B2620` (execution-manager vtable slot +0x00) is unregistered; recorded in the ledger.
* 5/5 clean, ~62,336 draws, ~2,707 frames.

### Part 131 -- part 130 was wrong; videos still do not run

* **Correction.** Part 130 claimed a deadlock in `XBoxExecutionMan_WaitForFrameEvent`. Host-side instrumentation disproved it: every wait returns `STATUS_SUCCESS` immediately. The sets do go to other handles (tokens 2/3, not 1) but that is moot when nothing blocks.
* **The methodological error is the useful part**: a guest-side probe counting *entries* to a function cannot tell "never returned from the 4th call" from "was simply not called a 5th time". Probe the **host** side of a kernel call, where the return code is visible, before declaring anything blocked.
* **Eliminated by measurement**: the frame timer (`Application_FrameTimerCallback` fires ~52/s and is itself the frame loop), the callback pump (`sub_0014B570`: 23,000 entries, 23,000 returns), and the frontend script -- `Script_PlayByName("FEStartScript")` runs and `Script_DispatchOpcode` executes 45 opcodes, all `0x0B` (TexFlip construction), across **45 distinct** records at a clean `0x20` stride. The script completes; it is not spinning.
* **False alarm worth noting**: `sub_00083288` carries four comments about dropped `fnstsw`/`test ah` pairs, but the code does emit `FNSTSW_AX` and `FPU_PARITY` is real. Those are historical notes on an applied fix, not live defects.
* **What is true**: `FEInit_Boot` never returns to the main loop, so `TitleIntroSequence_Tick` never runs and **no video code executes at all** -- `VideoPlayer_Open`, `VideoPlayer_Tick` and the three `eabig.mpc`/`ssxintro.mpc` filename builders all have zero call counts. The videos are not failing to play; the frontend never asks for them.
* **New tooling**: diag server `threads` command -- suspend each thread, read `RIP`, report a module RVA. Added because the label watchdog kept pointing at a 15-instruction leaf (the last stamp before unstamped code). It showed one thread in the image and the rest in ntdll waits: a healthy frame loop, not a deadlock.
* **Care note**: an over-broad regex stripping probes ate a closing brace and left `bridge_NtWaitForSingleObjectEx` unterminated. Third time probe removal has damaged real code -- use line-based removal, not regex.
* 4/5 clean, ~62,340 draws, unchanged from the start of the part; all instrumentation stripped again.

### Part 132 -- the spin located exactly: a list that is NULL where it must self-link

* **Why parts 129-131 took so long**: all three inferred position from the label watchdog, which only tells the truth while execution stays inside stamped code. It gave three confident wrong answers (a 15-instruction leaf as a "hot spot", a frame-event "deadlock", and `FEInit_Boot` "never returning" when it had returned long before). Extending the `threads` command to **scan the thread stack for in-image return addresses** replaced three parts of inference with one sample: `Application_RunMainLoop -> sub_00099717 -> sub_00099867 -> UI_BuildButtonGroup -> sub_00083288`.
* **The bug**: `UI_BuildButtonGroup` enters once and never exits. Its loop is bounded (`edi += 0xC` to `0x1B9DCC`, 17 iterations) and `edi` advances correctly, so no clobbered counter -- it stops inside the **6th** iteration.
* `sub_00083288` walks a list at `object + 0xF8` via `ebx = MEM32(ebx + 0xC)`, ending only when `sub_000A3890` sees a **self-link** (`[ecx+8]==ecx` or `[ecx+0xC]==ecx`). The node `0x007696BC` has **next=0, prev=0**. The walk steps to NULL, reads guest VA `0xC` (which holds `0x007696BC`), and cycles between the two forever.
* **The list was never initialised into its empty state.** The initialiser `sub_000A3E70` (three instructions past the stub transcribed in part 129) writes `[ecx+8]=ecx` and `[ecx+0xC]=ecx`; it runs 8 times a run, for `0x006E3378/0x006E33A0/0x006E33C4` and five at `0x01F87xxx`, and never for this object.
* **Tried and kept but did not fix it**: five unregistered methods in the same vtable (`0x0007FCA0`, `0x0007FCD0`, `0x0007FD00`, `0x0007FD30`, `0x0007FFD0`), recovered 3/3 clean. Legitimate recoveries; not the missing constructor.
* **Next**: arm the write-watch from startup (`XBOX_DIAG_WATCH=7696BC:8`) to name whatever last wrote those two words -- or prove nothing ever did.
* 5/5 clean, ~62,365 draws, ~2,707 frames.

### Part 133 -- the uninitialised list is the StartScreens; the watch perturbs the run

* **The write-watch is not a passive instrument.** Arming `XBOX_DIAG_WATCH=7696BC:8` at startup pushed the title into its hard-disk-check *failure* branch (the black "Checking hard disk / A continue B retry" screen). Back-to-back, same build: without the watch frame 600 is 88.6% lit (BASIC CONTROLS); with it, 1.1% lit (the A/B prompt). Page protection + single-stepping every write is heavy enough to change a timing-sensitive path -- same category as part 123s flush-per-label tracing. Arm it late, on a page the boot path does not touch.
* **Whose list it is**: `StartScreen_Create` allocates `0x3E98` bytes (object at `0x007681F0`) and poison-fills it -- that fill is what the watch actually caught at `loc_000AEECB`, not a list init. The spinning walk reads a list at `object+0xF8` where the object is `0x007695C4` = **StartScreen + 0x13D4**, so the list head is StartScreen + 0x14CC = `0x007696BC`.
* **Uncapped** (the earlier count was capped at 8 prints -- nearly a third wrong conclusion from a capped probe): `sub_000A3E70` runs **19 times** a run and **never** for `0x007696BC`; `sub_00083520` runs **twice**, for `0x021C87C0`/`0x021C8DE0`, the freshly built buttons. So the walked list belongs to the StartScreen itself and nothing ever self-links it.
* **Next**: find what constructs StartScreen + 0x13D4. `StartScreen_Create` calls `sub_000C20B0(esi+8)`; the shortlist is the 14 call sites of `sub_000A3E70`, one of which should be reached with `ecx = 0x007696BC` and is not.
* 4/4 clean, ~55,470 draws over 45 s (~62,350 scaled), zero crashes, zero corruption.

### Part 134 -- ROOT CAUSE: guest page 0 is not zero, so a NULL list terminator fails

* **The chain, measured end to end**: `UI_BuildButtonGroup` stops in its 6th of 17 iterations -> the per-object update it calls has a *correctly constructed* `this` -> that update walks a list whose head correctly returns **NULL** (the list is genuinely empty; `sete`/`dec`/`and` is the "first-or-NULL" idiom) -> the end test `sub_000A3890(NULL)` reads **guest VA 0x8 and 0xC**, which on hardware are zero so `0 == 0` ends the loop.
* **In this build they are not zero**: `[END] ecx=0x00000000 next=0x007696BC prev=0x007696BC` -- so the test says "not the end" and the walk cycles between NULL and that value forever.
* Every other link in the chain was disassembled from `default.xbe` and is **byte-faithful** (`sub_000A3900`, `sub_000A3890`, `sub_000A389D`, and the `+0xF8` offsets in both walker and constructor). The defect is only that **guest page 0 holds stale data where a console has zeros**.
* **The writer**: `XBOX_PROTECT_LOWPAGE=1` (in the tree since part 43 for exactly this class) catches it immediately -- `sub_00170215` increments a counter at `this + 0x1A0` with `this == 0`.
* **`sub_00170215` looks like a bad boundary**: zero real `E8` call instructions in all of `.text` target it, and decoding forward from `0x001701BB` puts it as the *last byte* of the 10-byte `mov DWORD PTR [esi+0x400140],0xffffffff` at `0x17020C` -- the next real instruction is `0x170216`. `sub_0016ED57`, which holds its eleven generated call sites, has the same two properties.
* **NOT claimed**: 4,846 registered functions are never a real call target nor a stored pointer, but that is *not* a phantom count -- the lifter splits functions at branch targets, so continuations legitimately have no callers. Needs an alignment test, which is the tool to write next.
* **Two candidate fixes, neither applied yet**: re-lift the `0x0016Exxx`-`0x00170xxx` region excluding those two as function starts; and/or zero + protect page 0 so a NULL read returns zeros as on hardware.

### Part 135 -- two retractions; the page-0 mechanism holds, its cause does not

* **RETRACTION 1**: part 134s "phantom function boundary" was an offset error. `sub_00170215`/`sub_0016ED57` live in the **D3D section** (VA 0x00166F80, raw 0x157000), not `.text` (which ends at 0x00166F80). Part 134 used the `.text` offset formula, so it disassembled unrelated bytes, and its call-target scan covered only `.text`. Redone correctly: `0x00170215` begins `push ebp; mov ebp,esp; push ecx` and has **11 real callers** in D3D. It is an ordinary function.
* **RETRACTION 2**: `case 44: return 12` (HalGetInterruptVector) is not the cause. It counts the `PUSH EDI` at `0x00170471` as a third argument while `sub_00170523` has a matching `POP EDI`, so 8 looked correct -- but with 8 `UI_BuildButtonGroup` still never exits and draws fall to ~48-60k from ~62.3k. Reverted to 12, which has its own recorded evidence (a bogus 128 MB MmAllocateContiguousMemoryEx).
* **Still true and re-verified**: `sub_000A3900` correctly returns NULL for an empty list; `sub_000A3890(NULL)` then reads guest VA 0x8/0xC, which hold `0x007696BC` instead of zero, so the walk never terminates. Every function in the chain is byte-faithful when disassembled from the **correct** section.
* **Measured but unexplained**: `esp` is stable at `0x0423FD88` at every sampled label in the chain, yet the epilogue `sub_00170524` runs 4 bytes high so its `POP esi` reads `ebx`s slot. The gap is between the last sampled label and the epilogue -- needs instruction-granularity sampling.
* **Next lever**: make guest page 0 read as zero (what the console does, and what the terminator test is written against). That does not fix the stray write but stops a NULL read returning garbage, which is the actual failure.
* 4/4 clean, ~62,332 draws, ~2,706 frames; bridge reverted to its committed state.

### Part 136 -- the hang traced end to end: a near-NULL bulk write destroys the TIB

* `UI_BuildButtonGroup` enters once and never exits; inside, `sub_00083288` walks a list whose head correctly returns **NULL** (empty list).
* `sub_000A3890(NULL)` then reads `MEM32(0+8)`/`MEM32(0+0xC)`. **Those do not reach page 0**: `xbox_resolve_uncached_alias` redirects everything below `0x100` to the thread TIB, because the lifter drops the `fs:` prefix and a NULL dereference is indistinguishable from `fs:[x]`. Page 0 is clean (`VA 8 = 0x04140000`).
* **The TIB is destroyed** -- read directly at `0x04110000`: `007696BC 0000013D 007696BC 007696BC 007696BC ...`, one pointer smeared across the whole structure. So the terminator sees non-zero, reports "not the end", and the walk cycles forever.
* **The writer**: `XBOX_DIAG_WATCH=4110008:8` names it in one run (9 hits) -- `sub_00150DCD`, the CRT bulk memory routine (`sub_00150DB0` is the poison fill), called via `sub_001543DE` / `sub_0016F458` / `sub_00172202`. **A memset/memcpy with a near-NULL destination**, redirected wholesale onto the TIB. Same class as the part-43 `_threadstartex` case; it has recurred.
* **Three hypotheses falsified**: "page 0 is corrupted" (it is clean; a watch there caught zero writes, which is what pointed at the redirect); "HalGetInterruptVector arg size is wrong" (8 does fix a real +4 esp error at one epilogue but the hang persists and draws drop to ~48k -- reverted); and an exclusion experiment whose header edit may not have forced a rebuild, so it proved nothing.
* **Next**: walk up from `sub_001543DE` / `sub_0016F458` / `sub_00172202` to find why the destination pointer is near-NULL.
* 3/3 clean, ~62,369 draws; all instrumentation stripped, `recomp_types.h` and `kernel_bridge.c` reverted.

### Part 137 -- FIXED: TIB allocator was writing 64 KB below where the guest reads

* `xbox_tib_alloc_for_thread` used `g_memory_base + (base_va - XBOX_BASE_ADDRESS)` (0x10000) while everything else uses `va + g_memory_offset` where `g_memory_offset = g_memory_base - XBOX_MAP_START` (0). **Every TIB was initialised 64 KB below where the guest reads it** -- the only place in the file using that conversion.
* Measured at the CRT thread-start `fs:[0x28]` read: **before** `fs28=0x00000000` on all four threads; **after** `fs28=0x04110300` (= tib + KTHREAD_OFF). Every guest thread had been running with an all-zero KPCR.
* That is what sent `sub_001543DE` (CRT thread bootstrap) into computing a TLS destination of Xbox VA 4 and `rep movsd`-ing over low memory -- the same shape as the part-43/44 `_threadstartex` bug, from a different cause.
* **4/4 clean, ~62,348 draws -- identical to baseline, no regression.** (Earlier ~48k readings were an unconditional probe printing on every thread start.)
* **It does not clear the hang, and cannot.** `sub_000A3890(NULL)` tests `MEM32(ecx+8) == ecx` with `ecx == 0`; the dropped `fs:` prefix redirects every access below `0x100` to the TIB, and `TIB+8` is `KPCR.StackLimit` -- **legitimately non-zero**. The terminator can never report "end" regardless of TIB cleanliness. This is the aliasing itself, not corruption.
* **The real fix** is preserving the `fs:` prefix through the lifter (translator change + regeneration). A narrower option: the redirect covers `va < 0x100`, but the title only uses a small set of `fs:` offsets (0x00, 0x04, 0x08, 0x18, 0x1C, 0x20, 0x28). If `+8`/`+0xC` are never read via a real `fs:` access, excluding them is two lines -- but that needs evidence; an earlier attempt showed no change and may not have rebuilt.

### Part 138 -- the narrow `fs:` exclusion is not safe (static evidence said it was)

* **Evidence gathered**: located every `0x64` segment-override prefix byte in the image and decoded the instruction at it -- **3,065 candidates** across .text/D3D/D3DX/XGRPH/DSOUND/XPP. The only absolute `fs:` offsets used are `0x00 0x04 0x20 0x24 0x28 0x58` (the KPCR fields); **neither `fs:0x8` nor `fs:0xC` appears anywhere**. The `fs:[reg...]` forms in that output are misaligned-decode noise.
* **Applied with a forced full rebuild** (`touch` on every generated .c -- the part-136 attempt may have run against a stale binary, explaining its "no change" result).
* **It regressed hard**: draws 62,348 -> 1,723; frames 2,707 -> 87; screen BASIC CONTROLS -> black (0.0% lit). Watchdog moved to `loc_000AA244` (Render), so the title takes a different path and dies much earlier. Reverted.
* **What it means**: something reads guest VA 8/0xC *through* the redirect and needs the KPCR values, even though no `fs:`-prefixed instruction reads those offsets -- most likely code that obtains the KPCR pointer once (via `fs:0x18`/`0x1C`/`0x0`) then indexes it with a plain register-relative access, which the redirect serves today because `g_xbox_tib_va + 8` and KPCR+8 are the same address.
* **Conclusion**: the two uses cannot be separated by offset. The distinction must come from the *instruction*, i.e. preserving the `fs:` prefix through the lifter -- a translator change plus full regeneration.
* 4/4 clean, ~62,310 draws; part-137 TIB fix kept, `recomp_types.h` reverted.

### Part 139 -- the `fs:` sites cannot be identified from the generated C

* **The shortcut tried**: only ~32 real `fs:` accesses exist in the image, so instead of a lifter change, add explicit `FS8/FS16/FS32(off)` macros, rewrite those sites, and delete the blanket `va < 0x100` redirect.
* **Round 1 (19 sites)**: absolute forms `MEM32(0x28)`x7, `MEM8(0x24)`x6, `MEM32(0x20)`x3, `MEM32(0x58)`x2, `MEM32(0x2C)`x1 -- counts matching the fs: scan offset-for-offset. Rewrote them, removed the redirect, forced a full rebuild: **draws 62,348 -> 1,744, screen black** -- the same regression as part 138.
* **Round 2 (the missing twelve)**: the scan showed `fs:0x0`/`fs:0x4` in use but no `MEM*(0x0)`/`MEM*(0x4)` existed -- the lifter emits those as **bare decimals**, `MEM32(0)`x6 and `MEM32(4)`x6, bringing the total to 31 (~matching the scan). Rewriting them too made the title **crash outright** (exit 139).
* **Conclusion**: `MEM32(0)` is ambiguous by construction -- the lifter emits it both for `mov eax, fs:0x0` and for a genuine null read. Rewriting none leaves the screen black; rewriting all crashes. **The fs: site set cannot be recovered from the generated C**; it has to come from the disassembler, which still holds the `0x64` prefix when each memory operand is emitted. This rules out any call-site shortcut around the lifter change.
* Everything reverted (`recomp_types.h` + whole `gen/` tree from backup). 4/4 clean, ~62,353 draws; the part-137 TIB allocator fix remains.

### Part 140 -- the 31 `fs:` sites identified exactly; the redirect still cannot be removed

* **Authoritative site list obtained** from Capstone `mem.segment` (the lifter drops the prefix but Capstone keeps it). Decoding at every `0x64` byte gives 1,412 raw matches, almost all noise; filtering to absolute forms (no base/index, small disp) leaves exactly **31**, matching the 19+12 counted in the generated C: `fs:[0x00]`x7 `[0x04]`x6 `[0x20]`x3 `[0x24]`x6 `[0x28]`x7 `[0x58]`x2, with exact addresses. The `fs:[0]` cluster at `0x0015DEC1`-`0x0015DFB4` is the SEH prolog/epilog.
* **Mapped to owners** via the `Original: 0xA - 0xB` header comments: 30 of 31 land in **19 functions**. Added `FS8/FS16/FS32` macros and rewrote those functions **by line span only** -- the fix for part 139s failure, where a global substitution also caught genuine null reads.
* **Removing the redirect entirely** -> immediate crash in `sub_00172202` (recomp_0009.c:13206), a `MEM32(edi)=0` fill loop in D3D that is not one of the rewritten functions. The redirect has been absorbing **latent null-pointer writes elsewhere in the title**.
* **Keeping the redirect but excluding offsets 8/0xC** (with FS macros in place) -> no crash, but draws 62,372 -> 1,597 and the screen goes black. So a **non-`fs:`-prefixed** access reads VA 8/0xC and needs KPCR data there.
* **Next step**: the scan showed `fs:[reg+disp]` forms that were dismissed as decode noise. Validate each against a real instruction boundary (disassemble forward from the enclosing function start, not from the `0x64` byte); if any are real they need the same treatment before the redirect can be narrowed.
* Everything reverted; 4/4 clean, ~62,372 draws. The part-137 TIB allocator fix remains.

### Part 141 -- LANDED: the lifter now preserves `fs:` (redirect still cannot be narrowed)

* **The translator change is done and kept**: `Operand.mem_segment` captured from Capstone in `tools/recomp/disasm.py`; `_mem_accessor(size, segment)` in `tools/recomp/lifter.py` emits `FS8/FS16/FS32` for `fs:` operands; `FS*` macros in `recomp_types.h` resolving against `g_xbox_tib_va`.
* **25 functions re-lifted and spliced** -- found by decoding each function from its *real* start and keeping those with a genuine `fs:` operand (39 sites, 25 functions -- six more than part 140s absolute-only filter). Linker-driven closure pulled in 9 more over six rounds.
* **`main.c` zeroes guest VA 0x0-0xFF** at startup; that region held pre-per-thread-TIB leftovers (`VA 0 = 0xDEADBEEF`, `VA 4` a stack pointer, `VA 8` a TIB address) with no legitimate reader left.
* **4/4 clean, ~62,320 draws -- identical to baseline.** All of it is infrastructure any future fix needs and none of it costs anything.
* **Still broken**: excluding offsets 8/0xC from the redirect collapses draws to ~1,597 even now. Bisected cleanly -- re-lifts + full redirect = 62,320 (fine); re-lifts + exclusion = 1,597; + zeroing page 0 = 1,597. So neither the re-lifts nor page-0 contents are at fault; the exclusion itself is.
* **Remaining hypothesis**: the redirect is keyed on the address, so it also serves accesses that compute a low address by accident -- a legitimately-NULL struct pointer plus a small field offset, whose reader has been silently depending on getting KPCR data. **Next step**: log the caller of every `va == 8 || va == 0xC` resolution over a run and resolve them -- the address-level equivalent of what the thread sampler did for control flow.

### Part 142 -- ANSWER: the redirect is masking a family of NULL-dereference bugs

* **Instrumented the resolver** (`XBOX_LOWACC_LOG=1`) to record the return address of every VA 8/0xC resolution, printed on first sight (the process is killed by a timeout so `atexit` never runs). One run gave **13 distinct readers**: `sub_00163493` x3 (CRT teardown), `sub_0016F458` x2 (D3D), `sub_00083288` x2 (the list walker), `sub_000841F0`, `sub_000AF7B0`, `UI_BuildButtonGroup`.
* **Neither `sub_00163493` nor `sub_0016F458` contains any `fs:` instruction** -- disassembled both from their real starts. They are **genuine NULL dereferences**. `sub_00163493` literally tests `esi` for NULL, branches away if zero, and still reaches `mov eax,[esi+0x24]` with `esi == 0`. On hardware that faults; here the redirect hands it TIB fields and it carries on.
* **So the redirect is one switch doing two things**: it *breaks* the list terminator (KPCR+8 is legitimately non-zero) and it *masks* at least five other NULL dereferences. Narrowing it fixes the first and unmasks the rest simultaneously -- exactly what parts 138, 140 and 141 each measured from a different angle. The `fs:` work was necessary but never sufficient.
* **The work queue is now concrete and short**: fix the NULL dereferences in `sub_00163493` (3 sites), `sub_0016F458` (2), `sub_000841F0`, `sub_000AF7B0` -- each an ordinary "why is this pointer NULL" question. `sub_00083288`/`UI_BuildButtonGroup` are victims, not causes. Only then can the redirect be narrowed.
* `XBOX_LOWACC_LOG` instrumentation kept -- env-gated, one compare on a near-never-taken path, and it is the tool for that queue.
* 4/4 clean, ~62,394 draws, ~2,707 frames.

### Part 143 -- FIXED: the XBE TLS index was never applied; intro completes 12/12

* **Root cause, traced end to end**: `sub_00163493` dereferences a pointer of `8`, from `edi = tls_array[index]; edi += 8` where `tls_array[index]` read zero. `tls_array` is `fs:[4]` -- and **all six `fs:[4]` reads in the image use it as a TLS array base**, while our KPCR wrote `stack_base` there. Fixed (`XBOX_KPCR_TLS_ARRAY = 0x04` -> `tls_va`; the old STACK_BASE was written but never read).
* **Still zero**, because the TLS index at `ds:0x2016D8` was `0xFFFFFFFB`. That displacement appears exactly six times in the image and **every one is a read** -- nothing in the title writes it. It is the **loaders** job: the XBE header `TlsAddress` (+0x12C, *not* +0x118 = CertificateAddress) points at a TLS directory whose `AddressOfIndex` is exactly `0x002016D8`. We never processed it.
* **Writing it at load does not stick** -- a write-watch found exactly one writer, `xbe_entry_point`, whose `.data` init copies the compiled-in `0xFFFFFFFB` back. So it is applied on the **first bridged kernel call**, latched only once it actually corrects the value.
* **Why it broke everything**: with index `-5`, every CRT TLS accessor did `[fs:4 + (-5)*4]` -> 0, then `[0 + 4] = value` -- a write to Xbox VA 4, landed on the **TIB** by the low-address redirect. That smeared the KPCR, corrupted `fs:[4]`/`fs:[8]`, and made `sub_000A3890` never report end-of-list.
* **RESULT: intro items 0 -> 12/12 every run, in 0.32 s** (previously never completed). `UI_BuildButtonGroup` no longer hangs. Five never-before-reached ICALL targets appeared at once; the recovery loop is productive again after ten stalled parts.
* Draws fell 62,300 -> ~7,500 because the title is now *past* the loading screen running new code; recovering three newly-exposed functions took it to ~9,700. The old 62,300 was one screen redrawn forever -- the gate floor needs re-baselining.

### Part 144 -- the hang is gone: intro completes, screen transitions, main loop healthy

* **`TitleIntroSequence_Tick`/`_Render` now run** -- they had never executed in any prior part. The tick gate `[obj->vt+0x38]` returns 1 every call.
* **`sub_0007BF50` recovered**: the boot-video items per-frame tick, `[item->vt+0x6C]` on vtable `0x001965E8`, previously unresolved so no video item ever ticked. Found via `sub_0007CBF3` -> `loc_0007CDE5`. Draws ~9,200 -> ~9,800.
* Also recovered `0x0004FA00`, `0x0007FD90`, `0x000A3830` (first wave exposed by the TLS fix). A second wave of six was auto-reverted by the gate for lowering draws -- correctly.
* **MAIN LOOP HEALTHY: 5,152 iterations at ~117/s.** For ten parts this counter stopped at 269 and never moved. A thread sample shows the main thread in `bridge_NtWaitForSingleObjectEx` -- an ordinary kernel wait, not a spin. So the intro ticking six times is completion, not a hang.
* **On screen**: the BASIC CONTROLS screen at mean brightness 26.0 (was 120.8) -- the same screen **fading out**. The exit transition, never previously reached. Then black.
* **Measured**: 4/4 clean; intro 12/12 every run (was 0); frames ~2,478 (~2,706 before); draws ~9,779. Draw count is not comparable across the fix -- the old 62,300 was one screen redrawn forever at high per-frame cost. Frames per run are essentially unchanged, which is the honest comparison.
* **Still open**: `VideoPlayer_Open` has never run, so the EA logo/opening videos still do not play. Unresolved: `0x00080FE0`, `0x00084B10`, `0x000A3730`, `0x000A3D60`, `0x000A5200`, plus recorded traps and the XPP pair.

## Part 145 (2026-09-03)

The EA logo video now opens: `VideoPlayer_Open("data/video/eabig.mpc")` runs and
the title reads `D:\data\video\eabig.mpc` on every run. The blocker was
`sub_000A3730`, the video widget's own tick (vtable 0x001965E8 slot +0x44) --
undetected, so the player was never constructed. The videos are MPEG-1 in an
`MPCh` container and the decoder is compiled into the title.

Separately: `recomp_lookup` binary-searches a table that was never sorted, so 40
registered functions were unreachable -- including `sub_00179411`, previously
mis-recorded as an XPP gap needing a host bridge. Now sorted by the generator,
again at startup, with `XBOX_DISPATCH_DENY` to bisect. 38 of the 40 are fine;
the audio thread (0x000151F0) and one DSOUND function (0x00179411) are excluded
with recorded evidence.

Three tool defects fixed: seeded addresses the linear sweep desynchronised past
were dropped silently (`resync_at`), `_find_function_end` cut functions at the
first `ret` and split FPO frames, and `xbrun.py` would happily measure a stale
binary.

State: 3/3 clean, ~7,560 draws, zero crashes. On screen still black -- the intro
opens but does not play.

**Addendum (part 145):** MMX `movq` was never implemented -- 100 dropped sites,
45 of them in the five functions that do the video colour conversion, including
the store that writes the pixels. Implemented, and `tools/audit/relift.py` added
to re-lift functions that already have bodies (recover_batch only handles new
ones, so a lifter fix changed nothing in the build until now). 3/3 clean,
~7,502 draws. Frame capture: the BASIC CONTROLS screen draws correctly at
present 296 -- the earlier "black" reading was my sampling interval, not the
truth. It appears briefly, then the screen goes dark entering the video state.

## Part 146 (2026-09-03)

The memory jump was real and worse than reported: working set reached 4.1 GB,
climbing 215 MB/s. Cause: `sub_0016BCE0` -- the helper LockRect uses to compute
the back buffer pitch and base -- was undetected, so LockRect returned stack
garbage (pitch 0x40, pBits 0) and the video blit wrote 29.6 MB per call into
whatever the RAM mirrors covered. Recovered: pitch 0x0A00, one 2304-byte row per
call, peak working set 148 MB.

Second bug it was hiding: Xbox `0xF0000000` write-combined alias was not handled
in `xbox_resolve_uncached_alias`, so the back-buffer pointer resolved ~62 MB off.

Newly exposed: the MPEG decoder now reports "Invalid motion_vector code" and
bugchecks, so the run exits at ~12 s instead of 55 s. Not caused by the part-145
MMX work. `KeBugCheck` now prints a backtrace -- it used to leave nothing.

New tool: `tools/audit/memwatch.py` (working set / private bytes / address-space
walk, lined up with the timestamped log).

**Addendum (part 146):** the `KeBugCheck(0x0A)` was not the MPEG decoder -- it
was the CRT`s _getptd reading `fs:[0x24]` (KPCR Irql) as 188 and refusing to run.
Three causes: the `KfRaiseIrql`/`KfLowerIrql` bridges read the stack for a
__fastcall argument that arrives in `ecx`; nothing ever published the tracked
IRQL into the KPCR byte the guest actually reads; and the null-pointer redirect
pointed at the calling thread`s own KPCR, so a stray null write landed on it.
All three fixed, plus a dedicated XBOX_NULL_PAGE_VA for null accesses.

Result: 3/3 clean, **draws 7,243 -> 69,212**, clears 310 -> 3,005, zero
bugchecks, zero IRQL warnings, and the BASIC CONTROLS screen renders **in full
colour** (mean 120.5, 87.8% lit, 75.3% chromatic) -- previously a 0.2%-chromatic
ghost for one frame.

Next: the title holds on that screen. Per-call-site ICALL accounting (new) names
`recomp_0002.c:60226` in `UI_BuildButtonGroup` with 134,768,796 NULL calls -- its
list walk cycles between VA 0 and 0x007696BC because a null write left that value
at VA 0. Writer identified: `sub_00172202` via `sub_0016FE73`, zeroing a block
through a null D3D device pointer.

**Boot screens identified (part 146):** the brief white-on-black screens are the
title`s own "Checking hard disk" / "Autoloading from hard disk", on the success
path, reading the save from the emulated HDD. Sequence: HDD check 0.07-1.4s,
autoload 1.4-3.6s, HDD check 3.6-4.9s, loading screen fades in from 5.0s.
Added `XBOX_D3D_DUMP_FROM` so a brief screen several seconds in can be captured
without dumping every frame from zero.

**The one-frame splash anomaly (part 146):** not the hard-disk screens (those run
before the splash -- my first answer was wrong and the user corrected it). At
present 1496 exactly one frame draws the **font atlas full-screen** instead of
the background; thereafter a fully black frame recurs every 249 presents
(4.15 s). Found with a new `XBOX_D3D_DUMP_ONCHANGE=1` (hash the back buffer,
dump only on change): 3,000 presents -> 79 distinct frames.

**Save-missing test (part 146):** moved the save aside and back (restored,
checksums verified). The title copes correctly -- "Checking hard disk" without
"Autoloading", loading screen ~2 s sooner, no error. It exposed a real bug:
`NtQueryDirectoryFile` was returning "." and ".." (the Xbox kernel does not), so
the title treated them as save folders. Fixed in both the Win32 and POSIX paths.

## Part 147 (2026-09-03)

The `UI_BuildButtonGroup` spin was **two swapped pushes** in a block I had
hand-transcribed in an earlier part. `loc_000AFBEE` should be
`memset(esi+0x14CC, 0, 0x29A8)`; it was written as `memset(NULL, esi+0x14CC,
0x29A8)`, so instead of clearing a 10,664-byte buffer it wrote that buffer`s
address over 10,664 bytes starting at guest VA 0. Every null-terminated list
walk then read 0x007696BC where it expected zero.

Found by dumping the null page added in part 146 (the whole 256-byte window held
that one value) and putting a write-watch on its tail.

**NULL indirect calls 134,768,796 -> 1.** The null page is all zeros. 3/3 clean,
draws ~68,938. The title now loads the entire frontend -- ssxfe.big, fe_1.xsh,
hud.xsh, splash.xsh, the fonts, the .loc files, character models, audio banks --
then goes idle waiting, not spinning. Screen is still the loading screen: the
assets are in, the frontend does not draw yet.

Lesson: second time a hand-transcribed block has been wrong in a way that looked
like a translation bug for parts. `xverify.py --function` diffs a body against
the XBE and would have caught it.

**Frontend blocker located (part 147):** app state reaches 2 (FrontEnd),
`app->0x730` holds the intro object, `FEInit_Boot` runs once -- and then
`TitleIntroSequence_Tick`/`_Render`/`_IsComplete`/`_CheckLoaderReady` are
**never called**. The object loads every asset and is then never driven.

**New: `xverify.py --range START-END`** compares push order between the XBE and
the generated block, for hand-edited code that has no `Original:` comment and so
cannot be audited by `--function` -- which is exactly why part 147`s bug
survived. Swept all seven hand-written blocks: the rest are clean.

## Part 148 (2026-09-03)

**The build only worked with an environment variable I was setting.** The two
known-defective addresses were excluded via `XBOX_DISPATCH_DENY` in every
measurement since part 145; without it drawing collapses to zero, which is what
the user saw when launching the .exe. Now denied **by default** in
`recomp_dispatch_init()`, with `XBOX_DISPATCH_ALLOW` to re-enable for testing.
Verified with no environment at all: 3/3 clean, draws 68,911, loading screen in
full colour. Audited every other env var -- all diagnostics, all off by default.

## Part 149 (2026-09-03)

Checked my own RE notes before instrumenting, and they reframed the search:
`Application_RunMainLoop` (0x000AA1A0) is **misnamed** -- `RE_NOTES_control_scheme.md`
already identifies it as `Application_RunInitialLoadPump`, a one-time boot pump.
The real per-frame driver is `Application_FrameTimerCallback` -> `Application_TickFrame`,
and **that chain is alive and firing**.

`TickFrame` tail-jumps to `[app->0x2C]->vt[0x10]` = **`XBoxExecutionMan_SignalFrameEvent`**
-- correcting a standing finding that said it was "called by nothing".

**Why the video is never queued:** `QueueBootVideos` is vtable slot +0x10 of the
intro object, and the only caller is the load pump, once. Probed: at the earlier
call site `app->4` is the intro object (0x006E3300, FEInit_Boot runs on it); by
the later one it has become 0x007681F0. `app->4` is reassigned inside
FEInit_Boot`s own 22-fragment chain, so the pump calls slot +0x10 on the wrong
object and nothing ever queues the videos. `QueueBootVideos`, `sub_0007CA83` and
`sub_0007CB64` all measure **zero** calls.

**Cxbx-Reloaded** (local copy in `reference/`): its `timeSetEvent` patch is
commented out -- the title`s own XAPI timer runs natively, matching what we see.
Its `hle_vblank()` bumps the counters the title reads rather than delivering an
interrupt; the vblank-callback thread is still a TODO. Useful shape if the frame
event turns out to matter.

State: 2/2 clean with no environment set, draws ~68,952.

## Part 150 (2026-09-03)

**Mapped the top-level state loop.** `Application_RunAndShutdown` is three lines:
`app->4 = Application_StateMachineTick()` / `Application_RunInitialLoadPump()` /
`Application_Purge()` -- the tick *returns* the next state object, the pump drives
it to completion, repeat.

**The pump is entered once and never returns.** Probed: the tick site fires twice,
the pump site once, and the two sites after it never fire. So the outer loop never
comes back round.

**Inside the pump `app->4` is always the boot/autoload state** (0x007681F0, vtable
0x0019A744, render = `sub_000AF7B0`) -- a guarded probe for any other value never
fired in 35 s. The intro object is never the current state while the pump runs,
which is why `TitleIntroSequence_Render`/`_Tick`/`QueueBootVideos` measure zero
calls and `eabig.mpc` is never queued. Part 149`s reason (reassignment inside
`FEInit_Boot`) was wrong; this is the real one.

The pump does reach its exit path (0xAA2A3 fires once) and stalls somewhere in the
~20-instruction tail after `SceneRenderer_SelectDetailLevel`.

Also corrected: `InputManager_PollDevicesIntoCache` **does** return non-zero --
the ring cursors advance 0/1, 1/2, 2/3, 3/4, 4/5. The older note blaming a
permanently-zero poll is out of date.

## Part 151 (2026-09-03)

**Correction to part 150:** `Application_RunInitialLoadPump` *is* the outer state
loop -- it calls `Application_StateMachineTick` itself at 0xAA2E6, installs the
result in `app->4`, and jumps back to its own top. It is entered once by design.

**Where it stops:** on iteration 2, with `app->4` = the intro object, the pump`s
very first call `[app->4]->vt[0x04]` = `FEInit_Boot` **never returns**. So the
videos are never queued because the pump never gets past the intro state`s init.

**One call deep, named by the watchdog.** New diag command **`loc`** samples
`g_main_loc`/`g_last_loc` 40 times and says whether they move -- one query, no
rebuild, instead of a rebuild per bisection step. It reported:

    main_loc  loc_0009998B   (static)
    last_loc  range loc_00082D50..loc_000A3900   32 of 40 moved -- running

So `sub_00099867` calls **`sub_00087350`** at 0x0009998D and never returns, and
inside it the widget list walk `sub_00082D50` -> `sub_000A3890`/`sub_000A3900`
spins forever. `sub_000A3890` is the circular-list terminator; some list it walks
is not terminating.

Ruled out: frame-pointer propagation across all 22 FEInit_Boot fragments (clean),
`UI_BuildButtonGroup` (returns fine), the input poll (returns non-zero).

State: 2/2 clean with no env set, draws ~69,005.

## Part 152 (2026-09-03)

**The hang has a mechanism now.** `sub_000841F0` calls `sub_000A3900`
("first child, or NULL if empty") and the **very next instruction dereferences
the result** without a null check -- `mov cl, [eax+0x91]`. On real hardware that
is a page fault; the game never expects the list to be empty. Our null-page
redirect turns it into an endless walk over node 0 instead, which is why this
looked like a hang and not a crash.

So the real defect is upstream: the title screen widget`s child list at `+0xF8`
is **empty** when `sub_00087350` runs.

Verified the watchdog inference first: none of the walk functions stamp
`g_main_loc`, so `main_loc` frozen at `loc_0009998B` genuinely means parked
inside `sub_00087350`.

`XBOX_PROTECT_LOWPAGE=1` faults earlier, in `sub_0016ED57` (the null D3D device
pointer from part 146), so it cannot isolate this one yet.

Terminology corrected (user): `eabig.mpc` is the **animated EA ident video**,
chained into `ssxintro.mpc`, the opening cinematic -- two videos, not a logo.

State: 2/2 clean with no env set, draws ~68,995.

## Part 153 (2026-09-03) -- full check-up

**Runtime gaps essentially closed:** 0 undetected stubs executed, 5 unresolved
indirect targets (all known/denied), 0 crashes, 0 .text corruption, 2/2 clean.

**Three real translation classes remain** (excluding the by-design cmp/test
deferral, which was verified correct for `setcc` consumers this part):
  * `pand` 17 / `por` 10 -- all in the **video colour-conversion functions**
    (sub_00149450/500/5E0/670). MMX movq was implemented in part 145; the masks
    and ors between the moves are still dropped, so decoded frames would have
    wrong colour even once decode works.
  * `fnstsw` 19 across 9 functions (tail of a class that was 2,074).
  * `sahf` 9, `std` 2, `repe` 1, `fld`/`fstp` 2 (sub_0017A965).

**Why it is not working, in two sentences:** the title screen`s widget layout
walks a child list that is **empty**, and the game`s own code dereferences the
empty-list result with no null check -- a case that cannot happen on hardware,
so something upstream failed to attach children to that widget. The null-page
redirect absorbs the NULL instead of faulting, so it surfaces as an endless walk
inside `FEInit_Boot`, which never returns, so the pump never reaches the state
that queues the two intro videos.

Measured at the site: list head 0x021C8EEC with `[head+0xC] == head` (empty), and
`sub_000A3900` verified faithful. Everything else on the path -- frame driver,
state machine, input poll, asset loading, widget construction -- measures working.

Detail worth keeping: at that head `next == head` but `prev == head - 0x10`. A
freshly initialised empty circular list should have both pointing at the head, so
the asymmetry may be the tell.

## Part 154 (2026-09-03)

**The hang is fully characterised.** The title screen`s `"f3wlst"` list widget
(0x021C87C0) is created by `UI_BuildListWidget_Alt` with wrap-around enabled
(`+0x138 = 1`) and **never has any items added** -- a guarded probe on
`sub_000A3BB0(ecx == 0x021C87C0)` never fires, and its list head points at itself.
A widget with wrap on and no children makes `sub_00082D50` loop by construction:
end-of-list -> wrap -> first child -> 0 -> end-of-list -> forever. Not a
translation defect; the state is wrong.

That walk is inside `sub_00087350` -> `FEInit_Boot`, which therefore never
returns, so the pump never reaches the state that queues the two intro videos.

`UI_BuildButtonGroup` (0x00086170) runs on the screen, `[screen+0x34]` is exactly
that list widget, and it attaches nothing.

**Naming correction:** earlier parts called `sub_00083288` "UI_BuildButtonGroup".
The real one is 0x00086170; the 134M-NULL-call site in part 146 was sub_00083288.

---

## Part 156 -- the blocker was a trap I set myself

### The finding

Every widget's child list in the game was empty. Not the title screen's list
widget specifically -- **all of them**. Dumping the two title-screen widgets
side by side showed both with identical, correctly-initialised, *empty* child
lists; the only difference between the "broken" one and the "healthy" one was
the wrap byte at +0x138. Wrap on means an empty list walks forever; wrap off
means it returns immediately. The wrap flag was never the bug, it was the
detector.

The reason nothing was ever attached:

    0x00082D30    add ecx, 0xf8
                  jmp 0xa39e0

Two instructions. It is vtable slot **+0xA0 -- AddChild -- for every widget
class in the game**, and it was **not translated**, because I recorded it as a
trap in `findings.json` back in part 127:

    "summary":  "causes infinite mutual recursion 0xA3A60 -> 0xA38F0 -> 0xA38F0 -> 0x85820"
    "evidence": "a real widget-tree defect, not a translation bug"
    "action":   "fix the widget tree before enabling"

That verdict was wrong in every part. The recursion came from *sibling*
functions the recovery batch pulled in alongside it, not from this body; the
"widget-tree defect" it blamed was the emptiness that trapping it had itself
caused; and part 153's check-up listed 0x00082D30 among five unresolved targets
with the line **"nothing here is load-bearing."**

### Correcting it

`sub_000A39E0`, the insert it tail-jumps into, was already translated and
correct (`[container+0x1C]` is an append cursor). So the fix was to recover the
two instructions -- but in the right order:

1. **List/visitor helpers first** (0x000A3850, 0x00082D20, 0x00085850, +1).
   Currently unreachable, so this should have been a no-op. It was not:
   **draws 68,947 -> 89,532, clean 3/3.** Kept.
2. **AddChild.** Gate failed twice (2 of 3 runs crashing), so it went in with
   `--keep-going` to diagnose rather than being abandoned again.
3. **The functions that only become reachable once the tree is populated** --
   0x0007D400, 0x0007D450, 0x000FA570, 0x00084B10, 0x00080FE0, 0x000A3D60,
   0x000A5200. These had never been lifted because nothing had ever called
   them. With them in: **3/3 clean, no crashes.**

Confirmed populated afterwards, live: `[widget+0x108]` (the container's
head.next) now reads a real node instead of pointing at the tail sentinel.

### Where it stops now, and why that is progress

`FEInit_Boot` **returns for the first time**. The load pump gets past it and
into its wait loop at `loc_000AA205` -- and spins there, 244 million indirect
calls to address 0.

Probed at `loc_000AA22C`: **`esi = 0`**. The pump reads `[esi+4]` from the
guest null page (which holds garbage, not zeroes), gets 4, reads a "vtable"
at 4, and calls slot +8 = 0, forever. The application object and its vtable
0x0019A744 are both intact at runtime -- dumped and byte-identical to the image.

An entry probe guarded on `ecx == 0` **never fires**, so `esi` is not passed in
wrong: it is clobbered *inside* the loop. Bisecting the loop's labels with
`--when 'esi == 0'` narrows it to one call -- the indirect call at
`loc_000AA244` through `vt[0x14]`, which resolves live to **`sub_000AF7B0`**
(the autoload state's Render). `esi` is good at `loc_000AA240` and zero at
`loc_000AA24C`.

Two candidate explanations were checked and **both ruled out**:

  * *Not an ICALL miss.* On a miss `RECOMP_ICALL_SAFE` finds no `_fn` and calls
    nothing, so it cannot clobber a register. It only sets `g_esp` and `eax`.
  * *Not a broken epilogue.* All 34 exits of `sub_000AF7B0` pop `esi`, and
    probing the epilogue at `loc_000AF8F9` shows the stack perfectly balanced:
    entry `esp = 0x0423FF14`, epilogue `esp = 0x0423D904`, and
    `MEM32(esp + 12) = 0x0031A440` -- the correct saved `esi`, in the right slot.
    Its `__chkstk` (0x0015CDF0, including the awkward `xchg esp, eax`) is also a
    faithful translation and touches only `eax`/`esp`.

So the first calls through `vt[0x14]` return cleanly and a **later** one does
not. The corruption is real but intermittent, which fits a path through one of
the conditional `push ebx/ebp/edi` branches whose matching epilogue disagrees --
that is the specific thing to catch next.

Draws fell 89,532 -> 7,701 with AddChild in. That is not a regression in the
useful sense: the pump is now spinning on the null page instead of re-rendering
a stalled loading screen. It is further along and drawing less.

### State

3/3 clean, no crashes, no .text corruption, no probes in the tree.
Ledger: 0x00082D30 moved trap -> resolved; 0x000AA1A0 recorded.

### Next

Kill the remaining ICALL misses on the pump's loop (0x00179411 x249 at
recomp_0009.c:17240 is the loudest), or make `ICALL_SAFE` preserve
ebx/ebp/esi/edi across a miss. That is what is between here and the pump
completing -- and the boot videos are queued immediately after it.

---

## Part 157 -- taking the rebuild out of the probe loop

Not a bug hunt. The question was whether this process can go faster, so the
first thing was to measure where the time actually goes rather than guess.

    no-op rebuild                        1 s
    rebuild after touching one gen file  52 s
    full rebuild, all 14 gen files      124 s
    one run                             ~35 s
    => one probe cycle                  ~90 s
    => a 3-run gate                    ~170 s

Part 156 spent eleven probe cycles. Almost all of that wall time was rebuilds,
and worse, each rebuild had to be paid *before* knowing whether the site was
the right one to instrument.

### Runtime-armable probes

Every generated label now emits `RECOMP_LOC(0xADDR)`, which is a single load of
a global and a branch the predictor always takes. When a probe is armed, the
address is looked up in a 4096-slot open-addressed hash and, on a match,
evaluated and printed.

  * `ssx_recomp/src/recomp/recomp_probe.{h,c}` -- table, hash, a small
    recursive-descent evaluator, and the arming parser.
  * `xboxrecomp/tools/audit/instrument_labels.py` -- idempotent pass that puts
    `RECOMP_LOC()` on every label. **69,683 labels** across 14 files.
  * `recover_batch.py` and `relift.py` call it after every splice, so a newly
    recovered body is never invisible to probes.
  * `XBOX_PROBE` arms at startup; the diag server's new `probe` command arms
    **mid-run, without even restarting**.

Spec: `ADDR[|WHEN][|SHOW,SHOW][|LIMIT]`, `;`-separated, no spaces. The
expression grammar is `EXPR := TERM (('+'|'-') TERM)*` and
`TERM := reg | 0xhex | decimal | '[' EXPR ']'`, so memory chains nest:
`[[[esi+4]]+0x14]` evaluates correctly.

**Cost when nothing is armed: none measurable.** 3/3 clean at 7,780 draws
median against a 7,730 baseline, inside ordinary run-to-run variance.

### Proof it works

The whole `esi` bisection from part 156 -- which cost three build cycles --
re-run as a single 35 s invocation with nine probes:

    [PROBE] 0x000AA244 esi=0x0031A440 [esi+4]=0x007681F0 [[esi+4]]=0x0019A744
                       [[[esi+4]]+0x14]=0x000AF7B0
    [PROBE] 0x000AA24C esi=0x00000000
    [PROBE] 0x000AA25E esi=0x00000000
    ...

Same conclusion, one run. And arming mid-run over the diag server works:

    probe 0xAA205||esi,eax,[esi+4]|3;0xAA276||esi,eax|2
    -> armed 2
    [PROBE] 0x000AA205 esi=0x00000000 eax=0x00000000 [esi+4]=0x00000004
    [PROBE] 0x000AA276 esi=0x00000000 eax=0x00000000

That last pair also confirms the spin path branches *around* `0xAA244`, which
is why a probe there reported zero hits while the loop was clearly running.

**Limitation, deliberately documented rather than hidden:** `ebp` is a C local
in every generated function, so the evaluator maps `ebp` onto `g_seh_ebp` --
right for functions that publish it, stale for those that do not. `xprobe.py`
stays for that, for floats, strings, backtraces, and arbitrary C conditions;
its docstring now says to try the runtime path first.

### Parallel runs

Three instances finished in **35 s wall against ~120 s serial**, no crashes and
no interference once `XBOX_KERNEL_LOG` is per-run. But draws came back
62,275 / 7,659 / 62,508, so contention perturbs the frame counter badly:
parallelism is sound for crash detection and for intermittent-bug hunting, and
must **not** be used for the draw gate.

That spread is itself a finding: same binary, same build. **The `esi`
corruption is intermittent**, which reframes part 156's hunt -- it points at a
second thread rather than one exit path with a mismatched pop count, and it
explains the earlier 115,427-draw outlier.

### Also noticed

`recomp_recovered.c` has 5 implicit-declaration warnings (`sub_000B2910`,
`sub_0012A4B0`, `sub_000B2810`, `sub_0014D070`, `sub_0012A690`). Pre-existing --
earlier builds were grepped with a filter that only showed errors. They link,
so the functions exist and only the declarations are missing, but this is
exactly the class that hides pointer truncation on Win64. Recorded, not yet
fixed.

### State

3/3 clean, no probes in the tree, draws median 7,780.

### Part 157b -- what the new tooling found in its first hour

Using runtime probes plus parallel runs together, with no rebuilds:

**The intro sequence is running.** `app->4`'s `vt[0x14]` normally resolves to
`0x000AF7B0` (the autoload state's Render), but once -- and only once -- it
resolves to **`0x0007CE10`**, and a probe there catches
`ecx = 0x006E3300`: **the intro object**. So the state machine *does* reach the
intro sequence; part 156's "the pump never advances" is now out of date.

**`sub_000AF7B0` is exonerated.** All 24 of its exit labels were probed in a
single run: every one shows `esp = 0x0423D904` and `[esp+12] = 0x0031A440` --
the correct saved `esi`, in the right slot. And 8 consecutive iterations of the
pump loop show `esp = 0x0423FF18` and `esi = 0x0031A440` before *and* after the
call. Part 156's suspicion of a mismatched pop count there was wrong.

**The corruption is on the intro path.** Tracing `esp` through
`TitleIntroSequence_Render`:

    0x0007CE10  esp=0x0423FF14  ecx=0x006E3300   entry
    0x0007CE26  esp=0x0423FF04                   after its 4 pushes -- correct
    0x0007CE31  esp=0x0423FF04  eax=2
    0x0007CF76  esp=0x0423FF04                   still correct

`sub_0007CF76` (the continuation fragment) has a complete, balanced epilogue --
pops ebp/edi/esi/ebx/ecx then `esp += 4`. So the loss happens **inside one of
its callees**: `sub_00082AE0`, `sub_000844C0`, `NodeRegistry_TickAllOfType`,
`sub_00082A50`, `sub_000A3920`, or the ICALL through `[edx+0x34]`.

**Intermittency confirmed.** Three parallel runs with the same binary: the
`esi != 0x31A440` guard fired in runs 1 and 3 and never in run 2.

**Two more stale traps cleared.** `0x00087350` was trapped as "never returns" --
it only spun because AddChild was trapped, so it is resolved now. And
`0x0007CE10` was never unregistered at all: it is `TitleIntroSequence_Render`,
and my `sub_XXXXXXXX` grep missed it because it has a real name. That is the
second time this session a rename hid a function from a grep -- **check the
dispatch table, not the `sub_` spelling.**

Recovered on the newly reachable path: `0x00060410`, `0x0007FF90`,
`0x00080D60`, `0x00080EF0`. 3/3 clean.

### Part 157c -- the chain runs all the way to the video, and stops in the pre-roll

Five probe runs, no rebuilds. Each one armed every label of the next function
along, so the trace advanced a whole function per run instead of a statement.

**The full path, measured end to end:**

    Application_RunMainLoop  vt[0x14] -> 0x0007CE10   (once, not 0x000AF7B0)
      TitleIntroSequence_Render      ecx = 0x006E3300   <- the intro object
        sub_0007CF76                 esp correct throughout
          sub_000855B0               ecx = intro + 0xA0
            sub_000A3920 -> 0x03832CA0, vtable 0x001965E8  <- the f3dstvid widget
            vt[0x44] -> sub_000A3730
              VideoPlayer ctor -> 0x02BF9A30, vtable 0x001A83FC
              vt[0x04] -> 0x00148E00  VideoPlayer_Open

Both vtable readings match what part 145 recorded, including the awkward
0x001A83FC base rather than the 0x001A83E4 a naive walk-back gives.

**Where it stops.** All 19 labels of `VideoPlayer_Open` armed at once: 15
reached, ending at `loc_00148F6B`, whose `jge` goes to `sub_00148F9A`. All 55
labels of that continuation armed at once: 41 reached, and the trace then cycles

    0x00149137 -> 0x0014913E -> 0x00149148 -> 0x00149137 ...

with `eax = 0` on every pass. Decompiled, that loop is:

    do {
        sub_0014B6E0(0);            /* pump -- CRT 0x00151CB5 */
        eax = sub_00148C60(this);   /* ready chunks */
        this->0x80 = eax;
    } while (eax < this->0x64 - 2);

**It is the video's pre-roll buffering wait, and `sub_00148C60` always returns
zero**, so the buffer never fills and Open never returns. That is what wedges
`sub_000855B0`, which never unwinds `TitleIntroSequence_Render`'s frame, which
is why `esi`/`esp` looked corrupted back in the pump. Every symptom chased in
parts 156 and 157 was downstream of this one wait.

`eabig.mpc` opens with status 0 (part 145), so the file handle is fine and only
the streaming is not. The next question is what fills the ring that
`sub_00148C60` counts.

### Honest note on the tooling

The runtime-probe facility has still not repaid its build cost in wall-clock
terms. What it bought instead was reach: five runs took the diagnosis from "a
register goes to zero, intermittently" to a named loop in the video decoder,
and four of those runs would each have been a 52 s rebuild plus a guess about
which of 55 labels to instrument.

### Part 157d -- into the MPEG decoder, and two corrections

**Correction 1 -- the pre-roll loop is not an infinite loop.** Part 157c said
`sub_00148C60` "always returns 0" and that the loop at 0x00149137 never exits.
Both wrong. Probing showed `loc_00149158` and `loc_00149174` *do* fire, so it
exits. And measuring the clock properly:

    eax=0x1B63DB85  [esi+0x48]=0x1B63DB85  [[0x187378]]=0x1B63DB85
    eax=0x1B63DB95  [esi+0x48]=0x1B63DB85  [[0x187378]]=0x1B63DB95
    eax=0x1B63DBA5  [esi+0x48]=0x1B63DB85  [[0x187378]]=0x1B63DBA5

The tick counter advances in steps of **0x10** -- the 15.6 ms Windows tick
([[reference-timer-resolution-1ms]]). My earlier "elapsed frozen at 31 ms"
reading was 60 samples that all landed inside one tick, not a stalled clock.
The clock function itself is three instructions (`eax = *[0x187378]; eax =
*eax; ret`) and is fine.

`sub_00148C60` returns `(int)(elapsed_ms * 0.001 * 29.97)` -- the current
playback frame at the NTSC rate, with the 29.97 living as a double at
0x203B60 and the 0.001 as a float at 0x1A9F44. Both read correctly at runtime.

**Correction 2 -- a limit-1 label sweep does not find a stall.** Once every
armed label has fired once, the log goes quiet, so the last line is the last
*new* label, not where execution stopped. That is how 0x001492DE looked like a
terminus when it is simply a `ret`. Sweeps map reachability; they do not locate
hangs.

**The right instrument, and what it says.** The diag `loc` command samples the
live label 40 times. In 2 of 3 runs:

    main_loc  loc_00143850
    last_loc  loc_000B2482
    0 of 40 samples moved -- BLOCKED (same label throughout)

`0x00143850` is the **MPEG bitstream reader**:

    getbits(n):  return (*(uint32_t *)(*(void **)0x203020 + 4)) >> (0x20 - n);

So the main thread is inside the video decoder, pinned in `getbits`, making no
progress -- the shape of a bitstream buffer that is never refilled. `0x203020`
is the reader state pointer, and that buffer is the next thing to look at.

The third run was elsewhere entirely (`loc_000AA296`, still in the pump), which
is the same intermittency seen all through part 157: whether a run reaches the
intro sequence at all is timing-dependent.

### State

2/2 clean, no probes in the tree, draws median 7,638. Findings recorded for
0x00143850 and 0x0014B6E0.

### Part 157e -- the game has been reporting its own assertion all along

Chasing the decoder led somewhere better than the decoder.

**The abort screen.** `last_loc` pinned in `0x000B2430..0x000B2482`. That range
is a **designed halt**:

    0x000B2430  al = [0x1E3DD8]          ; fatal flag
                if (!al) return 0
                ... load "ABORTFONT" (0x0019AE24), render the message ...
    0x000B2480  jmp 0xB2480              ; hang forever, by design

So `jmp 0xB2480` is not a lifter bug or a spin to debug -- it is the retail
panic screen. Recognising that is worth more than the address itself.

**The assert globals.** The panic formatter at `0x000B27EB` does:

    eax = [0x1E3DE8]                     ; source file string
    if (eax) { ecx = [0x1E3DEC];         ; line number
               printf("FILE %s LINE %d\n", eax, ecx); }   /* 0x0019AE60 */
    int3

`0x000B5C10` -- the printf -- is a single `ret` in retail, so the message goes
nowhere. **But the two globals are still written.** They can be read straight
off a stuck process with the diag server, which makes every future assertion
self-reporting for free.

Read live, in 3 of 4 runs:

    [0x1E3DE8] = 0x001A5FB8 -> "libs\include\pathxSND.c"
    [0x1E3DEC] = 0x0000017D  = 381

**An assertion is firing in the sound library at pathxSND.c:381.** The strings
around that filename confirm the subsystem: `data/config/musicmap.inf`,
`data/config/music.inf`, `PATHXbnk`, `LOOPDATA`, `AsyncLevel`, `PathLevel`,
`DelayLevel`, `DelayFeedback`, `DelayTime`.

**Why this matters for the video.** Two audio addresses are recorded traps and
are disabled right now: `0x000151F0` (the audio streaming thread body, which
still ends in an integer divide by zero) and `0x00179411` (DSOUND, which dies in
`sub_00178DA8` with `esp == 8`). The intro video's pre-roll waits on decoded
frames paced against a clock, and the decoder sits in `getbits` reading a bit
window that never refills. An audio subsystem that asserted out is a very
plausible reason the stream never advances -- and it is testable.

`[0x1E3DD8]` stays 0, so the abort screen is never actually entered; the assert
records itself and the game carries on wounded.

### State

2/2 clean, no probes in the tree, draws median 7,659.

### Part 157f -- re-testing the audio traps, and a hypothesis that did not survive

Part 157e ended by proposing that the intro video stalls because the audio
subsystem is disabled. Tested properly, **that is wrong**, and two other claims
from that part need correcting.

**Both audio traps are still valid.** Re-tested with `XBOX_DISPATCH_ALLOW`, the
way the AddChild trap was re-tested in part 156:

    0x000151F0  audio streaming thread   exit 10,  draws 6820,
                                         KeBugCheck code=0x0000000A (IRQL)
    0x00179411  DSOUND                   exit 124, draws 0
    both                                 exit 124, draws 0

Neither has been fixed by anything landed in parts 156-157, so both stay denied.
The audio-thread failure is an **IRQL** fault, not audio logic --
[[reference-kpcr-irql-and-null-page]] is the relevant area, not pathxSND.

**Correction 1 -- the game does not enter the abort screen.** The bugcheck
reported "last guest label loc_000B2430", and I read that as the panic screen
firing. Probing it directly:

    [PROBE] 0x000B2430 [0x1E3DD8]=0 [0x1E3DE8]=0 [0x1E3DEC]=0   (x4)

The fatal flag is zero every time, so 0x000B2430 takes its `je 0xB2482` early
return. It is a routine per-frame check that happens to be called constantly,
which is exactly why it turns up as "last label" in an unrelated report. The
abort screen at 0x000B2480 is never entered.

**Correction 2 -- the pathxSND assert is probably a symptom, not a cause.**
`libs\include\pathxSND.c:381` records itself while audio is **disabled**, which
is the expected state for a sound system that was never brought up. Reading it
as an independent defect put the cause and the consequence the wrong way round.

**The hypothesis, tested.** Probing `VideoPlayer_Open` (0x00148E00) and its
return (0x000A3776) with and without the audio thread:

    audio on    Open reached 1, returned 0
    audio off   Open reached 0, returned 0   (that run never got there at all)

So enabling audio does **not** make Open return. The video stall is not simply
downstream of the disabled audio, and the next step is back inside the decoder --
specifically the bit window at `[[0x203020]+4]` that holds 2 and never refills,
and `sub_00143870`, the flushbits/refill that is evidently not being called.

### State

2/2 clean with nothing set, draws median 8,002, no probes in the tree.
Ledger: 0x000151F0 and 0x00179411 re-confirmed as traps with fresh evidence.

---

## Part 158 -- MMX pand/por implemented, and the video decode proved healthy

### The fix that landed

`pand`, `pandn`, `por` and `pxor` were annotated but never implemented. The
`mm` registers are plain `uint64_t` in generated code, so these are ordinary
integer operations -- there was never a reason to drop them:

    pand  dst, src  ->  dst &= src
    por   dst, src  ->  dst |= src
    pxor  dst, src  ->  dst ^= src
    pandn dst, src  ->  dst = (~dst) & src

Implemented in `lifter.py` (both the SSE dispatch list and `_lift_sse`), then
the four video colour converters were re-lifted with `relift.py`:

    sub_00149450  1964 -> 1815 bytes     sub_001495E0  1480 -> 1299
    sub_00149500  2330 -> 2117           sub_00149670  1918 -> 1689

All four now verify **clean against the XBE** -- zero dropped SIMD, down from 8
sites. 3/3 gate clean, draws unchanged. That closes the SIMD class flagged back
in part 153.

### What the video is actually doing

Measured, not inferred:

  * **The MPEG decoder works.** `[[0x203020]+4]` reads `0x000001B3` -- the MPEG-1
    sequence header start code -- and walks real bitstream data. Over one run
    the buffer pointer advanced `0x037330C8 -> 0x037E572C`: **713 KB decoded**,
    with 200,000 flushbits calls. All three runs byte-identical.
  * **The playback loop runs.** `0x001490C0..0x001490E0` is
    `do { player->vt[8](); sleep(); player->vt[0xC](-1); } while (!player->vt[0x10]());`
    -- UpdateSkipInput / Tick / IsFinished on the VideoPlayer. It hit a 5000-hit
    probe limit while every neighbouring label hit exactly 4.
  * **The CPU colour converters never run.** Their only callers are
    `PixelBlit_ConvertRowsFormat2And3/4And5/6And7Interlaced`
    (0x001499E8/0x00149A81/0x00149B1B), and those have **zero callers and zero
    data references** -- `dataxref` finds nothing in the image storing their
    addresses, and 0x001499E8 sits immediately after a clean epilogue, so it is
    a real function start that nothing reaches.

So the Xbox build almost certainly does YUV-to-RGB on the GPU and these CPU
paths are inherited dead code. The pand/por fix was still correct and worth
landing -- it was a genuine translation defect -- but it is **not** the reason
the video does not appear.

### Methodology: three "stalls" that were probe limits

`[esi+0x64]` stuck at 3, `eax` stuck at 0, the pre-roll "never exiting" -- all
three were **the probe hit limit being reached**, not the guest stalling. The
pre-roll actually exits after ~100 iterations once elapsed passes 34 ms. A
limit-capped probe and a genuine hang produce identical logs.

**Always compare the hit count against the limit.** A count equal to the limit
means "still going, truncated"; a count below it means "it really stopped
there". That single check is what finally located the playback loop, by showing
5000 against 4.

### State

2/2 clean, draws median 7,868, no probes in the tree. Lifter change is general
and applies to every future lift.

### Next

Find the texture-upload path inside VideoPlayer `Tick` (vtable 0x001A83FC slot
+0x0C) -- that is where decoded frames must reach D3D, and it is where the black
picture will be explained.

### Part 158b -- the video's root cause: a corrupted esp eats the VideoPlayer's vtable

The intro video plays **synchronously**: `sub_00149075`, a fragment of
`VideoPlayer_Open`'s chain, blocks in

    do { player->vt[0x08]();        /* UpdateSkipInput */
         sub_0014B570(ebx);         /* task pump      */
         player->vt[0x0C](-1);      /* Tick           */
    } while (!player->vt[0x10]());  /* IsFinished     */

verified against the XBE (`je 0x1490c0` at 0x001490E2, then `ret 0xC`). That
loop runs 5000+ times while **`VideoPlayer_Tick`'s entry fires only 4 times**,
and the frame blit `LoadingScreen_BlitImageToBackBuffer` runs exactly 4 times in
a 40 s run -- confirmed with a 100,000 hit limit, so not a probe artifact.

**Why.** Probing `esi` and `[esi]` every iteration:

    iter 1-3   esi=0x02BF9A30  [esi]=0x001A83FC   <- correct VideoPlayer vtable
    iter 4     esi=0x02BF9A30  [esi]=0x00000000   <- vtable pointer destroyed

A startup write-watch (`XBOX_DIAG_WATCH=0x02BF9A30`) names the writer:

    [WATCH] WRITE to Xbox VA 0x02BF9A30 (hit #4, tid 3060)
      guest: eax=002025A0 ... esi=00000001 edi=00405088 esp=02BF9A30
      loc:   main=loc_00143850  last=loc_000B26A9

**`esp` is the object.** The same thread had `esp=0x0423FECC` one hit earlier, so
the stack pointer jumped from the stack straight to this heap object in one
step -- an assignment, not drift -- and the next push wrote over
`player->vtable`. After that every `vt[]` read is garbage: 110 million ICALL
misses to `0x00202AE8` and `0x00000000` from recomp_0007.c:11471/11486/11494,
`IsFinished` returns 0 from the miss path, and the loop can never exit.

So the chain is: **corrupted esp -> push destroys the vtable pointer -> all
three vtable calls miss -> IsFinished always 0 -> Open never returns -> the
intro render never completes -> black screen.** Every symptom chased across
parts 156-158 hangs off this one event.

`main=loc_00143850` places the thread in the MPEG bit reader when it happens.

### What was ruled out along the way

  * The MPEG decoder is healthy -- 713 KB decoded, correct `0x000001B3`
    sequence-header start code, byte-identical across runs.
  * `LockRect` still returns the right geometry: pitch `0x0A00` (640*4).
  * The CPU colour converters are dead code in this build (no callers, no data
    references), so the pand/por fix -- correct and worth landing -- was never
    going to make the picture appear.

### State

2/2 clean, draws median 7,711, no probes in the tree.

---

## Part 159 -- the video's root cause fixed: one undetected stub, 24 bytes a call

### The chain, end to end

    sub_0014441B is an UNDETECTED STUB
      -> sub_001442FA branches into it when eax < 0x10
      -> the empty stub returns without popping the 5 registers or the
         return address that sub_001441D0 pushed  ==  0x18 leaked per call
      -> the 1 MB guest stack (0x04140000..0x0423FFF0) walks past its base
      -> a push lands on the VideoPlayer object at 0x02BF9A30 and destroys
         its vtable pointer
      -> every player->vt[] read is then garbage: 110 M ICALL misses to
         0x00202AE8 and 0x00000000
      -> IsFinished returns 0 from the miss path, forever
      -> VideoPlayer_Open never returns, TitleIntroSequence_Render never
         completes, nothing draws

**Every symptom chased across parts 156-159 hung off that one stub.**

### How it was found

Not by reading code. Each step was a measurement:

  1. `esi`/`[esi]` probed per loop iteration -- correct for 3 iterations, vtable
     zero on the 4th.
  2. `XBOX_DIAG_WATCH=0x02BF9A30` named the writer: guest **`esp` = the object**.
  3. A guard probe `esp<0x04140000` on decoder labels showed `esp` below the
     stack base and **descending 0x18 per sample**.
  4. Arming the decoder's function entries with that guard isolated
     **`sub_001441D0`** -- which pushes exactly 5 registers, so 5 + the return
     address = the 0x18 observed.
  5. `stackbalance.py --from sub_0014441B` reported "chain has a missing link",
     and the link was a `recomp_undetected_stub` one-liner.

### The fix and what changed

`recover_batch.py 0x0014441B` -- "superseded 1 undetected stub", 3/3 clean.

    esp below the stack base     yes  ->  never
    [player] vtable              zeroed on iter 4  ->  stays 0x001A83FC
    frame blits per run          4  ->  60
    undetected stubs executing   1  ->  0
    clears per run               ~327  ->  ~382

### Still black

60 frames are blitted with the correct pitch (0x0A00), and the presented
backbuffer still reads mean brightness 1-2. The blit runs on the main thread
inside `VideoPlayer_Open`'s synchronous play loop while the frame-timer thread
independently clears and presents, so the most likely explanation is that the
blit is cleared before Present. That is the next thing to test -- and it is a
much smaller question than the one this part closed.

### State

3/3 clean, draws median 7,669, no probes in the tree, zero undetected stubs
executing.

### Part 159b -- the remaining black screen is not the video's fault

After the stub fix the video blits 60 frames a run with provably correct
geometry:

    pitch  0x0A00 (640*4)      x 0x20   y 0x10   width 0x240
    pBits  0xF3BA0000 / 0xF3CCC000 alternating -- double-buffered, and the
           write-combined alias, exactly the values part 146 recorded as correct

and the presented back buffer is still near-black. **That is not a video
problem.** With `XBOX_TEST_QUAD=1`, the known-good magenta quad drawn
immediately before `d3d8_PresentFrame` **also fails to appear**: dumps read mean
brightness 0.6 with only (0,0,0), (34,34,34), (17,17,17). The dump is taken
*before* Present, which is correct for `DXGI_SWAP_EFFECT_DISCARD`
(d3d8_device.c:293), so it is not a flip-model sampling error.

`nv2a_live_pb.c` already carries a comment recording this as investigated:
geometry, state and pipeline bindings verified correct, back buffer still pure
black. So it is a **known, pre-existing workstream**, separate from everything
parts 156-159 fixed.

The translator's own numbers say the same thing:

    671,412 dwords in 500 batches -> 503,682 methods
    draws=7,793   verts=31,172 (4 per draw -- UI quads)   clears=388
    ignored=141,631   <-- 28% of all methods dropped

**Architecture worth writing down:** the title runs its own statically linked
Xbox D3D8, which writes NV2A push-buffer commands into guest RAM;
`nv2a_live_pb.c` interprets those into host D3D11. Present is driven by a
fallback timer, not by a flip command carrying a scanout address. The video blit
writes pixels **straight into guest VRAM** and never enters the push buffer at
all, so even once the translator lands its draws, the video will additionally
need those bytes uploaded at present time.

### State

2/2 clean, draws median 7,778, no probes in the tree.

### Next

Two independent threads of work, in this order:
  1. Why no D3D11 draw lands (the test quad is the smallest reproducer, and
     28% of push-buffer methods being ignored is the obvious suspect).
  2. Upload guest VRAM at present so directly-blitted content -- the video and
     the loading screen -- can appear at all.

---

## Part 160 -- the draws were never the problem

The standing belief, written into `nv2a_live_pb.c`, was that no D3D11 draw ever
lands. **That is wrong, and correcting it is this part's main result.**

Two built-in diagnostics settle it:

  * `XBOX_TEST_CLEAR=1` clears the default RTV to teal right before readback.
    The dump comes back uniformly teal, so the
    **RTV -> back buffer -> Present -> readback chain is sound.**
  * `XBOX_TEST_NOBLEND=1` (added this part, default off) forces
    `D3DRS_ALPHABLENDENABLE` off. **White pixels appear** -- 42 to 70 sampled
    per frame. With blending on, the same frames are black.

So geometry, viewport, RTV, shaders and input layout are all correct and the
rasteriser is producing pixels. **The black screen is an alpha/blending
problem**, not a missing draw. The built-in draw audit agrees on every count:
rtv = the back buffer, viewport (0,0 640x480), depth off, cull none,
topology 5, stride 28, rtwritemask 0xF. Textures upload with real content
(sampled means 27.9 / 51.2 / 122.6 / 27.9).

Pure white output is itself a clue: it means the pixel shader's texture-stage
loop breaks on its first iteration (`if (colorop <= 1u) break;`), so the result
is just `input.diffuse`, which the vertex shader forces to (1,1,1,1) when
FLAG_HAS_DIFFUSE is clear.

### A real defect found on the way

`submit_draw`'s texture binding lived behind `#ifdef GAME_HAS_FONT_ATLAS` --
**a macro defined nowhere in the tree.** The compiled path was:

    /* Generic path: no game-specific texture lookup, use vertex color only */
    dev->lpVtbl->SetTexture(dev, 0, NULL);      /* unconditionally */

so the translator **never bound a texture at all**; every draw was flat vertex
colour, and nothing textured could appear whatever else was correct. The real
lookup (`tex_upload` + MODULATE/TEXTURE) was sitting in the dead branch, and
`apply_draw_state` in the same file already did it properly. Fixed by giving
the compiled path the same logic.

**Honest result: it did not change the picture.** 3/3 clean, draws unchanged,
frames still mean 0.6. Textures do upload and `tex[stage].enabled` is driven
from push-buffer param bit 30, so the path now runs -- but the alpha problem
still swallows every draw, so the fix is currently invisible. It is correct and
it removes a real trap; it is not the cure.

Also noted, not yet fixed: `dev_SetTexture(stage, NULL)` sets
`tss[stage][D3DTSS_COLOROP] = D3DTOP_DISABLE`. That is not D3D8 semantics --
binding a texture and the stage op are independent -- and it means any caller
that unbinds a texture without immediately re-stating COLOROP silently disables
the stage.

### Next

One question now, and it is narrow: **why is the pixel shader's output alpha 0
when the logged vertex colours are 0xFFFFFFFF?** Everything else in the frame
is verified working. `XBOX_TEST_NOBLEND=1` is the switch that makes the
difference visible while chasing it.

### State

3/3 clean, draws median 7,860, no probes in the tree.

---

## Part 161 -- I was wrong about the black screen

Instead of reasoning from pixel statistics, I converted a captured frame to PNG
and **looked at it**. It reads, in clean white text:

    Autoloading from hard disk

**The rendering pipeline works.** Draws land, text renders, the swap chain
presents. The frames measured "mean brightness 0.6" for the simple reason that
the game is legitimately showing a black loading screen with one line of text on
it. There is no alpha bug and there never was; parts 160's "every draw is
invisible under SRCALPHA" reading was an artefact of judging a mostly-black
screen by its average.

That retires a misconception that had been in the tree long enough to be written
into `nv2a_live_pb.c` as a comment.

**What is actually true:**

  * The game sits at the autoload screen while the intro video decodes.
  * The video player never draws through the push buffer. It calls the Xbox
    D3D8 statically linked into the XBE, locks the back buffer, and writes
    decoded pixels **straight into guest video memory** -- measured pBits
    0xF3BA0000 / 0xF3CCC000 alternating, pitch 0x0A00, a 576-wide region at
    x=0x20, y=0x10.
  * On hardware the GPU scans that memory out. Here the host swap chain is a
    separate D3D11 surface, so those pixels have nowhere to go.

**So the video is decoding correctly and being drawn correctly into a buffer
nothing ever displays.** That is the whole remaining gap.

### Attempted fix, not yet working

`pgraph_d3d11_present_guest_fb()` lifts the guest framebuffer into a texture and
draws it as a full-screen quad through the same device that renders the loading
text. Wired in ahead of `d3d8_PresentFrame`, gated on `XBOX_GUEST_FB=<hex VA>`.

It segfaults -- the `CreateTexture` / `LockRect` calls through the shim are not
right yet. **Default builds are unaffected** (no env var, no call): 3/3 clean,
draws median 7,730. The address is explicit for now because nothing announces
the scanout base; the NV2A PCRTC start register at 0xFD600800 reads back zero.

### Next

Fix the presenter's texture creation, then find the scanout base properly rather
than by env var. The mechanism is the last piece: everything upstream of it --
decode, pacing, blit geometry -- is now measured working.

### State

3/3 clean, draws median 7,730, no probes in the tree.

### Part 161b -- the guest framebuffer, and an honest stopping point

Built `d3d8_PresentGuestFramebuffer()` / `d3d8_SetGuestFramebuffer()`: the nv2a
side resolves the guest VA through `va_ptr` and registers it; the *present path*
does the upload, converting A8R8G8B8 guest pixels to the swap chain's
R8G8B8A8 and forcing alpha opaque. Gated on `XBOX_GUEST_FB=<hex VA>`.

Three defects were found and fixed in it along the way, each worth keeping:

  1. **The shim's texture objects are not a route to the swap chain.** The first
     version created an `IDirect3DTexture8` and locked it; `tex_LockRect` hands
     back the shim's own `sys_mem`/`pitch`, which produced a wild pointer and a
     segfault. Rewritten against raw D3D11.
  2. **`D3D11_USAGE_DYNAMIC` is not a legal `CopySubresourceRegion`
     participant.** The map and the pixel conversion both succeeded, so the
     source reported 4032 of 4800 sampled pixels non-black while the copy
     silently did nothing. Changed to `D3D11_USAGE_STAGING`.
  3. **The copy has to happen inside the present path.** The swap chain is
     `DXGI_SWAP_EFFECT_DISCARD` and there are **three** `IDXGISwapChain_Present`
     call sites, so a copy made before `d3d8_PresentFrame` is destroyed by any
     Present that lands in between. Moved into `d3d8_PresentFrame`.

**It still does not show the video.** With all three fixed, captured frames are
byte-identical to runs with the feature off -- the same mean of 0.6077 every
time -- so the copy is still not reaching the surface that gets dumped and
presented. Default builds are unaffected: 3/3 clean, draws median 7,843.

**What is nailed down**, and did not change: the guest framebuffer genuinely
holds the decoded video (`0x03BB0000` reads `00ED00ED 00FF00ED ...`, and the
upload's own sampler reports 4032/4800 non-black on its second call). The pixels
exist, at a known address, in a known format. Only the last hop is missing.

### Next

Find which surface the dump and Present actually observe. The three Present call
sites are the thread to pull: if the shim's own Present (d3d8_device.c:862 or
:1712) is the one the user sees, then `d3d8_PresentFrame` -- and everything
hooked into it, including this upload and the frame dumper -- is decorating a
buffer nobody looks at, which would also explain why the dumps never move.

### State

3/3 clean, draws median 7,843, no probes in the tree. Feature off by default.

---

## Part 162 -- the intro video is on screen

`XBOX_GUEST_FB=0xF3BA0000` and the EA SPORTS BIG logo renders: the ident, the
circular EA SPORTS mark, and the trademark line "EA SPORTS(TM) and EA SPORTS
BIG(TM) are Electronic Arts(TM) brands" all legible at 640x480.

**`eabig.mpc` decodes and displays.** That is the thing parts 145 through 161
were chasing.

### What the last hop actually was

Three separate mistakes, each of which hid the next:

  1. **The shim's texture objects are not a route to the swap chain.**
     `tex_LockRect` hands back the shim's own `sys_mem`/`pitch`; using that as a
     GPU upload path gave a wild pointer and a segfault. Rewritten in raw D3D11.
  2. **`D3D11_USAGE_DYNAMIC` is not a legal `CopySubresourceRegion`
     participant.** The map and the pixel conversion both succeeded, so the
     source reported 4032 of 4800 pixels non-black while the copy did nothing.
     Changed to `D3D11_USAGE_STAGING`.
  3. **I was measuring the wrong frames.** With the copy finally working, the
     captures were still black -- because the video's first frame with content
     lands on **present #328**, and every capture window I had used stopped at
     ~250. `XBOX_D3D_DUMP_FROM=330` and the logo was simply there.

Mistake 3 is the instructive one. Twice I "proved" the copy was failing when it
was working, because the evidence window did not overlap the event. Related to
[[feedback-probe-limit-vs-stall]]: the same failure of confusing "my instrument
saw nothing" with "nothing happened".

A fourth, caught by luck: I first checked the destination with
`d3d8_BackbufferHash()` and read a non-zero hash as proof the copy landed.
**FNV-1a of an all-zero buffer is non-zero.** Replaced with a real non-black
pixel count of the destination, which is what finally gave a trustworthy
"destination 4031 non-black after copy".

### Also settled

Only **one** of the three `IDXGISwapChain_Present` call sites ever runs --
`d3d8_PresentFrame`, reaching #300 in 35 s. `dev_Present` and `dev_BeginPush`
never fire. So the present path was never ambiguous, and the guest-framebuffer
upload belongs exactly where it is.

### Not finished

  * **Colours are wrong.** The image is magenta and black: the green channel
    reads as zero in the guest framebuffer (bytes run `ED 00 ED 00`). Red and
    blue are correct and the geometry is perfect, so this is a channel or
    pixel-format question in the guest's own YUV-to-RGB step, not in the upload.
  * **Block artifacts** in the upper right and along the lower edge.
  * **The framebuffer address is an env var.** Nothing announces the scanout
    base -- the NV2A PCRTC start register at 0xFD600800 reads zero -- so
    `XBOX_GUEST_FB` is explicit for now. It needs discovering properly before
    this can be on by default.

### State

3/3 clean with the feature off, draws median 7,780, no probes in the tree.

---

## Part 163 -- the video plays in colour

### The colour fix

`sub_001493E0` is the video's real YUV-to-RGB row converter: MMX, three lookup
tables, `paddw` to fold chroma into luma and `packuswb` to pack 16-bit results
down to bytes. The generated code was:

    mm2 = MEM64(eax * 8 + 0x1C37B8); /* movq */    <- U table
    mm3 = MEM64(ebx * 8 + 0x1C3FB8); /* movq */    <- V table
    /* TODO: paddw mm2, mm3 */
    mm0 = MEM64(eax * 8 + 0x1C2FB8); /* movq */    <- Y table
    /* TODO: paddw mm0, mm2 */
    /* TODO: packuswb mm0, mm1 */
    MEM64(ebp + -8) = mm0;

**`paddw` and `packuswb` were emitted as comments.** Chroma was never added to
luma, and the 16-bit table entries were stored without being packed to bytes --
which is precisely the `F3 00 F3 00` pattern seen in the framebuffer and the
magenta image of part 162.

Implemented `mmx_paddb/paddw/paddd`, `mmx_psubb/psubw/psubd`,
`mmx_packuswb/packsswb/packssdw` in `recomp_types.h` and wired the opcodes into
the lifter, then re-lifted. The same row now reads **`F3 F2 F5 FF`** -- proper
BGRA, near-white -- and the EA SPORTS BIG logo renders in full colour.

Note this is a *different* converter from the four fixed in part 158
(`sub_00149450/500/5E0/670`, which are dead code in this build). The live path
is `PixelBlit_ValidateAlignmentAndDispatch` -> jump table at 0x00149C54 ->
`PixelBlit_ConvertRowsFormat1` -> `sub_001493E0`. Format code is 1.

### A tooling bug that misled me for two parts

`d3d8_DumpBackbuffer` wrote the back buffer's first three bytes straight into a
BMP with the comment `BGRA -> BGR`. The swap chain is **R8G8B8A8_UNORM**, so
the bytes are R,G,B -- and BMP wants B,G,R. **Every capture I produced had red
and blue swapped.** That is why the `XBOX_TEST_CLEAR` teal read back as
(153,153,0), and why the logo looked blue in my dumps while the user saw it
correctly on screen. Fixed.

### Still open

  * **Choppy: ~1.5 fps.** 60 blits and 300 presents in 40 s. Sub-millisecond
    `KeDelayExecutionThread` intervals were being rounded up to a full 1 ms
    `Sleep`; that is now a `SwitchToThread()` yield, which is correct but
    changed nothing measurable, so the rounding was not the bottleneck. Whether
    the remaining cost is decode throughput or pacing is not yet measured.
  * **No audio.** Expected: both audio addresses are still recorded traps --
    0x000151F0 bugchecks 0x0A (IRQL) and 0x00179411 collapses drawing to zero
    (re-tested part 157f).
  * The framebuffer address is still `XBOX_GUEST_FB`.

### State

3/3 clean with the feature off, draws median 7,805, no probes in the tree.

---

## Part 164 -- the video was never slow; the window was

### Correcting part 163

Part 163 recorded the video as running at "~1.5 fps" from 60 blits in a 40 s
run. That arithmetic was wrong. Timestamping the blit itself:

    60 blits, span 1937 clock units (~1.94 s), mean interval 32.8 ms

**That is ~30 fps, which is correct**, and the EA ident is simply a short
~2-second clip. Dividing its 60 frames by the whole run length manufactured a
performance problem that did not exist. Same family of error as
[[feedback-match-evidence-window-to-event]]: the measurement window did not
match the event.

### The real cause of the choppiness

Presents, not decode. 300 presents in 40 s = 7.5 fps, so roughly **7 of the
video's 60 frames ever reached the screen**.

`nv2a_live_pb.c` presents on the title's push-buffer frame boundary, with
`PRESENT_FALLBACK_MS 500` as a safety net. **While a video plays the title
stops issuing flips entirely** -- it writes pixels straight into guest VRAM --
so the 500 ms net becomes the only pacing there is.

Fixed by pacing presents at video rate whenever a guest framebuffer is
registered (`d3d8_HasGuestFramebuffer()`), leaving the 500 ms net for everything
else:

    presents in 40 s   300  ->  1500      (7.5 fps -> 37.5 fps)
    draws              unchanged, 3/3 clean

Verified animating: with `XBOX_D3D_DUMP_ONCHANGE=1`, presents 304-310 each carry
a **different** frame with a smooth brightness ramp (4.4, 6.2, 8.1, 10.0, 11.9,
13.8, 16.6) -- the logo fading in, one video frame per present.

### Also this part

`KeDelayExecutionThread` rounded sub-millisecond intervals up to a full 1 ms
`Sleep`; that is now a `SwitchToThread()` yield. Correct in its own right --
rounding a 100 us wait to 1 ms overshoots 10x -- but it changed nothing
measurable, so it was not a bottleneck. Recorded as such rather than claimed as
a fix.

### State

3/3 clean with the feature off, draws median 7,837, no probes in the tree.
Remaining on the video: no audio (both audio addresses are still traps), and the
framebuffer address is still `XBOX_GUEST_FB`.

---

## Part 165 -- the glitching was tearing, not decoding

The remaining "glitchy texture" was neither the decoder nor the colour
conversion. A tree-wide `xverify` sweep confirmed **no MMX packed op is dropped
any more**; what is left is benign (`wait` 46, `emms` 7, `wbinvd` 5, `sfence` 2)
plus the known `fnstsw` 19 / `sahf` 9 tail, none of it in the video path.

**The title double-buffers its video surface** between 0xF3BA0000 and
0xF3CCC000. The upload was pinned to the first address, so every other present
showed a stale or half-written frame -- the horizontal band across the middle of
the logo, plus scattered blocks.

First attempt picked the buffer whose contents had *changed* since the last
present. That is exactly backwards: **a buffer that is changing right now is the
one the title is mid-blit into**, so copying it lands a torn frame -- new rows
above, old rows below. Inverted to take the **stable** buffer, which is the
completed one:

    if (a == sig_a && b != sig_b)      pick = A;   /* A settled, B being written */
    else if (b == sig_b && a != sig_a) pick = B;
    else if (a != sig_a)               pick = B;   /* both moving */
    else                               pick = NULL;/* nothing new, hold */

With that, the EA SPORTS BIG frame is **clean**: no band, no blocks, correct
colours -- orange outline, red EA, blue SPORTS, black BIG on white.

`XBOX_GUEST_FB2` registers the second address.

### Where the intro video stands

  * decodes at a correct ~30 fps
  * presents at ~37 fps while playing, so every frame reaches the screen
  * full, correct colour
  * clean -- no tearing or block artifacts
  * **no audio**

### State

3/3 clean with the feature off, draws median 7,743, no probes in the tree.

### Next

Audio is the only thing left on the intro. Both audio addresses remain traps:
0x000151F0 (the streaming thread) bugchecks 0x0000000A on IRQL, and 0x00179411
(DSOUND) collapses drawing to zero. Neither is a small fix.

---

## Part 166 -- one audio trap was stale; the other is an HLE gap

### 0x000151F0 -- cleared

The audio streaming thread was trapped because enabling it bugchecked
`0x0000000A` (IRQL) after ~6,800 draws. **Re-tested, it now runs clean**: exit
124, no crash, no bugcheck, draws ~7,800, 3/3. Nothing in this part fixed it --
it was fixed by work elsewhere in the session and the trap simply outlived its
cause, exactly the pattern [[feedback-recheck-traps-when-stuck]] exists for.

Removed from the built-in deny list in `recomp_dispatch.c`. **3/3 clean with it
on by default**, draws median 7,640.

That also means the host audio stack is live:

    [APU] DSP GP/EP initialized (STUBBED - passthrough mode)
    [XA2] XAudio2 initialized (48000 Hz stereo 16-bit, 3 x 1024-sample buffers)
    [APU] Using XAudio2 audio backend
    [APU] MCPX APU initialized (standalone), MMIO base 0xFE800000, 256 voices

### 0x00179411 (DSOUND) -- still a trap, but now diagnosed

With DSOUND allowed, drawing still collapses to zero. The shape of the failure
is now clear, and it is **not** a translation defect:

    total kernel calls        4,840   (vs 102,766 with it denied -- 20x less work)
    KeStallExecutionProcessor 2,001   (12% of all calls, doubled)
    KeWaitForMultipleObjects    296
    main thread               BLOCKED at loc_001435B5, 0 of 40 samples moved

`KeStallExecutionProcessor` is a **busy-wait** primitive. DSOUND is spinning on
a hardware condition that never becomes true, and the APU's DSP is initialised
`STUBBED - passthrough mode`. So the library is waiting for a DSP that never
answers -- an HLE gap of the kind [[feedback-hle-gap-before-lifter-bug]]
describes, not a lifting problem.

The encouraging part: the XAudio2 backend is already up and the MCPX aperture is
already trapped at 0xFE800000, so the plumbing to carry samples exists. What is
missing is whatever handshake DSOUND is polling for.

### State

3/3 clean, draws median 7,640, no probes in the tree. Audio streaming thread on
by default; DSOUND still denied.

### Next

Find the APU/DSP register DSOUND polls -- the MCPX aperture is trapped, so the
read can be caught -- and make the stub answer it.

---

## Part 167 -- DSOUND's blocker found, and a fix backed out

### What DSOUND is waiting for

Tracing `KeStallExecutionProcessor` (12% of all kernel calls with DSOUND on) to
its call site through the import thunk at 0x00187458, then probing all seven
guest call sites, isolated **`loc_0017E6E9`** with 2000 hits -- exactly the stall
count. The loop is:

    esi = 0x100
    0x17E6E2: if (--edi == 0) goto fail
    0x17E6E9: KeStallExecutionProcessor(20)
    0x17E6F1: test [0xFEC00130], esi
    0x17E6F7: je 0x17E6E2                 ; keep waiting
    0x17E6FB: ebx = 0                     ; 1000 tries, 20 ms, give up

`0xFEC00xxx` is the **AC'97** aperture, not the APU. With `ACI_NABM_OFF 0x100`:
`0xFEC0012C` is **GLOB_CNT** and `0xFEC00130` is **GLOB_STA**, and bit 0x100 is
**PCR -- Primary Codec Ready**. So DSOUND resets the AC-link, waits 20 ms for the
codec to report ready, never sees it, and audio init fails.

### The fix works, and was backed out anyway

Reporting PCR does exactly what it should: **the spin goes 2000 iterations -> 0
and the success path is taken for the first time.**

But it also **collapses drawing to zero in the default build** -- with DSOUND
denied *and* the audio streaming thread denied. So some other consumer of
GLOB_STA changes behaviour once the codec claims to be ready, and that path is
not understood yet.

Backed out. A working picture is worth more than an audio path that stops the
game booting. The constants and the full reasoning are left in `aci_mmio.c` so
the next attempt starts from here rather than rediscovering it.

**Default restored: 3/3 clean, draws median 7,707.**

### Also this part

  * `XBOX_APU_TRACE=1` -- histogram of APU MMIO reads/writes by register. It
    showed the APU is touched exactly **once** (a read of `NV_PAPU_FECTL`
    returning 0) and never written, which is what redirected the search from the
    APU to the AC'97 side.
  * `XBOX_STALL_TRACE=1` -- reports `KeStallExecutionProcessor` callers. Note the
    guest return address is useless here: the recompiler pushes a dummy 0, so
    `[esp-4]` reads 0. The static route (import thunk -> call sites -> probe
    each) is what worked.

### Next

Find what else reads GLOB_STA and why codec-ready stops the boot. That is the
one thing between here and sound.

---

## Part 168 -- the PCR switch, and a limit trap walked into again

`XBOX_ACI_PCR=1` now gates the codec-ready bit, so the finding is preserved and
reproducible without risking the default:

    gate off (default)   draws 7,757   3/3 clean
    gate on              draws 0

That isolation is worth having on its own: the consequence is now a one-flag
experiment rather than a rebuild.

### Where the investigation got to, and where it went wrong

With PCR on, the app is not deadlocked -- threads keep running, 3,500+ kernel
calls per report, `KeDelayExecutionThread` at 23%, and the kernel summary shows
`guest esp=0x03842730`, a **worker** stack. So the audio worker loops while the
main thread waits.

Chasing the main thread led to `sub_000B05D0` via `main_loc = loc_001435B5`, and
two of its labels pegged a probe limit of 10. I read that as the hang. It is
not: both are **bounded** -- a ten-iteration constructor loop and a table walk
that stops at a `-1` terminator. They hit the limit because they legitimately
iterate ten times.

That is [[feedback-probe-limit-vs-stall]], which I wrote three parts ago, walked
into again. The rule needs restating in the form that would actually have caught
it: **a count equal to the limit means nothing unless the limit is far above the
loop's natural trip count.** Ten is not a diagnostic limit; it is inside the
range of ordinary loops.

### State

3/3 clean, draws median 7,757, no probes in the tree. `XBOX_ACI_PCR` documents
and reproduces the DSOUND finding without endangering the default build.

### Next

The honest position: the remaining question -- what else consumes GLOB_STA and
why codec-ready stalls the boot -- is open-ended, and this part produced a
switch and a corrected mistake rather than progress toward sound. Worth picking
up deliberately rather than by continuing to poke.

---

## Part 169 -- the black screen was not audio, and the video now works unset

The user asked whether audio was crashing the game before the video could show.
Measured on a plain default run:

    exit 124   crash 0   bugcheck 0
    VideoPlayer_Open reached: 1
    video frames blitted:     60
    draws 7,941

**Not audio.** Nothing crashes. The video decodes and blits all 60 frames --
into guest video memory that the host swap chain never reads. Without
`XBOX_GUEST_FB` naming that memory, nothing copied it to the screen, so the
screen was black. The env var was the only thing between the user and the video,
which is not a fix, so it is gone.

### Taking the address from the title instead

Hardcoded addresses are a property of one build's VRAM layout. The title itself
knows where its surface is, so the wrapper takes it from the guest LockRect:

    sub_0016B1C0(surface, pLockedRect, pRect, flags)   /* ret 16 */

`pLockedRect` is at `[esp+8]` and must be read **before** the call -- the
original pops its arguments. Afterwards `[plr]` is the pitch and `[plr+4]` the
pBits. Surfaces outside the write-combined VRAM alias, or with an implausible
pitch, are ignored; the first two distinct ones become the front and back
buffers.

Result on a plain run, nothing set:

    [FB] guest framebuffer discovered: 0xF3CCC000 pitch 2560 (640x480)
    [FB] second framebuffer discovered: 0xF3BA0000 (double buffered)
    [FB] call 1: source 4032/4800 non-black, destination 4032 non-black

Both buffers found, exactly the pair that had been hardcoded.

### One thing that did not work, and why

`recomp_lookup_manual()` looked like the right hook and is not: it is consulted
for **indirect** calls only, and the blit calls LockRect **directly**, so the
override never fired. The call site in the generated blit is routed through the
wrapper instead. That is a hand edit inside a generated file, so it is commented
in place -- **a relift of `LoadingScreen_BlitImageToBackBuffer` will drop it.**

### State

3/3 clean, draws median 7,653, no probes in the tree. The intro video plays on a
plain launch: correct colour, ~30 fps, clean, every frame presented.
`XBOX_GUEST_FB` / `XBOX_GUEST_FB2` still work as an override.

---

## Part 170 -- the AC'97 model, worked out properly

Two real defects in the model, and a clear statement of what audio still needs.

### 1. The global registers were being swallowed by the channel range

    ACI_NABM_CHANNELS (4) * ACI_CHANNEL_STRIDE (0x10)  ->  0x100 .. 0x13F

AC'97 has **three** bus-master channels whose registers end at 0x2B; **0x2C is
GLOB_CNT and 0x30 is GLOB_STA**. With a four-channel range both globals fell
inside it, so GLOB_CNT was handled as channel 2 register 0x0C and GLOB_STA as
channel 3's BDBAR -- and `aci_reset_channel` on channel 2 would clobber
GLOB_CNT outright.

Fixed: the channel test is bounded by `ACI_GLOB_CNT_OFF`, and both globals are
handled explicitly ahead of it. **Kept unconditionally, 3/3 clean.**

### 2. Codec-ready belongs to the reset, not to power-on

The driver's sequence (0x0017E6B3) is: read GLOB_CNT, release the AC-link cold
reset (bit 1), clear warm-reset/shut-off, then poll GLOB_STA.PCR. Real hardware
raises PCR **in response to** that reset. Part 167 raised it at power-on
instead, which is not what the hardware does and is a plausible reason something
else took a different path. Now modelled properly: PCR is raised when bit 1 of
GLOB_CNT goes 0 -> 1, and it survives the write-1-clear of GLOB_STA's interrupt
bits because PCR is read-only status.

The handshake works -- `AC-link cold reset released -- primary codec ready`, and
DSOUND's 20 ms poll succeeds first time.

**It still stops the boot, so it stays behind `XBOX_ACI_PCR=1`.** Correct
timing changed nothing about the outcome, which rules the timing out as the
cause and is worth knowing.

### What audio actually needs

With PCR on, nothing spins -- a 60-label sweep at a limit of 30,000 shows counts
of 1 to 3 across the whole DSOUND range. The app **waits**:
`KeDelayExecutionThread` at 23%, no busy loop, main thread blocked.

The model says why. It defines `ACI_CR_RPBM` (run bus master) and the write-1-
clear mask for `BCIS` / `LVBCI`, and **nothing ever raises them**: there is no
DMA engine and no completion signalling. So the driver brings the codec up,
starts the bus master, and waits for a buffer-completion interrupt that cannot
arrive.

That is the gap, stated precisely at last. It is not a bit to flip -- it is a
buffer-descriptor walker consuming at the sample rate and raising BCIS, plus the
interrupt path to deliver it. The XAudio2 backend is already up and waiting for
samples on the other side.

### State

3/3 clean, draws median 7,872. Layout fix in unconditionally; PCR behind
`XBOX_ACI_PCR=1`.

### Still open on the video

The user reports a remaining "glitchy feeling" after the tearing fix. Not
investigated this part.

---

## Part 171 -- the DMA engine, built and dormant

Built the AC'97 bus-master DMA engine proposed in part 170: `aci_dma.c` walks
the buffer descriptor list exactly as the hardware does -- BDBAR, CIV/LVI,
PICB in samples, 8-byte descriptors with the IOC bit -- maintaining SR's DCH,
CELV, BCIS and LVBCI, and ticked 32 samples at a time from the APU frame thread.

It deliberately does **not** invoke the title's ISR. Delivering an interrupt
means running guest code from the APU's host thread, which has no guest stack,
and that exact class of mistake has cost this project real time already
(see [[reference-shared-tib-fs-prefix]] and the esp work of parts 156-159).
Drivers poll SR as well as taking interrupts, so the state machine is worth
having on its own and is safe without the ISR path.

**It never runs.** With `XBOX_ACI_PCR=1` and the DMA trace on, not one channel
ever has CR.RPBM set:

    [ACI] AC-link cold reset released -- primary codec ready
    (no [ACI-DMA] output at all)

So the title takes codec-ready, and then blocks **before it ever starts the bus
master**. The missing DMA was a real gap and is now filled, but it was not the
blocker: the blocker is earlier than that, somewhere between "codec ready" and
"program the channels".

Worth being plain about: this part built the thing I said would be next, and it
did not move audio forward. What it did do is eliminate a whole candidate --
and the engine is correct, dormant, and will be needed the moment the driver
does start a channel.

### State

3/3 clean, draws median 7,789. `aci_dma.c` in the build, inert unless a channel
runs. Layout fix from part 170 unconditional; PCR still behind `XBOX_ACI_PCR=1`.

### Next

Find what the title does between codec-ready and starting the bus master. It is
a bounded window now: PCR is raised, the channels are untouched, and everything
in between is one stretch of DSOUND init.

---

## Part 172 -- three more candidates eliminated, still no sound

Tracing forward from codec-ready, with proper limits this time:

  * **The AC'97 init completes.** `sub_0017EE1F` runs to its epilogue and
    returns. The whole reset/codec-ready sequence now succeeds.
  * **The channel-reset spin is fine.** A host backtrace of the stuck thread
    (`sub_0017D925 -> sub_0017E8B0 -> sub_0017EA9D -> sub_0017E97A`) pointed at
    what looks like an infinite loop: write CR.RR, read it back **once**, then
    `test cl,cl / jne` on a register nothing reloads. That is a faithful
    translation -- the original really does that, so the hardware must return RR
    already clear, which is exactly what `aci_mmio.c` was written to do. Probed:
    `loc_0017E9B0` is reached with the low byte of ecx zero. The loop exits.
  * **The waits return.** `KeWaitForMultipleObjects` with an INFINITE timeout on
    three objects, but the call count climbs steadily -- a worker looping, not
    the blocked thread.

So the boot still hangs with audio on and none of these is why.

### A regression caught, and closed

The part-171 DMA engine ticks from the APU's host thread while the MMIO trap
writes the same register file from guest threads, with no lock. One default run
in nine came back `draws=0`. Whether that race was the cause is unproven, but it
does not need to be carried: the engine now returns immediately unless
`XBOX_ACI_PCR=1`, so the default path is byte-identical to before it existed.
**6/6 clean afterwards.**

Also worth recording: the runtime probe evaluator has **no `*` operator**, only
`+` and `-`. `MEM32(esi)*4+0x17F3EC` silently evaluated as a parse failure
(printed with a `?` prefix) rather than an error.

### Honest state of the audio work

Five parts in. Real fixes landed -- a stale trap cleared, the AC'97 global
registers rescued from the channel range, codec-ready modelled to the reset, a
correct DMA engine written -- and **no sound**, with the blocker having moved
four times and still not located. Each part has eliminated a candidate rather
than advanced the goal.

### State

6/6 clean, draws median 7,774. Layout fix unconditional; codec-ready and the DMA
engine both behind `XBOX_ACI_PCR=1`. New diagnostics: `XBOX_STALL_TRACE`,
`XBOX_WAIT_TRACE`, `XBOX_APU_TRACE`, `XBOX_ACI_DMA_TRACE`.

---

## Part 173 -- video frame ordering, and a metric that could not settle it

Capturing 60 consecutive presents during playback and hashing each showed **7
frames that had already been superseded** reappearing -- the display stepping
backwards about 12% of the time. That is the shape of the residual "glitchy
feeling".

Two rules were missing, and both are needed:

  1. **Never copy a buffer that is changing right now** -- that is the one being
     blitted into, and it lands a torn frame. (Already in place from part 165.)
  2. **Never show a frame older than one already shown.** Picking "whichever is
     stable" satisfies rule 1 but not rule 2: with the video at ~30 fps and
     presents at ~37, both buffers are often idle and the stable one is
     sometimes the *older*. Now each buffer carries a generation stamped when it
     last changed; the newest idle one wins, and a candidate older than what was
     already shown is refused.

Plus: a "hold" now re-presents the **staging copy** rather than re-reading the
last-shown guest buffer, which by then may be mid-write.

### Why this is not marked fixed

The measurement cannot confirm it. Counting "a hash reappearing after a
different hash" treats genuinely-new frames that happen to look identical -- of
which a mostly-static logo has many -- as glitches. Successive runs gave 7, then
4, then 6, which is noise, not signal.

So: the two rules are correct on their own terms and the code is better for
them, but **whether the user-visible glitching is gone is unverified**. A real
answer needs a tearing detector (finding a horizontal discontinuity within a
single frame) rather than a frame-to-frame hash.

### State

4/4 clean, draws median 7,755.

---

## Part 174 -- consolidation, and audio by differential

### Consolidation

  * **Notes and ledger** current through part 173; 46 addresses tracked, down to
    **2 traps** (`0x00179411` DSOUND, and `0x00172202`).
  * **Tree hygiene**: no temp files in `build/`, no probes in the tree.
  * **Dashboard republished** -- the "Latest" section now covers the video
    result and the method notes instead of part 156.
  * **Memory consolidated**: 78 files -> 74. Four merged away, into two sharper
    ones:
      - `feedback-probe-limit-vs-stall` now covers the whole class -- probe
        limits, capture windows that miss the event, log caps, tracing overhead
        that changes the answer, and verifying a proxy instead of the
        destination. Four separate memories were describing one mistake.
      - `feedback-grep-dispatch-not-sub-names` absorbed the stale-export note:
        both are "an empty grep is not proof".
      - `project-m4-open-item` stripped of its dead milestone scheduling; the
        `.xbd` model-slot vtable question is still genuinely unanswered and
        still gates rider and prop meshes.
    Index at 74 lines / 11.6 KB, no dangling pointers.

### Audio by differential -- what it did and did not give

Instead of tracing forward again, ran the same wide label net under both
configurations and diffed coverage.

**Gave:** in a *working* run the main thread sits in the MPEG bit reader
(`loc_00143850`) the whole time; in a *hanging* run it stops at
`loc_001435B5`. So with audio on the main thread never reaches video decode at
all -- the failure is upstream of everything the last five parts were probing.
`sub_000B05D0` was cleared too: it completes in both (10 constructor iterations,
12-step table walk, reaches its end).

**Did not give:** the divergence point. A sparse net (every 97th label) appeared
to show the hanging run reaching only one label, `0x000B29E4`, suggesting the
frame-timer area. Netting that area finely showed **both runs reach the same 22
labels** -- so the sparse result was a sampling artifact, not a divergence. The
working run reaches more labels simply because it runs longer; "in A but not B"
is therefore everything after the hang, not the cause of it.

That is the same family of error as [[feedback-probe-limit-vs-stall]]: a
measurement that cannot distinguish the two outcomes it is being asked about. A
coverage diff needs the nets to be dense enough that the *first* divergence is
inside the sample, and needs to compare order, not membership.

### State

3/3 clean, draws median 7,576. Audio still not working; the useful new fact is
that its blocker is early -- before the application's own init -- not inside
DSOUND where the search has been.

## Part 175

### Audio: the AC-link spin was a missing SPDIF channel, and codec-ready is now the default

`XBOX_ACI_PCR` existed because raising GLOB_STA.PCR "broke the boot". It never
did. The Xbox MCPX puts a **fourth, SPDIF bus-master channel at NABM+0x70**,
outside the three-channel PC AC'97 layout `aci_mmio.c` modelled. Every access to
that channel fell through to the raw-register path, so the driver's reset of it
never completed and it spun at `0x0017E9AC` waiting for CR.RR to self-clear.

    #define ACI_SPDIF_OFF  (ACI_NABM_OFF + 0x70u)
    if ((off >= ACI_NABM_OFF && off < ACI_GLOB_CNT_OFF) ||
        (off >= ACI_SPDIF_OFF && off < ACI_SPDIF_OFF + ACI_CHANNEL_STRIDE)) {

Spin hits went **20,000 -> 6**, draws **0 -> 7,998**.

Codec-ready is therefore now **on by default** (`XBOX_ACI_PCR=0` forces it off,
for bisecting). Measured over 4 runs each, on and off are indistinguishable:
7,633..7,889 with, 7,587..7,954 without.

Two related corrections while the gate was open:

* The power-on `GLOB_STA.PCR` force in `aci_mmio_install()` is **removed**. Real
  hardware is not ready at power-on; it becomes ready a moment after the driver
  releases the AC-link cold reset, which `aci_write()` already models. Forcing
  it early let code sampling GLOB_STA take a path the hardware would never give.
* `aci_dma.c` shared codec-ready's switch. It now has its own, `XBOX_ACI_DMA=1`.
  Nothing is lost while it is off: no channel has ever set CR.RPBM, so there is
  nothing for the engine to walk. It stays off by default because it races the
  MMIO trap (documented in part 174).

**Still open:** with DSOUND (`0x00179411`) un-denied, draws still collapse to 0.
That is now a separate, later blocker from the AC-link spin.

## Part 176

### The video corruption was a second, hidden opcode-dispatch gate

The user sent a screenshot of the EA SPORTS BIG intro: the logo recognisable in
outline but overlaid with flat magenta, grey and white slabs on 16-pixel
boundaries. That picture named its own cause.

`sub_00149450 / 00149500 / 001495E0 / 00149670` are the video's **YUV-to-RGB565
converters**, written entirely in MMX: two chroma table lookups, `paddw` to
combine them, a luma lookup per pixel, `paddw` to add chroma to luma,
`packuswb` to saturate-pack, `paddusb` for the channel bias, then
`pand`/`pmaddwd`/`psrld`/`por` to fold RGB888 onto RGB565 bit positions.

**44 of those instructions were emitted as `/* TODO: ... */` comments.** Only
the `pand`, `por` and the `movd` store survived, so luma reached the screen
roughly intact and chroma was never added at all -- exactly the picture the user
photographed.

#### Why the earlier fix had not taken

Part 163 added `paddw`/`packuswb`/`paddb`/`paddd` to `_MMX_BIN` in the lifter
and they were still emitted as TODOs. Two reasons, and the second is the
important one:

1. **The bodies were stale.** A lifter fix changes nothing until the already
   translated functions are re-lifted (`relift.py`). The `pand` from part 157
   was present and the `paddw` from part 163 was not -- that gap dates a body
   precisely.
2. **There were two opcode lists, and the one that looks authoritative is not.**
   `_EFLAGS_PRESERVE` near the top of `lifter.py` lists a long set of SSE/MMX
   mnemonics -- but it is about *flags*, not dispatch. The real gate was an
   **inline tuple at the dispatch site** listing a subset. An opcode absent from
   that tuple never reaches `_lift_sse()` no matter how complete its handler is,
   and falls through to the generic TODO. Adding `paddusb`/`pmaddwd`/`psrld` to
   `_EFLAGS_PRESERVE` and writing their handlers therefore changed *nothing*.

The inline tuple is now the module-level `_SSE_DISPATCH` frozenset, with a
comment saying it is the only thing that decides whether an opcode is
translated. One list, one place to edit.

#### What was added

`ssx_recomp/src/recomp/recomp_mmx.h` (new, included from `recomp_types.h`):
saturating adds/subs, `pmaddwd`/`pmullw`/`pmulhw`/`pmulhuw`, all eight packed
shifts, the six unpacks, `pavgb`/`pavgw`, the compares, min/max and `psadbw`.
The shifts deliberately do **not** reduce the count modulo the lane width: x86
gives zero (or all-sign, for arithmetic) at or above the width, and C would make
the shift undefined.

After relifting the four converters, **every SIMD TODO in the tree is gone**
(the 22 remaining TODOs are all non-SIMD, in mis-decoded regions).

#### Result

Correct colour, logo intact, and the flat slabs gone. Residual: **34 flat-grey
8x8 blocks**, byte-identical across frames, in two small contiguous macroblock
runs (row 10 cols 33-36, row 19 cols 35-36). Persisting identically across
frames means one early decode failure propagating forward through P-frame
prediction, not a race.

### A duplicated helper family, and a relift that regressed

`recomp_types.h` already had an `mm_*` family (12 ops) that the lifter never
emits -- it is used only by **hand-transcribed** bodies. The new `mmx_*` family
(48 ops) is a superset. Both are live; the `mm_*` users are
`sub_00177AB2`/`sub_00178164` only.

Three more functions carried the *other* half of the same bug class: opcodes
that reach `_lift_sse` but have no handler fall through to a generic
`/* SSE: ... */` comment, which is the same silent loss as a TODO. 55 sites:

* `sub_00169AA0` -- a streaming MMX **memcpy** (`movntq`, `prefetchnta`). Its
  eight `movq` **loads** were dropped while all eight stores remained, so it
  copied uninitialised `mm0..mm7` over the destination. Relifted; **kept**.
* `sub_00177AB2` / `sub_00178164` -- an 8x8 `punpck` transpose and a 28 KB
  function around it. Relifted, and the user immediately reported **"now every
  text is garbage"**. Reverted; they keep their hand-written bodies.

Isolated afterwards by relifting `0x00169AA0` alone: text stayed clean, so the
regression is in the `recomp_0009.c` pair, not the memcpy. Their relift also
made **no** difference to the video (captures byte-identical), so they are not
on the intro's path at all. Left alone pending a `xverify --function` diff
against the XBE.

The lesson is not "do not relift" -- two of the three fixes were real and one is
kept. It is that a relift replaces working code and must be verified per
function, and that the fastest verification available here was the user looking
at the screen.

### State

5/5 clean, draws 7,704..7,820, no crashes. Video: correct colour, ~30 fps, minor
residual block artifacts. Text: clean. Audio: codec-ready on by default, DSOUND
still collapses draws to 0.

## Part 177

### Correction to part 176 first

Part 176 credited the relift of `sub_00149450 / 500 / 5E0 / 670` with fixing the
corruption in the user's screenshot. **Those four converters never run.** A
probe over a 40 s run: `sub_001493E0` (the live converter, fixed in part 163)
12,544 hits; the four relifted converters 0; the relifted memcpy `sub_00169AA0`
0. The notes for part 163 already said the four were dead in this build and I
did not re-read them. What actually happened: the user's screenshot was a frame
from the *animated* part of the intro, my "after" captures were of the *settled*
logo, and those had never had the magenta slabs. Different moments, compared
as if they were one -- the evidence-window error again. The part-176 lifter work
(`_SSE_DISPATCH`, `recomp_mmx.h`) is still correct and still closes real gaps;
it just was not the visible fix.

Also measured: **`sub_00178164` is live** (199 calls in 40 s). It is the function
whose relift turned every text glyph into garbage, so it sits on the text or
texture path, not the video.

### DirectSound runs live, and is on by default

`0x00179411` (DirectSound's `Release`) is off the default deny list; the list is
now empty. 5/5 clean on a plain launch, draws 7,482..7,772. Getting there took
four independent faults, each found by following the previous one.

**1. A host call with four of seven arguments zeroed the guest esp.**
With DSOUND live the main thread spun forever in `sub_0017E97A`'s AC'97 reset
wait (`test cl,cl; jne self` -- the compiler hoisted the register read out of the
loop, so the driver reads CR exactly once). The channel object was `0x0071B9B8`
with index 0xD0: garbage. Walking back: the device destructor `sub_0017EE12`
ran with `this = 0xD0`, because `sub_0017E014` computed `esi + 0xD0` with
`esi = 0`, because **esp was 4** after its member-free loop -- pinned to a single
kernel call, `MmFreeContiguousMemory` (ordinal 171), entered with esp
0x0423FC18 and left with 4.

`bridge_MmFreeContiguousMemory` calls `xbox_HeapFree`, which called
`xbox_heap_owner_of(va, &base, &size, NULL)` -- four arguments to a
seven-argument function, from above its definition with no prototype in scope,
so C accepted an implicit `int f()`. On Win64 the three missing arguments came
from the caller's stack slots, and the function copied the allocation's recorded
backtrace through that garbage `frames` pointer. Fixed: prototype in
`xbox_memory_layout.h` (xbox_diag.c's private extern removed), NULL-safe
definition, correct call. **And the class is closed:** `xboxrecomp/CMakeLists.txt`
now builds all six runtime libraries with `-Werror=implicit-function-declaration`
(verified in every target's `flags.make`). It was the only such call in the
runtime.

**2. A 64-slot timer map cancelled the frame clock.**
Drawing was still zero, with no crash. The main thread waited INFINITE on the
frame event; the frame callback had fired 2..48 times and stopped; XAPI's
multimedia-timer thread (`sub_001543DE`) sat in `KeWaitForMultipleObjects` on
its KTIMER array. The VA->timer map in `kernel_bridge.c` held **64** entries,
recycled round-robin **with no regard for whether the victim was armed**. XAPI
initialises 63 at startup; DirectSound initialises one per device at a fresh
heap address (`obj+0x618`). Default build: 69 inits (5 evictions). DSOUND live:
72 (8) -- enough to reach the periodic timer behind `timeSetEvent` that drives
`Application_FrameTimerCallback`. Fixed: 1024 slots; recycling picks an
**unarmed** timer and reports loudly if every slot is armed; the map has a lock
held for each whole bridge operation (several guest threads initialise timers);
and `xbox_KeReleaseTimerResources()` deletes a timer's host queue timer,
waiting out a running callback, before a slot is reused or re-initialised (a
fired one-shot used to keep its handle; a re-initialised armed timer lost its
handle to the memset and kept firing).

Result: DSOUND live drew **0 -> ~7,200**, then bugchecked 0x0A at ~10 s.

**3 and 4. Two lifter flag defects in DirectSound's IRQL lock.**
The bugcheck report now prints the thread's TIB and the IRQL it read. It said
**192** -- on the main thread, which has its own KPCR (the old "one shared TIB"
memory is out of date). A new `[IRQL]` report names any KfRaise/KfLower call with
a level above HIGH_LEVEL: `sub_0017E9EF` passed 48, then 192.

DirectSound's lock (`sub_0017992D`) computes `raised = (IRQL < 2)` with the MSVC
idiom `cmp al,cl; sbb eax,eax; neg eax`. The lifter never assigned `_cf` -- it
was a local initialised to 0 -- so every `sbb reg,reg` produced 0, the lock never
raised IRQL, and it never wrote the saved-level byte. Its release tests
`cmp [ebp-8], edi; pop edi; pop esi; je` -- and the lifter emitted the compare at
the branch, **after** `pop edi` restored the caller's edi. Wrong answer, so it
called `KfLowerIrql` with the byte the lock never wrote.

Both are fixed **in the lifter**, not in the generated C:

* `_needs_flag_snapshot()`: between a flag setter and a consumer in the same
  block (jcc, setcc, cmovcc, sbb/adc/rcl/rcr), if anything writes a register or
  memory the setter's operands read, the operands go into `_fsa`/`_fsb` -- before
  the setter for cmp/test, after it for result-based setters -- with their width
  preserved, and the consumers read the snapshots. `_insn_writes()`
  over-approximates on purpose.
* `_make_cf_expr()`: sbb/adc now get `_cf` computed from the last setter (cmp,
  sub, add, neg, and the logical ops that clear CF), width-masked.
* `translator.py` declares `_fsa, _fsb` only in functions that use them.

Relifted `sub_0017992D` and `sub_0017E9EF` (diff: exactly the intended lines).
DSOUND live: 3/3 clean, no bugcheck, no impossible IRQL.

### The same lifter fix repaired the intro video

Relifting the whole decoder range (0x00146000-0x0014C000) is **not safe**, and
that is its own finding: the diff showed it would undo five fixes that were made
to the generated C instead of the lifter -- the x87 memory-operand pass (fmul
[m] back to the popping register form), the `ftol` argument, the cmpsb carry,
ICALL save points after cdecl pushes, and every renamed callee (`sub_00150950()`
where the tree calls `Heap_Free()`, which does not link). Reverted.

New tool, `tools/audit/relift_flags_only.py`: relift, then per function keep
the new body **only** if every difference is a flag snapshot, a carry line, a
deferred-flag marker, the old script's `_fcN_` snapshot, or a branch/setcc whose
condition now reads a snapshot with the same destination; restore the old body
otherwise and say why. Over the decoder: **11 kept, 31 restored**, 965
unchanged.

The kept changes include branches and `sete`/`setns` sites the old
`fix_flagclobber.py` skipped as unrecognised shapes, and one bug that script
*introduced*: it paired the `test eax,eax` at the end of `sub_00146C16` with a
branch in a different function, `sub_00146C40`, and declared a fresh
`_fc7_eax` there that was never assigned -- a decoder branch testing
uninitialised stack. The original bytes are `mov eax,[0x203014]; test eax,eax;
jne`, which is what the lifter now emits.

**Result:** the animated part of the EA SPORTS BIG intro plays -- the red EA sweep,
SPORTS sliding in, the BIG zoom, the outline build -- where it was magenta bands.
The settled logo went from **34 flat-grey blocks to 0**, no magenta. Some
stair-stepped edges remain on fast-moving frames.

### Audio: why there is still no sound

With DSOUND live the APU is genuinely brought up -- VP/GP/EP buffer addresses,
front-end PIO methods, GP DSP setup. Two findings on the way:

* **The trace hid it.** `XBOX_APU_TRACE=1` printed its histogram only when an
  access arrived *and* 5 s had passed, so a burst read as "one register, once".
  It now reports whenever a register is seen for the first time, and
  `XBOX_APU_TRACE=2` logs the first 4,000 accesses in order (PIO free-space
  polls excluded).
* **Every sample the emulated APU produced was being thrown away.**
  `mcpx_apu_monitor_frame`'s XAudio2 branch cleared `monitor.frame_buf` -- where
  the VP->DSP bypass leaves its output -- then sent only the test tone and the
  HLE software mixer, which a title with its own statically linked DirectSound
  never feeds. Rewritten: each 256-sample block is APU output + software mixer +
  tone, saturated, gathered into sink-sized buffers, for both XAudio2 and
  waveOut. XAudio2 now reports its first non-silent buffer and a periodic peak.

Still silent, and now for an upstream reason: **no voice is ever switched on**
(no `VOICE_ON`, +0x20124, in 45 s). The game creates DirectSound, configures one
voice (0x40), and releases it -- four times -- then loops
`sub_001798D5 -> DirectSoundCreate (sub_00179837) -> sub_00179707 ->
sub_00178960` several hundred times on the main thread. That loop is the next
thing to read.

### Smaller things recorded

* D3D resource-lock recursion (`sub_0016B920 / B858 / B4F0 / B680`) overflowed a
  host stack once under `XBOX_KCALL_LOG=0`, on another thread; timing-sensitive,
  not seen without heavy logging. Not chased.
* `MmQueryAllocationSize` returns VirtualQuery's remaining region size (e.g.
  87 MB for a 4 KB buffer), so DirectSound's allocated-bytes counter goes
  negative. Harmless so far; wrong.

### State

Default launch 5/5 clean, draws 7,482..7,772, DirectSound live, nothing denied.
Intro video: plays including the animation, settled logo clean. Text clean.
Audio: APU initialised and its output now reaches XAudio2, but no voice is
started yet.

### Part 177, continued -- following the silence down

**DirectSoundCreate failed three times with E_OUTOFMEMORY.** Probed at the
create helper's return: `0x8007000E` x3, then `0x88780032` (DSERR_INVALIDCALL),
then "success" -- but no fifth construction happened: `DirectSoundCreate` handed
back the half-initialised global device left by the failed fourth attempt. That
is why every later buffer failed silently and no voice was ever started.

Traced down: `CDirectSound::Initialize` -> the internal looping buffer's
`SetBufferData(0x17F230, 64)` -> `sub_0017CB27` -> the APU scatter-gather heap's
Map (vtable 0x001A9BC0 slot 5, `sub_0017E458`) -> `sub_0017E31D` allocate ->
**free list empty, free count 0**. The heap is seeded by `heap->vtbl[1](0x7FF)`
from the APU init (`sub_0017A65D`), and slot 1 is `0x0017E453` -- a one-jump
thunk to `0x0017E291`, the real `Initialize(count)` -- **neither was in the
dispatch table.** The indirect call missed, the miss path returned eax=0, which
reads as S_OK, and init carried on with an empty heap. Recovered `0x0017E291`,
`0x0017E453` and `0x0017E424` (merge, slot 4) with `recover_batch.py`: kept,
3/3 clean. Now: heap initialised once, `DirectSoundCreate` S_OK first time,
**VOICE_ON for voices 0x40-0x46**.

On the way, two more kernel answers were wrong and are fixed: `MmQueryAllocationSize`
returned VirtualQuery's remaining region (87 MB for a 4 KB buffer) and now
answers from the heap ledger, page-rounded; `ExQueryPoolBlockSize` returned 0
unconditionally and now answers from the ledger. The contiguous-allocation
bridge also always reports failures now (its 16-line cap used to run out first).

**The voice processor never ran a frame.** Still silent, and a new `[VP]` report
(pipeline frames, voice-frames mixed, mixbin 0/1 peak, list heads, idle voices)
never printed: `se_frame` never ran. The APU frame thread starts paused and was
resumed only by the test tone and the HLE software mixer. xemu resumes it on the
VM state change to RUNNING; this port has none. Resumed at init: the VP runs at
exactly 1,500 frames/s (48 kHz / 32) and mixes the seven voices -- at peak
0.0000 so far; they are DirectSound's internal voices over 64 bytes of zeros.

**But running it stops the boot on the loading screen.** Draws rose to
55,000-90,000 only because the loading screen redraws every frame. The boot pump
(`Application_RunMainLoop` state 3) came back from `FEInit_Boot` with
`esi = 2`, then pumped a garbage object (`0x60000000`) forever. Found by a
held/running differential over the same probes:

* identical for 89 hits, then `sub_00080D40(widget, 2, 0)` returns **12 bytes
  short** for the third widget -- its `this->0x90->0x64` object at `0x00726584`
  has a **null vtable**, the indirect tail call misses, and an ITAIL miss does
  not pop the wrapper's `ret 8` ([[reference-itail-miss-no-esp-restore]]).
  `FEInit_Boot`'s epilogue restores esp from ebp, masking the drift, so the pops
  read the wrong slots.
* With the APU held that object is intact (vtable 0x00189AC8, the class whose
  slot 23 is `sub_0004C140`). With it running it is all zeros.
* Write-watch on it: hit #1 is its construction; every later hit is EA's audio
  stream thread (`sub_000151F0` -> ... -> `sub_00019040`, a float->PCM converter
  using the `fadd magic` trick, which stores the float back **into the source**).
  The mixer's four float buffers sit 0x840 apart from `0x00724B00`; the fourth,
  `0x007263C0..0x00726BC0`, **contains the object at 0x00726580**. Both are
  blocks of the title's own 54 MB heap arena, allocated by `sub_0011D7A0`: the
  object's block is filled at [0x00726580, +0x37500), then a block at
  [0x007263C0, +0x376C0) is filled and zeroed over it. The overlap exists in
  both modes; held, nothing writes into it.

Also noted: `sub_00019040` and `sub_0004C140` are recovered functions that
never received the x87 post-pass -- `fadd [m]` emitted as the popping register
form, `fnstsw` as a comment, a parity branch hard-coded `if (1)`. Another
instance of [[reference-relift-undoes-postpass-fixes]]: recovered code is born
without the post-pass fixes too.

**Default for now: the APU frame thread is held** (`XBOX_APU_RUN=1` runs it).
Everything else stays: DirectSound live, SGE heap initialised, voices started.
5/5 clean, intro animation plays, settled logo clean. Recovered `0x00119110`,
`0x00012210`, `0x000A3F5A` (EA audio service callbacks from the `0x205A6C`
table walked by `sub_00011370`) -- kept, clean, not the constructor.

**Next:** why two live heap blocks overlap at 0x007263C0 / 0x00726580 --
sub_0011D7A0's free-list handling, or a freed block still referenced -- and a
general fix for ITAIL misses (the lifter can take the enclosing function's own
`ret N` and emit it on a miss).


## Part 178

### Summary

With the APU frame thread running (`XBOX_APU_RUN=1`) the game produced about a
second of sound and then fell apart: tens of millions of ICALL misses per 5 s,
the kernel import table overwritten (`MEM32(0x187374)` = 1), `.text[0x000FB030]`
rewritten over and over, and a thread whose guest esp became `0x0000FFF4`. None
of it was the APU emulator. It was **EA's audio stream converter writing 16-bit
PCM from guest address 0 upward**, because of three translation defects -- one
of them a whole lifter bug class.

### How it was found

1. `XBOX_ESP_GUARD=1` named the first bad esp at `loc_001520EC`, right after the
   XAPI timer thread's `KeWaitForMultipleObjects` -- but nothing in that bridge
   writes esp. The broken esp was a *symptom*: the thunk table had been
   overwritten, so the call went nowhere.
2. New guard in `apu_shim.h`: every APU write into guest memory below
   `0x00214000` (the image) is reported with the base registers
   (`apu_phys_write_check` in `apu_core.c`), and every APU base register the
   guest programs is logged (`[APU] base register`). Result: **zero APU writes
   into the image**, all bases sane (0x0386xxxx..0x038Cxxxx). Not the emulator.
3. `XBOX_DIAG_WATCH=FB030:4` caught the writer: the EA stream thread
   (`sub_000151F0`), in `sub_00019040`, storing `0xFFFF8000` halfwords with
   `edi` walking through `0x000FB02E`.
4. Probe `0x00019040|[esp+0x10]<0x200000|...` showed the call:
   **dest = 0, count = 0x0072C710** (a pointer). 7.5 M samples from address 0.

### The three defects

**(a) Flags taken from the wrong predecessor -- lifter bug class.**
`translator.py` handed every block the flag state of the block *before it in
address order*. At `0x184C5` the only way in is `jge` from `0x184AD`
(`test ebx, ebx`), but the block before it in memory ends in `jmp`; the lifter
used its `add esp, 4` and emitted `if ((esp == 0)) goto loc_00018557;`. A
zero-length decode was never skipped, and the mixer called the converter with
garbage arguments. Fix: `_incoming_flag_states` -- a fixpoint over the CFG
using real predecessors; where predecessors genuinely disagree (MSVC
tail-merges a `jg` reached from `cmp eax,3` and from `cmp eax,4`), each
predecessor evaluates the needed condition into `_finXXXX_jcc` locals before
its terminator (`FIN_SETTER` in `lifter.py`). Also: a setter whose operands are
overwritten before block end gets a snapshot when a successor reads the flags
(backward liveness), and a block that overwrites incoming operands before
consuming them snapshots them first. Program-wide effect of the part-178 part
alone: 21 blocks corrected, 19 conflict blocks materialised, 162 lines.

**(b) x87 `fadd dword [0x1A9F3C]` collapsed to a register pop-add.**
The float-to-int magic constant never got added. `fixfpmem.py` repairs this
class, but it found ranges through the `Original:` header comment, and every
body added by `recover_batch.py` has none -- and its glob was `recomp_0*.c`,
so `recomp_recovered.c` was never even read. New
`tools/audit/add_origin_headers.py` restores the headers from a fresh full
translation (by address); `fixfpmem.py` now includes `recomp_recovered.c` and
resets its range per function (a function without a header used to be paired
against the previous one's bytes). 22 sites repaired, all paired exactly.

**(c) `cmp eax, 0x80000; sbb eax, eax` with no carry.** Part 177's `_cf` fix
lived in the lifter but had been rolled out to ~15 functions only, so every
other `sbb`/`adc` in the tree still read a never-assigned `_cf`, including
135 MSVC `neg; sbb` idioms (x != 0 -> -1). Here, every clamped sample became
-32768 (`eax = 0xFFFF8000` in the watch).

### Rolling lifter fixes into the tree without a relift

Relifting undoes post-pass fixes. New `tools/audit/apply_lifter_diff.py`
transplants the old->new diff of two **full scratch translations** hunk by
hunk: it matches each hunk by unique context (ignoring `RECOMP_LOC` /
`g_last_loc` lines), keeps dependent hunks together (each snapshot write with
its reads; all `_fin` hunks; declarations with either), and reports the rest.
Baseline = current recompiler with the part-177/178 flag logic switched off
(`tools/audit/baselines/make_baseline176.py`; args: SRC_TOOLS DST_TOOLS). Applied: **481 functions**; 258 not applied,
~220 of them because the tree already has an older equivalent fix at exactly
those sites (`_fcN_` snapshots from the part-flag-clobber pass, FPU-branch
rewrites, hand-written `_cf = (LO8(eax) != 0)` for `neg`).

Workflow (all output to the scratchpad, never gen/):

    python -m tools.disasm XBE -o W/disasm --text-only --force \
        --analysis-json ../xboxrecomp_output/ssx_analysis.json \
        --seed-functions W/seeds.json      # seeds = every dispatch-table address
    python -m tools.func_id XBE --functions W/disasm/functions.json ... -o W/funcid
    python -m tools.recomp XBE --all --split 1000 --gen-dir W/gen_new ...  (~20 s)
    (same with the baseline tools copy -> W/gen_old)
    apply_lifter_diff.py W/gen_old W/gen_new [--dry-run]

`XLIFT_FLAG_STATS=file` makes the translator list per function which blocks
changed and which were materialised.

### State

* Default launch (no env): **3/3 clean, draws median 7,942** (history
  7,576-7,872), exit 124, no crash, no .text corruption.
* NOT yet verified: text rendering by frame capture (sub_00178164, the
  part-177 text trap, received 15/19 flag hunks), and the audio run
  (`XBOX_APU_RUN=1`) -- the converter fix is expected to make the stream audio
  real and stop the memory sweep.
* Backup of gen/ before this part: `RE_NOTES/gen_backups/pre_part178/`.

### Next

1. Frame-capture the default launch; confirm text is clean.
2. `XBOX_APU_RUN=1` with `XBOX_ESP_GUARD=1`: expect no `[TEXT]` changes, no
   ICALL flood, continuous audio. If clean, make the APU run the default.
3. The 0x00726584 "heap overlap" (part 177) is probably this same converter
   defect -- re-test before chasing `sub_0011D7A0`.
4. Remaining program-wide lifter debt that could use the same transplant:
   x87 post-pass into the lifter, ITAIL-miss esp.


## Part 179

### Summary

Audio is **on by default**, the game plays the **whole attract movie** after the
EA SPORTS BIG logo and ends on the SSX Tricky title logo, the intro animation
decodes **cleanly in every frame** for the first time, and **controllers work**
(XInput, with a keyboard fallback). Three recompiler gaps fixed along the way:
XMM registers were still 4-byte floats in fresh lifter output, relifted bodies
used stale names and dropped the ftol argument, and 510 `switch` case targets
had never been translated.

### 1. The intro animation: two motion-vector bugs cancelling out

Part 178's flag rollout made the settled logo worse (glitch blocks from present
412 on). Bisection (`capcmp.sh` against the part-177 captures) pinned it on the
one semantic change in the decoder, `sub_00147440`'s `jge` at 0x14746B, whose
new translation is provably the correct one (flags from `test esi, esi`). The
old one never applied a negative motion vector.

It only looked worse because a second bug had been compensating: the sign of
every motion code is applied in the split fragment `sub_00146C96`
(`je 0x146C9A; neg eax`), whose flags come from `test eax, eax` in three
producer fragments, each of which then overwrites eax (`movsx`) before the tail
call. The consumer read `if (_flags ...)` -- never taken -- so **every non-zero
motion code was negated**. With both bugs, originally-positive codes were
dropped and negative ones applied with the wrong sign. `fixsplitflags.py` now
revives that branch (`SPLIT_CMP` at the test, before the overwrite); it had
never been applied there. Result: the animated section is clean in every frame,
not just the settled logo.

### 2. Sound: the stream codec chain

The APU-running corruption that survived part 178 was the same converter, now
called with `count` = a pointer because the stream mixer's **stack frame had
shifted 20 bytes**: its decode callback `0x00019950` was never translated, and
an ICALL miss at a cdecl site rewinds esp before the pushes and the caller's
`add esp, 0x14` pops them again. Recovering the chain -- `0x00019950`,
`0x0001A990`, `0x00012D30`, `0x00012D00` -- ended the corruption: 3/3 clean runs
with the sound chip running. Then the video object's `+0x10` method
(`0x000A37A0`) let the boot advance past the logo into the attract movie, whose
soundtrack plays (1,829 of 2,259 buffers audible in 50 s). **`XBOX_APU_RUN`
now defaults on** (`=0` holds it). The user reports the audio sounds right with
"a slight noise" -- not yet investigated (candidates: 3 x 1024-sample host
buffers underrunning, the resampler, the VP).

### 3. Controllers: XAPI's input entry points on the host

XAPI (and its USB/XID stack) is statically linked, so the title's
`XInputGetState` talks to USB hardware that does not exist. New
`ssx_recomp/src/xapi_input_hle.c` replaces the eight entry points (addresses from
Ghidra, `ret N` from the bytes):

    0x0017FF2C XInitDevices      0x00180916 XInputOpen      0x0018098B XInputClose
    0x00180997 XInputGetCaps     0x00180B89 XInputGetState  0x00180BFA XInputSetState
    0x00180C59 XGetDevices       0x00180C7B XGetDeviceChanges

on top of xboxrecomp's Windows XInput layer. The lifted bodies stay in
recomp_0009.c renamed `<name>_lifted`. Port 0 also reads the keyboard while the
window has focus (Enter Start, Space A, Esc/Backspace B, C X, V Y, Tab Back,
arrows); `XBOX_INPUT_AUTOPRESS=A@8,START@32` scripts presses for test runs.
The XPP ICALL miss (0x0017FCEC, XInitDevices' USB bring-up) is gone. The user
confirmed pressing A on a pad skips the video.

### 4. 510 switch cases that were never translated

Pressing A during the logo hit `sub_00084780`, a 25-way switch the disassembler
had ended at its `jmp [eax*4+0x84864]`, so all 14 case targets were ICALL misses.
New `tools/audit/switchtargets.py` decodes every `RECOMP_ITAIL(MEM32(r*4+T))`
table from the XBE (bounded by the switch's own `cmp r, N`): **77 tables, 63
with missing targets, 510 targets**. Batch 1 (128 + 47 never-translated callees
found by closure) kept; batch 2's first half crashes 3/3 in `sub_000A8EE0` with
`esi = 0x4B400000` (the float-to-int magic constant in an object field) --
isolating the offender with the new `recover_bisect.py`.

### 5. Recompiler: three gaps that broke every recovered body

* **XMM model.** The lifter still declared `float xmmN`, moved 4 of 16 bytes on
  `movaps` and emitted packed ops as comments. Part 47 fixed the *tree* with a
  one-off transform; everything recovered since was broken again. The lifter now
  emits `recomp_xmm_t`, lane-correct scalars (`.f`/`.d`, `movss` loads clear the
  upper lanes), per-width moves (`.x = MEMX`, `movlps/movhps` halves), and the
  runtime's `XMM_BINOP/SHUFPS/MINPS/MAXPS/CMPPS/UNPCK*/MOVLHPS/ANDNPS` plus
  bitwise, sqrt/rcp/rsqrt and `movmskps`. `comiss` conditions read `.f`.
* **Names.** Relifted bodies called `sub_X` where the tree says `Heap_Free` etc.
  (six undefined references this part). The lifter takes names from the tree's
  dispatch table when `XLIFT_NAME_MAP` is set; relift.py and recover_batch.py set it.
* **ftol.** Calls to `CRT_ftol_TruncateToInt64` now carry
  `g_ftol_arg = fp_top();`. One older recovered site lacked it and was fixed.
* recover_batch.py keeps each body's `Original:` header, runs fixfpmem on every
  splice, and `fix_scalar_simd` converts a whole function consistently.

### State

* Default launch (audio on): 3/3 clean, draws ~7,700, exit 124, no corruption.
* Plays: EA logo animation (clean), attract movie with music, ends on the title
  logo. A on a pad skips.
* Open: batch 2a offender; 250 more switch targets; `0x001033D0` (matrix code,
  now liftable); "slight noise" in audio; 3 remaining XInput-adjacent misses.

### Part 179, continued

* **Audio noise: pacing.** The voice processor ran on a wall-clock throttle
  (whole-millisecond waits, ~1,499.4 frames/s against the device's 1,500, time
  thrown away when behind) while `xa2_submit_samples` dropped a buffer whenever
  its 3-slot queue was full. Now the XAudio2 queue is the clock: the frame
  thread produces until 4 of 6 buffers are queued, then waits. 60 s run:
  **0 underruns, 0 drops** (new `[XA2]` counters). Video playback follows the
  audio clock, so the intro's frame timing shifted; capture comparisons now use
  a new reference (`base179`) and only the settled frames are comparable run to
  run.
* **Shared x87 stack.** 104 split fragments start by reading an x87 stack their
  predecessor filled; with a private `_fp_stack` per function they read zeros.
  All 2,064 private declarations removed; `_fp_stack`/`_fp_top` alias the
  per-thread `g_fp_stack`/`g_fp_top` (recomp_types.h), and the translator no
  longer emits them. 3/3 gate clean, intro frames clean.
* **The user plays during test runs.** XInput reads a pad whatever window has
  focus, so A presses (skipping cutscenes) reached gate runs and captures. That
  faked the "3/3 crash" of switch batch 2a (both halves then passed; the full
  set is in the tree and verified 3/3 clean) and an apparent regression from the
  shared x87 stack. `XBOX_INPUT_HOST=0` now ignores physical input; xbrun.py,
  recover_batch.py and the capture scripts set it.
* **Switch cases:** 193 of 510 recovered; the remaining 317 are going through
  `recover_bisect.py` in one pass.
* **Switch cases: done.** All 510 case targets of the 78 truncated jump tables
  are registered (`switchtargets.py`: 0 missing). One "target" was bogus:
  `0x00100100` came from reading a two-level switch's byte-index table as
  dwords; switchtargets.py now bounds a byte-indexed table by its largest byte.
* **x87 in the lifter.** `_lift_fpu` rewritten: correct st(i) operands,
  `fsubr`/`fdivr`, the `fi*` forms, pops for `fcomp`/`fcompp`/`fistp`, `fst`
  no longer pops, `fnstsw` via `FNSTSW_AX`, `fnstcw`/`fldcw` through
  `g_x87_cw`, `fistp` rounding by the control word (`x87_rint`), 80-bit loads
  and stores. `fixfppops.py` swept the same pops and operands into the existing
  tree. x87 top still drifts (-5,652 per report window), so some push/pop
  imbalance remains.
* **Mistake, recorded:** during a bisection I defined `g_x87_cw` a second time
  (kernel_bridge.c; the real one is xbox_memory_layout.c). Every later batch
  then failed to link and read as "reverted". recover_batch.py now prints
  multiple-definition / undefined-reference lines from the link.

## Part 180

### The post-skip freeze was missing functions, not the main loop

After START skipped the attract movie the title froze. Part 179 read the main
loop's label counts and blamed the loader poll. The run log said otherwise:
the press executed the **undetected stub `0x000823E0`**, and from then on
`Application_RunMainLoop` made three indirect calls per iteration through
`0`, `0x2000` and `0xFFFFFFFF` (~4.7 M misses per report window). The stub
returned without the function's epilogue, so the loop's `esi` pointed at
garbage.

* `0x000823E0` is a camera-mode switch (`this+0x160` = mode, calls
  `New_camnode`, `ret 4`); Ghidra knew it, the tree never lifted it. Recovered,
  3/3 clean.
* The skip then reached `0x000A3D00`, `0x00085360`, `0x00081010`: vtable
  methods nothing had disassembled. Recovered, 3/3 clean.
* Next run: five more (`0x000A7540`, `0x000A68B0`, `0x000A56A0`, `0x000810E0`,
  `0x000853B0`).

### vtsweep.py

`tools/audit/vtsweep.py` scans every non-code section of the XBE for runs of
dwords pointing into .text (method tables, callback tables) and lists the
unregistered targets, filtered to plausible function starts (known to Ghidra,
or preceded by ret / int3 / nop / jmp). 210 tables, **723 unregistered
targets**; the 26 tables holding the post-skip misses (the frontend screen
classes, mostly 27-slot vtables) account for 162. Recovering a whole table at
once replaces one test run per missing method. `0x0015CF26` (`_purecall`) is
always excluded.

### Test timing

Autopress times count from the first controller poll (1.4 s into the run).
The EA logo opens 6.0 s after that and the SSX intro 9.8 s after, so
`START@11` skips 1 s into the intro. Runs used `START@60` before, wasting
~50 s each. `tslog.py` (scratch) prefixes every log line with elapsed seconds.

### The title menu appears

After the recoveries above the skip no longer froze, but the screen stayed on
the intro's first frame while the frontend issued ~10,000 draws a second. Cause:
`d3d8_PresentFrame` copied the video player's guest framebuffer to the screen
on **every** present once it had been discovered, pasting the last video frame
over everything drawn afterwards. The copy now runs only while the player keeps
locking that buffer (`d3d8_NoteGuestFramebufferLock` from the LockRect hook;
idle after 30 presents, ~0.4 s). Log: `[FB] video framebuffer idle: presenting
rendered frames after 126 locks`. First capture of the title menu: "Start Game
/ DVD Content / Press START button / (c) 2001 Electronic Arts".

### Walking the menu

Timestamped walks (`START@11,START@16,A@24,...`) recovered, batch by batch,
each 3/3 clean:

| batch | what |
|---|---|
| 0x00084D50 (+0x00120D80) | title-menu method |
| 51 targets, vtable 0x00197AC0 + dtor 0x00033C20 | menu manager (50 of 57 slots untranslated) |
| 63 targets, 16 screen vtables 0x00188EC8..0x0019BC40 + fragments 0x00086A11/17 | screens after START |
| 16 targets, vtable 0x00196B98 | camera nodes (0x0007FDA0..0x00080A40) |
| 0x00083600, 0x000843E0, 0x001033D0, 0x00104040, stub 0x0007E680 | 3D menu |

START on the title plays the confirm sound (the user heard it) and enters the
3D frontend, which accepts A. Its frames are black: see below.

### Tool fixes

* `recover_batch.py` restored sources on a revert but never rebuilt, so the
  next walk still ran the reverted functions. It rebuilds now, and keeps each
  gate run's log (`gate_runN.log` in the batch folder).
* The intermittent `draws=0` boot (1 run in ~15) reverted a good batch with
  nothing else running. Open.
* `switchtargets.py` read tables at `VA - 0x10000`, true only for .text; the 10
  tables in the D3D section were read 0x80 bytes off. It maps through the
  section headers now (none were actually missing).
* `vtsweep.py` also covers pointers into D3D/DSOUND/XPP.

### The 3D frontend: vertex programs

After START the frontend draws indexed geometry (`ARRAY_ELEMENT16`, ~8 M
commands in 30 s) through a vertex program. The translator dropped the indices
and swallowed all transform state in mislabeled ignore ranges (`0x0394/0x0398`
are CLIP_MIN/MAX, not "TRANSFORM_EXECUTION_MODE"; `0x1E94` fell inside a
"combiners" range; `0x0B80..` are constants, not program).
`xboxrecomp/src/d3d/d3d8_vsh.c` has an NV2A decoder but its bit layout is wrong
(ILU field 4 bits wide, source registers in dword 0) against xemu's field table.

New `xboxrecomp/src/nv2a/nv2a_vsh_cpu.c`: decodes microcode from the hardware
layout and runs it per vertex on the CPU (paired MAC/ILU read-before-write,
paired ILU writes R1, R12 = oPos, MUL 0*inf = 0, ARL bias). The Xbox D3D
runtime folds the viewport into every program, so oPos is screen space:
x/y direct, z / CLIP_MAX, rhw = 1/w, triangles with w <= 0 dropped. The
translator now captures program memory, constants, start slot, execution
mode, clip range; collects ARRAY_ELEMENT16/32 (and, in program mode,
DRAW_ARRAYS, so split strips stay whole) and draws at END with the title's own
depth, blend and alpha-test state. `XBOX_VSH_CPU=0` disables it,
`XBOX_VSH_LOG=1` disassembles each program.

### The 3D frontend renders

The sweep (624 targets: every remaining undetected stub plus every vtsweep
target) was kept whole, 3/3 clean, no offenders -- after three closure rounds
(~55 min, almost all of it recompiling every gen file because each splice
rewrites recomp_funcs.h).

With the vertex-program interpreter live, an unattended walk
(`START@11,START@16,A@26`) shows: the full title screen art, the 3D
"SELECT MODE / Single Event" menu, and after A "PLAYER1 SELECT CHARACTER /
Eddie" with its 3D scene. No new code misses on the way. The 2D screens run
through a pass-through program too (the boot text still renders), so the
interpreter now draws everything in program mode: 45 s run = 425k program
draws, 36 M vertices, 28.5 M triangles, ~1 M dropped behind the eye, 0
fixed-function indexed draws. The viewport has to be mirrored into constant
memory (c[0x3A]/c[0x3B]) -- the runtime's appended screen transform reads it
there. Frame rate falls to ~40 presents/s in the 3D menu (CPU transform).

### Walking the frontend to a race

Walks with presses every 7 s now reach: Select Mode -> Select Character ->
Player1 Setup Character (Continue / Outfit / Board / Rider Profile / Trick
Book / User Name) -> Select Event (Race / Showoff / Time Challenge) -> Select
Difficulty -> Select Venue "Garibaldi" (preview video `data\video\gari.xss` on
the in-scene TV). A on venue select starts the race load: `hud.xsh`,
`loading.xsh`, `xboxload.big`, then loose-file probes `D:\ldmoderace.xsh`,
`D:\ldtrackgari.xsh`, `D:\ldridereddie.xsh` (not found; the names are inside
xboxload.big -- EA's loose-first lookup, probably benign).

* **Instrument, not game:** the "input held" log shared hle_trace's 24-line cap,
  so presses after the fifth vanished from the log and looked unread. Own
  budget now (400 lines). A "stall" at venue select was the walk running out
  of presses.
* **Instrument, not game (2):** the diag `threads` command faulted in a system
  DLL at t=140 s (2 MB-stride reads); walkdiag.py queries `loc` only.
* `vtsweep.py` rejected `0x00098C30` (venue select's handler) because the byte
  before it belongs to a jump table stored in front of the function. It now
  also accepts targets preceded by a code pointer or opening with a
  push/sub esp/mov reg,[esp+n] prologue, and skips DOLBY/ABORTFONT/$$XTIMAGE
  (not x86 data). 6 vtable methods recovered (0x4C220 0x98C30 0x9EDF0 0xDD560
  0xF9A90 0x132A80), 3/3 clean.
* `vtsweep.py --code-imm text.asm` lists unregistered code addresses used as
  immediates (`mov [g], fn` / `push fn`): callbacks registered at run time.
  The race loader's `0x0001B370` is stored to `[0x002062D8]` that way. 77
  found; recovering with 0x001074B0 via recover_bisect.
* recover_batch.py writes declarations to `gen/recomp_recovered.h` (included by
  recomp_recovered.c and recomp_dispatch.c) instead of `recomp_funcs.h`: a
  build after a splice is ~2 min instead of ~11 (one file instead of all of
  gen/).

### Into a race

`XBOX_INPUT_AUTOPRESS` gained a repeat form (`A@18+1.5x48`): the default
choices reach a Garibaldi race load in ~35 s. Recovered on the way: venue
select's handler `0x00098C30` (vtsweep missed it -- jump table in front of the
function) + 5 more vtable methods; 78 callbacks registered from code
(`vtsweep.py --code-imm`: the audio codec table 0x1AEB0/0x1B370/...); 10
switch cases in race-load code; fragment 0x000AE6BD; CRT math handlers
0x0015EFAB (atan2 special values) and 0x00160200.

**AI riders were spawned as human players.** `test al,al; jge` in
Race_SpawnRidersAndLoadAssets became `CMP_GE(LO8(eax) & LO8(eax), 0)`; `&`
promotes to int and RECOMP_SEXT (sizeof-based) never sign-extends, so tag 0xFF
read as 255 >= 0. Every AI rider took a Controller, the player got none, and
the race auto-paused "controller disconnected". Lifter `_make_condition` now
narrows every cast in a condition to the operation width (test/cmp/post-op
forms, signed immediates for byte/word cmp); apply_lifter_diff carried it to
159 functions and `fixnarrowsign.py` swept the rest; `sar` on bytes/words now
shifts the operand's own sign bit. The race now starts: intro screen with a
proper random AI field, live HUD, "GO!".

**The course is invisible: NaN camera.** Traced write by write
(`watchnan <va> [len]`, new in xbox_diag: reports only NaN stores, with the
value stored): course draws take their matrix from stride-0 vertex inputs
v10..v14 = NaN <- race camera matrix (race object +0xC0) <- FUN_000abd30's
rotation from a NaN camera record <- camera-script copies <- CRT atan2/acos
returning the indefinite NaN. Four real defects found in that chain, all
fixed:

1. `fxam` was a no-op, so the CRT's argument classifier (0x0015EEEE) dispatched
   on the last compare. Now `x87_fxam` sets C3/C2/C1/C0; g_fpu_cmp carries
   `0x1000|AH` for it (FPU_AH).
2. `rol`/`ror` on bytes used ROL32 (bit 7 went to bit 8 and was dropped).
   ROL8/ROR8/ROL16/ROR16; lifter width-aware.
3. `fnstsw ax` wrote only AH. It now writes AX; AL = 0x20 (PE, sticky on
   hardware). **AL = 0 blanked the title art and every 3D menu** -- the user
   caught it twice; the boot gate did not. `menucheck.py` now walks the
   frontend and fails on blank frames; recover_batch runs it after the gate.
   `XBOX_X87_COMPAT` / `XBOX_X87_STATUS_LO` switch these at run time.
4. **Callees never saw the caller's ebp on `call`.** Functions seed ebp from
   g_seh_ebp; tail jumps published it, calls did not. The CRT atan2 classifier
   (reached by `call` from 0x0015F33B) read a stale frame: garbage control word
   (`fldcw [ebp-0xA2]`), fxam results filed in the wrong slots. Translator now
   emits `g_seh_ebp = ebp;` before every call in functions with an ebp local;
   `fixebppublish.py` applied it to 20,353 call sites. After it: 0 float
   exceptions (were 18-20 per race).

Still open: the camera record stays NaN -- first NaN store now in
sub_00075DE0 copying from a rider-tracking array that sub_00078A20 fills;
the origin is further up (rider state). Gate 3/3 clean, menucheck PASS.

(Part 180, late additions.) 155 packed `sqrtps`/`rsqrtps`/`rcpps` sites computed
lane 0 only; now all four lanes. Removing fixfpret.py's 620 `fp_push(g_x87_st0)`
hand-offs broke the menu UI and was reverted (all 620 restored from
gen_backup_preebp). `menucheck.py` now compares against reference frames in
`tools/audit/menucheck_ref/` (good 0.1-13, broken UI 38-55, threshold 30) --
the first version measured lit area and passed a broken UI.

## Part 181 -- float returns across calls; the race camera; NV2A final combiner

**The x87 return hand-off hands callers the wrong value.** Since part 179 the
x87 stack is one per thread, so a callee's ST(0) is already on it; the
`fp_push(g_x87_st0)` hand-off pushes the callee's *last fp_push* on top. Any
callee whose result is computed after its last load gets it wrong -- e.g. the
angle wrapper sub_0001E600 (`a - 2pi*floor(a/2pi + .5)`) ends `fld 2pi; fmulp;
fsubr`, so every caller got pi. New `tools/audit/fpretcheck.py` rewrites the
620 sites as `_fpc = g_fp_top; <call>` ... `X87_RET(_fpc, site)`; `x87_ret`
(recomp_types.h) measures what the callee left. `XBOX_X87_RET`: 0 = old
hand-off (default for now), 1 = hand off only if the callee left nothing,
2 = use the callee's ST(0) and drop leaked slots (depth = before+1, as the ABI
guarantees); `|0x100` logs each site's outcomes, `|0x200` + `XBOX_X87_RET_SITES=
lo-hi` restricts the mode to a site range (diagnostics are out of line in
xbox_memory_layout.c, no full rebuild). Logged over menus + race: 38 sites got
a wrong value (pi instead of -1.57 / 2.51 / -3.11 ...); callees leak 1-5 extra
slots at ~50 sites; only two sites saw a callee leave nothing --

* **sub_000F9DF0** (hand-written float getter in recomp_stubs_unresolved.c)
  assigned g_x87_st0 without pushing. Fixed: pushes. This was why removing the
  hand-offs broke the UI in part 180.
* **sub_000FDDF0** (hand-written): popped the x87 stack a second time on every
  call (every frame) and skipped the block 0xFDEB0-0xFDEDE on a retired "jnp is
  always taken" convention. Rewritten from the XBE: the block eases fog density
  [ctx+0x15928] toward [ctx+0x15934] in reciprocal space. (Measured: the two are
  always equal in menus and race, so the block never changes anything -- the
  fog value was ruled out for "less geometry than a previous build".)

**Race with XBOX_X87_RET=2:** the camera is no longer inverted; the intro
fly-through shows the start gate, rider list, countdown and GO! with the start
structure. Then the riders fall through the course (the user's "players are
falling") -- next: collision / rider physics. Default (mode 0) still flips to
the upside-down sky after the start.

**Menus with mode 2:** the background light ribbons became wide sheets. Bisected
(`|0x200`) to one site, 0x000F7122 in TerrainNode_UpdateTrackSegmentProps: fog
near/far getters return 6000 / 6001 (`fld; fadd 1.0`); the hand-off gave 6000
for both, so `C / (2f^2 - 2n^2)` divided by zero and the prop fade was -inf+inf
= NaN -- which culled/hid most of each ribbon. Mode 2 computes what the CPU
does. So the thin look was an artefact; the real look depends on texturing:

* NV2A **final combiner** (CW0 = A*B + (1-A)*C + D) was ignored. The title
  uses 0x00000C00 (R0) and 0x150C0500 (V1.a*R0 + (1-V1.a)*V1.rgb: a fade via
  oD1), plus 1B080B00 / 150F0500 / 00000004 / 00000C0F (multi-texture / EF).
  `fc_eval` models the A*R0 + (1-A)*C forms exactly as diffuse *= A plus a
  specular add of (1-A)*C; fog state (SET_FOG_*) is tracked. Fogging on
  SET_FOG_ENABLE alone blacked out every 3D menu -- fog only reaches a pixel
  through the combiner.
* **Texture address modes** (0x1B08) were not tracked; every texture was
  clamped. Now honoured (NV2A 1-4 = D3D's values).
* Both are behind `XBOX_NV2A_P181=1` (default off) until checked against the
  real game: they change Select Mode (menucheck 24-29 vs 0-10). The shim's VS
  now takes a pre-transformed vertex's fog from specular alpha (D3D rule).

Default launch after this part: menucheck PASS (title 0.2, select_mode 9.7,
select_event 0.9, difficulty 2.0, venue 0.9), frames inspected by eye.
Next: decide mode 2 as default (it is the correct semantics) together with the
texturing work it exposes; then rider collision in the race.

**Update, end of part 181:** `XBOX_X87_RET=2` is now the default (the user asked
why the thin-ribbon build was kept: it only was because the menu references
came from it). The thin ribbons were NaN culling from the wrong hand-off, not
the game's look. `menucheck_ref/` re-baselined from an inspected mode-2 capture
(old set kept in `menucheck_ref_mode0/`); menucheck PASS at 0.0-0.8. Visible
mode-2 glitches in transitions (white streak frame after Select Mode, yellow
sheets during the intro fly-in) are open for the frontend polish pass, as is
`XBOX_NV2A_P181` (final combiner + texture address modes).

### Ground truth from xemu (end of part 181)

The user's xemu (shortcut in ssx_recomp/build) now serves as the reference.
`xboxrecomp/tools/audit/xemu/` launches it with a private copy of its config
(keyboard on port 1, F12 screenshots to ./shots) and drives it (`xk.py`).
A full run -- boot, menus, race -- is saved as `reference/xemu_frames/00..30`
(640x480). What it shows against our build:

* **The background "ribbons" are a textured ice cave.** Behind every menu stage
  is dark-blue rock/ice geometry with fog and light shafts. Our mode-2 build
  draws that geometry at full size (right) but untextured with bright green /
  blue vertex colours (wrong); the thin streaks of the old build were NaN
  culling. Needed: its texture (stage 0 not bound, or a later stage / combiner
  source) and the darkening.
* **The Select Mode centre panel** is teal with a repeating circle logo -- what
  `XBOX_NV2A_P181=1` (texture wrap modes) produces. Wrap modes confirmed.
* xemu shows **Select Number of Players** after Select Mode; our walk skips
  past it fast enough not to capture it.
* **Character select**: textured Eddie on a lit platform with other riders;
  ours has untextured grey riders and yellow/green planks.
* **Loading screen** fills location Canada / vertical drop 2300 m / course
  length 3150 m / rider Eddie -- blank in ours.
* **Venue select** plays a course preview video on the monitor.
* **Race**: letterboxed fly-over and rider cutscene, rider list, countdown in
  the start gate (ours: flat purple screen, black rider silhouettes -- world
  not drawn, riders untextured), GO!, chase camera behind Eddie; the rider
  descends on his own at 30-74 mph with no input.

Agreed plan with the user: polish the frontend (title -> venue select) first,
since those rendering gaps (texture binding, combiners, wrap modes,
perspective) are the same path the courses use, then return to the race
(riders falling through the course).

## Part 182 -- the NV2A pixel pipeline; memory exhaustion; flip presents; xemu RAM

**Register combiners -> HLSL.** New `xboxrecomp/src/nv2a/nv2a_psh.{h,c}` ports
xemu's `pgraph/glsl/psh.c` (LGPL) to HLSL, driven by the push-buffer state:
texture-shader stages (SET_SHADER_STAGE_PROGRAM 0x1E70: 2D/3D/cube, dot
products, dependent reads, OTHER_STAGE_INPUT 0x1E78), up to eight general
combiner stages (ALPHA/COLOR ICW/OCW, FACTOR0/1 per stage, CONTROL 0x1E60 with
its mux/unique-factor flags, input mappings, dot, mux, sum, blue-to-alpha) and
the final combiner (SPECULAR_FOG_CW0/1: A*B + (1-A)*C + D, EF_PROD, V1R0_SUM,
fog). Shaders are keyed by FNV-1a over the state (`nv2a_psh_key`), compiled
once with D3DCompile ps_5_0 and cached (2048). `d3d8_nv2a.c` is the draw side:
a pass-through VS takes the CPU vertex program's outputs (screen xyz + rhw, oD0,
oD1, fog, oT0..3 as float4, stride 116), rebuilds clip space (x*w, y*w, z*w, w)
so interpolation is perspective correct, and binds all four texture stages
with their address modes and filters (0x1B14 -> D3D11 filter).

* **The frontend ice cave is bump mapped.** Stage 0 is a normal map and the
  combiner dots it with a light vector packed into oD0; drawing oD0 as a colour
  painted the cave green and pink. With the port it is dark-blue textured rock
  as in xemu.
* **Fog** only reaches a pixel through the final combiner, so it is now exact
  (vsh fog factor per xemu; 1 when fog is off). Part 181's `fc_eval` and the
  `XBOX_NV2A_P181` switch are gone; texture address modes are always honoured.
* `XBOX_NV2A_PSH=0` falls back to t0*v0; `XBOX_NV2A_PSH_DUMP=<dir>` writes each
  generated HLSL; `XBOX_NV2A_PSH_SHOW=n` outputs one register (t0|t1|t2|t3|v0|
  v1|fog|r0|r1) as the colour -- the quickest way to see what a combiner input
  holds.
* Depth clip follows SET_ZMIN_MAX_CONTROL (the game uses CULL, param 1): the
  program path sets its own rasterizer state (cull none -- culling was already
  done on the CPU, DepthClipEnable from the register).

**Texture cache went stale.** The cache was keyed by (offset, format); the game
re-uses the same VRAM for different textures across screens, so booth and menu
panels showed old images. Entries now carry a signature of ~97 sampled dwords
(`tex_sig`), checked on lookup; stale entries are dropped.

**Allocations were failing.** The game carves a fixed 53.1 MB arena
(0x3519998) through sub_000B2980 -> sub_00151380 -> XPhysicalAlloc
(sub_00154565) and expects the rest of the 64 MB for itself; our kernel put
virtual-only allocations (NtAllocateVirtualMemory, worker thread stacks) in the
same physical range, so late contiguous allocations failed. New
`xbox_HeapAllocVirtual` allocates those top-down above the 64 MB GPU-visible
line; `xbox_HeapFree` does not recycle them into the physical pool. 0 failed
allocations over menus and race.

**Present at flips.** Presents used to fire on clears and on a fallback timer,
so partially drawn passes reached the screen (flashing half-frames). Now a
change of SET_SURFACE_COLOR_OFFSET (the game flips 0x03BA0000 <-> 0x03CCC000)
presents; clears and the timer present only while no flip has been seen for
250 ms (`pgraph_d3d11_flipping()`). **Exception:** while the guest video
framebuffer is active (`d3d8_GuestFramebufferActive()`), the timer keeps
presenting -- suppressing it stalled the intro videos (the user saw it). With
this the boot screens xemu shows ("Checking hard disk", "Autoloading", Basic
Controls) are now visible in ours.

**PFIFO acknowledgement order** is now: sample PUT/fences/producer ->
translate -> publish. `XBOX_PFIFO_ACK_LATE=1` publishes after translating; it
is correct in principle but slowed the game enough that scripted presses were
missed, so it is opt-in.

**Checked against real hardware (xemu RAM).** `launch.py -s` starts xemu with
QEMU's gdbstub; `gdb-multiarch` with `maintenance packet Qqemu.PhyMemMode:1`
dumps the 64 MB of physical RAM, and `xmem.py` (scratchpad) walks the page
tables (CR3 0xF000) to read guest virtual addresses. At character select:

* The camera node (vtable 0x00189C88, made by New_camnode 0x00080F10 from the
  record table at 0x1B8ED0 + idx*0x14 {name, start, end, fps, flag}) sits at
  pos (-649.5, -375, 100), angles (87.02, 0, -60), fov 25 -- identical to ours.
* The booth objects (vtable 0x00196880, matrix pointer at +0x90) form the
  carousel at y ~ -50000, radius 8530 -- identical to ours.

So the character-select camera and placement are correct; what is wrong there
is in drawing. Open: garbage frames (screen-filling shapes that change from
frame to frame while the draw list is identical), spikes/lines, and booth and
riders with black textures (the riders use 8-stage combiner shaders).

**Diagnostics added:** `XBOX_NV2A_DRAWLOG=F[:N]` (by present number) logs per
draw the depth state, texture stages, combiner stages, a v0 bounding box over
the indices really used with counts of far/NaN vertices, attributes, the vertex
program (once per hash) and its outputs. `XBOX_NV2A_SURFLOG=N` logs surface
changes. Probe MAX_SHOW 6 -> 16.

menucheck.py now samples frames 560+20k (the title appears earlier since flip
presents); `menucheck_ref/` re-baselined from an inspected capture of this
pipeline (part 181 set kept in `menucheck_ref_p181/`). PASS: title 0.1,
select_mode 5.6, select_event 0.8, difficulty 6.5, venue 5.6.

Not yet done: near-plane clipping (triangles with w <= 0 are dropped), mip
levels above 0, cube textures.

### Part 182, continued -- character select: three defects under one picture

Character select showed screen-filling shards that changed every frame while
the draw list stayed the same (989 draws, identical inputs). Three separate
defects, each found by comparing against xemu:

1. **Vertex ring wrap** (`d3d8_device.c`, `up_ring_upload`). The 4 MB
   dynamic ring mapped with `D3D11_MAP_WRITE` when it wrapped. D3D11 rejects
   that for a dynamic buffer; the draw was dropped and the next upload went to
   offset 0 with `NO_OVERWRITE`, over vertices that draws queued earlier in the
   same frame had not consumed. The program path uploads 116-byte expanded
   vertices, so character select (~1,000 draws) wrapped several times a frame.
   Now `WRITE_DISCARD` on wrap, and 32 MB. This one bug was the shards.

2. **Riders stood ~90 units low, boards stood up** (`sub_00107C30`, the pose
   sampler FUN_00107B80's body). Our RAM (new diag command `save <va> <len>
   <path>`) against xemu's: the skinned vertex buffers were full size in both,
   but every rider sat 20-110 units lower and every board 220 higher (in xemu
   the boards are still below the floor on this screen). The 12 rider objects
   (vtable 0x00196AF8, render slot 0x28 = 0x0007EEF0) matched field for field;
   their animation records (stride 0x378 from +0x7F0) did not -- record+0x108,
   the root bone's translation, was (-39, -92, 16) in xemu and the bind pose
   (~0) in ours. A write-watch showed only the bind-pose reset (sub_00107990)
   ever wrote it. At 0x108428 the generated code read
   `MEM8(esi + _fc14_eax + 0x14)`: the part-177 era `fix_flagclobber.py`
   snapshot of `fnstsw ax` (captured for a `test ah,0x44 / jp`) had been
   substituted into the *next* block's `test byte [esi+eax+0x14],5`, whose eax
   is the reloaded skeleton pointer. So when a bone's layer-0 weight was 0,
   the layer-1 translation copy read a random address and was skipped, while
   the rotation copy (which reloads eax) worked.
   `fccheck.py` (scratchpad) audits all 231 remaining `_fcN_` snapshots: a
   sound one feeds the first branch after it, in the same block, testing the
   captured operands. Five were mispaired, all verified against the XBE and
   fixed: 0x00032502 (`cmp eax,3` after a reload), 0x00144AB6 (`cmp al,5`
   after `and eax,0xF`), 0x00159690 and 0x0015D8F9 (`test eax,eax` on a
   freshly loaded function pointer, the second guarding `call eax`) and
   0x00108428. Three more snapshots are dead (no consumer).

3. **Intermittent partial frames** (a missing cave, one-frame shards) remained
   after 1 and 2. `XBOX_PFIFO_ACK_LATE` -- publish GET/fences only after
   translating -- measured over three runs each: early 8, 14, 9 bad frames per
   run; late 0, 0, 0. Its first measurement, which found no difference, was
   masked by defect 1. Now the default (`XBOX_PFIFO_ACK_LATE=0` restores the
   old order); it costs ~30% of the frames at character select, where the
   CPU-side vertex programs are now paced like a GPU. Intro video and menucheck
   unaffected (PASS: title 0.5, select_mode 9.5, select_event 5.9, difficulty
   4.0, venue 6.1).

Character select now matches the xemu frame: Eddie standing on the platform,
the booth, poster, the riders around him. Still different: the floor
spotlight (a warm pool of light under the rider in xemu, dark in ours).

### Part 182, continued -- pipeline state the translator ignored

A pass over what the NV2A applies per draw that `nv2a_pgraph_d3d11.c` never
forwarded, each checked against xemu's GL renderer:

* **Window clip** (0x02B4-0x02FC). The frontend draws with it set to the TV-safe
  area 30..609 x 22..457 and clears with it at the full surface; xemu's frames
  (and its framebuffer in RAM) have the 30/22-pixel black border, ours ran the
  scene to the edges. One inclusive rectangle becomes a D3D11 scissor for every
  game draw (`d3d8_SetWindowClip`, `d3d8_states.c`); other shapes fall back to
  a pixel-shader test (`nv2a_psh.c`, as xemu's psh.c). `XBOX_NV2A_WCLIP=0` off.
* **Near-plane clipping.** Triangles with any vertex at w <= 0 were dropped
  whole. Now w keeps its sign (clamped away from 0, as xemu's vsh-prog.c), the
  VS multiplies back by w, and the rasteriser clips in homogeneous space. Only
  non-finite vertices are dropped.
* **Point sprites.** D3D11 draws points as single pixels; points now expand to
  squares of oPts.x (SET_POINT_PARAMS_ENABLE) or SET_POINT_SIZE/8, and with
  SET_POINT_SMOOTH_ENABLE stage 3 samples the 0..1 sprite coordinate.
* **Stencil** (0x032C, 0x0360-0x0378) and **colour mask** (0x0358) applied to
  program draws; **face culling** (0x0308, 0x039C, 0x03A0) done by the
  rasteriser after clipping. NV2A coordinates go to GL as window coordinates in
  xemu, so NV2A's CCW front face is D3D11 FrontCounterClockwise = FALSE.
  `XBOX_NV2A_CULL=0` off. Character select and the title use a stencil shadow:
  front faces INCR, back faces DECRSAT, then a screen quad with blend
  ZERO/SRC_ALPHA (alpha 0.75) where stencil >= 1 -- without the stencil test
  that quad darkened the whole title and the floor around the rider.
* **Specular**: oD1 reaches the combiners only with SET_SPECULAR_ENABLE, its
  alpha only with SET_LIGHT_CONTROL bit 17; NaN colours become 1 (xemu). The
  game enables both at boot.
* `d3d8_states.c` dirty-checked its D3D11 state objects with XOR hashes that
  collide and left out the stencil ops and write mask; now FNV over every input.

**xemu screenshots are ~0.9x the rendered image.** Reading xemu's framebuffer
out of its RAM dump gives UI grey 99 -- exactly ours -- while its F12 screenshot
shows 89; the title's steady state is 232 in RAM terms (ours now) and 207 in the
screenshot. Compare against xemu screenshots with that factor in mind. (The
earlier title reference, 173, was the stencil-less darkening, not a match.)

**Push buffer vs xemu.** Decoding the NV2A methods around a texture bind in
both RAM dumps (`pbdump.py`, scratchpad): the command streams are the same, the
same textures bound the same number of times (offsets differ by a constant
0x325000). Differences left on screen are in how we render, not in what the
game asks for.

Still open on character select: xemu's floor under the rider is a bright,
warm grate; ours is the dark grate decal (texture 0x00B4F800 in our layout,
DXT1, opaque, blend SRC_ALPHA/INV, depth LESS, alpha test GREATER 2) drawn
over the lit booth floor. With the decal skipped (`XBOX_NV2A_SKIPTEX`) the lit
floor shows. In xemu the decal apparently fails the depth test there.

Also: `main.c` printed "could not raise the timer resolution to 1 ms" on every
launch -- a later edit had put the XBOX_LOWACC_LOG line between that `if` and
its `else`; the 1 ms period was always granted.

New diagnostics: `XBOX_NV2A_PICK=x,y` (every program draw covering a pixel,
with interpolated texture coordinates, colours, stencil/cull/alpha state),
`XBOX_NV2A_SKIPTEX=<hex>`, diag `save <va> <len> <path>` (guest RAM to a file),
SURFLOG now also logs window clip, specular/light control, zstencil clear.

menucheck re-baselined (refs from this pipeline, previous in
`menucheck_ref_p182b/`): PASS 0.0-2.1.

Race after these changes: countdown and GO! draw in the start gate; after GO!
the riders are in free fall above the mountain backdrop at 74 mph -- the
"riders fall through the course" problem, next.

### Part 182, continued -- why the riders fell through the course

Compared with an xemu RAM dump taken ~8 s into the race (`xemudump.sh`,
scratchpad; the rider objects are a list, player vtable 0x00189F24, AI
0x00188DA4): xemu's player rider sits on the course at (-1775, -6315, -71)
with a filled contact block at +0x550..0x680; ours was at z -28751 falling at
-3347/s with that block all zero. The level data were not the cause: the world
singleton 0x001FAF88 (+4 `.ltg`, +8 `.xbd`) and both buffers are identical to
xemu's apart from ~90 per-cell "touched" halfwords only a landed rider writes.

Probes: the riders run physics mode 1 (`Rider_PhysicsMode1_GroundRide`), and
`Terrain_SampleHeightAt` (0x13F480) returned -1.0 ("no ground") from the first
frame after GO!, for every rider. Its cell and sub-cell AABB tests are SSE
(`cmpltps`, `cmpnltps`, `andnps`, `movmskps`) and the generated code had

    ecx = 0 /* movmskps xmm2 */;

-- the mask was a constant 0, so every overlap test rejected. The lifter has
translated `movmskps` correctly for a while (lifter.py), but these ten sites
(0x13F5A1..0x1403DE, all terrain collision) predate that and were never
relifted. Patched with the lifter's expression. After it: ground heights come
back, the riders ride the course from the start gate (12 -> 74 mph), and the
spline contact query (`Terrain_QuerySurfaceContact`) starts hitting too.

**SSE/MMX registers are now per-thread globals.** Our rider still carried NaN
(0xFFC00000) at +0x7C0..0x7CC and +0x8FC where xemu has zeros. The write-watch
named `sub_00030CA0`: a lifter-split fragment whose first instruction is
`shufps xmm0,xmm0,0` on the xmm0 its predecessor loaded (`movss xmm0,[esp+14]`
at 0x30C9A, falling through into 0x30CA0). XMM registers were locals of each
generated function, so the fragment read an uninitialised local, `rcpps` made
NaN. Compiling gen/ with -Wuninitialized found 27 such functions (split
fragments at odd addresses; also 0x126B50, 0x128E90, 0x150E2A, ...). Same cure
as the x87 stack in part 179: `g_xmm[8]` / `g_mm[8]` are `__thread` register
files (`recomp_types.h`, defined in `recomp_manual.c`), `xmm0..7` / `mm0..7`
are macros for them, the 643 + 8 local declarations were removed from gen/,
and `translator.py` no longer emits them.

### Part 182, continued -- the race camera turned upside down

With the riders on the course the view was rolled (stands at the bottom,
signs upside down). The view matrix in the graphics context (`[0x1E3C7C]`
+0xAA0 and three copies) had "right" pointing up in ours and "up" = world +z in
xemu. It is loaded by the per-viewport loop at 0xDE03D from `[edi+0x90+i*0x80]`,
built by `sub_000ABD30` from a camera state `object+0x42C` = {x, y, z, yaw,
pitch, roll, fov, ...} (`FUN_000781d0`; object vtable 0x00195E90). xemu: yaw
1.26, pitch -0.24, roll ~0 (radians). Ours: yaw -12.6, pitch -53.1, roll -23.8
-- drifting without bound. The damping update (0x79020..) does
`angle += wrap(target - angle) / k` with the wrap in `sub_00078260`:
`d - floor(d/2pi + 0.5) * 2pi`. The CRT floor (0x15C826) sets rounding control
to "down" (cw 0x173F) and rounds with `_frnd` (0x15EC88) -- whose `frndint` was
emitted as the host `nearbyint`, i.e. round-to-nearest regardless of the
control word. The lifter emits `x87_rint` (honours `g_x87_cw`) nowadays; four
stale `frndint` sites and two `fistp` sites that truncated with a C cast were
fixed. After it the chase camera sits upright behind the rider, as in xemu.

Race now: countdown in the start gate, GO!, the rider rides Garibaldi from the
top (CLIFF / ICE / JUMP signs, the half-pipe, trees) at up to 74 mph with the
camera behind him. menucheck PASS (0.0-1.8).

### Part 182, continued -- riders rode "backwards"

The user saw the riders facing the wrong way. Two more xemu dumps (start gate,
just after GO) settled it: stance (`comp+0x1E4`, comp = rider+0x30+[[rider+0x30]+4])
= 1 and heading (`+0x4534`) = -pi on hardware too at the start
(`Rider_ApplyMotionUpdate` sets stance = the character's natural stance,
heading = wrap(stance ? pi : 0)) -- so not a physics difference; the board
frame and velocity matched as well. The player's body pose record
(rider+0x1080, 19 bones) differed: same layer yaw -pi, but xemu's root bone
had rotation z = base + pi and its translation rotated by pi, ours had neither.
The pose sampler applies the layer yaw only to a bone whose parent index is -1:
`cmp word ptr [edx+ecx+0x10], -1` (0x107E07, 0x10803B), emitted as
`CMP_NE(MEM16(...), 0xFFFFFFFFu)` -- a zero-extended 0xFFFF never equals
0xFFFFFFFF, so the root was never rotated and the body rendered mirrored
against the board. The lifter narrowed both operands to the operation width
only for signed relational compares; it now does it for EQ/NE and unsigned
compares too. Five sites in gen/ had the pattern (also 0x1596xx / 0x159Fxx /
0x15AFxx) and were fixed. Eddie now rides with his back to the chase camera,
as in xemu. menucheck PASS (0.0-1.3).

## Part 183 -- a launcher, any resolution, and the game's own widescreen mode

**Asked for:** a higher window resolution; 4:3 or 16:9; a launcher (Start game /
Settings) where Start is enabled only once an ISO is set; the HDD folder
selectable; settings in a file beside the .exe.

### 16:9 is a game feature, not a stretch
SSX Tricky reads the dashboard's video setting. Its linked XAPI
`XGetVideoFlags` (0x15299F) queries EEPROM setting 8 and returns
`(flags >> 16) & 0x5F`; `Video_GetScreenModeFromFlags` (0xFE580) maps bit 0
(widescreen) to screen mode 2 and bit 4 (letterbox) to 1; the mode is stored at
`g_screenMode` 0x1DD840 and handed to renderer vtable slot +0x94,
`Renderer_SetScreenMode` (0xF9EA0), which writes the frame layout at
this+0x530..0x540: mode 0 = full frame; mode 1 = letterbox (y0 = 0.125,
height = 0.75); mode 2 = anamorphic (x-scale +0x53C = 0.75, a wider view
squeezed into 640x480 for a 16:9 TV to stretch out). The intro sequence
(0x7C97F) re-applies it. Ghidra had 0xF9EA0 as `GfxContext_SetBlendPresetByMode`;
renamed (the C symbol in gen/ keeps the old name until the next full relift).
So 16:9 = report widescreen in the EEPROM and show the frame at 16:9 -- what a
console on a widescreen TV does; no game code is touched. Verified: in the
race the field of view widens and riders keep their proportions.

Found alongside: kernel.h's `XC_*` indices were one too high (language is 7,
video 8, audio 9 -- the title's own XAPI queries 8/9/10 for video/audio/
parental), and the stored video flags live in the high half (widescreen
0x00010000). The live bridge (`bridge_ExQueryNonVolatileSetting`) still answers
"not found" for everything except index 8 when the host sets it
(`xbox_SetVideoFlags`); the unused table-driven copy now defaults to 4:3.

### Rendering at any resolution
Everything the title draws used to land in the 640x480 swap-chain buffer. Now
(d3d8_device.c) it renders into an offscreen **scene target** at the chosen
size; `host_present` fits it into the window at 4:3 or 16:9 -- a plain copy
when sizes match, otherwise a linear scaling blit with black bars -- and all
three present paths go through it. Things in the title's pixel units follow the
scale (`d3d8_GetGuestScale`): the window-clip scissor, the shader clip regions,
the D3D8 viewport; `d3d8_GetBackbufferWidth/Height` now return the title's
640x480 (the space its vertices are in). The video player's 640x480 frames
are scaled into the scene. Captures (XBOX_D3D_DUMP etc.) read the scene target.
Window: resizable, Alt+Enter borderless fullscreen (DXGI's own disabled),
swap chain follows the client size, hidden cursor in fullscreen, per-monitor
DPI aware (manifest + SetProcessDpiAwarenessContext).

### Launcher and settings
`ssx_recomp/src/launcher.c`: Start game / Settings / Exit (keyboard and
controller); Settings = disc image + Browse, save folder + Browse, aspect,
resolution presets per aspect, fullscreen. The ISO is checked by mounting it
and comparing default.xbe's entry point with the one this build was
recompiled from (0x154218), so another game or region is refused with its
title named. Saved to "SSX Tricky.ini" beside the exe. Icon + manifest in
`ssx_recomp.rc`.

**Launch modes (important for tooling):** no args = launcher; `--play` = start
with the saved settings; `--direct`, an ISO argument, or **redirected stdout
(pipe, file or NUL -- every harness)** = the old behaviour exactly (ISO beside
the exe, 640x480, 4:3, hdd beside the exe); `--launcher` forces the launcher.
Test overrides in direct mode: XBOX_RENDER=WxH, XBOX_WIDESCREEN=1,
XBOX_FULLSCREEN=1.

**Verified:** menucheck PASS in direct mode (0.0-3.5); launcher -> Settings ->
Start boots the ISO at 1440x1080; 16:9 1920x1080 walk and race captured, 0
CRASH; fullscreen 2560x1440 pillarbox, Alt+Enter back to a 1280x960 window,
and an odd-shaped window all scale correctly.

**Still open:** race timer boxes, missing mipmaps (more visible at high
resolution), cube maps, AI riders at the start, character-select floor decal.

### Part 183, continued -- menu bar, mipmaps, anisotropy, MSAA, controls, log

**Closing the window did nothing.** WM_CLOSE set `s_host_window_close_requested`,
which nothing read; and the window belonged to whichever thread created the
device and was only pumped inside a present, so during loads it did not
answer at all. The window now has its own UI thread (`host_ui_thread`,
d3d8_device.c) that always pumps; WM_CLOSE / Game > Exit call
`d3d8_HostExit` (flush stdio, TerminateProcess -- the title has no quit path
of its own). Measured: closed in 0.09 s. Menus and window drags no longer
stall the renderer either.

**Menu bar** (ssx_recomp/src/hostui.c through `D3D8HostUiHooks`): Game
(screenshot F12 -> Screenshots\*.png via WIC, open screenshots/save folder,
open log, exit), Video (fullscreen Alt+Enter/F11, window size presets,
textures, anti-aliasing, frame rate in the title), Input (Controls...), Help.
Hidden in fullscreen; game input is suspended while a menu is open.

**Mipmaps (a fidelity fix).** Only level 0 was ever uploaded and the mip
filter was forced off, so distant snow/ice aliased into speckle. tex_upload
now uploads the whole chain from guest memory (levels contiguous after level
0, each swizzled on its own, DXT levels >= one block, as xemu lays them out),
the D3D8 layer locks/uploads any level, and the sampler follows NV2A: min
filter 3-6 -> mip point/linear, LOD bias (TEXTURE_FILTER 12:0, signed /256),
min-LOD clamp (CONTROL0 29:18 >> 8), the texture's own anisotropy (CONTROL0
5:4). The D3D11 filter table gained the three missing linear-mip cases.
`XBOX_NV2A_MIPS=0` restores the old upload for comparison. Race frames:
speckle gone, matches xemu's smooth snow. menucheck 0.4-2.3.

**Anisotropic (option)** 2-16x for linearly filtered textures
(`d3d8_SetAnisotropy`). **MSAA (option)** 2/4/8x: multisampled scene target
resolved into scene_tex; switchable live (rebuilt between frames); video
frames go through the blit when multisampled. Verified in the race at
1440x1080 4x/16x, stencil shadows intact.

**Controls** (controls.c): every Xbox control bindable to a key and (buttons,
triggers, D-pad) a controller input; sticks pass through. Defaults keep the
part-178 keys plus Q/E triggers, R/F Black/White; controller LB = White,
RB = Black (as xemu/Cxbx; the runtime had them the other way round).
[Keyboard]/[Controller] sections in the .ini; Controls window from Settings
or Input > Controls, live.

**Log file** option ("SSX Tricky.log", stdout+stderr), and a crash message
box in launcher mode only (never in tests) pointing at it.

**No language option:** the game never queries the Xbox language (setting 7
is not read anywhere); the text file is chosen by a hardcoded `push 0`
(American) at 0xAE104 into 0xA9F20, and the USA disc carries only
american.loc (british.loc is referenced but absent). Part 183's earlier
claim that it reads the language like widescreen was wrong.

### Part 183, continued -- the race timer, and a race from ~20 fps to 60

**Race timer showed boxes.** The HUD time is formatted by `wsprintfA`, and
19 of its handlers were one-line "not detected" placeholders in
`recomp_stubs_unresolved.c`, so the string stayed garbage. Recovered with
`recover_batch.py` (tag `wsprintf183`, backup in `RE_NOTES/recover_batches/`)
and the placeholders removed. Times now read `0:17.71`.

**Frame rate, in the order the causes were found** (race, 1440x1080):

| Cause | Fix | Race fps |
| --- | --- | --- |
| (start) | | 17-27 |
| Texture cache: linear list, evicted in-use entries | hashed cache, 2048 entries / 1024 buckets | ~25 |
| NV2A vertex programs interpreted on the CPU per vertex (render thread ~100%) | GPU vertex shaders: `nv2a_vsh_hlsl` (d3d8_nv2a_vsh.c) emits HLSL mirroring `vshcpu_run`; CPU kept for point sprites and programs that write constants; `XBOX_VSH_GPU=0` forces CPU | 45-52 |
| (white shards on the GPU path) up-ring wrapped between a draw's streams | `d3d8_UpRingReserve(total, pieces)` | |
| Windows timer-queue timers fire on the 15.6 ms clock grid whatever the resolution; the frame limiter re-arms a one-shot timer every frame | one TIME_CRITICAL timer thread with exact QPC due times (kernel_sync.c); KeTickCount from QPC | ~50-58 |
| vblank raised by the pump every 16 iterations (slower as scenes got heavier) | 59.94 Hz vblank thread on an absolute schedule (`XBOX_VBLANK_PUMP=1` = old) | |
| pump Sleep(1) between batches while the game waits on fences | SwitchToThread while PUT moves, Sleep only when idle | |
| critical sections: spin-then-shadow lookup | lock-free shadow publish; Enter blocks | |
| Present waiting for the monitor | sync interval 0 by default (`XBOX_VSYNC=1` waits) | avg ~53 |
| **ring-wrap tail dropped** (below) | walk the tail to the jump | **60.0** |

**The frame model** (`Application_FrameTimerCallback` 0xB26B0): after
`Application_TickFrame`, `acc += target - elapsed` (target 16.667 ms,
elapsed from KeTickCount), clamped at -2 x target; the next one-shot delay is
`(int)acc`, and at 2 ms or less it ticks again at once. TickFrame (on the XAPI
timer thread) only signals the frame event; the main thread wakes, simulates
and submits for ~9 ms, then waits on handle 0x48000001 again. The re-arm
alternates between two KTIMERs.

**The hitches.** At ~53 fps the histogram showed 5-10 present intervals over
40 ms every 2 s while nearly every other interval was 16 ms. A stall-only
profile (`XBOX_PROFILE=...,stall`) found both threads *idle* in the hitches,
so it was not work. Timestamped waits, timer fires, surface switches and
push-buffer batches (`frametimeline.py`) showed the game thread waking every
16.6 ms throughout, but two frames in a row with no render-surface switch,
always right after a ring wrap (`03FE1F04 -> 03E090DC wrap`).

At a wrap the title writes a jump to the ring base where it stopped
(`0x03DF8001`), continues at the base, and moves the context's segment limit
(+0x04) to the new segment. `nv2a_live_pb_tick` computed the tail as
`limit - last_wp` after refreshing the limit, so it was always 0: everything
between the last sample and the jump -- up to ~20,000 dwords, part of a frame
and often its `SET_SURFACE_COLOR_OFFSET` -- was never translated. With one
switch lost, the next frame set the buffer the translator already believed
current, so it looked like no switch either: two frames unpresented, a 50 ms
hitch. Fix (`live_pb_tail_len`, nv2a_live_pb.c): walk commands from the last
sample to the jump; the old limit is no bound (the last command can run
~200 bytes past it); a count-0 method (`0x40001800` between draws) is a
no-op, not the end; a tail with no jump is not parsed. Every wrap in a race
now ends at `jump 0x03DF8001`. Since this dropped state and draws on every
wrap in every scene, it may also explain occasional one-frame state glitches
seen before.

Result: race 60.0 fps with 0-1 hitches per 2 s; the race clock advances
2.00 s per 120 presents. menucheck PASS (0.4-1.5).

**Not done, measured:** MinGW's emulated TLS (`__thread` registers ->
`__emutls_get_address`) is ~20% of the game thread; GCC 13 has no
`-fno-emulated-tls` -- fixing it means a per-thread register block reached
through the TEB. No longer needed for 60 fps.

**Test hygiene.** Audit scripts started with `taskkill /IM "SSX Tricky.exe"`,
which also closed a game the user was playing -- looking to them like the
game dying at its hard-disk check. `tools/audit/stale.py`
`kill_test_instances()` stops only instances whose parent is a script host.

**Diagnostics** (tools/audit/README.md): `XBOX_FPS_LOG`, `XBOX_PROFILE` (+
`profsum.py`), `XBOX_WAIT_LOG=N`, `XBOX_TIMER_LOG=2`, `XBOX_FLIP_LOG`,
`XBOX_READ_STATS` (+ `frametimeline.py`).

### Part 183, continued -- _ftol never popped, and the distant-texture report

**`CRT_ftol_TruncateToInt64` (0x0015CA68) left its argument on the x87
stack.** The helper converts ST(0) through its own private stack
(`g_ftol_fp_stack`, seeded from `g_ftol_arg`) and never popped the caller's
shared one (`g_fp_stack`), where the real `_ftol` pops. Every one of the 539
call sites therefore left one value behind. Harmless in code that discards
the stack; wrong in code that keeps values there across the call. The course
tessellator `sub_000F8920` (called from `sub_000F8A30` <-
`BoardMesh_DrawAttachedPatches`, every frame) keeps u/v in ST(0)/ST(1) across
three ftol calls, so its lerp started from the last two ftol arguments -- the
lightmap step (256, 0) -- and wrote texture coordinates of 256.143 next to
0.143: the snow tiled 256 times across a triangle (the moire on every course
surface) and the red markings became fine concentric lines. Found by dumping
the terrain vertices (`XBOX_NV2A_DRAWLOG_VERTS`) and write-watching one
texcoord (`XBOX_DIAG_WATCH=234818C:4`). Fix: `g_fp_top++` in the helper's
entry, which covers the `fp_top()` sites, the `g_x87_st0` sites (their callee
pushed the value) and the tail-jump sites. Also fixed by it: the AI riders
race (positions change; the player was always "1st"), and the speed blur
appears. menucheck PASS, race 60 fps.

**"Textures wrong at a distance, fine up close" (user report).** Ruled out:
mip data (every level of the race's DXT1/DXT3/R5G6B5 textures decoded from a
RAM dump is correct -- tools: `savemem.py`, `XBOX_TEXUP_LOG`, a mip-sheet
decoder), LOD bias/clamps (same as xemu), and the w-buffer (CONTROL0 is
`00110001` only during start-up; races run `00100001`, a fixed-point
z-buffer). The texture modes in use are all 2D (`XBOX_NV2A_TEXSTATS`): no
cube maps anywhere from boot to the race. What it is: **distant fog is ~10x
too weak.** The terrain program (C1E8DC9E) computes its own exp2 fog into
oT3.w, `2^-(dot(pos, v9) * c99.w)^2`, with `v9` a per-object constant
attribute (like the matrix in v11-v14), and the final combiner lerps to the
fog colour c98 (192,192,224) by T3.a. At an eye depth of 18,800 ours gives
0.976 (unfogged); xemu shows the valley at the fog colour. The far valley
also sits on the far plane (z 0.999-1.007), so without fog it speckles
between drawn and clipped pixels. Next: trace where v9 / c99.w are computed
-- CPU-side game math, the likeliest place for another translation bug.

Particles: the 160-point trails (additive, stage-3 sprite texture) take their
size from the program (`max(..., 1)` px, 1-3 px); `SET_POINT_PARAMS` is now
recorded (A = (64/480)^2, scale 64) but, as in xemu, not applied to program
sizes -- the missing mist is not these sprites.

Diagnostics added (all off by default): `XBOX_NV2A_TEXSTATS`, `XBOX_TEXUP_LOG`,
`XBOX_NV2A_PRIMLOG`, `XBOX_NV2A_DRAWLOG_MODE=N`, `XBOX_NV2A_DRAWLOG_VERTS=N`
(now with constant registers), and XBOX_NV2A_PICK works on the GPU path by
taking the CPU path in the DRAWLOG frames.

**Release build:** MinGW's `xinput` import library is XInput 1.3, missing on
stock Windows 10/11; linked `xinput1_4` instead (xboxrecomp/src/input and the
port's CMakeLists).
