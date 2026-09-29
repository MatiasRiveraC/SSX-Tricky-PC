# RE notes: `SaveContent` — the Xbox save-game content package system

Fresh direction opened 2026-07-21 by surveying extracted game assets:
`data/icon/Replay/SaveImage.xbx` and `data/icon/Settings/SaveImage.xbx` (2
files) had never been traced to any loader code.

## The Xbox XDK save-content package format, decoded from strings

The reference strings found near the loader code are the real, official
Microsoft Xbox XDK save-content package filenames — this is the standard
retail Xbox "save game" package format, not a custom SSX format:

- **`Data.ssx`** — the game's actual save-payload file (the real save data).
- **`SaveImage.xbx`** / **`SaveMeta.xbx`** — the per-save-slot thumbnail
  image and metadata, shown in the Xbox dashboard's save-game browser.
- **`TitleImage.xbx`** / **`TitleMeta.xbx`** — the *game's own* icon/info
  shown in the dashboard's title list (not per-save, one per game).
- **`$$XSIMAGE`** / **`$$XTIMAGE`** / **`$$XTINFO`** — the official
  Microsoft XDK-reserved special filenames used internally by the Xbox
  content-package APIs for Save Image / Title Image / Title Info.
- Raw device paths confirm this targets the real Xbox HDD/memory-unit
  content store: `\Device\Harddisk0\partition0`, `\Device\Harddisk0\Partition%d\`.
- **Two distinct save-content types confirmed** via the extracted asset
  paths matching exactly: `D:\data\icon\Replay\SaveImage.xbx` and
  `D:\data\icon\Settings\SaveImage.xbx` — the game creates separate Xbox
  save-content packages for **replay saves** and **settings saves**.

Also found `"XBOXMemCard"` and `"BlockManMem"` tag strings nearby (memory-
card-oriented allocation tags), and `"BigFileUnpack"` — though the latter
two turned out to belong to a generic, reused block-pool allocator
(`FUN_0012a2e0`) rather than being save-content-specific.

**Follow-up (2026-07-22)**: checked the actual `SaveImage.xbx` files
(`Game Data\data\icon\{replay,settings}\SaveImage.xbx`, 32896 bytes each,
content differs between the two, confirming distinct icon art per
save-content type). Both start with the standard Microsoft **`"DDS "`**
magic — a genuine, standard **DirectDraw Surface** file, the well-
documented official Xbox dashboard save-icon format, not a custom SSX
format needing reverse-engineering. Closes out this project's asset-survey
checklist for the `icon\` folder — nothing further to decode here (a
publicly-documented external format, out of scope for custom format-decode
work). `video\`'s `.mpc`/`.xss` files were already fully investigated in
earlier sessions (`RE_NOTES_title_intro_sequence.md`) — both remaining
extracted-asset folders are now confirmed either decoded or standard/
external, closing out the broader "explore the other data\ subfolders"
thread from `reference_extracted_game_data` memory.

## A large, hard-to-bound NodeBase-derived class

The loader code lives in a genuinely difficult-to-analyze ~2KB code region
(`0x0012be70`-`0x0012c8b0`) with **no `INT3`/`CC` padding anywhere** and only
sparse `RET`+short-NOP-run boundaries — several boundary-finding attempts
(scanning for `CC` runs, `search_address_refs`, direct `create_function` at
guessed addresses) failed or hit false positives before manually walking
forward byte-by-byte from a confirmed boundary at `0x0012be70` to find the
real function starts. (A `search_address_refs` hit on `0x0012c000` turned
out to be a false positive — a coincidental match against an unrelated
16-bit value table, consistent with this project's documented caution about
that endpoint's false-positive rate.)

Found a genuine **~32-slot vtable** at `0x001a7560`-`0x001a75dc`, 2 of whose
slots are the shared `Node_NoOpStub1`/`Node_NoOpStub2` stubs — confirming
this is another **NodeBase-derived class** (matching the architecture
already documented for `CameraScriptManager`/`PowerFXParticles`/`SnowFallMan`/
`ReplayManager`), here managing Xbox save-content packages. Named the 2
methods reached from the actual `SaveImage.xbx`/`Data.ssx` string
references:

- **`SaveContent_EnumerateSaveSlots`** (was `FUN_0012c210`, vtable slot 3,
  moderate confidence) — a large (1182-byte), complex function using
  XAPI directory-enumeration calls (`FindFirstFile`/`FindNextFile`/
  `FindClose`-shaped, via unread helpers `FUN_00153932`/`FUN_00153a39`/
  `FUN_00153a80`) that scans a save-device directory and, per entry, builds
  the full `<entry>\SaveImage.xbx` path — reads as the save-slot browser/
  enumeration step that would populate a load-game UI with each save's
  icon. Internal branch logic (multiple entry-type dispatch paths) not
  traced field by field — named by confirmed overall role, not
  exhaustively reverse engineered.
- **`SaveContent_InitializeDataFile`** (was `FUN_0012c740`) — builds a save
  slot's `Data.ssx` path, sets up buffer/size fields, zeroes a 64KB write
  buffer. The "prepare this save slot's main data file for writing" entry
  point.

Also created (but not yet named) 5 more function boundaries found along the
way while walking through this code region: `0x0012be70`, `0x0012be90`,
`0x0012beb0`, `0x0012c6b0`, `0x0012c710` — small helper functions, part of
the same vtable/class, not individually characterized this pass.

## Still open

- **~30 of the ~32 vtable slots** — a substantial, well-scoped future
  thread. The vtable base (`0x001a7560`) and its 2 known members are a
  solid anchor to resume from.
- **`FUN_0012a940`/`FUN_0012af70`/`FUN_0012b3f0`/`FUN_0012a9b0`** — helper
  functions called from `SaveContent_EnumerateSaveSlots`, sampled briefly
  but not individually named.
- ~~The connection (if any) to the long-standing `SaveGame`/`SaveOverlay`
  record-content-writer mystery~~ **Partially checked (2026-07-22)** — see
  `RE_NOTES_player_snapshot_system.md`'s "found a deserializer" update
  (including the IMPORTANT CORRECTION there). Found a load-side deserializer
  for the shared `0xe24`-byte-record format
  (`AggressionManager_DeserializeStateFromBuffer`, was `FUN_000bc910`). Its
  receiver was **register-verified by disassembly** to be the static global
  AggressionManager host at `0x1dbf50` (`MOV ECX,0x1dbf50; CALL ...` at the
  `InGameState_LoadLevel` call site) — **not** InGameState and **not**
  `SaveContent`'s own object. The buffer it consumes lives at
  `host+0x3940`, whose populating file-load call was **not** found (no
  non-zero write to that offset exists in the static image). So this does
  **not** confirm the `Data.ssx`→records connection — it neither reaches
  this `SaveContent` class's `InitializeDataFile`/`EnumerateSaveSlots`
  methods nor shows where the loaded buffer comes from. The record FORMAT is
  now confirmed to have a live-side reader, but which on-disk file feeds it
  (this class's `Data.ssx`? the replay package?) is still open. A concrete
  remaining sub-thread: trace what writes `0x1dbf50+0x3940` (likely an async
  file-load), which would finally tie the on-disk `Data.ssx`/replay payload
  to this deserializer.
  **Update (same session): the buffer IS a replay buffer.** Traced the
  writer of `0x1dbf50+0x3940`: `AggressionManager_AllocateReplayLoadBuffer`
  (was `FUN_00069a70`) allocates a 512KB buffer tagged literally
  **"Replay load"** (`s_Replay_load_001919bc`). So the deserializer chain
  restores a loaded **replay**, matching this class's confirmed **`Replay`**
  save-content package type. The `Replay`↔deserializer linkage is now
  strongly evidenced by the tag string, not just plausible. The narrowed
  remaining gap: connect the on-disk `Replay\Data.ssx` file read to the
  *filling* of that 512KB buffer (the fill site is in the still-unbounded
  `~0x93a00`–`0x94010` replay-handler region — a `/create_function` job).
  See `RE_NOTES_player_snapshot_system.md`'s "buffer SOURCE found"
  follow-up.

2 renames (plus 5 additional function boundaries created but not yet named).
