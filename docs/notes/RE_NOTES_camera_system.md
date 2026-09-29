# RE notes: the Camera class

Picked up the camera system, previously touched only in passing (`Camera_AddShake`,
`Camera_WarpToTarget`, `New_Camera`/`Camera_ConstructBase` — all from earlier
sessions, no vtable read). Read the real polymorphic vtable this round and found a
clean, self-contained class plus a nice architectural connection to the already-
documented level-script and crowd systems.

## Cameras are level-script objects, not a separate subsystem

`New_Camera` is called from **`ScriptVM_DispatchOpcode`'s opcode `0x01`**, already
documented as "spawns a tagged `Camera` object — cutscene camera." This means the
camera class is a first-class member of the same level-script object family as
`TrickTrigger`, `Boost`, `Roller`, etc. (`RE_NOTES_level_script_system.md`) — not a
separate top-level system.

## The vtable (`0x00189c88`), 16 slots

Several slots reuse already-known generic stubs shared across many unrelated
classes in this codebase (`InputDevice_StubReturnFalse`, `RaceState_NullHandler`, a
couple of trivial single-instruction stubs, `Rider_CompareThresholdGE`) — normal for
this engine's style, not camera-specific. The camera-specific slots:

- **`Camera_ScalarDeletingDestructor`** (slot 0) — standard destructor pair.
- **`Camera_UpdateBehaviorTimer`** (slot 1) — advances a running timer clamped
  between 3 threshold fields, then tail-dispatches to vtable slot 11
  (`Camera_DelegateUpdate`) on the same object. Reads as "advance the behavior
  clock, then run whatever behavior is currently active."
- **`Camera_DelegateUpdate`** (slot 11) — a one-line forwarder: calls the function
  pointer at `*(this+0x34)+4` with `this+0x2c` (the behavior timer) as its sole
  argument. See "The `this+0x34` curve-shake system" below — fully resolved this round.
- **`Camera_UpdateAudioPanning`** (slot 13) — throttled to run once every 5 calls.
  Iterates every rider (the same `+0x88`/`+0xc4` fields used throughout this
  project, e.g. `Race_ComputeRankings`), computes 3D distance from each valid rider
  to the camera's position, and for the nearest one within a threshold computes an
  8-way (0-7) audio pan-direction value. **The camera-relative positional-audio
  panning system.**
- **`Camera_UpdateViewTransform`** (slot 14) — the real per-frame view-matrix work:
  builds position + target vectors, checks visibility/frustum status via the
  graphics device vtable, and if not already satisfied, issues a D3D8-style
  "set view transform" call. **This is the actual camera-to-screen matrix update.**

## Bonus find: `Crowd_RandomizeAnimationVariants`, called from the audio-panning slot

`Camera_UpdateAudioPanning` calls **`Crowd_RandomizeAnimationVariants`** (was
`FUN_0004ed50`) — a genuinely distinct system that ties directly back to the
already-documented **`CrowdBox`** level-script opcode (`0x11`,
`RE_NOTES_level_script_system.md`). It maintains 16 per-slot animation timers, each
incrementing and, on a 16-frame boundary, rolling a random 0-3 pose/frame variant
(plus an occasional reversed-playback flag) — giving spectator crowd members varied,
non-synchronized idle motion instead of uniform lockstep animation. The fact it's
called from the camera's audio-panning update (rather than ticking independently)
suggests crowd animation is throttled/driven by camera relevance rather than running
on its own schedule — a sensible LOD-style optimization for background crowd detail.

## The `this+0x34` curve-shake system, resolved

Went back to the open thread flagged at the end of the last round: what does
`Camera_DelegateUpdate` (slot 11) forward to via `this+0x34`? Initial guess was a
polymorphic sub-object with its own vtable — correct in spirit, refined below after
disassembling the piece that first looked like inert data.

**`this+0x34` is a tiny, stateless polymorphic "shake mode" object.**
`Camera_DelegateUpdate`'s body is
`(**(code**)(*(int*)(this+0x34) + 4))(*(undefined4*)(this+0x2c))`: dereference
`this+0x34` to get a vtable pointer, then call slot 1 of that vtable with `this+0x2c`
(the behavior timer `Camera_UpdateBehaviorTimer` advances every frame) as the sole
argument. `Camera_ConstructBase` writes `this+0x34` twice in a row: first
`0x00189280` (mode A), then immediately `0x00189288` (mode B) — B is what's live
post-construction. **The object pointer and the vtable pointer are the same
address** — a classic zero-instance-data "strategy object" idiom, no separate
allocation needed.

Both are real 2-slot vtables, confirmed by disassembling slot 0 of each (initially
an unresolved `LAB_`, not yet a Ghidra function):

- Slot 0 (both modes, shared): **`CameraShakeMode_ScalarDeletingDestructor`** (was
  `FUN_0004ae70`, never auto-analyzed) — the standard 2-param thiscall
  scalar-deleting-destructor shape, pool-bounds-checked against
  `DAT_001fad60`/`DAT_001fad64`, the same pool `TrickTrigger_Destruct` uses.
- Slot 1, mode A (`0x00189280`): **`Camera_EvaluateShakeCurves`** directly.
- Slot 1, mode B (`0x00189288`): `0x000e0730`, a plain unconditional-`jmp` thunk
  (`E9 5B FE FF FF`, no `this`-adjustment) to the *exact same*
  `Camera_EvaluateShakeCurves`. Both modes resolve identically at runtime.

Why two modes that behave identically isn't resolved — plausibly other classes reuse
this same mode-object convention with genuinely different slot-1 implementations,
and Camera's A/B split is either a template/generic-usage artifact or a hook point
for a mode that was never differentiated for this particular class. Not chased
further; the mechanism and its live behavior are both now fully nailed down, and the
remaining "why" is low payoff relative to effort.

**`Camera_EvaluateShakeCurves`** (`0x000e0590`) — the real payload. Reads an
active-curve bitmask from `*(this+0x4c)+0x18`; for each of up to 16 active bits,
evaluates a cubic polynomial `((t·a+b)·t+c)·t+d` over a piecewise curve segment
(found via **`Camera_FindShakeCurveSegmentCached`**, a classic temporal-coherency
cached lookup — try the segment after last frame's first, fall back to linear scan
only on a timer reset/jump), using the behavior timer as `t`. Writes up to 16 float
results to `this+0x50`.

**`Camera_ApplyShakeOffset`** (`0x000e06a0`, a third, separate caller of
`Camera_EvaluateShakeCurves` found via xrefs — not reached through the `this+0x34`
record at all) evaluates the curves, then takes outputs `[0..2]` as a raw position
offset and outputs `[3..5]` (negated, scaled by a `_DAT_0019d050/_DAT_00187514`
ratio constant — likely a degrees↔radians conversion) as a rotation-angle offset,
and calls **`Math_BuildMatrixFromEulerAndPosition`** (a generic, widely-shared
Euler+position → 4x4 matrix builder, 8 unrelated call sites elsewhere in the
binary — not camera-specific) to compose a single offset transform.

**Conclusion**: this is a data-driven camera-shake / animated-offset generator —
a bank of up to 16 keyframed curves, driven by the camera's own behavior timer,
producing a position+rotation offset transform every frame. It sits at a different
layer than the already-documented `Camera_AddShake` (ScriptVM opcode 0x10, which
accumulates a scalar "shake amount" into a global active-camera struct) — plausibly
the low-level curve-evaluation engine that a shake *amount* like `Camera_AddShake`'s
would modulate the intensity/duration of, though no direct call link between the two
was found. `Camera_ApplyShakeOffset` itself has no traceable caller — same
vtable/dynamic-dispatch-invisible-to-static-analysis pattern as everything else in
this file.

## The `.cml` camera-script format, characterized (not fully decoded)

Previously flagged as a dead end: found the `.cml` filename is built at runtime
via a `"%s.cml"` format string with zero traceable xrefs, not pursued further.
Picked back up in a later session (after the `.big`/RefPack archive work made
this kind of format-RE feel tractable) by just opening a `.cml` file directly --
turns out **`.cml` is not compressed and not the `c0fb` container** (a
completely separate, unrelated format from `.big`), so the RefPack tooling
doesn't apply here, but plain byte inspection got a long way.

**Confirmed structure, `garibald.cml` (134,688 bytes) as the worked example:**

A small binary header (still not fully decoded -- version/count-like fields),
followed by a tree of ~500 named records, each one of two shapes:

- **Container** (`edc0adde` × 3, i.e. the literal placeholder value `0xDEADC0ED`
  repeated 3 times, right before a null-terminated name): a "branch" node whose
  children are linked via runtime pointers the *game's loader* fills in --
  `0xDEADC0ED` is a classic "not yet initialized" poison value, present in the
  static file, overwritten at load time. Not statically walkable by itself.
  **[RETRACTED -- see "`.cml` CONTAINER -- SOLVED" at the end of this file.
  The children are linked by ordinary file-relative offsets and the container
  IS statically walkable; `0xDEADC0ED` marks unused slots, not links.]**
- **Leaf/reference** (`[u32 size][u32 size-repeated][u32 offset][u32 hash]`
  right before the name -- the repeated size field is the distinguishing
  signature): the `offset` field is a **real, static, directly-usable file
  offset** -- confirmed by direct example (`Gate_Master`'s offset points
  exactly to where its child `Master_1` begins; `LoadBlocks`'s offset points
  exactly to `lb_1`). This is the one part of the format that *is* statically
  walkable without a runtime loader.

**A clean, repeating template emerges.** Every named "region" under the track
root (`Garibaldi` → `Gar_DemoRegio`/`Gari_GATE_CAM`/`Gari_STG_CAM`/
`GariFlyThru`/`Master Scripts`, all sharing one container type-tag) contains the
*same* fixed set of 6 container sub-categories, verified appearing twice
verbatim in file order: **`Animation Script`, `Camera Regions`, `Cameras`,
`Director Script`, `Paths`, `Scenes`**. Under `Paths`/`Scenes`, leaf records
follow a numbered naming convention (`Location01`/`01a`, `Location02`/`02a`,
`Moment01`/`02`/`03`, `Transition01`/`02`, `Staging01`) at a near-constant
~400-byte stride between consecutive entries' `size` field -- consistent with
each being one fixed-size camera-cut/keyframe descriptor record.

**Checked what's actually at a `Location`/`Moment` leaf's own offset target: not
dense literal float data.** Expected raw transform floats (position/rotation);
found instead a sparse structure mostly `0x00000000` and `0xDEADC0ED`
placeholders, with only a handful of populated fields. Reads as a **runtime
object template** -- the compiled game constructs the real, fully-linked
in-memory object from a small set of authored values at load time, and the
placeholder-heavy static layout reflects that (pointer/vtable-shaped slots
that don't exist yet on disk), not a simple packed array of numbers a static
tool can read directly.

**Cross-checked against `alaska.cml` -- the template holds exactly.** Same root
container type-tag (`0x7d7160ed`, byte-identical to `garibald.cml`'s), and
critically, **`FreeLoaded`'s and `Gate_Master`'s leaf hashes are byte-identical
across both files** (`0x9b0ced0a`/`0xa6fb36bb` in both) -- confirming these
hashes are computed from the generic category/field *name* itself (a shared,
track-independent convention, e.g. a `FreeLoaded` game-state category every
track defines the same way), not from anything track-specific. Alaska has one
extra top-level region Garibaldi doesn't (`LarryCams`, likely a named
special/bonus camera rig) -- nice confirmation the region *set* is flexible
per-track while the leaf/hash schema underneath stays fixed. **Wrote
`classify_cml.py`** (project root) to do this scan/classification generically --
run it against any `.cml` file to get the same container/leaf breakdown.

## Found the actual consuming code: the `VenueStaging` system

Pushed on the "find the loader function" thread rather than leaving it as a
flagged-but-unattempted next step. Instead of re-chasing the `"%s.cml"` format
string (zero traceable xrefs, a dead end already tried twice), searched
`default.xbe` for the literal category-name strings found inside `.cml` leaf
records (`"FreeLoaded"`, `"Director Script"`, `"Gate_Master"`) directly. Got one
real hit: **`"FreeLoaded"` exists as a compiled string in `default.xbe`**, part
of a string table alongside `ShowLoaded`/`RaceLoaded`/`STG_VEN_1`-`4`/
`STG_COM_1`-`3`/`PIPE_VEN`/`UNTR_VEN` -- venue/staging category names matching
`.cml` leaf-record names exactly, with genuine code cross-references (unlike
the filename format string).

Traced the chain fully:

- **`VenueStateTable_ResolveNameByID`** (was `FUN_0006eab0`) -- indexes a
  100-byte-stride table using a state-object's own ID field, returning the
  matching category name string. The literal `(&PTR_s_RaceLoaded_...)[id]`
  lookup Ghidra had already partially auto-labeled.
- **`VenueStaging_TransitionToNamedState`** (was `FUN_0002dce0`) -- the
  orchestrator: resolve the new state's name, exit the old state, enter the
  new one.
- **`VenueStaging_ExitState`**/**`VenueStaging_EnterNamedState`** (were
  `FUN_0007a0b0`/`FUN_0007a030`, already referenced by
  `Level_PreloadAndCheckCountdown` as "'PreLoad'-tagged setup" in an earlier
  session, never previously named) -- exit/enter halves of the state
  transition. `VenueStaging_ExitState` contains the literal string **`"chase
  near"`** -- the exact camera-mode name flagged in this project's *earliest*
  bookmark note (`"'chase near/board/eyes/reverse'/'replay cam' camera
  modes"`) -- confirming this is genuinely the camera/venue staging system,
  not a coincidental name collision several sessions removed from that note.
- **`VenueStaging_TickPhaseSequence`**/**`VenueStaging_TickIndexedPhase`**
  (were `FUN_0002eec0`/`FUN_0002f440`) -- two sibling ~20-state sequential
  state machines (one fixed 3-phase, one generically index-parameterized) that
  gate on `GameMode_Current` and call the transition orchestrator at each
  phase boundary. These are the actual level/venue-loading drivers, directly
  connected to the already-documented `InGameState_LoadLevel`/
  `Level_PreloadAndCheckCountdown` pipeline (same "PreLoad"-tagged calls).

**This is the closest thing to a real `.cml`-consuming code path found so
far** -- not a byte-level parser for individual `Location`/`Moment` keyframe
records (that thread is still open), but the actual state machine that walks
through named venue-staging categories (`FreeLoaded`, `RaceLoaded`,
`STG_VEN_1`-`4`, etc.) by name during level load, exactly matching the
category names found as `.cml` leaf-record names. 6 renames, all verified live.

## Update: cracked the keyframe field layout via cross-record statistical analysis

Followed through on the flagged next step. Took 6 consecutive `Location`
leaf records (`Loacation01`, `Location01a`, `Location02`, `Location02a`,
`Location03`, `Location03a`) from `garibald.cml`, dumped their full 404-byte
payload side by side, and found only **15 of ~100 dwords actually differ**
across all 6 -- everything else is identical structural padding/placeholder,
confirming the earlier "sparse runtime-object template" read was right, but
the real per-record data is a small, findable subset rather than nothing.

Decoding the 15 varying dwords as floats revealed a clean, physically
plausible pattern:

- **Offset `+0x3c`**: large positive floats, ~60,000-235,000 range.
- **Offset `+0x68`**: large positive floats, ~144,000-235,000 range.
- **Offset `+0x94`**: large *negative* floats, ~-137,000 to -236,000 range.

Three large-magnitude floats, one negated relative to the other two -- the
classic shape of a **3D world-space position vector** (X/Y/Z), consistent
with each `Location` record marking a distinct camera waypoint.

- **Offsets `+0x134`/`+0x138`/`+0x140`**: small floats, mostly in the
  `[-2, +2]` range (one outlier at -17.5) -- consistent with **rotation**
  components (Euler angles in radians, or part of an orientation
  representation), a natural pairing with the position fields above.

The remaining ~6 varying dwords at higher offsets (`+0x174` onward) turned
out to be a red herring -- they're **spillover from the following record's
own leaf-header tuple** (`{size,size,offset,hash}` + name), meaning the true
per-record data region is everything below roughly `+0x170`, not the full
404-byte stride originally assumed.

**Tried to cross-validate the position magnitude against `gari.map`** (hoping
to match a known landmark's coordinates) but `gari.map` turned out to be a
plain-text ColdFusion level-editor export (model name / UID / hash-value
columns), not raw binary transform data -- no numeric cross-check available
from that file. The finding rests on its own strong internal evidence
(consistent magnitude, sign pattern, and position/rotation pairing across 6
independent records) rather than an external confirmation.

**Result**: the `.cml` keyframe field layout is now understood at a
structural level -- position (3 floats) + rotation (3 floats) per
`Location`-class record, embedded within an otherwise-placeholder runtime
object template -- closing most of what was flagged as the format's last
open question. Exact semantic units (world-space scale, radians vs. another
angle representation) and the equivalent fields for `Moment`/`Transition`/
`Staging` record types (not individually re-verified, though the same
statistical technique would apply directly) remain for a future pass if
byte-exact precision is ever needed.

**Honest scope of this pass**: this is a large upgrade from "totally opaque,
zero xrefs, abandoned" to a well-characterized hierarchical scene-graph format
(container/leaf record shapes, a confirmed 6-category per-region template,
naming conventions matching the project's existing "camera modes" bookmark
note from early sessions). It does **not** reach exact keyframe-field semantics
(position/rotation/timing byte offsets within a `Location` record) -- that
would need either finding the actual in-`default.xbe` loader function for this
format (not yet located; the `"%s.cml"` filename builder has zero traceable
callers, same dead end as before) or substantially more cross-record
statistical analysis (comparing many `Location`/`Moment` records' non-zero
fields against known in-level landmark coordinates from `gari.map`, a
plausible but unattempted next step). Good, well-scoped candidate for a future
session with that specific framing.

## The recurring "orphaned Update method" pattern, now clearly a systemic thing

Checked `Camera_UpdateBehaviorTimer` and `Camera_UpdateAudioPanning` for callers —
both are `DATA` xrefs only (the vtable slot itself), no traceable call chain to
whatever invokes them each frame. **This is now the fourth or fifth time this exact
pattern has shown up this project** (`Component_UpdateAll`'s `this`,
`Rider_UpdateSubsystems`, `ReplayManager_UpdateSequenceState`, now `Camera`'s own
update slots). The likely unifying explanation: this engine's per-frame object
update loop iterates a list of **base-class `Node*` pointers** and calls a fixed
vtable offset generically — the loop itself never references `Camera`, `Rider`, or
any other concrete derived class by name, so there's no call chain for static
analysis to follow back. It's not that these functions are unreachable or dead code
(they're clearly used — they read and write live per-frame state) — it's that the
*caller* only knows about the abstract base type, and Ghidra's cross-reference
analysis can't see through that kind of dynamic dispatch without runtime
information. This isn't a new mystery to chase per-class; it's the same explanation
recurring, and probably won't resolve without dynamic analysis regardless of which
class's Update method is being searched for next.

## Update (2026-07-20): found the `.cml` runtime execution engine, closing the last gap

The earlier "found the actual consuming code" pass explicitly flagged what was
still missing: *"not a byte-level parser for individual `Location`/`Moment`
keyframe records ... that thread is still open"*. Found while tracing forward
from an unrelated discovery this session (`GfxContext_InitCameraModeRecords`,
which turned out to initialize the exact `"chase near"`/`"chase board"`/
`"chase eyes"`/`"chase reverse"`/`"replay cam"` camera-mode name records
already flagged in `VenueStaging_ExitState`):

- **`VenueStaging_SetActiveCameraMode`** (was `FUN_00079380`) — the real
  "activate this camera mode by name" function: copies the given name string
  directly into `this+0x2c`, the *exact* field `VenueStaging_ExitState`
  string-compares against `"chase near"`. Confirms the camera-mode tag
  strings are read/written by `VenueStaging`, not `GfxContext` — the
  `GfxContext_InitCameraModeRecords` function is a separate, GfxContext-owned
  preset-storage init, not the consumer.
- **`VenueStaging_Tick`** (was `FUN_0007adf0`) — the master per-frame update
  for the whole system: walks a state list checking transition conditions
  (calling the already-named `VenueStaging_EnterNamedState` on match), then
  falls through to a camera-mode-ID-to-name dispatcher (switch on
  `this+0x428`, resolved through an alias table at `DAT_001b8928` for the
  common cases, hardcoded `"chase far"`/`"replay cam"` for others, and a
  dedicated `"manual target"` mode with explicit camera position/target
  setup) that calls `VenueStaging_SetActiveCameraMode`.
- **`VenueStaging_TickCameraScriptCommand`** (was `FUN_0007a780`) — **the
  missing byte-level `.cml` runtime interpreter itself.** A 43-opcode
  (`0x00`-`0x2b`) command processor walking `0x2c`-byte keyframe/command
  records, advancing playback by elapsed time (`this+0x48`) against
  per-record timing thresholds (matching the already-decoded keyframe field
  layout above). Opcode `0x25` formats `"CameraScript_%d"` and calls the
  already-named `Script_PlayByName` — directly tying `.cml` keyframe playback
  to the level-script system documented elsewhere in this project. Other
  opcodes handle camera-mode switches (opcode `0`, string-compares against
  the active mode and re-triggers it via `VenueStaging_SetActiveCameraMode`),
  blend/transition setup (opcodes 1-3, calling an unread `FUN_00079be0`),
  state-flag toggles, RNG-gated branching (opcode `0xa`), a "push saved
  state" mechanism (opcode `0xb`, up to 16 deep), and a `CameraScript_%d`
  level-script trigger (opcode `0x25`). Individual opcode semantics beyond
  what's listed here weren't all traced — the function itself and its role
  as the `.cml` execution engine is the confirmed, load-bearing finding.
- **`VenueStaging_CheckSecondaryExit`** (was `FUN_00079d20`, structural
  confidence) — checks the same `this+0x1e0` flag `VenueStaging_ExitState`
  manages and conditionally re-triggers its exit-notification sequence.

**This closes the `.cml` format investigation end to end**: on-disc container/
leaf format (characterized earlier) → keyframe field layout (position +
rotation, cracked via statistical analysis) → the actual runtime interpreter
that walks those keyframes frame by frame and drives the active camera mode
(found this pass). 4 renames.

**Follow-up, same day**: pushed one level deeper into the opcode handlers.
**`VenueStaging_BlendToCameraModeEased`** (was `FUN_00079be0`, opcodes 1-3's
handler) computes a normalized time fraction and applies one of 3 easing
curve presets (ease-in/ease-out/ease-in-out, selected via 2 control-point
constants fed into a bezier-style evaluator) before calling the underlying
blend implementation and writing the new mode name once the blend completes
-- a proper eased camera-mode transition, not an instant cut.
**`VenueStaging_SetPendingExitCameraState`** (was `FUN_00079fa0`, opcode 4's
handler) sets the same exit flag `VenueStaging_ExitState` manages and
captures a 9-field camera transform. **`VenueStaging_InterpolateCameraModes`**
(was `FUN_00079400`) is the underlying blend implementation the eased
wrapper calls -- large and not fully characterized internally, but its role
in the chain is confirmed. 3 more renames.

## Update (2026-07-21): resolves the declared "`%s.cml` filename builder has zero traceable callers" dead end

A later session picked `.cml` as what looked like fresh ground (asset-survey
technique: scan `Game Data/data/` for untouched file types) without first
reading this file's history in full -- and initially mischaracterized the
whole thread as "never previously traced." **That framing was wrong and is
corrected here**: this file already closed the `.cml` format end-to-end from
the *runtime-interpreter* side (`VenueStaging_TickCameraScriptCommand`,
above). What that later session actually found is a genuinely new,
complementary piece this file had explicitly given up on: **the raw
synchronous file-load call itself**, resolving the "zero traceable callers"
dead end declared just above.

- **`CameraScript_LoadFile`** (was `FUN_000752a0`) — builds `"<name>.cml"`
  and loads it synchronously via **`FILE_LoadRawFileSync`** (see the
  naming-mistake fix below).
- **`CameraScript_LoadTrackAndCommonFiles`** (was `FUN_0007b890`) — loads the
  current track's file (`Level_GetCurrentTrackNameTag` -> `"data/camera/
  <track>"`) plus the 2 always-loaded shared files `commonob`/`scripts`,
  matching the file listing exactly (11 per-track `.cml` files + `commonob.cml`
  + `scripts.cml` = 13 total).
- **`CameraController_Construct`** (was `FUN_000ac240`) — called directly
  from `InGameState_LoadLevel`: resolves a difficulty/mode enum, then
  allocates a 12-byte **`"CamController"`**-tagged block — the per-level
  entry point that ultimately owns the manager below.
- **`CameraScriptManager_Construct`/`_Tick`/`_Destructor`/
  `_ReleaseResources`/`_InitFlags`/`_InitFieldDefaults`** (were
  `FUN_0007bde0`/`FUN_0007bce0`/`FUN_0007bc90`/`FUN_0007b750`/`FUN_0007b460`/
  `FUN_0007b340`) — a full **NodeBase-derived class** (vtable `0x00195f78`,
  same architecture as `PowerFXParticles`/`SnowFallMan`/`LessonMan`, see
  `RE_NOTES_powerfx_particles.md`) that owns the `.cml` load lifecycle.
  **`CameraScriptManager_Tick` calls `VenueStaging_Tick` directly** (when a
  staged-sequence flag is set) — the concrete missing link between "who
  loads the `.cml` bytes" and "who drives `VenueStaging_Tick`/
  `VenueStaging_TickCameraScriptCommand` every frame," closing the loop
  between this session's interpreter find and the loader.

**A real naming mistake found and fixed along the way**: the function
`CameraScript_LoadFile` calls had been named `Font_LoadFileData` (from an
unrelated earlier `.ffn` font-loading thread), but `xrefs_to` shows 8 total
callers, only one of which is font-related — it's a **generic synchronous
raw-file loader**. Renamed to **`FILE_LoadRawFileSync`** (the direct/
uncompressed sibling of `FILE_ResolvePackEntryHandle`, which does the same
shape for packed/compressed pack-file entries). Fixed immediately per this
project's standing correction rule.

Vtable slots 2/4/6/8 of `CameraScriptManager` point at already-named shared
stubs (`Node_NoOpStub1`/`InputDevice_StubReturnFalse`/`RaceState_NullHandler`/
`Node_NoOpStub2`, each doubled across 2 slots) — the same "generic Update
slot inert, real logic in a custom slot" pattern as every other NodeBase
subsystem in this project.

10 renames (9 new + 1 correction of an existing mislabeled function,
`Font_LoadFileData` -> `FILE_LoadRawFileSync`).

**Immediate follow-up, same pass — closed 2 of the 3 items flagged above:**

- **`InGameState_IsBlockingOverlayActive`** (was `FUN_000ca120`, moderate
  confidence) — scans up to 19 slots for one with a non-null `+0x10` field
  and non-zero `+0x15` byte. Called from `CameraScriptManager_Tick` but also
  from several already-named core `InGameState`/HUD functions
  (`GameState_ShouldSkipGameplayTick`, `InGameState_ApplyHudElementVisibility`,
  `HUD_TickRiderDisplayState`, `HUD_DrawWorldSpaceMarkers`,
  `InGameState_TickFrame`) — always alongside `InGameState_IsHudOverlayActive`,
  so it reads as a general "is some blocking overlay/pause state active
  anywhere" check, not camera-specific despite gating this system too.
- **`CameraScriptManager_RegisterActiveInstance`** (was `FUN_0007b5d0`) — the
  registration side of the active-instance array `CameraScriptManager_Tick`
  iterates: a trivial array-append + count-increment.
- **`CameraScriptManager_HandleDebugCameraCommand`** (was `FUN_0007baf0`,
  moderate confidence) — a 10-opcode pan/zoom/cycle dispatcher. Opcodes 4/5
  cycle a camera index with wraparound and toggle flags on the first
  registered active instance (`DAT_001e26b0[0]+0x14/+0x15` — the exact
  fields `InGameState_IsBlockingOverlayActive` checks). Reads as a free/
  developer camera controller, plausibly the input handler behind the
  `"LarryCams"` named set seen in the `.cml` dumps — not proven by an
  explicit tag, named by strong structural inference.

**Still genuinely open**: the per-instance camera-script object's own
vtable (whatever the active-camera array's entries' vtable+4 points to —
the actual "camera sequence player" class each array slot is an instance
of) — not located this pass.

3 more renames (13 total this update).

**Process note, for future sessions**: this is exactly the "near-duplicate-
work detour" pattern this project has hit before (e.g. the `btnmap0.dat`
re-investigation) — always grep `RE_NOTES_INDEX.md` and the specific
candidate topic's own file for existing work *before* declaring something a
"fresh, untouched direction," not just before writing up the conclusion.
Caught partway through here (immediately after applying renames, before the
writeup was finalized), so no renames were wasted or duplicated -- but the
initial framing in the first draft of this update was wrong and has been
corrected in place rather than left standing.

## Tier-2 close-out (2026-07-22): the remaining `.cml` record types, decoded statistically

Per the explicit "finish Tier 2" directive, extended the cross-record
statistical-diffing technique (that solved `Location` earlier) to every
remaining record family, this time across **all 12 track `.cml` files at
once** rather than 6 records from one file. Record payload sizes were
established from consecutive-record offset deltas (the 16-byte leaf
preamble's first two dwords are a cumulative stream position, not a size —
a refinement of `classify_cml.py`'s original reading).

- **`Moment` (808 bytes) = two back-to-back `Location`-shaped 404-byte
  keyframes.** The second half repeats the exact same field offsets at
  +404: position vec3 at `+0x3c/+0x68/+0x94` and `+0x1d0/+0x1fc/+0x228`,
  rotation block at `+0x134..0x140` and `+0x2c8..0x2d4`. Reads as a
  camera-pose *pair* (start/end, or position + look-at target).
- **`Transition` (808 bytes)** — the identical two-keyframe structure,
  plus a clean halving float series at `+0x180` (128.21 / 64.105 / 32.05 —
  a blend duration/rate parameter), and generally small/near-zero position
  values (consistent with relative offsets rather than world positions,
  fitting a blend between cameras).
- **Refinement to the original `Location` finding**: the rotation block is
  **4 floats** (`+0x134/+0x138/+0x13c/+0x140`), not 3 — `+0x13c` varies
  too (small values near 0); the 4th component (`+0x140`, ~0.9–2.0 range)
  is plausibly FOV/zoom rather than a rotation axis. The earlier 3-float
  claim was an under-count from the smaller sample, not an error in the
  identified offsets.
- **Staging/category records (112 bytes, `STG_*`)** — directory/linkage
  nodes, not keyframes: an ordinal at `+0x00` (consecutive 6,7,8,9,10
  across siblings), runtime-pointer placeholder triples at `+0x50..0x58`
  (the same prev/next/parent linkage shape the leaf preambles use), a
  hash-like id at `+0x5c`, and an embedded name fragment. Matches their
  known role as the named category markers `VenueStaging_EnterNamedState`
  walks by name.
- **Demo/gate composite records (916 bytes, `GATE_*`/`STG_COM_*`/
  `*_Demo*` — the *majority* of all records)** — not flat keyframes but
  composites built from **44-byte named cells** (embedded ASCII names at
  0x2c stride: `S_COM_1`/`Est1`-style), with a `0xDEADC0ED`-placeholder
  header block. The 44-byte cell is the same unit visible inside
  `Location`'s own component storage (position components sit 0x2c apart)
  — the whole `.cml` format is cell-based ("sparse runtime-object
  template", as earlier hypothesized), with per-cell names/hashes. Cell
  *interior* semantics beyond the identified position/rotation cells not
  exhaustively decoded — honestly scoped as diminishing-returns detail,
  since the camera keyframe data (the port-relevant part) is covered by
  the Location/Moment/Transition layouts above.

**Still open (accepted, non-blocking)**: the in-binary `.cml` loader
function (the `"%s.cml"` builder still has zero traceable static callers —
unchanged), and per-cell semantics of the 916-byte composites. The camera
*keyframe* layouts — the part a port actually needs to replay camera
sequences — are now decoded for all record types.

## The camera-script COMMAND record, decoded from the interpreter (2026-07-28)

Earlier passes characterised the `.cml` container statistically. This one takes
the layout straight from `VenueStaging_TickCameraScriptCommand` (0x0007a780),
the runtime interpreter, so it is read from code rather than inferred:

```c
rec = base + index * 0x2c;                       // stride 0x2c = 44 bytes
if (*(float*)(rec + 0x20) < this[0x48]) { ... }  // +0x20 = TIME threshold
switch (*(u32*)(rec + 0x10)) { ... }             // +0x10 = OPCODE, 0x00..0x2b
VenueStaging_SetActiveCameraMode(rec);           // rec+0x00 = the NAME string
```
`this+0x48` is playback time, advanced by `DAT_00187558` = 1/60 per tick.

```
+0x00  char  name[16]     camera-mode / target name
+0x10  u32   opcode       0x00 .. 0x2b
+0x1c  u32   operand A    (read by opcode 0x16)
+0x20  float time         playback threshold, SECONDS
+0x28  u32   operand B    (read by opcode 0x21)
```

### Verified against the shipped files
A run in `scripts.cml` reads out cleanly:
```
op=0x0e t=0.0  name="S_COM4"
op=0x00 t=0.0  name="COM_Est3"      <- op 0 = camera-mode switch, carries a name
op=0x01 t=0.0  name="COM_Est3a"     <- op 1 = blend/transition
op=0x00 t=4.0  name="COM4_3"
op=0x0f t=7.0
```
Names are real identifiers, times are non-decreasing seconds, every opcode is
in range. Unnamed slots hold the `0xDEADC0ED` poison, consistent with this
file being a serialised runtime object graph.

### A hypothesis TESTED AND REJECTED
The `Location` entry above reports a position vec3 at `+0x3c`/`+0x68`/`+0x94`.
Those offsets are exactly `0x2c` apart -- the command stride -- which raised
the possibility that the earlier statistical pass had mistaken **one field
across three consecutive 44-byte records** for a vec3.

**It had not.** Reading a `Location` leaf's payload as 44-byte command records
produces garbage (`op=0x481d4b40`, `t=-6.3e18`), so `Location` payloads are a
*different* structure and the 0x2c spacing there is coincidence. **The original
vec3 finding stands**; this note exists so the same false lead is not chased
again.

### What is still NOT possible statically
**[RETRACTED IN FULL -- the section below was wrong. Placing the front-end
camera IS possible statically, and is now done. See "`.cml` CONTAINER --
SOLVED" at the end of this file: the loader's linking pass (FUN_00075120) walks
file-relative offsets, all 977 camera records and 259 scene placements parse,
and the FE camera is scene VEN_1 slot V_1. The "1 of 970 walkable" figure came
from scanning for name-like strings instead of following the category lists.]**

Placing the front-end camera. `scripts.cml` exposes essentially no walkable
leaf records (1 of 970 name-like strings), because container children are
linked through `0xDEADC0ED` pointers the loader fills in at runtime -- exactly
as the original container note warned. So the FE camera position cannot be
extracted from the file by static walking alone; it needs either the loader's
own linking pass or dynamic capture.

---

## `.cml` CONTAINER -- SOLVED, AND A CORRECTION TO THIS FILE

**This section supersedes every earlier claim in this file that the `.cml`
containers are not statically walkable.** Specifically it retracts:

* line ~138-140: *"children are linked via runtime pointers the game's loader
  fills in ... Not statically walkable by itself."*
* line ~573-575: *"1 of 970 name-like strings ... container children are linked
  through `0xDEADC0ED` pointers the loader fills in at runtime ... So the FE
  camera position cannot be [read statically]."*

Both are **wrong**. The links are ordinary file-relative offsets. The mistake
was reading record `+0x00` as a size field and reading the neighbouring poison
words as if they were the links.

### Where the truth came from

`CameraScript_LoadFile` (0x000752a0) -> `FUN_000753a0` -> `FUN_000752e0` ->
**`FUN_00075120`**, the container lookup. Decompiled:

```c
base = *this;                                   // raw file buffer, no relocation
node = *(u32*)(base + 4 + category*4);          // per-category list head
if (node == -1) return 0;
hash = FUN_00074fe0(name, 0);
do {
    if (*(u32*)(base + node + 0x0c) == hash &&           // +0x0c name hash
        strcmp((char*)(base + node + 0x10), name) == 0)
        return base + *(u32*)(base + node + 0x08);       // +0x08 data offset
    node = *(u32*)(base + node);                         // +0x00 NEXT
} while (node != -1);
```

Each category is a **singly linked list**; every link is file-relative and made
absolute by adding the base. `CameraScript_LoadFile` does no relocation at all,
which is exactly why this works on the raw file.

### Layout

Header: `u32` at `+0x00`, then 8 per-category list heads at `+0x04 + cat*4`.
`0xffffffff` = empty category.

Container node:

| off | meaning |
|-----|---------|
| +0x00 | next node offset, file-relative; `-1`/0 ends the list |
| +0x04 | size |
| +0x08 | this record's data offset, file-relative |
| +0x0c | name hash |
| +0x10 | NUL-terminated name |

Categories match the authoring template already documented above:
0 Animation Script, 1 Camera Regions, 2 Cameras, 3 Director Script, 4 Paths,
5 Scenes. `FUN_000753a0` passes **2**, confirming index 2 is Cameras. Index 6
exists in all 13 files holding one malformed `"Scripts"` record whose data
offset is `-1`; nothing ever requests it.

Nodes are **interleaved with the data**: each data block's tail holds the next
record's node, which is why blocks sit at a fixed stride.

### The name hash -- `FUN_00074fe0`

```c
h = 0;
for (c = *s; c && n; n--) { h = (h << 23) ^ ((int)h >> 7) ^ (int)c; c = *++s; }
```

The `>> 7` is **arithmetic** on a signed int and the char is signed. Worked by
hand: `"V_1"` -> `0x2FD60031`, matching the hash stored in `scripts.cml`.
The engine uses it only as a pre-filter and confirms with `strcmp`, so matching
on the name alone is behaviourally identical.

### Camera scripts (category 2)

Fixed capacity of **20 commands x 44 bytes = 880**, inside a 916-byte block
(the remaining 36 bytes are the next node). A command:

| off | meaning |
|-----|---------|
| +0x00 | `char[16]` name -- usually the target Animation Script / Region |
| +0x10 | opcode; **`0xDEADC0ED` here means the slot is unused and the list ends** |
| +0x20 | start time, seconds |
| +0x24 | end time == the NEXT command's start time |

So `0xDEADC0ED` was never a link to resolve -- it is the allocator poison in
unused slots, and it is the terminator.

### Scenes (category 5) -- the camera placements

6 slots x 48 bytes:

| off | meaning |
|-----|---------|
| +0x00 | `char[16]` region name, `"V_1"`..`"V_6"` (matches category 1's records) |
| +0x10 | enabled (1) |
| +0x14 | flags: 0 on slot 0, 0x0d on the rest |
| +0x20 | world position XYZ, 1 unit = 1 cm, **Z up** |
| +0x2c | heading, radians |

### Verification (all 13 shipped `.cml` files)

| check | result |
|-------|--------|
| container walk | 2608 records, no cycles, no overruns |
| **stored hash == `FUN_00074fe0`(name)** | **2608 / 2608** |
| camera records parsed | 977 / 977 (895 with commands, 82 empty stubs) |
| command times non-decreasing | 3241 commands, 0 violations |
| `endTime == next.time` | 2346 / 2346 |
| scene slots | 259 / 259, heading always within +/-pi |
| ssxfe `VEN_*` placements inside `ssxfe.xbd` object bounds | yes, at Z ~= 280 cm |

The 2608/2608 hash agreement is the decisive one: a hash reproduced from the
disassembly matches every name in every file, which independently confirms both
the hash and the walk.

### Front-end camera

`scripts.cml` is the global database (it is the only file with Camera Regions
and the 43 `VEN_*` Scenes). Scene `VEN_1` slot `V_1` =
`pos (-703.6, -104.4, 279.8) cm, heading -1.56 rad (-89.4 deg)`, and that slot
is byte-identical in `VEN_2`.

**[SUPERSEDED -- see the correction section immediately below: these are RIDER
placements, not cameras. The port does NOT use this as its camera.]**

**Still open:** pitch and FOV. Those are not in the Scene record. They are in
the 404-byte **Animation Script** records (category 0), which hold what look
like angles and damping (`225.0`, `30.0`, `1000.0`, `0.2`, `0.6`, `-0.03`) but
are not decoded yet. The port therefore renders level with a default FOV and
says so on screen.

Port: `port/src/assets/cml.{h,cpp}`; test in `port/tests/asset_test.cpp`.

### CORRECTION to the section above: Scenes place RIDERS, not cameras

Written the same session, immediately after. The container decode, the name
hash, the camera command format and all the verification counts above stand
unchanged. **What was wrong is the meaning of the Scene (category 5) slots.**
I called them "the camera placements" and used `VEN_1` slot `V_1` as the
front-end camera eye. They are rider placements on a staging set.

**Evidence** -- `FUN_0007a150`, the only consumer of the Scene object built by
`FUN_00077790` (the cat-5 constructor):

```c
i = 0;
do {
    if (*(int*)(scene + 0x10 + i) != 0) {        // slot in use
        v = *(int*)(scene + 0x14 + i);           // participant selector
        if (v == 0)        { ...user slot... }
        else if (v == 10 || v == 11 || v == 12) { ...three roles... }
        else if (0xd < v && v < 0x1a) {          // 14..25
            // scan the rider list for the one whose id == v - 0x0e
        }
    }
    i += 0x30;                                   // 48-byte slots
} while (i < 0x180);                             // exactly 6
```

and its second loop has `case 0xd:` which pops from a list of every rider *not*
explicitly named by another slot -- so **13 = "any remaining rider"**, a filler.

`+0x14` is therefore a participant selector, not a flag word. `14..25` maps to
rider index `v - 0x0e` = 0..11, exactly the project's 12-rider roster order,
which is independent confirmation.

Decoded, `scripts.cml` scene `VEN_1` is **slot0 = the user + five fillers** --
a character lineup. That also re-explains the numbers that had looked like
camera data: 6 slots because the set holds 6 characters, the near-constant
Z ~= 280 cm is the set's floor, and the heading is which way each rider faces.

This is consistent with what the rest of `scripts.cml` turned out to contain --
`STG_*` (staging), `FL_*_VS_USR` (face-offs), `WIN_1`/`LOS_1`, `6COM_*`
(six competitors). It is the character-select / staging / results database, not
the title screen's.

**Consequence for the port.** The title-screen camera is NOT recovered. The
port no longer claims it is: it uses the rider placements only to locate the
staging set and frames the view on it, labelled on screen as
`"framed on the VEN_1 rider stage (camera ours)"`. The authored camera still
needs the **Animation Script** (category 0) records decoded -- the camera
scripts' opcode-0 commands name those, and that is where a real eye/pitch/FOV
will come from.

### Camera modes (category 0) decoded -- the authored camera is recovered

The `Animation Script` category is really the **camera mode** table: 0x174
bytes of content per record (the 404-byte stride's remaining 0x20 are the next
container node). These are what a camera script's commands name.

**Code anchors** (not inferred from bytes):

* `VenueStaging_SetActiveCameraMode` (0x00079380) copies the name to
  `this+0x2c`, looks the record up via the cat-0 accessor, then reads record
  `+0x170` (gates a counter capped at 3) and `+0x168` (-> VenueStaging+0x448).
  Ghidra's decompile made the accessor and the record look like one variable;
  the **disassembly** settles it -- `PUSH EDI` shifts ESP, so the accessor is at
  `ESP_before+8` and `MOV EAX,[ESP+0xc]` reads accessor+4, i.e. the record.
* `ReplayCamera_ApplyViewParams` (0x0007b990) looks up the mode named
  `"replay cam"` and writes a distance **clamped to [200, 1200]** into the live
  camera at `+0xcc`, and a zoom clamped to [0.5, 1.9]. The file record holds a
  float at the *same* `+0xcc` ranging -350..1000. **The record and the live
  camera object share a layout** -- the record is the serialised camera state.
  (This also means `Audio_PlayScaledCue`, which scales `+0xcc`, is scaling
  camera DISTANCE, not audio. Misnamed; see the progress notes.)

**Fields** (profiled across all 1049 records in all 13 files):

| off | meaning |
|-----|---------|
| +0x3c, +0x68, +0x94 | world position XYZ, cm. **945 of 1049 records** have one |
| +0xcc | distance (live camera clamps to [200,1200]) |
| +0xd0 | 30.0 in most follow cams |
| +0x15c | 1000.0 / 300.0 |
| +0x168 | -> VenueStaging+0x448 |
| +0x170 | flag gating the capped counter |
| +0x11c, +0x164 | stale runtime pointers (0x07D9xxxx) frozen into the file |

Two kinds of record: **fixed cameras** carrying a world position (the `VEN_Est*`
establishing shots), and **follow cameras** with no position but distance/angle
params (`VEN_char1`: 225 / 30 / 1000).

**NOT decoded:** which field is pitch, yaw or FOV. An attractive "int flag then
float value" pairing was tested across every record and **cleanly rejected** --
58 records carry a value with the supposed flag clear. It is deliberately not
written down as fact.

### The front-end camera, read end to end

`scripts.cml` camera `STG_VEN_1` decodes to:

| # | op | t | name | meaning |
|---|----|---|------|---------|
| 0 | 36 | 0 | - | |
| 1 | 14 | 0 | `VEN_1` | select the rider scene |
| 2 | 0 | 0 | `VEN_Est1` | camera to the establishing position |
| 3 | 1 | 0 | `VEN_Est1a` | blend toward Est1a |
| 4 | 0 | 2.5 | `VEN_char1` | hand over to the follow-cam |
| 5-7 | 15/23/35 | 7 | - | |

So **op 14 = set scene, op 0 = set camera mode, op 1 = blend to camera mode**,
and a script is a timed sequence of them. `Est1 -> Est1a` is only a 1 cm move;
the visible motion on the original's title screen comes from the t=2.5 handover
to `VEN_char1`, a follow-cam.

**Verified:** all four ssxfe establishing shots (`VEN_Est1..4`) land inside
`ssxfe.xbd`'s own object-translation bounds -- an independent check that
`+0x3c/+0x68/+0x94` is a world position. Test in `port/tests/asset_test.cpp`.

**Ported.** `port/src/game/boot_flow.cpp` now plays `STG_VEN_1` positionally:
eye = `VEN_Est1` at (-1408, 266, 473), interpolated across each command's time
window. Only the EYE is the game's; the look-at is still ours, because the
orientation field is undecoded. Labelled on screen accordingly. Follow-cam
modes are skipped rather than faked.
