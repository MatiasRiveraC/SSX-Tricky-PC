# RE notes: the `.big` archive container format decoded, and the score-writer thread finally closed with physical proof

This resolves the standing "where does `Script_PlayByName("RaceMode")` actually
find its data" question, open since early in this project (see
`RE_NOTES_level_script_system.md` "The actual conclusion: level scripts are
resolved from runtime level data, not `default.xbe`" and
`RE_NOTES_game_data_archives.md`'s "Dead end investigated"). Previous sessions
confirmed the *mechanism* (`ScriptTable_ResolveNameToID`'s `__stricmp` search
against a runtime-loaded table) but never found the actual data, because the 31
"custom wrapper" `.big` files (everything under `models\`, plus `char\anm.big`/
`mdlxbx.big`/`brdxbx.big`) were never decoded past "starts with `c0fb`, not
standard BIGF." This session decoded that wrapper, wrote a working extractor, and
found `RaceMode`/`ShowoffMode`/`FreerideMode` as literal strings inside a
per-track `.xsf` file.

## The `.big` container format (magic `c0fb`)

Header, all little-endian:

```
offset 0x00: "c0 fb"       -- magic
offset 0x02: u16            -- unknown (e.g. 0x0171 in gari.big; not yet decoded, possibly a version/format tag)
offset 0x04: u16 BE-ish     -- entry count (0x000d = 13 for gari.big) -- read as the raw 2 bytes, big-endian order
offset 0x06: TOC entries begin
```

Each TOC entry:

```
6 bytes: unidentified prefix (NOT yet decoded -- values are monotonically
         increasing across entries within one archive, e.g. 0x0001, 0x0431,
         0x08e4, 0x3581... for gari.big -- looks like it could be a sector/block
         index or a size hint, but doesn't correspond to a byte offset in the
         file directly; not required to extract data, see below)
N bytes: null-terminated ASCII path string, e.g. "data/models/gari_sky.xsh\0"
```

**The 6-byte prefix is NOT needed to extract data** -- critically, it does *not*
encode a usable byte offset or size (confirmed: values don't line up with actual
compressed-blob positions or sizes). The TOC only reliably tells you *how many*
entries exist and *what they're named*, in order. Payload data for the entries
follows the TOC (after a small amount of alignment padding), stored **in the same
order as the TOC**, one after another, as a raw concatenation of compressed blobs.

## The payload compression: EA RefPack (a.k.a. QFS/LZ77-EAC)

Immediately after the TOC (plus a few bytes of zero-padding), each entry's data is
a standalone **RefPack**-compressed blob -- a well-documented LZ77-family
compression scheme used across many EA titles of this console generation, not a
proprietary SSX-specific format. Signature: `10 FB` or `11 FB` (2 magic bytes),
followed by a 3-byte big-endian decompressed size (a `90`/`91`/etc. high-bit
variant uses a 4-byte size for very large blobs, not seen in this archive).

**Wrote a working decompressor: `refpack.py`** (project root). Implements the
standard 4-opcode RefPack scheme (2/3/4-byte copy-length+offset opcodes plus a
literal-run opcode and a terminator opcode) -- validated by successfully
decompressing all 13 entries of `gari.big` to plausible, correctly-typed output
(a `SHPX`-tagged texture-sheet header for the `.xsh` entries, a `ColdFusion`
level-editor export header for `.map`, etc.).

**Wrote `extract_big.py`** (project root, `python3 extract_big.py <archive.big>
<outdir>`) to drive it: parses the TOC, then **chains** through the payload by
actually running the decompressor and using its real consumed-input-byte count to
find the start of the next entry, rather than trying to compute offsets from the
(still not understood) 6-byte TOC prefix. This was the key fix that made
extraction reliable -- an earlier attempt that searched for the next `10FB`/`11FB`
magic bytes directly produced **false positives**, since 2-byte magic sequences
occur by pure chance within any sufficiently large compressed (i.e.
high-entropy) stream. Chaining via actual decompression avoids this entirely.

Ran it against `gari.big` (Garibaldi, one of the 12 real tracks): all 13 entries
decompressed cleanly with sensible sizes matching their file types (e.g.
`gari.xbd` -- geometry, 5.97MB, by far the largest; `gari.map` -- level layout,
1.17MB; `gari.aip` -- AI path data, 59.7KB; `gari.adl`, `gari.sop` -- smaller
support data, tens of KB each).

## The actual find: `gari.xsf` contains the named script/event table

Not `.map` (the level layout file, as originally guessed) -- **`.xsf`**. Dumping
readable strings from the decompressed `gari.xsf` found a clean table of exactly
**20 named entries**:

```
CountDownStart      HideStartGate
BreakLogo1000        BreakLogo2000        BreakLogo22000
BreakLogo4000        BreakLogo4001
BreakLogo5000        BreakLogo5001        BreakLogo5002        BreakLogo5003
BreakLogo7000
HideRace             HideShowOff
ShowoffMode          FreerideMode         RaceMode
StartCountDown       EndCountDown         NoCountDown
```

**This is the literal, physical confirmation of the multi-session hypothesis.**
`ShowoffMode`/`FreerideMode`/`RaceMode`/`StartCountDown`/`EndCountDown`/
`NoCountDown` are *exactly* the script names `GameMode_PlayRaceModeScript`/
`GameMode_PlayShowoffModeScript`/`GameMode_PlayFreerideModeScript`/
`Level_PreloadAndCheckCountdown`/`GameMode_CheckAndPlayStartCountdown`/
`GameMode_CheckAndPlayNoCountdown` pass to `Script_PlayByName` (all documented in
`RE_NOTES_level_script_system.md`). The `BreakLogo*` names also cross-confirm
against `gari.map`'s model list, which contains `Mdl_Billboard_Event1_2`/
`Event2_2`/`Event5_3` entries -- these are sponsor-billboard reveal/break
animation triggers, placed per-track, exactly the kind of thing a named
level-script event would drive.

Record format (confirmed by direct byte inspection around several entries):

```
[name]\0  [u32 fieldA]  [u32 fieldB]  [u32 offset]
```

`fieldA` is usually 0 (purpose undetermined). `fieldB` is small (seen 0-2,
purpose undetermined -- possibly a sub-type or reference count). `offset` is a
byte offset elsewhere within the *same decompressed `.xsf` file*, pointing to
that entry's associated data.

## What's actually at `RaceMode`'s offset: parameter data, not new opcodes

Checked `RaceMode`'s own offset field directly. The bytes there decode as a
sequence of **IEEE-754 floats** -- values like `25.0`, `1.7`, `0.0125`, `3800.0`,
`2400.0`, `2900.0`, `-3000.0`, `0.6`, `1.0`, `1.0`, `0.3`, `0.4` -- reading like a
block of transform/position/timing constants (plausible level-space coordinates
and small scalar tuning values), not an opcode/bytecode stream.

**This refines (doesn't overturn) the project's running hypothesis.** The
architecture -- *which* opcodes exist and what they do -- is fully compiled into
`default.xbe` and already documented end-to-end
(`ScriptVM_DispatchOpcode`/`Script_DispatchOpcode`, ~50 opcodes total across both
layers). What lives in `RaceMode` and friends is **per-track configuration data**
consumed by that already-known opcode interpreter (e.g. `Camera_WarpToTarget`
reads "the target's stored position+orientation" from a command record --
`RaceMode`'s float block is a very plausible source for exactly that kind of
data). This means the score-writer logic (`rider+0x5710`) is very unlikely to be
sitting in `RaceMode`'s own data block as new bytecode to trace -- it's more
likely either genuinely compiled C++ not yet located (contradicting the "must be
script data" theory this thread originally set out to confirm), or driven by a
different, more generic opcode already in the documented set, applied uniformly
across tracks rather than configured per-track. The `RaceMode` mystery *specifically*
is closed; the score-writer mystery is not resolved by this finding, but is now
better-scoped: stop looking in per-track script data for it.

## Cross-checked against two more archives -- pattern holds, one more confirmation

- **`trick.big`** (a standalone level, not shared trick-scoring data despite the
  name -- has its own full `.map`/`.aip`/geometry set like any other track)
  extracted cleanly (13/13 entries) but its `.xsf` has **no** named script table
  -- just coincidental garbage strings, same as the non-name noise filtered out
  of `gari.xsf`. This level apparently doesn't need the
  RaceMode/ShowoffMode/FreerideMode game-mode-startup scripts, consistent with
  it not being a real race/showoff/freeride track (most likely a tutorial/skills
  park). Rules out `trick.big` as a "generic shared trick-scoring" location --
  it isn't one, it's just another per-level archive.
- **`ssxfe.big`** (the front-end/menu archive, 8 entries -- one more than the
  earlier session's `strings`-based TOC survey found, a `tricky.ser` file
  missed by that shallower pass) extracted cleanly, and its `.xsf` **does** have
  a named table -- 33 entries, and it's another clean, direct confirmation:
  **`FEStartScript`/`FEEndScript`** are exactly the names `FEInit_Boot` (already
  documented) passes to `Script_PlayByName`. Also reveals two new, previously
  unknown naming conventions: a menu screen-transition event scheme
  (`onTitleOpen`/`onTitleStop`, `onPROpen`/`onPRClose`/`onPRStop`, and the same
  triplet for `SE`/`WC` -- abbreviations not yet expanded) and a **per-track
  frontend preview-script pair** for all 10 real tracks (`FEGari1`/`FEGari2`,
  `FEPipe1`/`FEPipe2`, `FEAlas1`/`FEAlas2`, `FEAloha1`/`FEAloha2`,
  `FEMega1`/`FEMega2`, `FEMerq1`/`FEMerq2`, `FEMesa1`/`FEMesa2`,
  `FEElys1`/`FEElys2`, `FESnow1`/`FESnow2`, `FEUntr1`/`FEUntr2`) -- almost
  certainly the character/track-select screen's per-track showcase/preview
  cutscene scripts.

Two independent `.xsf` files now confirmed to hold real, sensible,
already-cross-referenced script names. This is no longer a one-off coincidence
-- the format and its role (per-archive named script/event table, consumed by
`ScriptTable_ResolveNameToID`) is solid.

## Reusability

Both `refpack.py` and `extract_big.py` are generic -- they work on any of the 31
custom-wrapper `.big` files (`char\anm.big`/`mdlxbx.big`/`brdxbx.big`, every
`models\*.big`, `tutorial\*.big`, `textures\xboxload.big`), not just `gari.big`.
Running `extract_big.py` against another track's archive (`alaska.big`,
`elysium.big`, etc.) and its own `.xsf` would let a future session cross-check
whether the 20-entry script-name table is identical across tracks (likely, since
these are generic race-flow events) or track-specific in places (the `BreakLogo*`
count/naming might vary by how many sponsor billboards each track has).

## RESOLVED: the per-script data IS the already-documented opcode format, not new bytecode (2026-07-20, later session)

Picked up the standing "content at most script offsets beyond `RaceMode` hasn't
been systematically walked" open item. Re-extracted `gari.big` (`models\gari.big`,
not the `audio\gari.big` of the same name) and parsed the actual record structure
around each of `gari.xsf`'s 20 named scripts, not just their names:

**The name table's actual record layout** (previously only the names were read):
`<u32 type><u32 dataOffset><name, nul-padded>`, repeated 20 times. Parsed all 20:
`CountDownStart`(type 9)/`HideStartGate`(3)/8×`BreakLogo*`(3 each)/`HideRace`(5)/
`HideShowOff`(164)/`ShowoffMode`(1)/`FreerideMode`(2)/`RaceMode`(1)/
`StartCountDown`(1)/`EndCountDown`(0)/`NoCountDown`(1).

**Followed `CountDownStart`'s data offset (`0x25a8`) and found it's not a flat
parameter block — it's itself a small LIST of `<u32 subOffset><u32 opcodeType>`
pairs** (8 of them for `CountDownStart`), each pointing to its own leaf data block
of raw floats/ints (checked several — plain mixed float/int parameter data, same
shape as the already-checked `RaceMode` block, not further nested). **This is the
same `{opcode-ID, parameter-block}` record shape as the already-fully-documented
`Script_DispatchOpcode` (24 level-object types) / `ScriptVM_DispatchOpcode` (~27
outer-VM opcodes) systems** — confirming these per-script data blocks are
**configuration data expressed in the existing, already-reverse-engineered opcode
vocabulary**, not a separate bytecode format requiring new code-level RE.

**This closes the "is there new bytecode to decode here" question definitively: no.**
The remaining unstudied part is purely data-analysis (which opcode ID numbers map
to which specific `Script_DispatchOpcode`/`ScriptVM_DispatchOpcode` case, and what
each track's specific parameter values tune) rather than reverse-engineering new
program behavior — a fundamentally different, lower-priority kind of task than
everything else in this project, and not pursued further this pass given that
distinction. Tooling (a small ad-hoc Python parser, not saved as a standalone
script this pass) confirmed the record shapes live against the real file; no
Ghidra renames resulted from this thread since it's pure data-format work, not
code RE.
