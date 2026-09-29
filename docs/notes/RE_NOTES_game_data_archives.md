# RE notes: extracted game data archives (`Game Data\data\`)

**Update (later session): the custom `c0fb` wrapper format described below as
"not decoded further this pass" IS now decoded** — see
`RE_NOTES_archive_format_decoded.md` for the full container format, the working
`refpack.py`/`extract_big.py` extractor tools, and the resulting discovery of
`RaceMode`/`ShowoffMode`/`FreerideMode` as literal strings inside per-track
`.xsf` files (finally closing the "dead end investigated" score-writer lead
below with a concrete data location, even though the score-writer's own logic
turned out not to be there).

Follow-on from the `.loc` localization-archive discovery (see `RE_NOTES_race_hud.md`).
The user pointed out the ISO's extracted files live in the project at `Game Data\data\`
— this note covers a broader sweep of that directory beyond just `lang\`.

## `.big` archives — EA's classic BIGF format, 55 files, TOC dumped

Wrote `parse_all_big.py` (project root) and ran it across every `.big` file in
`Game Data\data\`, producing `big_archive_inventory.txt` (project root, 976 lines) — a
full table-of-contents dump for every archive that uses the format. Format (standard
EA BIGF, big-endian):

```
offset 0x00: "BIGF" magic
offset 0x04: u32 BE total archive size
offset 0x08: u32 BE file count
offset 0x0c: u32 BE header/TOC size
offset 0x10: per-entry: u32 BE offset, u32 BE size, null-terminated name string
```

**24 of the 55 `.big` files use this format** (all of `audio\*.big` and
`char\texxbx.big`). **The other 31** (`char\anm.big`/`mdlxbx.big`/`brdxbx.big`,
everything in `models\`, `tutorial\`, `video\tb\`, `textures\xboxload.big`) use a
different, not-yet-decoded wrapper — the one file peeked at manually
(`char\brdxbx.big`) starts with binary bytes then a single embedded path string
(`data/char/board.mxf`), suggesting a single-nested-file wrapper rather than a
multi-file TOC like BIGF. Not decoded further this pass — lower priority than the
BIGF sweep since it would need real format reverse-engineering, not just parsing.

## Confirmed connections to already-documented code

- **`audio\jukebox.big`** — 21 real song files (`adamsrevenge`, `bassinvader`,
  `boardburner`, `bonecracker`, `downtime`, `finsymphony`, `ginandsin`,
  `hiphopphenom`, `itstricky`, `kingofthebeats`, `moveit`, `peaktime`, `reality`,
  `shakemomma`, `smartbomb`, `songfordot`, `superwoman`, `systemoverload`, `topbomb`,
  `slayboarder`, `leader`). Matches `config\jukebox.inf`'s song list exactly. This is
  the authoritative full soundtrack list consumed by `UI_BuildJukeboxList` (was
  `FUN_0009a020`, see `RE_NOTES_frontend_menu_map.md`) — didn't find a matching
  loop-bound constant of exactly 21 in a quick pass of the decompile, not pursued
  further (lower priority than the trick-scoring lead below).
- **`char\texxbx.big`** (324 entries) — per-character texture sets, clean naming
  scheme: `<charname><N>_bord.xsh` (numbered board-skin textures, up to 12 per
  character — matches `UI_BuildBoardSelect`'s domain), `<charname><N>_boot.xsh`,
  `<charname><N>_suit.xsh`, `<charname>_head.xsh`, `<charname>_helm.xsh`. Character
  names seen: `brodi`, `eddie`, `marisol`, (others not fully scanned) — ties to the
  rider-select/character-select screens already documented in
  `RE_NOTES_frontend_menu_map.md`.
- **`config\crowd.inf`** — confirms exactly **3 trick-quality crowd-reaction tiers**
  (`TRICK1`/`TRICK2`/`TRICK3` → `Cheer10`/`Cheer20`/`Cheer30.eam`) and 3 fall-severity
  tiers (`FALL1`-`FALL3`) — a different, coarser tiering than the 4-tier
  `Trick_GetScoreTier` combo system found in `HUD_DrawWorldSpaceMarkers` (`4000`/
  `8000`/`12000`/`16000` → tier 2-5), i.e. the crowd audio reacts to a broader/simpler
  quality bucket than the on-screen combo counter uses.

## Character roster, confirmed via `char\mdlxbx.big`'s embedded names

`char\mdlxbx.big` and `char\brdxbx.big` use a different, custom wrapper format (starts
`c0fb` + a varint-like compact size/offset encoding, not decoded byte-for-byte — but
the embedded filenames are plain readable strings regardless). A `strings` pass over
`mdlxbx.big` confirms the full body/head model roster: **`brodi`, `eddie`, `elise`,
`jp`, `kaori`, `luther`, `marisol`, `moby`, `psymon`, `seeiah`, `zoe`** (11 named
characters, matching the `tutorial\*.big` and `textures\rp_*.xsh` per-character asset
sets already noted in passing elsewhere) plus a `zz_mmm_head.mxf` (likely a
placeholder/default model, `zz_` prefix sorting it last). Useful confirmation for
`RE_NOTES_frontend_menu_map.md`'s character-select screen (`UI_BuildCharacterSelect`)
if a future session wants to map specific tag strings to specific roster names.

## Dead end investigated: the real-time trick-scoring writer

Per the standing open lead (who writes the score at rider `+0x5710`), tried two more
angles this round, both dead ends: (1) `Trick_GetScoreTier`'s only caller is
`HUD_DrawWorldSpaceMarkers` itself — no second call site pointing at a scoring-update
function. (2) Checked `Rider_ConstructBase`'s field-0xc pointer (initially guessed as a
vtable slot) — it's actually a **tuning-data pointer** (raw float constants, not
function pointers), meaning Rider is composed from many per-subsystem config-data
blocks rather than a simple single-vtable class, and its real polymorphic vtable
(offset 0, not inspected this round) is set by a different, earlier constructor in the
hierarchy. Finding the per-frame Rider update/tick method — and from there the actual
trick-detection and scoring logic — would need a dedicated session starting from that
vtable slot 0, not a quick side-investigation. Still the clearest "next big thing" if
continuing this project.

## Assessment: lower code-behavior value than the `.loc` find

Unlike the localization archive (which directly resolved open questions about specific
decompiled functions), this sweep is mostly **asset cataloging** — useful context and
a genuinely reusable inventory for PC-port research, but it doesn't unlock new
understanding of code behavior the way the string dump did. Good candidate for a
future session if the goal shifts toward asset extraction/porting rather than pure
code RE; the `models\*.big`/`char\mdlxbx.big` wrapper format would be the natural next
target there (holds the actual 3D geometry, directly relevant to the already-documented
`BoardMesh_*` functions).
