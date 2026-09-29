# RE notes: the Career Mode challenge/objective record system — a false-lead byproduct

**2026-07-21.** Found while chasing the `this+0x3a0` steering-angle field (see
`RE_NOTES_ai_path_system.md`): a full re-run of the `/search_bytes` displacement search
for `0x3a0` turned up 25 raw hits, most already ruled out as coincidental collisions.
Three of the remaining hits (`FUN_000e4720`, `FUN_000e48c0`, `FUN_000e5040`) landed in a
completely different, previously-untouched system — **confirmed unrelated to riders**:
here `+0x3a0` is a name-string field on a `0x4c`-byte-stride "challenge slot" record
array, not a rider's steering angle. Documented separately since it's a genuine, fresh
subsystem discovery, just not the one being searched for.

## The record shape

Each slot (`0x4c` bytes, indexed via a per-object current-index field) holds:

- `+0x398` / `+0x399` / `+0x39a` / `+0x39b` — 4 boolean gating flags, reset to `1`
  (default-eligible) and cleared progressively as checks fail — reads like an
  unlock → availability → completion progression.
- `+0x39c` — an int score/count/target value.
- `+0x3a0` — a name string buffer (~0x21–0x42 bytes depending on caller).

All access goes through a shared per-record vtable at `this+0x650`, with virtual query
slots seen at offsets `0xac`/`0xb4`/`0xbc`/`0xc`/`0x40`/`0xd4`/`0xf8` — a genuine
polymorphic "Challenge" interface (unlock-check / availability-check / completion-check
/ get-value / get-name style methods), not individually named this pass.

## The 3 functions named

- **`Challenge_RefreshEntryState`** (was `FUN_000e5040`) — per-slot refresh: resets the
  4 gating flags and clears the name buffer, then checks the 3 vtable conditions in
  sequence; on the first failure, clears the corresponding flag and returns early. If
  all 3 pass, fetches a score/count via vtable+0x40 and a name via vtable+0xf8. Called
  from 2 not-yet-named functions (`FUN_000e5380`, `FUN_000e55e0`) — likely a per-frame
  or per-menu-open refresh loop over all slots.
- **`Challenge_FormatScoreThresholdText`** (was `FUN_000e4720`) — formats one record's
  progress into a localized string: locked-state messages when the gating flags aren't
  set, otherwise `"<name> 50,000+ <suffix>"` or `"<name> <count> <suffix>"` depending on
  whether the target value exceeds 50000, falling back to a plain name-only message.
- **`Challenge_FormatObjectiveDescription`** (was `FUN_000e48c0`) — a large (~40-case)
  switch on a per-record challenge-type code (`this+0x44`), mapping each type to a
  distinct localized objective description (beat a rider's score/time, land a specific
  trick, reach a score threshold via `Challenge_FormatScoreThresholdText`, locked/
  unlocked/completed variants, etc.), gated by the same vtable-query pattern
  `Challenge_RefreshEntryState` uses.

## Honest scope — a UI text generator, not decoded further

This is clearly the Career Mode challenge/objective-list screen's text-generation
backend (matches the "career mode challenges" mechanic already flagged in
`reference_official_manual_terminology.md`). Not pursued further this pass because it
was a tangent from the AI-steering investigation that surfaced it:

- Most individual localization string-ID constants (the hex values passed to
  `Localization_ResolveString`) were not decoded.
- The calling UI/menu code itself (many call sites clustered around `0x00096xxx` and
  `0x000e5axx`) is **not yet bounded as proper functions in Ghidra** — `get_function_containing`
  returned nothing for several call addresses, meaning this region needs
  `/create_function` treatment (the same methodology used for the AI-path query API)
  before it can be traced further.

A well-scoped, concrete next thread if this area gets revisited — not a dead end.

3 renames total.

## Immediate follow-up: bounded the calling UI code, closing that gap

Both cluster gaps flagged above turned out to be missing function boundaries, not raw
data — the exact same class of problem this project has fixed before with
`/create_function`: each gap was a `RET` followed by `NOP` alignment padding, then a
genuine function prologue that Ghidra's auto-analysis simply never picked up. Bisected
each by reading raw bytes to find the padding-then-prologue pattern, and created 5 new
function boundaries:

- **`ChallengeMenu_HandleScreenEvent`** (was `FUN_00096630`) — found only via a raw
  **data** xref at `0x00198d34` (not a `CALL` instruction), confirming it's
  **vtable-dispatched**: a `Widget`-subclass virtual method for the challenge menu
  screen. Gated on a mode flag, dispatches a huge switch on the challenge-type code
  (`this+0x44` — the same field `Challenge_FormatObjectiveDescription`/
  `Challenge_RefreshEntryState` use, confirming this is the same challenge-record
  object), builds icon/text pairs via the already-named generic `Widget_*` framework
  calls, and repeatedly calls `Challenge_FormatObjectiveDescription`. Structural
  confidence — dozens of cases, not individually traced.
- **`ChallengeMenu_InitializeScreenState`** (was `FUN_000e5990`) — the screen's
  construction/entry setup: registers a state ID (`0x29a8`) via a vtable call, sets a
  header/title string.
- **`ChallengeMenu_RefreshDescriptionAndRedraw`** / **`ChallengeMenu_RefreshDescriptionLabel`**
  / **`ChallengeMenu_RefreshDescriptionWithTitle`** (were `FUN_000e5a30`/`FUN_000e5a70`/
  `FUN_000e5aa0`) — a small family of near-identical wrappers, each formatting the
  current challenge's description via `Challenge_FormatObjectiveDescription` and pushing
  it to a display widget via 1-2 vtable calls, differing only in whether they also
  trigger a redraw or compose an extra title string.

**Honest scope**: several more call sites to `Challenge_FormatObjectiveDescription` in
the immediate vicinity (~`0xe5b15`/`0xe5b75`/`0xe5bd5`/`0xe5c35`/`0xe5c94`) were not
individually bounded/named this pass — very likely more of the same small-wrapper
pattern, a quick, low-effort follow-up if this area is revisited again.

5 more renames (3 + 5 = 8 total for this whole challenge-system thread).

## Closed out the remaining wrapper cluster ("cover as much as possible" pass)

Bisected the remaining 5 gaps (same RET+NOP-padding+prologue pattern used throughout
this project) and confirmed the cluster properly terminates at the next already-
analyzed function (`0x000e5cd0`, a large ~0x43d-byte function, not part of this wrapper
family — not investigated). All 5 share the same "chain 2 localization strings, set via
vtable+0x34" shape as `ChallengeMenu_RefreshDescriptionWithTitle`, differing only in
which specific string IDs are composed — named as numbered variants
(`ChallengeMenu_RefreshDescriptionWithTitleAlt`/`Alt2`/`Alt3`/`Alt4`) rather than
invented distinct names, since the exact semantic difference between each string-ID
pairing isn't decoded. The last one (`ChallengeMenu_RefreshDescriptionWithSuffix`) has a
slightly different shape — a single `Localization_ResolveString` call with 2 extra
positional args rather than 2 chained calls, likely a suffix/pluralization-aware
variant.

5 more renames (8 + 5 = 13 total for this whole challenge-system thread). **This
cluster is now fully bounded and named — no more open gaps in this specific area.**
