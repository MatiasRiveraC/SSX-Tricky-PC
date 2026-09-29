# RE notes: `LessonMan` — the in-game tutorial (Lesson mode) system

Picked as a continuation of the "map the untouched `InGameState_LoadLevel`
tagged objects" direction (2026-07-20), right after `RE_NOTES_weather_effects.md`'s
`SnowFallMan`. `LessonMan` (`0x52c`/1324 bytes) is only constructed when
`GameMode_Current == 6` (the tutorial/lesson mode) — genuinely fresh, previously
unexplored territory, and turned out to be a real, substantial tutorial-lesson
step state machine with its own screen overlay.

## Architecture

`LessonMan_Construct` (was `FUN_00058d40`) calls the same `NodeRegistry_Insert`
wrapper (`FUN_000aa940`) `PREAI`/`PostAI`/`SnowFallMan` use — passed literal type
**`6`**. Sets up an 11-slot vtable (`0x00189e10`) that shares several slots
(including slot 7, the generic `NodeBase` Update slot) with `SnowFallMan`'s own
`Node_NoOpStub2` — same "real logic lives outside the generic dispatch slot"
shape as that system.

**Construction itself is rich**: loads a `"LessonBuffer"` scratch buffer
(`0x4d598` bytes), loads **30 tutorial button-icon textures** (6 groups of 5, via
a `"data/textures/tb_%s_%d_%s"`-shaped format string) plus one
`"data/tutorial/%s_big"` image, then reads the *actual current controller
layout* (via a cluster of ~15 paired getter functions) and registers each
button/analog-stick's on-screen icon state via
**`LessonMan_RegisterButtonIconStates`** (was `FUN_000a7f40`) — keyed by the
same normalized-gamepad bitmask values (`0x400`/`0x2000`/`0x200`/etc.) already
found in the input pipeline (`RE_NOTES_control_scheme.md`). This is the
tutorial's live "press this button" overlay setup, built directly from the
player's real control scheme rather than a fixed image.

- **`LessonMan_TickStepStateMachine`** (vtable slot 1, was `FUN_000566c0`,
  found in completely un-analyzed code) — the real per-frame tick, gated by 2
  pause/loading-style checks (same shape as `HUD_TickRiderDisplayState`'s own
  `FUN_000ca120` gate). A genuine **lesson-step state machine**: each step has
  its own small enter/exit/tick vtable (`this+0x48`/`+0x4c`), and when the
  target step index (`this+0xc`) differs from the current one (`this+0x14`), it
  calls the old step's exit then the new step's enter before ticking every
  frame regardless. This is the real tutorial-lesson progression driver — each
  numbered "lesson" (analog turning, carving, jumping, grabbing, etc.) is
  presumably one step in this table.
- **`LessonMan_RenderOverlay`** (vtable slot 2, was `FUN_00058800`, also found
  in completely un-analyzed code) — sets up a fullscreen 2D-overlay-shaped
  `GfxContext` viewport/render-state sequence (644×480, matching this project's
  known screen-space HUD convention), with a special-case extra draw call when
  the current lesson step is 5 or 7.
- **`LessonMan_ScalarDeletingDestructor`** (slot 0) — standard destructor pair.

## Honest status — same pattern as `SnowFallMan`

Checked `xrefs_to` for both `LessonMan_TickStepStateMachine` and
`LessonMan_RenderOverlay`: only their own vtable-slot data references, no static
code callers found. This is now the **second confirmed instance** of the same
architectural pattern this session found with `SnowFallMan`: a `NodeBase`-derived
`InGameState` subsystem whose real per-frame logic lives in custom vtable slots
outside the generic `NodeRegistry` Update dispatch, invoked some other way not
yet traced. Given both are gated on specific conditions (`SnowFallMan` on a
level's snow flag, `LessonMan` on `GameMode_Current==6`), a plausible shared
mechanism is a generic "for each optional subsystem InGameState owns, tick+render
if present" loop somewhere in `InGameState`'s own per-frame code that this
project hasn't located yet — worth checking directly if this thread is picked up
again, rather than re-deriving the same "no xref found" result per subsystem.

5 renames.

## Update (2026-07-21, "go fresh" pass): `trickdef.dat` and the on-screen trick prompt

Picked another genuinely fresh direction: `Game Data\data\tutorial\trickdef.dat`
(10080 bytes), never previously examined. Searched for the literal path string
`"data/tutorial/trickdef.dat"` and traced its one reader forward through the
entire on-screen trick-instruction display chain. Unlike `SnowFallMan`/`LessonMan`'s
own per-frame callers, **this whole chain is well-anchored**: confirmed connected
to `LessonMan_Construct` directly (not speculative), and the file's own loader is
called from the already-documented `InGameState_LoadLevel`.

- **`TrickDef_LoadFile`** (was `FUN_0005c900`) — loads the raw file into a fixed
  global buffer. Called from `InGameState_LoadLevel`.
- **`TrickDef_GetRecordByIndex`** (was `FUN_0005c930`) — a trivial 2D accessor
  that reveals the exact table shape: **30 columns × 12 rows of 28-byte records**
  (10080 = 30×12×28). Row is almost certainly a trick-category/stance axis,
  column an individual directional-input/button-combo slot — matching SSX
  Tricky's control scheme (each direction+button combo maps to a specific trick).
- **`TrickDef_FormatRotationLabel`** (was `FUN_0005c980`) — builds a compact
  display string from a record's 2 signed 16-bit rotation-count fields (e.g. a
  `"540 backside + 180 off-axis"`-style compact label with single-letter suffix
  codes not individually decoded).
- **`TutorialHUD_DrawTrickPrompt`** (was `FUN_0005cd30`, needed `/create_function`)
  — the on-screen "do this stick+button combo" prompt renderer: looks up the
  current trick via `TrickDef_GetRecordByIndex`, formats it via
  `TrickDef_FormatRotationLabel`, then draws the result character by character,
  substituting special characters for directional icon glyphs (via a 12-entry
  icon table) and drawing the rest as text via the already-named
  `HUD_DrawNumberBufferShadowed`.
- **`TutorialHUD_DrawTrickPromptIcon`** (was `FUN_0005ceb0`, needed
  `/create_function`) — draws one directional icon glyph via the already-named
  `IconAtlas_GetEntry`/`Sprite_DrawAligned`, using the same icon table.
- **`TutorialHUD_MeasureTrickPromptWidth`** (was `FUN_0005cf40`, needed
  `/create_function`) — the measurement companion to the draw function (same
  character walk, accumulates width instead of drawing) — used to center the
  prompt before the real draw pass.
- **`TutorialHUD_ConstructTrickPromptContext`** (was `FUN_0005ce80`, needed
  `/create_function`) — trivial constructor (vtable + 3 default float fields).
  **Called directly from `LessonMan_Construct`** — the confirmed tie-in anchoring
  this whole cluster to the tutorial-lesson system documented above.

7 renames (4 needed `/create_function`, including one self-caught off-by-one
recreation along the way, same bisection technique used throughout this
project). This closes out `trickdef.dat` as a genuinely understood, well-anchored
subsystem — not merely a guessed connection.

### Fully closed the trick-prompt widget's own vtable (immediate follow-up)

Read the widget's small vtable (`0x0018a080`, confirmed installed by
`TutorialHUD_ConstructTrickPromptContext`) end to end — all 6 slots now named or
accounted for:

- Slot 0 — **`TutorialHUD_TrickPromptScalarDeletingDestructor`** (was `FUN_00055f40`,
  needed `/create_function`) — standard destructor shape, swaps to a shared base
  vtable.
- Slot 1 — the already-named shared `Node_NoOpStub1`.
- Slot 2 — **`TutorialHUD_SetCurrentTrick`** (was `FUN_0005c950`, needed
  `/create_function`) — stores the target trick's row/column index (the exact
  fields `TutorialHUD_DrawTrickPrompt` reads), then resolves an extra related
  value via a further vtable call. The "select which trick to prompt for" setter.
- Slot 3 — `TutorialHUD_DrawTrickPromptIcon`.
- Slot 4 — `TutorialHUD_MeasureTrickPromptWidth`.
- Slot 5 — `TutorialHUD_DrawTrickPrompt`.

**This is now a genuinely complete, fully-mapped small widget class.** The
`trickdef.dat`/tutorial-prompt thread is exhausted end to end: data file → loader
→ table accessor → label formatter → the widget class that displays it, every
function and every vtable slot named. 2 more renames (7 + 2 = 9 total for this
whole thread).

## Update (2026-07-22): decoded the `.inp` lesson-playback file format

Fresh direction: `Game Data\data\tutorial\<char>.big` archives each contain 30
files (`psym01.inp`-`psym30.inp` for the `psym` character, etc.), every one
exactly **316824 bytes** uncompressed. Found the format string
`"|data\tutorial\%s%02d.inp"` at `0x001aa678` and traced its 3 xrefs
(`0x00057d1b`, `0x0005818a`, `0x00058335` — all in completely unbounded code)
forward, walking one `/create_function` boundary at a time per the safe
methodology in `feedback_safe_function_boundary_creation`.

**Confirmed this is all inside `LessonMan` itself** — not a guess: every new
function's `this` reads/writes `this+0x4fc` (dword index `0x13f`) as a
character-roster index into `PTR_DAT_001b53f8`, the *exact same field offset*
`LessonMan_Construct` uses for its own `"data/textures/tb_%s_%d_%s"` path
(`param_1[0x13f]`). Also finally resolved `LessonMan_TickStepStateMachine`'s
previously-undocumented `this+0x10` field: it's a queued "request transition to
step N" value, applied once per tick by `FUN_00055f90` (not renamed — a
generic, self-contained step-transition applier with no distinctive role), which
looks up each step's enter/exit/tick function-pointer triple from a fixed global
table at `0x00189d8c + 0xc*(step-1)`. Read that whole table directly via
`/read_bytes` (non-mutating) to map all 11 steps' addresses before bounding only
the ones on the direct path to the `.inp` loader (steps 2-5); **steps 6-11 are
now known addresses but deliberately left unbounded/unnamed** — a well-anchored
follow-up, not a dead end (see "Still open" below).

- **`LessonMan_Step2Tick_AdvanceToStep3`** (was `FUN_00057b90`) — step 2's tick,
  trivial: unconditionally requests step 3 every frame it runs. A one-frame
  pass-through, not a real state of its own.
- **`LessonMan_Step3Tick_AdvanceToStep4WhenReady`** (was `FUN_00057ba0`) —
  step 3's tick: calls a generic, multi-owner flag-check helper (`FUN_00078390`,
  also called by `RiderEvent_UpdateEndRaceFadeSequence` and
  `VenueStaging_TickPhaseSequence`/`TickIndexedPhase` — a shared utility, not
  LessonMan-exclusive, left unnamed), requests step 4 once it reports done.
- **`LessonMan_Step4Enter_InitMenu`** (was `FUN_00057bd0`) — step 4's enter:
  refreshes menu text, sets a per-step duration field (`this+0x528=436.0f`;
  step 6's enter sets the same field to `240.0f` — a recurring per-step
  timer/duration pattern).
- **`LessonMan_RefreshLessonMenuText`** (was `FUN_00057aa0`) — dispatches on
  `this+0x9c` (menu page 0/1/2), resolves localized labels (ids
  `0x228`/`0x18e`/`0x18a`/`0x18d`/`0x183`) per page.
- **`LessonMan_InitLessonTypeSubmenu`** (was `FUN_00057a30`) — sets up a 3-item
  selectable submenu (`this+0x58=0` index, `this+0x5c=3` count) + 3 localized
  labels (ids `0x227`/`0x186`/`0x183`). Called twice from step 7's enter
  (`0x582d0`-`0x583d0`, found but not yet bounded/named) — the "(re-)enter the
  lesson-type-select menu" setup.
- **`LessonMan_HandleMenuNavigationInput`** (was `FUN_00056180`) — edge-triggered
  input read; while on the lesson-type page (`this+0x9c==2`), adjusts the
  selected index `this+0x58` up/down bounded by `this+0x5c-1`. Returns the raw
  button bitmask for callers to test further bits themselves.
- **`LessonMan_Step4Tick_HandleLessonMenu`** (was `FUN_00057bf0`) — step 4's
  tick, **the function this whole investigation was aimed at**. Reads input via
  the navigation helper above, dispatches on `this+0x9c`. On the confirm page
  with a difficulty/type choice (`this+0x58` 0/1/2), builds the exact
  `"|data\tutorial\%s%02d.inp"` path (character code from
  `PTR_DAT_001b53f8[this+0x4fc*0xe]`, lesson number from `this+0x4d4+1`) and
  requests step 5/7/8 respectively; choice 3 instead shows a localized
  "already mastered" message (ids `0x17d`/`0x1d1`/`0x140`) without transitioning.
- **`LessonMan_Step5Enter_LoadLessonInpFile`** (was `FUN_000580f0`) — **the
  actual file loader**. Builds the identical path and calls
  `FILE_loadpackat(path, this+0x458, 0x4d598)`. `0x4d598` = 316824 decimal —
  byte-exact the size of every real extracted `.inp` file **and** the exact
  size of the `"LessonBuffer"` scratch allocation from `LessonMan_Construct`
  (`this+0x458` is literally that buffer's stored pointer). Confirms the whole
  file is read raw/uncompressed straight into that fixed buffer. Zeroes the
  buffer's first dword after load (live "current frame" position counter,
  distinct from the file's own on-disk header value there).
- **`LessonMan_Step5Tick_CheckPlaybackComplete`** (was `FUN_00058210`) —
  compares buffer dword[1] (total frame count from the file header — 1042 for
  `psym01.inp`) against dword[0] (live position, zeroed on load); once
  position >= total, requests step 6. Does **not** itself advance the position
  counter or read per-frame records — that consumer is a separate, not-yet-
  located function (likely tied to `LessonMan_RenderOverlay` or `InGameState`'s
  own per-frame update). Left open.

**The `.inp` format itself, byte-verified against `psym01.inp`**: a fixed
316824-byte buffer of which only a leading portion is real content — **52-byte
header** (dword0/1 = live-position/total-frame-count, both `1042` on disk before
the live copy gets zeroed at load) + **1042 × 44-byte records** (4 leading
floats, constant `-0.0563/-0.0256/0.1763/0.2222` in literally every record
checked — reads as a fixed per-record template/marker, not varying motion data
— followed by 7 words that DO vary sparsely: `8193/2048/2048` near the start,
`6/64/32/32` around frame 500, all-zero elsewhere) + zero padding out to the
fixed 316824-byte capacity (exact accounting: `52+1042*44=45900` bytes used,
remaining 270924 bytes verified all-zero). The sparse, mostly-zero varying
fields read as a **scripted-event/cue timeline** (e.g. trick-prompt or
voice-line trigger ids) rather than continuous per-frame position/rotation
capture — consistent with a tutorial "watch the game perform this trick"
demo rather than a full physics ghost-replay. Exact per-word semantics not
yet decoded; would need the per-frame consumer function (not yet found) to
confirm. First divisor found by brute-force header-size search in Python
against the real file, then verified structurally (record-boundary alignment,
tail all-zero-past-`used`) rather than trusted on arithmetic alone.

9 renames this update (57 total across this file's threads).

### Immediate follow-up (same session): finished steps 6-10

Went back and bounded/decompiled every remaining step 6-10 address found via
the step table above (step 11 still open — see below). This closed out the
whole `LessonMan` step-machine picture and, along the way, **caught and fixed
a mistake from the update above**: `LessonMan_InitLessonTypeSubmenu`'s caller
was originally attributed to "step 7's enter" based only on an xref-address
range before step 7 was actually bounded. Now that step 9 is bounded and
decompiled, the real confirmed call site (`0x5856d`) is inside
**`LessonMan_Step9Enter_ResetAndShowMenu`**'s body — not step 7's. Fixed the
rename comment and this note accordingly (the second xref, `0x5861d`, falls
just past step 9's body end, in a still-unbounded neighbor — not chased
further this pass).

- **`LessonMan_Step7Enter_LoadLessonInpFile`** (was `FUN_000582d0`) — step 7's
  enter, a near-twin of `LessonMan_Step5Enter_LoadLessonInpFile` (same path
  build, same `FILE_loadpackat` call, same buffer-zero/SFX-stop), plus one
  extra conditional call to `FUN_000bc170` (a large ~0x1028-dword object
  reset, not renamed — too broad to characterize confidently, but plausibly a
  rider/physics-state reset after a demo) gated on a global flag. Reached from
  `LessonMan_Step4Tick_HandleLessonMenu`'s 2nd difficulty choice.
- **`LessonMan_Step7Tick_AdvanceToStep10WhenDone`** (was `FUN_000583d0`) —
  step 7's tick, same shape as step 5's tick (checks the shared `this+0x54`
  done flag) but requests step 10 instead of step 6 — confirms the 2
  difficulty paths (step5→6, step7→10) lead to different post-lesson steps.
- **`LessonMan_Step8Enter_SelectRandomCommentaryLine`** (was `FUN_000583f0`)
  — step 8's enter, a genuinely different path: no file load at all, just
  picks one of 3 fixed ids (`0x23c`/`0x23e`/`300`) via `RNG_NextGlobalUInt32`
  into `this+0x98`. Reached from the menu's 3rd difficulty/type choice.
- **`LessonMan_Step8Tick_AdvanceToStep9WhenDone`** (was `FUN_000584a0`) —
  checks the shared done flag (→step 9); otherwise checks a string via a
  trivial, unnamed accessor (`FUN_0005dbe0`, ambiguous receiver) and sets
  `this+0x64=1` if non-empty — reads as a "commentary subtitle currently
  showing" flag matching step 8's role.
- **`LessonMan_Step9Enter_ResetAndShowMenu`** (was `FUN_000584f0`) — the
  confirmed real caller of `LessonMan_InitLessonTypeSubmenu` (see correction
  above); also sets the per-step duration field (`this+0x528=240.0f`, same as
  step 6) and conditionally calls the same `FUN_000bc170` reset step 7 does.
- **`LessonMan_Step10Enter_ResetAndShowMenu`** (was `FUN_000585a0`) —
  near-identical to step 9's enter (same menu-reinit call) but a different
  duration constant (`331.0f`) and without the conditional reset call.
  Reached from step 7's tick once its playback completes.
- **`LessonMan_Step6Tick_HandlePostLessonMenu`** (was `FUN_00058a40`) — step
  6's tick: reads input via `LessonMan_HandleMenuNavigationInput`, dispatches
  on `this+0x58` (the same selected-index field the type-select submenu
  uses) — a genuine interactive post-lesson menu reusing the main menu's
  input machinery. Choice 0 transitions back to the character-select page via
  an indirect vtable call Ghidra couldn't recover as a jumptable ("Too many
  branches"); choice 1 resets the `.inp` buffer. Named by confirmed overall
  role, not exhaustively traced branch-by-branch given the unrecovered
  jumptable.

7 more renames (57 + 7 = 64 total across this file's threads). **All 11 of
LessonMan's steps now have at least one known address**; steps 2-10 are fully
bounded, only step 11 remains genuinely untouched.

### Immediate follow-up (same session): closed step 11, chased the `FUN_000bc170` lead

- **`LessonMan_Step11Tick_SignalLessonComplete`** (was `FUN_00056380`) —
  step 11's tick, a genuinely tricky boundary case: its vtable-referenced
  address points 2 bytes into what read, linearly from the previous
  function, as a 10-byte NOP padding run. Trusted the actual vtable
  reference over that boundary heuristic (a stronger signal than dead-
  reckoning from padding) and created the function directly there — clean
  result, 2 literal leading NOP instructions followed by real code. Sets a
  flag on the same global object LessonMan's other steps touch
  (`*(DAT_001e3c7c+0x72c)+0x50=1`), stops the same 2 SFX channels, requests
  no further transition — a terminal state, read as "signal the lesson
  attempt is fully finished." **This closes LessonMan's entire step
  machine: all 11 steps now bounded and named.**
- **`ReplayManager_ClearRecordedFrames`** (was `FUN_000bc170`) — chased the
  large reset function steps 7/9 call conditionally. `xrefs_to` revealed
  it's also called from the already-named `ReplayManager_ResetState`
  (which allocates a `"Frame Block"`-tagged buffer, then hands off here for
  the rest of the reset) — a genuine, unexpected connection between
  `LessonMan`'s `.inp` playback and the game's separate, already-extensively-
  documented race-replay/ghost-camera system. **Also found 2 more callers**
  (`FUN_000ac550`/`FUN_000ac610`, a near-duplicate pair of generic
  venue-exit handlers, not renamed) that check the *exact same*
  `*(DAT_001e3c7c+0x72c)+0x40` flag LessonMan's steps check before calling
  this function — confirms `+0x40` is a shared "replay/lesson recording was
  active" flag on that global object, used consistently across at least 3
  independent call sites. Body: clears a linked list, resets a 4-slot array
  of `0xe00`-byte buffers (structurally similar in *shape* to
  AggressionManager's own 4-slot `0xe24`-byte record array — a different
  object, not the same one, just a similar architecture), rebuilds a
  circular free-list. **Important negative result**: despite this
  connection, `ReplayManager`'s own already-named playback tick functions
  (`ReplayManager_TickPlaybackAdvance`, `ReplayManager_TickActiveSnapshotPlayback`)
  operate on ReplayManager's own racer/snapshot/camera system (race-end
  instant replays) and do NOT appear to consume LessonMan's `.inp` buffer —
  the two systems share a reset-utility function, not a playback pipeline.
  **The `.inp` per-frame consumer is still a genuinely separate, unlocated
  function** — this thread doesn't resolve it, just rules out one plausible
  shortcut (ruling out is still progress, not a dead end).

2 more renames (66 total across this file's threads; 18 for the
`.inp`/`LessonMan`-step thread specifically).

### Immediate follow-up (same session): FOUND the `.inp` per-frame consumer

The one genuinely open thread from this whole investigation — resolved by
going back to the render path exactly as flagged above.
`LessonMan_RenderOverlay` (already named, see the original architecture
section at the top of this file) makes one special-case extra draw call
when the current step (`this+0xc`) is 5 or 7 — **the exact same 2 steps
that load and play a `.inp` file.** That extra call is the consumer.

- **`LessonMan_DrawRecordedButtonPrompt`** (was `FUN_00056760`) — reads an
  11-dword (44-byte) window from the loaded `.inp` buffer, offset
  `pos*0xb-5` dwords from the buffer base (`pos` = the buffer's live
  position counter, dword0), and matches individual bits of that window
  against the **same button-icon lookup table** `LessonMan_Construct`
  populates from the player's real controller layout (the tutorial's
  "press this button" overlay setup documented at the top of this file).
  Resolves multiple candidate matches down to one icon, then draws it via
  `IconAtlas_GetEntry`/`Sprite_DrawAligned` — the identical pattern already
  used by `TutorialHUD_DrawTrickPromptIcon`. **This confirms the `.inp`
  file's sparse per-record varying fields are literally recorded
  controller button-bitmask data** — matching the `.inp` = "input" filename
  convention exactly — not a generic event/cue timeline as originally
  hedged in the format-decode update above. The lesson demo plays back a
  recorded run and draws a live "press this button" prompt synced to it:
  the actual mechanism behind SSX Tricky's "watch and repeat" tutorial
  lessons. Gated on the live position exceeding a fixed 120-frame
  threshold (a warm-up/intro grace period) and a separate display-mode
  check.

This closes the loop on the `.inp` format's semantics (recorded input, not
generic events) but does **not** fully resolve the position-counter
question — whatever increments `pos` (dword0) each frame is still not
located. That's now a narrower, better-scoped remaining gap rather than the
open-ended "find the whole consumer" search it was before.

1 more rename (19 total for the whole `.inp`/`LessonMan`-step thread this
session).

### Still open

- ~~**What increments the `.inp` buffer's live position counter (dword0)**~~
  **RESOLVED (2026-07-22, Tier-3 sweep)** — found via a targeted
  `/search_bytes` for the `this+0x458` displacement within the LessonMan
  code range: **`LessonMan_AdvanceInpPlaybackFrame`** (was `FUN_00056620`)
  increments dword0 (capped at dword1) during steps 5/7, and
  **`LessonMan_InjectRecordedInputFrame`** (was `FUN_000565b0`) copies 8
  dwords of the current 44-byte record into the live input object via its
  vtable — **the demo rider is driven by replaying recorded controller
  input through the real input pipeline**. Also named
  `LessonMan_RestartLessonPlayback` (rewind + re-dispatch) and
  `LessonMan_DestructorFreeBuffers`. The `.inp` loop is now closed end to
  end: load → inject → advance → prompt-draw → completion check.
- **Exact bit-to-button mapping** — which bit position in the recorded
  dwords corresponds to which physical button, and step 8's `this+0x98`
  "commentary line id" field's exact meaning.
- **`FUN_00078390`** (the 3-flag "all complete" check) and **`FUN_0005dbe0`**
  (step 8's string accessor) — both read this pass but deliberately left
  unnamed: the former is shared across 3 unrelated systems (no exclusive
  owner to name it after), the latter's receiver object is ambiguous.
- **`FUN_000ac550`/`FUN_000ac610`** — the near-duplicate generic venue-exit
  handlers found via `ReplayManager_ClearRecordedFrames`'s xrefs. Confirmed
  to share the `+0x40` flag semantics but not otherwise characterized or
  named — outside this thread's scope, a candidate for a `VenueStaging`-
  focused pass.
