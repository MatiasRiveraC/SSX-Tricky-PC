# RE notes: the `RiderEvent` lifecycle-dispatch system (found, not exhausted)

Found while tracing `VenueStaging_EnterNamedState`'s callers (see
`RE_NOTES_camera_system.md` "Found the actual consuming code") — that trace
led into a large, previously entirely-undocumented event system, distinct from
both already-mapped opcode interpreters (`Script_DispatchOpcode`'s 24
level-object types, `ScriptVM_DispatchOpcode`'s ~27 outer-VM opcodes).

## The dispatch mechanism

Two sibling switch statements, both reading the same shape of state-ID field
(`*(int*)(*(int*)(*(int*)(this+0x30)+4)+0x488+this)`):

- **`RiderEvent_DispatchTypeA`** (was `FUN_00031510`) — ~17 cases.
- **`RiderEvent_DispatchTypeB`** (was `FUN_00031640`) — ~22 cases.

**`RiderEvent_SetState`** (was `FUN_00032cb0`) is the canonical setter: checks
current state != new state, validates the new value via `FUN_000313f0`,
captures the old state, writes the new one, then dispatches via
`RiderEvent_DispatchTypeA`. This is a clean, ordinary state-machine idiom —
not a mystery in its own right, just previously unnamed.

## Confirmed connections to already-documented code

- **`RiderEvent_DispatchTypeB` is called from the per-frame
  `Rider_UpdatePhysicsState`** (`0x36990`) — ties this system directly to the
  rider's per-frame physics/state tick. (NOTE: that function was formerly
  mislabeled `Rider_TeardownSubobjects`; corrected after reading its body —
  it's an update, not a teardown. See `RE_NOTES_terrain_collision.md`.)
- **`RiderEvent_DispatchTypeA` is called from `Rider_NotifyLifecycleEvent`**
  (was `FUN_00034160`) — a function *already referenced, but never named*, in
  an earlier session's notes on `GameMode_CheckAndPlayStartCountdown`: "the
  same per-active-entity notification idiom seen in
  `Timer_RebuildPlayerRegistry` and `HUD_ShowTimeGapCallout`." Confirmed: it's
  called once per active entity at race-countdown time, reads the HUD-struct
  pointer at `rider+0x58e0` (the same offset `HUD_ShowTimeGapCallout` uses),
  and transitions the `RiderEvent` state to `0x11` or `8` (selected by a bool
  parameter) if not already there.

## Two confirmed handlers name real venue-staging states

- **`RiderEvent_EnterRaceStarted`** (was `FUN_00023c60`, `DispatchTypeA` case
  7) — gated on a timer/countdown field being non-negative, calls
  `VenueStaging_EnterNamedState("RaceStarted", 0)`.
- **`RiderEvent_EnterRaceStarted2`** (was `FUN_00023ac0`, `DispatchTypeB` case
  0x11) — gated on a game-context state field `== 4`, calls
  `VenueStaging_EnterNamedState("RaceStarted2", 0)`, plus two more calls with
  numeric IDs (`FUN_00067410(0x1fe,0,5)`, `FUN_00032c60(6)`,
  `RiderEvent_SetState(0x12)`) not traced further this pass.
- **`RiderEvent_ResetTripleCounter`** (was `FUN_000239c0`, `DispatchTypeA`
  case 8) — zeroes 3 consecutive fields (`+8`/`+0xc`/`+0x10`) on the HUD-struct
  object. Reads as a per-rider HUD counter reset (combo/streak display is a
  plausible guess, not confirmed) — **not** the core `+0x5710` score field,
  which lives on a different struct entirely.

## Checked directly for the score-writer lead — negative, but a real check

Swept **all ~35 case-handler functions** across both switches for any
reference to the score offset `+0x5710` (the standing multi-session
score-writer lead) — **zero matches**. Consistent with, and reinforcing, the
earlier exhaustive project-wide `/search_bytes` sweep that found no write
instruction anywhere in the binary. This doesn't newly rule anything out (the
+0x5710 field was already known not to be written anywhere statically
findable) but confirms this new system isn't a previously-invisible exception
to that.

## Clarified the whole system's role: rider/board animation triggers, not scoring

Sampled 6 more of the unread `DispatchTypeA` case handlers (cases 2, 3, 4, 6,
9, 0xc). Most are dense 3D/quaternion physics math (direction flips, distance
threshold checks, blend calculations) — not individually decoded to full
confidence this pass, but they share one clear, common pattern: several call
**`RiderAnimation_TriggerByEventCode`** (was `FUN_00067410`) with a numeric
event code (`0x1fe`, `0x1ff`, `0x200`, `0x20c`, `0x20d`, `0x20e` seen so far).
That function allocates a **`New_BdrSeq`** ("Board Sequence", an
already-documented tagged-allocator pool) and triggers a board animation
matching the event code.

**This clarifies the whole `RiderEvent` system's architectural role**: it's a
rider/board **animation-trigger dispatcher** reacting to physics and
lifecycle events (landing, direction changes, race-start, etc.) — not the
core trick-scoring logic. This is consistent with (and reinforces) the
earlier finding that `RiderEvent_ResetTripleCounter` only touches a small
HUD-struct, not the `+0x5710` score field. The event-code space (`0x1fe`
onward, sparse jumps to `0x20c`-`0x20e`) is a plausible enum worth mapping in
full if a future session wants the complete animation-trigger vocabulary, but
that's a different, lower-priority goal than the score-writer question this
thread originally set out chasing.

## Closed the loop: the animation event-code table

Followed `RiderAnimation_TriggerByEventCode` one level deeper.
**`BdrSeq_ConfigurePlaybackFromEventCode`** (was `FUN_00066e70`) configures a
`BdrSeq`'s low-level playback state (frame indices, blend-source selection,
timing) by looking up per-event-code parameters from a 28-byte-stride table.
That table is populated entirely by **`BdrSeq_InitEventCodeTable`** (was
`FUN_00062110`) — a single, ~19KB mechanical initializer (hundreds of
unrolled constant writes, one per event code) with exactly one caller (a
generic-looking object constructor, not individually named this pass). This
fully closes the loop on "what does an event code like `0x1fe` actually
configure" — it's real, complete, explainable animation-blend machinery, not
a mystery. Not worth cataloguing all ~200+ individual table entries by hand;
the *mechanism* is now fully understood even though the *full event-code
vocabulary* isn't enumerated.

## The race-finish sequence and a real lead on the score-field region

Swept more of the unread `DispatchTypeA` handlers and case 6 turned out to be
the session's best find in this thread: **`RiderEvent_RaceFinishSequence`**
(was `FUN_00024330`) — a complete, previously undocumented 4-sub-state
finish flow:

- **Sub-state 0** waits for a timeout, a threshold float condition, or
  elapsed time (`FUN_000215b0`, the same helper `HUD_ShowTimeGapCallout`
  already uses) before advancing.
- **Sub-state 1** scales some vector fields, then (on the same kind of
  condition) triggers `RiderAnimation_TriggerByEventCode(0x2ee,0,5)` — very
  likely a finish-line-crossing animation.
- **Sub-state 2** branches heavily on `GameMode_Current` (different logic for
  Race/Showoff (3/5), mode 7, mode 6, and a generic fallback checking active
  player count), reads **`rider+0x5720`**, and resolves it to a medal tier via
  **`MedalTier_ResolveFromValue`** (was `FUN_000737b0`). Then triggers a
  second event (`0x2ef`, likely a celebration/results animation), does a
  brief field fade (0.5 → 1.0), calls a sibling `RiderEvent_SetState`-family
  setter, and advances.
- **Sub-state 3** waits for `RiderEvent_GetSubState(1)==8` (an animation
  playback-complete signal) then finishes.

**`MedalTier_ResolveFromValue`** is the key clue: for one specific
`DAT_001dec90` value, it compares its input directly against fixed
thresholds **250000 / 500000 / 799999** (bronze/silver/gold) — numbers only
sensible for an accumulated *score*, not elapsed time. For every other
`DAT_001dec90` value, it instead delegates to
**`MedalTier_LookupTrackThresholds`** (was `FUN_00073770`), an 8-entry
per-track table lookup — the per-track time-target medal system already
partly documented via `Race_ComputeRankings`.

**This means `rider+0x5720` is very likely a "final/session score" field**,
sitting just 16 bytes from the confirmed live trick-score field at
`rider+0x5710`, and consumed specifically at race-finish for medal
determination.

**Searched the whole binary for the raw `0x5720` displacement bytes**:
exactly **one** static reference exists anywhere — this same read inside
`RiderEvent_RaceFinishSequence`. No write instruction found, mirroring
`+0x5710`'s own situation exactly (also never found written anywhere
statically, across multiple exhaustive sessions). This doesn't newly *solve*
the score-writer mystery, but it meaningfully **enriches** it: the
score-field region (`+0x5710`-`+0x5720`) has a confirmed, concrete consumer
(medal determination at race finish) even though its producer (whatever
increments/finalizes it) remains unfound by any static technique tried so
far across this whole project.

## STALE-NOTE CORRECTION (2026-07-22): the "still open" list below was already fully closed

Picked this file back up (per an explicit user request to prioritize core
gameplay over frontend UI) expecting to read ~30 unread case handlers.
Per this project's "check before declaring fresh" discipline, checked each
candidate address live via `/decompile_function` *before* touching
anything — and found **every single one already named**. The whole
`DispatchTypeA`/`DispatchTypeB` cluster had already been fully closed in an
earlier session (2026-07-20 — a "17 renames" comment already sits in
`ssx_auto_rename.py` right above the `DispatchTypeB` entries), but this
file's own "still open" section below was simply never updated to match.
Corrected here rather than silently re-doing (or worse, mis-attributing as
new) already-finished work — see `feedback_fix_old_mistakes_on_discovery`.

**What that closed cluster turned out to be**: almost entirely the
**rail-riding subsystem** (matches the manual's "Rail Riding" section —
jump onto a rail, lean/balance, exit), plus ground-steering/lean input
handling, grab-input combo resolution (multi-button grab detection matching
the manual), landing-orientation/stance resolution, and the race-finish
sequence documented above. Key named functions from that cluster:
`RiderEvent_ProcessGrabInput`, `RiderEvent_EnterRailRide`,
`RiderEvent_TransitionToRailMode`/`TransitionToGroundMode`,
`RiderEvent_UpdateRailRideMovement` (the master per-frame rail-ride tick),
`RiderEvent_ProcessRailLeanInput`/`ProcessRailBalanceInput`/
`ProcessRailBalanceAndExit`, `RiderEvent_CheckRailComboTimeout`,
`RiderEvent_ResolveLandingOrientation`, `RiderEvent_ResolveJumpTakeoffStance`,
and `RiderEvent_RaceFinishSequence` (see above). Full detail for every one
of these lives in `ssx_auto_rename.py`'s comments, not repeated here.

**The one genuinely leftover sub-item, chased this pass**: "`RiderEvent_
SetState`'s other 8 callers not individually traced" was *also* stale —
re-checked via a fresh `xrefs_to` and found almost all of its ~39 callers
were already named too. Found and named 5 of the remaining 6 genuinely
unnamed ones:

- **`Rider_FinalizeGroundLandingState`** (was `FUN_0003c630`) — called from
  `Rider_ResolveTerrainContactPhysics` and `Rider_CheckLandingRecoveryState`;
  commits the "landed" RiderEvent state and normalizes steering angle for
  the new ground orientation.
- **`Rider_HandleGroundModeEntry`** (was `FUN_0003c6a0`) — sole caller
  `Rider_PhysicsMode2_GroundContact`'s own per-frame tick; the ground-
  contact tick's own state-entry handler.
- **`Rider_CheckLandingRecoveryState`** (was `FUN_00039e60`) — bridges
  `RiderEvent_TransitionToGroundMode` and `Rider_HandleGroundModeEntry`
  into one shared landing-recovery path.
- **`RiderEvent_SetStateThunk`** (was `FUN_0001e590`) — a trivial 1-
  instruction forwarding thunk called from `Rider_ApplyMotionUpdate`/
  `HUD_ShowTimeGapCallout`.
- **`Rider_ResolveLandingOutcome`** (was `FUN_000375e0`) — picks one of 6
  landing-animation event codes based on stance/threshold checks, triggers
  the matching commentary reaction, then transitions state — "the trick
  just ended, resolve how well it landed."

The 6th (`FUN_0003c360`) was picked back up immediately after and closed
too: **`Rider_CheckAttachedComponentOrientationLimit`** — gated on the
rider having an attached component of category `0xe`/`0xf`/`0x10`, it
builds an orthonormal basis via cross products and checks 2 computed
angles against character-tuning-scaled thresholds, returning a packed
comparison-flags result — an orientation/tilt-limit check for a rider
attached to a rail/special component. Sole caller:
`Rider_PhysicsMode1_GroundRide`.

6 renames this pass (13 total across this file's threads: 7 from the
original session + 6 here). **This fully closes every `RiderEvent_
SetState` caller** — the whole ground-landing/mode-transition cluster is
now named end to end.

## CORRECTION + port: the transition is enter/exit, not validate (M3c)

### Correction to this file
The "dispatch mechanism" section above says `RiderEvent_SetState` "validates the
new value via `FUN_000313f0`". **It does not validate anything.** Reading the
body: `FUN_000313f0` switches on the state field *before* the write, so it is
the **EXIT handler** for the state being left. Renamed
**`RiderEvent_DispatchStateExit`** (`0x000313f0`, 14 cases).

The real shape is a textbook enter/exit machine:
```c
if (newState != cur) {
    RiderEvent_DispatchStateExit();   // switches on the CURRENT state (14 cases)
    old = cur; cur = newState;
    RiderEvent_DispatchTypeA(old);    // switches on the field, now NEW (16 cases)
}
```
`RiderEvent_DispatchTypeA` takes the old value as an argument but **ignores it
and reads the field**, so it is the ENTER handler. `RiderEvent_DispatchTypeB`
(22 cases), called per frame from `Rider_UpdatePhysicsState`, is the TICK
handler. Three tables, 14 / 16 / 22.

### The state field's true address
`*(int*)(*(int*)(*(int*)(this+0x30)+4)+0x488+this)` is the component-frame
pattern with slot `0x30`, whose descriptor offset is `0x840` — so the state
lives at **`rider+0xCC8`**, immediately after the physics-mode selector at
`rider+0xCC4`. Two adjacent selectors, which is a useful sanity anchor.

### Enter-handler table (all already named)
| State | Handler |
|---|---|
| 2 | `RiderEvent_ResetSubStateTimer` |
| 3 | `RiderEvent_ResetSubStateFields` |
| 4 | `RiderEvent_ToggleSwitchStance` |
| 6 | `RiderEvent_CheckVelocityThreshold` |
| 7 | `RiderEvent_EnterRaceStarted` |
| 8 | `RiderEvent_ResetTripleCounter` |
| 9 | `RiderEvent_ApplyCharacterTuningValue` |
| 0xb, 0xc | `RiderEvent_ProcessGrabInput` |
| 0xd | `RiderEvent_ResetTimerAndSyncFlag` |
| 0xe | `RiderEvent_ResetAllTimersAndNotify` |
| 0xf | `RiderEvent_ResolveJumpTakeoffStance` |
| 0x10 | `RiderEvent_ApplyGrabDecayScaling` |
| 0x13 | `RiderEvent_InvokeStateEnterCallback` |
| 0x15 | `RiderEvent_ResetToRegularStance` |
| 0x16 | `RiderEvent_ClearSubStateFlag` |

State **4** (`ToggleSwitchStance`) is the same mechanic found from the physics
side in `Rider_EvaluateGroundMovementTransition` — two independent routes to the
switch/fakie flip.

### Ported: `port/src/game/riderevent.{h,cpp}`
Verified in `asset_test` — the ordering is the easy thing to get wrong, so it is
asserted as a trace:
```
[revt] trace: X0;E7;X7;E15;T15;X15;E21;
[revt] state=21 prev=15 transitions=3 (expect 21, 15, 3)
```
Exit fires for the state being left, enter for the one entered, a repeated
`setState` is a no-op and does not count as a transition.
