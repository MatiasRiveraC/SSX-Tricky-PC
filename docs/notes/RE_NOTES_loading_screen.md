# RE notes: Loading / splash screen system (the `.xsh` picture screens)

Fresh direction opened 2026-07-21 by surveying the **extracted game assets**
(`Game Data/data/`) for untouched file formats rather than guessing subsystem
names. The winner: **`.xsh` — 112 files, all in `data/textures/`**, by far the
most numerous asset type and never previously traced.

## The `.xsh` texture-bank format (decoded from raw bytes)

`.xsh` files are **EA Gimex texture banks**, magic **`"SHPX"`**:

```
+0x00  "SHPX"                     (magic)
+0x04  u32 file size              (e.g. hud.xsh = 0x000281a0 = 164256, matches)
+0x08  u32 entry count            (hud.xsh = 3)
+0x0c  "G278"/"G240"...           Gimex version tag ("G" + version)
+0x10  per-entry records: 4-char sub-format id ("map1","map4","xbox") + offset
+...   "EASports" watermark, then the pixel/palette data
```

The `"G###"` Gimex tags line up with the already-known `"Encoded with MPEG
Gimex module"` string (see `RE_NOTES_title_intro_sequence.md`) — EA used its
Gimex image library for both video and textures. The `"SHPX"` magic does **not**
appear anywhere in `default.xbe` (the loader doesn't validate it), which is why
a magic-string search came back empty; the anchor that actually led into the
code was the `.xsh` **filename** strings.

## The screen-class family (vtables 0x001a76d8 .. 0x001a7a68)

The `.xsh` filename strings led to a cluster of filename-builders, which turned
out to belong to a **family of 9 full-screen "picture screen" classes** that
all share a common base. Every vtable in the family shares:

- **slot 0** = `ScreenBase_LoadHudTexture` (`0x0012f260`) — async-loads
  `data/textures/hud.xsh` into `obj+8` on enter.
- **slot 1** = `ScreenBase_TickAsyncAssetLoad` (`0x0012f2a0`) — a 5-phase async
  loader: `title.ffn` -> `menu.ffn` fonts (parsed via `Font_ParseGlyphTable`,
  tying this cleanly to `RE_NOTES_application_boot.md`'s `.ffn` cluster) ->
  subclass layout hook -> iterate the subclass picture list loading each `.xsh`
  and creating a D3D texture -> done.
- **slot 5** = `ScreenBase_DrawFrame` (`0x00135a60`) — per-frame draw: backdrop,
  then invokes two subclass content hooks (vtable **slot 7** and **slot 8**);
  blank screens point both at a shared stub (`0x0015cf26`). Draws a status
  string (Localization id `0x16e`) at `obj[0x30]==4` and a progress bar.

Slots 2/3/4 are shared among a subset (two flavors — "loading" vs. "simple",
see below); slots 6/7/8/9(/10) are the per-subclass content overrides.

### Shared lifecycle methods (span the family)

- `ScreenBase_ConfigurePulseTimers` (`0x0012f690`) — on-enter viewport + 3
  pulse/oscillator timers (`FUN_000ed510`, ids 1/2/3) that the draw hooks sample
  via `FUN_000ed740`/`FUN_000ed710` for pulsing picture/text alpha.
- `ScreenBase_UpdateFade` (`0x0012f770`) — per-frame fade of `obj+0x98` toward
  0.0/1.0 by the `obj+0x9c` direction flag.
- `ScreenBase_TickFadeOutAndRelease` (`0x00133380`) — slot-2 exit tick (the
  loading-flavored subclasses): fades out, then after a 2-frame settle returns
  done, calling `ScreenBase_ReleasePictureResources`.
- `ScreenBase_ReleasePictureResources` (`0x0012f5f0`) — frees the in-flight
  picture file (`obj+0x16c`) and the `LoadPicDataBuffer` (`obj+0x174`, via
  `Heap_Free` or `Pool_FreeSlot` by range), clears the list base/count.
- `LoadScreen_OnEnterConfigure` (`0x00134ae0`, slot 3) / `LoadScreen_TickFadeUpdate`
  (`0x00134b00`, slot 4) — thin loading-screen wrappers over the two above.
- **Simple-screen flavor** (subclasses with no picture list to release):
  `Screen_TickFadeOut` (`0x0012f740`, slot 2 — same fade/settle shape, never
  calls `ScreenBase_ReleasePictureResources`), `Screen_OnEnterConfigure`
  (`0x00131c60`, slot 3), `Screen_TickFadeUpdate` (`0x001342c0`, slot 4) — twins
  of the `LoadScreen_*` wrappers above but passing 0 instead of 1 to
  `FUN_0010ec10`/`FUN_0010eca0`.

### Identified subclasses — the complete 9-screen family

| vtable         | class                     | content (slot 8 prepare hook)                              |
|----------------|---------------------------|--------------------------------------------------------------|
| 0x001a76d8     | (blank base)              | blank — slots 6-9 all -> shared stub `0x0015cf26`             |
| **0x001a7730** | **`SplashScreen`**        | **`data/textures/splash.xsh`, fullscreen quad** (confirmed via real code xref) |
| **0x001a7780** | **`LoadScreen`**          | **race loading: mode+track+rider pics from `xboxload.big`**  |
| **0x001a7968** | **`DualLoadScreen`**      | **dual-rider/versus loading: 2 rider pics @+0xd04**           |
| 0x001a79a4     | `InfoRowsScreen`          | no pictures; 8-row generic content + pulsing icon             |
| 0x001a79d0     | `RiderComparisonScreen`   | 2-rider picture list @+0x1178 (a 2nd, distinct 2-rider impl)  |
| 0x001a79fc     | `LoadTipScreen`           | single indexed title+description tip panel                   |
| 0x001a7a28     | `RulesScreen`             | no pictures; fixed rules/legend text rows                     |
| 0x001a7a54     | `SoloLoadScreen`          | solo-rider: 1 rider pic @+0xd7c, same tip ids as `LoadScreen` |

The 4 later 11-slot vtables (`InfoRowsScreen`/`RiderComparisonScreen`/
`LoadTipScreen`/`RulesScreen`) have an extra **slot 10**: a standard C++
scalar-deleting destructor (`*this = &baseDtorVtable; ...; if (owns) free(this);`
— e.g. `InfoRowsScreen_Destructor`, `0x00137cd0`), reassigning a base-destructor
vtable located immediately after each subclass's own vtable. `SoloLoadScreen`
has only 10 slots, same as the other loading-flavored screens.

**`SplashScreen`** (vtable `0x001a7730`) — shows `data/textures/splash.xsh`
fullscreen, no text:
- `SplashScreen_LoadTexture` (`0x0012f9c0`, slot 8) — async-loads
  `data/textures/splash.xsh` into `obj+8` (structural twin of
  `ScreenBase_LoadHudTexture`).
- `SplashScreen_CreateTextureFromAsset` (`0x0012f9e0`, slot 9) — waits for the
  load, builds the D3D texture (tag `"spla"`), stores the handle at `obj+0x414`.
- `SplashScreen_DrawTexture` (`0x00136270`, slot 6) — draws that `obj+0x414`
  handle as a fullscreen 640x480 quad. Slot 7 is the shared `Node_NoOpStub1`
  (no label text). **This was initially left unowned in the first pass of
  this thread** (named `Screen_DrawFullscreenTexture` with an explicit "owner
  not pinned" hedge) rather than assumed to belong to whichever filename
  string happened to sit nearest in memory — the real owner was only
  confirmed once a genuine code xref to the `splash.xsh` string led to
  `SplashScreen_LoadTexture`, whose address turned out to sit at this exact
  vtable's slot 8. Renamed once proven, not before.

**`LoadScreen`** (vtable `0x001a7780`) — race loading screen: mode+track+rider
pictures, captions, progress bar.
- `LoadScreen_PreparePictureList` (`0x0012fd90`, slot 8) — builds the 3-entry
  list at `obj+0xe30` from `data/textures/xboxload.big`, entries formatted by:
  - `LoadScreen_FormatModeTextureName` (`0x0012fa50`) -> `ldmode{freeride,
    race,showoff,timec}.xsh` (**confirms GameMode enum**: 1=freeride,
    2/4=race, 3/5=showoff, 7=time challenge)
  - `LoadScreen_FormatTrackTextureName` (`0x0012fae0`) -> `ldtrack{gari,snow,
    elys,mesa,merq,aloh,pipe,untr,toky,alas}.xsh` (indices 0-8,0xb)
  - `LoadScreen_FormatRiderTextureName` (`0x0012fc20`) -> `ldrider{...}.xsh`
    (see roster below)
- `LoadScreen_DrawPictures` (`0x00136470`, slot 6) — draws the 3 pictures with
  a time-pulsed alpha + spinner sprite.
- `LoadScreen_DrawLabels` (`0x001367c0`, slot 7) — mode/track/rider captions +
  a column of loading-tip rows (Localization ids `0xe88`/`0xe89` for modes 4/5).
- `LoadScreen_SetupLayoutRects` (`0x0012ff10`, slot 9) — hardcoded UI rects.

**`DualLoadScreen`** (vtable `0x001a7968`) — versus/multiplayer loading: builds
a **2-rider** picture list for `Team_GetRiderTagByte(0)` and `(1)`.
- `DualLoadScreen_PreparePictureList` (`0x00130d60`, slot 8)
- `DualLoadScreen_DrawPictures` (`0x00136dc0`, slot 6)
- `DualLoadScreen_DrawLabels` (`0x001372f0`, slot 7) — two-column comparison grid.
- `DualLoadScreen_SetupLayoutRects` (`0x00130eb0`, slot 9)

**`InfoRowsScreen`** (vtable `0x001a79a4`, moderate confidence) — no picture
list; an 8-row generic content layout + small pulsing icon.
- `InfoRowsScreen_PrepareContent` (`0x00131c80`, slot 8)
- `InfoRowsScreen_DrawIcon` (`0x001376e0`, slot 6) — fixed-coord pulsing sprite.
- `InfoRowsScreen_DrawRows` (`0x001377a0`, slot 7) — 8 rows via
  `FUN_000c3160`/`FUN_000c2fd0`, same call shapes as the comparison screens'
  label drawers, but with no rider pictures backing it (generic content —
  possibly a records/credits/legend list; exact purpose not pinned).
- `InfoRowsScreen_SetupLayoutRects` (`0x00131cb0`, slot 9)
- `InfoRowsScreen_Destructor` (`0x00137cd0`, slot 10)

**`RiderComparisonScreen`** (vtable `0x001a79d0`, moderate confidence) — a
**second, independently-implemented** 2-rider screen (structural twin of
`DualLoadScreen` but different field offsets, reusing the same `ldr1`/`ldr2`
tag constants).
- `RiderComparisonScreen_PreparePictureList` (`0x00132a80`, slot 8)
- `RiderComparisonScreen_DrawPictures` (`0x00137d40`, slot 6)
- `RiderComparisonScreen_DrawLabels` (`0x00137e10`, slot 7) — comparison grid
  + a glyph-buffer string.
- `RiderComparisonScreen_SetupLayoutRects` (`0x00132bc0`, slot 9)
- `RiderComparisonScreen_Destructor` (`0x001381c0`, slot 10)

**`LoadTipScreen`** (vtable `0x001a79fc`, moderate confidence) — no picture
list; a single indexed title+description "did you know" tip panel, distinct
from `LoadScreen`'s column of fixed short tip rows.
- `LoadTipScreen_PrepareContent` (`0x00133980`, slot 8)
- `LoadTipScreen_DrawIcon` (`0x00138230`, slot 6) — identical shape to
  `InfoRowsScreen_DrawIcon`.
- `LoadTipScreen_DrawTipPanel` (`0x001382f0`, slot 7) — gated on `obj+0xc0==4`;
  resolves a title+description pair via 2 consecutive localization ids
  (`index*2+0xcfe`/`+0xcff`, index at `obj+0x1b50`), draws the title centered
  and the description in a measured/wrapped text box.
- `LoadTipScreen_SetupLayoutRects` (`0x001339b0`, slot 9)
- `LoadTipScreen_Destructor` (`0x00138690`, slot 10)

**`RulesScreen`** (vtable `0x001a7a28`, moderate confidence) — no picture
list, but uses the *loading*-flavored slot-3/4 lifecycle (unlike the other
picture-less siblings); draws fixed (non-indexed) rules/legend text.
- `RulesScreen_PrepareContent` (`0x001342f0`, slot 8)
- `RulesScreen_DrawIcon` (`0x00138700`, slot 6) — same
  `FUN_00135c70`/`FUN_001359a0`/`Sprite_DrawAligned` shape as
  `LoadScreen_DrawPictures`'s per-entry draw, called generically over an
  empty picture list (an edge case of the same draw routine, not a distinct
  fixed-coord icon like `InfoRowsScreen`/`LoadTipScreen`).
- `RulesScreen_DrawRows` (`0x001387b0`, slot 7) — fixed localization ids
  (`0xc86`-`0xc8c`, `0xe01`-`0xe04`, not index-selected) — static
  instructional/legend text.
- `RulesScreen_SetupLayoutRects` (`0x00134320`, slot 9)
- `RulesScreen_Destructor` (`0x00138eb0`, slot 10)

**`SoloLoadScreen`** (vtable `0x001a7a54`, moderate confidence) — a
single-rider-only variant of `LoadScreen`, no mode/track picture, reusing
`LoadScreen_DrawLabels`'s exact tip-text localization ids (`0xe88`/`0xe89`).
- `SoloLoadScreen_PreparePictureList` (`0x00134b30`, slot 8)
- `SoloLoadScreen_DrawPicture` (`0x00138f20`, slot 6)
- `SoloLoadScreen_DrawLabels` (`0x00139000`, slot 7)
- `SoloLoadScreen_SetupLayoutRects` (`0x00134c30`, slot 9)

**Identification method note**: none of these 9 vtables have a direct pointer
xref (`search_address_refs` came back empty for all of them — they're likely
constructed via an indirect/table-driven screen-factory mechanism not traced
this pass, not a plain `MOV [ecx], imm32`). Two — `LoadScreen` and
`SplashScreen` — were pinned with certainty via real code xrefs to nearby
filename strings (`xboxload.big`, `splash.xsh` respectively). The other 5
have no adjacent identifying string, so they're named from structural
behavior with explicit "moderate confidence" hedges in the script comments.

## The definitive 12-character roster (resolves the Mac/Marty question)

`LoadScreen_FormatRiderTextureName`'s switch is the game's own canonical rider
list in index order (0-0xb):

| idx | id      | idx | id       |
|-----|---------|-----|----------|
| 0   | eddie   | 6   | jp       |
| 1   | kaori   | 7   | elise    |
| 2   | luther  | 8   | psymon   |
| 3   | **mac** | 9   | seeiah   |
| 4   | moby    | 10  | brodi    |
| 5   | zoe     | 11  | marisol  |

Index 3 is **`mac`**, not "Marty" — this resolves the roster gap flagged in the
official-manual terminology memory. (`Team_GetRiderTagByte`/`Team_GetPointerByTag`
map a team slot to this index.)

## The Gimex bitmap codec (closes the texture-decode-path question)

Traced `ScreenBase_TickAsyncAssetLoad` phase 3's decode call chain and found
something bigger than expected: the decoder isn't loading-screen-specific at
all — it's the engine's **general-purpose Gimex-format asset decompression
codec**, sitting directly beneath the already-named core file functions
`FILE_loadpack`/`FILE_loadpackat`.

Every Gimex-compressed blob (any pack-file entry, not just `.xsh` textures)
shares a 2-byte header: a format/flags byte at offset 0, and a fixed marker
byte `0xfb` at offset 1.

- **`GimexBitmap_DecodeDispatch`** (`0x00149c80`) — the central dispatcher.
  Checks the `0xfb` marker, then switches on the format byte (masked `&
  0xfe`) to one of 4 per-format decoders, or a dynamically-hooked format
  (`0x1e`/`0x9e`, via callback `DAT_001fe2cc`).
- **`GimexBitmap_GetDecodedSize`** (`0x00149df0`) — probes the decoded output
  size without decoding, reading a 3- or 4-byte big-endian size prefix (width
  selected by the format byte's low bit) right after the header.
- **`GimexBitmap_DecodeFormat10`** (`0x0014b0b0`, formats `0x10`/`0x90`) — the
  simplest variant; reads a 1- or 2-byte palette-index size then unpacks.
- **`GimexBitmap_DecodeFormat18`** (`0x0014b020`, formats `0x18/0x1a/0x1c/
  0x98/0x9a/0x9c`) — reads a 3-byte size + width/height-shaped header; reads
  as a raw/near-uncompressed variant.
- **`GimexBitmap_DecodeFormat30`** (`0x0014a0f0`, formats `0x30/0x32/0x34/
  0xb0/0xb2/0xb4`) — substantially more complex (~0x400 bytes of stack
  working state); likely RLE or bitplane-packed, exact algorithm not traced
  bit-for-bit.
- **`GimexBitmap_DecodeFormat46Lzw`** (`0x00149fb0`, formats `0x46`/`0xc6`) —
  allocates 256-entry prefix and suffix/remap tables plus a working buffer —
  the classic shape of an **LZW decompressor** (GIF-style), fitting Gimex's
  known lineage as an EA image-compression library.
- **`FILE_LoadPackedGimexAsset`** / **`FILE_LoadPackedGimexAssetInto`**
  (`0x0014bae0`/`0x0014bbf0`) — the two higher-level wrappers, called
  directly and solely from `FILE_loadpack`/`FILE_loadpackat` respectively.
  **This confirms the codec is engine-wide, not loading-screen-specific.**
- **`FILE_ResolvePackEntryHandle`** (`0x0014c040`) — resolves a pack-file
  entry to a raw handle + size (via `RaceState_NullHandler`, reused here as a
  generic callback dispatcher despite its race-specific name, and
  `FILESYS_atomic`).

Not fully reverse engineered: the exact bit-level compression algorithms of
`DecodeFormat10`/`18`/`30` (identified structurally, not decoded byte-for-
byte) — a good target for a future pass if the actual pixel data ever needs
re-encoding (e.g. for a PC port), but not necessary for function-level
identification.

9 renames.

## Still open (good follow-up threads)

- **The exact game context of the 5 structurally-named screens**
  (`InfoRowsScreen`/`RiderComparisonScreen`/`LoadTipScreen`/`RulesScreen`/
  `SoloLoadScreen`) — static analysis found no owning constructor or unique
  string for any of them; pinning exact usage (which menu/mode triggers each)
  would need dynamic analysis or a broader search for the screen-factory
  table that selects between all 9 vtables.
- **Where `LoadScreen_DrawPictures` (slot 6) is invoked from** — `ScreenBase_
  DrawFrame` calls slots 7 & 8, not 6; the slot-6 draw dispatch site wasn't
  located this pass (documented honestly rather than guessed).
- **The exact bit-level Gimex compression algorithms** (see above) — a
  future thread if ever needed, not required for RE completeness at this level.

60 renames total across this whole thread (15 initial + 6 shared lifecycle
methods + 27 across the 5 remaining sibling screens + 3 splash-screen
functions + 9 for the Gimex bitmap codec).
