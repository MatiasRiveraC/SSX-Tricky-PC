# RE notes: in-race HUD overlay

Found while chasing the open question of what consumes the rider stat at `+0x5710`
(already known to be the Showoff-mode sort key in `Race_ComputeRankings`, see
`RE_NOTES_level_script_system.md`). Grepped the flat `default.xbe.c` export for the
literal offset `0x5710` and found a hit inside a large, then-unexplored function —
decompiled it live and it turned out to be the master in-race HUD compositor.

## `HUD_DrawRaceOverlay` (was `FUN_000c6730`)

`void __thiscall HUD_DrawRaceOverlay(float param_1, int param_2, float param_3,
float param_4, float param_5, float param_6)` — one big function, gated internally by a
bitmask read from `*(rider + 0xfc)` (a per-rider "which HUD elements are visible" flag
field), one `if` block per HUD panel. Each panel reads its own screen-position/style
constants from parallel `DAT_001e87xx`-`DAT_001e8bxx` tables indexed by a global "which
HUD layout/player slot" selector (`DAT_001e9418`). Confidence noted per panel — all
panels below were read directly, not guessed from proximity:

| Flag bit (`+0xfc`) | Panel | Confidence |
|---|---|---|
| `0x200` (appears twice — two separate draw passes) | **Rank/position indicator** ("1st", "2nd", ...) — reads `rider+0x140`, the exact field `Race_ComputeRankings` writes the sorted rank into. `iVar11 == 1` (i.e. rank 1) gets different tint constants than other ranks. | **Confirmed** — direct `+0x140` read matches the known rank field exactly. |
| `1` (also requires a resource-context mode field `!= 3`) | **Race time display** — computed via `Race_GetClampedRoundedTime` (Showoff picks the smaller of two candidate times, other modes the larger — a "best so far" vs "current attempt" comparison), formatted via `HUD_FormatRaceTime`. | **Confirmed.** |
| `0x20000000` | **Time-gap display** — a delta between two time values, formatted via `HUD_FormatTimeGap` (signed mm:ss.cc). Likely the on-HUD gap-to-leader/rival readout (separate render path from the popup `HUD_ShowTimeGapCallout`). | **Confirmed formatting, panel purpose probable.** |
| `0x800` | **Speed readout** — computes `sqrt(vx^2+vy^2+vz^2+w^2)` from a 4-float vector at `rider+0x180..0x18c`, formats the rounded value via `CRT_FormatString("%d", ...)`, appends `" km/h"` or `" mph"` depending on a units flag (`DAT_001dd83c`). | **Confirmed.** |
| `8` | **Score display** — `CRT_FormatString(buf, "%d", *(rider+0x5710))`. This is the field that answers the original open question: `+0x5710` is drawn directly as an on-screen integer score. | **Confirmed — this closes the original investigation.** |
| `0x400`, `0x100000`, `0x400000` | Three panels that all walk the same small "event slot" array at `rider+0x577c` (up to 12 slots, 5 ints/slot: type tag, a couple of value fields, a ratio numerator/denominator pair). Type `0xd` (bit `0x400`) draws a fraction-based progress value with a lap-count label (`Localization_ResolveString(0x10f)`="**FINAL LAP**" or `(0x110)`="**%d LAPS TO GO**" depending on `piVar6[1]==2`) — **the lap-counter HUD element.** Type `0xc` (bit `0x100000`) draws a colored number, tinted per `piVar6[1]` value (2/3/5). Type `0xb`/`0xa` (bit `0x400000`) draws a signed delta number with `+`/`-` prefix — a **floating score-gap number popup.** | **Confirmed via the real `.loc` string data — see below.** |
| `0x20000` | **"UBER TRICK" availability announcement** — fades in a player portrait/icon (`UI_GetPlayerIconSlot`) plus, when a per-rider flag bit is set, the label `Localization_ResolveString(0xdf8)` = **"UBER TRICK"** drawn via `Text_DrawCenteredVertically`, based on a countdown timer at `rider+0x24`. This is the on-screen announcement that the Über Trick move has become available/is charging. | **Confirmed** — string id `0xdf8` = `"UBER TRICK"`. |
| `0x100` | Iterates `rider+0x24` entries from an array at `rider+0x8c`, each paired with a float; for entries with a small type value (0/1/2) draws one of 3 localized strings via `Localization_ResolveString`: id `0xe37`=**"CHECKPOINT"**, `0xe38`=**"FINISH"**, `0xe3c`=**"LAP"**. | **Confirmed — this is the checkpoint/finish/lap status banner, not a trick-rating popup as originally guessed.** |
| `0x1000000` (further gated on a resource-context field `== 1`) | A multi-case medal-progress status panel switching on a mode value (0-3): case 0 draws `"BRONZE"` (`0xe47`) + a formatted number; case 1 draws `"BRONZE"`+`"FOR SILVER"` (`0xe3b`) + a number; case 2 draws `"SILVER"` (`0xe50`)+`"FOR GOLD"` (`0xe3a`) + a number; case 3 draws `"GOLD"` (`0xe4a`) alone. Plus a `"MEDAL"` (`0xe3d`) label and a formatted float/time value (`+0x98`/`+0xf8`/`+0xf4`/`+0xf0` fields). | **Confirmed — this is the live medal-pace indicator** ("on pace for Silver, X seconds for Gold" style readout). |
| (final block, no single flag — gated on a resource-context sub-object's fields) | Draws a localized string id `0x1af` = **"Replay Full"** plus a count, only when a resource-context sub-object has a non-zero flag at `+0x40` and a count `> 0` at `+0x3c`. | **Confirmed — a replay-buffer-full warning indicator, not a pickup/coin counter as originally guessed.** |

## Formatting/support helpers, all confirmed live

- **`HUD_FormatRaceTime`** (was `FUN_000c9fa0`) — formats a millisecond time as
  `"%d:%02d.%02d"` (mm:ss.cc). Special cases: exactly `50000` → localized string id
  `0x156` = **"DNF"**, negative → id `0x181` = **"N/A"**, anything over `359999` ms
  clamped to display `"59:59.99"`.
- **`HUD_FormatTimeGap`** (was `FUN_000ca050`) — the signed sibling: same mm:ss.cc
  format but with a `"-"` prefix for negative gaps, no sentinel-string special cases,
  same `59:59.99` clamp.
- **`CRT_FormatString`** (was `FUN_0015cece`) — a thin wrapper around `FUN_0015f6e7`
  (an internal `vsprintf`-style variadic formatter, MSVC-CRT-shaped: fixed 0x7fffffff
  max-count, `__flsbuf` overflow-flush call) — i.e. a generic `sprintf`. `DAT_0018a07c`
  (a format string constant passed to it throughout the HUD/menu code) is literally
  `"%d"`.
- **`Localization_ResolveString`** (was `FUN_0014fec0`) — resolves an integer string ID
  (called throughout with constants like `0x156`, `0x181`, `0xe37`, `0xe38`, `0xe3c`,
  `0x10f`, `0x110`, `0x1af`, `0x156`, `0xdf8`) against a resource-context pointer
  (`DAT_001e3c7c+0x50`) into a usable string/handle, fed into text-setting calls
  (`FUN_000aa5c0`/`FUN_000aa490`/etc.). Internal logic involves a packed-byte checksum
  read before dispatch — not fully traced, but the calling convention and pervasive
  reuse across HUD and menu code make the localization-lookup interpretation solid.

## The 2D text/glyph rendering primitive cluster — also resolved

The low-level draw primitives used throughout `HUD_DrawRaceOverlay` (and presumably
other HUD/menu code) turned out to be a clean, self-consistent little module:

- **`Font_GetGlyphMetrics`** (was `FUN_000c2130`) — binary-searches a sorted
  per-character glyph metrics table (stride `0xc`/12 bytes) by character code, with a
  fast-path range check first. The foundation everything else here builds on.
- **`Text_MeasureStringAnsi`** / **`Text_MeasureStringWide`** (were `FUN_000c2940`/
  `FUN_000c2b60`) — walk an 8-bit or wide string respectively, look up each character's
  metrics via `Font_GetGlyphMetrics`, and accumulate a bounding box scaled by the font
  table's own scale factors. Used to size/center text before drawing.
- **`Text_DrawGlyphBuffer_Numeric`** / **`Text_DrawGlyphBuffer_String`** (were
  `FUN_000c22f0`/`FUN_000c23f0`) — near-identical low-level single-pass draw calls into
  a D3D-like device vtable (`DAT_001e3c7c+0x720`), differing only in which vtable slots
  they call — one for pre-formatted numeric glyph buffers, one for general
  string/localized-text handles.
- **`HUD_DrawTextShadowed`** / **`HUD_DrawTextShadowed_Alt`** (were `FUN_000c2700`/
  `FUN_000c2830`) and **`HUD_DrawNumberBufferShadowed`** (was `FUN_000c24f0`) — all
  three share the same two-pass pattern: save the current draw color, draw once offset
  by a couple pixels with color forced to black (a drop shadow), restore the real
  color, draw again at the true position. The three differ only in which low-level
  primitive they call and their exact parameter shape (Ghidra's decompiled signature
  for `HUD_DrawTextShadowed` itself is wrong — shows `void(void)` due to the same
  calling-convention confusion seen elsewhere in this codebase; call sites clearly pass
  color+x+y+handle).
- **`Text_DrawCenteredVertically`** (was `FUN_000c3070`) — measures a wide string via
  `Text_MeasureStringWide`, then draws it via `HUD_DrawTextShadowed_Alt` with the Y
  position offset by half the measured height.

9 renames, all verified live. This closes out the immediate HUD-drawing investigation
started by chasing `+0x5710` — every low-level call `HUD_DrawRaceOverlay` makes now
resolves to a named, understood function.

**Confirmed against the real font files** (`Game Data\data\fonts\*.ffn`, "FNTF" magic):
a 0x2c-byte header (magic, 2 unidentified dwords, then a first-char-code field and a
table-size field) followed by a flat, ascending-by-charcode glyph table with **exactly
the 12-byte (`0xc`) stride** `Font_GetGlyphMetrics` binary-searches over. Decoded the
per-glyph record layout by cross-checking which byte offsets `Text_MeasureStringAnsi`
actually reads (`+2`/`+3`/`+8`/`+9`/`+10`, all single bytes): `+0` u16 char code, `+2`/
`+3` glyph width/height, `+4`-`+7` (unused by measurement — almost certainly texture
atlas U/V coordinates, consumed by the draw path instead), `+8` advance-width, `+9`/
`+10` left-bearing/right-extent used for the bounding-box accumulation. A clean,
satisfying confirmation of a function documented purely from Ghidra decompilation.

## The remaining support helpers — also resolved (structural confidence)

- **`Text_DrawWordWrapped`** (was `FUN_000cf010`) — the general word-wrap text
  renderer: draws directly if the string fits `maxWidth`, otherwise wraps on spaces,
  measuring/drawing each line, honoring right-align and vertical-centering flag bits.
  **Confirmed.**
- **`Team_GetScoreTier`** (was `FUN_0006a920`) — same team-tag lookup as
  `Team_FindSlotByRiderTag`, then buckets a combined value from the matched team's
  struct into a 0-9 tier via a fixed threshold ladder — likely picks an icon/color for
  team standing. **Structural, plausible interpretation.**
- **`UI_GetPlayerIconSlot`** (was `FUN_0002dc20`) — scans a small array for the slot
  matching a given rider tag byte, returning a stored icon/resource handle. Called from
  the rank-change/new-best fade panel to fetch the player portrait. **Structural,
  plausible interpretation.**
- **`HUD_FindActiveOverlaySlot`** (was `FUN_000ca0f0`) — scans up to 19 slots for the
  first one with both a couple of nested fields non-zero; used at the very top of
  `HUD_DrawRaceOverlay` to pick a base opacity (1.0 vs 0.35) for the whole overlay.
  **Structural only — exact meaning of the slot array not traced.**
- **`HUD_FindComboEventSlot`** (was `FUN_0005dcb0`) — scans up to 12 tagged slots (same
  5-int-stride shape as the `+0x577c` array `HUD_DrawRaceOverlay` itself scans, but a
  different array at `+0x14c`) for type tag 7 or 8; gates the player-icon-fade vs
  `Text_DrawWordWrapped` branch. **Structural only — exact meaning of type tags 7/8 not
  confirmed.**

Not renamed: `FUN_000ca1e0`, a one-line vtable-forwarding call (`(**(this+0x28)->vtbl)[2]()`)
too thin to name with any confidence.

This closes out essentially everything reachable from `HUD_DrawRaceOverlay` short of
the actual localized string contents (would need a live string-table dump, not
attempted this round — confirmed unreachable statically, see below) and the true
game-semantic meaning of the remaining structural-only slot types
(7/8/0xa/0xb/0xc/0xd) — good next-step material if picked up again.

## Sibling function: `HUD_DrawWorldSpaceMarkers` — the trick/combo score-popup system

Found immediately adjacent to `HUD_DrawRaceOverlay` in address space (`0xc44e0`-`0xc6712`,
right up against `HUD_DrawRaceOverlay`'s own start at `0xc6730`) — same
`__thiscall(riderList, riderIndex)` signature, same per-rider `+0xfc` flags-bitmask
panel-dispatch pattern, but instead of 2D screen-space HUD elements it draws **billboards/
icons and floating localized text in 3D world space above riders**. Most of its ~14
panel branches are now decoded, and this turned out to be genuinely the highlight of
the whole HUD investigation — **it's the trick/combo score-popup and meter system**:

- **Combo counter with sparkle burst** (bits `0x1000`/`0x2000`/`0x8000`/`0x4000`) —
  draws a **"COMBO"** label (string id `0xe56`, a second copy at `0xe7a`) plus a tinted
  number (tint color depends on a type value `0x12`/`0x13` from a global slot array
  `DAT_001bafb8`), and when a threshold is crossed, a variable-count (0-6) sparkle/star
  burst via the cascading-fallthrough switch calling `Sprite_DrawIconByIndex`
  repeatedly. **Confirmed: this is the trick-combo counter**, not the Über meter as
  originally guessed — separate from the score-popup callouts below, it's the running
  "COMBO" multiplier readout with a celebratory sparkle effect when it increases.
- **Stacked score-popup callouts** (bits `0x4000000`/`0x8000000`/`0x10000000`) — three
  near-identical branches, each scanning a per-rider tagged-event array (`+0x5780`/
  `+0x5784`) for a specific type tag, drawing a two-line floating callout: a formatted
  point value plus a **tier-graded combo-multiplier label** resolved via
  **`Trick_GetScoreTier`** (was `FUN_0005d700`) — maps the trick's point value to a
  tier via thresholds `4000`→2, `8000`→3, `12000`→4, `16000`→5, each tier picking a
  string: `0xec7`=**"1x COMBO"**, `0xec8`=**"2x COMBO"**, `0xec9`=**"3x COMBO"**,
  `0xeca`=**"4+ COMBO"**. The other two fixed-id panels show `0xea8`=**"KNOCKDOWN!"**
  and `0xea9`=**"BIG AIR BONUS"**. The three panels visibly avoid vertically
  overlapping each other (`bVar3`/`bVar4` flags shift each callout's Y position when a
  sibling callout is also showing). **Confirmed: this is the full trick/combo/wipeout
  score-popup feedback system** — combo-multiplier callouts, a big-air landing bonus
  callout, and a knockdown/wipeout callout, all stacking vertically above the rider.
- **Two simpler single-line callouts** (bit `0x400000`, type tags `0xA`/`0xB` in the
  `+0x5784` array) — no point value, just a single localized string: type `0xA`=
  `0xe55`=**"CHECKPOINT"**, type `0xB`=`0xe6e`=**"TIME BONUS"**. **Confirmed** — the
  floating callout shown when passing a checkpoint gate that grants a time bonus.
- **Race-start countdown number** (bit `0x800000`, gated on the rider's not-yet-started
  flag and a time-window check) — draws a counting-down integer via the new
  **`HUD_DrawNumberStyled`** (was `FUN_000ca410`), which renders a number either as one
  formatted string or digit-by-digit as separate stylized glyphs (a scoreboard-style
  numeral font) — the same helper `HUD_DrawRaceOverlay` uses for its rank indicator.
- A couple of panels remain lower-confidence: the bit-7 (sign bit of the low flags
  byte) branch draws a loop of stacked marker icons from a fixed global table
  (`DAT_001bb148`) — read directly via `/read_bytes`: 6 triplets of `int32`s, each
  `[iconA, iconB, iconC]` with `iconA`/`iconB` forming a *sequential pair*
  (`0x19,0x1a` / `0x1a,0x1b` / `0x1b,0x1c` / ... / `0x1d,0x1e`) and `iconC` also
  sequential (`0x1f`...`0x24`), terminated by `0xffffffff`. The loop picks `iconA` or
  `iconB` per slot based on a counter comparison (`unaff_EDI < pfStack_88`, `pfStack_88`
  read from rider `+0x56a8`) — reads as a row of up to 6 icons each with two visual
  states ("achieved"/"pending", or similar), structure confirmed but exact game meaning
  not — candidates: a trick-category checklist row, or a medal/pip progress row.
  The `0x1000000` panel (shares its bit number with a *different* panel in
  `HUD_DrawRaceOverlay`) draws a single icon gated on rider `+0x94` being non-zero,
  purpose not confirmed.

## Open lead: who writes the score at `+0x5710`?

Confirmed `HUD_DrawRaceOverlay` *displays* `+0x5710` and `Race_ComputeRankings`
*initializes* it (to medal-time-target constants in Showoff mode — see
`RE_NOTES_level_script_system.md`), but grepped `default.xbe.c` for every occurrence of
the literal offset and found no increment/write site beyond that initialization — the
live per-trick scoring update (wherever it actually adds points as the player performs
tricks) is not in the text export in a form the literal-offset grep can find, meaning
it's either addressed through a different code shape (e.g. a sub-object pointer offset
rather than the raw `+0x5710` literal) or lives in an entirely different, not-yet-
explored subsystem (real-time trick detection / physics, which this session never
looked at — no `Rider_Update`-style per-frame tick function has been found or named
yet, only event-driven helpers like `Rider_UpdateCueTimer`/`Rider_TriggerTrackedCue`).
**This is the natural next big investigation** if continuing this project: finding the
real-time trick-detection and scoring update loop would be a substantial, high-value,
mostly-unexplored subsystem — likely large (comparable to or bigger than the two HUD
functions above) and physics/animation-heavy.

## Update (2026-07-20, later session): found the HUD-side tick, ruled it out as the writer

Searched for the raw 32-bit displacement bytes for `0x5780`/`0x5784` directly
(`/search_bytes`, much higher signal than a generic offset sweep since it needs a
4-byte immediate match) — only 9 hits in the entire binary, 6 already inside the
known `HUD_DrawWorldSpaceMarkers`. The 3 new hits led to:

- **`HUD_TickRiderDisplayState`** (was `FUN_000c3da0`) — a large, previously
  completely unknown per-frame function sitting right before
  `HUD_DrawWorldSpaceMarkers` in address space. Iterates all 4 rider slots (reads the
  real rider pointer from the global rider-list array, same `[DAT_001e3c7c+0x72c]+0xa4`
  base this project has seen before) and updates a persistent per-rider HUD-display
  state block embedded in the same HUD-context singleton `HUD_DrawRaceOverlay`/
  `HUD_DrawWorldSpaceMarkers` operate on (confirmed via the shared `+0xfc` flags
  field). Per rider it: decays/smooths timer and position-lerp fields, reads
  `rider+0x5710` and buckets it against 3 threshold fields on the HUD context
  (`+0xf0`/`+0xf4`/`+0xf8`) into a tier, and scans `rider+0x5784`'s tagged-event
  array for type-2/type-3 entries specifically. **This is the master per-frame HUD
  tick that feeds both draw functions -- but it only reads `+0x5784`, it never
  writes/pushes a new entry.** Rules out the entire HUD side as the writer, cleanly.
- **`HUD_SelectActivePanelMask`** (was `FUN_000c38f0`) — sets the HUD context's
  `+0xfc` panel-visibility mask from one of 3 mode-dependent presets. Confirms panel
  visibility is precomputed per mode, not evaluated live.
- **`OptionsMenu_CacheDisplaySettingsFromWidgets`** (was `FUN_0009e1d0`) — an
  unrelated find from the same search (its own `0x5784` byte match turned out to be
  coincidental, not a real displacement — the classic false-positive this project's
  memory notes warn about). Reads 5 options-menu widget children and caches their
  values into small display-setting globals, including the already-known
  `DAT_001dd83c` km/h-vs-mph units flag. Kept and named since it's genuine new
  territory, just not related to the score question.

**Net result**: the HUD side of the `+0x5710`/`+0x5784` pipeline is now fully
mapped and conclusively ruled out as the writer/pusher. The real writer must live in
the gameplay/physics tick (`Rider_UpdatePhysicsState` or one of its component
sub-calls, per `RE_NOTES_rider_update_chain.md`) or a dedicated trick-detection
subsystem not yet located. 3 renames this pass.

Two more low-level primitives named while tracing this:

- **`IconAtlas_GetEntry`** (was `FUN_000f24e0`) — bounds-checked accessor into an icon
  metadata array (stride `0x1c`/28 bytes), returns a default sentinel for negative
  indices. Used by both `HUD_DrawWorldSpaceMarkers` and `HUD_DrawRaceOverlay`.
- **`Sprite_DrawAligned`** (was `FUN_000f4380`) — the general aligned-sprite/billboard
  draw primitive: sets a texture from the icon entry, computes UV/position adjustments
  from a bitflag byte (horizontal/vertical flip, edge-anchor offsets, a rotate-90 mode),
  dispatches to one of two low-level quad-draw functions.
- **`Sprite_DrawIconByIndex`** (was `FUN_00056450`) — thin `IconAtlas_GetEntry` +
  `Sprite_DrawAligned` convenience wrapper, used for the sparkle-burst effect above.

## RESOLVED: the localized string content, via the extracted disc data files

Originally flagged as unrecoverable from `default.xbe` alone (`Localization_ResolveString`'s
archive base pointer, `DAT_001e3c7c+0x50`, reads as `0` in the static XBE image — it's
populated at runtime from a separate file, not embedded in the executable). **The user
pointed out the ISO's extracted `Game Data\data` directory is present in the project
root** — and `data\lang\american.loc` (197,712 bytes) is exactly that archive.

Format, fully reverse-engineered and confirmed against `Localization_ResolveString`'s
decompiled logic:

```
offset 0x00: "LOCH" magic
offset 0x04: u32 header size (0x14)
offset 0x08: u32 (0, unused/reserved in this build)
offset 0x0c: u32 (1)
offset 0x10: u32 LOCLbase (offset to the LOCL block, = 0x14 here)
LOCLbase+0x00: "LOCL" magic
LOCLbase+0x04: u32 block size
LOCLbase+0x08: u32 (0, unused/reserved)
LOCLbase+0x0c: u32 string count (3794 in american.loc)
LOCLbase+0x10 + id*4: u32 offset (relative to LOCLbase) to string `id`
  -> string data is UTF-16LE, null-terminated
```

Wrote a parser (`parse_loc.py`, in the project root) and dumped all 3794 ids to
`american_loc_strings.txt` (also project root) — a durable, reusable reference for any
future work touching UI/HUD/menu text. **`data\lang\` actually has three `.loc` files
sharing the same 3794-id space, each a different facet of the same string**:
`american.loc` = the display text, `constant.loc` = the **internal symbolic/debug
name** (invaluable — completely unambiguous, e.g. `kHUD_ubertrick`, `kOvCombo1`),
`letter.loc` = an alternate/condensed text variant (mostly a `"WWWWWWWWWW"` placeholder
for HUD/overlay-category ids, populated for other categories). The parser dumps all
three side by side.

Looked up every string id referenced anywhere in this session's HUD investigation —
every one resolves to real text *and* an unambiguous symbolic name confirming the
panel's purpose exactly:

| id | symbolic name | text | panel |
|---|---|---|---|
| `0x156` | `kOvDidNotFinish` | "DNF" | `HUD_FormatRaceTime` sentinel |
| `0x181` | `kOvNotApplicable` | "N/A" | `HUD_FormatRaceTime` sentinel |
| `0xe37` | `kHUD_checkpoint` | "CHECKPOINT" | `HUD_DrawRaceOverlay` status banner |
| `0xe38` | `kHUD_finish` | "FINISH" | `HUD_DrawRaceOverlay` status banner |
| `0xe3c` | `kHUD_lap` | "LAP" | `HUD_DrawRaceOverlay` status banner |
| `0x10f` | `kHUD_finallap` | "FINAL LAP" | `HUD_DrawRaceOverlay` lap-counter panel |
| `0x110` | `kHUD_lapstogo` | "%d LAPS TO GO" | `HUD_DrawRaceOverlay` lap-counter panel |
| `0x1af` | `kOVReplayFull` | "Replay Full" | `HUD_DrawRaceOverlay` final block |
| `0xdf8` | `kHUD_ubertrick` | "UBER TRICK" | `HUD_DrawRaceOverlay` `0x20000` panel |
| `0xea8` | `kHUDKnockdown` | "KNOCKDOWN!" | `HUD_DrawWorldSpaceMarkers` callout |
| `0xea9` | `kHUDBigAirBonus` | "BIG AIR BONUS" | `HUD_DrawWorldSpaceMarkers` callout |
| `0xec7`-`0xeca` | `kOvCombo1`-`kOvCombo4` | "1x COMBO".."4+ COMBO" | `Trick_GetScoreTier` labels |
| `0xe55` | `kOvCheckpoint` | "CHECKPOINT" | `HUD_DrawWorldSpaceMarkers` callout |
| `0xe6e` | `kOvTimeBonus` | "TIME BONUS" | `HUD_DrawWorldSpaceMarkers` callout |
| `0xe56` | `kOvCombo` | "COMBO" | `HUD_DrawWorldSpaceMarkers` combo-counter label |

**The `kHUD_`/`kOv` symbolic-name prefixes independently confirm the split between the
two sibling functions** — `HUD_DrawRaceOverlay` (2D screen HUD) draws `kHUD_`-prefixed
strings, `HUD_DrawWorldSpaceMarkers` (3D world-space markers, i.e. the original devs'
"Overlay"/`Ov` concept) draws `kOv`-prefixed ones — an unplanned but pleasing
cross-check that the renaming/categorization done earlier this session was correct.

A few extra ids looked up for context while here, not otherwise referenced in code:
`0xe33`="Top %d Record Times", `0xe34`="Top %d Record Scores", `0xe35`="New Record!",
`0xe36`="Enter your name:", `0xe39`="FOR BRONZE", `0xe3e`="%d seconds",
`0xea6`="newbie", `0xeaa`="Single Event Race", `0xeab`="Time Challenge",
`0xecb`="vs. %S", `0xecc`="Replay".

**Two loose ends from earlier in this file are still open** (the bit-7 marker-icon-row
panel and the `0x1000000` single-icon panel in `HUD_DrawWorldSpaceMarkers` — neither
calls `Localization_ResolveString`, so the string dump doesn't help there; they'd need
actual gameplay-logic tracing to resolve, not just string lookup).
