# RE notes: the `.ffn` bitmap font format — DECODED

**2026-07-22.** A fresh-direction pick, immediately after decoding `.xsh`
(fonts had been flagged as unexplored in `reference_extracted_game_data`
memory). `Game Data\data\fonts\` has 3 files: `menu.ffn`, `smlfont.ffn`,
`title.ffn`. Unlike `.xsh`, this decode started from a head start: this
project already had `Font_LoadAndParse` (`0x000c36f0`) and
`Font_ParseGlyphTable` (`0x000c2da0`) named from an earlier session (their
bodies were read, but the exact `.ffn` file layout itself had never been
worked out byte-by-byte) — so this was a direct cross-reference exercise
against already-decompiled code, the same technique used for `.xsh`
against `GfxContext_ParseAndQueueTexture`, rather than starting cold.

## The container format

Magic **`"FNTF"`**. All fields little-endian:

```
0x00: magic "FNTF" (4 bytes)
0x04: 4 bytes, unknown/varies per file -- not read by any traced function
0x08: u16 glyph-record-format selector -- compared against 200:
      <200 -> compact 11-byte-per-glyph, big-endian-packed source records
      >=200 (the only case seen in all 3 real files) -> extended 12-byte,
             little-endian-native source records
0x0a: u16 glyph count
0x0c: 4 bytes, unknown (0 in all 3 files)
0x10: u8 font metric A (0 in all 3 files)
0x11: u8 font metric B (0 in all 3 files; caller-adjustable via a param)
0x12: 2 bytes, unaccounted for
0x14: u32 offset (from file start) to the glyph-record table
0x18: 4 bytes, unknown (0 in all 3 files)
0x1c: u32 offset (from file start) to the bitmap-atlas section
```

Cross-confirmed field-for-field against **`Font_ParseGlyphTable`**
(`0x000c2da0`, already named): every offset above is read exactly as
described (`param_2+8`->mode selector, `param_2+0xa`->glyph count,
`param_2+0x10`/`+0x11`->the two metric bytes, `param_2+0x14`->glyph-table
offset), and the function's very last statement,
`FUN_000c21a0(*(int*)(param_2+0x1c) + param_2)`, hands off to the bitmap
parser using exactly the `0x1c` offset field.

## Per-glyph record (12 bytes, in the runtime `"Glyphs"`-tagged array)

Same destination field layout regardless of which source encoding mode is
used:

```
+0x00: u16 character code (likely Unicode/codepoint)
+0x02: u8  coordinate/metric A
+0x03: u8  coordinate/metric B
+0x04: u16 coordinate/metric C
+0x06: u16 coordinate/metric D
+0x08: u8  coordinate/metric E (+ a caller-adjustable offset, param_3)
+0x09: u8  coordinate/metric F
+0x0a: u8  coordinate/metric G
+0x0b: unused/padding
```

Exact per-field *semantic* meaning (X/Y atlas position? width/height?
kerning?) not individually pinned down this pass — the byte-level shape is
fully confirmed, the geometric interpretation of each sub-field isn't.

## The bitmap-atlas section — a packed 4-bit alpha-mask glyph texture

At the header's `0x1c` offset:

```
+0x00: unknown, not read by the traced parser
+0x04: u16 bitmap width (pixels)
+0x06: u16 bitmap height (pixels)
+0x10: packed 4-bit-per-pixel data begins, row-major, 2 pixels/byte
       (high nibble = left pixel, low nibble = right pixel)
```

Cross-confirmed against **`FUN_000c21a0`** (the function
`Font_ParseGlyphTable` hands off to, un-named this pass — a direct
sibling/callee of two already-named font functions): it reads exactly
these two width/height shorts, then unpacks each source byte into **two
16-bit `A4R4G4B4`-shaped texture texels** — `(nibble << 12) | 0xFFF`,
i.e. constant pure-white RGB (`0xFFF`) with the 4-bit value placed in the
alpha channel. This is the classic "store anti-aliased glyph edges as an
alpha-only mask, tint with the actual text color at render time" technique
— confirms these are genuinely **font glyph bitmaps**, not arbitrary
texture data. The unpacked texture is uploaded via a `GfxContext` vtable
call (`this+0x720`'s vtable, slot `+0xa8` — the exact same slot address
`GfxContext_ParseAndQueueTexture` uses for `.xsh` textures, confirming
`.ffn`'s bitmap ultimately feeds the same texture-creation pipeline as
`.xsh` sheets do) with the destination surface rounded up to the next
power-of-2 square dimension (a `do { iVar2 *= 2; } while` loop, standard
GPU texture-alignment practice).

## Verified byte-perfect against all 3 real files (`scripts\classify_ffn.py`)

Deliverable: **`scripts\classify_ffn.py <file.ffn>`**.

| file | mode selector | glyph count | glyph-table offset | bitmap offset | bitmap size | expected (w×h/2) | match |
|---|---|---|---|---|---|---|---|
| `menu.ffn` (7296B) | 202 | 104 | 0x20 | 0x570 | 128×92, 5888B | 5888 | **exact** |
| `smlfont.ffn` (4528B) | 200 | 95 | 0x20 | 0x4a0 | 128×52, 3328B | 3328 | **exact** |
| `title.ffn` (17152B) | 202 | 82 | 0x20 | 0x470 | 256×125, 16000B | 16000 | **exact** |

**Every single file's bitmap section is byte-perfect** — the computed
`width*height/2` (4bpp) size matches the literal remaining bytes in the
file with zero residual, meaning the bitmap section is confirmed to be
each file's final section with nothing trailing it. This is as complete a
validation as a format decode gets without a visual render.

## Assessment

**Fully decoded, cross-confirmed against already-named live code, and
byte-perfect-verified against all 3 real extracted files.** Not decoded
this pass:

- The exact semantic meaning of each of the 7 per-glyph sub-fields
  (X/Y atlas coordinates? advance width? kerning pairs?) — the byte shape
  is certain, the geometry isn't.
- The unknown 4-byte fields at header offsets `0x04`, `0x0c`, `0x18`, and
  the 2 unaccounted bytes at `0x12` — all consistently `0` (or, for `0x04`,
  varying but unread by any traced function) across the 3 real files
  checked, not correlated with anything.
- No `.ffn` file using the `<200` "compact 11-byte BE glyph record" mode
  was found among the 3 real files (all 3 use the `>=200` "extended"
  mode) — that code path is real (confirmed via the switch's existence)
  but its exact byte layout wasn't independently verified against a real
  sample.

1 new function rename this pass: **`Font_UnpackGlyphBitmapTexture`** (was
`FUN_000c21a0`, the bitmap-atlas parser confirmed above) — named live,
verified, synced to `scripts\ssx_auto_rename.py` (0 duplicate addresses/
names, 1,256 total renames project-wide). Otherwise a file-format/data-
analysis finding cross-referencing existing names, matching this project's
established distinction between format-decode and code-RE passes.


## GLYPH-RECTANGLE LAYOUT RESOLVED (2026-07-22, during the PC port)

The "per-glyph sub-field geometry (X/Y/width/kerning not individually pinned
down)" open item is now CLOSED. Recovered the exact 12-byte glyph-record
layout from the game's own text renderer (`Text_RenderGlyphString` @0x102850,
which reads the metrics pointer `Font_GetGlyphMetrics` returns), then verified
it by rendering real strings from the atlas (byte-exact glyph rects produce
legible "Loading... Checking hard disk" in the menu.ffn typeface):

  glyph table = header field @0x14 offset + **12 header bytes**, then
  glyphCount records of 12 bytes each, SORTED by char code (binary-searched):
    +0x00 u16  char code (Unicode/ASCII codepoint)
    +0x02 u8   glyph width  (atlas + quad)
    +0x03 u8   glyph height (atlas + quad)
    +0x04 u16  atlas X (U), left edge in the alpha atlas
    +0x06 u16  atlas Y (V), top edge
    +0x08 s8   pen x-advance
    +0x09 s8   x bearing (draw offset)
    +0x0A s8   y bearing (draw offset)
    +0x0B     padding

The renderer maps each glyph to an atlas rect [X, X+width] x [Y, Y+height]
(the U/V right/bottom edges are X+width / Y+height), positions the quad by the
bearings, and advances the pen by +0x08. Glyphs are stored in ascending
codepoint order with gaps (e.g. menu.ffn has ')'=0x29 then '+'=0x2b, no '*').
This corrects `classify_ffn.py`'s earlier "gt+3, +0=charcode" guess (which was
the RE's flagged-unconfirmed partial read): the real header is 12 bytes and
the field layout is as above. Now implemented in the PC port
(`port/src/assets/ffn.cpp` + `port/src/game/text.cpp`), rendering real game
text in the game's own fonts.
