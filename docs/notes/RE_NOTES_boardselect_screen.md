# RE notes: `BoardSelectScreen` — the character/board/team selection screen

Picked as a fresh direction (2026-07-22), per explicit user request, right
after the `.inp`/`LessonMan` step-machine thread wrapped up. `BoardSelectScreen`
had only ever had 3 functions named — a byproduct of an unrelated
`FEStateMCOverlay` investigation in `RE_NOTES_player_snapshot_system.md` — with
no dedicated pass and no RE_NOTES file of its own, despite clearly being a
substantial, real UI screen (matches the already-named `UI_BuildBoardSelect`
screen builder, tagged `f3BoardattribGrp`/`f3bar`/`f3boadType`/`f3wsngltxt`/
`f3wtxtentry`).

## Finding and reading the class's own vtable

The 3 known methods (`HandleTeamCommit`/`HandleSelectionEvent`/
`HandleTrickPreviewSelect`) all shared field offsets, so their vtable-slot data
xrefs were the natural way in: `xrefs_to` on each address found single `DATA`
references at `0x001982b0`, `0x001982e0`, `0x001982d0` respectively — 12 bytes
apart, confirming a real vtable at base `0x001982b0`. Read the surrounding
region directly via `/read_bytes` (non-mutating) to map the whole thing, and
found 2 near-identical **sibling vtables immediately adjacent**
(`0x198278`-`0x1982ac` and `0x1982e8`-`0x19831c`) that turned out, on close
comparison, to be a **completely different, unrelated widget class** — not 2
more instances of `BoardSelectScreen` as first suspected. Confirmed this by
checking `xrefs_to` on every slot individually before naming anything:
`BoardSelectScreen`'s real vtable is the one 14-slot block in the middle
(`0x001982b0`-`0x001982e4`), and several of ITS OWN slots also turned out to be
shared generic `Widget` infrastructure (not exclusive to this class) —
correctly left unnamed rather than force-attributed.

## The 5 newly-named methods (8 total with the pre-existing 3)

- **`BoardSelectScreen_CommitTeamSelectionAndStream`** (was `FUN_00093260`,
  vtable slot 0) — called from `HandleSelectionEvent`'s codes-0-5 branch. Sets
  the selected index (`this+0x58`), maps the resolved team
  (`Team_GetPointerByTag`) to a per-team base offset via a 10-case switch (one
  entry per playable character/team), kicks off an async asset load/stream for
  the new selection, sets the busy-gate flag (`this+0x4c=1`) the other handlers
  check, and resets 5 child board-icon widgets to their default visual state.
- **`BoardSelectScreen_ExitAndStopPreview`** (was `FUN_000886d0`, vtable slot 1,
  confirmed exclusive via `xrefs_to`) — stops any in-progress trick-preview
  video, resets its handle, restores a widget field, and defers to a generic
  base handler — this screen's "leaving/closing" method.
- **`BoardSelectScreen_TickPositionAnimation`** (was `FUN_00093420`, vtable
  slot 3, confirmed exclusive — genuinely vtable-dispatched only, zero static
  callers, consistent with this project's documented systemic pattern for
  generic per-frame Update slots needing dynamic analysis to pin down exactly
  when it fires) — computes screen-space transform offsets for the 3D board/
  character preview elements based on the current selection, clears the
  busy-gate flag, and ends with an odd tail-call into the already-named
  `UI_BuildPauseMenuText` (not chased further — likely just a loosely-reused
  generic text-refresh utility, not a real pause-menu connection).
- **`BoardSelectScreen_HandleSecondaryEvent`** (was `FUN_00088750`, vtable slot
  7, confirmed exclusive) — same busy-gate check and the same event-code range
  (7-11) as `HandleSelectionEvent`'s trick-preview branch, but reached via a
  different vtable slot entirely. Exact distinguishing trigger (which event
  source/dispatcher routes here instead of `HandleSelectionEvent`) not
  determined this pass.
- **`BoardSelectScreen_ConfirmSelectionAndEnterLesson`** (was `FUN_000887c0`,
  vtable slot 10, confirmed exclusive, needed `/create_function` — was
  unbounded, found only via the vtable read) — **the standout finding**. Sets
  `GameMode_Current=6` unconditionally — the exact value `LessonMan`'s own
  construction gates on — then transitions a global game-state field and exits
  the screen. When the board/character tag byte (`this+0x27`) equals 1, first
  resolves the team and runs a 2-player team-setup helper; otherwise proceeds
  straight to entering Lesson mode.

## Ties this session's two threads together

`BoardSelectScreen_ConfirmSelectionAndEnterLesson` is the concrete link between
this screen and the tutorial system investigated earlier this session (see
`RE_NOTES_tutorial_system.md`): the character chosen here — via `this+0x27`/
`Team_GetPointerByTag`, the exact same resolution chain
`BoardSelectScreen_CommitTeamSelectionAndStream` uses — determines which
character's `PTR_DAT_001b53f8` roster entry, and therefore which `.inp`
tutorial files, `LessonMan` loads once `GameMode_Current==6` takes effect.
Concretely: this is the screen where the player picks (e.g.) "Psymon," and
that choice is what makes `LessonMan` later build `"...psym%02d.inp"` paths
rather than some other character's.

## What's genuinely shared, not board-select-specific (correctly left unnamed)

- **Vtable slot 2** (`0x000876d0`) — checked `xrefs_to` before naming and found
  15+ data references across totally unrelated classes project-wide. A
  generic `Widget` transform-sync helper, not exclusive to this screen.
- **Vtable slots 4-6/9/11** — a shared generic no-op stub (`0xb5c10`, distinct
  from the similarly-shaped `0xb5cf0` stub seen extensively in
  `RE_NOTES_tutorial_system.md`'s `LessonMan` work).
- **Slot 13** (`0x001982e4`) reads as `0` — either a genuinely unimplemented
  slot or the real vtable is only 13 entries and this address belongs to
  separate adjacent data. Not investigated further.
- **`FUN_00068e60`** (the `this+0x27` tag-byte-to-array-lookup helper used by
  nearly every method here) — a trivial one-line index computation, but its
  actual calling convention looks inconsistent (decompiled as taking 2
  params, callers visibly pass only 1) — likely a decompiler artifact from an
  unusual calling convention. Not renamed, flagged for anyone revisiting this
  area.

## Immediate follow-up: chased the sibling-vtable identity lead further

Picked up the first "still open" item from above (per continued user
direction). Traced both sibling classes' constructors by searching
`xrefs_to` on their vtable base addresses (`0x00198278`/`0x001982e8`) rather
than the destructor code — found `FUN_000879c0`/`FUN_00087a90`, each with
exactly 1 caller (`0x00089686`/`0x000896d4`).

**Result: these are generic, reusable frontend "text group" container
widgets — NOT part of `BoardSelectScreen`, and not one single unidentified
class either.** Both constructors call a shared base-widget constructor
(`FUN_00085be0`) that allocates a `"f3strtgrp"`-tagged block — the same `f3`
tag-prefix convention `UI_BuildBoardSelect` uses (`f3BoardattribGrp`/`f3bar`/
etc.), confirming this is generic "frontend screen 3" infrastructure shared
across many different screens, not something exclusive to board-select.

Bounding one constructor's caller (`0x00089686`, previously unbounded) via
`/create_function` landed inside a **switch-statement case body** Ghidra
auto-recognized and labeled `switchD_000895dd::caseD_4` — case 4 of an
unrelated, larger, still-unbounded screen-builder switch (jump table at
`0x000897a8`, dispatching on a selector compared against a 5-value range via
`DEC EAX; CMP EAX,4; JA skip; JMP [table+EAX*4]`). That case allocates BOTH
a `"f3strtgrp"` widget (via `FUN_000879c0`) and a `"f3sttkbk"`-tagged buffer,
copying 3 bytes (`this+0x25`/`+0x26`/`+0x27`) from the case's own local
context into the new widget — the same `+0x27` offset `BoardSelectScreen`
uses for its own team/character tag byte, though here it's just the base
widget class's own generic field, not evidence of a `BoardSelectScreen`
relationship.

**This closes the immediate question** (confirmed: unrelated to
`BoardSelectScreen`, a different screen's widget) **without fully resolving
a new one** (which screen is `switchD_000895dd` building, and what are its
other 4 cases) — a well-scoped, concrete lead for a future fresh-direction
pick, not chased further this pass given it's a whole separate,
substantial-looking subsystem in its own right. Left `switchD_000895dd`
itself and its jump table's other 4 entries unbounded/uncharacterized.

## Second immediate follow-up: identified `switchD_000895dd`'s owning screen

Picked this straight back up per continued user direction. Bounded the
remaining 3 cases plus the switch's own entry point (`/create_function` at
`0x000895d4`, which correctly re-merged Ghidra's 4 auto-split case
functions into one). **This is SSX Tricky's Create/Edit Rider Profile
screen's per-tab widget builder** — 5 tabs (Outfit/Board/Profile/TrickBook/
Username), identified unambiguously via allocation tags
(`"f3stoutfit"`/`"f3stboard"`/`"f3stprofile"`/`"f3sttkbk"`/`"f3stusrname"`).
Renamed the dispatcher `ProfileEditor_BuildTabWidget` and all 5 tab
constructors plus 2 destructors (9 renames) — full writeup in the new
`RE_NOTES_profile_editor_screen.md`. **Fully closes this thread**: not
just "unrelated to `BoardSelectScreen`" but now a properly named, understood
subsystem of its own.

## Still open

- **What distinguishes `HandleSelectionEvent` from `HandleSecondaryEvent`** —
  both handle the same event-code range with similar logic shapes but are
  separate vtable slots; the dispatcher/event-source logic that routes to one
  vs. the other wasn't traced. Likely needs dynamic analysis (matches this
  project's systemic pattern for vtable-dispatch-only methods).
- **`BoardSelectScreen_TickPositionAnimation`'s exact invocation site** — like
  several other per-frame Update slots documented elsewhere in this project,
  it's vtable-dispatched with no static caller found; needs dynamic analysis.
- **The `UI_BuildPauseMenuText` tail-call** in `TickPositionAnimation` — not
  chased; unclear if it's a real connection or generic reused utility.

8 renames total (3 pre-existing + 5 this session). 1 additional function
boundary created (`switchD_000895dd::caseD_4`, Ghidra's own auto-generated
switch-case label, left as-is rather than force-naming without knowing the
owning screen).
