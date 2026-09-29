# RE notes: `PowerFX Particles` — the 12-category particle pool, and a real lead back to the Uber Trick glow

Continued the "map the untouched `InGameState_LoadLevel` tagged objects" direction
(2026-07-20), after `SnowFallMan` (`RE_NOTES_weather_effects.md`) and `LessonMan`
(`RE_NOTES_tutorial_system.md`). `"PowerFX Particles"` (`0xb60`/2912 bytes) turned
out to be a third instance of the same architecture — but this one has a genuine,
traceable static caller chain, and it connects directly back to this session's
very first thread: the trick-score-writer investigation.

## Architecture

`PowerFXParticles_Construct` (was `FUN_000e85e0`) calls the same
`NodeRegistry_Insert` wrapper (`FUN_000aa940`) as `PREAI`/`PostAI`/`SnowFallMan` —
passed literal type **`2`**, the *same* type `SnowFallMan` uses. This confirms
`NodeRegistry` types are shared **categories** that can hold multiple
simultaneous instances, not unique per-class IDs (worth remembering for anyone
continuing the `{2,5,7,8}` type-array thread in `RE_NOTES_rider_update_chain.md`).

Zeroes **12 fixed category slots** (`0xf0`-byte stride each: an active-flag byte
+ a `0xe0`-byte descriptor buffer + a small particle-instance list), then
allocates a **32000-byte `"dynamic particles"` pool** and threads it into a
250-slot free-list — a classic pool allocator. This pool is the real backing
store particle instances get allocated from.

- **`PowerFXParticles_ActivateCategory`** (was `FUN_000e7dd0`) — claims the
  first free category slot and copies in a caller-supplied `0xe0`-byte
  descriptor. The real "start this particle effect" entry point.
- **`PowerFXParticles_DeactivateCategory`** (was `FUN_000e7e70`) — the
  counterpart: returns a category's instances to the free-list pool, clears
  the active flag.
- **`PowerFXParticles_RenderActiveCategories`** (vtable slot 2, was
  `FUN_000e80a0`, found in completely un-analyzed code) — sets up alpha-blend
  render state and draws every currently-active category via
  **`PowerFXParticles_DrawCategory`** (was `FUN_000e7f20`).
- Same shared-stub pattern as `SnowFallMan`/`LessonMan`: vtable slots 1 and 7
  are the generic `Node_NoOpStub1`/`Node_NoOpStub2`, so the real per-frame
  render dispatch does NOT go through the standard `NodeBase` Update slot.

This is now the **third confirmed instance** of the "NodeBase-derived
`InGameState` subsystem, generic Update slot inert, real logic in custom vtable
slots" architecture found this session.

## The connection back to the trick-score-writer thread

`PowerFXParticles_ActivateCategory` has exactly **2 callers**, both inside ONE
function — which turned out to be **`FUN_000455b0`**, one of the 15 sub-calls of
`Rider_UpdatePhysicsState` this session flagged as unexplored during the very
first trick-score-writer push (see `RE_NOTES_rider_update_chain.md`'s "one more
check" list) and never got back to. Opened it via this completely unrelated
thread.

Renamed **`Rider_UpdateUberTrickGlowFX`** — moderate confidence, not proven with
an explicit string/tag, named from strong circumstantial evidence rather than
certainty. The function gates a whole branch on a per-rider condition byte; on
the rising edge (first frame the condition becomes true) it activates **two**
PowerFX particle categories, computed from what look like two limb/board
attachment-point positions tracked frame-to-frame with an exponential-smoothing-
shaped update (`FUN_0003e040`/`FUN_0001e750`); on the falling edge it
deactivates both (with debug-print breadcrumbs `"deleting 0 = %d"`/`"deleting 1
= %d"` still present in the retail binary — a nice confirmation this code
genuinely runs/ran during development).

The two-simultaneous-glow-points-tracking-limb-positions shape, combined with
the `"PowerFX"` naming and this project's already-documented Über Trick
availability/charge HUD announcement (`rider+0x24` countdown timer, see
`RE_NOTES_race_hud.md`), strongly suggests this is the **Über Trick charge/glow
visual effect** — the two glow points likely being the rider's hands or the
board's tips. Not certain: no explicit tag string ties `FUN_000455b0` to "Uber
Trick" by name, so treat this identification as informed, not proven.

**Did not find the score write here either** — this function is real, verified,
substantial gameplay logic tied to `Rider_UpdatePhysicsState`, but it's a visual
effect trigger, not a score accumulator. No `+0x5710`/`+0x5780` references found
in it. Still, it closes out one item from the 15-function "unexplored
`Rider_UpdatePhysicsState` sub-calls" list flagged much earlier this session —
14 remain (`FUN_0001e5d0`, `0001e7f0`, `0001ea00`, `00021580`, `000308d0`,
`00032f20`, `00033790`, `00033cb0`, `00033df0`, `00038530`, `0003bc30`,
`0005d1f0`, `0005e410`, `000ab170`).

**Stale by the end of this session — all 14 are also now named** (via other
threads across many sessions: `Math_Atan2`/`Vector4_DotProduct`/
`Math_ClampTowardTargetWithMargin`/`Vector4_Scale`/`Rider_InitAmbientZoneInfluences`/
`Rider_SelectLocomotionAnimState`/`Rider_DecayTrackedGrudgeAfterCooldown`/
`Rider_UpdateTrackPathPosition`/`Rider_UpdateTrackEventTriggers`/
`Rider_ProcessCollisionsWithOthers`/`Rider_UpdateSpeedIntensityFX`/
`Rider_AccumulateStateTimer`/`Rider_AccumulateCameraShakeInputs`/
`GameState_ShouldSkipGameplayTick`). See `RE_NOTES_rider_update_chain.md`'s own
"This closes out the entire original 15-function `Rider_UpdatePhysicsState`
sub-call sweep" note — that file already had the correct, up-to-date closure;
this file's list just wasn't updated to match. Fixed 2026-07-21.

7 renames.
