# RE notes: the `Node` base class (long-hypothesized, now found)

Since early in this project, `RE_NOTES_camera_system.md` and others have repeatedly
run into the same wall: a class's per-frame "Update" method is reachable only via a
`DATA` cross-reference from its own vtable slot, with **no traceable caller** —
observed for `Component_UpdateAll`, `Rider_UpdateSubsystems`,
`ReplayManager_UpdateSequenceState`, `Camera_UpdateBehaviorTimer`, and
`Camera_UpdateAudioPanning`. The standing hypothesis was that a per-frame loop
iterates a list of shared base-class `Node*` pointers and calls a fixed vtable
offset generically, invisible to static xref analysis. That hypothesis is now
directly supported by a real vtable found this session.

## How it was found

While tracing `Camera_ScalarDeletingDestructor` (already named, vtable slot 0 of
Camera's own vtable at `0x00189c88`), its body turned out to be:

```c
undefined4 * __thiscall Camera_ScalarDeletingDestructor(undefined4 *param_1, byte param_2)
{
  *param_1 = &PTR_FUN_00187c20;             // reset vptr to a DIFFERENT vtable
  FUN_000aa960();                            // call the base-class teardown
  param_1[0xd] = &PTR_CameraShakeMode_ScalarDeletingDestructor_00189280;
  ... (pool-or-heap free, standard scalar-deleting-destructor tail)
}
```

The vptr reset to `0x00187c20` — not back to Camera's own `0x00189c88` — is the
standard C++ "destructor chaining" idiom: once Camera's own teardown is done, the
compiler resets the vtable pointer to the **immediate base class's** vtable before
any further (base-level) teardown logic runs, so virtual calls made from that point
on resolve to the base class, not to an already-destructed derived override.

## The base vtable (`0x00187c20`)

**16 direct references from construction/destruction code across the whole
binary**, all `DATA` xrefs (from Camera's own destructor, plus 15 more unrelated
functions: `FUN_000538d0`, `FUN_000f3900` x2, `FUN_00053b60`,
**`Node_ScalarDeletingDestructor`** itself, `FUN_000311f0`, `FUN_00049280`,
`FUN_000494f0`, `FUN_0005a890`, `FUN_00036970`, `FUN_000aa990`, `FUN_000aa9c0`,
`FUN_000aacc0`, `FUN_00050210`) — confirming this is a genuinely shared root class,
not something Camera-specific. This is the strongest evidence yet for the project's
long-standing "shared `Node*` base" hypothesis, so the class is named `Node`
throughout, matching the terminology already used informally in earlier notes.

Read and compared all 16 slots against Camera's own (derived) vtable slot-by-slot:

| Slot | `Node` base (`0x187c20`) | Camera derived (`0x189c88`) | Overridden? |
|---|---|---|---|
| 0 | `Node_ScalarDeletingDestructor` | `Camera_ScalarDeletingDestructor` | yes |
| 1 | `Node_NoOpStub1` | `Camera_UpdateBehaviorTimer` | yes |
| 2 | `Node_NoOpStub1` | `Node_NoOpStub1` (same) | no |
| 3 | `InputDevice_StubReturnFalse` | same | no |
| 4 | `InputDevice_StubReturnFalse` | same | no |
| 5 | `RaceState_NullHandler` | same | no |
| 6 | `RaceState_NullHandler` | same | no |
| 7 | `Node_UpdateStub` | `Node_NoOpStub2` | yes (differently) |
| 8 | `Node_NoOpStub2` | same | no |
| 9 | `Node_CheckField0x14` | same | no |
| 10 | `RaceState_NullHandler` | same | no |
| 11 | `Node_NullSubObject_Destructor` | `Camera_DelegateUpdate` | yes |
| 12 | `Node_NoOpStub2` | `CameraAudioPanningMode_ScalarDeletingDestructor` | yes |
| 13 | `Node_NoOpStub3` | `Camera_UpdateAudioPanning` | yes |
| 14 | `Node_NoOpStub4` | `Camera_UpdateViewTransform` | yes |
| 15 | `Node_NoOpStub2` | `Rider_CompareThresholdGE` | yes |

Camera overrides 8 of 16 slots; the other 8 are inherited unchanged from `Node`.

## What each unique/new `Node` slot turned out to be

- **`Node_ScalarDeletingDestructor`** (slot 0) — same shape as every other
  destructor in this project: resets its own vptr to `0x00187c20` (self), calls
  `FUN_000aa960` (an even-more-base teardown step, not chased further), then the
  standard pool-or-heap free tail.
- **`Node_NoOpStub1`**/**`Node_NoOpStub2`** (slots 1/2 and 8/12/15 respectively) —
  both are literal 1-byte `{ return; }` stubs. ~100 xrefs each (display-capped),
  reused across dozens of unrelated classes' vtables — the same pattern already
  named `InputDevice_StubReturnFalse`/`RaceState_NullHandler` in earlier sessions.
  Not linker-folded into one address (each trivial stub keeps its own distinct
  address despite identical bodies) — mildly surprising but not investigated
  further.
- **`Node_UpdateStub`** (slot 7) — **the most interesting find**: `DebugBuffer_Write`
  + a `0xdeadbefc` marker constant, byte-for-byte the same shape already documented
  in `Rider_UpdateSubsystems` as a "stripped no-op profiler marker — real logic
  compiled out in this retail build." This slot was almost certainly a genuine
  `Update()` virtual method in a development build, and got compiled down to an
  inert debug-telemetry stub for retail. Named to match the existing
  `AIWorld_UpdateStub` precedent. **This is likely the exact slot the per-frame
  loop calls on every live `Node`** — explaining, at last, why none of
  `Component_UpdateAll`/`Rider_UpdateSubsystems`/`Camera_UpdateBehaviorTimer`/etc.
  have a traceable caller: the real dispatch happens through this slot on a generic
  `Node*`, and in THIS retail binary the base implementation is a no-op — but
  derived classes still override it (Camera overrides slot 7 with... `Node_NoOpStub2`,
  i.e. Camera does NOT override this specific slot with real logic either).
  Cross-referencing: none of the 5 previously-orphaned Update methods
  (`Component_UpdateAll`, `Rider_UpdateSubsystems`, `ReplayManager_UpdateSequenceState`,
  `Camera_UpdateBehaviorTimer`, `Camera_UpdateAudioPanning`) sit at vtable offset
  `+0x1c` (slot 7) in their respective classes — so this specific slot is not
  directly the mechanism for those five. Still valuable negative/structural
  evidence: it independently confirms retail stripping of an `Update`-shaped slot
  at the `Node` root, consistent with (if not identical to) the same phenomenon
  recurring at other offsets in derived classes.
- **`Node_CheckField0x14`** (slot 9) — `return *(int*)(this+0x14) != 0;`, a trivial
  boolean predicate. **38 xrefs**, all `DATA`, from vtables spanning a huge address
  range (`0x00187c44` through `0x001a2694`) — confirms dozens of distinct classes
  share this exact slot unchanged, strong independent confirmation this really is
  a common root class. Semantic meaning of the `+0x14` field not determined (an
  optional owner/parent/target pointer, going by the shape).
- **`Node_NullSubObject_Destructor`** (slot 11) — turned out to be **another
  self-referential embedded 2-slot mini-vtable**, exactly the same idiom as
  `CameraShakeMode`/`CameraAudioPanningMode` found earlier this session: it resets
  its object's vptr to `0x00187c4c`, which is its *own* position in this same
  table (slot 11 = `0x187c20 + 11*4 = 0x187c4c`) — meaning slots 11-12 together
  form a nested `{destructor, method}` pair for a tiny stateless helper object,
  with slot 12's "method" being `Node_NoOpStub2` (i.e. a no-op — this nested helper
  object does nothing in this build either).
- **`Node_NoOpStub3`**/**`Node_NoOpStub4`** (slots 13/14) — both literal
  `{ return; }`, only 1-2 bytes long, found only within this specific vtable (not
  independently confirmed shared elsewhere, unlike stubs 1/2).

## Also resolved this session: `CameraAudioPanningMode`

While investigating Camera's slot 12 (`0x00189cb8`), found it's **not actually part
of Camera's own logical vtable** — it's a *third* self-referential embedded mini
2-slot vtable (same idiom as `CameraShakeMode`, see `RE_NOTES_camera_system.md`),
physically laid out immediately following Camera's real 12-slot vtable. Slot 0 is
**`CameraAudioPanningMode_ScalarDeletingDestructor`** (was `FUN_00055930`, resets
vptr to `0x00189cb8` — its own position), slot 1 is the already-documented
`Camera_UpdateAudioPanning`. Both `CameraAudioPanningMode_ScalarDeletingDestructor`
and `Camera_UpdateAudioPanning` have exactly one xref each (their own vtable slot),
consistent with this reading. Functionally this doesn't change
`Camera_UpdateAudioPanning`'s documented behavior — only clarifies the mechanism
(a nested stateless mode-object, like `CameraShakeMode`, not a literal slot of
Camera's 12-slot base vtable).

## Conclusion

This is genuinely the best evidence this project has produced for the "shared
`Node*` base class, per-frame loop dispatches through it generically" hypothesis
that's been informally repeated for 5+ classes without ever being pinned down. The
base class exists, is shared by 16+ derived classes' construction/destruction code,
and has a slot (`Node_UpdateStub`) matching the exact "stripped Update in retail"
shape already seen elsewhere. It does **not**, however, hand over a smoking-gun
"here is the per-frame iterator" caller — that would need either dynamic analysis
or a much broader sweep for whatever generic list-walking code calls a fixed vtable
offset on a `Node*`-typed pointer, which is a different, larger search than reading
one more vtable. Good candidate for a dedicated future session if picked up again:
search for code that calls a fixed small-integer vtable offset (multiples of 4
between the confirmed `Node`-derived classes' override points) through a pointer
NOT of any single confirmed derived type.

## Update: the chain goes one level deeper, and connects to already-known systems

Followed `Node_ScalarDeletingDestructor`'s own base-teardown call
(`NodeBase_UnlinkAndResetVtable`, was `FUN_000aa960`) one more level and found the
true bottom of the hierarchy, plus a clean connection back to systems this project
already fully mapped many sessions ago:

- **`NodeBase_UnlinkAndResetVtable`** resets vptr to yet another vtable
  (`0x0019a328`) and inlines a doubly-linked-list unlink
  (`*(next+4)=prev; *(prev+8)=next`). Direct byte-for-byte comparison against
  `NodeRegistry_Remove`'s own body confirms **this is the exact same unlink
  logic** — not a new, separate list. `NodeBase_UnlinkAndResetVtable` is simply a
  shared, non-virtual extraction of `NodeBase`'s own teardown step, letting any
  derived class's destructor run it without a virtual dispatch through `NodeBase`'s
  vtable.
- **`0x0019a328` is `NodeBase`** — literally the same class as
  `NodeBase_Construct`/`NodeBase_ConstructRoot`, already documented in an earlier
  session as "true root base class for every script node" (type ID at `+0x14`,
  instance ID at `+0x20`, `NodeRegistry_Insert` called during construction).
  Confirmed directly: `NodeBase_ConstructRoot`'s own body installs
  `*param_1 = &PTR_FUN_0019a328` — the identical vtable address. Its destructor,
  **`NodeBase_ScalarDeletingDestructor`** (was `FUN_000aab90`, adjacent in address
  space to `NodeBase_ConstructRoot`, the classic construct/destruct pair pattern
  seen throughout this project), self-resets to its own vtable and does the same
  inlined `NodeRegistry_Remove`-equivalent unlink before the standard
  pool-or-heap free tail. It's the true bottom of the chain — no further base call.

**Confirmed hierarchy**: `NodeBase` (`0x0019a328`, the level-script node root) →
`Node` (`0x00187c20`, an intermediate abstraction layer, purpose beyond "shares a
few no-op stubs and one stripped Update slot" not fully pinned down) → `Camera`
(`0x00189c88`) and presumably every other level-script node type (`TrickTrigger`,
`Boost`, etc. — not individually re-verified this round, but the construction path
via `NodeBase_ConstructRoot`/`NodeRegistry_Insert` is shared by all 24 opcode
types per `RE_NOTES_level_script_system.md`).

**Honest scope note**: this confirms Camera's full inheritance chain and ties it
into the already-well-understood `NodeRegistry` system, but does **not** reveal a
new "per-frame Update iterator" — `NodeRegistry`'s own bucket-walker
(`GameState_ResetAndRebuildTransientNodes`) was already established in an earlier
session as a race-restart/checkpoint routine, not a per-frame tick. The "who calls
Update generically every frame" question remains open. `NodeBase_ConstructRoot`
also installs a *second* vtable (`0x0019a34c`) immediately after
`NodeRegistry_Insert` — checked and found to have only 2 xrefs (far narrower reuse
than `NodeBase`'s 16 or `Node`'s 16), so likely a minor/default-case intermediate
class, not chased further given the low apparent payoff.

## Follow-up: proved the mechanism, then exhaustively ruled out `NodeRegistry` as the per-frame driver

Went looking for the actual generic-dispatch call site. `Rider_UpdateSubsystems`
sits at vtable slot 7 (offset `+0x1c`) in `Rider`'s own vtable — the exact same
slot index as `Node_UpdateStub` (Node's own, stripped, slot 7). That's a strong
signal offset `+0x1c` really is the shared "Update" slot across the whole family.

Patched GhidraMCP again (round 6) to add `GET /get_function_containing`
(`FunctionManager.getFunctionContaining`, vs. the existing
`/get_function_by_address`'s exact-entry-only lookup) — needed because a
`/search_bytes` sweep for the x86 `CALL [reg+0x1c]` opcode (`ff501c`/`ff511c`/
`ff521c`/`ff531c`/`ff561c`/`ff571c`) turned up 50+ hits, all at mid-function
addresses the old endpoint couldn't resolve. (Aside: the local Ghidra SDK used to
compile the plugin lives at the **project root**
`ghidra_11.4.1_PUBLIC/`, not inside `GhidraMCP/` — tripped on this once. See
`reference_ghidra_mcp_connection.md` "Round 6".)

Resolving all 50 hits to their containing functions and checking each:

- **Proved the mechanism directly.** `NodeRegistry_UpdateAllOfType` (already named,
  found in an earlier session) decompiles to exactly the hypothesized shape: walk a
  type's bucket via `NodeRegistry_PeekHead`/`NodeRegistry_GetNext`, then
  `(**(code**)(*node + 0x1c))(param_1)` on each one — a literal, confirmed,
  type-erased call through offset `+0x1c`. This is no longer a hypothesis; it's
  directly observed working code.
- **Then exhaustively ruled it out as the per-frame source.** Checked every caller
  of `NodeRegistry_PeekHead` (the only way to walk a bucket) — exactly 6 functions,
  **every one of them already documented from earlier sessions, and none of them
  a per-frame tick**: `ScriptObject_DestroyByID` (one-off destroy),
  `GameState_ResetAndRebuildTransientNodes` and `GameState_ResetTransientTriggerNodes`
  (checkpoint/race-restart), `NodeRegistry_UpdateAllOfType` itself,
  `ReplayManager_CheckNearbyTimerNodes` (replay-specific), and
  `Timer_RebuildPlayerRegistry` (lifecycle boundary). This is a complete, exhaustive
  set — not a sample.
- Checked the two other functions with multiple `+0x1c` hits in this sweep
  (`FUN_000d5980`, `FUN_000e2790`) — both are unrelated frontend/UI menu-flow state
  machines calling into a UI-widget vtable that happens to share the same numeric
  offset. False positives, not connected to `Node`.

**Conclusion, well-evidenced and worth taking as settled**: `NodeRegistry`'s
type-bucketed hash table is used *only* for one-off lifecycle events (spawn,
destroy, checkpoint, replay bookkeeping) — never for continuous per-frame ticking.
Whatever *does* drive `Rider_UpdateSubsystems`/`Component_UpdateAll`/etc. every
frame, if it's genuinely generic, must walk a **different structure** than
`NodeRegistry`'s buckets — most likely a separate flat list or array not yet
identified, since the intrusive `+4`/`+8` prev/next fields `NodeRegistry` uses are
themselves general-purpose and could in principle back a second, unrelated list
using the same fields with a different head pointer. This narrows the search
meaningfully for whoever picks it up next: don't re-check `NodeRegistry`'s own
mechanism again, it's been checked exhaustively and cleared. The productive next
step is hunting for a *different* master list/array construct (or accepting this
needs dynamic analysis to resolve conclusively).

**FOUND (2026-07-20)**, from the `RE_NOTES_rider_update_chain.md` side of
this same investigation: it's not a single master list at all -- raw
disassembly (tracing the true `this` the decompiler hid behind an
`unaff_ESI` artifact) showed `Component_UpdateAll` is called 3 times per
Rider, on 3 *fixed, inline-embedded* component slots
(`rider_this+0x28`/`+0x80`/`+0xd8`), each the sentinel of its own intrusive
circular list -- confirming the "different structure than NodeRegistry's
buckets" prediction exactly right, down to using different field offsets
(next at `+0x24` relative to the node, not `NodeRegistry`'s `+4`/`+8`). "Whose
list is it" -- the question this section left open -- now has a real answer.

**Update (2026-07-20, same day)**: the insertion side was also found and
fully reconciled with the traversal side above. Each 0x58-byte component
slot actually embeds **two separate** intrusive doubly-linked lists, not
one: `+0x50` (self-pointing sentinel) is the list `Component_UpdateAll`
walks every frame; `+0x28` (dummy sentinel at `+0x2c`) is a second,
independent list -- a BdrSeq animation-event queue -- populated by
`Component_InsertNode`/`New_BdrSeq_2` from animation-trigger code. Proven by
`Component_InitEmptyList` initializing both fields off the identical `this`
in one call, which only makes sense if they're distinct fields, not the
same field at two relative bases. See `RE_NOTES_rider_update_chain.md`'s
"`+0x28` vs `+0x50` -- RESOLVED" section for the full trace.

### Checked the remaining raw `+0x1c` call sites too — all resolve to an unrelated system

Went further than the `NodeRegistry`-caller check: manually inspected every other
`CALL [reg+0x1c]` hit from the original 50+ byte-pattern sweep that wasn't already
explained. Consistent result across all of them: **`FUN_000d5980`, `FUN_000e2790`,
`FUN_00085510`, `FUN_000a3f90`, `FUN_000ae150`, `FUN_000aef50`** are all frontend
menu/UI code — state-machine screen transitions, icon/sprite drawing, or (via their
own caller chains, address ranges `0x0008xxxx`-`0x0009xxxx` matching
`RE_NOTES_frontend_menu_map.md`) the already-documented **`Widget`** UI class
hierarchy. `Widget` apparently places its own "Update"-equivalent virtual method at
the *same* slot index (offset `+0x1c`) as `Node` does — a shared engine-wide
convention/coincidence across (at least) two separate, parallel class hierarchies,
not evidence of a connection between them. None of these six point back to
`Node`/`NodeBase`/`Camera`/`Rider` in any way.

**Final, confident conclusion for this thread**: every `+0x1c` call site
findable via static byte-pattern search in this binary is now accounted for --
either the one confirmed `NodeRegistry_UpdateAllOfType` path (proven, but not
per-frame) or the unrelated `Widget` UI hierarchy. The `Widget` class here is the
same one already documented in `RE_NOTES_frontend_menu_map.md`
(`Widget_BaseDestructor`/`Widget_ScalarDeletingDestructor`/`Widget_UnlinkFromList`/
etc.) -- a separate base-class family for the 2D menu/UI tree (doubly-linked
child-list container), entirely distinct from `Node`/`NodeBase`'s game-object
hierarchy, that happens to independently place its own per-frame "Update"-style
method at the identical slot index. No generic "walk every live `Node` and call
Update" site exists anywhere a static byte/xref search can reach.
This is a genuinely exhausted lead for this toolset -- resolving it further would
need dynamic analysis (a live debugger breakpointing the retail EXE, or a runtime
trace) rather than more static searching. Recommend not re-opening this specific
question with more static techniques; it's been attacked from three independent
angles (xref tracing of named Update methods, `NodeRegistry` caller exhaustion, and
raw opcode pattern search) with consistent results each time.

10 renames this thread (`Node_ScalarDeletingDestructor`, `Node_NoOpStub1`-`4`,
`Node_CheckField0x14`, `Node_UpdateStub`, `Node_NullSubObject_Destructor`,
`CameraAudioPanningMode_ScalarDeletingDestructor`, plus the earlier
`CameraShakeMode_ScalarDeletingDestructor`/`Camera_EvaluateShakeCurves`/
`Camera_FindShakeCurveSegmentCached`/`Camera_ApplyShakeOffset`/
`Math_BuildMatrixFromEulerAndPosition`/`Pool_FreeSlot`/`Heap_Free` cluster —
see `RE_NOTES_camera_system.md`). All verified live via `/get_symbol_status`.

**Cross-reference (later session):** `NodeBase`'s instance-ID field
(`NodeBase_ConstructRoot`'s `param_1[8]`, offset `0x20`, assigned from one of
two global monotonic counters `DAT_001e3c80`/`DAT_001e3c84`) turned out to be
exactly what the game's `SaveGame`/`PlayerSnapShot` serialization system
reads to make live object references save-file-portable — see
`RE_NOTES_player_snapshot_system.md` "Identified the object type in state
5's list."

**Cross-reference (2026-07-20, "full throttle" pass): partial answer to "what
drives NodeRegistry's generic Update dispatch every frame."** Found two real,
previously-unknown `NodeBase`-derived singletons — `"PREAI"`/`"PostAI"`,
registered as `NodeRegistry` types 5/0xb — whose own `Update` methods (found in
completely un-analyzed code, no `Function` object existed) turned out to drive
the entire per-rider trick-animation-event lifecycle (activate newly-triggered
BdrSeq events, tick every active one, fire audio cues at each phase). This
answers "what is type 5/0xb's real content" concretely. **Still open**: what
generically calls these two singletons' own `Update` each frame.
`NodeRegistry_UpdateAllOfType`'s only 2 static callers are for types 3/4, not
5/0xb, and live inside `GameState_ResetTransientTriggerNodes` — which itself has
zero static callers (likely a computed call, consistent with the "genuine
per-frame drivers evade static xref tracing" pattern this exact question has
hit before). See `RE_NOTES_rider_update_chain.md`'s "Found the real BdrSeq
animation-event queue consumer" for the full trace.

**Half-resolved (same session, continued, "keep going")**:
`GameState_ResetTransientTriggerNodes` has a SECOND dispatch mechanism besides
`NodeRegistry_UpdateAllOfType` — a raw bucket-walk loop over `DAT_0019b994`,
which is a flat array of 4 literal type IDs (`{2, 5, 7, 8}`), calling
vtable`+0x1c` on every node of each. Type 5 is `PREAI` — so it *does* have a
confirmed call site after all, just not through the generic dispatcher. Type
`0xb` (`PostAI`) is still not in any confirmed dispatch list anywhere found so
far. See `RE_NOTES_rider_update_chain.md`'s "PREAI's own call site WAS found"
update.

## RESOLVED (2026-07-22, Tier-1 push): the generic per-frame Update dispatcher, found

Per an explicit user directive to deep-research and resolve the remaining
Tier-1 blockers, this file's longest-standing open question — "what
generically calls registered nodes' Update each frame" — is now **fully
answered, statically**. The root cause of every prior failed search: the
project had proven the mechanism `(**(*node+0x1c))()` (vtable slot 7, the
`NodeRegistry_UpdateAllOfType` lifecycle path) and kept hunting for
per-frame callers of *that slot* — but the real per-frame tick goes through
**vtable slot 1 (`+0x4`)** via a completely different, then-unnamed
dispatcher. Found not by another byte search but by finally reading
`InGameState_TickFrame`'s unexplored tail call `FUN_000aa830(9)`.

The full machinery (all named this pass):

- **`NodeRegistry_TickAllOfType`** (was `FUN_000aa830`) — walks the
  registry's per-type active list (head at `registrySub+8+type*0x34`) and
  calls **slot 1** on every node, saving the next pointer first so nodes
  can self-unregister mid-tick. This is the slot
  `LessonMan_TickStepStateMachine`, `SnowFallMan`'s tick, and `PREAI`/
  `PostAI`'s real per-frame drivers occupy.
- **`NodeRegistry_RenderAllOfType`** (was `FUN_000aa880`) — identical walk,
  calls **slot 2** (`+0x8`, `LessonMan_RenderOverlay`'s slot).
- **`NodeRegistry_IntegratePendingNodesOfType`** (was `FUN_000aaa80`) —
  drains the per-type *pending* list (`+0x20+type*0x34`) and sorted-inserts
  each newly-registered node into the active list using slot 3 (`+0xc`) as
  an insert-position comparator and slot 4 (`+0x10`) as a replace test
  (destroying the displaced node on replace). Explains how
  `NodeRegistry_Insert`-ed nodes defer into the live tick order.
- **`InGameState_TickSubsystemsByTypeOrder`** (was misnamed
  `InGameState_ApplyHudElementVisibility` — corrected per the
  fix-on-discovery rule; the old name came from misreading the dispatcher
  as a "set HUD element state" call) — the master loop: unblocked frames
  tick the 11-entry type-order table `g_SubsystemTickOrder_Main`
  (`0,1,2,3,5,6,7,8,4,0xa,0xb`), then integrate pending type-3 nodes, then
  tick type `0xc`; overlay-blocked frames tick only
  `g_SubsystemTickOrder_OverlayActive` (`8,0xa,0xc`). Type 9 is ticked
  separately by `InGameState_TickFrame` under its own gating.
- **`InGameState_RenderSubsystemsPerViewport`** (was `FUN_000ddf30`) — the
  render-side master loop: per active viewport (split-screen aware), walks
  the 13-entry `g_SubsystemRenderOrder` table
  (`0,1,2,3,5,7,8,4,9,0xa,0xb,6,0xc` — LessonMan's type 6 renders
  second-to-last, consistent with a UI overlay).

This resolves, in one stroke: who ticks `LessonMan` (type 6), `SnowFallMan`,
`PREAI`/`PostAI` (types 5/`0xb` — the "type 0xb is still not in any
confirmed dispatch list" note above is now closed, it's in the main tick
table), and every other "orphaned Update method" flagged across this
project. The `+0x1c` slot-7 path (`NodeRegistry_UpdateAllOfType`) remains
what it was proven to be: a one-off lifecycle mechanism, not the per-frame
driver. 5 function renames + 3 data renames (the type-order tables).
