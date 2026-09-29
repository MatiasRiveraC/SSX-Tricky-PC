# RE notes: the `.xsh` texture-sheet container format — DECODED

**2026-07-22.** A fresh-direction pick after closing out the AggressionManager/
replay-load thread: `Game Data\data\textures\*.xsh` files had only ever been
enumerated by *filename* in this project (`RE_NOTES_game_data_archives.md`
lists per-character texture naming conventions) — the actual container
*format* had never been decoded, unlike `.ltg`/`.cml` which got full
byte-level treatments. Same asset-survey technique applied here, following
the established `.ltg`-style methodology: hexdump real extracted files,
diff several of different sizes/entry-counts to isolate the fixed structure,
then cross-confirm against already-analyzed Ghidra code.

## The container format

Magic **`"SHPX"`** (not previously searched for/documented). All fields
little-endian:

```
0x00: magic "SHPX" (4 bytes)
0x04: u32 total file size            -- confirmed exact match against every
                                          file's actual on-disk size, 5 files
                                          checked (5184/65664/164256/264352/
                                          and one more)
0x08: u32 entry count N
0x0c: 4-byte build/generation tag (e.g. "G278", "G240", "G266" -- varies per
      file; purpose not confirmed, likely an asset-pipeline version/build id,
      not a per-texture name despite initially appearing concatenated with
      the first entry's name in a raw hexdump)
0x10: N x { 4-byte ASCII name, u32 offset } entry table (8 bytes/entry)
      -- offsets are absolute from file start
right after the entry table: 8-byte tool-signature string, either
      "Buy ERTS" or "EASports" (both seen across different files) -- an EA
      internal asset-compiler watermark ("ERTS" was Electronic Arts' actual
      NASDAQ ticker symbol before their 2001 rebrand)
```

Header size is therefore `12 + 4 + N*8 + 8` bytes, confirmed exactly against
every file checked (e.g. `hud.xsh`, N=3: 12+4+24+8=48=0x30, and the
high-entropy pixel data does start exactly at 0x30).

## The per-entry sub-header (at each entry's offset)

A 16-byte sub-header immediately precedes each entry's pixel data:

```
+0x00: u8  format code (see below)
+0x01..0x03: unknown/reserved, varies per entry, not decoded
+0x04: u16 width
+0x06: u16 height
+0x08..0x0b: unknown, varies, not decoded
+0x0c: u32, top 4 bits read as mip-count/flags (>> 0x1c)
+0x10: pixel data begins
```

**Cross-confirmed against live, already-analyzed Ghidra code** — this is
the strongest part of the finding, the same "file format ↔ runtime struct"
correlation this project did for `.ltg`↔`TerrainGrid`. `GfxContext_
ParseAndQueueTexture` (`0x000fa1d0`) reads exactly this shape before
dispatching to a vtable texture-creation call:

```c
switch(*param_2 & 0xff) {      // <- our +0x00 format byte
case 0x60: uVar1 = 8;  break;
case 0x61: uVar1 = 9;  break;
case 0x62: uVar1 = 10; break;
case 0x6d: uVar1 = 3;  break;
case 0x78: uVar1 = 1;  break;
case 0x7d: uVar1 = 0;  break;
case 0x7e: uVar1 = 2;
}
(**(code**)(*param_1+0xa8))(param_2+4,             // pixel data (skips the
                                                    //   16-byte sub-header)
    param_3, (int)(short)param_2[1],               // <- our +0x04 width
    (int)*(short*)((int)param_2+6),                // <- our +0x06 height
    param_2[3] >> 0x1c,                             // <- our +0x0c mip/flags
    uVar1, 0, 0, param_4);
```

`uVar1` (0-10, or the default 2 for unmatched format bytes) is almost
certainly a `D3DFORMAT`-family index the vtable call maps to a real Xbox
D3D texture format.

## Verified against real extracted files (`scripts\classify_xsh.py`)

Deliverable: **`scripts\classify_xsh.py <file.xsh>`** — parses the full
container + per-entry sub-headers and reports width/height/format/
approximate bits-per-pixel for each entry.

- **`mist.xsh`** (65664 bytes, 1 entry `"mist"`, format `0x7d`): 128×128.
  Pixel data length matches an uncompressed ~4 bytes/pixel texture almost
  exactly (65664 total - 36-byte header - 16-byte sub-header ≈ 65612 vs.
  128×128×4=65536 — very close, small residual likely a trailing
  footer/padding not yet identified).
- **`hud.xsh`** (164256 bytes, 3 entries: `"map1"`/`"map4"`/`"xbox"`, all
  256×256): format `0x60` entry ("map1") comes out to almost *exactly*
  **4.01 bits/pixel**; the two format `0x61` entries ("map4"/"xbox") both
  come out to almost exactly **8.00-8.01 bits/pixel**. **Correction below**:
  I originally read these clean values as evidence for paletted/indexed
  formats specifically ("not DXT1/DXT5, which would look numerically
  identical") — that framing was wrong. 4bpp/8bpp is exactly what DXT1 and
  DXT3/DXT5 also produce, so bits-per-pixel alone can't distinguish the two
  families. See the "which format family" follow-up below — genuinely
  undetermined, not resolved in either direction.
- **`crowd.xsh`** (264352 bytes, 16 entries `"cd00"`-`"cd15"`, all format
  `0x61`, all 128×128): **extremely clean and consistent** — every single
  entry (15 of 16 identical down to the byte in their sub-headers) comes
  out to ~8.05 bits/pixel, an 8-bit indexed crowd-sprite animation sheet
  (16 frames of a background-crowd sprite).

## Follow-up (same day): the format-code → bits-per-pixel table is now PROVEN, not just measured

Traced `GfxContext_ParseAndQueueTexture`'s vtable call (`this+0xa8`) to its
real target: `GfxContext_QueueTextureFromRawData` (already named, vtable
slot confirmed via `GfxContext`'s known vtable base `0x001a2b38+0xa8`).
It re-switches on the SAME format index (0-10) and produces a `local_1c`
scale factor used directly in its own size formula:

```c
size = (width>>mip) * (height>>mip) * local_1c >> 1;   // i.e. local_1c/2 bytes/pixel
```

| `.xsh` format byte | index (uVar1) | `local_1c` | **bits/pixel** | reassigned internal format id | swizzled? |
|---|---|---|---|---|---|
| `0x7d` | 0 | 8 | **32** | 6 | yes (XGSwizzleRect) |
| `0x78` | 1 | 4 | **16** | 5 | yes |
| `0x7e` | 2 | 4 | **16** | 2 | yes |
| `0x6d` | 3 | 4 | **16** | 4 | yes |
| `0x60` | 8 | 1 | **4**  | 0xc | no (direct CRT_MemCopy) |
| `0x61` | 9 | 2 | **8**  | 0xe | no (direct CRT_MemCopy) |
| `0x62` | 10 | 2 | **8**  | 0xf | no (direct CRT_MemCopy) |

**This exactly matches every empirical measurement above**: `0x60`
("map1", measured ~4.01 bpp) → table says exactly 4 bpp; `0x61` ("map4"/
"xbox"/all of `crowd.xsh`'s 16 frames, measured ~8.00-8.05 bpp) → table
says exactly 8 bpp. **The bits-per-pixel VALUE is now proven**, confirmed
directly from the engine's own size-computation code, not just external
byte counting. (Correction below: which *format family* achieves that bpp
— paletted color vs. DXT block compression — is a separate question this
table does NOT settle, despite what I first wrote.)

The reassigned "internal format id" column (2/4/5/6/0xc/0xe/0xf) is passed
onward to `FUN_00049580` (a tagged D3D-texture-slot allocator), and for the
`0xc`/`0xe`/`0xf` cases (the 3 that measure 4bpp/8bpp) the data is copied
verbatim (`CRT_MemCopy`, no swizzle) — while the other 4 formats get
swizzled via the real Xbox SDK call `XGRAPHC::XGSwizzleRect`.

## Follow-up (same session): traced the internal format id into the real Xbox texture header — and corrected my own "paletted" call

Traced where the internal format id (bits placed into `GfxContext_
RegisterTextureTable`, `0x000f9f20`) actually lands: it builds a genuine
**Xbox native `D3DTexture` format DWORD** (Xbox D3D8's texture objects
pack Format/Dimensions/Mipmap directly into the header, unlike PC D3D8's
opaque handles) via a sequence of shifts:

```c
*puVar7 = 0x40001;                              // fixed base tag
puVar7[3] = (internal_format_id << 8) | 0x21;   // format id -> bits [8:15]
puVar7[3] |= (mip_related << 0x10) | 8;
puVar7[3] |= log2(width)  << 0x14;              // bits [20:23]
puVar7[3] |= log2(height) << 0x18;              // bits [24:27]
D3D8::D3DResource_Register(puVar7, ...);
```

This confirms the internal format id is a real Xbox `D3DFORMAT`-family
value (placed at bits 8-15 of the texture header, the documented location
for that field in Xbox's native texture format), not an arbitrary engine
invention — a stronger result than last update's "internal engine format
id, not further traced."

**Correction to my own earlier claim**: I previously wrote "the 4-bit/8-bit-
paletted-format hypothesis is now proven... more consistent with paletted/
indexed-color... than with block compression." **That conclusion doesn't
follow from the evidence and I'm retracting it.** DXT1 (4bpp) and
DXT3/DXT5 (8bpp) block compression produce numerically identical
bits-per-pixel to 4-bit/8-bit palette formats — bpp alone cannot
distinguish the two families, so proving the bpp value proves nothing
about which family it is. If anything, the "no swizzle, direct `CRT_
MemCopy`" behavior for exactly these 3 formats now reads *more* consistent
with DXT block compression (DXT blocks are already stored in a
swizzle-compatible linear layout on Xbox and typically skip the
swizzle step) than with raw paletted pixels (which usually DO get
swizzled for tiled GPU memory access, like the other 4 formats here do).
**Neither hypothesis is proven** — settling it would need either the real
Xbox `D3DFORMAT` enum's exact numeric values (not confidently recalled
this pass) or a visual render of decoded pixel data. Documented honestly
as open rather than asserted either way.

## Follow-up (same session): settled it — strong structural evidence for DXT1/DXT5, not paletted

Decoded `hud.xsh`'s `"map1"` (format `0x60`, 4bpp) pixel data directly as
DXT1 blocks (8 bytes/block: 2×`u16` RGB565 color endpoints + a packed
2-bit-per-pixel index `u32`) and `"map4"` (format `0x61`, 8bpp) as
DXT3/DXT5 blocks (16 bytes/block: 8-byte alpha block + 8-byte DXT1-style
color block). Both interpretations produce **structurally coherent,
plausible results** — not noise, and not what a raw palette-index stream
would look like:

- **DXT1 test (`map1`)**: 34% of all 4096 blocks (1394/4096) show the
  textbook DXT1 "solid-fill" encoder signature — `color0=0x0000`,
  `color1=0x0001` (two near-identical near-black RGB565 values, a classic
  quantization artifact of encoding a solid black/background region) with
  `indices=0xffffffff` (every pixel selects the same color). The remaining
  blocks decode to **varied, plausible UI-icon colors**
  (e.g. RGB(222,218,230)/RGB(82,16,57), RGB(238,238,246)/RGB(115,129,164))
  — light highlights and dark shadow/outline tones, exactly what a real
  compressed HUD icon sheet should look like, not arbitrary bytes.
- **DXT5 test (`map4`)**: several blocks show the **unique DXT5 alpha
  signature** — 2 single-byte alpha *endpoint* values followed by packed
  3-bit interpolated indices (e.g. `alpha0=0x11, alpha1=0xff` then 6 index
  bytes) — a structure **only DXT5 has**; DXT3 stores 16 explicit 4-bit
  alpha values directly with no endpoint/interpolation scheme at all, so
  this specifically rules out DXT3. Other blocks show `alpha=0x00...00`
  (fully transparent, matching the same near-black background regions
  seen in the DXT1 test) or `alpha=0xff...ff` (fully opaque, paired with
  solid white/black colors — a crisp icon outline/edge).

**This is strong, concrete structural evidence — not proof from rendering,
but a specific, hard-to-fake signature** (DXT1's paired near-equal color
endpoints for solid fills; DXT5's uniquely-shaped alpha-endpoint-plus-
interpolation block) that a coincidental raw-palette byte stream would be
very unlikely to reproduce by chance. **Working conclusion, upgraded from
"genuinely undetermined" to "strong evidence"**: format `0x60` = **DXT1**,
format `0x61` = **DXT5**. Not chased to a full visual render, but this is
about as close to confirmed as static analysis gets without one.

## Follow-up (same session): exhaustive format-usage census across all 112 extracted `.xsh` files

Scanned every `.xsh` file in the entire extracted `Game Data\data\` tree
(112 files, 515 total texture entries) for format-byte usage:

| format byte | count | % | bpp | family |
|---|---|---|---|---|
| `0x7d` | 455 | 88.3% | 32 | uncompressed (dominant format by far) |
| `0x61` | 50  | 9.7%  | 8  | DXT5 (strong evidence) |
| `0x6d` | 5   | 1.0%  | 16 | uncompressed/16-bit color |
| `0x60` | 2   | 0.4%  | 4  | DXT1 (strong evidence) |
| `0x7b` | 2   | 0.4%  | **~10.03 measured, 16 expected — MISMATCH** | not in the original 7-case switch, falls to the `default:` case (`uVar1=2`, same as `0x7e`) — but real data contradicts the resulting 16bpp expectation, see below |
| `0x78` | 1   | 0.2%  | 16 | uncompressed/16-bit color |
| `0x62` | **0** | 0% | 8 | **never used anywhere in the shipped game** — the DXT3 guess for this code is untestable from real data |
| `0x7e` | 0   | 0%    | 16 | never used directly (`0x7b` resolves to the same `uVar1=2` via the pre-switch default, not "through" `0x7e`) |

**Key findings**:
- **Format `0x60` (DXT1) is used in exactly 2 places in the whole game**,
  both now checked: `hud.xsh`'s `"map1"` (the original test) and
  `particle.xsh`'s `"exlm"` entry (a particle/flash-effect sprite, 128×128).
  The second entry independently reproduces the *same* DXT1 signature —
  74.3% of its blocks show the identical near-black solid-background
  pattern (`color0≈(0,0,0)`, `color1≈(0,0,8)`, all-1s indices), with a
  bright white core (`color1=RGB(255,255,255)` at block 50) exactly where
  a flash/explosion particle's bright center should be. **This exhausts
  format `0x60`'s entire real-world usage** and independently confirms the
  DXT1 conclusion from a second, unrelated file with different content —
  about as complete a validation as this format code can get without a
  visual render.
- **Format `0x62` (the DXT3 guess) is never used anywhere in the shipped
  retail game** — confirmed by exhaustively scanning all 515 entries, not
  an undersample. The DXT3 hypothesis for this code is now permanently
  untestable from real game data; it remains the switch table's own
  documented mapping (internal id `0xf`, same bpp/no-swizzle treatment as
  `0x61`) but its exact `D3DFORMAT` identity is unverifiable this way.
- **Format `0x7b`, not part of the original 7-case switch, genuinely
  appears in shipped data** (`lightmap.xsh`/`spot1.xsh`, both entry name
  `"spt1"` — spotlight textures) and falls into
  `GfxContext_ParseAndQueueTexture`'s `default:` case (`uVar1=2`, which
  *should* mean 16bpp/swizzled, the same as `0x7e`) — so the fallback path
  is a real, exercised code path, not dead code. **However**: checking the
  actual measured data contradicts the 16bpp expectation. Both `"spt1"`
  entries are 64×64 with only 5136 bytes of data available — 16bpp would
  need 8192 bytes, a 3056-byte shortfall far too large to explain by
  trailing padding/footer. This is a genuine, unresolved discrepancy —
  **not silently accepted or forced to fit**. Most likely explanation:
  lightmap/spotlight textures are loaded through a *different* code path
  than the generic sprite-sheet loader traced here (their distinct
  filenames — `lightmap.xsh`, unlike every other file which is named for
  its screen/content — hint at a specialized use), not that the bpp
  arithmetic itself is wrong (which measured perfectly for every other
  format, including a spot-check of `particle.xsh`'s `"fog0"` entry at an
  exact 32.00 bpp for format `0x7d`). Left deliberately **unmapped** in
  `classify_xsh.py` rather than asserting an unverified 16bpp value.

  **Follow-up, same session — searched for the real lightmap loader,
  genuine dead end via string search**: unlike `"hud.xsh"` (found hardcoded
  directly in `ScreenBase_LoadHudTexture` as a literal `ASYNCFILE_load_3`
  argument), **none** of `"lightmap"`, `"spot1"`, `"lightmap.xsh"`, or
  `"spot1.xsh"` appear as literal strings anywhere in the binary (checked
  all 4 both with and without the extension). This means these two files
  are not loaded via a hardcoded-path call the way most `.xsh` files are —
  most likely they're referenced dynamically (built from a per-scene/
  per-light name at runtime, or reached via directory enumeration/an
  index rather than a literal filename). Not chased further via dynamic
  analysis or by reading every `GfxContext`/lighting-related function for
  a computed-path pattern — a genuinely open thread, distinct from (and
  probably requiring more effort than) the rest of this format's
  decode.

## Assessment

**The container format is fully, cleanly decoded and cross-confirmed against
live runtime code** — byte-perfect header parsing verified across 3 files of
very different sizes/entry-counts (1, 3, and 16 entries), and the per-entry
sub-header shape matches `GfxContext_ParseAndQueueTexture`'s own field reads
exactly (not a guess — the switch cases, the width/height offsets, and the
mip/flags shift all line up).

**Not decoded / open for a future pass**:
- The exact `D3DFORMAT` each of the 7 format-code cases (0-10, excluding
  gaps) maps to — would need either a real Xbox SDK `D3DFORMAT` enum
  cross-reference or tracing the vtable's `+0xa8` texture-creation call
  further.
- The unknown bytes at sub-header `+0x01..0x03` and `+0x08..0x0b` (varies
  per entry, not yet correlated with anything).
- The 4-byte "build tag" field's exact purpose (`"G278"`/`"G240"`/`"G266"`
  — a version/generation id, never traced to consuming code).
- Whether/how the file's own magic (`"SHPX"`) is validated at load time —
  searched the binary for both byte orders of the magic, found **zero**
  hits; the loader most likely trusts the format unconditionally rather
  than checking it (or checks it in a form not amenable to a literal byte
  search, e.g. reading it as 4 separate byte compares).
- The 4-bit/8-bit-indexed-format hypothesis for format codes `0x60`/`0x61`
  isn't proven with a palette dump or visual render — a good next step if
  this thread is revisited.

No new function renames this pass (a file-format/data-analysis finding, not
code RE — matches this project's established distinction between the two,
same as `.ltg`'s and `.cml`'s initial format-only passes).


## PORT CORRECTION (2026-07-22): file data is LINEAR, not swizzled

Discovered while building the PC port's C++ `.xsh` decoder (`port/src/assets/
xsh.cpp`) and testing it against the real `splash.xsh` (fmt 0x7D, 512x512):
the pixel data decodes to a coherent image ONLY when read **linearly**. The
"is_swizzled" flag in `classify_xsh.py`'s FORMAT_BPP table does NOT describe
the on-disk layout -- it reflects that the Xbox engine SWIZZLES the linear
source into an NV2A GPU texture at upload time (via XGRAPHC::XGSwizzleRect in
GfxContext_QueueTextureFromRawData). For a PC port targeting a modern GPU
(which wants linear data), the swizzle step is simply omitted. So: `.xsh`
32bpp/16bpp pixel arrays are stored row-major/linear; no Morton de-swizzle is
needed to read them. (DXT block formats are unaffected -- block layout is the
same either way.) This does not change the format spec above, only clarifies
that the "swizzled" annotation is an upload-time engine behavior, not a file
property.
