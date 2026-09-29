# RE notes: Front-end (menu) system function map

While chasing `FUN_0008ff00` (see `RE_NOTES_results_screen.md`), found that its UI
widgets are built through `FUN_00150d70(size, flags, "f3...")` — the same generic tagged
allocator seen elsewhere, here used pervasively across the *entire front-end menu
system* with 78 distinct `"f3..."` tags ("f3" = front-end screen 3 / menu manager).
Originally mapped every tag to its enclosing function via statistics only; **most of
that map is now verified live** (tag strings confirmed directly in each decompiled
body), covering essentially every menu screen in the game.

**Confidence level:** each renamed function below had its tag string(s) checked
directly in the live-decompiled body — a real confirmation, not a guess from proximity.
Most weren't read line-by-line for full internal behavior (that's still a good next
step for anyone continuing this thread), so treat the *category* as confirmed and the
*exact internal mechanics* as unread, except where noted otherwise (the 6 generic
widget helpers below were read in enough detail to describe their actual construction
pattern, not just tag-matched).

## Map (verified — tag confirmed live in the decompiled body)

### DVD / Jukebox menu (the "DVD featurette" extras mode from the port notes)
- `UI_BuildJukeboxList` (was `FUN_0009a020`) — f3DVDLeftJukeLst, f3DVDPlayArtist/Title, f3DVDRightJukeLst, f3DVDcurPlayArtist/Title, f3DVDjukeLst, f3dtxtTRACK
- `UI_BuildJukeboxVoicePlayer` (was `FUN_0009c250`) — f3DVDTalentName, f3DVDvoiceTXT, f3DVDvoicesENTRY, f3JukeplayDVD
- `UI_BuildDVDCharacterList` / `UI_BuildDVDCharacterList_Alt` (were `FUN_0009a900`/`FUN_0009ac00`) — both f3DVDcharLst, two call-site variants
- `UI_BuildDVDIconList` (was `FUN_000999e0`) — f3DVDicon, f3DVDtxt, f3DVDOnecharLst

### Character / board / track select
- **Correction (earlier session):** `FUN_0007daa0` is NOT a select-screen widget
  builder — it's the **front-end boot/initialization function** (`FEInit_Boot`), which
  incidentally sets up placeholder resource slots for board/track assets as part of
  one-time FE setup and kicks off the whole level-scripting system via
  `Script_PlayByName("FEStartScript")`. See `RE_NOTES_level_script_system.md`.
- `UI_BuildCharacterSelect` (was `FUN_0007e680`) — f3stcharsel
- `UI_BuildBoardSelect` (was `FUN_000929f0`) — f3BoardattribGrp, f3bar, f3wtxtentry, f3boadType, f3wsngltxt
- `UI_BuildPlayerCountSelect` (was `FUN_0007e710`) — f3stnumply

### Rider bio / profile screen
- `UI_BuildRiderBioEntry` (was `FUN_0008d2a0`) — f3drdrBioTXTNTRY, f3drdrBioTxt
- `UI_BuildRiderBioText` / `UI_BuildRiderBioText_Alt` (were `FUN_0008f9f0`/`FUN_0008fc80`) — both f3rdrbiotxt, two call-site variants
- `UI_BuildRiderProfile` (was `FUN_00092630`) — f3rdrPrflGrp, f3wsngltxt

### Pause / stop menu
- `UI_BuildPauseMenu` (was `FUN_000a3260`) — f3stopmnu, f3stopname, f3stopwgrp. Complex
  (multiple `unaff_*` decompiler-confusion registers), renamed at tag-match confidence,
  internals not fully read.
- `UI_BuildPauseMenu_Small` (was `FUN_000a1ac0`) — f3stopsmlar (a smaller/alternate pause
  menu variant)
- `UI_BuildPauseMenuText` (was `FUN_00085d50`) — f3stopt

### Rank / medals / results (sibling of `UI_BuildResultsScreen`)
- `UI_BuildRankIcons` (was `FUN_00089980`) — f3charrankICN, f3currRankICN, f3rdrranktxt, f3boadType
- `UI_BuildLeaderboard` (was `FUN_0008ea10`) — f3colmntitle (shared with `UI_BuildResultsScreen`), f3snglntxtRANK, f3wlstWCrank, f3diconSLDR — the actual rank/leaderboard list screen

### Name entry (high-score initials entry, etc.)
- `UI_BuildNameEntry` (was `FUN_0009b870`) — f3nameback1/2/3, f3usrName

### Resolved: the 3 "unconfirmed tag" functions were never screen-tag owners

`FUN_00085ab0`, `FUN_00094a80`, and `FUN_000859c0` (previously left un-renamed because
no `f3` tag string appeared live in their bodies) turned out **not to be screen-specific
builders at all** — they're generic base-`Widget`-class internals that only showed up in
the original tag-proximity statistical pass by coincidence. Decompiling and cross-checking
them uncovered the whole doubly-linked child-list container framework underneath every
widget (offsets +8/+0xc = prev/next, +0x10 = list head, +4 = child type tag, +0xf0/+0xf4 =
two named child slots), fully verified live and renamed:

- **`Widget_UnlinkFromList`** (was `FUN_000a38b0`) — doubly-linked-list node removal.
- **`Widget_ReleaseChildSlotsAndUnlink`** (was `FUN_000a4740`) — releases the two named
  child pointers at +0xf0/+0xf4 via their vtable dtor (arg=1), then unlinks self.
- **`Widget_BaseDestructor`** (was `FUN_00085ab0`) — the shared non-freeing base
  destructor, referenced only from 3 vtables (`0x197e78`/`0x197f18`/`0x198d40`): swaps
  vtable, releases children + unlinks, swaps to base vtable, unlinks again (two separate
  list memberships).
- **`Widget_ScalarDeletingDestructor`** (was `FUN_000859c0`) — same base-dtor body plus
  the standard scalar-deleting-destructor epilogue (conditional pool/heap free based on a
  flags-byte low bit), matching the deleting-destructor pattern already documented
  elsewhere in the codebase.
- **Cross-reference (later session):** `Widget`'s own vtable places a per-frame
  "Update"-style virtual method at the same slot index (offset `+0x1c`) as the
  completely separate `Node`/`NodeBase` game-object hierarchy's own Update slot —
  confirmed while chasing the "who calls Update generically" question for `Node`.
  Two independent class families, unrelated to each other, that happen to share
  this engine-wide convention. See `RE_NOTES_node_base_class.md`.
- **`Widget_FindChildByType`** (was `FUN_000a3990`) — walks a widget's child list looking
  for the first child whose type-tag (+4) matches.
- **`Widget_GetChildAtIndex`** (was `FUN_000a3940`) — walks N steps into a widget's child
  list.
- **`Widget_ResetIconTextPairs`** (was `FUN_00094a80`) — finds the child-type-0xb
  container, loops up to 4 elements resetting/hiding a paired icon+text sub-widget per
  element. Called from several screen-level vtable handlers, likely the name-entry or DVD
  icon-list widgets.
- **`Widget_SetChildVisibilityMask`** (was `FUN_00085000`) — generic "show/hide subset of
  siblings by bitmask" helper, called with screen-specific magic masks (e.g. `0xb00`/
  `0x500`).

This is a genuinely useful, reusable finding for a PC port: the entire widget tree is a
doubly-linked child list with typed lookup, not an array — any port needs to replicate
that traversal semantics, not just the visual layout.

### Credits
- `UI_BuildCreditsScreen` (was `FUN_0009e630`) — f3crdts, the only function tagged for
  this screen, confirmed as the whole credits screen.

### Generic widget helpers (the "leaf" UI primitives everything else composes from)

**6 read in real detail** (not just tag-matched — actual construction pattern
described):
- **`UI_BuildButtonGroup`** (was `FUN_00086170`) — f3dBUTTON, f3buttns. Creates an
  `f3buttns` container widget, then walks a bitmask lookup table (`DAT_001b9d00`, one
  entry per button) spawning an individual `f3dBUTTON` widget for each set bit in the
  caller's button mask — the generic "show these N buttons" builder.
- **`UI_BuildDialog`** (was `FUN_00085070`) — f3dlg, a straightforward dialog-box
  container builder.
- **`UI_BuildListWidget`** / **`UI_BuildListWidget_Alt`** (were `FUN_000870b0`/
  `FUN_000871a0`) — both f3wlst, confirmed genuinely two call-site variants of the same
  widget type.
- **`UI_BuildSingleLineText`** (was `FUN_00088570`) — f3wsngltxt, confirmed as one of
  (at least) 7 call sites this tag has; the others (`FUN_0009e2f0`/`FUN_0009f080`/
  `FUN_000a0620`/`FUN_000a2280`) not individually checked, presumably the same shape.
- **`UI_BuildIcon`** (was `FUN_000a7b40`) — f3icon, a small, clean icon-widget builder.

**4 still un-renamed** — read live but too complex/decompiler-confused to name
confidently (all show multiple `unaff_*` register warnings and/or "type propagation not
settling"/"heritage after dead removal" Ghidra warnings — genuinely larger, more
tangled functions than the ones above, not just unread):
`FUN_0008ae80` (f3bufsngltxt/f3tbslider/f3icnTrick), `FUN_00086320` (f3hlptxt/f3wgrp/
f3frmtl/f3icn), `FUN_00099690` (f3TitleHlp), `FUN_0008cdf0` (f3dltrbox).

## Status: complete

26 of the ~30 originally mapped screen-builder functions are renamed and tag-verified
live, plus 8 more functions from the generic Widget child-list framework uncovered while
resolving the last 3 "unconfirmed tag" cases (see above) — 34 renames total from this
thread.

**Update (later session): named the last 4.** All four (`UI_BuildTrickSelectPanel`,
`UI_BuildHelpOverlay`, `UI_BuildTitleHelp`, `UI_BuildDeleteRiderConfirm`) genuinely
are too decompiler-mangled to verify behaviorally (11-level pointer indirection
artifacts, "type propagation not settling"/"heritage after dead removal" warnings) —
but the same tagged-allocator string technique this whole project relies on still
works even when the surrounding logic doesn't decompile cleanly, so all 4 were named
at tag-confirmed (not full-behavioral) confidence:

- **`UI_BuildTrickSelectPanel`** (was `FUN_0008ae80`) — `f3bufsngltxt`/`f3icnTrick`/
  `f3tbslider`: a trick-icon list with a tab slider, the trick-select browsing panel.
- **`UI_BuildHelpOverlay`** (was `FUN_00086320`) — `f3frmtl`/`f3hlptxt`/`f3icn`/
  `f3wgrp`: generic help-overlay chrome (frame title + help text + icon + widget
  group), likely reused across several screens.
- **`UI_BuildTitleHelp`** (was `FUN_00099690`) — `f3TitleHlp`, matching an earlier
  session's bookmark note exactly.
- **`UI_BuildDeleteRiderConfirm`** (was `FUN_0008cdf0`) — `f3dltrbox`/
  `f3drdrprflback` ('delete box' + 'rider profile background'): a delete-rider
  confirmation dialog over the profile background screen. `f3drdrprflback` is a
  newly-found tag not previously catalogued anywhere in this project.

**Every function originally flagged in this investigation is now named.** The
frontend menu-map thread is genuinely complete, not just "essentially" complete.

## Update: mapped Widget's own real vtable methods

Only `Widget_BaseDestructor`/`Widget_ScalarDeletingDestructor`/
`Widget_ReleaseChildSlotsAndUnlink` were previously named (the destructor
family). Read the other 11 real vtable slots (Widget appears in 3 vtables at
`0x00197e78`/`0x00197f18`/`0x00198d40`, all sharing the same slot layout) and
found a coherent, previously undocumented **property-cascade system**:

- **`Widget_SetFlagA`**/**`Widget_SetFlagB`**/**`Widget_SetRect`**/
  **`Widget_AddOffset`**/**`Widget_PlaySound`** — each writes a local field,
  then forwards the identical call to up to 2 **named child widgets**
  (`+0xf0`/`+0xf4`) via their own vtable. A widget-tree property cascade: set
  a value on a container and it propagates to its children automatically.
- **`Widget_UpdateTransitionAnimation`** — a 6-state state machine
  interpolating a position field, the widget slide/fade transition animation.
- **`Widget_UpdateLayout`** — the largest function in the batch, heavy
  float/vector math; reads as the widget's own per-frame layout computation.
- **`Widget_ProcessState`**/**`Widget_TriggerIfReady`**/**`Widget_TickWrapper`**
  — smaller dispatch/gating helpers.
- **`Widget_ReleaseNamedChildPair`** — same shape as the already-documented
  `Widget_ReleaseChildSlotsAndUnlink`, but operates on a *different* named
  child-pointer pair (`+0x3c`/`+0x3d`, not `+0xf0`/`+0xf4`) — confirms a
  single `Widget` can own more than one independently-managed named
  child-pair slot.

11 renames, structural confidence throughout (not byte-exact on every
field's precise game-visible meaning, but the cascade pattern and animation
state machine are clearly evidenced). Good next step if continuing: a full
internal behavioral read of any specific screen that matters most for a
port — the category-level mapping (both the screen builders and now
Widget's own event system) is solid enough that reading any one screen's
full logic should now be straightforward.

## M5a — the Widget base class, read and ported (2026-07-28)

Read the traversal helpers and the constructor rather than more tag-matching,
because every screen composes from these.

### Layout, from `Widget_FindChildByType` / `Widget_GetChildAtIndex`
```
+0x04  u32   type tag       -- what FindChildByType matches
+0x08  prev  pointer
+0x0c  next  pointer
+0x10        -> the child list
+0xf0  i32   = -1 at construction
+0xf4  u32   = 0  at construction
+0xf8  float caller-set scale (established earlier by UI_BuildTitleHelp)
```

The child list is a **circular doubly-linked list with a sentinel**. Both
walkers bail out when a node's `next` *or* `prev` points at itself -- that is
the sentinel test, and it is why the port models a list rather than a vector.

### Construction (`0x000858a0`)
Sets the vtable (`PTR_FUN_00197d38`), writes `+0xf0 = -1` and `+0xf4 = 0`, then
pushes a **10.0 x 10.0** default pair through a virtual call. Allocation is
`FUN_00150d70(tag, 0x100, 0)` -- **widgets are 256 bytes** -- poisoned with
`0xdeadc0de` before the constructor runs. `UI_BuildIcon` shows the pattern
concretely: allocate `"f3icon"`, construct against the parent's `+0xd4`
context, set `child+0x60 = 1`, store the child pointer at `+0x128`/`+0x12c`.

Ported as `port/src/ui/widget.{h,cpp}` with the traversal semantics
reproduced, including the self-referencing-node bail-out. Asserted in
`asset_test`: first-match ordering, index walk, past-the-end, the
`Widget_SetChildVisibilityMask` bit-per-child rule, and the constructor
defaults.

### What this does NOT cover
The concrete screen builders are still tag-confirmed only. This is the base
class they all use, not the screens themselves -- so M5's size still depends on
reading those ~20 builders.

## M5a continued: the property cascade and transition animation, PORTED

### CORRECTION to the earlier M5a entry
That entry recorded `+0xf0` as an `id` (= -1) and `+0xf4` as `flags` (= 0),
taken from the constructor. **Wrong.** They are the two **named child
references** that drive the engine's property cascade; -1 and 0 simply mean
"unset". Corrected in the port and in the tests.

### The cascade (`Widget_SetFlagA` 0x000a46f0, `SetRect` 0x000a4530, ...)
```c
void Widget_SetFlagA(this, v) {
    *(char*)(this + 0x10) = v;
    (*this[0xf0]->vtable[0x0c])(v);            // UNCONDITIONAL
    if (this[0xf4]) (*this[0xf4]->vtable[0x0c])(v);
}
void Widget_SetRect(this, r) {
    memcpy(this + 0x50, r, 16);
    (*this[0xf0]->vtable[0x24])(r);            // UNCONDITIONAL
    if (this[0xf4]) (*this[0xf4]->vtable[0x24])(r);
}
```
Set a property on a container and it propagates down the named-child chain
automatically. `namedA` is dereferenced **without a null check** -- so a widget
with cascading behaviour must have it assigned by its builder. The port
null-checks it anyway, and says so at the call site, because a half-built tree
in a test harness would otherwise fault.

Field map recovered from these: `+0x10` flagA, `+0x50..0x5c` rect.

### The transition animation (`Widget_UpdateTransitionAnimation` 0x000a3f10)
A state machine on `+0x14`:
* **state 2** -> ADDS the four `+0x30` deltas into the four `+0x40` values,
  drops to state 1;
* **state 4** -> MULTIPLIES, but only the **first three** -- `+0x4c` is left
  alone -- and drops to state 3;
* states 0/1/3/5 fall through to a common tail;
* any other value returns early **without** running the tail.

The "first three only" asymmetry in state 4 is the kind of detail that would
silently drift in a reimplementation, so the port asserts it explicitly.

Ported into `port/src/ui/widget.{h,cpp}`; asserted in `asset_test`: the
recursive cascade through a grandchild, the rect cascade, and both transition
states including the untouched fourth component.

## M5b: `UI_BuildButtonGroup` and its table — DECODED AND PORTED

`UI_BuildButtonGroup` (0x00086170) takes a **bitmask** and walks the 17-entry
table at `DAT_001b9d00` (stride 12, ending at `0x001b9dcc`), spawning one
`"f3dBUTTON"` widget (**0x130** bytes) per set bit into an `"f3buttns"`
container (**0x170** bytes). Two behaviours worth reproducing exactly:
* buttons come out in **table order**, not caller order;
* the builder **clears each bit as it consumes it** (`mask &= ~bit`), so a bit
  can only ever produce one button.

Row 0 has a zero mask and is skipped by the engine's own `*p != 0` guard -- a
sentinel, not a button.

### The table, with every label resolved against `american.loc`
| bit | icon | id | label | bit | icon | id | label |
|---|---|---|---|---|---|---|---|
| `0x0001` | 0x3e | 530 | next | `0x0100` | 0x3f | 525 | cancel |
| `0x0002` | 0x3e | 536 | **select** | `0x0200` | 0x3e | 535 | save |
| `0x0004` | 0x3e | 3325 | play video | `0x0400` | 0x3e | 529 | load |
| `0x0008` | 0x3f | 533 | previous | `0x0800` | 0x40 | 527 | delete |
| `0x0010` | 0x3e | 538 | yes | `0x1000` | 0x40 | 528 | format |
| `0x0020` | 0x3e | 531 | no | `0x2000` | 0x40 | 532 | options |
| `0x0040` | 0x3e | 526 | continue | `0x4000` | 0x3e | 524 | accept |
| `0x0080` | 0x3e | 534 | retry | `0x8000` | 0x40 | 537 | tutorial |

The icon field takes exactly three values -- `0x3e`, `0x3f`, `0x40` -- i.e.
three button-icon sets.

### Cross-check that closes the loop
`UI_BuildTitleHelp` was already known (from the boot-sequence work) to call
`UI_BuildButtonGroup(2, 0)`. Mask `2` resolves to a single button: **"select"**
-- which is exactly what the title screen shows. Two independent pieces of
earlier work agreeing.

Ported as `port/src/ui/buttongroup.{h,cpp}`. `asset_test` re-resolves all 16
ids against the real `american.loc` (**16/16 match**), checks the title mask,
and checks that a multi-bit mask emits in table order.

## M5c: the widget TYPE CATALOGUE and the roster, ported

### 78 widget types with their allocation sizes
Every front-end widget goes through `FUN_00150d70("<tag>", <size>, 0)`, so a
sweep of those call sites gives the complete type list. **78 distinct
(tag, size) pairs**, sizes from `0x10` (`f3bigtex`) to `0x1580` (`f3hlptxt`).

Two tags carry **two** sizes -- `f3DVDtxt` (0x110 / 0x310) and `f3wsngltxt`
(0x110 / 0x310). That is not an extraction artefact: this file already recorded
that `f3wsngltxt` has "at least 7 call sites", and the pairs are a plain and an
extended variant. Both are kept.

Cross-checked against sizes read directly out of six builders --
`f3icon` 0x100, `f3dBUTTON` 0x130, `f3buttns` 0x170, `f3dlg` 0x150,
`f3stcharsel` 0x14c, `f3stnumply` 0x4c -- **6/6 agree**.

Ported as `port/src/ui/widgettypes.{h,cpp}`.

### `UI_BuildCharacterSelect` (0x0007e680)
Small and clear: allocate `f3stcharsel` (0x14c), then -- gated on
`(DAT_001df3f4 & 3) == 0`, the same boot-state global the title/intro sequence
uses -- walk **4 player slots** at `screen+0x64` (stride 4), calling a
per-slot builder for any slot whose `+0x3c` byte is clear, and bail early if
`screen+0x26` got set. Otherwise it falls through to a single call with `-1`.
So the screen is inherently 4-player-aware.

### The roster, ported in the engine's own index order
`LoadScreen_FormatRiderTextureName` (0x0012fc20) switches 0..0xb:
**0 eddie, 1 kaori, 2 luther, 3 mac, 4 moby, 5 zoe, 6 jp, 7 elise, 8 psymon,
9 seeiah, 10 brodi, 11 marisol.**

Independently re-confirmed while porting, without re-deriving it: all 12 names
appear in `american.loc`, all 12 have both `<name>_body.mxf` and
`<name>_head.mxf` in `char/mdlxbx.big` (**12/12 in the test**), and `marty`
appears in neither -- consistent with the already-settled finding that this is
the NTSC build and Marty is the PAL swap for index 3.

Ported as `port/src/game/roster.{h,cpp}`, with the Mac/Marty note recorded at
the point of use so it is not re-investigated.
