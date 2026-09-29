# RE notes: `ProfileEditor` — the Create/Edit Rider Profile screen's tab builder

Found as a byproduct (2026-07-22) while chasing a loose end from
`RE_NOTES_boardselect_screen.md`: two "sibling" vtables had turned up
immediately adjacent to `BoardSelectScreen`'s own in memory, and rather than
leave their identity as "different, unrelated, not investigated," traced
their constructors via `xrefs_to` on the **vtable base addresses** (not the
destructor code, which all classes in this family enter through a
near-identical shared shape and so doesn't distinguish them). Each
constructor turned out to have exactly 1 caller — and bounding those callers
(both previously unbounded) revealed a whole 5-case switch statement Ghidra
auto-recognized as `switchD_000895dd`, which turned out to be a completely
separate, substantial subsystem in its own right: **SSX Tricky's Create/Edit
Rider Profile screen**, building one of 5 tab widgets depending on which tab
is selected.

**This cleanly resolves the original question**: the 2 "sibling" vtables
near `BoardSelectScreen` are not related to it at all — they're 2 of this
screen's 5 tabs (Profile and TrickBook), which merely happen to share the
same generic base-widget class and allocation-tag convention (`f3st...`)
that `BoardSelectScreen`'s own builder (`f3BoardattribGrp`/etc.) also uses,
since both are "frontend screen 3" family UI.

## The tab dispatcher

**`ProfileEditor_BuildTabWidget`** (was `FUN_000895d4`, needed
`/create_function` — Ghidra had already auto-split 4 of its 5 case bodies
into separate `caseD_1`..`caseD_5` functions before the entry point itself
was created, and correctly re-merged them once it was) — a clean 5-way
switch on an `EAX` selector, dispatched via a real jump table
(`0x000897a8`):

| Selector | Tag | Size | Tab |
|---|---|---|---|
| 0 | `f3stoutfit` | 0x50 | Outfit |
| 1 | `f3stboard` | 0x88 | Board |
| 2 | `f3stprofile` | 0x500 | Profile (stats/bio — by far the largest) |
| 3 | `f3sttkbk` | 0x108 | TrickBook (moderate confidence on the name) |
| 4 | `f3stusrname` | 0x6c | Username |

Each case allocates its tagged buffer, constructs the matching tab widget,
then copies 3 bytes (`+0x25`/`+0x26`/`+0x27`) from the caller's own stack
frame into the new widget — likely propagating the currently-selected
character/team context into the tab (the same `+0x27` tag-byte offset
`BoardSelectScreen` uses for its own team tag, though here it's just this
base widget class's own generic field — not evidence of a direct
relationship between the two screens beyond shared UI infrastructure).

**No static caller found for the dispatcher itself** — needs dynamic
analysis or a wider search, consistent with this project's documented
systemic pattern for similar screen-selector dispatchers (e.g. `LessonMan`'s
own per-frame Update slots, `BoardSelectScreen_TickPositionAnimation`). The
higher-level "which settings screen is currently active" driver that feeds
the `EAX` selector is still open.

## The shared base widget

**`ProfileEditor_TabWidgetBase_Construct`** (was `FUN_00085be0`, called
first by all 5 tab constructors) — initializes a circular self-linked list
head (the same `NodeBase`-family idiom used throughout this project), a
handful of small state fields, then allocates its own embedded 0xc0-byte
`"f3strtgrp"` (string/text-group) sub-widget and installs a fixed vtable on
it — a generic label-container every tab embeds equally, not something
tab-specific. (This resolves what `"f3strtgrp"` actually is: not a distinct
class of its own, just this shared base's own internal child widget.)

## The 5 tab constructors (and 2 destructors)

- **`ProfileEditor_OutfitTab_Construct`** (was `FUN_00087b00`) — trivial
  beyond the shared base construct: own vtable (`0x00198358`), one zeroed
  byte field.
- **`ProfileEditor_BoardTab_Construct`** (was `FUN_00088c60`) — own vtable
  (`0x001986b0`), zeroes a small block of fields (likely the selected
  board's attribute/stat display fields).
- **`ProfileEditor_ProfileTab_Construct`** (was `FUN_00087a90`) — own
  vtable (`0x001982e8`). This is one of the 2 originally-mysterious
  "sibling vtables."
- **`ProfileEditor_TrickBookTab_Construct`** (was `FUN_000879c0`) — own
  vtable (`0x00198278`), sets 5 additional zeroed fields beyond the shared
  base construct (not traced further). This is the other original
  "sibling vtable." Moderate confidence on the "TrickBook" reading of the
  `"tkbk"` tag abbreviation — a plausible trick-list/reference tab
  alongside outfit/board/profile/username in a rider-customization flow;
  "Trackback" was considered and rejected as a worse contextual fit.
- **`ProfileEditor_UsernameTab_Construct`** (was `FUN_00087c50`) — own
  vtable (`0x001984a8`), sets a small handful of fields including a default/
  active-field-index flag (likely for a text-entry widget).
- **`ProfileEditor_ProfileTab_Destructor`** (was `FUN_00087ab0`) and
  **`ProfileEditor_TrickBookTab_Destructor`** (was `FUN_00087a30`) — both
  standard `ScalarDeletingDestructor` shapes; the TrickBook tab's makes one
  extra call (`FUN_00129e00`, not identified) the Profile tab's doesn't — a
  confirmed, genuine difference between the two classes, not just a
  coincidental resemblance from sharing a base class.

## Still open

- **The dispatcher's own caller** — what feeds the `EAX` tab selector and
  when this screen is entered/active. Needs dynamic analysis or a search
  from a different angle (e.g. a menu-state enum already named elsewhere).
- **The Outfit/Board/Username tabs' own destructors** — not individually
  found this pass (only the Profile and TrickBook tabs' destructors were
  traced, since those were the ones already reached via the original
  "sibling vtable" investigation).
- **`FUN_00129e00`** — the extra call the TrickBook tab's destructor makes
  that the Profile tab's doesn't. Not identified.
- **Each tab's own event-handling/tick vtable slots** — only the
  constructors and 2 destructors are named; the rest of each tab's ~14-slot
  vtable (matching the shape `BoardSelectScreen`'s own vtable had) is
  unexplored. A natural follow-up if this screen is revisited.

9 renames total.
