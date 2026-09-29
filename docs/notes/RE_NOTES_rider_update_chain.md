# RE notes: Rider's Update vtable chain — a thorough dead end, honestly documented

Follow-on from the standing open question "who writes the score at rider `+0x5710`?"
(see `RE_NOTES_race_hud.md`/`RE_NOTES_game_data_archives.md`). This note is the result
of a dedicated deep-dive into `Rider_ConstructBase`'s real polymorphic vtable, tracing
the per-frame Update dispatch chain as far as static analysis can follow it. **16
renames, real architectural understanding gained, but the trick-scoring/physics logic
itself is confirmed to live elsewhere** — this note exists so a future session doesn't
have to re-walk the same path.

## Finding Rider's real vtable

`Rider_ConstructBase` (was `FUN_00036490`) sets dozens of pointers on `param_1` before
`*param_1 = &PTR_FUN_001886e4` — that assignment (line ~72 of the function) is the
*actual* polymorphic vtable; everything set earlier are per-subsystem **tuning-data
pointers** (raw float constants, confirmed by reading one via `/read_bytes` — not code
addresses), not vtable slots. Easy to mistake for vtable setup at a glance given the
sheer number of similar-looking assignments.

## The vtable: 11 real function-pointer slots, then unrelated field-offset data

Read `0x001886e4` directly. Slots 0-10 are real code addresses; slot 11 onward are
small ascending offset values (`0x18`, `0x34`, `0x50`, `0x60`...) mixed with negative
adjustor-looking values — **not more vtable entries**, a separate data table (likely
field offsets for the same debug-serialization system `DebugBuffer_Write` feeds, given
the ascending-offset pattern). Didn't chase that further; lower priority.

| Slot | Function | Role |
|---|---|---|
| 0 | `Rider_ScalarDeletingDestructor` | destructor pair, standard shape |
| 1 | `Rider_UpdatePhysicsState_Thunk` → `Rider_UpdatePhysicsState` | **CORRECTED** (was mislabeled "Rider_BaseDestructor→Rider_TeardownSubobjects"): an adjustor thunk to the per-frame physics/state update, NOT a destructor. The real destructor is slot 0 (`Rider_ScalarDeletingDestructor`), which calls `Rider_DestructUnlinkFromRegistry` + the real `Rider_TeardownSubobjects` (`0x31f10`). See `RE_NOTES_terrain_collision.md`. |
| 2 | **`Rider_UpdateWorldSpaceMarker`** → `Rider_DrawWorldSpaceMarker` | AABB-culled floating nameplate/marker draw above the rider |
| 3 | `Rider_CompareThresholdGE` | `this+0x20 <= other->vtable[0x14]()` predicate |
| 4 | `Rider_CompareThresholdEQ` | same shape, `==` instead |
| 5 | **`Rider_GetThresholdValue`** | getter for the same `this+0x20` field slots 3/4 compare |
| 6 | `Rider_GetSubsystemFlags` (low confidence, generic `this+0x24` getter) | — |
| 7 | **`Rider_UpdateSubsystems`** | the master per-frame update dispatcher |
| 8 | **`Rider_ResetSubsystemBuffers`** | companion post-update buffer-clear pass |
| 9 | `Node_CheckField0x14` (resolved via the `Node`/`NodeBase` investigation, see `RE_NOTES_node_base_class.md`) | shared base-class predicate, not Rider-specific |
| 10 | `RaceState_NullHandler` (already named, from earlier session) | no-op placeholder |

**Update (later session):** resolved slots 2, 5, and 6, closing out the
vtable — every slot is now either named or (slot 6) named with explicitly
flagged low confidence. Slots 2 and 5 were still unanalyzed `LAB_` labels
(never even disassembled) until this pass. **`this+0x20`** (the field
slots 3/4/5 all revolve around) is confirmed to be the `NodeBase` instance-ID
field (see `RE_NOTES_node_base_class.md`) — meaning slots 3/4/5 together form
a small "compare/get by instance ID" family, most plausibly used by code
like `TrickTrigger_Update` (already documented as walking a list "looking for
a rider/player object matching the tracked target") to identify a specific
tracked rider generically through the vtable rather than a hardcoded type
check. **`Rider_DrawWorldSpaceMarker`** (slot 2's real target) is a
substantial, previously unread function — builds a height-offset AABB around
the rider, does a graphics-device visibility/culling check (same vtable+0x158
pattern `Camera_UpdateViewTransform` uses), and issues D3D8-style draw calls
plus a glyph/text draw matching the HUD rendering module's call shape. Reads
as the floating nameplate/rank marker drawn above each rider in multiplayer
or replay contexts. 4 renames this pass.

## Update: the shared slot family isn't Rider-specific — confirmed across 4 classes

Per an explicit instruction to keep finding "essential functions," checked whether
other already-documented Node-derived classes (`TrickTrigger`, `Boost`, `Fence`)
had the same kind of unresolved vtable slots Rider's did. They did, and the
answer reshaped how slots 3/4/5/6/10/11/14 should be understood: **read
`TrickTrigger`'s vtable (`0x001894f8`) directly and found slots 3/4/5/6 are
byte-identical addresses to Rider's own slots 3/4/5/6** — not a coincidence.
Checked `Boost` (`0x00189880`) and `Fence` (`0x00189b38`) too: **same result,
13 of 16 slots byte-identical across all four classes**, differing only in
each class's own destructor, Update, and (for some) one or two other genuinely
class-specific overrides.

**This means the earlier `Rider_CompareThresholdGE`/`Rider_CompareThresholdEQ`/
`Rider_GetThresholdValue`/`Rider_GetSubsystemFlags` names were mis-attributed**
— accurate in behavior, wrong in implying they were Rider-specific. Corrected
to a generic `Node_` prefix: **`Node_CompareThresholdGE`/`Node_CompareThresholdEQ`/
`Node_GetThresholdValue`/`Node_GetField0x24`**, plus the newly-found
**`Node_GetField0x28`** (also shared) and two massively-reused constant stubs,
**`Node_StubReturnFalse`**/**`Node_StubReturnTrue`** (~90-100 xrefs each — a
sibling pair to `InputDevice_StubReturnFalse`/`RaceState_NullHandler`).

**Each class's own distinct slots, read and named:**

- **`TrickTrigger_UpdateStub`** (slot 7) — a multi-level chain of stripped
  debug-marker writers, entirely compiled out in retail, same phenomenon as
  `Rider_UpdateSubsystems`'s stripped subsystem calls.
- **`Boost_Destruct`**/**`Boost_Update`**/**`Boost_UpdateStub`** —
  `Boost_Update` (slot 1) is genuine, substantial per-frame logic: walks the
  broad-phase overlap-pair list (same traversal shape as the already-documented
  `SweepPrune`/`OverlapManager` system), checks each overlapping object's type
  ID against `0x3ef` (the confirmed Rider/Player type), and applies the boost
  via a vtable callback on a genuine match. The actual "does touching this
  trigger boost the rider" logic.
- **`Fence_Destruct`**/**`Fence_Update`**/**`Fence_UpdateStub`**/
  **`Fence_UpdateWorldSpaceMarker`** — `Fence_Update` (slot 1) runs two
  countdown timers that call `New_Fence` on neighbor segments when they
  expire, matching `Fence_Construct`'s already-documented neighbor-linking
  fields exactly. `Fence_UpdateWorldSpaceMarker` (slot 2) is Fence's own
  override of the slot Rider/TrickTrigger/Boost just inherit as a no-op —
  builds an AABB and does a visibility-gated device transform call, the same
  `Camera_UpdateViewTransform`/`Rider_DrawWorldSpaceMarker` pattern.

Several of these were still fully unanalyzed (`LAB_` labels or "no function
found" regions) before this pass — the same "never auto-analyzed, needs
`/disassemble_at` + `/create_function`" pattern that's recurred throughout this
whole project. 15 functions touched (11 new, 4 corrected), all verified live.

**Takeaway for future sessions**: when checking one class's vtable slot family,
check sibling classes' vtables too before naming a shared slot with a
class-specific prefix — this generic Node-level virtual-method footprint likely
extends to some or all of the remaining ~20 level-script node types
(`Roller`, `Cracked`, `Debounce`, `Timer`, `Counter`, etc., see
`RE_NOTES_level_script_system.md`), not individually re-checked this pass.

## Update: extended to Roller/Cracked/Timer/Debounce — footprint confirmed, 2 old guesses corrected

Followed the takeaway immediately in the next pass. Read all four vtables
(`Roller` `0x1897a0`, `Cracked` `0x189ba8`, `Timer` `0x189308`, `Debounce`
`0x189298`) — **the shared `Node` slot footprint holds in every one** (slots
3-6 and the 10-15 tail byte-identical wherever not class-overridden). Named
each class's own Update (slot 1) and Destruct (slot 0), 8 renames:

- **`Roller_Update`** — real per-frame float math advancing the rolling
  obstacle's rotation/translation, gated on a vtable+0x2c condition.
- **`Cracked_Update`** (supersedes the statistical guess **`New_Script_7`**) —
  countdown timers; on break, resolves the attached script via
  `ResourceContext_GetTableField(cmdRecord, 4)` (the same column
  `TrickTrigger_Update` uses) and spawns it via `ScriptVM_CreateByHandle`,
  falling back to `New_DeadNode` if none attached. The "cracked ice breaks →
  scripted event fires" logic.
- **`Timer_Update`** (supersedes **`New_Script_6`**) — same
  resolve-and-spawn-script pattern at countdown expiry, plus a second
  independent countdown firing the expiry notify. Both supersessions are the
  *same misleading-tag story* as `New_Script_9`/`TrickTrigger_Update` from an
  earlier session: the statistical pass tagged these "New_Script" because
  spawning a Script is what the node does *when it fires* — the allocation is
  the effect, not the function's identity.
- **`Debounce_Update`** — minimal one-countdown-then-notify; was a completely
  unanalyzed region (not even a `LAB_`) before this pass.
- Plus the four standard-shape `_Destruct` slot-0 functions
  (`Cracked_Destruct` notably also resets an embedded mini-vtable at `[0x10]`,
  the same idiom as `CameraShakeMode`).

**The pattern-completion payoff**: with TrickTrigger/Boost/Fence/Roller/
Cracked/Timer/Debounce all read, every one of the *simple* level-script node
types now has its Update and Destruct verified — and three of them
(TrickTrigger, Cracked, Timer) turn out to share the exact
`ResourceContext_GetTableField(cmd, 4)` → `ScriptVM_CreateByHandle` idiom for
firing their attached scripts, i.e. **column 4 of the command-record table is
now confirmed as the standard "attached script handle" field across node
types**, not a TrickTrigger quirk.

## Update: swept the remaining node types — UVScroll/TexFlip/Movie/ZBoost/AnimObject family/Particle/cMeshAnim

Continued the same technique across every remaining level-script node type
with a real vtable. All 18 renames standard shapes (destructor + real
per-frame Update logic), no individual surprises worth a deep write-up here
— but two things worth recording:

- **`Movie_Destruct`** confirms the already-documented "releases a video/movie
  handle at `[0xb]`" behavior from `Movie_Construct`'s notes directly (calls
  `FUN_000f6850(param_1[0xb])`).
- **`AnimObject_NotifyIfFlagged`** (slot 2) is shared byte-identically across
  `AnimObject`/`AnimDelta`/`AnimTexFlip` — yet another confirmation of the
  same base-class-vtable-sharing pattern, this time within the `Anim*` family
  specifically (on top of the broader `Node`-level sharing already
  established).
- Several were still unanalyzed `LAB_` labels or fully un-typed data regions
  (`TexFlip_Update`, `AnimObject_NotifyIfFlagged`, `AnimDelta_Update`,
  `AnimTexFlip_Update`) before this pass — the same recurring "never
  auto-analyzed" story.

**A genuinely interesting side-find while locating `CrowdBox`'s vtable**:
`CrowdBox_Construct`'s own vtable pointer is the *exact same address*
(`0x00189cb8`) as `CameraAudioPanningMode`'s vtable (see
`RE_NOTES_camera_system.md`/`RE_NOTES_node_base_class.md`) — not a
coincidence or misread, confirmed directly from the constructor's own
assignment. Two logically unrelated classes' tiny 2-slot vtables (destructor
+ 1 method) happen to be byte-identical in content, so the linker folded
them into one shared physical location — the same "identical code/data
folding" phenomenon already observed for the generic no-op stubs
(`Node_NoOpStub1`-`4`, `Node_StubReturnFalse`/`True`), just applied to a
whole vtable rather than a single function this time. No new rename needed
(both halves already correctly named); logged as a structural curiosity.

With this pass, essentially every level-script node type from
`RE_NOTES_level_script_system.md`'s opcode table now has verified
Update+Destruct methods. Remaining unswept: the two "unknown opcode" types
(`UnknownOpcode09`/`0d`) and the full `AnimCombo` vtable (its Update was
already named in an earlier session).

## Update: closed the last two — every node type in the opcode table is now done

Swept `AnimCombo`'s destructor and both "unknown opcode" types
(`UnknownOpcode09`/`0d`, the two whose tag strings couldn't be recovered
from the text export). Standard shapes throughout, two were unanalyzed
`LAB_` labels before this pass. **`UnknownOpcode0d_UpdateWorldSpaceMarker`**
(slot 2) is yet another instance of the AABB+visibility-check pattern
already seen in `Fence_UpdateWorldSpaceMarker`/`Rider_DrawWorldSpaceMarker`
— a third independent confirmation this is a genuine, reusable engine
idiom, not a one-off.

**This closes the vtable sweep entirely: every one of the 24 node types in
`Script_DispatchOpcode`'s opcode table now has a verified Update and
Destruct method.** What started as "check whether Rider's 3 unresolved
slots extend to other classes" ended up mapping Update/Destruct for
TrickTrigger, Boost, Fence, Roller, Cracked, Timer, Debounce, UVScroll,
TexFlip, Movie, ZBoost, the AnimObject family (AnimObject/AnimDelta/
AnimTexFlip/AnimCombo), Particle, cMeshAnim, and both unknown-opcode types
— 29 renames across this whole multi-part thread, plus the 4 corrected
`Node_`-prefix names. A genuinely complete architectural sweep of the
level-script object system's per-frame lifecycle, not a partial one.

## `Rider_UpdateSubsystems` (slot 7) — the master per-frame dispatcher

`void __thiscall Rider_UpdateSubsystems(int param_1, undefined4 param_2)` — writes a
`DebugBuffer_Write` profiler-zone marker (`0x480` bytes, tag `0xdeadbef8`), then calls
**18 per-subsystem stub functions** in a fixed sequence, each taking the same
`param_2` (almost certainly a delta-time or the rider object). Each stub follows an
identical pattern:

```c
void FUN_xxxxx(undefined4 *param_1) {
    *param_1 = <size>;
    param_1[1] = <magic tag, 0xdeadbfXX>;
    DebugBuffer_Write(param_1, <size>);
    // ... sometimes a real call here, usually nothing ...
}
```

**16 of the 18 are pure no-op profiler markers** — write the debug tag and return,
zero real logic. This retail build has stripped out most subsystems' actual update
code, leaving only the profiling/telemetry instrumentation harness. Only **2** have any
surviving call:

- **`Rider_UpdatePhysicsComponents`** (was `FUN_00066c40`) — calls
  **`Component_UpdateAll`** (was `FUN_00060a90`) **three times, on three different
  embedded sub-objects**. Confirmed via `/disassemble_function`: `ESI` starts as the
  rider's own `this`, then `ADD ESI,0x28` before the first call, `ADD ESI,0x58` before
  each subsequent call — meaning the three calls target `rider+0x28`, `rider+0x80`, and
  `rider+0xd8` respectively (i.e. `Component_UpdateAll`'s own `+0x50` list-head field
  lands at absolute rider offsets `+0x78`, `+0xd0`, `+0x128`). **Not a 3-substep
  physics pattern as first guessed — three distinct attached-component sub-lists,**
  each likely a different category of attached behavior (candidates: collision
  volumes, animation-driven effects, trick/trigger detectors — not confirmed which is
  which). This is the one subsystem with real, active work in this build.
- The other real call (inside a different stub, `FUN_000299f0`) turned out to itself be
  a no-op marker with no further call — a dead end, not pursued as a separate rename.

## `Component_UpdateAll` — a generic attached-component iterator

`void __thiscall Component_UpdateAll(int *param_1, undefined4 param_2)` — walks a
**circular linked list** (head at `this+0x50`, next-pointer at each node's `+0x24`,
terminating when it loops back to `this`) and calls **`vtable+0xc`** on every attached
node, passing `param_2` through. This is a generic "update every attached component"
dispatcher — the same architectural idea as `NodeRegistry`/`Widget`'s child-list
patterns found earlier this session, but a third, distinct instance specific to
whatever object owns this particular list.

**Whose list is it? ANSWERED (2026-07-20).** Traced this via raw x86
disassembly rather than the decompiled C (which hid the true `this` behind
an `unaff_ESI`-style artifact, exactly as flagged). At
`Rider_UpdatePhysicsComponents`'s entry, `MOV ESI,ECX` captures the Rider's
own `this`. It's then used directly (not through a sub-object): the function
calls `Component_UpdateAll` three times, on `rider_this+0x28`,
`rider_this+0x80`, and `rider_this+0xd8` (`ESI` advanced by `0x58` between
each call, `EDI` a loop counter of 3). **These are NOT a dynamically-populated
sub-object's list -- they're 3 fixed, inline-embedded component slots baked
directly into the Rider object's own layout**, each `0x58` bytes apart,
starting at `+0x28`. `Component_UpdateAll` itself confirms each slot is the
sentinel head of its own intrusive circular linked list (walks
`slot_this[0x14]` i.e. `+0x50` relative to the slot, following each node's
`+0x24` field as "next", terminating when it loops back to the slot itself --
the same `NodeBase`-style circular-list idiom used throughout this project).
So concretely: `Rider+0x78`, `Rider+0xd0`, and `Rider+0x128` are 3 separate
component-list heads (list head = slot base + 0x50), and whatever gets
inserted into each is called via vtable+0xc during every physics tick.

**Not yet found**: `Rider_ConstructBase` was checked for where these 3 list
heads get self-initialized (the classic "points to itself when empty" idiom)
-- not found in the portion read; it's a large constructor and the init
may be in a different base-class step. **The actual "Insert" call sites**
(which would reveal the real component TYPES -- physics? trick-detection?
something else) also weren't found this pass; a targeted search for the
generic circular-list-insert pattern (matching `Rider+0x78`/`+0xd0`/`+0x128`
specifically, not just any `+0x78` in the whole binary -- too much noise
otherwise) is the concrete next step if continuing. This is nonetheless the
strongest, most concrete progress on "whose list is it" across every session
that has touched this thread.

## Checked the derived classes too: `OtherRider` (AI) and `Player` (human)

Both `OtherRider_Construct` and `Player_Construct` call `Rider_ConstructBase` first,
then install their own derived vtable overriding slots 0, 1, 7, and 8 (destructor pair
+ the two update-dispatch slots) — confirming slots 7/8 really are the right place to
look for subclass-specific per-frame behavior. But:

- **`OtherRider_UpdateSubsystems`** (was `FUN_00046e10`) and **`Player_UpdateSubsystems`**
  (was `FUN_0005a2c0`) both just call the base `Rider_UpdateSubsystems` and then add
  **one more no-op `DebugBuffer_Write` marker** — no additional real logic in either,
  including the human-controlled `Player` version. This rules out "the trick logic is
  a Player-specific override" — it isn't, at least not reachable this way.
- **`OtherRider_ResetSubsystemBuffers`** (was `FUN_00046e50`) is the one exception with
  real content: reads a stored state index at `+0x894` and looks up 3 floats from a
  fixed table (`DAT_001c6c70`, 3-float/12-byte stride) — reads as an **AI
  decision/pathing state-table lookup** (which fixed point/behavior to head toward
  next), not trick-scoring. Not pursued further — different subsystem (AI navigation).
- **`Player_ResetSubsystemBuffers`** (was `FUN_0005a300`) — no extra logic beyond a
  buffer clear.

## Honest conclusion

**The real-time trick-detection and score-accumulation logic is confirmed NOT to live
in Rider's (or Player's or OtherRider's) own polymorphic Update dispatch chain** in
this retail build — that whole chain has been reduced to profiler instrumentation
plus a single physics-substep call into a generic attached-component list. The actual
gameplay logic almost certainly lives in one of the objects attached to that
component list (via `Component_UpdateAll`), or possibly in the same "attached script/
trigger" machinery already documented in `RE_NOTES_level_script_system.md`
(`ScriptVM_DispatchOpcode`/`Script_DispatchOpcode`) rather than in Rider's C++ class
hierarchy at all — level-scripted games from this era commonly put per-frame gameplay
rules in the script VM rather than hardcoded C++, which would also explain why 16 of
18 "subsystem" stubs in `Rider_UpdateSubsystems` are empty: the real logic was moved to
script data, and the C++ stub layer is legacy/vestigial instrumentation points.

**If continuing this thread**, updated next steps: (1) is now **DONE** (see
above -- `Component_UpdateAll`'s true `this` is `rider_this+0x28/+0x80/+0xd8`,
3 fixed component-list heads, not a sub-object's list); the remaining
concrete step is finding what code `Insert`s nodes into those specific 3
lists (`Rider+0x78`/`+0xd0`/`+0x128`), which would reveal the real component
types. (2) check whether `ScriptVM_DispatchOpcode`'s per-frame tick (if one
exists — not confirmed) handles trick detection instead, tying back into the
already-well-understood script-VM architecture rather than the C++ side.

## Follow-up: an exhaustive, definitive byte-level search — confirmed no direct write exists

Went one step further than the earlier `default.xbe.c` text-grep (which only reflects
whatever Ghidra's C exporter produced, and could in principle miss something): used
`/search_bytes` for the literal little-endian displacement bytes `10 57 00 00` (i.e.
`+0x5710` as it would appear in *any* x86 instruction encoding it, regardless of which
function's decompile happened to get exported) across the **entire live Ghidra
database**. Got 11 hits — more than the 6 the text-grep found, since raw byte search
catches instructions Ghidra's C exporter might phrase differently or that live in
functions not yet given real names.

Checked each hit's actual instruction via `/read_bytes` (opcode byte immediately
before the displacement):

| Address | Bytes | Instruction | Verdict |
|---|---|---|---|
| `0x2da78` | `8b 8f 10 57 00 00` | `MOV ECX,[EDI+0x5710]` | read |
| `0x2dad5` | `8b 8f 10 57 00 00` | `MOV ECX,[EDI+0x5710]` | read |
| `0x2f88a` | `8b 83 10 57 00 00` | `MOV EAX,[EBX+0x5710]` | read |
| `0x4f81b` | `8b 80 10 57 00 00` | `MOV EAX,[EAX+0x5710]` | read |
| `0x5380c` | `8b 90 10 57 00 00` | `MOV EDX,[EAX+0x5710]` | read (inside `TrickTrigger_Update`) |
| `0xab4b4` | `db 80 10 57 00 00` | `FILD dword [EAX+0x5710]` | read (loads to FPU stack) |
| `0xc4441` | `8b 83 10 57 00 00` | `MOV EAX,[EBX+0x5710]` | read (inside `HUD_DrawRaceOverlay`) |
| `0xc7159` | `8b 88 10 57 00 00` | `MOV ECX,[EAX+0x5710]` | read |
| `0xcb53d` | `8b b8 10 57 00 00` | `MOV EDI,[EAX+0x5710]` | read |
| `0x16913c` | `e8 10 57 00 00` | `CALL rel32` | **false positive** — displacement bytes coincide, not a data reference at all (the same E8/E9 false-positive pattern documented as a project-wide lesson) |
| `0x212c8a` | (non-code bytes) | — | **false positive** — inside a data section |

**Every single real reference to this literal offset, across the entire ~2MB+ binary,
is a read.** No `MOV [reg+0x5710], value`, no `FISTP [reg+0x5710]` (which would show
the same trailing displacement bytes and would have been caught by this search), no
`LEA reg,[reg+0x5710]` (which would indicate "compute the address once, write through
it later" — also would have been caught). This is about as close to a **proof** as
static disassembly search can offer that no compiled function anywhere writes to this
field using a compile-time-constant offset.

**Conclusion, now well-evidenced rather than just "not found yet":** the field is
almost certainly written through a **runtime-computed offset** — i.e. some generic
"set object field N to value" mechanism where the field index/offset is *data* (from
a level-script command record or a resource table), not a literal baked into any x86
instruction. This fits the codebase's demonstrated style perfectly:
`ResourceContext_GetTableField` already reads variable-offset table data at runtime,
and the level-script VM (`ScriptVM_DispatchOpcode`) is explicitly a data-driven system
where command records supply arbitrary per-instance parameters (exactly how
`TrickTrigger_Construct` reads its own threshold value from `param_4+0x10`, "duration
or score-multiplier value"). **This makes the whole trick-scoring system very likely
data-driven through the script VM rather than hardcoded in any single C++ function** —
consistent with why `Rider_UpdateSubsystems`'s subsystem stubs are mostly empty in
retail (the real logic moved to script data, not C++) and why no static disassembly
search, however thorough, can find a single "the score increment happens here" line:
there may not be one — it could be scattered across dozens of different level scripts'
command-record data, interpreted generically by opcode handlers.

**Follow-up: checked the vtable-dispatch opcodes directly — ruled out too.** Read
`ScriptVM_DispatchOpcode`'s cases 9/10/0xb directly: each calls a *fixed*, hardcoded
vtable slot on the owner node (`+0x5c`, `+0x54`, `+0x58` respectively) with 0-2 literal
arguments pulled from the command record — i.e. these are event *notifications*
("OnComplete"/"OnStart"-style calls), the same mechanism `TrickTrigger_Update` itself
uses to fire its own event when `+0x5710` crosses a threshold. **Not a generic
"write field N to value" mechanism** — the vtable slot is baked into which opcode you
use, not read from data. This hypothesis is now disproven with direct evidence, not
just deprioritized. The `Counter` opcode (0x06) was also checked and ruled out (a
self-contained countdown timer). **No specific opcode has been found that writes the
score field** — the data-driven-write hypothesis remains the best-supported
explanation for the "no instruction anywhere writes it" result, but the exact
mechanism (which opcode, if any, actually performs the write) is still unidentified.

**Also checked `AnimCombo`'s Update method** (opcode 0x102, was `FUN_000c0030`, not
previously read — only its constructor had been) on the theory that "combo" in its
name might connect to trick combos rather than animation blending. It doesn't: it's a
plain lerp/tween accumulator advancing a value toward a target and firing a completion
callback — confirms the earlier "animation-blend transitions, not trick/score combos"
read was correct. Ruled out.

**This closes the search-for-the-writer thread with a satisfying, rigorous answer**
even without a single line of code to point to: not a gap in the investigation, but
architectural evidence that the mechanism itself is data-driven.

## Found the component-slot constructor, narrowing the search further (2026-07-20)

Continued from the disassembly-tracing find above. Traced `Rider_ConstructBase`'s
own construction chain (`FUN_00033b20`, called early) and found
**`Rider_ConstructComponentSlots`** (was `FUN_000688f0`): loops exactly 3
times over `rider_this+0x28`, advancing `0x58` bytes each iteration -- an
exact match for the 3 fixed slot offsets found via disassembly. Confirms:

- Each slot's vtable is set to the shared **`Node_NoOpStub1`** placeholder --
  the same generic no-op vtable this project has repeatedly found reused
  across unrelated `Node`-family classes. These slots start life as inert
  placeholders, not specialized "trick-detection" objects with unique logic
  baked in from construction.
- **`Component_InitEmptyList`** (was `FUN_00107ae0`, called once per slot)
  does the exact self-referential empty-list init: `this+0x50 = this` --
  precisely matching `Component_UpdateAll`'s read of that same field as the
  list's first-element pointer. This pins down the **exact field layout**:
  `+0x50` (relative to the slot) = first-element pointer, `+0x24` (relative
  to each node) = that node's own "next" pointer -- a real, verified layout,
  not a guess.
- Lazily initializes `BdrSeq_InitEventCodeTable` right after the loop -- a
  real tie-in to the already-documented `RiderEvent`/board-sequence animation
  system, though checking `BdrSeq_InitEventCodeTable` itself showed it's an
  unrelated *global* event-code lookup table population, not the list-
  insertion mechanism. Still a meaningful adjacency worth remembering if this
  thread is picked up again.

**Still not found**: the actual insertion call sites for these 3 specific
list heads. But the search is now much more targeted than a blind offset
sweep would be -- anyone continuing this should search for code that writes
a node's `+0x24` field and updates a slot's `+0x50` field together (the
real "attach a component" operation), rather than searching for the generic
`rider+0x78`/`+0xd0`/`+0x128` byte patterns directly (too noisy, and those
are absolute addresses that only apply to one specific Rider instance
anyway -- the real insert function almost certainly takes the slot base as
a parameter, matching every other generic list utility in this project). 2
renames.

## Found the actual node-insertion function (2026-07-20, same session)

Read the shared `Node_NoOpStub1` vtable's raw bytes directly and found
something unexpected: embedded right after groups of 5 function pointers are
**tag strings** -- `"SkelAnim"` and `"BdrSeq"` -- naming small per-type method
tables. Checked the 5 `"BdrSeq"`-tagged functions (destructor, reset, a
per-frame tick, a debug-dump, and a setup/attach step) -- none was the
insertion itself, but this pointed straight at the already-documented
`RiderEvent` animation-trigger system. Traced the real chain:

**`RiderAnimation_TriggerByEventCode`** (already named) resolves an event
code to a slot index via a lookup table, computes the exact component-slot
address (`iVar1 * 0x58 + 0x28 + rider_this` -- an **exact match** for the
component-slot formula found via disassembly earlier this session), calls
**`New_BdrSeq`** (already named) to allocate a fresh animation-state object,
then calls **`BdrSeq_ConfigurePlaybackFromEventCode`** (already named)
passing the slot address directly. Inside that function, right before every
return path, it calls **`Component_InsertNode`** (was `FUN_00107ac0`,
confirmed via raw disassembly -- `MOV ECX,EDI` immediately before the call,
another implicit-parameter case the decompiled C hid). That function is a
genuine intrusive doubly-linked-list push-front: sets the new node's
back-link, forward-link, re-links the old first element, and updates the
sentinel's first-element pointer -- the real "attach a component" operation.

**This is the first time this project has found an actual, confirmed "insert
a real gameplay object into a Rider's per-frame component list" operation**,
tied directly to animation/trick-sequence triggering. 1 rename.

## `+0x28` vs `+0x50` -- RESOLVED (2026-07-20, same session, continued)

Picked this back up per "keep going." Traced a second, independent path to
the same slot struct: `Rider_ResetSubsystemBuffers` (vtable slot 8, already
named) calls a previously-unnamed function at its very first real step --
raw disassembly showed a hidden implicit `this` computed as
`*(*(rider_this+0x30)+4) + rider_this + 0x4544` (the familiar per-Rider-
variant indirection this project has seen before: `[this+0x30]` points to a
small per-type descriptor whose `+4` field is a base delta, added to a
literal offset to reach a field whose absolute position varies by Rider
subtype). That function -- renamed **`ComponentSlot_ResetAndReseedBdrSeq`**
(was `FUN_00066c80`) -- does `ADD ESI,0x28` then loops 3 times advancing
`0x58` bytes, calling **`New_BdrSeq_2`** once per slot. Confirmed
`New_BdrSeq_2`'s body is byte-for-byte identical list-insertion logic to
`Component_InsertNode` (both write `node+0x24=slot`, `node+0x28=*(slot+0x28)`,
`*(*(slot+0x28)+0x24)=node`, `*(slot+0x28)=node`) -- so this independently
confirms `+0x28` really is the field these two insertion paths agree on.

The actual reconciliation came from re-reading two already-verified
functions side by side, both taking the exact same "slot base" as their
`this`:

- **`Component_InitEmptyList`** (`this` = one slot's base address) sets
  **four** fields off that single base in one call: `this+0x24 = 0`,
  `this+0x28 = this+0x2c` (a small dummy sentinel node embedded just past
  the slot's own header), `this+0x50 = this` (self-pointing), `this+0x54 = 0`.
- **`Component_UpdateAll`** (same `this` convention, confirmed via
  `Rider_UpdatePhysicsComponents` calling it directly on
  `rider_this+0x28`/`+0x80`/`+0xd8`) reads `this+0x50` as the walk's
  first-element pointer.

Since **one function initializes both `+0x28` and `+0x50` relative to the
identical base in the same breath**, they cannot be the same field viewed
through two relative offsets -- that would require `Component_InitEmptyList`
to write to itself twice with different meanings, which the code plainly
doesn't do. **Conclusion: each 0x58-byte component slot embeds TWO separate,
independently-maintained doubly-linked lists, not one list under two
names:**

- **`+0x28`** (dummy sentinel at `+0x2c`) -- the **BdrSeq animation-event
  queue**. Populated by `Component_InsertNode`/`New_BdrSeq_2` from animation
  triggers (`RiderAnimation_TriggerByEventCode -> New_BdrSeq ->
  BdrSeq_ConfigurePlaybackFromEventCode -> Component_InsertNode`, and now
  also from `Rider_ResetSubsystemBuffers -> ComponentSlot_ResetAndReseedBdrSeq
  -> New_BdrSeq_2`).
- **`+0x50`** (self-pointing sentinel) -- the **active-component list**,
  walked every frame by `Component_UpdateAll` (via
  `Rider_UpdatePhysicsComponents`), calling vtable+0xc on every attached node.

Also notable: `ComponentSlot_ResetAndReseedBdrSeq`'s own 3 slots live at a
**different, indirected base** (`rider_this + delta + 0x4544`, not
`rider_this` directly) -- a second, separate trio of the same 0x58-byte
slot struct reused for a different subsystem, not the same 3 slots
`Rider_ConstructComponentSlots` builds at `rider_this+0x28` directly. The
0x58-byte "component slot" shape (vtable + two independent lists + misc
fields) is a genuinely reused generic building block in this codebase, not
unique to the trick-physics components. 1 rename this stretch.

**Corroborating check**: read the small utility cluster sitting right next to
`Component_InsertNode`/`Component_InitEmptyList` in memory
(`0x00107a**`-`0x00107b**`). All three turned out to be a self-contained
`+0x28`-queue API, with zero involvement of `+0x50`:

- **`BdrSeqQueue_Count`** (was `FUN_00107b00`) -- walks the `+0x28` queue
  counting entries. Its only caller, confirmed via `xrefs_to`, is
  `Component_UpdateAll` itself -- it calls this purely to write the count to
  a `DebugBuffer_Write` profiler marker, immediately before doing its own,
  completely separate `+0x50` walk. Direct confirmation the two lists are
  tracked independently even in the debug instrumentation, not just in the
  init/insert code.
- **`BdrSeqQueue_RemoveAndDestroyNode`** (was `FUN_00107b20`) -- unlinks one
  node from the `+0x28` queue and calls its vtable+0 (destructor). 9 callers
  (`FUN_00066d00`, and a run of handlers at `0x00068300`-`0x00068780`) --
  plausibly per-event-type BdrSeq completion/cancel handlers; not
  individually traced this pass.
- **`BdrSeqQueue_DestroyAllAndReset`** (was `FUN_00107b50`) -- walks and
  destroys every node in the `+0x28` queue, THEN resets both sub-lists in one
  call (`+0x28=+0x2c`, `+0x24=0`, AND `+0x50=this`, `+0x54=0`). So a full
  slot reset touches both lists together even though day-to-day
  insert/traversal is fully independent. Called from
  `BdrSeq_ConfigurePlaybackFromEventCode` (x2), `FUN_00066dd0` (a tail call
  from `Rider_ConstructComponentSlots`), and
  `ComponentSlot_ResetAndReseedBdrSeq`.

3 renames this stretch.

**If continuing**: the actual consumer of the `+0x50` active-component list
(what real object type gets attached, and where trick-scoring logic would
live inside vtable+0xc's implementation) is still the open thread. The
`+0x28` utility cluster's address range offered no leads for it -- the
`+0x50` inserter is not colocated with these functions, so it likely lives
wherever the real (non-`Node_NoOpStub1`) component types get constructed.
The list mechanics themselves are now fully understood on both sides,
though, which narrows any future search considerably.

## Three more search angles tried for the `+0x50` inserter, all struck out (2026-07-20, same session)

Per "keep going," pushed further on finding whatever populates the `+0x50`
active-component list. Three independent angles, all genuinely exhausted
this pass:

1. **Tag-string guessing**: `/search_bytes` for candidate allocator-tag
   strings (`"Trick"`, `"Combo"`, `"Score"`, `"Grind"`, `"Rail"`, `"Air"`,
   `"Spin"`, etc.). `Score`/`AirTrick`/`TrickMgr`/etc. don't exist anywhere
   in the binary. `Trick`/`Combo`/`Rail`/`Grind`/`Air`/`Spin` DO exist, but
   every hit checked resolved to either HUD/font text resources (`"Trick
   Tutorial"`, `"Tricks Completed"`, `"Combo Grab"`, glyph-table names like
   `"Cf3wlstTricks"`) or `"ComboXform"` -- the allocator tag for
   `AnimCombo`/`New_ComboXform`, a system **already checked and ruled out**
   earlier this session (a plain animation-blend lerp accumulator, not a
   trick/score combo tracker, despite the tempting name). No new leads.
2. **Opcode sweep** for `MOV [reg+0x50],reg` (the shape a `+0x50`-list
   push would need) across `ECX`/`EDX`/`EBX`/`ESI`/`EDI`/`EBP` source
   registers with a 1-byte `+0x50` displacement -- 19 raw hits across the
   whole binary. Checked every single one with `/get_function_containing`:
   **none** land anywhere near `Component_UpdateAll` (`0x60a90`),
   `Rider_UpdatePhysicsComponents` (`0x66c40`), or the component-slot
   cluster (`0x688f0`/`0x107a**`) -- they're scattered across totally
   unrelated systems (`Rider_UpdateSnowSprayFX`, `GfxContext_*`,
   `AggressionManager_ResetMatrix`, `Text_RenderGlyphStringScreenSpace`,
   `XGSwizzleRect`, several unnamed leaves). Confirms this project's
   standing lesson (see `reference_ghidra_mcp_connection.md`'s "Round
   2/3" note): a generic-offset byte sweep with no other constraint is
   too noisy to be useful, even scoped to a single displacement value.
3. **Re-read the `Node_NoOpStub1` vtable's raw bytes for more embedded tag
   strings** beyond the already-found `"SkelAnim"`/`"BdrSeq"` pair (dumped
   1024 bytes starting at `0x001914b8`). Nothing else readable follows --
   just a large numeric table (event-code lookup data), confirming the
   two known tags are the only ones embedded there.

**Honest conclusion**: this specific sub-question (what populates `+0x50`)
is now genuinely exhausted for this session across three independently-
chosen search strategies, not abandoned after one attempt. Combined with
this project's repeated finding that many extension points are simply
inert/no-op'd in this retail build (16 of 18 `Rider_UpdateSubsystems` stubs,
`AnimCombo` turning out to be unrelated, etc.), there's a real possibility
`+0x50` is **never populated at all in retail** -- i.e. `Component_UpdateAll`'s
per-frame walk may always be a no-op in the shipped game, and the actual
trick-score write happens through an entirely unrelated mechanism this
whole component-slot thread was never going to reach. Not proven, but
consistent with the pattern. Recommend deprioritizing this specific
`+0x50`-inserter search if picked up again; the `+0x5710`/`+0x5720` static-
byte sweep (already tried, zero hits) and dynamic analysis remain the more
promising remaining paths for the original trick-score-writer question.

**One more check, same session**: `Rider_UpdatePhysicsState` (the real per-frame
rider tick, `0x00036990`) still has **15 unnamed, un-decompiled-in-detail
sub-calls** (`FUN_0001e5d0`, `0001e7f0`, `0001ea00`, `00021580`, `000308d0`,
`00032f20`, `00033790`, `00033cb0`, `00033df0`, `00038530`, `0003bc30`,
`000455b0`, `0005d1f0`, `0005e410`, `000ab170`). Grepped every one directly for
any `0x57xx` literal (the `+0x5710`/`+0x5780`/`+0x5784` score/event region) —
**zero hits in all 15**. Consistent with the established pattern (neither
`+0x5710` nor `+0x5780` ever appear as a fresh literal anywhere except already-
mapped consumer code): if the writer is among these 15 (or their own children),
it computes the address through a stored/passed pointer rather than a fresh
`rider_base + literal` each time, which a literal grep cannot find. These 15
functions remain a legitimate, concrete list of unexplored territory for
whoever continues this thread — each would need to be individually decompiled
and read for physics/trick-detection-shaped logic (float-heavy, animation-
state-machine-like), not just grepped.

**Update (2026-07-20, much later same session)**: one of these 15
(`FUN_000455b0`) got opened via a completely unrelated thread — mapping
`PowerFX Particles` (see `RE_NOTES_powerfx_particles.md`). Renamed
**`Rider_UpdateUberTrickGlowFX`** (moderate confidence, not proven by an
explicit tag) — activates/deactivates two particle-glow points tracking limb/
board positions, gated on a per-rider condition byte, very plausibly the Über
Trick charge visual. No `+0x5710` reference found in it either. 14 of the 15
flagged functions remain unopened.

## Major find: rider-vs-rider collision system (same session, continued "keep going")

Picked the 14-function list back up directly. 5 turned out to be trivial,
widely-reused generic math primitives — named for what they clearly do
(**`Math_Atan2`**, **`Vector4_DotProduct`**, **`Math_ClampTowardTargetWithMargin`**,
**`Vector4_Scale`**, **`Rider_AccumulateStateTimer`**) rather than investigated
as Rider-specific. **`GameState_ShouldSkipGameplayTick`** (was `FUN_000ab170`)
turned out to be another shared pause/loading gate, combining several
already-known checks (`FUN_000b5d90`, `FUN_000b9d20`, and the same
`FUN_000ca120` gate `HUD_TickRiderDisplayState`'s own caller uses).

**The big one**: `FUN_00038530` (303 lines — by far the largest of the 15, and
the one this note's earlier pass correctly flagged as the highest-value
target) turned out to be a previously **completely unexplored, substantial
system: rider-vs-rider collision detection and physics response.** Renamed
**`Rider_ProcessCollisionsWithOthers`**. Two-part structure:

1. **A linear all-riders proximity scan** (the `this+0x730==0` branch) — checks
   every other rider for eligibility (rate-limited via a last-check timestamp,
   gated on `ComponentSlot_ResolveCategoryFromType`/`ComponentSlot_CheckCategoryFlag`
   — see below), computes relative position/velocity along a contact normal,
   and on a genuine close-approach detects a new collision pair.
2. **A binary-tree walk** (the `this+0x730!=0` branch, nodes chained via
   `+4`/`+8` child pointers — a real broad-phase-confirmed collision-pair
   tree, not a flat list) — filters pairs down to actual `Rider`-type objects
   via a type-ID check (`*(iVar2+0x14)==0x3ef`), then does genuine
   **elastic-collision-shaped physics**: relative velocity along the contact
   normal, position separation (pushing the two riders apart), and impulse
   application via **`Rider_ApplyCollisionImpulse`** (was `FUN_00037ad0`,
   clamped to a max magnitude, scaled by per-object mass-like fields). A
   **collision counter** (`(short)this+0x20`) increments on each resolved hit;
   crossing a threshold (excluding specific animation states 5/6) triggers
   `FUN_000335b0`/`FUN_00031b60` (a reaction of some kind — sound, animation,
   or knockdown, not traced further this pass) — otherwise calls the
   **already-named `Rider_UpdateCueTimer` twice**, directly tying this
   collision system into the existing `RiderEvent` animation-trigger
   machinery.

**Real, valuable structural connection**: the eligibility gate
(`ComponentSlot_ResolveCategoryFromType`/`ComponentSlot_CheckCategoryFlag`, was
`FUN_00066b60`/`FUN_00067580`) reads a component slot's type code through the
**exact same shared lookup table** (`DAT_001acf20`, `0x1c` stride) this
session's `BdrSeqEvent` dispatch tables use, and returns the same `8`/`0x300`
default-sentinel shape as `BdrSeqEvent_DispatchCompletionByType`'s own type-0
case. This means rider-vs-rider collision eligibility is gated by the exact
same per-slot component-category system this whole session's trick-animation
thread has been mapping — a genuinely unexpected, real cross-system link, not
a coincidental naming overlap.

**Still not the score writer** — no `+0x5710` reference in `Rider_ProcessCollisionsWithOthers`
or any of its direct helpers checked this pass. But this is easily the most
substantial single system found in this entire 15-function sweep, and a
genuinely new, previously-undocumented piece of the physics simulation. 9
renames this stretch (5 math utilities + 1 gate + 3 collision-system core
functions). 8 of the original 15 flagged functions are now opened
(`Rider_UpdateUberTrickGlowFX` earlier this session, plus these 7).

## Closed 3 more (same session, continued)

- **`Rider_SelectLocomotionAnimState`** (was `FUN_00032f20`, high confidence) —
  a clean, well-understood find: computes rider velocity, and above a speed
  threshold projects it onto forward/right/up basis vectors to pick one of 9
  directional locomotion/idle animation event codes, firing
  `RiderAnimation_TriggerByEventCode` only when the resolved code differs from
  the current sub-state (via the already-named `RiderEvent_GetSubState`). A
  genuine "which movement-direction animation should play" selector.
- **`Rider_UpdateSpeedIntensityFX`** (was `FUN_0003bc30`, moderate confidence)
  — computes velocity magnitude into a scaled "intensity" field, resets on a
  specific rider state, caches a forward-ish vector; likely feeds a
  speed-driven audio/visual effect (wind, speed-blur), exact consumer not
  traced.
- **`Rider_AccumulateCameraShakeInputs`** (was `FUN_0005e410`, moderate
  confidence) — four independent counters feeding a shared accumulator field
  with different formulas (one from a *different* object's velocity via a
  cross-reference field); shape matches the already-documented
  `Camera_EvaluateShakeCurves` system closely, but the direct connection isn't
  confirmed.

**Honest status — 4 remain genuinely unopened**, checked but not confidently
understood enough to name: `FUN_00033790` (a decay-countdown pattern on a
per-slot byte, gated on level-time and 3 flag bytes — possibly equipment wear
or a combo-timeout, not confirmed), `FUN_00033cb0`/`FUN_00033df0` (both call
unexplored terrain/geometry-query-shaped helpers plus a per-rider callback
vtable — likely terrain-contact or path-following logic), `FUN_000308d0`
(a one-time-init loop over a level-owned position array, plausibly a
checkpoint/spline-position cache-build step). All 4 remain open if this
thread is picked up again — **11 of the original 15 flagged functions are now
opened, 4 remain.**

## Major find: 2 of the 4 remaining cracked — a whole new track-distance system (same session, "take your time, deep research")

Picked this list back up directly rather than opening a fresh tag. Traced
`FUN_00033cb0`/`FUN_00033df0`'s shared helper calls (`FUN_000beed0`/
`FUN_000bf110`) fully rather than stopping at "terrain/geometry-query-shaped,"
and it turned out to be a genuinely major, previously-completely-unknown
system: **every rider tracks its own distance-along-track position via an
embedded spline path, and separately queries a track-wide scripted-event
system keyed by track distance.**

- **`SplinePath_FindClosestPoint`** (was `FUN_000beed0`, high confidence) —
  given a path object (segment count + segment array, each segment encoding
  a direction/length) and a 2D query point, finds the closest point on the
  path via per-segment projection, returning the arc-length distance to it.
  Supports **incremental/cached evaluation** (an in/out state struct tracks
  the last-found segment + accumulated distance) so it doesn't re-search the
  whole path from scratch every frame — the standard "nearest point on
  spline, cached from last frame" technique.
- **`SplinePath_EvaluateAtDistance`** (was `FUN_000bf110`, high confidence) —
  the complementary operation: walks the same path by arc length to return
  the 3D position at a *given* distance (rather than finding the distance
  for a given position).
- **`Rider_UpdateTrackPathPosition`** (was `FUN_00033cb0`) — calls
  `SplinePath_FindClosestPoint` on an embedded `SplinePath` (`this+0x170`)
  using the rider's own position, caching distance-along-track frame to
  frame. Then calls `SplinePath_EvaluateAtDistance` for a look-ahead point
  further along the path and computes the angle (atan2) from the rider
  toward it, storing the result at `this+0x3a0`. **Honest caveat**: checked
  every sibling `Rider_UpdatePhysicsState` sub-call directly for a consumer
  of that stored angle — none found. A strong, plausible candidate for
  AI-rider steering input (this runs unconditionally for every rider,
  human and AI alike), but **not proven** — could instead feed a HUD
  element, an "on-line" bonus check, or something else. Flagged honestly,
  not asserted.
- **`Rider_UpdateTrackEventTriggers`** (was `FUN_00033df0`, moderate-high
  confidence, ~85 dense lines not traced field-by-field) — reads an
  *external* pointer to a separate polymorphic object and queries its
  vtable slot 0 with a distance range (old-distance, new-distance) to find
  which scripted events fall within the distance the rider just traveled,
  firing a callback for each one crossed (plus a second pass for
  still-active ones). Reads as a genuine checkpoint/scripted-trigger-along-
  the-track mechanism, distance-keyed rather than world-space-triggered.

**This directly connects to a lead from this session's `DebugMenu` investigation**
(`RE_NOTES_debug_menu.md`): the embedded per-tag memory-usage page there
surfaced two brand-new tag names, `"Splinepath"` and `"EventPaths"` — at the
time checked and set aside as "not individually traced to real code." They
now have real, confirmed homes: `SplinePath_FindClosestPoint`/
`EvaluateAtDistance`'s path-object shape, and `Rider_UpdateTrackEventTriggers`'s
external vtable-dispatched object, respectively. A genuinely satisfying
payoff from taking the time to trace shared helper calls fully instead of
stopping at "terrain/geometry-query-shaped, not traced further" — the label
I'd given these two functions the first time through this list.

4 renames. **13 of the original 15 flagged functions are now opened, 2
remain** (`FUN_00033790`, `FUN_000308d0`).

## Cracked another: a spatial ambient-zone audio influence system (same session, continued)

Kept going on the same list. `FUN_000308d0` turned out to be another
genuine, substantial, previously-unknown system — this time tying directly
into the already-documented `AudioSystem`/`AmbientZone` classes
(`RE_NOTES_audio_system.md`) rather than opening brand-new territory.

**`Rider_InitAmbientZoneInfluences`** (was `FUN_000308d0`) — one-time
(gated on `this+0x490==0`), runs once per active local player (loops
`level+0x298` times, the same "1 or 2 split-screen player count" field found
elsewhere this session). For each active player's position, calls:

- **`AmbientZone_QueryInfluencesNearPosition`** (was `FUN_0011e470`) — a
  genuine spatial-hash-grid proximity query: computes a grid-cell index from
  a 3D position (the same floor-divide-by-cell-size technique this
  project's `TerrainGrid` uses) and walks the bucket's linked list of nearby
  ambient zones. Each zone has one of **4 shape types** — sphere (squared-
  distance falloff), ellipsoid (3-axis normalized distance), directional
  cone (a sensitivity-direction dot product), and a simple binary-inside
  sphere — computing a distance-based influence weight via the newly-found
  **`Math_EvaluateFalloffCurve`** (a generic, reusable easing/curve
  evaluator, not zone-specific). For any zone within range, calls:
- **`AudioSystem_RegisterActiveZoneInfluence`** (was `FUN_0011ffd0`) —
  confirmed via its own gate byte offset (`this+0x4439`, matching
  `AudioSystem`'s ~32KB allocation range) that `this` really is
  `AudioSystem`. Finds an existing slot (via
  **`AudioSystem_FindActiveZoneInfluence`**) or claims a free one in a fixed
  **40-slot active-zone-influence tracking array**, storing the zone
  reference, computed weight, and position data.

**This is the real backing store for the already-documented `AmbientZone`
class's runtime influence** — closes a real gap between "here's the
`AmbientZone` class's methods" (found earlier this session) and "here's how
a rider's position actually finds and blends nearby ambient zones every
frame" (found now). A satisfying structural connection between two
investigations that happened at very different points in this session.

5 renames. **14 of the original 15 flagged functions are now opened — only
`FUN_00033790` remains** (the per-slot decay-countdown pattern; re-attempted
with fresh context from everything learned this session, still not
confidently understood — checked its caller context in
`Rider_UpdatePhysicsState` directly, checked for a connection to the
just-found collision system, checked neighboring functions in its own
address range; genuinely exhausted for this pass, not an early stop).

## RESOLVED: `FUN_00033790` — the 15th and final flagged function (2026-07-20, "keep going non stop" pass)

Cracked it on a third attempt via a different angle: instead of chasing
callers/neighbors again, searched raw bytes for the literal dword `0x148`
(the field this function gates on) and checked which hits sat in `MOV
reg,[reg+0x148]`-shaped instructions rather than float-constant false
positives. That led straight to **`FUN_000335d0`** — the function
*immediately preceding* `FUN_00033790` in address space (body
`0x335d0`-`0x33784`) — which turned out to be its own setter, called
directly from `Rider_ProcessCollisionsWithOthers` right after every
successful `Rider_ApplyCollisionImpulse`. Together they form a genuine,
previously-undocumented **rider-vs-rider grudge/rivalry-commentary system**:

- **`Rider_RegisterCollisionGrudge`** (was `FUN_000335d0`, called
  `(this, otherRider)`) — tracks which other rider (by slot index,
  `otherRider+0x490`) `this` rider is currently "grudge-tracking"
  (`rider+0x148`=active flag, `+0x14c`=last-hit level-time timestamp,
  `+0x150`=tracked partner's slot, `+0x158`=cooldown-expired flag). Builds a
  per-partner **grudge-intensity byte** (`rider+80+slot*40`, incremented by
  15 per collision, capped per-partner via `rider+0x51+slot*0x28`), maps it
  through 2 float thresholds into a 3-tier reaction level (`rider+0x154`:
  0/1/2, tallied per-tier at `rider+0x64+slot*0x28`), and on the top tier
  calls **`Commentary_TryTriggerRivalryReaction`**. Switching to a newly-hit
  *different* partner immediately force-decays the previously-tracked
  partner's grudge byte (same `-15`/clamp-to-0 shape as the decay function
  below) rather than leaving it stuck mid-cooldown.
- **`Rider_DecayTrackedGrudgeAfterCooldown`** (was `FUN_00033790`, the
  original 15th function) — `Rider_UpdatePhysicsState`'s own per-frame
  sub-call: once the cooldown since the last hit with the currently-tracked
  partner expires (level-time check against `+0x14c`, gated on
  `+0x148`/`+0x154==1`/`+0x158==0`), decays that partner's grudge-intensity
  byte (`rider+0x53+slot*0x28`) by 15, clamped to 0, marking it done — lets a
  grudge fade out naturally if the rivalry isn't kept "fresh" by more hits.
- **`Commentary_TryTriggerRivalryReaction`** (was `FUN_00125570`) — gated on
  a rider flag, NOT Showoff mode, and a chance-roll via the new
  **`Probability_RollCategoryThreshold`** (was `FUN_001238b0`, a generic
  reusable "roll random uint against a per-category probability table"
  helper, same shape as `AggressionManager`'s tables) — then calls
  **`Commentary_QueueRivalryEvent`** (was `FUN_00121310`, dispatches a fixed
  command constant `0x100204f` through `Commentary_QueueEvent` (was
  `FUN_00158630`)). **Correction (same
  session): originally described as sharing its dispatch mechanism with
  `Camera_WarpToTarget`/`HUD_ShowTimeGapCallout` — checked both directly and
  that's wrong, they're plain ScriptVM opcode handlers with no
  `Commentary_QueueEvent` call at all.** `Commentary_QueueEvent` is its own
  separate, substantial, previously-undocumented system — see the next
  section.

**Very likely tied to the already-documented `AggressionManager`**
(`RE_NOTES_application_boot.md`'s 12×12 `Buddy`/`Friend`/`Rival`/`Enemy`
relationship matrix) — this looks like the actual *runtime accumulator* that
feeds rivalry intensity from repeated collisions, escalating into crowd
commentary reactions. The exact connection (does this grudge byte write
into `AggressionManager`'s matrix, or run alongside it as a separate but
related system?) wasn't traced further this pass — a good next thread.

5 renames. **This closes out the entire original 15-function
`Rider_UpdatePhysicsState` sub-call sweep — all 15 are now understood and
named**, the longest-running open thread in this whole project.

## Bonus find: `Commentary_QueueEvent` — a whole undiscovered voice-line/commentary queue manager

Following `Commentary_QueueRivalryEvent`'s own call one level deeper (rather
than stopping at "dispatches a fixed command constant") turned up something
much bigger: **`Commentary_QueueEvent`** (was `FUN_00158630`) has **~29
distinct small wrapper callers**, tightly clustered in `0x120e30`-`0x123fa0`
— each one zeroes a small local command-record buffer, fills in 0-4 caller
arguments, stamps a unique opcode constant (e.g. `0x100200f`, `0x1002010`,
`0x100201a`, `0x100204c`, `0x1002033`, `0x300600f`, `0x100204f` for
`Commentary_QueueRivalryEvent`), and calls `Commentary_QueueEvent`. This is
a genuine, previously entirely undiscovered subsystem: a real voice-
line/commentary **event queue manager**, structurally distinct from the
already-documented `ScriptVM_DispatchOpcode` system (confirmed by directly
checking `Camera_WarpToTarget`/`HUD_ShowTimeGapCallout` — neither calls it).

Read enough of the core to name it with real confidence:

- **`Commentary_QueueEvent`** (was `FUN_00158630`) — the master entry point.
  Re-entrancy-guarded (`DAT_00201928`). Resolves the passed opcode via
  **`Commentary_ResolveEventTableEntry`** (was `FUN_00158410`) to get a
  probability byte, a flags byte (bit `0x20` = "category event"), and a
  short ID. For simple (non-category) events: rolls a straight percent-
  chance gate against the probability byte. For category events: resolves
  a category ID via **`Commentary_ResolveEventCategory`** (was
  `FUN_001584f0`), claims or evicts a playback slot via
  **`Commentary_ClaimOrEvictPlaybackSlot`** (was `FUN_00158810`, scans a
  fixed 0x60-byte-stride slot table, falling back to priority/elapsed-time-
  based eviction), then copies the full 20-dword command record into a
  fixed playback-slot array and marks it active.
- **`RNG_ChooseWeightedCandidate`** (was `FUN_00159210`) — a generic,
  reusable-looking helper (not commentary-specific in shape, though its
  only known caller is `Commentary_QueueEvent`): repeatedly rolls a random
  value and scores up to 10 candidates via an unread weight/recency lookup,
  keeping the best-scoring one across up to 32 attempts (or accepting
  immediately on a sentinel `-1` score) — a "weighted pick, avoid recent
  repeats" pattern, very plausibly how the game avoids replaying the exact
  same commentary line twice in a row.

**Very likely tied to the already-documented `AggressionManager`**
(`RE_NOTES_application_boot.md`'s 12×12 relationship matrix) and to the
many `kAggression_*`/commentary-sounding string IDs already found in
`constant.loc` — this reads like the actual runtime trigger mechanism
behind crowd/rival commentary lines, though the connection to specific
localized strings wasn't traced this pass.

5 renames. **Honestly scoped, not exhaustive**: most of the ~29 individual
wrapper callers (each presumably a specific commentary trigger point
elsewhere in the game — collisions, tricks, race events, etc.) and several
inner sub-helpers (`FUN_001584a0`, `FUN_00158450`, `FUN_00158960`,
`FUN_001590c0`/`d0`, `FUN_001592c0`, `FUN_00159330`, `FUN_001588f0`) remain
unread — a rich, well-scoped, low-risk thread for a future session (each
wrapper is small and self-contained, unlike some of this project's harder
"unresolved fragment" investigations).

## Follow-up: confirmed the grudge system ↔ `AggressionManager` connection (same pass)

Sampled the callers of 3 more wrapper functions from the `Commentary_QueueEvent`
cluster and hit real payoff — this closes the "very likely tied to
`AggressionManager`... exact connection not traced further" gap flagged just
above.

- **`Rider_TriggerOvertakeCommentary`** (was `FUN_00125d30`) — a rate-limited
  (599 level-time units since last trigger) rider-pair proximity check: if
  two riders' forward-projected positions land within a height/distance
  band, and `Rider_CheckGrudgeQualifiesForReaction` passes, resolves each
  rider's voice-bitmask via `Character_ResolveVoiceBitmask` and queues one
  of 2 phrasing-variant commentary wrappers (chosen by a random 1-bit coin
  flip for "who's speaking") — an "overtake/pass" rivalry-commentary
  trigger, distinct from the collision-triggered grudge system (this one
  fires just from being *near* a rival, not hitting them).
- **`Rider_CheckGrudgeQualifiesForReaction`** (was `FUN_001230b0`) — **directly
  reads the same grudge-tracking fields** (`rider+0x148`/`+0x150`) as
  `Rider_RegisterCollisionGrudge`/`Rider_DecayTrackedGrudgeAfterCooldown`
  *alongside* `AggressionManager_GetRelationshipField3` — hard proof the two
  systems are wired together. Max-tier rivalry (tier 2) always qualifies;
  lower tiers require either an active grudge against this specific partner
  or the grudge-intensity byte crossing a threshold.
- **`Character_ResolveVoiceBitmask`** (was `FUN_00122fc0`) — maps a rider's
  character-index byte (0-0xb, 12 values — matches the 11-character roster
  from `RE_NOTES_game_data_archives.md` plus a spare slot) to a distinct
  power-of-2 bitmask via a plain switch table, for per-character voice-line
  selection.
- **`Commentary_TriggerAnimationCueEvent`** (was `FUN_00123780`) — called
  directly from the already-named `AudioSystem_DispatchAnimationCueEvent`.
  Gated on a threshold value (likely a duration/score tied to the animation
  cue), queues a commentary event, then optionally shows a floating
  world-space reaction cue via a helper (`FUN_0011c1e0`/`FUN_0011c1c0`)
  reused identically by `Rider_TriggerOvertakeCommentary` — very plausibly a
  generic "attach floating reaction-text bubble to a rider position"
  primitive, not yet named itself.

4 renames. **This upgrades the grudge/`AggressionManager` connection from
"likely" to definitively confirmed**, and reveals the commentary system is
broader than just collision-triggered — it also fires from mere proximity
(overtaking) and from animation-cue events, all through the same
`Commentary_QueueEvent` queue manager.

## Deep-dive: resolving the shared `SpeechSlot_*` mechanism properly (same pass)

`Rider_TriggerOvertakeCommentary` and `Commentary_TriggerAnimationCueEvent`
both call a pair of helper functions to show a floating reaction cue. A
first pass left these unnamed, flagging "calling-convention ambiguous,
needs more work" — **told explicitly to stop guessing and do the deep
research instead of stopping short**, so traced it properly via raw
disassembly and cross-reference chains rather than decompiler inference
alone:

- **Traced `Rider_TriggerOvertakeCommentary`'s own `this` (`EBX`) back
  through its caller `FUN_00114170`** (which propagates it unchanged) **to
  `FUN_0002f800`**, which extracts it directly from a genuine
  sorted-by-rank rider-pointer array (`param_1+0xc4+idx*4`, the same
  rider-array/rank-sort pattern used throughout this project, e.g.
  `Race_ComputeRankings`) — **confirms `this` really is a Rider pointer**,
  not inferred from field-offset guessing.
- **Traced `SpeechManager_StopLine`/`SpeechManager_UpdateActiveLines`'s own
  `this` back through the construction chain**: `SpeechManager_Construct`
  chains to `MusicManager_Construct` and sets the global `DAT_001f88e8`,
  itself called from `SoundGroupManager_Construct` ← `AudioChannelMixer_Construct`
  ← `AudioSystem_Construct` — **a genuine one-time singleton construction
  chain, confirmed via 3 hops of `xrefs_to`**, not assumed. (This is an
  already-documented class from an earlier session —
  `SpeechManager_CanInterruptLine`/`TriggerLineByEventCode`/`Construct`/
  `Destruct`/`UpdateActiveLines`/`StopLine`, plus `SpeechLineSet_Construct`/
  `Destruct`/`ParseSpeechConfig` — see `RE_NOTES_audio_system.md`.)

Both traces are independently solid, and both confirm the SAME field
(`this+0x3988`, a 2-slot active-line-record array pointer, same semantics
in both places — `SpeechManager_UpdateActiveLines`'s own writeup already
calls it "the fixed 2-slot active-line array") is used by **two clearly
unrelated top-level owner types**: a per-Rider instance, and the one global
`AudioSystem`-embedded `SpeechManager` singleton. The most defensible
conclusion — not a guess, but the natural reading once both ownership
chains are independently proven — is that this is a **shared, reusable
"speech/reaction slot" sub-component embedded by both classes
independently**, matching this whole project's established pattern of
generic reused building blocks (`Component_UpdateAll`'s lists,
`Node_NoOpStub` vtables, `ComponentSlot` category checks). Named the
functions generically to reflect this confirmed dual ownership rather than
picking one owner arbitrarily:

- **`SpeechSlot_SetLineParamsAndProcess`** (was `FUN_0011c1e0`) — writes
  duration/end-fade-time into the slot record, optionally records an
  "active event" on the owner, calls `SpeechSlot_ProcessOrExpire` to
  process/trigger it, then clears the slot back to idle.
- **`SpeechSlot_SetLineID`** (was `FUN_0011c1c0`) — writes the slot's
  active line/event ID field. Companion setter.
- **`SpeechSlot_ProcessOrExpire`** (was `FUN_00158a50`) — re-entrancy-
  guarded via its own separate flag (`DAT_0020192c`, distinct from
  `Commentary_QueueEvent`'s `DAT_00201928` — confirms this is a sibling
  mechanism, not literally the same dispatcher). Called both by
  `SpeechManager_UpdateActiveLines` (expiring old VO lines) and by
  `SpeechSlot_SetLineParamsAndProcess` (processing a newly-set reaction
  slot) — genuinely shared. **Caught and immediately corrected my own
  first-draft name here too**: initially named this
  `Commentary_TryProcessQueuedSlot`, but that overstates its scope once the
  `SpeechManager_UpdateActiveLines` connection was found — renamed to the
  neutral `SpeechSlot_ProcessOrExpire` in the same pass.

3 renames (plus corrections to 2 already-existing script comments that
referenced the old `FUN_` names). This is exactly the kind of ambiguity
that's worth pushing through with register-level tracing rather than
leaving flagged — the payoff was discovering a genuinely reused
sub-component spanning two very different systems (gameplay rider state and
the audio engine singleton), not just resolving two function names.

**Honest status after this session's push**: six independent static-analysis
angles across two whole subsystems (the component-list side and the HUD side)
have now been tried and exhausted without finding the actual write instruction.
This is a genuine, hard-earned wall, not an early stop. Dynamic analysis
(breakpoint/watchpoint on the live `+0x5710` memory address during actual
gameplay) is now the most promising remaining path — static analysis alone
has been pushed about as far as it reasonably can go on this specific
question without it.

## Found the real BdrSeq animation-event queue consumer (2026-07-20, "full throttle" pass)

Picked a genuinely fresh direction from `InGameState_LoadLevel`'s own allocation
list: two tiny (`0xc`-byte) tagged objects, **`"PREAI"`** and **`"PostAI"`**,
constructed right next to the already-explored `"AIWorld"` (terrain streaming).
Both are real `NodeBase`-derived objects, registered into `NodeRegistry` under
types **5** and **0xb** respectively (via a shared helper, `NodeRegistry_Insert`).
Read their own tiny vtables directly and found their `Update` methods (slot 1)
were sitting in **completely un-analyzed code** — no `Function` object existed at
either address. Created both boundaries and traced the full chain:

- **`RiderAnimEvents_ProcessTriggeredQueue`** (PREAI's `Update`, was `FUN_0002dbd0`)
  — walks every rider, calling **`Rider_TriggerAnimActivationPass`** →
  **`RiderAnimQueue_ActivateFlaggedEntries`**, which walks the rider's 3 fixed
  component slots (`rider+0x28`/`+0x80`/`+0xd8`, `0x58` stride — **the exact same
  slots this session's trick-score-writer thread already mapped**) via each
  slot's own `+0x28` BdrSeq-event-queue field, collects every node with a `+0x22`
  flag byte set, and dispatches each to **`BdrSeqEvent_DispatchActivationByType`**
  (an 8-case per-type switch, reusing the same `"BdrSeq"`-tagged handler cluster
  found earlier this session — type 0 just immediately destroys the node via the
  already-named `BdrSeqQueue_RemoveAndDestroyNode`). **This is the real,
  previously-unknown consumer of the insert side found earlier**
  (`Component_InsertNode`/`New_BdrSeq_2` push new animation events onto exactly
  this `+0x28` queue) — the "activate a newly-triggered animation event" step.
- **`RiderAnimEvents_TickActiveQueue`** (PostAI's `Update`, was `FUN_0002ecd0`) —
  the per-frame companion: walks every rider, calling
  **`Rider_TriggerAnimTickPass`** (picks a 1x/2x speed multiplier from a per-rider
  mode flag) → **`RiderAnimQueue_TickAllEntries`**, which walks the same 3 slots'
  queues but calls **`BdrSeqEvent_DispatchTickByType`** (a 12-case per-type
  switch) on **every** node unconditionally — the real "advance this active
  trick/grab/spin animation one frame" step. One handler read in full,
  **`BdrSeqEvent_SelectDirectionalAnimClip`** (type 4) — genuine, substantial
  logic: reads the rider's current analog-stick-derived direction vector and
  selects/blends the correct directional grab/spin animation clip. Every type's
  tick handler also calls **`BdrSeqEvent_FirePhaseFlags`**, which reads a
  bitmask on the node and fires **`AudioSystem_DispatchAnimationCueEvent`** for
  each set phase bit — a genuinely new discovery: animation playback fires
  audio/voice cues at a **finer, per-keyframe-phase granularity** than the
  already-documented `WorldTriggerManager`/`SpeechManager` per-trick-completion
  system, tying directly into the `~0x7df0`-byte `BXAudioSystem` singleton.

10 renames for the dispatch architecture + 1 for the audio hookup = 11 renames,
2-3 newly-created function boundaries.

**Honest status, not forced further**: this closes a real, standing question from
`RE_NOTES_node_base_class.md` (which classes actually use `NodeRegistry`'s
generic per-type Update dispatch) for types 5 and 0xb specifically — but **what
calls `PREAI`/`PostAI`'s own `Update` every frame is still not conclusively
found**. Checked `NodeRegistry_UpdateAllOfType`'s own callers: exactly 2, both
inside the already-named `GameState_ResetTransientTriggerNodes`, and both for
types 3/4 — not 5 or 0xb. `GameState_ResetTransientTriggerNodes` itself has zero
static callers (likely reached through a computed call, matching this project's
established pattern for genuine per-frame drivers that evade static xref
tracing).

**Update (same session, "keep going"): PREAI's own call site WAS found — half
resolved.** `GameState_ResetTransientTriggerNodes` doesn't only call
`NodeRegistry_UpdateAllOfType` for types 3/4 — it *also* has its own raw
bucket-walk loop right before that, over `DAT_0019b994`. Read those bytes
directly: it's not a pointer table, it's a **flat array of 4 literal type IDs**
— `{2, 5, 7, 8}`. The loop calls vtable`+0x1c` (the real `Update` slot) on every
node of each of those 4 types. **Type 5 is `PREAI`** — so
`RiderAnimEvents_ProcessCompletedQueue` *does* get a confirmed call site after
all, just not through `NodeRegistry_UpdateAllOfType`. Types 2/7/8 aren't
individually identified (not chased further this pass), and — critically —
**`0xb` (`PostAI`) is NOT in this list either**, so `PostAI`'s own `Update`
(`RiderAnimEvents_TickActiveQueue`) genuinely still has zero confirmed call
sites anywhere. `GameState_ResetTransientTriggerNodes` itself remains
caller-less statically, so the deeper "what ultimately triggers this" question
still isn't closed — but the immediate "is `PREAI` ever actually invoked"
question now has a real, verified yes.

**Exhaustive check for `PostAI`'s own call site**: found every single bucket-walk
site in the entire binary via `xrefs_to` on `NodeRegistry_PeekHead` (9 call sites
across exactly 5 distinct functions — no more exist anywhere) and checked each
one's literal type arguments: `ScriptObject_DestroyByID`, `NodeRegistry_UpdateAllOfType`
(types 3/4 only), `GameState_ResetAndRebuildTransientNodes`/`GameState_ResetTransientTriggerNodes`
(both use the same `{2,5,7,8}` array), `ReplayManager_CheckNearbyTimerNodes` (type
`0xd`), `Timer_RebuildPlayerRegistry` (types `3`/`0xd`/`4`/`8`). **Type `0xb` does
not appear in any of them.** This isn't "didn't find it yet" — it's a complete,
exhaustive sweep of every possible static dispatch path to `PostAI`'s own
`Update`, and it's absent from all of them. `RiderAnimEvents_TickActiveQueue`
(PostAI) is either reached exclusively through a computed call this project's
established techniques can't trace, or it's genuinely dead/unreachable code in
this retail build — consistent with the many other "stripped in retail"
findings across this whole project. Static analysis has been pushed as far as
it can go on this specific question.

So the generic "what walks every registered Node type each frame"
question from `RE_NOTES_node_base_class.md` remains open even after this find —
but the **content and real purpose** of two of its member types (5/0xb) is now
fully mapped, and — more importantly for the original trick-score question — **the
BdrSeq animation-event queue's full lifecycle (insert → activate → tick →
destroy) is now completely understood on all sides**. No `+0x5710` reference
found in any function touched this pass either (checked the same way as before);
the score write still isn't here, but this closes out a large, previously
completely dark corner of the Rider animation system regardless.

## Correction: `+0x22` is a completion flag, not a trigger/pending flag (same session, "keep going")

The naming above ("activate," "triggered queue," `RiderAnimQueue_ActivateFlaggedEntries`)
was **wrong about what the `+0x22` flag means**, caught and fixed before it could
mislead a future session. Traced where `+0x22` actually gets *set*: BdrSeq's own
real per-frame tick method (its vtable slot 2, renamed **`BdrSeq_TickPlayback`**,
was previously un-analyzed code at `0x00060920`) calls a generic, reused playback-
timer utility, **`AnimTimer_AdvanceAndDetectCompletion`** (was `FUN_00107a60` --
already decompiled earlier this session during the `+0x28`/`+0x50` investigation,
but not renamed at the time). That function advances a normalized playback timer
each frame and, when it detects a "no-loop, stop after this cycle" sentinel
condition, sets `+0x22 = 1` — **a "this animation just finished playing" flag**,
not a "newly queued, needs activating" flag.

So the whole cluster processing flagged nodes is actually a **completion/phase-
transition dispatcher**, not an activation one. Corrected 4 names (old ones kept
commented out in `ssx_auto_rename.py` per project convention, never silently
deleted):

- `RiderAnimEvents_ProcessTriggeredQueue` → **`RiderAnimEvents_ProcessCompletedQueue`**
- `Rider_TriggerAnimActivationPass` → **`Rider_TriggerAnimCompletionPass`**
- `RiderAnimQueue_ActivateFlaggedEntries` → **`RiderAnimQueue_ProcessCompletedEntries`**
- `BdrSeqEvent_DispatchActivationByType` → **`BdrSeqEvent_DispatchCompletionByType`**

This also recontextualizes the 8 per-type handlers cleanly: every one calls
`BdrSeqQueue_RemoveAndDestroyNode` first (tear down the just-finished entry), and
all but type 0 immediately call `RiderAnimation_TriggerByEventCode` with a new
event code — a genuine **multi-phase animation state-machine chain** (e.g. type 7
maps a whole range of old grab-variant codes `0x226`-`0x277` down to a handful of
canonical follow-up codes `0x22e`-`0x235`). Type 0 is the only one that doesn't
chain to anything — "this phase has no follow-up, just stop." 2 more renames for
the newly-understood timer functions. 6 renames total this correction.

This is exactly the kind of mistake this project's discipline exists to catch:
verified live, re-checked against the actual bit-setting code rather than assumed
from surface-level naming intuition, and corrected transparently rather than left
to quietly mislead whoever reads these names next.

## Finished the per-type tick handler sweep (same session, "keep pushing")

Read all 7 remaining `BdrSeqEvent_DispatchTickByType` handlers (types 5-11; type
4 was already done, types 0-3 share identical inline code with no separate
function). Every one follows the exact same shape as type 4: read rider-state
field(s), select/blend the appropriate trick-animation clip via
`FUN_00061d70(slot, clipID)`, set blend-state fields on the `BdrSeq` node. Named
by *verified input shape* rather than guessing the real-world trick name
(spin/grab/flip/grind-turn), matching this project's `UnknownOpcode09`-style
honesty precedent:

- **`BdrSeqEvent_TickType5_SelectCategoryClip`** — single clip via a
  rider+0x450 category index.
- **`BdrSeqEvent_TickType6_UpdateBlendDuration`** — no clip selection at all,
  just recomputes one blend duration from rider+0x234.
- **`BdrSeqEvent_TickType7_SelectTiltDirectionClip`** — 2-way selection on the
  sign of a tilt value (rider+0x240).
- **`BdrSeqEvent_TickType8_SelectCategoryClip`** — single clip via
  rider+0x450, duration from rider+0x21c.
- **`BdrSeqEvent_TickType9_SelectVectorDirectionClip`** — reads a 2D vector
  (rider+0x2a8/+0x2ac), computes its angle via an atan2-shaped helper
  (`FUN_0015ca54`), selects by angular sector — the same directional-clip
  family as type 4, different source fields.
- **`BdrSeqEvent_TickType10_SelectVectorDirectionClip`** — same
  atan2-sector shape, different vector pair (+0x24c/+0x258) and a wider
  category table.
- **`BdrSeqEvent_TickType11_UpdateBlendDuration`** — like type 6, duration
  only, from rider+0x264.

**Found something more important while reading these**: the shared function
called by types 0-3 (and by several of the type handlers above, passed the
speed multiplier) — **`BdrSeq_AdvanceBlendTimersAndDetectCompletion`** (was
`FUN_000604b0`) — turned out to be an even more central completion-detector
than `AnimTimer_AdvanceAndDetectCompletion`. It advances **every active blend
channel** on a node (`+0x38` = channel count, `+0x3c[]` = per-channel timers),
checking each channel's timer against **8 threshold values** pulled from the
node's own embedded keyframe/event-data table — exactly the same 8 phase-flag
bits `BdrSeqEvent_FirePhaseFlags` fires audio cues for
(`node+0x64`/`+0x65`/`+0x66`/`+0x67`). When the last channel's timer reaches
the clip's total duration, it sets **the same `+0x22` completion flag** —
independent confirmation of the corrected understanding above, and likely the
*real* completion-detector for `BdrSeq`'s richer multi-channel case (vs.
`AnimTimer_AdvanceAndDetectCompletion`, probably used by the simpler
single-timer `SkelAnim` class instead).

This fully completes the per-type tick-handler sweep — every case in
`BdrSeqEvent_DispatchTickByType` is now named and understood at the field
level. 8 renames this pass (7 type handlers + 1 shared timer function).

## Fresh `RiderEvent`/ground-physics territory found via a coincidental byte-search hit (2026-07-20, later session)

Picked up while checking whether the manual's "Snow Crystal" multiplier strings
(`kShowoffLegendMult2`/`3`/`5`, ids 511/512/513 — see the newly-added
`reference-official-manual-terminology` memory) were referenced anywhere in the
binary. A `/search_bytes` hit for the literal dword `0x1ff` (511) turned out to be a
**false positive for the localization system** — but a genuine, valuable hit for a
completely different, previously-unexplored cluster: `RiderAnimation_TriggerByEventCode`
is called with the literal animation event code `0x1ff` in several places (numerically
coincidental with the string id, unrelated in meaning). Followed the callers rather than
discard the "false positive":

- **`RiderEvent_ToggleSwitchStance`** (was `FUN_000221e0`, called from
  `RiderEvent_DispatchTypeA`) — toggles a stance flag, flips two direction/velocity
  vectors to their negation, and fires the switch-stance animation event depending on
  state. **This is the actual code behind the manual's "Switch" mechanic** (a trick
  performed with goofy-foot forward) — nice, clean cross-reference between the manual
  and the code, even though it wasn't the crystal-multiplier lead originally being
  chased.
- **`RiderEvent_ResetToRegularStance`** (was `FUN_000241e0`, also from
  `RiderEvent_DispatchTypeA`) — the trivial counterpart, resets back to regular stance.
- **`RiderEvent_UpdateGroundSteeringState`** (was `FUN_00023550`, called from
  `RiderEvent_DispatchTypeB`) — decodes an input bitmask and applies analog steering/
  lean-blend physics, ending in a call to the already-named `HUD_ShowTimeGapCallout`.
- **`Rider_EvaluateGroundMovementTransition`** (was `FUN_00025bd0`) — called directly
  from the core `Rider_PhysicsMode1_GroundRide`. Computes 3 velocity/lean threshold
  checks, resolves a component-slot category via the already-named
  `ComponentSlot_ResolveCategoryFromType`, and dispatches one of several ground-movement
  animation event codes before transitioning `RiderEvent`'s own state machine — the
  "which turn/movement animation should play right now" selector for ground riding.
- **`Rider_SetPhysicsMode`** (was `FUN_00032c60`, referenced but never itself named in
  earlier sessions' comments as "the `RiderEvent_SetState`-family setter") — the actual
  physics-mode transition dispatcher: calls an enter-hook for the new mode, stores it,
  then an exit-hook for the old mode. Backs the already-named `Rider_PhysicsMode1_GroundRide`
  family (`this+0x484` is the current mode ID).

5 renames. **This is more of the ~30 still-unread `RiderEvent_DispatchTypeA`/`TypeB`
case handlers** flagged as open territory in this file's earlier "swept all ~35 handlers"
section — a good, low-risk vein to continue in if this thread is picked up again. The
original crystal-multiplier lead that led here remains unresolved (no hits found for
"Crystal"/"Legend" tag strings or the sequential string-id table) — an honest side-effect
discovery, not the answer to the question that started the search.

## `RiderEvent_DispatchTypeA` fully closed (immediate follow-up)

Continued straight through the rest of `RiderEvent_DispatchTypeA`'s switch — every
remaining case is now named:

- **`RiderEvent_ResetSubStateTimer`**/**`ResetSubStateFields`**/**`ResetAllTimersAndNotify`**/
  **`ResetTimerAndSyncFlag`**/**`ClearSubStateFlag`** (cases 2/3/0xe/0xd/0x16) — a family of
  small, structurally trivial sub-state reset handlers (zero/reset timer or flag fields),
  named individually since they're each a distinct case even though the logic is simple.
- **`RiderEvent_CheckVelocityThreshold`** (case 6) — squared-vector-length vs. threshold
  check, sets a stationary/moving flag.
- **`RiderEvent_ApplyCharacterTuningValue`** (case 9) — reads a per-character tuning byte
  and blends it into a float pair via fixed constants.
- **`RiderEvent_ProcessGrabInput`** (case 0xc) — **the core grab-input-combo resolver**.
  Builds a 12-bit combo bitmask from a large per-item lookup table, one bit per possible
  simultaneous grab button — directly matches the manual's "press two or more grab
  buttons at the same time to perform more complex grabs" description (see
  `reference-official-manual-terminology` in memory) — then does a vector-projection/
  steering calculation and dispatches one of 3 grab-related animation events.
- **`RiderEvent_ResolveJumpTakeoffStance`** (case 0xf) — a substantial state machine
  toggling a 4-value stance state and dispatching one of 4 sequential takeoff animation
  codes, reading as the regular/switch/fakie takeoff-animation selector. Named on overall
  role, not every branch's exact tuning constant.
- **`RiderEvent_ApplyGrabDecayScaling`** (case 0x10) — decay-scaling on a grab/tweak
  accumulator pair, gated on a threshold and a stance-mismatch check.
- **`RiderEvent_InvokeStateEnterCallback`** (case 0x13) — trivial generic vtable
  pass-through, a polymorphic state-enter hook with no case-specific logic.

11 renames this batch (16 total across both `RiderEvent_DispatchTypeA` passes this
session). **`RiderEvent_DispatchTypeA`'s entire switch is now fully named** — every case
resolves to either a named handler or the already-generic `Node_NoOpStub2`.
`RiderEvent_DispatchTypeB`'s cases remain the next natural continuation if this thread is
picked up again.

## `RiderEvent_DispatchTypeB` fully closed — the rail-riding subsystem, mapped end to end

Continued straight into `RiderEvent_DispatchTypeB`'s 17 remaining unnamed cases. Turned
out to be almost entirely **the rail-riding subsystem** — directly matching the manual's
"Rail Riding" section ("hold down then release A to jump on a rail... rotate CCW/CW...
adjust your balance on the rail"):

- **`RiderEvent_EnterRailRide`**/**`RiderEvent_RetryRailModeTransition`**/
  **`RiderEvent_CheckRailModeTransition`**/**`RiderEvent_TransitionToRailMode`** — the
  family of "jump onto a rail" entry handlers, all calling `Rider_SetPhysicsMode(2)`
  (confirming mode 2 = rail-riding physics, alongside the already-known mode 1 =
  `Rider_PhysicsMode1_GroundRide`).
- **`RiderEvent_TransitionToGroundMode`** — the inverse: exits back to
  `Rider_SetPhysicsMode(1)` once a decay timer fully expires.
- **`RiderEvent_ProcessRailLeanInput`**/**`ProcessRailBalanceInput`**/
  **`ProcessRailBalanceAndExit`**/**`SmoothSteeringLean`** — the balance/lean input
  processing family, all sharing the same input-decode + `Math_ClampTowardTargetWithMargin`
  smoothing shape already seen in the ground-steering handlers.
- **`RiderEvent_CheckRailComboTimeout`** — checks 5 separate decay timers for being fully
  at rest, and if so resets combo-adjacent fields on a linked sub-object — a genuine "no
  further rail tricks within the window ends the grind combo" mechanic.
- **`RiderEvent_UpdateRailRideMovement`** — **by far the largest handler in this whole
  cluster**, the master per-frame rail-ride physics/balance tick (`Vector4_DotProduct`
  rail-alignment projection, dual-axis lean smoothing, an exit-rail check, sub-states
  `0x23c`/`0x23d` for entry-orientation variants). Named on overall role only — this one
  function would need its own dedicated deep-dive to decode every branch; the smaller
  satellite handlers around it are well understood.
- **`RiderEvent_ResolveLandingOrientation`** — toggles the same stance flag
  `RiderEvent_ToggleSwitchStance` uses, based on a landing-angle dot-product check,
  dispatching one of 3 landing animations.
- **`RiderEvent_ProcessGroundInputAndTransition`**/**`ApplyLeanAndCheckSubState`**/
  **`ProcessInputWithComponentDecay`** — more ground/rail-adjacent input processors, same
  family shape as `RiderEvent_UpdateGroundSteeringState`.
- **`RiderEvent_UpdateTimeGapDisplay`** — trivial, just the standard HUD callout gate
  standalone.
- **`RiderEvent_UpdateEndRaceFadeSequence`** — **the one handler in this cluster that
  isn't rail-related**: a two-stage fade-alpha timer that, once expired, calls the
  already-named `VenueStaging_ExitState` — the race-end fade-to-results-screen sequence.

17 renames. **Both `RiderEvent_DispatchTypeA` and `RiderEvent_DispatchTypeB` are now
completely closed** — every case in both switches resolves to a named handler. This
closes out the multi-session "~35 RiderEvent handlers, mostly unread" thread for good,
and gives the rail-riding mechanic (previously only known from the manual and a couple of
already-documented functions) a genuinely complete architectural map: mode transitions,
balance/lean input, combo timeout, and the master physics tick are all identified.

## Two more shared rail-riding helpers, closing the loop to `TrickCombo` (immediate follow-up)

Pushed one level deeper into `RiderEvent_UpdateRailRideMovement`'s own sub-calls, since
they're reused across several of the handlers just named:

- **`RiderEvent_CheckRailExitJump`** (was `FUN_0001f5b0`) — decodes the stick input's
  angle and checks it against 8 angular sectors; if it falls in a valid "jump off the
  rail" gap, fires an animation event and — the key find — calls the already-named
  **`TrickCombo_StartNewSequence`** and **`Rider_UpdateCueTimer`** before transitioning
  state. This is the exact code behind the manual's "if you really want to show off some
  moves, jump off the end of the rail with a trick" — and it directly ties the
  rail-riding cluster into the `TrickCombo` scoring system resolved earlier this session:
  jumping off a rail with style starts a fresh trick combo sequence.
- **`RiderEvent_RotateOnRail`** (was `FUN_00024aa0`) — advances a 90°-per-call rotation
  counter and switches on the same 4-value stance state
  `RiderEvent_ResolveJumpTakeoffStance` uses, dispatching one of 8 sequential rotation
  animations (one per stance × direction combo) — the manual's "press left/right to
  rotate counter-clockwise or clockwise on the rail."

2 renames. A satisfying architectural closure: the rail-riding subsystem doesn't just
stand alone — its exit path is a real, direct caller into this session's other big find
(`TrickCombo`), confirming rail tricks and air tricks feed the exact same scoring
pipeline.

## Correction: "physics mode 2 = rail-riding" was an overreach, plus a genuine new discovery resolving an old open item

While tracing `Math_WrapAngleToRange`'s (was `FUN_0001eac0`) other callers — a generic
math utility reused by many of the just-named rail handlers — found `Rider_PhysicsMode5_NoTerrain`
in the call graph and cross-checked it against `Rider_DispatchPhysicsMode`'s mode table.
This caught a real mistake: several rename comments in the batch above described physics
mode 2 (`Rider_SetPhysicsMode(2)`, called by `RiderEvent_CheckRailModeTransition`/
`TransitionToRailMode`/`EnterRailRide`/`RetryRailModeTransition`) as **"the rail-riding
physics mode."** Checking `Rider_PhysicsMode2_GroundContact`'s own decompiled body
directly (grepped all 637 lines) found **zero overlap** with any of the confirmed
rail-specific fields (`ComponentSlot` category checks, `Vector4_DotProduct` rail-alignment
math, the `0x23c`/`0x23d`/`0x453c` fields `RiderEvent_UpdateRailRideMovement` uses) — the
claim doesn't hold up. **Fixed immediately** in all 4 affected rename comments in
`ssx_auto_rename.py`, per the standing correction rule.

Worth noting: an *earlier* session had already handled this exact ambiguity correctly —
its own rename comments for `Rider_PhysicsMode5_NoTerrain`/`PhysicsMode6_NoTerrain`
explicitly say "airborne / rail-grind / wall-ride; exact identity not resolved," rather
than guessing. This session's mistake was re-guessing a specific answer (mode 2) for a
question an earlier pass had already correctly left open.

**And that earlier open question is now very likely resolved, just not at mode 2.**
`Rider_CheckRailAttachmentAlignment` (was `FUN_00029ff0`, called from
`Rider_PhysicsMode5_NoTerrain`) reads the same per-item lookup table
`RiderEvent_ProcessGrabInput` uses, computes an alignment delta against a nearby point via
a rotation matrix, checks it against angular sector thresholds, and — regardless of which
sector matched — calls **`Rider_SetPhysicsMode(6)`**. This reads as: while airborne (mode
5), scan for a nearby attachable surface; if the alignment checks out, transition into
mode 6 to actually ride it. This is a strong, evidence-based candidate for resolving the
`PhysicsMode5`/`6` "exact identity not resolved" flag — likely rail or wall attachment,
found via a completely different code path than `RiderEvent_DispatchTypeB`'s own
rail-adjacent handlers.

**Honest, unresolved tension, not force-unified**: this session now has *two* separate
bodies of evidence that both look rail/attachment-related — `RiderEvent_DispatchTypeB`'s
cluster (tied to physics mode 2, `Rider_SetPhysicsMode(2)`) and this new
mode-5-to-6 transition. Whether these are the same mechanic seen from two angles, two
genuinely distinct mechanics (e.g. thin-rail grinding vs. a different attachable surface
type), or something else entirely hasn't been reconciled. Flagged honestly as a real open
question rather than picking one interpretation to force a clean narrative — a good
next thread if this area is revisited.

**Follow-up check, same pass: read `Rider_PhysicsMode6_NoTerrain`'s full body directly —
zero overlap with the confirmed rail fields, same as mode 2.** What mode 6 actually
does: gravity/velocity integration, a quaternion-style orientation blend
(`FUN_00139870`/`FUN_001396d0`, slerp-shaped), and indexes into a **100-byte-stride
table** at `Application(DAT_001e3c7c)+0x72c+0x24` using a stored index (`this+0x2c8`) —
reads like generic "follow a predefined path/track object" movement, architecturally
consistent with how rails are commonly implemented (as spline curves) but **not the same
table shape** as the already-documented `SplinePath` object (`this+0xc`/`this+0x1c`
segment array), so not conclusively the same thing.

**However, reading `Rider_PhysicsMode5_NoTerrain`'s full body (the mode that calls
`Rider_CheckRailAttachmentAlignment`) in its entirety strengthens the hypothesis rather
than weakening it.** It's a large general airborne-flight physics tick (drag/lift-style
aerodynamics, its own internal event-code range `0x2dc`-`0x2e0` distinct from the
rail-rotation `0x25x` range) that, near its two exit paths, checks a dot product against
a nearby object and **gates on `ComponentSlot_ResolveCategoryFromType() == 0x11`** — a
*specific* component category (not yet independently decoded, but distinct from every
other category value seen elsewhere in this project: `0xe` for ground-movement, `1` for
input-decay, `0xc` for the earlier trick/grab-input context) — before calling
`Rider_CheckRailAttachmentAlignment` and transitioning toward mode 6. This reads as: while
in general free-flight (mode 5), continuously check for a nearby category-`0x11` object
(plausibly "attachable structures" — rails and/or walls) and, on alignment, grab onto it.

**Best current picture, still not fully reconciled with `RiderEvent_DispatchTypeB`'s own
mode-2-tied cluster**: modes 5→6 look like a genuine "detect and attach to a nearby
grindable/attachable object while airborne" mechanic, complementary to (not necessarily
the same code path as) the ground-level rail-entry handlers already named. These could
be two different ways of getting onto a rail (jumping onto one from a standing start vs.
grabbing one mid-air), or mode 5/6 could cover a different attachable type entirely (e.g.
wall-rides). Not forced to a single conclusion — a good, well-scoped next thread (decode
category `0x11` and the `Application+0x72c+0x24` table) rather than a dead end.

2 new renames this correction pass (`Math_WrapAngleToRange`, `Rider_CheckRailAttachmentAlignment`),
plus 4 corrected rename comments on already-named functions (no function-name changes, just
fixed prose).

## Closing out the `Commentary_QueueEvent` wrapper-caller sweep (2026-07-21)

Picked back up the "Bonus find" thread above, whose closure note said "most of
the ~29 individual wrapper callers... remain unread — a rich, well-scoped,
low-risk thread for a future session." A fresh `xrefs_to` on
`Commentary_QueueEvent` (`0x00158630`) turned up far more than 29 — **60+
total callers**, most already named from unrelated threads across many
between-session passes (e.g. `Commentary_QueueRivalryEvent`,
`Rider_TriggerOvertakeCommentary`, `Commentary_DispatchRelationshipTierReaction`).
Named every remaining `FUN_`-prefixed caller across 4 batches, **36 renames
total** — confirmed via a final `xrefs_to` sweep returning zero unnamed
callers left.

Full list (all in `ssx_auto_rename.py`, see that file for per-function
opcode/context comments): `Commentary_QueueRaceStartReaction`,
`Commentary_QueueOvertakePhraseA/B/C/D`, `Commentary_QueueRelationshipTierReaction`,
`Commentary_QueueGenericMilestoneReaction`/`ReactionAlt`,
`Commentary_QueuePositionAnnouncementReaction`, `Commentary_QueueRivalryPhraseVariant`,
`Commentary_QueueRacePositionReaction`, `Commentary_DispatchRaceStandingsReaction`,
`Commentary_QueueRaceStandingsReaction`, `Commentary_QueueCollisionReactionA/B`,
`Commentary_QueueMiscEventReaction`, `Commentary_QueueLapPhraseA/B/C/D`,
`Commentary_QueueRaceTransitionAck`/`AckAlt`,
`Commentary_DispatchLeaderAnnouncementReaction`,
`Commentary_QueueLeaderAnnouncementReaction`,
`Commentary_DispatchPlayerStatusFlagReaction`,
`Commentary_QueueAmbientVariantReaction`, `Commentary_QueueAmbientReaction`,
`Commentary_QueueScriptedEventA/B`, `Commentary_QueueRaceEventDetailReaction`,
`Commentary_QueueAmbientRandomReaction`, `Commentary_QueueRivalryShowcaseReaction`,
`Commentary_DispatchRaceMarginReaction`, `Commentary_DispatchTrickFlagReaction`,
`Commentary_QueueComponentCollisionReaction`, `Commentary_QueueRepeatedCollisionReaction`.

A few notable structural findings from this sweep:

- **`Commentary_QueueScriptedEventA/B`** (were `FUN_00122e80`/`FUN_00122f20`)
  are called directly from `ScriptVM_DispatchOpcode` — the first confirmed
  case of the level-script VM directly triggering a commentary event, not
  just gameplay systems.
- **`Commentary_QueueRivalryShowcaseReaction`** (was `FUN_00122220`) is called
  3x from `VenueStaging_TickCameraScriptCommand`, gated on
  `GameMode_Current==4`, and computes its bitmask via
  `AggressionManager_GetRelationshipField3` — a camera-script-driven rivalry
  narration trigger, another confirmation of the
  commentary-system ↔ `AggressionManager` link.
- **`Commentary_QueueComponentCollisionReaction`**/`Commentary_QueueRepeatedCollisionReaction`
  (were `FUN_001211d0`/`FUN_001210f0`) are called from `Rider_HandleComponentStateEvent`
  (directly, and via 2 small intermediate gating wrappers) — ties the
  commentary queue into the per-component collision-event path, alongside
  the already-named `Commentary_QueueCollisionReactionA/B`.
- **`Commentary_DispatchTrickFlagReaction`** (was `FUN_001223e0`) reads/writes
  a sticky bitmask at `this+0x7ddc` — close to, but **not the same field**
  as, the `this+0x7dd0` offset searched exhaustively (and confirmed empty)
  during the narrator-commentary-consumer investigation in
  `RE_NOTES_audio_system.md`. Worth remembering as a near-miss so a future
  pass doesn't conflate the two fields.

36 renames.

## Closing the inner sub-helper cluster too (same pass, 2026-07-21)

Immediately followed up on the "remaining, not pursued" inner sub-helpers
flagged above. Named all 7, plus 3 more directly discovered via their own
xrefs while tracing the flagged set (kept in the same batch since they're
the same tightly-coupled cluster) — **11 renames total**, all verified live.

- **`Commentary_LookupCategoryDescriptor`** (was `FUN_001584a0`) — the shared
  low-level resolver underneath the whole system: linear-scans a fixed 8-
  entry table (`DAT_002044c0`) matching 2 identity bytes from an opcode-
  entry pointer against 2 bytes per table entry. Called from both
  `Commentary_ResolveEventTableEntry` and `Commentary_ResolveEventCategory`
  (explaining why it sits address-wise right between them), plus 2 tiny
  field-accessor wrappers found the same way:
  **`Commentary_GetCategoryDescriptorPriority`** (was `FUN_00158530`,
  descriptor byte `+9`) and **`Commentary_GetCategoryDescriptorLimit`** (was
  `FUN_00158590`, descriptor short `+0x14`).
- **`Commentary_FindEventTableSlotById`** (was `FUN_00158450`) — a generic-
  shaped index-array lookup, sole caller `Commentary_ResolveEventTableEntry`.
- **`Commentary_ReleasePlaybackSlot`** (was `FUN_00158960`) — the direct
  counterpart to `Commentary_ClaimOrEvictPlaybackSlot`'s claim path: frees a
  slot, clears any category current/pending references to it, walks and
  clears a linked chain of related entries, decrements the category's
  active-count. Called straight from `Commentary_ClaimOrEvictPlaybackSlot`
  as its eviction step.
- **`Commentary_GetElapsedTimeCallback`** (was `FUN_001590c0`) — a trivial
  indirect-call thunk through a global function pointer (`DAT_00203df8`),
  used everywhere a slot's elapsed time needs checking against its duration.
  Called from `Commentary_QueueEvent`, `Commentary_ClaimOrEvictPlaybackSlot`,
  and the newly-found `Commentary_FindBestActiveSlotInCategory`.
- **`RNG_NextCommentaryPickUInt32`** (was `FUN_001590d0`) — turned out to be
  a real, second RNG core: a self-contained 5-word additive/carry-chain
  (lagged-Fibonacci-shaped) generator over its own static state, completely
  address-distinct from the main `RNG_NextGlobalUInt32` (`0x0012a4c0`). Its
  only callers are `RNG_ChooseWeightedCandidate`'s 2 call sites, so scoped
  the name to commentary rather than assuming it's the engine's general RNG.
- **`Commentary_FindRecentPickInHistory`** (was `FUN_001592c0`) /
  **`Commentary_RecordRecentPick`** (was `FUN_00159330`) — confirm the
  "avoid recent repeats" mechanism `RNG_ChooseWeightedCandidate`'s original
  comment only speculated about: a real 32-entry circular history buffer
  (`DAT_00203d60`/`62`) of `(value, category)` pairs, searched before each
  pick and updated after.
- **`Commentary_CategorySlotsAreValid`** (was `FUN_001588f0`) — bounds-check
  predicate on a category's 2 tracked slot-index fields, sole caller
  `Commentary_QueueEvent`.
- **`Commentary_FindBestActiveSlotInCategory`** (was `FUN_00158c10`, moderate
  confidence) — a level below `Commentary_ClaimOrEvictPlaybackSlot`'s own
  simpler linear scan: walks the slot table for a given category, evicts any
  expired entries found along the way, and tracks the best remaining
  candidate by priority/duration. Calls one more still-unnamed filter helper
  (`FUN_00158d90`) not traced this pass.

11 renames. Immediately closed the one remaining leaf too: **`Commentary_FilterListContainsId`**
(was `FUN_00158d90`) — a generic `{count, short-array}` membership-test
predicate, sole caller `Commentary_FindBestActiveSlotInCategory` (filtering
candidate slots against an optional caller-supplied ID list). 1 more rename.

**Genuinely closes the entire `Commentary_QueueEvent` subsystem — every
wrapper caller, every core dispatch function, and every inner helper is now
named. Zero unnamed functions remain anywhere in this subsystem.**

12 renames this section.
