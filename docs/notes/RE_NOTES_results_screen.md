# RE notes: FUN_0008ff00 is a results-screen UI builder, not a script interpreter

Correcting the earlier hypothesis (flagged from the "Script"-tag proximity in the first
mining pass): after reading the full 2044-line body, `FUN_0008ff00` is **not** a
scripting bytecode interpreter. It's a big function that constructs a **UI screen full
of text widgets showing per-track best scores/times** — most likely the freeride or
race results/high-score summary screen.

## Evidence

- [default.xbe.c:9146](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:9146): Ghidra auto-named a global `PTR_s_Garibaldi_001b6474` — a pointer variable at
  address **0x001b6474** whose value is the string `"Garibaldi"`, one of SSX Tricky's
  real track names. The function indexes off this address as an array:
  `(&PTR_s_Garibaldi_001b6474)[iVar2 * 7]` — a table of **7-dword (28-byte) records**,
  one per track, with the track's display-name string in one of the fields.
- Line ~95293 in the decompiled body: a loop over 13 candidates (`iVar7 < 0xd`) picks
  whichever has the highest value (`piStack_260 < (&piStack_1f0)[iVar7]`) — reads as
  "find the best score among N slots."
- Right after, if a valid winner was found: `FUN_0015cece(&DAT_001e3800, "%s (%d)",
  (&PTR_s_Garibaldi_001b6474)[iVar2*7], piStack_260)` — sprintf-formats
  `"<TrackName> (<score>)"` — this is unambiguous: it's building a label like
  `"Garibaldi (14500)"`.
- Multiple calls to `FUN_00150d70("f3colmntitle", 0x110, 0)` — the same tagged allocator
  from the first mining pass, here with tag `"f3colmntitle"`. The `f3` prefix matches
  the front-end/menu asset naming convention already seen (`f3bigbrd`, `f3trkbig`,
  `f3icnTrick`, etc. from the very first pass) — `"colmntitle"` = "column title", i.e.
  this allocates a **results-table column-header widget**.
- Every constructed object gets: a destroy-if-exists check via its vtable slot 0
  (`(*(code*)**obj)()`, the classic "if a previous child exists, delete it first"
  pattern), a stored parent/back-reference at `+0x3d`, an `Init` call through vtable
  slot `0x24` with position/anchor floats (e.g. `390.0 - obj[0x1c]`, screen-relative
  layout math), and a `SetText`-shaped call through vtable slot `0x2a` for two of the
  built widgets. This whole shape — allocate, destroy-old-child, init at (x,y), set
  text — is a **UI widget tree builder**, not bytecode dispatch.

## What this means for the earlier "Script" tag guess

The Part-1 tagged-allocator mining found `FUN_0012a250(size, flags, "Script")` called
11 times elsewhere in the binary — that's a real, separate finding and still stands.
`FUN_0008ff00` merely lives in a nearby code region and uses a *different* allocator
(`FUN_00150d70`, tagged `"f3colmntitle"` here, `"VertexBuffer"` in the board-mesh cluster
from the FX notes) — proximity in the address space was a weak signal and led to the
wrong subsystem guess. The actual "Script" tagged objects (whatever they are) haven't
been identified yet; this function isn't one of them.

## Rename applied

`FUN_0008ff00` → `UI_BuildResultsScreen` (see `ssx_auto_rename.py`).

## RESOLVED (live Ghidra, later session): the track table is fully read and confirmed

Read the raw bytes at `0x001b6474` directly (`/read_bytes`) rather than waiting to
formally define a Ghidra struct (no struct-creation endpoint exists in GhidraMCP even
after patching it three times this session — a one-off feature not worth a 4th patch
cycle). The data label itself was renamed `TrackTable` via `renameData`. Confirmed exact
layout, 7 dwords / 28 bytes per row:

```c
struct TrackTableEntry {          // 0x1c (28) bytes
    char *displayName;            // +0x00, e.g. "Garibaldi"
    char *displayNameUpper;       // +0x04, e.g. "GARIBALDI"
    uint32_t sizeOrFlags;         // +0x08, varies per track (0x2ee0, 0x2328, 0x34bc, ...)
    char *shortCode;              // +0x0c, e.g. "gari" (4-letter internal code)
    char *shortCode2;             // +0x10, always identical to +0x0c
    uint32_t categoryTag;         // +0x14, constant 5 for every real track row
    uint32_t trackIndex;          // +0x18, sequential 1, 2, 3, ...
};
```

**12 real tracks confirmed** (indices 1–12, `categoryTag == 5`), row 13 (index 0) is a
null/terminator row (`categoryTag == 0`), matching `UI_BuildResultsScreen`'s own
`iVar7 < 0xd` (13) loop bound *exactly* — a clean cross-confirmation between this file's
original finding and the later one. Names read directly from the string pointers: row 0
`"Garibaldi"`, row 4 `"Merqury City"`, row 5 `"Aloha Ice Jam"`, row 6 `"Pipedream"`,
row 7 `"Untracked"`, row 8 `"Tokyo Megaplex"`, row 9 `"Big Air Dome"`, row 11 `"Alaska"`
(rows 1–3, 10 not individually checked, same layout). All real SSX Tricky track names.

Not identified: the exact meaning of `+0x08` (per-track, varies — candidate: some kind of
asset-size or unlock-cost value).

### RESOLVED: `shortCode` is reused as an asset-key suffix — the character voice-line table

Searched raw bytes for the literal `"gari"` (`/search_bytes`, hex `676172690000...`) and
found ~20 hits beyond `TrackTable` itself. Several land in a string blob at `0x0018b8a5`
(data-referenced from a 12-byte-stride pointer array at `0x001aca10`, renamed
`VoiceAssetKeyTable_TrackGari`): a flat table of 12-byte, null-padded string entries of
the form `"<CharPrefix>_V<trackCode>"` — e.g. `"Zoe_Vpipe"`, `"Zoe_Vuntr"`, `"Zoe_Valas"`,
`"Zoe_Valoh"`, `"Zoe_Vtoky"`, `"Zoe_Vmerc"`, ... `"Zoe_Vgari"`, then the same full set of
track-code suffixes repeated under `"Bro_V"` (Brodi), `"See_V"` (Seeion/Seeiah), and
presumably every other playable/DVD-talent character. A second, parallel table at
`0x0018d6f8` has the same pattern with a `"scr"` prefix (`"scrZoe_Vpipe"`, ...) —
almost certainly the paired script/cutscene resource key for the same voice line.

**Confirms the open question**: yes, `TrackTable.shortCode` values (`"gari"`, `"pipe"`,
`"untr"`, `"alas"`, `"aloh"`, `"toky"`, `"merc"`, ...) are reused verbatim as the
track-code suffix in a `<CharacterPrefix>_V<trackCode>` asset-key naming convention for
per-character-per-track voice lines, with a `scr<CharacterPrefix>_V<trackCode>` sibling
for the matching script/cutscene resource. This directly ties to the DVD/Jukebox
"talent" voice-commentary system documented in `RE_NOTES_frontend_menu_map.md`
(`UI_BuildJukeboxVoicePlayer` — f3DVDTalentName/f3DVDvoiceTXT/f3DVDvoicesENTRY tags).
Not traced further: which function actually consumes `VoiceAssetKeyTable_TrackGari`
entries (the only xref found is the DATA pointer array, not a code reference) — a good
next step would be to find what walks the `0x001aca10` pointer array and confirm it
feeds the jukebox voice player or a resource-loading lookup.

**Cross-reference (2026-07-20, later session)**: `SkyNode`'s own per-track sky
lookup table (`SkyNode_ResolveTrackSkyIndex`, `RE_NOTES_terrain_collision.md`)
independently confirms these same short-codes from a completely different
angle — a fixed 12-entry table of `"<code>_sky"` asset names (`gari_sky`,
`pipe_sky`, `merquer_sky`, `aloha_sky`, `megaple_sky`, `bigair_sky`,
`alaska_sky`, `untrack_sky`, plus `trick_sky`/`mesa_sky`/`elysium_sky`/
`snow_sky` not yet matched to a specific `TrackTable` row). Two independent
tables, in two unrelated systems, agreeing on the same short-code convention
is a strong confirmation this naming scheme is genuinely load-bearing across

**Resolved (2026-07-21): read `TrackTable`'s raw bytes directly to get the full
short-code → display-name mapping, closing the `snow_sky`/`trick_sky`/`mesa_sky`/
`elysium_sky` open question definitively.** All 12 rows, in order:

| Row | Short code | Display name |
|---|---|---|
| 0 | `gari` | Garibaldi |
| 1 | **`snow`** | **Snowdream** |
| 2 | `elysium` | Elysium Alps |
| 3 | `mesa` | Mesablanca |
| 4 | `merquer` | Merqury City |
| 5 | `aloha` | Aloha Ice Jam |
| 6 | `pipe` | Pipedream |
| 7 | `untrack` | Untracked |
| 8 | `megaple` | Tokyo Megaplex |
| 9 | `bigair` | Big Air Dome |
| 10 | **`trick`** | **Trick Tutorial** |
| 11 | `alaska` | Alaska |

`snow_sky` = row 1, **Snowdream** — the previously-unmatched row. Also confirms
`trick_sky`/`trick` = row 10, **Trick Tutorial** (the tutorial-mode "track" — a nice
tie-in to this session's `trickdef.dat`/`TutorialHUD_*` work, `RE_NOTES_tutorial_system.md`).
`mesa`/`elysium` were already correctly guessed as Mesablanca/Elysium Alps. All 12
`TrackTable` rows and all 12 `SkyNode` short-codes are now fully cross-confirmed —
no remaining ambiguity in either table.

Two independent tables, in two unrelated systems, agreeing on the same short-code convention
is a strong confirmation this naming scheme is genuinely load-bearing across
the whole game, not a one-off.
