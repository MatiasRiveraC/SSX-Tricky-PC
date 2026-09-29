# RESOLVED: the score-writer mystery — `TrickCombo`, the rider's trick/combo scoring sub-object

**2026-07-20.** This closes out the single most actively-hunted-for question across this
entire multi-session project: *what actually writes to `rider+0x5710`, the field
`HUD_DrawRaceOverlay` displays as the on-screen score?* Every earlier attempt (an
exhaustive `/search_bytes` sweep for the literal `10 57 00 00` displacement across the
whole binary, a full C++ vtable-chain trace of `Rider`'s own methods, checks of
`ScriptVM_Tick`, `Component_UpdateAll`'s list mechanism, `RiderEvent_Dispatch*`'s ~35
handlers, and more) found only *reads* of `+0x5710`, never a write — see
`RE_NOTES_rider_update_chain.md`'s long "score-writer investigation" thread and
`RE_NOTES_application_boot.md`'s parallel attempts.

## How it was found

Picked up from a completely different thread this session: sampling gameplay-relevant
callers of the newly-found `Commentary_QueueEvent` wrapper cluster led to
`TrickCombo_NotifyTrickLanded` (was `FUN_0005ede0`), called from
`Rider_ResolveTerrainContactPhysics`/`Rider_PhysicsMode1_GroundRide`/
`Rider_UpdateTerrainContact` — all already-documented Rider physics functions. Direct
disassembly of the call site in `Rider_UpdateTerrainContact` showed:

```
LEA ECX,[ESI + 0x5630]
...
CALL 0x0005f1c0
```

i.e. this whole cluster operates on an embedded Rider sub-object at **`rider+0x5630`**,
not the Rider directly. Reading `TrickCombo_ScoreRailCompletion`/`TrickCombo_ScoreAirCompletion`
(the two functions that actually call `TrickCombo_NotifyTrickLanded`) turned up the key
line, present in **both, independently**:

```c
*(int *)(param_1 + 0xe0) = *(int *)(param_1 + 0xe0) + iVar4;
```

with `param_1 = rider+0x5630`. **`0x5630 + 0xe0 = 0x5710`.** This is the write. It was
never found by any earlier byte-level search because the compiler expressed it as a
local offset (`+0xe0`) off an already-rebased pointer (`this = rider+0x5630`), not as a
literal `rider+0x5710` displacement anywhere in the instruction stream — the byte
pattern `10 57 00 00` genuinely does not exist for this write. A real, mechanically
well-evidenced answer for *why* the exhaustive search missed it, not a shrug.

## The `TrickCombo` sub-object

Every `Rider` embeds a `TrickCombo`-style tracker at `+0x5630`, managing one
in-progress trick/combo sequence:

- **`TrickCombo_StartNewSequence`** (was `FUN_0005f650`) — called when the rider
  leaves the ground/rail. Flushes/finalizes any in-progress trick via
  `TrickCombo_ScoreRailCompletion`, then resets the sequence-tracking fields
  (`+0x20`/`+0x24`/`+0x38`/`+0x3c`/`+0x28`/`+0x2c`/`+0x30`/`+0x10`) to start fresh.
- **`TrickCombo_ScoreRailCompletion`** (was `FUN_0005ef70`) and
  **`TrickCombo_ScoreAirCompletion`** (was `FUN_0005f1c0`) — the two trick-completion
  paths (rail tricks vs. air/ground tricks). Both: compute a score bonus (trick-count
  scaled, plus combo-streak and "big air" bonuses for the air path), dispatch a HUD
  score-popup callout via `HUD_DispatchScoreEventCallout`, **accumulate the bonus into
  the running score total (`this+0xe0` = `rider+0x5710`)**, update the best-single-trick
  record (`+0xd4`/`+0xd8`/`+0xdc`) if beaten, and call `TrickCombo_NotifyTrickLanded`.
- **`TrickCombo_NotifyTrickLanded`** (was `FUN_0005ede0`) — formats the human-readable
  trick/combo name (`Trick_FormatComboName`), then — gated on this being the
  watched/local rider in a valid state — queues rivalry-adjacent commentary or a
  team-mode variant, and fires a plain HUD event code for trick-landed/knockdown-style
  feedback.
- **`Trick_FormatComboName`** (was `FUN_0005d030`) — **content-confirmed, not
  structural guesswork**: read the actual lookup-table bytes live from the binary.
  Builds a 14-part name string from a packed 8-byte trick-record bitfield via 14
  bitfield-indexed word tables. Confirmed real SSX vocabulary in the tables: `"50/50
  Rail"`, `"Switch 50/50 Rail"`, `"BS Rail"`, `"FS Rail"`, `"Rail To Switch"`,
  `"180"`/`"360"`/`"540"`/`"1800"`, `"Late"`, `"Double"`/`"Triple"`, `"Front Flip"`,
  `"Back Flip"`, `"Rodeo"` — this is the actual generator behind SSX Tricky's
  signature dynamically-composed on-screen combo names.
- **`HUD_DispatchScoreEventCallout`** (was `FUN_0005e680`) — takes a numbered event
  code (2/3/7/8/0xc/0xf/0x10 confirmed via call sites) and searches a fixed 12-entry
  event-slot table (`this+0x14c`) for a matching/free slot — the dispatch mechanism
  behind the score-popup/callout panels, very likely feeding the floating score-popup
  panels already documented in `RE_NOTES_race_hud.md`'s `HUD_DrawWorldSpaceMarkers`
  writeup.

6 renames.

## The full scoring formula, resolved (immediate follow-up, same session)

Pushed straight on into `TrickCombo_ScoreRailCompletion`/`ScoreAirCompletion`'s own
5 remaining sub-calls — the actual point-value formula, not just the write location:

- **`TrickCombo_ComputeTrickPoints`** (was `FUN_0005d150`) — the base point-value
  formula: `round(this+0x14 [accumulated difficulty/airtime] * this+0x18 [a
  multiplier — 1.0 normally, ~1.3 for a rail-transfer bonus] * a global scale
  constant)`, truncated down to the nearest 10, or 0 below a tiny epsilon. **This is
  the actual point-value formula this whole investigation was ultimately after.**
- **`TrickCombo_ComputeBaseTrickPoints`** (was `FUN_0005d1a0`) — the same formula
  without the `this+0x18` multiplier, used as an extra display parameter for the HUD
  callout.
- **`TrickCombo_GetStreakBonusValue`** (was `FUN_0005d6b0`) — a clean switch table:
  streak count 0/1→0, 2→4000, 3→8000, 4→12000, 5+→16000 — the combo-streak bonus
  added on top of the base score. Same 4 point thresholds as the already-documented
  `Trick_GetScoreTier` (was `FUN_0005d700`, a *different*, unrelated function that
  maps a score back to a display tier) — confirmed these are inverses of each other,
  not duplicates, before naming.
- **`TrickCombo_CountRecentRepeats`** (was `FUN_0005d790`) — maintains a 5-entry
  circular history of recent trick-type records and counts how many of the last 5
  match the current one. **This is a genuine, previously-undocumented anti-farming
  rule**: `TrickCombo_ScoreAirCompletion` divides the computed score by
  `(repeatCount+1)` — repeating the exact same trick back-to-back measurably reduces
  its point value.
- **`TrickCombo_EncodeTrickRecord`** (was `FUN_0005d860`) — the encoder that feeds
  `Trick_FormatComboName`'s decoder: converts the rider's raw accumulated spin/flip
  angles (reduced to spin-count-in-180°-units and flip-count-in-360°-units) plus
  switch/fakie/grab-type flags and a grind-transfer count into the packed 8-byte
  trick-record bitfield. Also resolves a specific spin+flip combination into a
  named-trick lookup index via a second pair of 15×7 tables
  (`DAT_001aad48`/`DAT_001aaf98`) — very likely how specific named tricks get their
  own distinct name rather than a generic "N degrees + M flips" description. Large
  and complex; named with structural confidence on its overall role, not every
  individual bit.

5 more renames (11 total for the whole `TrickCombo` investigation).

## What this resolves, and what's still open

This definitively answers "does a write exist, where, and by what formula" — the
location, the base point-value math, the combo-streak bonus table, and a genuine
anti-farming repeat-penalty rule are all now confirmed. What remains unread: the exact
bit-layout `TrickCombo_EncodeTrickRecord` packs (which specific bits mean what, beyond
the overall shape), and the big-air-bonus threshold constant's exact tuning value —
low-value polish rather than open architecture questions.

**This also means every earlier "honest negative result" documented across this
project's score-writer investigation was genuinely correct as far as it went** — the
write really isn't reachable via `Rider`'s C++ vtable, `Component_UpdateAll`'s lists,
or a literal-offset byte search — it just required following gameplay physics call
chains (terrain contact) into a previously-unexplored embedded sub-object instead.

## Bonus: the actual Uber Trick names, confirmed by reading real string content

`TrickCombo_EncodeTrickRecord` resolves a rider's spin+flip combination into a
"named trick" lookup index (via **`UberTrickSpinFlipToNameIndex`**, was
`DAT_001aad48` — a 21×7 spin-count × flip-count grid), writes it into the trick
record's own byte 0, and `Trick_FormatComboName` consumes it directly as one of its
14 word-slots via **`UberTrickNameTable`** (was `PTR_DAT_001aaa58`). Read the actual
table content live — this is SSX Tricky's real, complete Uber Trick name escalation:

| Index | Name | Index | Name | Index | Name |
|---|---|---|---|---|---|
| 2 | Egg Flip | 17 | Horrifying | 32 | Homesick |
| 3 | Mc Twist | 18 | Gross | 33 | Double Jointed |
| 4 | Haakon Flip | 19 | Backwash | 34 | Spider |
| 5 | Wetcat | 20 | Ambulance Trip | 35 | Chunks |
| 6 | Full On | 21 | Ligament | 36 | Heimlich |
| 7 | Breakdancer | 22 | Cartilage | 37 | Gargle |
| 8 | Scratch Artist | 23 | Hospitalized | 38 | Sick |
| 9 | Famous | 24 | Life Insurance | 39 | Shell Cracker |
| 10 | DJ | 25 | Tripped Out | 40 | Deathwish |
| 11 | Mindless | 26 | Plain Brown Wrapper | 41 | Iron Lung |
| 12 | Banzai | 27 | Brown Bag | 42 | Multiple Fracture |
| 13 | Shiny | 28 | Lunch Box | 43 | Roadkill |
| 14 | Torpedo | 29 | Grab Bag | | |
| 15 | Twister | 30 | Crippled Squirrel | | |
| 16 | Nose Picker | 31 | Montezuma | | |

A clean progression: low indices are genuine named tricks (`McTwist` and
`Haakon Flip` are real professional snowboarding tricks — Terje Haakonsen's
signature move), escalating into SSX's signature comedic
increasingly-dangerous-sounding names as the spin+flip count rises past what's
physically reasonable ("Ambulance Trip", "Hospitalized", "Life Insurance",
"Multiple Fracture", "Roadkill"). A second, much sparser table
(**`UberTrickSpinFlipToNameIndex_SpecialVariant`**, was `DAT_001aaf98`, only 3
nonzero entries) is used under a separate flag condition — likely a switch-stance
or rail-to-air naming variant, not fully traced.

3 data renames (`UberTrickNameTable`/`UberTrickSpinFlipToNameIndex`/
`_SpecialVariant`), bringing this whole investigation to 11 function renames + 3
data renames.

## The trick-type commentary dispatch, resolved (immediate follow-up)

`TrickCombo_NotifyTrickLanded` calls one of two commentary dispatchers depending on
game mode — both now fully mapped:

- **`Commentary_DispatchTrickLandedReaction`** (was `FUN_00123fa0`) — the main,
  non-team-mode path. `this`=the `AudioSystem` singleton, `param_2`=the rider,
  `param_3`=the packed trick-record (the SAME bitfield `TrickCombo_EncodeTrickRecord`
  builds and `Trick_FormatComboName` decodes), `param_4`=the trick's point value.
  Rolls a value-tier-scaled probability check and queues a generic "big trick
  landed" event (flagging whether the value crossed 3999). Then, cooldown-gated,
  decodes the trick-record's grab/spin/flip/rail bitfields through two large
  (~100-entry) switch-table lookups converting trick-component IDs into specific
  voice-line IDs, and dispatches them via **`Commentary_QueueTrickTypeReaction`**
  (was `FUN_00120e30`, opcode `0x100200f`, up to 8 category parameters) and, for a
  secondary trick-modifier byte, **`Commentary_QueueTrickVariantReaction`** (was
  `FUN_00120eb0`, opcode `0x1002010`, single parameter). Finally rolls one more
  probability check (gated on ShowoffMode) for a generic reaction line.
- **`Commentary_DispatchTeamTrickReaction`** (was `FUN_001252e0`) — the much
  simpler team-mode sibling: a probability roll plus a re-entrancy guard, then
  **`Commentary_QueueTeamTrickReaction`** (was `FUN_00120ef0`, opcode `0x100201a`,
  no arguments).

Confirms `this+0x6e1c`/`+0x6e20` (the fields `TrickCombo_ScoreRailCompletion` writes
directly on the `AudioSystem` singleton) are a cooldown/dedup pair specifically for
this trick-commentary path — the loop closes end to end: land a trick →
`TrickCombo_Score*Completion` computes points and sets the cooldown flags →
`TrickCombo_NotifyTrickLanded` formats the name and picks a dispatcher →
`Commentary_Dispatch*TrickReaction` resolves the specific trick type into a voice
line → `Commentary_QueueEvent` actually queues it.

5 renames. The two ~100-entry switch tables are named on overall role
(trick-component-ID → voice-line-ID resolution) rather than every individual
mapped value — a large but low-uncertainty finding, good enough to stop at
structural confidence here.

## More `Commentary_QueueEvent` wrapper callers swept (immediate follow-up)

Continued through 5 more of the remaining ~25 wrapper callers, all clean, structurally
consistent finds:

- **`Commentary_DispatchRiderEventReaction`** (was `FUN_00125140`) — the richest of
  this batch. For event-type codes 4/0xb/0xc/0x10, resolves a flag and queues
  `Commentary_QueueRiderEventFlagReaction`. **Separately**, for event-type codes
  0xe/0xf, checks the point value against the **exact same 4000/8000/12000/16000
  thresholds as `TrickCombo_GetStreakBonusValue`/`Trick_GetScoreTier`** and queues
  `Commentary_QueueRiderEventTierReaction` — very likely called from the
  already-documented `RiderEvent_DispatchTypeA`/`TypeB` system
  (`RE_NOTES_rider_event_system.md`), directly tying trick-value tiers into that
  dispatcher's event-code space.
- **`Rider_TriggerProximityCommentary`** (was `FUN_00123150`) — a sibling of
  `Rider_TriggerOvertakeCommentary`: same rider-pair validity/relationship gate and
  same track-segment proximity check, but simpler (no forward-projected
  position/height-band math, just "are both riders on the same segment"). Resolves
  both riders' voice bitmasks and shows a floating reaction cue via
  `SpeechSlot_SetLineParamsAndProcess` — a plain "riding near a rival" trigger,
  distinct from the overtake-specific one.
- **`Commentary_TryTriggerLimitedReaction`** (was `FUN_00125610`) — gated on a
  per-rider usage counter under a fixed limit (6) — a genuine "only comment on this
  a limited number of times per race" reaction.
- **`Commentary_TryTriggerFlagReaction`** (was `FUN_001257d0`) and
  **`Commentary_TryTriggerGenericReaction`** (was `FUN_00125aa0`) — simpler
  probability-gated reaction triggers, same shape as the team-trick dispatcher.

Plus their 6 wrapper targets (`Commentary_QueueFlagReactionEvent`,
`Commentary_QueueRiderEventFlagReaction`, `Commentary_QueueRiderEventTierReaction`,
`Commentary_QueueLimitedReactionEvent`, `Commentary_QueueGenericReactionEvent`,
`Commentary_QueueProximityReactionEvent`).

11 renames this batch. Roughly 20 wrapper callers remain unread — smaller,
lower-value variants of the same pattern now that the main trick/overtake/proximity
triggers are all mapped.

## The entire `Commentary_QueueEvent` wrapper cluster is now closed (immediate follow-up)

Swept the remaining ~20 wrapper callers to completion — every one of the original
~29 `Commentary_QueueEvent` callers found via `xrefs_to` is now named. This final
batch turned out to be a coherent "solo/single-rider commentary" sub-cluster (no
second-rider parameter, unlike the overtake/proximity/team-relationship functions):

- **`Commentary_TryTriggerGenericFillerReaction`** / **`Commentary_TryTriggerSpecificOrFillerReaction`**
  — both first check whether the currently-active speech line already matches this
  rider's character (or a special line type `0xc`) via `SpeechManager_StopLine`'s
  own lookup chain, stopping it if so, before queuing a new one. The "generic"
  variant randomly coin-flips between two filler lines tagged with a fixed
  "announcer" bitmask (`0x40`, distinct from any real character ID) — genuine
  no-specific-content filler commentary.
- **`Commentary_TryTriggerSpeedThresholdReaction`** / **`Commentary_TryTriggerTimeGapReaction`**
  — gated on a rider stat crossing a threshold, or a float parameter (likely a race
  time gap) crossing a different threshold, each picking between 2 phrasing
  variants.
- **`Commentary_TryTriggerSoloReaction`** / **`Commentary_TryTriggerSimpleReaction`** /
  **`Commentary_TryTriggerMiscReaction`** — simpler, single-fixed-line probability
  triggers, same shape as earlier-resolved ones.
- **`Commentary_DispatchValueBasedReaction`** — rolls two *independent* probability
  checks: one for a value-computed reaction, one gated specifically on ShowoffMode
  for a separate reaction.
- **`Commentary_DispatchRelationshipTierReaction`** — **the direct caller of the
  already-named `Commentary_QueueRivalryEvent`**. Calls
  `AggressionManager_GetRelationshipField3` directly and maps the tier (0/1/2) to
  a parameter before queuing the rivalry line — **independent confirmation** (a
  second, separate code path) of the `AggressionManager` ↔ grudge-commentary
  connection already proven via `Rider_CheckGrudgeQualifiesForReaction`.

21 renames this batch, closing the cluster completely. **This whole
`Commentary_QueueEvent` investigation (started while resolving
`Commentary_TryTriggerRivalryReaction` several batches ago) is now finished**: the
queue manager itself, every wrapper caller, the trick-landed/team/overtake/proximity/
solo dispatch paths, and the tie-ins to `AggressionManager`, `SpeechManager`, and
`TrickCombo` are all mapped end to end.

## RESOLVED (2026-07-22, Tier-1 push): the `rider+0x5720` "second score field" was a phantom

The last open item in this file's orbit — who writes `rider+0x5720`, the
"final/session score" read at race finish for medal determination, never
found written anywhere across multiple exhaustive static searches — is now
**closed: the field does not exist.**

The key was a fresh `/search_bytes` for the *solved* score field's
displacement (`0x5710`), which turned up two references in a
previously-unbounded function sitting between `FUN_0002d800` and
`Race_ComputeRankings` the whole time. Bounding it (now
**`RaceOutcome_EvaluateAndBeginPostRace`**) produced a Rosetta stone: it
performs the **identical** GameMode-branched medal/placement logic as
`RiderEvent_RaceFinishSequence`'s sub-state 2 — but on a **raw rider
pointer**:

| Field | raw-rider frame (`RaceOutcome_...`) | component frame (`RiderEvent_RaceFinishSequence`) |
|---|---|---|
| medal value (modes 3/5) | `rider+0x5710` | `rebase+0x5720+this` |
| placement/rank (mode 7, default) | `rider+0x140` | `rebase+0x150+this` |
| race time (mode 7) | `rider+0x448` | `rebase+0x458+this` |

Same `MedalTier_ResolveFromValue(value, DAT_001dec90)` call, same
`DAT_001deca0` time threshold, same `(DAT_001de8fc+1)/2` top-half-finish
test — and **all three field pairs offset by exactly `+0x10`**. Three
independently-aligned pairs plus identical branch structure is conclusive:
the RiderEvent component's rebase frame sits `0x10` off the rider frame,
so the lone "`rider+0x5720`" read **is `rider+0x5710`** — the live trick
score whose writer this file already documents
(`TrickCombo_ScoreRailCompletion`/`ScoreAirCompletion` via
`TrickCombo+0xe0`). There is no second score field, no missing writer, and
no dynamic analysis needed. (It also means the earlier "ruled out
`TrickCombo+0xf0`" check was correct for the right reason — that really is
the landing-count histogram — the error was upstream, in taking the
component-frame displacement at face value as a rider offset.)

Side profits from the same pass, all named:

- **`RaceOutcome_EvaluateAndBeginPostRace`** — the race-end outcome
  committer: rivalry-outcome code (scans the rider-pair matrix for the
  focused player's strongest rivalry result), medal/placement tier, then
  **per-track personal-best recording** and the venue transition into
  `"PreLoad"`.
- **`Team_RecordBestRaceTimeIfBetter`** / **`Team_RecordBestShowoffScoreIfBetter`**
  — the career personal-best tables on the character/team object
  (`+0x22c`/`+0x260`, per-track `0x7e`-stride slots; lower-time-wins with a
  `-1` sentinel / higher-score-wins).
- **`InGameState_FindLeadingViewportPlayer`** — current-leader pick per
  viewport (max score in modes 3/5, min time otherwise).
- **`Race_ComputeRiderStandingsMetric`** — the standings comparator
  (score, or `0x7fffffff - time`, or a `TrickCombo+0xac`-minus-`+0xb0`
  metric for one mode — those two sub-fields not individually identified).

Bonus clarity: `rider+0x140` = final placement/rank and `rider+0x448` =
race time are now confirmed in the raw rider frame, and their
component-frame twins (`0x150`/`0x458`) explain those offsets everywhere
they appear in RiderEvent handlers. 6 renames total this pass (1 needed
`/create_function`).

## Ported: `port/src/game/trickcombo.{h,cpp}` (M3b)

Transcribed from the live decompiles, with the constants read out of the binary
rather than taken from prose:

| Constant | Value | Source |
|---|---|---|
| global score scale | **0.678654** | `DAT_0018a0d0` |
| big-air threshold | **4.0 s** | `DAT_001878c4` |
| big-air rate | **1000 / whole second** | literal in `ScoreAirCompletion` |
| frame time | 1/60 | `DAT_00187558` |

### The complete air-scoring formula
```c
points  = computeTrickPoints(difficulty, multiplier) / (repeats + 1);   // anti-farming
if (gate) { if (streak > 1) points += streakBonus(streak); streak = 0; }
airSec  = airFrames * (1/60);
if (airSec >= 4.0) points += ftol(airSec) * 1000;                       // big air
total  += points;
```
with `computeTrickPoints = ftol(difficulty * multiplier * 0.678654)` floored to
a multiple of 10.

### Two corrections to this file's own prose
1. The earlier text said `ComputeTrickPoints` returns 0 "below a tiny epsilon".
   The code's guard is `< DAT_001a9f34`, and **`DAT_001a9f34` is exactly 0.0** —
   so it is a plain negative check, not an epsilon.
2. `CountRecentRepeats`'s skip conditions are described in the decompile as
   comparing against `'e'` and `','`. Those are **byte values 0x65 and 0x2c in
   the packed trick record**, not characters — Ghidra types the bytes as `char`.
   The port comments this so nobody "fixes" it into a string comparison.

Also worth noting: `ComputeTrickPoints` calls `CRT_ftol_TruncateToInt64` — the
helper this project had misnamed `CRT_RoundFloatToInt64` until it was corrected
earlier in this same session. Using rounding here would shift the score at every
10-point boundary.

### Verified in `asset_test`
```
[trck] points + streak tables: 0 checks failed
[trck] same trick x3: 6780 -> 3390 -> 2260 (expect halving then thirding)
[trck] big air: 3.0s -> 0,  4.5s -> 4000 (expect 0 and 4000)
```
The repeat divisor is exactly `/1, /2, /3`, and the hand-computed point values
(670 / 880 / 6780 for difficulty 1000 x1.0, 1000 x1.3, 10000 x1.0) match — an
independent check on both the 0.678654 scale and the floor-to-10 behaviour.

## The DIFFICULTY model, decoded (retires the port's biggest scoring stand-in)

`this+0x14` — the "accumulated difficulty" the whole point formula multiplies —
is fed by **three** accumulators, now found and named:

| Function | Was | Adds | Gate |
|---|---|---|---|
| `TrickCombo_AccumulateSpinDifficulty` | `FUN_0005dd60` | **0.0699543** (`DAT_0018a0c4`) | every new **180 deg** of spin |
| `TrickCombo_AccumulateFlipDifficulty` | `FUN_0005ddc0` | **0.2499898** (`DAT_0018a0c0`) | every new full **360 deg** of flip |
| `TrickCombo_AccumulateElementDifficulty` | `FUN_0005d220` | **(count+1) x 0.0424975** (`DAT_0018a0a8`) | count = active elements at `this+0x50` (0..3) |

plus two flat bonuses inside `ScoreAirCompletion` itself:
`+0.1999773` when `this+0x10 != 0`, and `+0.1800110` when its `param_2 != 0`.

### The spin/flip asymmetry
Both accumulators share the same angle-snapping preamble (`0xb4` = 180), but
differ in the credit gate:
```c
// spin (0x0005dd60)
if (a > this[0x38]) { this[0x38] = a; this[0x14] += 0.0699543f; }

// flip (0x0005ddc0)
if (a > this[0x3c] && a % 0x168 == 0) { this[0x3c] = a; this[0x14] += 0.2499898f; }
```
`0x168` = 360. So **spin credits every 180, flip only every full 360** — exactly
the "spin-count-in-180-units, flip-count-in-360-units" the earlier
`EncodeTrickRecord` note inferred, now confirmed from the accumulators.

Each also tracks a *high-water mark* (`+0x38` / `+0x3c`), so rotating back and
forth cannot farm difficulty — only exceeding your best so far pays.

**A flip is worth 3.57x a spin** per credited unit (0.24999 / 0.069954), which is
what makes flips the high-value trick.

### An asymmetric rounding boundary worth preserving
The snap is `rem < 0x5b`, i.e. **`< 91`** — so a remainder of exactly **90
rounds DOWN**, and 91 rounds up. Not the mathematically-nearest rule. The port
reproduces this, and the self-test asserts the engine's answers
(`snapTo180(90) == 0`, `snapTo180(270) == 180`) rather than the intuitive ones —
an initial version of that test asserted the "nearest" values and failed, which
is how the asymmetry was noticed.

### Ported and verified
```
[trck] spin 180/360/540/360 -> 0.2099 (want 0.2099)
[trck] flip 180/360/540/720 -> 0.5000 (want 0.5000)
[trck] flip360 / spin180 ratio = 3.57
[trck] difficulty model: 0 checks failed
```
The repeated 360 spin and the 540 flip both correctly score **nothing** — the
high-water-mark and 360-gate rules working.
