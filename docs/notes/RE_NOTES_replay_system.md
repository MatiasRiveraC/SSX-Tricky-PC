# RE notes: `ReplayManager` — the instant-replay recording/playback system

Opened 2026-07-21 after checking `RE_NOTES_INDEX.md` first (per this
project's standing "check before declaring fresh" discipline) — `ReplayManager`
had only ever been touched in passing (5 functions named: `Construct`/
`ResetState`/`UpdateRecordingState`/`UpdateSequenceState`/
`CheckNearbyTimerNodes`, discovered while chasing the trick-score-writer
mystery) despite clearly being a substantial subsystem. No dedicated deep-dive
existed, so this is genuinely new ground.

## Construction and layout

`ReplayManager_Construct` (called directly from `InGameState_LoadLevel`, a
plain struct member, not a NodeBase-polymorphic class) sets up:

- **4 recording-buffer slots**, each `0x389` dwords = **0xe24 (3620) bytes**.
  Zeroes a `0xe00`-byte sub-block and a `0x1c`-byte header per slot.
- A dynamically-sized secondary buffer, capacity computed from a caller
  parameter (`param_2`) capped at 16 entries and a duration field derived
  from `param_4` — the actual snapshot-recording ring buffer used for
  in-race instant replay.
- Two construction modes (`param_3==1` vs else) setting different initial
  state-machine values (4/3 vs 2/1).

**Checked directly against the still-open `SaveGame` "3620-byte record"
mystery** (`RE_NOTES_player_snapshot_system.md`/`RE_NOTES_DECOMP_PROGRESS.md`):
the 4-slot, 3620-byte buffer shape here (`this+0x48+i*0xe24`) is byte-for-
byte identical to `SaveOverlay`'s own record layout. Traced
`SaveGame_WriteDataRecords` -> `SaveGame_StageChunkAndAccumulateChecksum`
directly: the source pointer it checksums is `param_1+0x48+i*0xe24`, where
`param_1` is `SaveOverlay`'s **own** object — **not** a pointer into a live
`ReplayManager` instance. So this is not "`SaveOverlay` reads `ReplayManager`'s
buffer" as first hypothesized; `SaveOverlay` has its own separate, inline
3620-byte-record storage at the identical offset/stride. The record *count*
source is identical too (`DAT_001e3c7c+0x72c -> +0x1c -> +0x7c`, matching
`ReplayManager`'s own use of the same current-track-object chain elsewhere).
**Sharper, more actionable lead than before**: this strongly suggests both
objects are populated by the same underlying "build a checkpoint/snapshot
record" routine, parameterized by destination pointer — one call site writing
into `ReplayManager`'s buffer during live gameplay, another (not yet found)
writing into `SaveOverlay`'s buffer during a save operation. Finding a
function that takes a `this+0x48+i*0xe24`-shaped destination as a *parameter*
(rather than hardcoding either object) would likely be the actual shared
record-content writer this project has been looking for since the earliest
SaveGame investigation.

**Tried one concrete search angle, came up empty**: searched the whole binary
for the raw `0xe24` immediate (the record-size constant) via `/search_bytes`.
All hits outside the already-known `ReplayManager`/`SaveGame` code region
(`0x000b93xx`-`0x000bdxxx`) turned out to be coincidental — e.g.
`LoadScreen_SetupLayoutRects`/`InfoRowsScreen_SetupLayoutRects` contain
`0xe24` only as an unrelated floating-point screen-coordinate bit pattern,
not the record size. No shared parameterized writer found this way; the
search technique itself is now exhausted for this specific lead (would need
either a different search angle — e.g. tracing what else reads/writes
`ReplayManager`'s active-list nodes' actual payload fields — or dynamic
analysis).

## The recording/playback state machine

`ReplayManager_UpdateRecordingState` (called directly from
`InGameState_TickFrame`, the master per-frame gameplay tick) is a 14-state
(`0`-`0xd`) machine:

- **States 0/0xc** — idle/recording-tick: advances a frame counter, ticks
  `OverlapManager_ReorderBySize` periodically, and (once every `param_1+0x38`
  frames) calls `ReplayManager_AllocateAndInsertSnapshotNode` to capture a new
  snapshot.
- **State 2** — gated by `ReplayManager_CheckSnapshotBoundaryReached`
  (moderate confidence: checks whether the current position has passed the
  next queued snapshot's recorded position within an 11-unit margin).
- **State 3** — `ReplayManager_TickActiveSnapshotPlayback` (moderate
  confidence: advances an elapsed-time accumulator on the active snapshot by
  the position delta since the last tick — the actual "play back this
  recorded moment" step).
- **State 9** — the big one: the **race-end transition**. Calls
  `VenueStaging_ExitState(1)`, gathers up to 10 racer pointers (via
  `ReplayManager_AppendRacerToList`, filtering by a type flag at `+8`) from
  the current track's rider-slot table, then calls
  `ReplayManager_ResetAllRecordingSlots` and
  **`CameraScriptManager_EnterStagedCameraSequence`** — handing control to
  the camera system for the replay sequence.
- **State 0xb** — full teardown: `NodeRegistry_DestroyAllOfType(3)`/`(0xd)`/
  `(4)`, clearing whole node categories.
- Default/several states fall through to **`ReplayManager_TickCleanupState`**
  (a *nested* 3-state machine, own states 1/2/3) and
  **`ReplayManager_TickPlaybackAdvance`**.

## The snapshot allocator

A genuine time-indexed ring-buffer allocator underlies the recording:

- **`ReplayManager_AllocateAndInsertSnapshotNode`** — pulls a node from a
  free list (growing it via `ReplayManager_GrowSnapshotFreeList` if empty),
  resets its fields, and inserts it into a **time-ordered doubly-linked
  active list** (sorted by the passed time/frame key). Also calls the
  already-named `GameState_ResetTransientTriggerNodes` — ties replay-snapshot
  capture into that system too.
- **`ReplayManager_ReleaseExpiredSnapshotNodes`** — garbage-collects: walks
  the active list, returns any node with both its `+0x1c` and `+0x18` fields
  clear (unreferenced) back to the free list.
- **`ReplayManager_GrowSnapshotFreeList`** — allocates a new block of nodes
  when the free list is empty (via an unread pool/heap allocator,
  `FUN_000b8c80`).

## The bridge into `CameraScriptManager`

**`CameraScriptManager_EnterStagedCameraSequence`** (was `FUN_0007bd60`) is
the concrete link between "a race just ended, start the replay" and "switch
the camera system into venue-staging mode":

- Sets `DAT_001e26c6`/`DAT_001e26cc` — **the exact flag
  `CameraScriptManager_Tick` checks** to decide whether to hand off to
  `VenueStaging_Tick` each frame (see `RE_NOTES_camera_system.md`).
- Resolves a **gate-camera position** from the current track/level object
  (`DAT_001e3c7c+0x72c+0x1c+0x8c+index*4`) — ties directly to the `.cml`
  `"GATE_CAM"`-tagged records found in the loading-screen/camera-system
  investigation.
- Sole caller is `ReplayManager` (both the race-end transition and the
  slot-reset code) — confirming this is genuinely a `ReplayManager`-driven
  entry point into the camera system, not a general-purpose one.

This closes a real gap: previously, `CameraScriptManager`'s "staged sequence"
flag had no known setter; now the full chain is: **race ends → `ReplayManager`
state 9 → `CameraScriptManager_EnterStagedCameraSequence` → staged-sequence
flag set → `CameraScriptManager_Tick` (next frame) → `VenueStaging_Tick` →
`VenueStaging_TickCameraScriptCommand`** (the `.cml` runtime interpreter,
already documented) plays the actual gate/staging camera sequence.

## The racer-tracking / camera-slot cluster

Pulling the thread on `ReplayManager_FinalizeRaceEndSnapshot`'s call to the
then-unnamed `FUN_000bb1c0` turned up a whole per-racer tracking sub-system —
each racer gets a small **tracking-handle record** (holding a snapshot-node
reference plus a secondary allocation) claimed/released as racers enter or
leave the replay camera's tracked set, and sorted into up to **10 per-
camera-slot lists** keyed by a position/score value:

- **`ReplayManager_ClaimRacerTrackingHandle`** (was `FUN_000bbed0`, moderate
  confidence) — the entry point: gated on the current track's AI-manager
  sub-object not being disabled, pops a tracking node, increments its
  ref-count, registers the racer's position via
  `ReplayManager_InsertRacerIntoSlotList`.
- **`ReplayManager_InsertRacerIntoSlotList`** / **`RemoveRacerFromSlotList`**
  (were `FUN_000bac40`/`FUN_000bad30`) — insert/remove a racer into a
  per-camera-slot sorted list (`obj+0x3db4+slot*0x28`, up to 10 entries),
  keyed by score, popping/returning nodes from a shared free list.
- **`ReplayManager_FindBestCameraSlotForRacer`** (was `FUN_000bb2f0`,
  moderate confidence) — scores a racer against a threshold to pick which
  camera slot it belongs in.
- **`ReplayManager_ReassignRacerSlot`** (was `FUN_000bbf60`, moderate
  confidence) — re-scores a racer each tick and moves it between slots if
  its assignment changed, falling back to a full release if no slot fits.
- **`ReplayManager_ComputeRacerElapsedTime`** (was `FUN_000bc710`, moderate
  confidence) — computes an elapsed-time delta for a tracked racer before
  falling through to `ReassignRacerSlot`.
- **`ReplayManager_ReleaseRacerTrackingHandle`** (was `FUN_000bb1c0`) — the
  release side: decrements the snapshot-node ref-count (freeing once both
  ref-count fields hit zero) and removes the slot-list entry. Called from
  8+ sites across this cluster.
- **`ReplayManager_ReleaseRacerTrackingHandleFull`** / **`ReleaseAllRacerHandles`**
  (were `FUN_000bbe80`/`FUN_000bc6e0`) — fuller teardown variants (extra
  secondary pointers; iterating the whole 4-slot racer array).
- **`ReplayManager_ExitStagedSequence`** (was `FUN_000bd500`) — the
  counterpart to the race-end staging entry: de-stages the camera
  (`CameraScriptManager_EnterStagedCameraSequence(0,0)`), releases all
  tracked racer handles, and conditionally calls the already-named
  `GameState_ResetAndRebuildTransientNodes`.
- **`ReplayManager_TryClaimRacerHandleIfValid`** (was `FUN_0005a960`) — a
  thin adjustor-thunk-shaped guard found while tracing
  `ClaimRacerTrackingHandle`'s own caller into a previously-unanalyzed code
  region (`0x5a960`, far from the main `0xbxxxx` cluster) — its own caller
  (`0x0005a953`) sits in yet another still-unanalyzed region, not pursued
  further this pass.

This reads as the mechanism behind **instant-replay camera cuts between
racers** — e.g. the replay automatically following whichever racer is
"best" for a given camera angle, re-evaluated as positions change.

## The `Player` ↔ `ReplayManager` interface (closes the racer-tracking loop)

Tracing `ReplayManager_TryClaimRacerHandleIfValid`'s own caller led into a
previously-unanalyzed code region that turned out to be **`Player`'s own
destructor and vtable** — proving `Player` objects (not some generic
rider-array walker) are what claim/release `ReplayManager`'s racer-tracking
handles directly.

- **`Player_Destructor`** (was `FUN_0005a890`, the real `~Player()` body
  called from the already-named `Player_ScalarDeletingDestructor`) —
  releases 2 tracked handles via **`ReplayManager_RemoveRacerFromPendingArray`**/
  **`RemoveRacerFromActiveArray`** (were `FUN_000bbda0`/`FUN_000bdd40`) —
  confirmed by their bodies searching `ReplayManager`'s exact `+0x390c`/
  `+0x391c` racer arrays (the same `+0x390c` array
  `ReplayManager_ReleaseAllRacerHandles`/`FinalizeRaceEndSnapshot` iterate) —
  **direct proof `Player` unregisters itself from `ReplayManager` on
  destruction.**
- A small, clean **vtable interface** at `0x00189ef0`-`0x00189f2c` (adjacent
  to `Player_ScalarDeletingDestructor`'s own slot) turned out to be
  `Player`'s side of this connection — 5 thin adjustor-thunk wrappers (the
  `(this - *(this-4)) - 0x18` pattern is a standard multiple-inheritance
  `this`-pointer adjustment) around the `ReplayManager_*` racer-tracking
  functions already named:
  - **`Player_ClaimReplayHandleThunk`** (was `FUN_0005a950`) ->
    `ReplayManager_TryClaimRacerHandleIfValid`
  - **`Player_SetReplayTrackingFieldA`**/**`FieldB`** (were `FUN_0005a970`/
    `FUN_0005a9b0`, structural confidence) — write a float into the claimed
    tracking record's `+0xc`/`+0x10` fields, likely position/progress values
    feeding `ReplayManager_FindBestCameraSlotForRacer`'s scoring.
  - **`Player_ComputeReplayElapsedTimeThunk`** (was `FUN_0005a9f0`) ->
    `ReplayManager_ComputeRacerElapsedTime`
  - **`Player_ReleaseReplayHandleThunk`** (was `FUN_0005aa10`) ->
    `ReplayManager_ReleaseRacerTrackingHandleFull`

**This genuinely closes the racer-tracking cluster end to end**: `Player`
claims a handle (via the thunk interface) when it should be tracked for
replay/camera purposes, feeds it 2 position-shaped values every tick, and
releases it on destruction or exit — with `ReplayManager` doing the actual
bookkeeping (sorting into camera slots, scoring, snapshot capture) underneath.

## The rest of `Player`'s secondary interface vtable, fully mapped

The full 13-slot vtable (`0x00189ef0`-`0x00189f20`) `Player` implements this
alongside its own primary vtable turned out to hold more than just the
replay-tracking group: mapped every remaining slot.

- **Slots 0/2** were already-named shared `Rider` base methods
  (`Rider_ResetPhysicsState`/`Rider_HandleComponentStateEvent`) — not
  Player-specific.
- **Slots 9/11** are the shared `Node_NoOpStub1`/`Node_NoOpStub2` stubs.
- **3 genuine Player-specific overrides**, each a thin vtable-slot wrapper
  over a real implementation, all gated on the same current-track
  AI-manager racer-count field (`DAT_001e3c7c+0x72c+0x40+0x20` — the exact
  field the racer-tracking cluster above uses):
  - **`Player_HandleInputPollOverride`** -> **`Player_RemapInputButtonBit`**
    (moderate confidence) — checks `HUD_FindActiveOverlaySlot`, toggles a
    button bitmask bit (`0x800`) based on a stored/restored button-code
    byte — reads as button aliasing/remapping, exact semantics not pinned.
  - **`Player_TickDirectionalAudioCueOverride`** ->
    **`Player_TickDirectionalAudioCue`** -> **`Player_TriggerDirectionalWindCue`**
    (moderate confidence) — when not in a small tracked-replay set, computes
    a 4-component dot product (facing vs. velocity, plausibly) and triggers
    a directional audio cue past a threshold.
  - **`Player_TickVibrationGateOverride`** -> **`Player_TickVibrationGate`**
    -> **`Player_TriggerRumbleIfNotReplaying`** (moderate confidence) — same
    gate, calls a virtual method suspected to be controller rumble/force-
    feedback (a `"Vibration"`-tagged string sits adjacent to this vtable
    region, though not proven as this exact call's target).
- **`Player_GetUnitConstant`** (was `FUN_00031f60`) — a trivial float-
  constant getter rounding out the vtable; exact role in this interface not
  determined.

All 3 real overrides share the exact same racer-count gate the
racer-tracking cluster uses — a nice internal consistency check that this
whole 13-slot vtable is genuinely one cohesive "how does `Player` behave
differently while an instant-replay/staged-camera sequence is active"
interface, not several unrelated things bundled together.

**Left deliberately unexplored**: the underlying `FUN_000311d0`
(32 callers project-wide — genuinely generic shared infrastructure, out of
scope for Player-specific naming) that `Player_TickDirectionalAudioCue`
calls first.

38 renames total (11 initial + 10 for the racer-tracking/camera-slot
cluster + 8 for the `Player`↔`ReplayManager` interface + 9 for the rest of
the secondary interface vtable).

## Still open

- **`ReplayManager_TickCleanupState`'s 3 inner states** and
  **`ReplayManager_TickPlaybackAdvance`**'s exact semantics — read at
  structural confidence, not fully traced field-by-field.
- **Where `Player_SetReplayTrackingFieldA`/`FieldB` are actually called
  from each frame** — checked: virtual-dispatch only (`DATA` xrefs to the
  vtable slots), no static caller found. Genuinely needs dynamic analysis,
  not a static-search gap.
- **The `ReplayManager` ↔ `SaveGame` 3620-byte-record connection** — checked
  directly (see above): confirmed separate buffers, refined to "likely a
  shared record-builder routine, not found"; one search angle (`/search_bytes`
  for the `0xe24` immediate) tried and exhausted.
- **`ReplayManager_UpdateSequenceState`** (already named in an earlier
  session) still has zero static callers — an "orphaned Update method,"
  consistent with the systemic pattern already documented project-wide
  (needs dynamic analysis).
