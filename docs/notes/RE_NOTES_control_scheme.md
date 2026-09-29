# RE notes: the trick control scheme (`data\config\btnmap0.dat`/`btnmap1.dat`)

Found while looking for a fresh angle on the still-open "who writes the score"
question (see `RE_NOTES_rider_update_chain.md`) — checked the controller config files
on the theory that input handling might connect to trick detection. It doesn't connect
directly to any Ghidra code (see "Why this doesn't bridge to code" below), but it's a
complete, human-readable, and extremely valuable document in its own right: **the
exact button/analog-stick mapping for every trick input in the game**, straight from
the developers' own config format. Genuinely useful for a PC-port control scheme,
independent of the code-RE side of this project.

## Format

Plain text, not binary despite the `.dat` extension (`\r\n` line endings — use
`tr -d '\r'` or similar, the file reads as garbage through naive binary-file tools).
Three sections, each a simple `{ name, condition, condition }` table:

- **`IS_BOOL`** — digital (on/off) actions, gated on a button-pressed mask and a
  button-not-pressed mask (`ALL`/`NONE`/`(A)`/`(B | X)` etc.).
- **`IS_BOOLGRAB`** — the grab-trick selection table (see below).
- **`IS_ANALOG`** — analog-stick-driven actions, each mapped to an analog input mask
  (`ANALOG_PAD_*`/`ANALOG_JOY_L_*`/`ANALOG_JOY_R_*`) plus a negate-output flag.

`btnmap0.dat` and `btnmap1.dat` are near-identical (almost certainly per-controller-port
configs, or a default vs. a lightly-tuned alternate) — a handful of small differences
(e.g. `CameraReverse` bound to `BLACK` in one, unbound in the other; `PrewindSpin`
reading one analog source vs. two).

## The full trick action list

**Digital actions** (`IS_BOOL`): `Antic`/`AnticAbort` (anticipation/wind-up gesture —
ties to `crowd.inf`'s `ANTICIPATE1`-`3` crowd-reaction sounds found earlier),
`Boost`/`Tweak` (both bound to `B | X`), `CameraToggle`/`CameraReverse`,
`LateSpinMode`/`RailSpinMode`/`PrewindTurnMode` (trick-style mode selectors),
`RandomGrab`, `Reset`, `ShoveLeft`/`ShoveRight`, `LessonWait` (tutorial-specific).

**Grab-trick selection** (`IS_BOOLGRAB`) — **15 distinct grabs across 3 difficulty
tiers**, selected by shoulder-button (`LSHIFT`/`RSHIFT`) and `Y` combinations:
`easyA`-`easyD` (4), `medA`-`medF` (6), `hardA`-`hardE` (5). This is the actual
trick-variety breadth behind the game's scoring — matches the general shape of
`Trick_GetScoreTier`'s difficulty-graded combo system found earlier
(`RE_NOTES_rider_update_chain.md`), though not a direct 1:1 mapping (that system
grades *combo count*, this selects *which specific grab*).

**Analog actions** (`IS_ANALOG`): `Brake`/`Crouch` (vertical stick), `Spin`/`Flip`/
`Turn` (basic aerial rotation), `ShoveSide`/`ShoveFwd` (right-stick shove, likely a
push/interaction move), `RailSpin`/`LateSpin`/`LateFlip` (rail-grind and late-trick
variants), `AirTurn`/`AirTilt` (left-stick aerial fine control), `PrewindTurn`/
`PrewindSpin`/`PrewindFlip` (a "prewind" mechanic — winding up before a trick for a
bigger spin, a known SSX Tricky feature), `GateRock` (likely a starting-gate rocking
motion for a launch boost).

## Why this doesn't bridge to Ghidra code by name — but the mechanism was found anyway

Searched the live binary (`/search_bytes`) for the literal action names (`"RandomGrab"`,
`"LessonWait"`, etc.) — **zero matches**. These names don't exist anywhere in
`default.xbe`. This confirms the `.dat` file is a **design-time/build-time config**:
some offline tool almost certainly compiles this human-readable table into a compact
binary lookup (button-mask → small integer action-ID enum) before it ever reaches the
shipped game, the same way the `.loc` files' `constant.loc` symbolic names don't
appear as literal strings either — they're both authoring-side artifacts.

**But the compiled mechanism itself was found and fully traced**, starting from
`XInputGetState` (the real Xbox kernel controller-read API) and following its single
caller up through the whole pipeline — the first time this project has looked at raw
input handling at all (0 `Input_*` functions existed before this pass):

1. **`Input_PollDevice`** (was `FUN_000a91d0`) — the only caller of `XInputGetState`.
   Manages device connect/disconnect (via `XGetDevices`) and opens/closes the
   controller handle, polling raw state each call.
2. **`Input_NormalizeGamepadState`** (was `FUN_000a9350`) — translates the raw
   `XINPUT_GAMEPAD` struct (digital button word, 8-byte analog-button array, 4 analog
   stick shorts — the original-Xbox controller's actual struct layout, distinct from
   later XInput) into a **game-specific normalized bitmask** plus processed float
   stick values, treating each analog button (A/B/X/Y/Black/White/triggers) as
   digitally "pressed" past a `0x1f` (31) pressure threshold, and flagging stick-axis
   deflection past thresholds as extra bits.
3. **`Input_ResolveActionCode`** (was `FUN_000a8ff0`) — walks a **14-entry table**
   (`DAT_0019a038`) matching the normalized bitmask against known patterns — **14
   entries, exactly matching `btnmap0.dat`'s `IS_BOOL` section's 14 actions**. Several
   resolved action codes for shoulder/face-button combos are literally the ASCII
   letters `'a'`-`'d'`/`'A'`-`'D'`, matching `IS_BOOLGRAB`'s `easyA`-`easyD` naming —
   strong circumstantial evidence this table really is the compiled form of the
   `.dat` config, even without the exact per-entry mapping pinned down. Includes a
   genuine **repeat/debounce state machine**: a resolved action only "fires" on first
   detection or after 8+ consecutive frames of the same input (with a hold-repeat
   flag gating the latter) — explains why menu navigation and trick inputs both feel
   responsive on first press but don't spam-repeat every single frame while held.
4. **`Input_GetAnalogState`/`Input_SetAnalogState`** (were `FUN_000a9590`/`FUN_000a95d0`)
   — plain accessors for the 8-dword processed analog state.
5. **`Input_SetFeedbackPulse`/`Input_SetFeedbackEnabled`** (were `FUN_000a9610`/
   `FUN_000a9660`) — rumble/force-feedback control, part of the same device
   descriptor structure (found tagged `"InputFeedback"` in the binary next to these
   function pointers).

All of these are referenced only via a **data table** — turned out to be a real C++
vtable, not a loose function-pointer array. Followed it all the way up:

## The full class hierarchy: `InputDevice` → `GamepadInputDevice`, owned by `InputManager`

- **`InputDevice`** (base class, vtable at `0x0019a0e0`, 11 slots) — mostly stub
  methods: `InputDevice_ScalarDeletingDestructor` (slot 0), `InputDevice_StubReturnFalse`
  (slot 1, always returns 0), `InputDevice_StubReturnZero` (slot 2, always returns
  0.0f), 2 no-op slots, then slots 5-10 pointing at two more trivial stubs. Constructed
  by `InputDevice_Construct` (zero-inits all fields, sets a hold-repeat flag on by
  default).
- **`GamepadInputDevice`** (derived class, vtable at `0x0019a118`, right after the base
  vtable + 3 dwords of static float data) — overrides slots 3-10 with the *real*
  implementations found above: `Input_SetFeedbackPulse`/`Input_SetFeedbackPulseB`/
  `Input_SetFeedbackEnabled`/`Input_GetAnalogState`/`Input_SetAnalogState`/
  `InputDevice_GetTypeID` (slot 8, a trivial "return `0x16`" constant getter)/
  `Input_PollDevice`/`Input_NormalizeGamepadState`. Slots 0-2 (destructor + the two
  stubs) are **not** overridden — reused straight from the base class. Constructed by
  `GamepadInputDevice_Construct`, which allocates a tagged `"XInputFeedback"` buffer
  and opens the XInput device handle if connected.
- **`InputManager`** (was `FUN_000a8ce0`, now `InputManager_Construct`) — constructs
  **4** `GamepadInputDevice` instances, tagged `"XBoxjoypad0"`-`"XBoxjoypad3"` (one per
  original-Xbox controller port), stores them in an array, sums each device's
  `InputDevice_GetTypeID` result (polymorphic — would differ for a hypothetical other
  device type), and allocates an **8-frame-deep `"InputCache"` history buffer** sized
  by that sum. An 8-frame rolling history of every controller's resolved input state —
  exactly the kind of structure a combo/gesture-sequence detector would need to look
  back over recent frames.

## Update (2026-07-20, later session): the trail warms back up — found the whole frame-pump

Picked this up again per an explicit "different subsystem" request. The earlier
"level-transition/resource-reset routine" read on `InputManager_InitOrReset`
(already named by that point) was corrected: its only caller is
**`Application_ConstructAndInitInput`** — a genuine one-time application-boot
function (confirmed in `RE_NOTES_application_boot.md`), not a per-level reset. The
`DAT_001e3c7c = 0` line at its end is a scope/context flag clear, not an
`InputManager` teardown — the actual teardown-looking calls inside it free a small
temporary `"PadCache"` scratch buffer used only during setup, not the `InputManager`
itself. **`InputManager` really is a persistent, once-per-session object.**

Then found the actual **`InputCache` per-frame producer and the game's entire
frame-pump mechanism** in one continuous trace, starting from
`InputManager_InitOrReset`'s neighbor `FUN_000aa1a0`:

- **`InputManager_PollDevicesIntoCache`** (was `FUN_000a8f30`) — the master
  per-frame poll function. Computes the current ring-buffer slot address from the
  `InputManager`'s own fields (matches `InputManager_Construct`'s layout exactly),
  then for each of the 4 devices calls `Input_NormalizeGamepadState` (vtable+0x28)
  to write normalized state directly into that slot, advancing by each device's own
  serialized-state size (vtable+0x20 — **correction**: this is the same slot
  previously named `InputDevice_GetTypeID`; it's actually a state-size constant,
  not a type identifier, given how it's used here purely for pointer arithmetic).
  Confirmed as a real, reused utility via multiple genuine callers — not just boot
  code — including the already-named `VideoPlayer_UpdateSkipInput`.
- **`Input_CatchUpPollAndTick`** (was `FUN_000aa170`) — drains the poll function in
  a loop while it reports pending work, then one final vtable tick.
- **`Application_RunInitialLoadPump`** (was `FUN_000aa1a0`, my original lead) — a
  one-time boot-time blocking pump (spin-waits for the initial resource batch,
  polling input the whole time so the app doesn't look hung) that calls
  `SceneRenderer_SelectDetailLevel` once ready. Not the perpetual game loop itself.
- **`Application_TickFrame`** (was `FUN_000aa310`) — the real per-frame update:
  frame-delta computation, calls `Input_CatchUpPollAndTick`, a small state machine,
  then an unconditional final vtable dispatch every tick (plausibly the top-level
  app/game state's own per-frame Update, not individually confirmed).
- **`Application_FrameTimerCallback`** (was `FUN_000b26b0`) — **this is the
  answer to "how is the game's main loop actually driven," found for the first
  time in this whole project.** It's not a `while(1)` loop anywhere in the call
  graph — it's a self-rescheduling Win32/Xbox multimedia timer callback. Calls
  `Application_TickFrame`, computes real elapsed time, smooths/clamps the frame
  delta, then re-arms `XAPILIB::timeSetEvent` for the next callback using the same
  thunk. An adaptive frame-rate limiter, not a busy loop.
- **`Application_FrameTimerCallback_StdcallThunk`** (created at `0x000b26a0`, no
  function existed there before) — the literal callback `timeSetEvent` invokes: a
  tiny `__stdcall`-to-`__thiscall` adapter.
- **`Application_ArmFrameTimer`** (created at `0x000b29d0`) — the function that
  fires the very first `timeSetEvent`, kicking off the whole self-perpetuating
  chain. Seeds the frame-time-target constant (`0x41855555` ≈ 16.67ms/60fps) and a
  tick baseline.

**Resolved (same session, immediately after)**: the ambiguity above turned out to
be a real but fully reconcilable two-object design, not a genuine conflict. Re-read
`Application_FrameTimerCallback`'s raw disassembly with full precision: its own
`this` (`ESI` throughout the function) is consistently the **`XBoxExecutionMan`**
singleton for every field it touches (`+0x8` exit flag, `+0xc` last-tick, `+0x10`
accumulator, `+0x14` frame-time-target) — it only ever switches to a *different*
object, `[DAT_001e3c7c]` (the **`Application`** singleton), for the single call
into `Application_TickFrame`, then switches straight back to `ESI`/`XBoxExecutionMan`
for all the timer re-arming math. **Two objects, two cleanly separated
responsibilities** — `XBoxExecutionMan` owns the raw OS timer/event plumbing,
`Application` owns the actual per-frame game update — no aliasing or coincidence
needed. Confirmed further by finding `XBoxExecutionMan`'s remaining vtable slots
(2/3/4 — previously un-analyzed code, no `Function` object existed at any of the
three):

- **`XBoxExecutionMan_Shutdown`** (slot 2, was `FUN_000b2670`) — sets the exit
  flag, spins on slot 3 until it returns, then closes the Win32 event handle.
- **`XBoxExecutionMan_WaitForFrameEvent`** (slot 3, created at `0x000b2750`) — a
  `WaitForSingleObject`-shaped wrapper on the same event handle, `INFINITE` timeout.
- **`XBoxExecutionMan_SignalFrameEvent`** (slot 4, created at `0x000b2760`) — calls
  `XAPILIB::SetEvent` on the same handle. **This is exactly what
  `Application_TickFrame`'s mystery final unconditional dispatch calls** (its
  `param_1+0x2c` field holds the `XBoxExecutionMan` pointer, set once by
  `Application`'s own base-constructor) — so every single game frame ends with a
  `SetEvent` on this handle. **Corrected (2026-07-21): not a secondary-thread
  wait as originally speculated** — the waiter is `Application_RunMainLoop`
  itself (see the "fully reconciled" update below), most plausibly running on
  the SAME main thread, blocking between per-tick work rather than busy-spinning.
  Also named **`XBoxExecutionMan_Construct`** (was `FUN_000b2a20`,
  allocates the singleton and hands its pointer down through
  `Application_Construct` into the base-constructor that stores it at
  `Application+0x2c` — the exact link that resolves the whole chain). 5 more
  renames (2 newly-created function boundaries).

**Where the InputCache's *history* consumer still isn't found**: this trace found
the *producer* (who fills the ring buffer every frame) and the entire surrounding
frame-pump architecture, but not a reader that looks back across the buffered
8-frame history for gesture/combo pattern matching — if such a consumer exists at
all, it's still open. ~~`Application_TickFrame`'s own final vtable dispatch (to
whatever the current app/game state object is) is the most promising unexplored
lead for it.~~ **Corrected (2026-07-21, "make sure not mistake" pass): this lead
was a dead end, already checked and resolved elsewhere in this project before this
note was written** — `Application_TickFrame`'s final vtable dispatch is confirmed
to be `XBoxExecutionMan_SignalFrameEvent` (a Win32 `SetEvent` frame-boundary sync
primitive), not a game-state object at all. The real
top-level state object (`this+4` on `Application`) is instead ticked from the
newly-corrected `Application_RunMainLoop` (see `RE_NOTES_application_boot.md`'s
fully-reworked startup-chain section) via `Application_StateMachineTick` — but
that's a state-*transition* checker (swapping FrontEnd/InGameState), not
obviously a place that would consume 8 frames of buffered gesture history either.
The InputCache history-consumer question remains genuinely open; this specific
suggested next step just isn't it.

**"Keep going harder" pass, same day: the sync/async loop relationship is now
fully reconciled, and 2 more stale claims above were corrected.** (1) The old
`InputManager_InitOrReset`/`Application_ConstructAndInitInput` naming above is
superseded — see `RE_NOTES_application_boot.md` for the corrected
`Application_RunAndShutdown`/`Application_RunMainLoop`/`mainXapiStartup` chain.
(2) `Application_RunInitialLoadPump` (line ~153 above, "a one-time boot-time
blocking pump... not the perpetual game loop") was mischaracterized — it's
`Application_RunMainLoop` now, the genuine master game loop for the whole
session (unconditional `goto`-based loop, single quit-flag exit). (3) The
"secondary thread (audio/streaming/network)" guess for who waits on
`XBoxExecutionMan`'s frame event was also wrong — the waiter is
`Application_RunMainLoop` itself: its own `DAT_001ba53c` check (a fixed-value-1
flag with zero writers anywhere, so its `==0` branch is dead code) leads
unconditionally to `XBoxExecutionMan_WaitForFrameEvent`, a genuine
`WaitForSingleObject(event, INFINITE)`. **Complete picture**: the OS timer
paces `Application_TickFrame` (~60fps), which signals the event;
`Application_RunMainLoop` does its per-tick work then blocks on that same
event until woken — a classic single-thread, timer-paced game loop, not two
independent mechanisms. Still open: the writer of the `this+0x24` quit flag
that ends `Application_RunMainLoop` (checked `DAT_001df3f4` as a candidate —
it turned out to be a different, valuable finding: a pending-transition
bitmask on `Application_StateMachineTick`, not the quit flag).

19 renames total for the input pipeline + class hierarchy + frame-pump chain (up
from 0 `Input_*`/`InputDevice_*`/`InputManager_*` functions before the original
session).

## Ported: `port/src/assets/btnmap.{h,cpp}` (M3a)

Parses both `btnmap0.dat` and `btnmap1.dat` from the real game data. Verified in
`asset_test`:

```
[btn ] btnmap0.dat -> 14 digital, 15 grabs, 16 analog
        bindings: 0 checks failed
        Brake = PAD_NEG_Y|JOY_L_NEG_Y, negate=true  (correct)
        fires(Boost, B|X)=1  fires(Boost, B)=0  (expect 1, 0)
[btn ] btnmap1.dat -> 14 digital, 15 grabs, 16 analog
```

Counts match this file's documented shape exactly (15 grabs = 4 easy + 6 med +
5 hard), as do the specific bindings checked (`Boost`/`Tweak` = `B|X`,
`Reset` = `BACK`, `Antic` = `A`, `LateSpinMode` = `BLACK`, `easyA` = `LSHIFT`,
`easyB` = `Y`, `Brake` = `PAD_NEG_Y|JOY_L_NEG_Y` negated).

### One semantic decision worth recording
`(ALL)` appears in the **not-pressed** column of nearly every `IS_BOOL` row,
e.g. `{ Antic, (A), (ALL) }`. Read literally that would exclude every button and
make the row unfireable — yet these rows plainly must fire. So `(ALL)` there is
the file's idiom for **"no exclusions"**, and the parser treats it as an empty
exclusion set. That is the only reading consistent with the data; noted rather
than silently assumed.

Similarly `(NONE)` in the **pressed** column (e.g. `AnticAbort`, `RandomGrab`)
means "no held-button requirement", not "requires no buttons" — those rows are
gated purely by their exclusion mask.

Unknown symbols are ignored rather than failing the load, so an edited or
future map still parses.
