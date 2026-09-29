# RE notes: the Application boot sequence and top-level state machine

Found by chasing `InputManager`'s owner as far as possible (see
`RE_NOTES_control_scheme.md`) — that chase led all the way up to the actual root of
the game. This is the first time this project has mapped anything above individual
subsystems: the real application object, its one-time boot sequence, and the
top-level Boot→FrontEnd→InGame state machine. **13 renames.**

## The big payoff: `DAT_001e3c7c`'s identity is now confirmed

`DAT_001e3c7c` has been referenced constantly throughout this entire project's notes
as "the resource context pointer" — dozens of fields off it
(`+0x50`=localization, `+0x720`=graphics device, `+0x72c`=world state, etc.) were
inferred from usage but never definitively pinned to a real object. That's now
resolved:

- **`Application_InitSubsystems`** (was `FUN_000a9fe0`) sets
  `*(DAT_001e3c7c+0x720) = <graphics device>` and calls
  `FUN_0014c0f0("data/lang/american.loc", *(DAT_001e3c7c+0x50), ...)` — an explicit,
  literal load of the exact localization file this project's `.loc`-format
  reverse-engineering (`RE_NOTES_race_hud.md`) already decoded, using the exact field
  offset `Localization_ResolveString` reads from. This is about as close to a smoking
  gun as static analysis gets: **`DAT_001e3c7c+0x50` is definitively the loaded
  localization archive pointer.**
- **`InGameState_LoadLevel`** (was `FUN_000ac9b0`) sets
  `*(DAT_001e3c7c+0x72c) = <this InGameState object>` — confirms `+0x72c` is the
  "current world/level state" pointer read everywhere in the HUD and race-lifecycle
  code.
- Also allocates `"shpMngr"` (shape/texture manager, `0x1614` bytes — almost
  certainly the consumer of the `.xsh` texture-sheet files in `Game Data\data\
  textures\`, the format this project attempted and set aside earlier) and
  `"Transition Man"` (transition manager).

## The chain, top to bottom

**Fully corrected and closed (2026-07-21, "make sure not mistake" pass)** — see the
"Traced `Application_ConstructAndInitInput`'s one caller" entry further down for how
this was found, and the corrected `ssx_auto_rename.py` entries for the full live
verification of each step:

0. **`XAPILIB::mainXapiStartup`** (real program entry point, found at `0x001541a9`)
   — `XapiInitProcess` → TLS setup → CRT init → `Application_ConstructAndInitInput(0,0,0)`
   → `XapiBootToDash(1,1,0)` (return to Xbox dashboard, i.e. the whole game session
   has ended by this point).
1. **`Application_ConstructAndInitInput`** (was `FUN_000ad7d0`) — allocates the
   `"Application"`-tagged buffer (`0x7c0`/1984 bytes), calls `Application_Construct`,
   then **`Application_RunAndShutdown`** (corrected name — was misnamed
   `InputManager_InitOrReset`; its `this` is Application itself, proven via
   `param_1+0x2c` = the same XBoxExecutionMan field documented elsewhere, and it
   unconditionally nulls the global `DAT_001e3c7c` on the way out), then calls
   Application's own destructor (vtable slot 0, arg 1) once that returns.
2. **`Application_Construct`** (was `FUN_000a9770`) — sets up the Application
   object's own vtable (`0x0019a200`): slot 0 = destructor, slot 1 =
   `Application_InitSubsystems`, slot 2 = **`Application_Purge`** (newly named,
   tag-confirmed via `"cApplication::Purge\n"` — a comprehensive engine-subsystem
   teardown, previously mislabeled "not individually traced"), slot 3 =
   `Application_StateMachineTick` (corrected slot number — a previous session
   mislabeled it "slot 2"). Also loads `"Title Font"`/`"Menu Font"`.
3. **`Application_InitSubsystems`** (was `FUN_000a9fe0`, vtable slot 1) — the
   one-time subsystem boot described above.
4. **`Application_RunAndShutdown`** calls **`Application_RunMainLoop`** (corrected
   name and characterization — was named `Application_RunInitialLoadPump` and
   described as a bounded one-time loading pump, "not the perpetual per-frame
   loop." That was wrong: its body ends in an unconditional `goto` back to its own
   entry, exiting only via a quit-flag check (`this+0x24`) — **this is genuinely
   the master game loop, running for the player's entire session**. Each iteration
   polls input (`InputManager_PollDevicesIntoCache`) and ticks/replaces the current
   top-level state object (`this+4`) by calling into `Application_StateMachineTick`
   — which is how the two state machines connect: `Application_StateMachineTick`
   is called FROM here, not from some separate per-frame source. Only after this
   loop returns (player quit) does `Application_RunAndShutdown` tear down PadCache/
   XBoxExecutionMan and null `DAT_001e3c7c` — which is also why
   `mainXapiStartup` calling `XapiBootToDash` immediately afterward makes sense
   and isn't a premature exit.
5. **`Application_StateMachineTick`** (was `FUN_000a9b90`, vtable slot 3 — corrected
   from "slot 2") — **the actual top-level app state machine**, driven by a state
   field at `this+0x738`:
   - state 0: early-init, calls into a sub-function not traced further
   - state 1: allocates `"FrontEnd"` (`0x120` bytes) and enters the menu system
   - state 2: **sets `GameMode_Current`, `DAT_001dec90`, and `DAT_001de8fc`** — this
     is the actual initialization site for the game-mode selector referenced
     throughout the entire project (`RE_NOTES_level_script_system.md` and everywhere
     that branches on `GameMode_Current`). Then either returns to `"FrontEnd"` or
     allocates a level-object buffer and calls `InGameState_Construct`.
   - state 3: transitions from 3→2, same level-allocation path as above.
   
   **Confirmed (2026-07-21): called directly from `Application_RunMainLoop`** every
   time the current top-level state object (`this+4`) needs replacing — not just
   "almost certainly every frame" as previously guessed. This IS "the top of the
   main loop," full stop — see item 4 above.

   **Update (2026-07-20, later session): a second, separate per-frame mechanism was
   also found.** Starting from `RE_NOTES_control_scheme.md`'s open "InputCache
   per-frame consumer" question, traced a frame-pump mechanism running ALONGSIDE
   `Application_RunMainLoop`: `Application_ArmFrameTimer` (created at `0x000b29d0`)
   fires the first `XAPILIB::timeSetEvent`, whose callback
   (`Application_FrameTimerCallback_StdcallThunk` at `0x000b26a0`, a
   `__stdcall`-to-`__thiscall` adapter) invokes
   **`Application_FrameTimerCallback`** (was `FUN_000b26b0`) — which calls
   **`Application_TickFrame`** (was `FUN_000aa310`, `this`=`[DAT_001e3c7c]`), computes
   real elapsed time, smooths/clamps the frame delta, then **re-arms
   `timeSetEvent` for the next tick using the same callback**. This is genuinely an
   async, self-rescheduling timer chain. **Correction (2026-07-21)**: this is NOT
   what makes `Application_ConstructAndInitInput`'s "caller" untraceable — that
   was resolved separately by finding the real entry point,
   `XAPILIB::mainXapiStartup` (see item 0 above and the "Traced
   `Application_ConstructAndInitInput`'s one caller" entry further down). The
   relationship between this async timer chain and the synchronous
   `Application_RunMainLoop` found the same day is NOT fully reconciled — both are
   real, both are confirmed live, but whether the timer is a coarser pacing/V-sync
   signal consulted by `Application_RunMainLoop`'s own `DAT_001ba53c` check, or a
   genuinely parallel second update path, is an honest open question, not
   something to guess at further without more evidence. `Application_TickFrame`
   itself does frame-timing bookkeeping, drains input polling
   (`Input_CatchUpPollAndTick` → `InputManager_PollDevicesIntoCache`), runs a small
   state machine, then makes one unconditional final vtable dispatch every tick —
   this final dispatch is confirmed (elsewhere in this project) to be
   `XBoxExecutionMan_SignalFrameEvent` (a Win32 `SetEvent` frame-boundary sync
   primitive), NOT a connection to `Application_StateMachineTick` — that
   speculative link is now superseded by the confirmed direct call from
   `Application_RunMainLoop` instead.

   **The two loops are now fully reconciled (immediate follow-up, same session,
   "keep going harder" pass).** `Application_RunMainLoop`'s own `DAT_001ba53c`
   check (a flag with a fixed value of `1` baked into the retail binary — zero
   writers found anywhere, so its `==0` branches are dead code in this build)
   leads to an unconditional call to `XBoxExecutionMan_WaitForFrameEvent` — a
   genuine `WaitForSingleObject(event, INFINITE)` blocking wait on the exact
   same Win32 event `Application_TickFrame`'s final dispatch signals via
   `XBoxExecutionMan_SignalFrameEvent`. This is the complete, classic
   architecture: the OS multimedia timer paces `Application_TickFrame` at
   ~60fps, which signals the event each tick; `Application_RunMainLoop` does
   its own per-tick work (poll input, tick/transition the state object), then
   blocks on that event until the next timer tick wakes it, rather than
   busy-spinning. Not a loose end anymore — both loops are real, confirmed
   live, and now connected. **Still open**: the writer of the `this+0x24` quit
   flag that ends `Application_RunMainLoop` — checked `DAT_001df3f4` as a
   candidate (it's read/written by `Application_StateMachineTick` itself), but
   that turned out to be a genuinely different, valuable finding: a
   pending-transition-reason bitmask (bit `0x20` = launch the VideoPlayer
   module, bits in `0x3b` = other conditions) gating FrontEnd/InGame/
   VideoPlayer transitions, not a quit flag. `this+0x24`'s writer remains
   unfound.

   **Resolved
   (same session, immediately after first pass)**: `Application_ArmFrameTimer`'s
   `this` is the small `XBoxExecutionMan` class (vtable slot 1), and
   `Application_FrameTimerCallback`'s own `this` is ALSO `XBoxExecutionMan`
   throughout (exit flag, last-tick, accumulator, frame-time-target fields all
   confirmed via raw disassembly) -- it only switches to the separate `Application`
   object (`[DAT_001e3c7c]`) for the single call into `Application_TickFrame`, then
   switches straight back. Two objects, two cleanly separated responsibilities, no
   aliasing needed. Confirmed further by finding `XBoxExecutionMan`'s remaining
   vtable slots: `XBoxExecutionMan_Shutdown` (slot 2), `XBoxExecutionMan_WaitForFrameEvent`
   (slot 3), and **`XBoxExecutionMan_SignalFrameEvent`** (slot 4) -- the latter is
   exactly what `Application_TickFrame`'s mystery final per-tick dispatch calls
   (`Application+0x2c` holds the `XBoxExecutionMan` pointer, set by `Application`'s
   own base-constructor), closing that loop too: every frame ends with a `SetEvent`
   on a Win32 event handle, almost certainly for a secondary thread's frame-sync.
   Also named `XBoxExecutionMan_Construct` (was `FUN_000b2a20`). See
   `RE_NOTES_control_scheme.md` for the full trace. 12 renames total (4 newly-
   created function boundaries).
5. **`InGameState_Construct`** (was `FUN_000ab8f0`) — trivial constructor, sets up a
   7-slot vtable at `0x0019a4fc`.
6. **`InGameState_LoadLevel`** (was `FUN_000ac9b0`, vtable slot 1, 2738 bytes) — the
   real level/race entry sequence: sets the global world-state pointer (see above),
   allocates `"AIWorld"` (`0x51c` bytes), runs a phased setup counter
   (`DAT_001e3c7c+0xc`, 0→1→2). One-time, not per-frame, despite its size.
   **Re-swept (2026-07-21)**: this enumeration predates several later finds this
   session that are ALSO called directly from here — `TrickDef_LoadFile`
   (`RE_NOTES_tutorial_system.md`), `AudioSystem_LoadRaceConfigs`
   (`RE_NOTES_audio_system.md`), and the newly-found **`Font_LoadAndParse`**
   (called twice with `"data/fonts/title.ffn"`/`"data/fonts/menu.ffn"` — see below).
   Not exhaustive when first written; treat this list as the tagged-allocation
   highlights, not a complete call inventory.
7. **`InGameState_LoadingDispatch`** (was `FUN_000ad4a0`, vtable slot 5, called every
   tick) — **corrected twice in this same investigation.** First read as a pure
   post-race results-screen selector (its `"FELoad"`/`"VideoLoad"`-allocating
   branches, still accurate for when they trigger). **Then corrected again**: its
   *first and most common* branch — `this+0x50==0` (not currently mid-load) and
   `this+0x49==0` — calls **`InGameState_TickFrame`** and returns immediately.
   That's the normal-frame path; the loading/results-screen branches only trigger
   when those flags are set during an actual transition. So this function is both
   the per-frame tick dispatcher *and* (conditionally) the loading-sequence/
   results-screen selector, not an either/or.
7a. **`InGameState_TickFrame`** (was `FUN_000ac6b0`) — **the master per-frame
    gameplay tick, the single closest thing to "the top of the per-frame race loop"
    found this entire project.** Called every frame via `InGameState_LoadingDispatch`'s
    default branch. Conditionally calls `ReplayManager_UpdateRecordingState`,
    manages HUD overlay-slot transitions (via `HUD_FindActiveOverlaySlot`), checks
    pause/lesson-wait/cutscene state, and increments a frame counter (`this+0x4c`)
    — confirmed to run exactly once per frame. **Does not directly touch
    rider-array fields itself**. ~~Several of its sub-calls (`FUN_000ab730`/
    `FUN_000ab7d0`/`FUN_000b5d90`/`FUN_000a8fa0`/`FUN_000ca1b0`) remain unexplored
    and are the most promising remaining candidates for where per-rider updates
    (and thus the score write) actually get dispatched from.~~ **Corrected
    (2026-07-21): this note was stale.** All 5 of these were already confirmed
    (in `InGameState_ApplyHudElementVisibility`'s own rename comment) as
    HUD/overlay-state management, not rider physics or scoring — a genuine,
    valuable negative result closing off this specific candidate path. Only 2 of
    the 5 had actually been renamed at the time, leaving this note looking
    open when it wasn't; the remaining 3
    (`InGameState_IsHudOverlayActive`/`InGameState_FindActiveOverlaySlotAlt`/
    `InGameState_TickActiveOverlay`) are now also named. **This specific path is
    a dead end for the rider-update-dispatch question — not a remaining
    candidate.**
8. **`InGameState_HandleLoadComplete`** (was `FUN_000ab560`, vtable slot 4) — sets up
   the rendering viewport and marks loading complete once a cutscene/intro-done flag
   is set.
9. **`InGameState_UpdateFadeTimer`** (was `FUN_000ab610`, vtable slot 6) — a
   countdown-timer-driven fade/transition handler.
10. Two tiny stub slots (`InGameState_StubTiny`/`StubTiny2`, 3-6 bytes each) and the
    standard destructor pair, not individually interesting.

## Followed the `AIWorld` object too — a real per-frame system, but not the one

`InGameState_LoadLevel` allocates `"AIWorld"` (`0x51c` bytes) with its own 8-slot
vtable (`0x0019a40c`) — read its slot 1 (374 bytes, the best "Update" candidate):

- **`AIWorld_UpdateTerrainStreaming`** (was `FUN_00030750`) — a genuine per-frame
  system: iterates every rider (`DAT_001e3c7c+0x72c` → `+0x1c` → `+0x88` count,
  `+0xc4` array — **the identical world/rider-array field offsets
  `Race_ComputeRankings` uses**, cross-confirming the object identity), calling
  **`Rider_ComputeTerrainCellIndex`** (was `FUN_00031880`) on each rider to compute
  their current terrain grid-cell from world position, then compares against a
  cached previous cell to detect grid crossings and trigger streaming/LOD updates.
  **This is terrain streaming, not general gameplay or trick logic** — a real,
  confirmed per-frame system, just not the one being searched for. `AIWorld`'s
  remaining vtable slots reuse generic stubs from other classes
  (`InputDevice_StubReturnFalse`, `RaceState_NullHandler`) rather than providing
  real AI-navigation logic — this object turned out narrower in scope than its name
  suggests, or the real AI logic lives in yet another sibling object.

  **Update (2026-07-20, "full throttle" different-direction pass)**: found that
  sibling object — or rather, two of them. `InGameState_LoadLevel` also allocates
  two tiny `"PREAI"`/`"PostAI"` tagged objects (real `NodeBase`-derived,
  `NodeRegistry` types 5/0xb), whose own `Update` methods (previously
  un-analyzed code, no `Function` existed) turned out to drive the entire
  per-rider trick-animation-event lifecycle -- not rival-racer navigation AI
  despite the "AI" naming, but genuinely substantial, freshly-mapped territory.
  See `RE_NOTES_rider_update_chain.md`'s "Found the real BdrSeq animation-event
  queue consumer" section for the full trace. 11 renames.

## `WorldTriggerManager` — the first confirmed consumer of a resolved input action

`Application_InitSubsystems` allocates several more subsystems beyond graphics/
localization, all found by reading its remaining calls: `"Aggression Manager"`
(`0x900` bytes — very likely the rival-relationship system already found in
`constant.loc`'s `kAggression_Level1_c` etc. naming, see `RE_NOTES_race_hud.md`),
`"BXAudioSystem"` (`0x7df0` bytes, the audio engine), and **`"WorldTriggerManager"`**
(`0x443c`/17468 bytes) — which turned out to be the payoff:

- **`WorldTriggerManager_Construct`** (was `FUN_0011ff10`) allocates a tagged
  **`"m_aWorldTriggerInstances"`** pool — 40 slots, 64 bytes each (the `m_a`
  Hungarian-notation prefix is a genuine C++ source convention, not something this
  session invented).
- **`WorldTriggerManager_Update`** (was `FUN_00120cf0`) walks all 40 slots each call,
  and for each *active* instance either finishes a running audio cue or calls
  **`WorldTriggerInstance_ProcessActionCode`**.
- **`WorldTriggerInstance_ProcessActionCode`** (was `FUN_001209f0`) checks the
  instance's stored action code against the literal values `0x61`/`'a'`,
  `0x62`/`'b'`, `99`/`'c'` — **the exact same ASCII-letter action codes
  `Input_ResolveActionCode` resolves for grab-trick button combos** (matching
  `data/config/btnmap0.dat`'s `easyA`-`easyD` naming from `RE_NOTES_control_scheme.md`).
  On a match, it calls **`Trick_ResolveSoundCueID`** (was `FUN_001201c0`, a large
  switch-case table mapping trick/action IDs to sound-cue IDs) and starts audio
  playback through the `BXAudioSystem` handle.

**This is the first confirmed per-frame consumer of a resolved input action code
found anywhere in this project** — the trick-action-to-voice/SFX pipeline, cleanly
closing the loop from `RE_NOTES_control_scheme.md`'s input tracing. It's genuinely
satisfying even though it's audio feedback rather than the score increment itself:
`WorldTriggerManager_Update` is called from `InGameState_UpdateFadeTimer`
(`InGameState`'s vtable slot 6) whenever its fade timer reaches 0 outside of a load
state — meaning it runs as part of `InGameState`'s **regular per-frame tick during
normal gameplay**, not just at load boundaries. A second caller exists in a currently-unanalyzed code region (around `0x000e9e72`).
**Attempted to establish its function boundary and backed off deliberately**: raw
byte-pattern scanning for a preceding `RET` (`0xc3`) found two candidates
(`0xe9e1b`/`0xe9e55`), but the second turned out to be a false positive (part of a
`CALL`'s relative-displacement bytes, not a real return — the exact same false-
positive class documented project-wide for `E8`/`E9` opcodes). `create_function`
failed at every address tried (`0xe9c00`, `0xe9e56`), consistent with the region
already being claimed by data or an unanalyzed function whose true start wasn't
found. Didn't force it further — this is exactly the kind of guess that caused the
`Timer_RebuildPlayerRegistry` truncation incident earlier this project; worth
exploring first if continuing this exact thread, since it may be a more central/
general call site than the fade-timer one, but needs more careful boundary-finding
(disassembling forward from a known-good function rather than backward from the
target) than attempted this pass.

## `AggressionManager` — the rider-pair relationship/banter matrix

Also found via `Application_InitSubsystems`'s `"Aggression Manager"` allocation
(`0x900` bytes) — ties directly to `constant.loc`'s `kAggression_Level1_c`-style
symbolic names (`Buddy`/`Friend`/`Rival`/`Enemy`, found during the localization-archive
work in `RE_NOTES_race_hud.md`).

- **`AggressionManager_Init`**/**`AggressionManager_ConfigureDefaults`**/
  **`AggressionManager_ResetMatrix`** (were `FUN_0006cf90`/`FUN_0006af60`/
  `FUN_0006d110`) — setup. `ResetMatrix` fills a **12×12 grid** (matching this
  codebase's consistent 12-rider-max pattern) with default per-cell values.
- **`AggressionManager_ComputeRelationshipMatrix`** (was `FUN_00073870`) — the real
  work: a nested `riderCount × riderCount` loop computing a 5-byte relationship
  record for every rider pair, reading from the 12×12 matrix through 4 small
  accessor helpers. Heavily referenced (18 xrefs to the manager's global pointer
  across ~10 different functions) — a genuinely active, widely-used system. Reads as
  the backing data for crowd/commentary reactions
  (`config/nascript.inf`'s narrator segments, `config/crowd.inf`'s cheer/fall
  cues) keyed by each rider's relationship tier to every other rider.

  **Update (2026-07-20)**: read all 4 accessor helpers and the finalize step.
  Each 12×12 matrix cell is 16 bytes: offset 0 (`AggressionManager_GetRelationshipLevel`)
  is the aggression/relationship level byte, offset 4
  (`AggressionManager_GetDecayMode`) selects a per-pair decay rate, offsets 8/0xc
  (`GetRelationshipField2`/`Field3`) feed the record's remaining bytes with
  their exact semantic not pinned down. **`AggressionManager_DecayRelationshipLevels`**
  (was `FUN_0006e380`) is a genuine relaxation mechanic — relationship levels
  drift back toward a baseline over time (decrement by 1-3 per tick depending
  on decay mode, floored at 0) rather than staying at an extreme permanently.
  **`AggressionManager_FindCategoryIndex`** (was `FUN_000697a0`) resolves a
  rider's relationship-tier category (Buddy/Friend/Rival/Enemy) for the
  matrix's default-value fallback path. Confirmed `ComputeRelationshipMatrix`
  is called once per race setup (inside a large, mostly-unrelated race-init
  function, `FUN_00074780`) alongside the decay step — this whole system
  refreshes once per race start, not per-frame. **The actual banter-trigger
  call site** (where these bytes get read during gameplay to pick a specific
  commentary line) still wasn't found — the most plausible lead is the
  already-documented `SpeechManager`/narrator system in
  `RE_NOTES_audio_system.md`, not confirmed this pass. 6 renames.

## `ShapeManager` — very likely the `.xsh` texture-sheet consumer

Also found via `Application_InitSubsystems`: **`ShapeManager_Construct`** (was
`FUN_000a9a90`), tagged `"shpMngr"`, initializes a 200-slot table with a 28-byte
(`0x1c`) stride per slot. That stride is an **exact match** for `IconAtlas_GetEntry`'s
indexing math (`index*0x1c+8+this`) found during the HUD investigation
(`RE_NOTES_race_hud.md`) — strong evidence `IconAtlas`/`ShapeManager` are the same
underlying system. Separately, found a large embedded table of `.xsh` file paths via
`/search_bytes` (`data/textures/hudgame.xsh`, `hudtrick.xsh`, dozens of per-character
numbered texture sheets like `t01mari.xsh`-`t49mari.xsh`) — didn't connect this table
to a specific loader function this pass, but the pieces (manager class + slot
format + file-path table) are all now identified for a future session to finish
wiring together.

## `OverlapManager` — resolves a "good next thread" from an earlier session

Read `InGameState_LoadLevel` (2738 bytes) in its entirety this round — it allocates
a long list of tagged subsystem objects (`"VideoStreamMan"`, `"PostAI"`/`"PREAI"`,
`"PowerFX Particles"`, `"SnowFallMan"`, `"OverlayNode"`, `"ModelsNode"`, `"SkyNode"`,
`"TerrainNode"`, `"FogMan"`, `"LessonMan"` for tutorial mode, `"DebugMenu"`) but
**no dedicated "race"/"score"/"trick manager" object** — confirming the score-writing
logic is embedded within the already-mapped systems rather than a separate monolith.

The most valuable of these: **`"OverlapMan"`** (`0x44` bytes), which resolves a
"good next thread" explicitly flagged in an *earlier* session's notes
(`RE_NOTES_level_script_system.md`'s sweep-and-prune section) — what actually
consumes the broad-phase overlap-pair records `SweepPrune_ToggleAxisOverlap`
allocates. Found and fully mapped: `OverlapManager` owns a **`"OverlapFlags"`**
buffer (`0x1fe00`/130,560 bytes — confirmed exactly `256×255/2×4`, a packed
upper-triangular pairwise flag matrix for up to 256 tracked objects).
`OverlapManager_ComputePairIndex` computes the flat offset for any object pair; when
`SweepPrune_ToggleAxisOverlap` flips a pair's 3rd axis-overlap bit to 1 (all 3 axes
now overlapping — a genuine AABB overlap begins), it calls
`OverlapManager_LinkOverlapRecord` **twice** (once per object), inserting the new
pair record into *each object's own* per-object linked list of active overlaps.
Losing the bit calls `OverlapManager_UnlinkOverlapRecord` symmetrically.

**Key architectural conclusion: overlap notification is poll-based, not
callback-based.** There's no direct virtual dispatch from `OverlapManager` into
`TrickTrigger`/`Boost`/etc. when an overlap begins — each object is expected to walk
its own per-object overlap list during its own `Update()`, exactly matching
`TrickTrigger_Update`'s already-documented behavior. The full collision pipeline is
now mapped end-to-end: `SweepPrune_MaintainAxis` (3-axis interval sort) →
`SweepPrune_ToggleAxisOverlap` (per-axis-crossing bit toggle) → `OverlapManager`
(persistent pairwise flag + per-object linked-list bookkeeping) → each trigger-family
object's own `Update()` polling its list. 7 renames. Full detail in the updated
`RE_NOTES_level_script_system.md` (the section this resolves).

## `TransitionEffect` — the screen/menu wipe controller (closes out `"Transition Man"`)

`Application_InitSubsystems` allocates a tagged `"Transition Man"` object
(`0x7c`/124 bytes) that had been found but never read. It's a small, complete,
self-contained system:

- **`TransitionEffect_Construct`** — trivial defaults.
- **`TransitionEffect_Start`** (8 params) — begins a transition: target transform
  values, 3 phase durations, a completion callback.
- **`TransitionEffect_Cancel`** — early-cancel while mid-transition.
- **`TransitionEffect_Update`** — the real per-frame work: a 3-phase (fade-in/hold/
  fade-out) timed linear interpolation between two 5-float transforms, rendering the
  interpolated result through the graphics device each call. The actual screen-wipe
  effect used for transitions between menu/game states.

4 renames, all verified live via `/get_symbol_status` (the new GhidraMCP endpoint,
round 4/5 — see `reference_ghidra_mcp_connection.md` in project memory for the build
history, including a real bug in `renameData` found and fixed via this same
verification pass: it silently no-op'd on any address without a pre-existing `Data`
type, affecting `VoiceAssetKeyTable_TrackGari` from earlier this session — fixed and
re-applied, now confirmed `USER_DEFINED`).

## Recovering an unanalyzed code region — carefully — and finding `ReplayManager`

Went back to the flagged lead: `WorldTriggerManager_Update`'s second caller, sitting
in a ~1.6KB region with zero defined functions. Did this properly this time (unlike
the earlier attempt in the same investigation), using the project's established
"read bytes, don't guess" methodology:

1. Found the nearest preceding known function (`0xe97b0`, ending at `0xe9804`).
2. Read raw bytes from `0xe9804` forward and found genuine `0x90` (NOP) alignment
   padding, with real x86 code (a clean function prologue) resuming at exactly
   `0xe980f`.
3. Created the function there — succeeded immediately, no boundary-detection
   failures this time. Got a **3038-byte function** (`0xe980f`-`0xea3ed`).
4. **Verified both neighboring functions were untouched** (`0xe97b0` and `0xea3f0`
   both still report their original, correct boundaries) before trusting the result
   — no repeat of the `Timer_RebuildPlayerRegistry` truncation incident from earlier
   in this project.

The recovered function — **`ReplayManager_UpdateSequenceState`** (was `FUN_000e980f`)
— turned out to be the state-machine driver for **`ReplayManager`**, the instant-
replay recording system:

- **`ReplayManager_Construct`** (was `FUN_000bd760`) — the object `InGameState_LoadLevel`
  allocates and tags `"ReplayMan"` (`0x40e8`/16,616 bytes). Allocates 4 per-rider
  replay-recording sub-buffers, computes frame-buffer capacity by dividing a fixed
  ~800KB budget by a per-frame size derived from rider count — a classic circular
  replay-buffer sizer. Ties directly to the `"Replay Full"`/`kOVReplayFull` localized
  string found earlier (`RE_NOTES_race_hud.md`) — the warning shown when this buffer
  runs out.
- **`ReplayManager_ResetState`** (was `FUN_000bcd00`) — a field-reset helper called
  from the constructor.
- **`ReplayManager_UpdateSequenceState`** (was `FUN_000e980f`) — reads/writes state
  on the `ReplayManager` object via the world-state pointer
  (`DAT_001e3c7c+0x72c+0x40`, the exact offset `ReplayManager_Construct`'s result
  gets stored at). Candidate states observed: 3/6/7/9/10 (idle/recording/playing-
  back/buffer-full etc., not individually mapped this pass). Calls
  `WorldTriggerManager_Update` in one branch, gated on rider count ≥ 2 — plausibly a
  versus-mode replay-comparison feature. **Its own caller was not found** (no
  `/xrefs_to` or `/search_address_refs` hits) — likely reached through a computed
  call.

3 renames. Genuinely satisfying: not the score-writer, but a real, previously-
completely-unknown subsystem recovered safely from unanalyzed binary space, with a
clean cross-confirmation against the localization work from earlier in this session.

## Where this leaves the score-writer / trick-detection search

**Major update: `InGameState_TickFrame` (was `FUN_000ac6b0`) is now confirmed as the
master per-frame gameplay tick** — found via `ReplayManager_UpdateRecordingState`'s
one caller, which turned out to sit in the *first, most common branch* of
`InGameState_LoadingDispatch` (not a separate "race object" as earlier guessed —
`InGameState_LoadingDispatch` itself does double duty as both the per-frame
dispatcher and the loading/results-screen selector, corrected above). This closes
the "where is the top of the per-frame loop" question that motivated the whole
`Application`/`InGameState` investigation.

**`InGameState_TickFrame` itself doesn't touch rider-array fields directly** — its
frame counter increments, HUD overlay-slot management, and `ReplayManager` calls are
all confirmed. **Read all 5 of its immediate sub-calls to completion this round**:

- **`InGameState_ApplyHudElementVisibility`** (was `FUN_000ab730`) — walks two small
  fixed ID tables, toggling a group of HUD elements on/off by state.
- **`InGameState_BuildActiveRiderTagMask`** (was `FUN_000ab7d0`) — iterates the
  rider array building a bitmask of present rider tags (purpose not traced further,
  candidate: active-player-icon display).
- `FUN_000b5d90` — a trivial two-field non-zero check (an "is X active" gate).
- `FUN_000a8fa0` — a generic "find first pending entry" list scan.
- `FUN_000ca1b0` — a thin vtable-forwarding call.

**Conclusion: all 5 are HUD/overlay-state management or generic utility helpers —
none of them dispatch per-rider gameplay updates.** This is a genuinely valuable
negative result: it definitively rules out `InGameState_TickFrame`'s own call graph
(one level deep) as the score-writer's location, rather than leaving it an open
guess. The actual per-rider update dispatch — and with it, wherever the score gets
written — happens through a path `InGameState_TickFrame` does **not** call directly.
Given `Rider_UpdateSubsystems` is confirmed reachable only through a vtable call
(`RE_NOTES_rider_update_chain.md`), the most likely remaining explanation is that
per-rider updates are driven by a **separate call chain entirely** — possibly
issued directly from the low-level game loop/scheduler (outside `InGameState`'s own
object graph) rather than through `InGameState_TickFrame`'s "UI-flavored" per-frame
work. Finding that separate chain would need a different starting point than
anything explored this session — worth a fresh session's dedicated attention rather
than another layer of "read what X calls."

## Two more angles tried, both genuine (informative) dead ends

- **Searched for a generic "update all nodes of type N" dispatcher** hoping it
  would reveal the rider-update call site. Found one —
  **`NodeRegistry_UpdateAllOfType`** (was `FUN_000bba80`) — but it's only used for
  checkpoint/replay-adjacent node categories (Debounce/sub-dispatch/Boost/Timer via
  **`GameState_ResetTransientTriggerNodes`**, was `FUN_000bc4d0`; and a replay-
  timing check via **`ReplayManager_CheckNearbyTimerNodes`**, was `FUN_000bbb80`),
  never for riders. A real, useful finding (also named **`ScriptObject_DestroyByID`**,
  was `FUN_00049830`, a related "destroy a script-spawned object by ID" utility — 4
  renames for the cluster) but not the lead hoped for.
- **Traced `Application_ConstructAndInitInput`'s one caller** to confirm there's
  truly nothing above it in the game-logic hierarchy. It leads into address range
  `0x153xxx`-`0x154xxx` — tightly-packed, non-round addresses with `FS:`-segment-
  prefixed instructions right at the boundary, the classic signature of MSVC CRT
  startup code, not hand-written game logic. Didn't force a function boundary there
  (low expected value even on success, and the address style itself is strong
  enough evidence). **This confirms `Application_ConstructAndInitInput` really is
  the top of the game-logic call graph** — nothing above it needs mapping.
  ~~**Correction (2026-07-21, "make sure not mistake" pass): this call WAS worth
  forcing after all.**~~ Bisected the boundary anyway (a `RET`+`NOP`-padding gap,
  the same class this project has repeatedly hit) and created the function at
  `0x001541a9`. Ghidra's own auto-analysis immediately named it
  **`XAPILIB::mainXapiStartup`** — a real, recognized Xbox-SDK symbol, not
  something invented. Its body: `XapiInitProcess` → TLS setup → `_rtinit`/`_cinit`
  (CRT init) → `Application_ConstructAndInitInput(0,0,0)` → `XapiBootToDash(1,1,0)`.
  This *is* the real entry point/`WinMain`-equivalent this whole project has been
  looking for — the earlier "low expected value" call was reasonable given the
  evidence at the time (the address style really is a strong, correct signal for
  "CRT-adjacent code"), but the specific conclusion that nothing more was worth
  finding there was wrong. See the corrected `Application_ConstructAndInitInput`/
  `Application_RunAndShutdown`/`Application_RunMainLoop` entries in
  `ssx_auto_rename.py` for the full, now-closed startup-to-shutdown chain this
  unlocks.
- Also re-checked whether `ReplayManager_UpdateSequenceState` has any caller at all,
  via both `/xrefs_to` and `/search_address_refs` — still none. Consistent with
  `Component_UpdateAll`'s similarly-orphaned `this` pointer: this codebase has a
  recurring pattern of functions reachable only through computed/indirect calls
  that literal-address search cannot find.

## The actual conclusion: level scripts are resolved from runtime level data, not `default.xbe`

Tested a new hypothesis: given how much of the score-writer search kept hitting
"there's no compiled instruction for this," what if the trick-scoring logic simply
isn't compiled C++ at all? Checked what happens when a race actually starts —
**`GameMode_PlayRaceModeScript`** (already named from an earlier session) calls
`Script_PlayByName("RaceMode")`. Sibling functions `GameMode_PlayShowoffModeScript`/
`GameMode_PlayFreerideModeScript` play `"ShowoffMode"`/`"FreerideMode"`.

Traced `Script_PlayByName`'s name resolution fully: it calls
**`ScriptTable_ResolveNameToID`** (was `FUN_00141ed0`), which does a `__stricmp`
linear search through a **runtime-loaded table of script name strings** — count and
table base read from the live resource context, not any static address in
`default.xbe`. Searched for the literal string `"RaceMode"` across every `.big`/
`.cml` file checked so far (level model archives, camera scripts, common objects) —
**not found anywhere**, meaning it's very likely embedded in each track's own model
archive using the custom, still-undecoded wrapper format (`c0fb` magic, varint-like
encoding — see `RE_NOTES_game_data_archives.md`), not in any file this session
managed to read as plain data.

**This is the most likely real explanation for the entire score-writer search coming
up empty**: `"RaceMode"` (and its siblings) are **level-script data**, executed
generically by the already-fully-documented `ScriptVM_DispatchOpcode` interpreter
(`RE_NOTES_level_script_system.md`, all ~27 opcodes mapped). If the score increment
is expressed as script opcodes operating on a script-supplied field offset (matching
how `TrickTrigger_Construct` already reads its own threshold as raw command-record
data, `param_4+0x10`), there would be **no line of compiled C++ to find** — the
exhaustive `/search_bytes` sweep for a literal `+0x5710` write instruction
(`RE_NOTES_rider_update_chain.md`) came up empty not because the search wasn't
thorough enough, but because the write genuinely isn't compiled code.

**If continuing this investigation**, the productive next step is no longer "read
more C++" — it's **decoding the model archive wrapper format** well enough to
extract the `"RaceMode"` script's actual bytecode, then interpreting it against the
already-documented opcode table. That's a real, bounded, different kind of task
(binary format reverse-engineering, not Ghidra decompilation) — a good candidate for
a session explicitly framed around it rather than another disassembly deep-dive.

This investigation, taken together with `RE_NOTES_control_scheme.md` and
`RE_NOTES_rider_update_chain.md`, now provides a complete top-to-bottom map of the
application's structure: `Application` (confirmed top of the game-logic hierarchy)
→ state machine → `InGameState` → **`InGameState_TickFrame`** (the real per-frame
tick, confirmed, all 5 of its immediate sub-calls read and ruled out as HUD-only)
→ [not found: the separate call chain that reaches `Rider`] → `Rider` (per-entity,
fully mapped) → attached components (found, not identified) / level-script VM
(fully mapped). The remaining gap is precisely "what calls `Rider`'s update outside
`InGameState`'s object graph" — genuinely narrow, but needs a different starting
point than this session found, not another layer of "read what X calls."

## Update (2026-07-21, "fresh" pass): found the `.ffn` font-loading cluster

Re-swept `InGameState_LoadLevel`'s own call list (it's called twice, with
`"data/fonts/title.ffn"`/`"data/fonts/menu.ffn"`) and found the actual runtime
loader for the already-decoded `.ffn` font format — closing a gap between file
format and code that had sat open since an earlier session first decoded the
format from raw file bytes alone.

- **`Font_LoadAndParse`** (was `FUN_000c36f0`) — the outer per-font wrapper:
  reads the raw file via `Font_LoadFileData`, builds the runtime glyph table via
  `Font_ParseGlyphTable`, frees the raw buffer, and sets default scale factors.
- **`Font_LoadFileData`** (was `FUN_0014bed0`) — trivial `FILESYS_atomic(FILE_load, ...)`
  wrapper.
- **`Font_ParseGlyphTable`** (was `FUN_000c2da0`) — parses the `.ffn` binary header
  and allocates a `"Glyphs"`-tagged array sized `glyphCount*0xc` — **the exact
  12-byte-stride sorted glyph table the already-named `Font_GetGlyphMetrics`
  binary-searches over at `fontTable+8`**, confirmed via matching field offsets,
  not assumed.

3 renames. A clean, well-anchored find — closes the loop between the `.ffn` file
format (decoded from raw bytes in an earlier session) and its actual loader code
(never previously traced).
