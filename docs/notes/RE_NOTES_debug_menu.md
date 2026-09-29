# RE notes: `DebugMenu` — a real developer debug menu that shipped in retail

Picked as a fresh direction (2026-07-20, continuing the "map the untouched
`InGameState_LoadLevel` tagged objects" thread). `"DebugMenu"` (`0x3864`/14436
bytes) is **unconditionally constructed** by `InGameState_LoadLevel` — not
gated on any `GameMode_Current` check, unlike `LessonMan`. This is by far the
richest of the recurring "`NodeBase`-derived `InGameState` subsystem" family
found this session, and a genuinely exciting result: **a fully-functioning
developer debug menu that survived into the shipped retail Xbox binary**, with
real, human-readable strings still present in the executable:

> `"BX Debug Menu"` (title), `"Return To Race"`, `"Instant Replay"`,
> `"Restart Race"`, `"Render Options"`, `"Game Options"`, `"Sound Options"`,
> `"Exit the Game"`

("BX" is very likely an internal studio/team codename, not previously seen
elsewhere in this project.)

## Architecture

`DebugMenu_Construct` (was `FUN_000b5900`) builds the whole top-level menu in
one call: the title, 3 top-level actions, and 3 submenu links (`"Render
Options"`/`"Game Options"`/`"Sound Options"`, each a sub-*region* within this
same 14KB allocation — not separate objects). Unlike the other 5 instances of
this recurring architecture found this session (`SnowFallMan`/`LessonMan`/
`PowerFX Particles`/`TerrainNode`/`VideoStreamMan`, all 8-slot vtables with a
shared no-op at slot 7), `DebugMenu`'s vtable is a **7-slot** shape with the
shared no-op at slot **3** instead — and **6 of the other 7 slots hold real,
distinct logic**, not just 1-2:

- **`DebugMenu_TickCurrentPage`** (slot 1, was `FUN_000b6370`) — the menu-page
  input/navigation tick. Notably this exact function is **shared across 14
  different vtable slots** (confirmed via `xrefs_to`), meaning every
  individual submenu/widget reuses this same tick logic at its own slot 1 —
  a real, load-bearing shared utility, not menu-specific.
- **`DebugMenu_HandleTopLevelSelection`** (slot 2, was `FUN_000b4420`) —
  dispatches the 3 top-level actions by index: **0 = "Exit the Game"** (sets
  quit flags `DAT_001df898`/`DAT_001df3f4` and the level's own `+0x50` flag),
  **1 = "Restart Race"** (`FUN_000ac550`), **2 = "Instant Replay"**
  (`FUN_000cf390(9, 0xffffffff)`).
- **`DebugMenu_ComputeScrollLayout`** (slot 4, was `FUN_000b7ff0`) — real
  scrollable-list layout math: queries each visible item's size, accumulates
  total height, computes a scroll window when the list exceeds a
  screen-height threshold (`0x1a5`).
- **`DebugMenu_RenderPage`** (slot 5, was `FUN_000b44d0`) — calls
  `AudioSystem_BeginOverlayDucking` then `AudioSystem_PlayUIClickSound`, then
  draws the current page.
- **`DebugMenu_CheckAutoReplayTrigger`** (slot 6, was `FUN_000b4500`) — if the
  menu isn't already showing something, calls `AudioSystem_EndOverlayDucking`;
  separately, if the level's race-context has a specific flag set, fires the
  exact same `FUN_000cf390(9, 0xffffffff)` call the manual "Instant Replay"
  selection uses — plausibly an **automatic** replay trigger on some
  race-finish condition, independent of manually opening the menu.

## Correction (same session, "take your time, deep research" pass)

The function originally named `DebugMenu_CleanupAfterClose` was **wrong** —
caught by tracing its true `this` via raw disassembly rather than trusting the
decompiler's generic `param_1`. It's `DAT_001f82f4`, the already-known
**`AudioSystem` singleton**, not anything `DebugMenu`-specific. Confirmed via
`xrefs_to`: this function has **9 call sites** spanning
`ReplayManager_UpdateSequenceState`, the newly-found
`ResultsScreen_HandleTransitionState` (below), and several other unrelated
functions — a generic "undo audio ducking" utility any overlay/transition can
call, not a `DebugMenu`-only cleanup step.

Renamed properly to **`AudioSystem_EndOverlayDucking`** — checks `AudioSystem`'s
own `+0x3aa8` "ducking active" flag and, if set, clears it and resets several
audio-channel subsystems. Tracing its counterpart (the flag *setter*) led
straight to two more real `AudioSystem` methods hiding behind
`DebugMenu_RenderPage`'s call site:

- **`AudioSystem_BeginOverlayDucking`** (was `FUN_0010f050`) — the set side of
  the same `+0x3aa8` flag: if not already ducking, sets it (idempotent — only
  runs once per overlay session), sets a couple more fields, and calls 3 more
  audio-subsystem functions to pause/duck ambient audio.
- **`AudioSystem_PlayUIClickSound`** (was `FUN_0010f090`) — a one-line
  wrapper calling `AudioSystem`'s own vtable`+0x30`, confirmed via the
  already-documented `AudioSystem` vtable (`0x001a5ae0`, slot 12) to be the
  already-named `AudioSystem_PlaySoundSimple`, with fixed arguments (sound ID
  0, param `0x7e`) — almost certainly a standard UI-open/click chime shared
  across menu systems.

Both are generic `AudioSystem` utilities `DebugMenu` happens to call, not
`DebugMenu`-specific — likely reused by other overlay systems too (not
individually checked this pass). Old rename kept commented out in
`ssx_auto_rename.py` per project convention, never silently deleted.

**Bonus find while tracing this**: the exact same "Instant Replay" trigger
code (`FUN_000cf390(9,...)`) that `DebugMenu_HandleTopLevelSelection` fires is
*also* fired from within a large, substantial post-race state machine —
renamed **`ResultsScreen_HandleTransitionState`** (was `FUN_000e8a00`, a
6-case switch, ~250 lines, moderate-high confidence). It drives real
results-screen transitions (entering/exiting results, audio ducking
begin/end, a localization-driven message dialog, and the actual replay path)
and calls already-named functions directly (`VenueStaging_ExitState`,
`AggressionManager_FindCategoryIndex`). **This confirms `DebugMenu`'s actions
hook into real, live game state machines — not an isolated or dead
debug-only code path.** A genuinely satisfying result from taking the time to
trace one lead properly instead of stopping at the first "no static caller
found."

## Exhaustive check for the actual "open the menu" trigger (same session, continued deep research)

Pushed specifically on "how does the menu open" rather than leaving it as a
vague "not found." `DebugMenu`'s own pointer is stored at
`InGameState+0x2a0`. Checked every angle available without dynamic analysis:

1. **`InGameState_TickFrame`** (full body, already read) — no `+0x2a0`
   reference anywhere.
2. **`InGameState_LoadingDispatch`** (re-read in full this pass) — no
   `+0x2a0` reference either; confirmed purely a loading/results-transition
   dispatcher.
3. **Every other function in `InGameState`'s own code neighborhood**
   (`0x000ac240`-`0x000ac930`, the full set between `InGameState_LoadLevel`
   and `InGameState_LoadingDispatch`) — checked all 5 unread ones
   (`FUN_000ac240`/`460`/`610`/`8e0`/`930`). None reference `DebugMenu` or
   `+0x2a0`; they're split-screen viewport setup and an unrelated
   destructor pair.
4. **A targeted binary-wide search** for the raw `0x2a0` displacement
   (`a0020000`) — found exactly 2 hits anywhere near `InGameState`'s address
   range: one inside `InGameState_LoadLevel` itself (the already-known
   construction/storage site) and one inside `FUN_000ab920`, confirmed via
   raw disassembly to be `InGameState`'s real destructor body doing
   `if ([this+0x2a0] != 0) CallVtable0(this+0x2a0, 1)` — i.e. **just
   tearing the `DebugMenu` object down on level teardown**, not activating
   it.

**Conclusion**: `DebugMenu` is genuinely constructed and destroyed every
level load/unload, but nothing in the statically-reachable code ever reads
its stored pointer to actually tick or render it. This is consistent with —
and a strong candidate for — the same "compiled in but the activation hook
was stripped/disabled for retail" pattern this whole project has found
repeatedly elsewhere (16 of 18 `Rider_UpdateSubsystems` stubs, etc.). Not
proven (a hotkey check reachable only through a computed call can't be ruled
out by this method), but this is a genuine, multi-angle exhausted search, not
an early stop. Dynamic analysis (or a full input-combo brute-force test in an
emulator) is the only remaining path to settle it for certain.

## A bonus find: an embedded memory/tag-usage debug page

Reading the raw bytes right after `DebugMenu`'s vtable and tag string turned
up a long list of readable ASCII labels: `"Frame Block"`, `"WriteTmp"`,
`"WriteBlock"`, `"Restore"`, `"Splinepath"`, `"MeshAnim"`, `"Dead Object"`,
`"PlayerSnapShot"`, `"ReplayCache"`, `"AIPaths"`, `"EventPaths"`, a
color-selector trio (`"Blue"`/`"Green"`/`"Red"`), and a `"-- Invalid --"`
placeholder string.

Initial hope: this might be a `NodeRegistry` type-index → name table (which
would have been huge for calibrating this whole project's many still-numeric
`NodeRegistry` type IDs). **Checked and ruled out** — cross-referenced
`"Dead Object"` against the already-named `New_Dead_Object` constructor, which
uses that *exact* string as its own tagged-allocator name
(`FUN_0012a250(0x2c, ..., "Dead Object", ...)`). This confirms the list is a
**per-tag memory/allocation usage display** (a classic dev-menu feature —
"how much memory is each named heap pool using") reusing the project's
existing tagged-allocator naming convention, not a `NodeRegistry` type table.

Still valuable: **5-9 tag names not previously seen anywhere in this
project** — `"Splinepath"`, `"MeshAnim"`, `"AIPaths"`, `"EventPaths"`,
`"ReplayCache"`, `"Frame Block"`, `"WriteTmp"`, `"WriteBlock"`, `"Restore"` —
worth remembering if any of these turn up via a `FUN_0012a250` tagged-
allocator call in a future investigation (particularly `"MeshAnim"`, which may
tie into the already-documented `cMeshAnim` level-script node type, and
`"AIPaths"`/`"EventPaths"`, which sound like real navigation/scripting data
structures not yet located).

**Update (2026-07-20, later same session): 2 of these found real homes.**
Picked the still-open `Rider_UpdatePhysicsState` sub-call list back up
(`RE_NOTES_rider_update_chain.md`) and traced 2 previously-vague-labeled
functions all the way through: **`"Splinepath"`** is the path-object shape
`SplinePath_FindClosestPoint`/`SplinePath_EvaluateAtDistance` operate on (an
embedded per-rider spline the rider tracks its track-distance position
against), and **`"EventPaths"`** is the external, vtable-dispatched object
`Rider_UpdateTrackEventTriggers` queries for scripted events keyed by track
distance (checkpoints, camera cuts, etc.). `"AIPaths"` still has no confirmed
code location.

8 renames for the core menu architecture + 3 new from the "deep research"
correction pass (`AudioSystem_BeginOverlayDucking`/`AudioSystem_PlayUIClickSound`/
`ResultsScreen_HandleTransitionState`, plus 1 in-place correction of the
mis-named `DebugMenu_CleanupAfterClose` → `AudioSystem_EndOverlayDucking`,
same address) = 11 renames total.
