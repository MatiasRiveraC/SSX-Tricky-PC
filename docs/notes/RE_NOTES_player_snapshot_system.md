# RE notes: the `PlayerSnapShot` replay/save serialization system

Found while sweeping untraced "New_X" tagged allocators for other rich
systems — the same technique that led to the `RiderEvent`/`BdrSeq` animation
system this session (see `RE_NOTES_rider_event_system.md`). `New_PlayerSnapShot`
had only ever been statistically tagged (never traced) since an early session.

## Allocation

**`New_PlayerSnapShot`** (`0x000bbc90`) is called only from `Player_Construct`
(human players specifically, not `OtherRider_Construct`/AI). Allocates a small
28-byte tagged `"PlayerSnapShot"` object into one of **4 fixed slots** at
`player+0x390c`. Poisoned with `0xDEADC0DE` before init, then mostly zeroed
except one caller-supplied field.

## The serialization chain

Found by searching for other code touching the shared `+0x390c` slot-array
offset — all 18 hits cluster tightly in `0x000ba0-0x000bd0`, right next to the
already-documented checkpoint/reset cluster (`NodeRegistry_UpdateAllOfType`,
`GameState_ResetTransientTriggerNodes`, both in the same neighborhood).

- **`PlayerSnapShot_SerializeAllSlots`** (was `FUN_000bc110`) — the entry
  point. Walks all 4 slots, calls `PlayerSnapShot_WriteContainerChunk` for
  each non-null one, reports the total byte delta, increments a counter.
- **`PlayerSnapShot_WriteContainerChunk`** (was `FUN_000bbfd0`) — writes a
  tagged, fixed-size (0x18/24-byte) chunk with magic **`0x11111114`**,
  referencing up to 4 attached sub-objects (fields `+8`/`+0xc`/`+0x10`/`+0x14`
  on the snapshot), then recursively serializes each one.
- **`PlayerSnapShot_WriteSubObjectChunk`** (was `FUN_000bb520`) — writes a
  second tagged chunk type, magic **`0x11111115`**, 0x20/32 bytes. Resolves a
  *live object pointer* to a *portable list index* by walking the
  active-object list at `DAT_001e3c7c+0x72c+0x40` (the same base pointer
  chain used throughout this project for the active-rider/world-object list)
  looking for a matching entry — the classic technique needed to make
  in-memory pointers survive being written to a save file or replay buffer
  and read back later.

## Assessment

This is a genuine, previously-undocumented **binary serialization format**
(sequential tagged-chunk magic numbers `0x1111111X`, pointer→index
resolution) — most plausibly the mechanism behind SSX Tricky's save-game or
replay-file format. Not fully decoded: the exact field semantics *within*
each chunk (what `PlayerSnapShot_WriteContainerChunk`'s `local_14`/`local_10`/
`local_c` fields or `PlayerSnapShot_WriteSubObjectChunk`'s `local_18` actually
represent — position? score? timestamp?) weren't traced to full confidence
this pass. The mechanism and its role are clear; the payload contents are not.

**Not directly connected to the score-writer investigation** — `PlayerSnapShot`
serializes existing player state for save/replay purposes, it doesn't compute
or increment anything. If the score field (`rider+0x5710`) is included in a
serialized chunk somewhere, that would confirm it's a real, live-tracked
value (consistent with everything already known) but wouldn't reveal where
it's *written* during gameplay — a different question this system doesn't
answer.

## Update: found the full save-game pipeline

Followed `PlayerSnapShot_SerializeAllSlots`'s own caller and it resolved
immediately and cleanly:

- **`SaveGame_TickSerializationStateMachine`** (was `FUN_000bc760`) — the
  real top-level pipeline `PlayerSnapShot` feeds into. A **17-state (0-0x11)
  incremental state machine**, capped at `0x4000` (16KB) bytes written per
  call — a classic "spread the work across multiple frames to avoid a
  hitch" streaming design. `PlayerSnapShot_SerializeAllSlots` is just **one
  state (0xb) among 17** — most of the others (game header, misc sections)
  weren't individually read this pass.
- **`SaveGame_TickAndFlush`** (was `FUN_000e9530`) — found by patching a
  mis-analyzed code region (RET+NOP-padding boundary scan, the same
  technique used throughout this project whenever `xrefs_to` comes back
  empty for a suspicious address). Referenced only via a `DATA`/vtable slot
  (`0x0019d530`) — consistent with this project's recurring "per-frame
  virtual dispatch, no traceable static caller" pattern seen for every
  `Update`-shaped method so far. Calls the state machine, and on success
  flushes the produced bytes.
- **`SaveGame_FlushToDevice`** (was `FUN_000e42e0`) — accumulates a running
  byte total, sets a global "save in progress" flag (`DAT_001e98cc` — also
  touched by the level-transition state machine found earlier this session
  while investigating `RiderEvent`'s "chase near" sibling, another
  cross-connection), then calls a vtable method (`+0x90`) on a device/stream
  object — the actual storage I/O (Xbox memory-unit/HDD write).

**This is genuinely the complete save-game/profile-save pipeline**, from
per-player state through incremental serialization to hardware flush. 3 more
renames.

## Update: the complete save-file format, all 17 states read

Read every remaining state of `SaveGame_TickSerializationStateMachine`. The
full, well-understood layout:

| State | Function | Role |
|---|---|---|
| 0 | `SaveGame_WriteHeaderChunk` | 11-dword header: live-object count, a world-time/frame value, 4 GUID/checksum-seed fields, magic=1 |
| 1 | `SaveGame_ChecksumHeaderChunk` | 17-iteration checksum pass over the header |
| 2 | `SaveGame_InitDataRecordLoop` | sets the loop bound for state 3 |
| 3 | `SaveGame_WriteDataRecords` | writes N x fixed 0xe24 (3620)-byte records (contents undecoded) |
| 4 | `SaveGame_InitObjectListIterator` | walks a linked list, sets up state 5's iterator |
| 5 | `SaveGame_WriteObjectListChunk` | per-object 0x18-byte chunk, magic `0x11111113` (object type undetermined, possibly active level-script nodes) |
| 6 | (inline) | resets to state 7 |
| 7 | `SaveGame_WritePlayerSubObjectChunks` | calls `PlayerSnapShot_WriteSubObjectChunk` across 4 fixed categories -- the max-4-local-players pattern |
| 8 | `SaveGame_InitPlayerProfileLoop` | sets a loop bound of exactly 4 (one per player) |
| 9 | `SaveGame_WritePlayerProfileChunk` | per-player (x4): a tagged chunk, magic `0x11111112`, with 2 compressed data blocks (sizes `0x6664`/`0x1999`) -- very likely each player's profile/save-icon thumbnail |
| 10 | (inline) | resets to state 0xb |
| 0xb | `PlayerSnapShot_SerializeAllSlots` | the `PlayerSnapShot` container chunks, magic `0x11111114`/`0x11111115` |
| 0xc | `SaveGame_ComputePaddingSize` | computes padding to reach a fixed total size of `0x7f660` (521,824 bytes, ~510KB) -- a fixed Xbox memory-unit/HDD save-block size |
| 0xd | `SaveGame_WritePadding` | writes the padding, <=16KB per call |
| 0xe | (inline) | resets to state 0xf |
| 0xf | `SaveGame_FinalizeChecksum` | folds 8 more bytes into the checksum |
| 0x10 | (inline) | marks the state machine complete (state -> 0x11) |

**The chunk-magic family is now fully confirmed**: `0x11111111` (trailer,
seen inside the object-list chunk), `0x11111112` (per-player compressed
profile/icon), `0x11111113` (object-list entry), `0x11111114` (`PlayerSnapShot`
container), `0x11111115` (`PlayerSnapShot` sub-object) -- five sequential,
purpose-built tagged-chunk types making up one coherent binary save format.

12 more renames this pass -- the entire `SaveGame_TickSerializationStateMachine`
is now named and understood end to end.

**What's still genuinely unread**: the exact byte layout *within* the 3620-byte
data records (state 3) and the object-list chunks (state 5) -- the mechanism
is fully mapped, but individual field semantics inside those two record types
weren't traced. Also not found: a corresponding *deserialize*/load function
(would very plausibly live in the same `0x000ba0-0x000bd0`/`0x000e9xxx`
neighborhood, not searched for this pass).

**Partial follow-up (later session)**: confirmed two more structural facts
without fully resolving either record type. State 3's 3620-byte records are
read directly from `param_1+0x48+i*0xe24` -- i.e. embedded *inline* within
the save-context object's own memory layout, not from an external
pointer/list; `param_1` is therefore a large (16KB+) object that mixes
serialization bookkeeping fields (`+0x409c` onward) with embedded
save-payload data (`+0x48` onward) in the same struct. State 5's object-list
uses a **self-referential circular list** -- the save-context object's own
first field (offset 0) is the list head, and the sentinel/termination check
is against the save-context object's *own* `+0x1c` field, the same idiom as
a dummy-head circular linked list.

**Identified the object type in state 5's list**: both
`SaveGame_WriteObjectListChunk` (state 5) and `PlayerSnapShot_WriteSubObjectChunk`
read a field at `+0x20` from each listed object and write it into their
respective chunk. That offset is an exact match for `NodeBase`'s own
**instance-ID field** -- confirmed directly against `NodeBase_ConstructRoot`
(`param_1[8] = iVar1`, `8*4 = 0x20`, assigned from one of two global
monotonic counters, `DAT_001e3c80`/`DAT_001e3c84`, per already-documented
`NodeBase` fields, see `RE_NOTES_node_base_class.md`). **Both save-chunk
types are therefore serializing `NodeBase`-family objects by their
globally-unique, save-portable instance ID rather than a raw pointer** --
exactly the technique a save/replay format needs, since live pointers can't
survive being written to disk and reloaded. The *specific* concrete class of
object in state 5's list (which `NodeBase` subtype) still isn't identified.

## Related but distinct: checkpoint-restore deserialization (not the SaveGame format)

Checked one more untraced `New_X` allocator (`New_Dead_Object`, called from
the already-documented `GameState_ResetAndRebuildTransientNodes` checkpoint
system) hoping it might be the `SaveGame` format's missing deserializer.
It isn't the same system, but it's a genuine, related find: `New_Dead_Object`
reads a count then N 4-byte indices from a buffer, allocating a `"Dead
Object"`-tagged instance and resolving each index via the already-documented
`CmdTable_LookupByIndex` -- this is the checkpoint/reset system restoring
which trigger nodes were destroyed, from serialized level-state data, not
from a `SaveGame`-format profile file. The read primitive it (and the whole
`SaveGame` write path) uses, `FUN_00151120`, turned out to be nothing more
than the CRT's own generic alignment-aware `memcpy` -- renamed
**`CRT_MemCopy`** for clarity, not a custom (de)serialization routine. The
actual `SaveGame`-format deserializer/load function is still not found.

## Update: identified the SaveGame context object's own class — it's a UI overlay panel

An earlier attempt to precisely locate `SaveGame_TickAndFlush`'s owning
vtable was abandoned after reading backward from the one known slot produced
an inconsistent boundary. Retried with a cleaner technique: checked `xrefs_to`
across the whole byte range the vtable plausibly occupied (not just the one
already-known slot) and found a genuine constructor reference at
`0x0019d520` — one dword before where the earlier attempt had assumed the
vtable started. **`SaveGame_TickAndFlush` is actually vtable slot 4, not
slot 0 or 1 as first guessed.**

Traced from there:

- **`SaveOverlay_Construct`** (was `FUN_000e8820`) — the real constructor,
  confirmed via `*this = &PTR_FUN_0019d520`. Called with a flag
  (`param_2=1`) from...
- **`OverlayManager_Construct`** (was `FUN_000dd7d0`) — allocates **19
  distinct `"Overlays"`-tagged sub-objects** (sizes from `0x24` to `0x162c`
  bytes), one of which (`0x11dc`/4572 bytes) is `SaveOverlay_Construct`'s own
  panel. This is a top-level manager for many independent HUD/menu overlay
  panels — pause menu, results overlays, and (per this thread) the
  save-progress prompt — **not previously documented as its own system**.
  Called from the already-documented **`InGameState_LoadLevel`**, closing a
  loose thread from an earlier session that noted an `"OverlayNode"` tag in
  passing during that function's read, without ever tracing it to a
  constructor.
- **`SaveOverlay_ResetToBaseState`** (was `FUN_000e9650`) — same initial
  vptr assignment as the constructor, but immediately re-assigns to a
  different, more-base-looking vtable (`0x0019c190`) afterward — reads as a
  state-reset step, not the primary constructor (structural confidence only).

**This confirms the architectural picture**: the `SaveGame` serialization
state machine isn't a bare, freestanding save-file writer — it's the
internal state machine of a **UI overlay panel** (the "Saving..." progress
prompt shown to the player), constructed as part of the same
`OverlayManager` system that owns every other HUD/menu overlay. `param1[0x39e]`
(used throughout `SaveOverlay_Construct`) is a shared-context field set from
a fixed global (`&DAT_0019d61c`) on first construction. 3 renames, all
verified live except the reset function (structural confidence).

## Update: identified 6 more of the 19 overlay panels via localization IDs

Pushed further into `OverlayManager_Construct`'s remaining allocations.
Every panel constructor calls `Localization_ResolveString` with real string
IDs — looked those up directly in the already-decoded
`american_loc_strings.txt` (see `reference-extracted-game-data` in memory)
to identify each panel's exact on-screen purpose with high confidence, the
same technique that cracked the HUD panels in `RE_NOTES_race_hud.md` many
sessions ago:

- **`WorldCircuitNextRaceOverlay_Construct`** — `kOvNextRaceQuarterFinal`/
  `SemiFinal`/`Final` ("World Circuit Race Quarter/Semi/Final"), 3
  placeholder-initialized ("No points") standings slots.
- **`WorldCircuitResultsOverlay_Construct`** — `kOvQuarterFinalResults`/
  `SemiFinalResults`/`FinalResults` — the sibling results-display overlay
  for the same tournament bracket.
- **`NameEntryOverlay_Construct`** — copies the literal character-set string
  `"ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890 !()de"` into a global input buffer
  — the rider name-entry overlay (matches the already-documented frontend
  "Name entry" screen category from a much earlier session).
- **`UnlockNotificationOverlay_Construct`** — resolves 20 unlock-related
  strings (`kUnlockBoard`/`Char`/`Course`/`Outfit`/`ShowoffCourse`/
  `SSXCourse`/`TrickBook`/`StatusLevel`, singular+plural, e.g. `"%d New
  Board"`/`"%d More Characters!"`) — the post-race "you unlocked X"
  notification overlay.
- **`ReplayTitleOverlay_Construct`** — `kOVReplayTitle` ("Replay") — the
  instant-replay title/label overlay, ties to the already-documented
  `ReplayManager` system.
- **`PauseHudDetailOverlay_Construct`** — `kPauseHUDDetailHi`/`Low`/`Med`
  ("full"/"none"/"minimal") — the pause-menu HUD-detail-level option
  overlay. Sets vptr to `0x0019c190` — the same base vtable
  `SaveOverlay_ResetToBaseState` resets to, confirming that address is a
  genuine shared overlay-panel base class, not incidental.

**7 of the 19 `OverlayManager` panels are now identified by name** (the 6
above plus `SaveOverlay`). 6 renames this pass, all verified live with high
confidence (backed by exact localization string content, not guesswork).
The remaining ~12 panels include several small (`0x24`-`0x94`-byte) ones
sharing a common tiny base vtable (`0x0019bd58`) — likely simple icon/
indicator sub-widgets rather than full screens, a reasonable place to pick
up if this thread continues.

## Update (2026-07-20, "change direction" pass): the full OverlayManager access chain, and a new SaveReplayOverlay family

Picked up the standing open item "who writes `SaveOverlay`'s 3620-byte data-record
content" (`RE_NOTES_DECOMP_PROGRESS.md`'s "what's missing" list). Traced
`InGameState_LoadLevel`'s `OverlayManager_Construct` call site via raw disassembly
(not decompiler inference, which hid the exact field offsets) and found the
**complete, concrete singleton-access chain** — previously only described in prose
as "a field", now pinned down exactly:

```
Application (DAT_001e3c7c) -> +0x72c = InGameState -> +0x38 = OverlayManager
    -> +0x34 (slot 0xd of 19) = SaveOverlay
```

The record-content writer itself still wasn't found this pass (finding other code
that independently computes this same 4-level chain needs either more specialized
cross-reference tooling or dynamic analysis — noted honestly as still open, not
force-guessed).

**However, reading through all 19 of `OverlayManager_Construct`'s panel-construction
calls in full (not just the 7 previously identified) turned up a genuinely new
find**: 3 previously-unnamed panels at slots `0x13`/`0x14`/`0x15` share `SaveOverlay`'s
exact size (`0x11dc` bytes for the first) and an almost-identical constructor shape
(same shared-base-vtable step, same self-referential embedded-object idiom). Content-
confirmed via `Localization_ResolveString` id `0x1b9` = **`kOVSaveReplay` / "Save
Replay"** (looked up directly in `american_loc_strings.txt`) — this is a whole
separate **"Save Replay" overlay family**, distinct from the profile/game-save
`SaveOverlay`, matching the already-documented `ReplayManager`/`"Replay Full"` system
found much earlier in this project. Named:

- **`SaveReplayOverlay_Construct`** (was `FUN_000ceb60`, slot `0x13`)
- **`SaveReplayOverlay_ConstructStateB`** (was `FUN_000cecd0`, slot `0x14`)
- **`SaveReplayOverlay_ConstructStateC`** (was `FUN_000cee50`, slot `0x15`)

All three share the same base-construction shape and the same "Save Replay" string,
but each installs a different final vtable — read as 3 sibling states/pages of one
UI flow (e.g. confirm → naming → progress), though the exact per-state role wasn't
individually distinguished — structural confidence, not guessed content.

**10 of the 19 `OverlayManager` panels are now identified.** 3 renames this pass.
The record-content-writer question remains the genuinely open item; the concrete
access-chain discovery is a real, if partial, advance on it.

## Checked the remaining 9 unidentified panels — mostly structurally inert (immediate follow-up)

Sampled representative panels from both size categories among the 9 still-unnamed
slots (4/8/0xb/0xc/0xe/0xf/0x10/0x12/0x16):

- The small ones (`0x24`-`0x94` bytes, slots `8`/`0xb`/`0xc`/`0x10`) share a common
  base vtable (`0x0019bd58`) and copy 3 fields directly from `Application`
  (`+0x5c`/`+0x60`/`+0x724`, likely a camera/shake-curve handle and a related
  pointer, based on `+0x5c`/`+0x60`'s use elsewhere in `InGameState_LoadLevel`).
  Checked slot `0xb`'s full vtable (`0x0019c5d0`) — 5 of 6 slots are the shared
  generic `Node`-family no-op stub (`0x000b6140`), and the one "real" slot is a
  trivial identity function (`return param_1;`). No further identifying content.
- The larger ones (`0x38c` bytes, slots `0xf`/`0x16`) use the same self-referential
  embedded-vtable idiom as `CameraShakeMode` elsewhere in this project. Checked
  slot `0xf`'s vtable (`0x0019c328`) — 6 of 7 slots are the same generic no-op
  stub; the one real slot is just a scalar-deleting destructor
  (`FUN_000ce730`, matches the standard `DAT_001fad60`/`DAT_001fad64` pool-bounds-
  check idiom used throughout this project) chaining to a base destructor. No
  panel-specific logic found.

**Assessment: these remaining panels are almost entirely structurally inert in
this retail build** — the same "N of M vtable slots are stripped no-op stubs"
pattern documented dozens of times elsewhere in this project (`NodeBase`,
`Rider_UpdateSubsystems`, `DebugMenu`, etc.). Not worth individually naming without
a distinguishing tag/string or real logic to point to — left unnamed rather than
forcing weak names, consistent with this project's established discipline. Good
place to stop this specific sub-thread; the `SaveOverlay` record-content-writer
question remains the one genuinely open, high-value item here.

## Update (2026-07-22): found a deserializer for the shared record format — but it targets the AggressionManager host, NOT SaveOverlay

The line above ("Also not found: a corresponding *deserialize*/load function —
would very plausibly live in the same `0x000ba0-0x000bd0`/`0x000e9xxx`
neighborhood") pointed the right direction. Found while searching the
exported `default.xbe.c` for every literal use of `0xe24` (3620, the
confirmed per-record byte stride).

**`AggressionManager_DeserializeStateFromBuffer`** (was `FUN_000bc910`, at
`0x000bc910`, inside the predicted range) parses a buffer in the **exact
same chunk format** as the SaveGame write pipeline, confirmed field-for-
field:

- Validates a header's `+0x18` record count against the exact same
  `DAT_001e3c7c->+0x72c->+0x1c->+0x7c` chain `SaveGame_InitDataRecordLoop`
  reads for its own record count.
- Copies that many `0xe24`-byte (3620) records into `this+0x48`, matching
  `SaveGame_WriteDataRecords`' own `iVar*0xe24+0x48+this` addressing.
- Rebuilds a free-list/active-list pair of `NodeBase`-style intrusive
  doubly-linked nodes from `0x18`-byte records (matching the state-5
  object-list format), calling `FUN_0012d9b0` per node for a size-prefixed
  payload blob; then loads further per-category chunks via 2 unread
  siblings (`FUN_000bb5f0`/`FUN_000bc080`).

### IMPORTANT CORRECTION (same session, register-level disassembly)

My **first** naming of these functions was WRONG and I fixed it. I initially
called the deserializer `SaveGame_DeserializeFromBuffer` and its 3 callers
`InGameState_LoadPendingSaveBuffer`/`ExtractSaveBufferHeader`/
`DiscardPendingSaveBuffer`, on a moderate-confidence "fastcall this-
passthrough" inference that I never register-verified. Disassembling the
actual call site proved the receiver is a **fixed global, not InGameState**:

```
000ad402: MOV ECX,0x1dbf50   ; the object
000ad407: CALL 0x00069af0    ; the "load pending buffer" function
```

And `Application_InitSubsystems` calls `AggressionManager_Init` on the **same
global** (`000aa0b6: MOV ECX,0x1dbf50` / `000aa0c1: CALL 0x0006cf90`). So
`0x1dbf50` is the object the project already calls "the AggressionManager"
— a large static-global host (the aggression/rivalry matrix singleton it
allocates at `DAT_001e13b0`, **plus** a board/trick-name + high-score records
table written by `AggressionManager_ConfigureDefaults` with strings like
"QUANTUM"/"NEOPOLITAN"/"AIR JER"/"RIPPER" and score thresholds, **plus** the
`0xe24`-byte data records at `+0x48`, **plus** the pending-load buffer at
`+0x3940`). All 4 functions operate on this host, so all now carry the
`AggressionManager_` prefix:

- **`AggressionManager_DeserializeStateFromBuffer`** (was `FUN_000bc910`).
- **`AggressionManager_LoadPendingStateBuffer`** (was `FUN_00069af0`) — the
  call site in `InGameState_LoadLevel`, gated on `host+0x3940 != 0`; skips a
  `0x998`-byte header and deserializes the rest.
- **`AggressionManager_ExtractStateBufferHeader`** (was `FUN_00069ac0`) —
  copies that same `0x998`-byte header block into `host+0x29ac`, sets a flag
  at `host+0x394b`. No static callers found.
- **`AggressionManager_DiscardPendingStateBuffer`** (was `FUN_00069aa0`) —
  frees the buffer, no deserialize. No static callers found.

**Two-object situation (key structural insight)**: the SaveGame *writer*
pipeline (`SaveGame_TickAndFlush`, a vtable-`0x19d530` method) runs on
**SaveOverlay** (a heap `OverlayManager` panel), a **different** object than
this deserializer's `0x1dbf50` host. So the writer (SaveOverlay) and this
deserializer (AggressionManager host) are two separate objects that **share
the `+0x48`/`0xe24` record format**. That is why they don't trivially
round-trip on one object. **Working hypothesis, NOT proven**: `0x1dbf50` is
the *live* persistent career/rivalry state, and `SaveOverlay` is a
write-staging snapshot copied from it — which would make the long-standing
"who fills SaveOverlay's records" mystery actually about *what populates
`0x1dbf50+0x48` during gameplay*. Not chased to proof this pass.

### Follow-up (same session): buffer SOURCE found — it's a "Replay load" buffer

The "where does `host+0x3940` get populated" question is now **resolved**.
My "no non-zero write to `+0x3940` anywhere in the static image" claim above
was an artifact of searching the C export by literal offset: the writer
lives in an **unbounded** code region (~`0x93a00`–`0x94010`, a replay
save/load handler cluster Ghidra's auto-analysis never bounded), so it never
appeared in `default.xbe.c`. Querying **live Ghidra's xref engine for the
absolute data address `0x1df890`** (= `0x1dbf50 + 0x3940`) immediately found
the write: `00069a91 [WRITE]`.

Bounded that function via `/create_function` and named it
**`AggressionManager_AllocateReplayLoadBuffer`** (was `FUN_00069a70`):

```c
*(host + 0x3944) = 0x80000;                              // size = 512KB
uVar1 = FUN_00150d70("Replay load", 0x80000, 0x10);      // tagged alloc
*(host + 0x3940) = uVar1;                                // <- the buffer ptr
```

Receiver confirmed `this=0x1dbf50` (`MOV ESI,ECX` / `MOV [ESI+0x3940],EAX`,
Ghidra resolving the target to the absolute `0x1df890`; plus airtight
data-flow — this is the sole writer of `+0x3940`, which
`AggressionManager_LoadPendingStateBuffer` reads and frees). The tag string
`s_Replay_load_001919bc` = **"Replay load"** **definitively identifies the
buffer as a replay-load buffer**.

**What this settles**: the whole `+0x3940` chain is the **REPLAY-load path**
— `AllocateReplayLoadBuffer` grabs a 512KB buffer, something fills it from
disk (the replay `Data.ssx` payload — ties directly to
`RE_NOTES_savecontent_system.md`'s confirmed `Replay` save-content package
type), and `LoadPendingStateBuffer` → `DeserializeStateFromBuffer` restore
that replay into the host object's `+0x48` record store. It was NOT a
general "save game" load and NOT a mid-level checkpoint — it's replay
playback loading.

**Refined hypothesis for the still-open record-content-writer mystery**:
since the deserializer restores REPLAY data into `0x1dbf50+0x48`, the code
that populates `0x1dbf50+0x48` *during a live race* is the **replay
recorder** — the exact counterpart worth finding. The unbounded
`~0x93a00`–`0x94010` handler cluster (which calls
`AllocateReplayLoadBuffer` at `0x93ffc`, the reader `FUN_00094010`, and the
Extract/Discard siblings at `0x941f3`/`0x9421c`) is the concrete,
well-scoped place to resume — it needs `/create_function` boundary work
first, same as this project has done for other unbounded regions. 1 rename
(the newly-bounded allocator).

## Update (same session): bounded part of that cluster — it's the Trick/Board-Select screen, plus one rich unresolved lead

Started walking backward from `AggressionManager_AllocateReplayLoadBuffer`'s
call site (`0x93ffc`) to find its containing function, using
`/read_bytes`-verified RET+NOP-padding+prologue detection, one step at a
time, each new boundary decompiled and sanity-checked before proceeding.

**A costly lesson, caught immediately**: my first instinct was to blindly
probe `/create_function` across a range of candidate addresses in a loop.
This **fragmented an existing, correctly-analyzed function into 4 bogus
pieces**. Caught it right away by decompiling the result and noticing the
body no longer matched sensible code — repaired via `/delete_function` on
the 3 spurious pieces. Lesson: never batch-probe `create_function` across
multiple addresses; verify each single boundary via `/read_bytes` (or at
minimum a decompile sanity-check) before creating the next one. Logged to
`feedback_no_guessing_verify_deeply` memory.

**What the careful walk found**: the whole `0x93770`–`0x93fab` stretch is
the **Trick/Board-Select screen**'s own tick/input/event-handling code —
confirmed via shared field offsets (`+0x27`/`+0x4c`) with the already-named
`UI_BuildBoardSelect` (its screen builder). Named 3 with confidence:

- **`BoardSelectScreen_HandleTeamCommit`** (was `FUN_00093770`) — on team
  change: loads `TrickDef` data, resolves the team via
  `Team_GetPointerByTag`, formats a per-team trick-preview video path
  (`data/video/tb/tb_%s.big`), updates 6 widget children.
- **`BoardSelectScreen_HandleSelectionEvent`** (was `FUN_00093930`) —
  dispatches on an event-index field: codes 0-5 commit a team-selection
  change, codes 7-11 stream a per-team/per-index trick-preview video
  (`data\video\tb\%s_%03d.xss`).
- **`BoardSelectScreen_HandleTrickPreviewSelect`** (was `FUN_00093ae0`) —
  stops any in-progress preview video, resolves the selected trick via
  `TrickDef_GetRecordByIndex`, formats its combo name via
  `Trick_FormatComboName`, pushes the update to 2 widget children.

**Confirmed the `0x93ffc` call site is a genuine auto-thunk** (`SourceType
DEFAULT`, 4-byte JMP-shaped body — it now inherits the
`AggressionManager_AllocateReplayLoadBuffer` name automatically, no separate
script entry needed, same convention used everywhere else in this project).
It has no static callers found — almost certainly reached only via an
indirect jump-table dispatch inside `BoardSelectScreen_HandleSelectionEvent`
(confirmed to have a real switch/jump table over its event-index field), not
proven this pass.

**Two more functions bounded but deliberately left UNNAMED** (this
project's discipline: don't force a name without real class-identity
evidence):

- `FUN_00093d80` — a pure joypad button-check dispatcher (checks presses via
  `FUN_00082a00`/`FUN_000829e0`, switches on its own `+0x44` field). No other
  field overlap with `UI_BuildBoardSelect` found, so its class membership
  isn't confirmed — could be the same screen or a sibling reusing the same
  generic dispatch convention.
- `FUN_00093d6e` — a tiny stub (`return 0xd;`), no static callers, likely a
  vtable-dispatched `GetTypeID`-style getter (a pattern seen many times
  elsewhere in this project) — too little evidence to attribute confidently.

**A genuinely rich, unresolved lead surfaced along the way**:
`FUN_00093c70`, sitting right next to this cluster, is a **constructor**
installing a **different** vtable (`0x00198c40` — NOT the same as the known
`SaveReplayOverlay` family's vtables) and allocating either a `0x80000`
(512KB) or `0x29a8` (10664-byte) buffer depending on a state parameter —
sizes that exactly match the already-found "Replay load" buffer and
`AggressionManager_ConfigureDefaults`' own zeroed sub-block, respectively.
Its sole caller (`0x9ea4a`) sits in yet another unbounded region, not
chased this pass. **This is the most promising concrete next thread** for
resolving how the replay-load buffer gets triggered end-to-end, and
possibly for the broader save/replay-progress state machine — needs the
same careful, single-step `/read_bytes`-verified boundary-finding
methodology applied to `0x9ea4a`'s containing function.

4 renames this update (7 total across both updates this session, including
the earlier `AllocateReplayLoadBuffer`).

## Update (same session): resolved the `FUN_00093c70` lead — it's the Save/Load Memory-Card overlay panel

Traced `FUN_00093c70`'s sole caller (`0x9ea4a`) using the same careful
`/read_bytes`-verified single-boundary methodology, skipping past an
intervening jump table belonging to a sibling function
(`OptionsMenu_HandleDisplaySettingWidgetEvent`, see below) along the way.

**Found the caller allocates a tagged `"cFEStateMCOverlay"` object** (0x3888
bytes) and constructs it via `FUN_00093c70` — this **confirms the mystery
constructor's class identity**: it's the constructor for
`cFEStateMCOverlay`, the FrontEnd (menu) **Save/Load Memory-Card-state
overlay panel**. This directly ties the whole thread into the save/replay-
slot UI system (matches this project's established `cXxx`→`Xxx`
class-name-stripping convention, e.g. `cXBoxGraphicsMan`→`GfxContext`).

Named 3 functions:

- **`FEStateMCOverlay_ConstructIfActive`** (was `FUN_0009e9d0`) — gated on
  `this+0x70==3` (a UI/menu state code). Resolves a mode (0/1/2) from a
  nested field chain (a UI selection index, likely Save/Load/Delete-shaped)
  into a `(mode, isSpecialFlag)` pair, allocates the tagged
  `cFEStateMCOverlay` object, constructs it. Reached only via a DATA/vtable
  xref (slot `0x00199940`, a previously-uncatalogued vtable) — the
  recurring "per-event virtual dispatch, no static caller" pattern.
- **`FEStateMCOverlay_Construct`** (was `FUN_00093c70`) — installs 2
  vtable-family pointers (confirmed **not** the same vtable as the known
  `SaveReplayOverlay` family — a genuinely distinct overlay class), then
  branches on `(mode, isSpecialFlag)`: `mode==0` allocates the `0x80000`
  (512KB) buffer (matching the "Replay load" buffer's size — strongly
  suggests this is the **LOAD** path); `mode==1` allocates the `0x29a8`
  (10664-byte) buffer (matching `AggressionManager_ConfigureDefaults`' own
  sub-block size — suggests a **SAVE/write** path). Exact
  mode-to-Save/Load/Delete mapping not traced further — structural
  confidence only.
- **`OptionsMenu_HandleDisplaySettingWidgetEvent`** (was `FUN_0009e930`,
  the function immediately preceding this thread in the same unbounded
  stretch) — switches on a widget-event code, setting the exact same
  display-option globals `OptionsMenu_CacheDisplaySettingsFromWidgets`
  reads back (`DAT_001dd83c`/`DAT_001dd860`/`DAT_001dd878`/`DAT_001dd874`)
  — the setter counterpart to that already-documented reader. Notably,
  `DAT_001dd878` is the *same* flag `AggressionManager_
  LoadPendingStateBuffer`'s sibling code reads to pick between two
  camera-config sub-arrays — confirming it's a genuine display-quality/HQ
  toggle, not coincidental.

**What this means for the broader picture**: the Save/Load flow now has a
confirmed shape — a FrontEnd overlay (`cFEStateMCOverlay`) is constructed
in either a load-sized or save-sized mode, and the load mode's buffer
(`0x80000`) matches the size of the buffer that
`AggressionManager_AllocateReplayLoadBuffer` fills with "Replay load"-
tagged data. 3 renames.

**Follow-up, same session — the buffer-identity question is RESOLVED
(negative)**: decompiled `FUN_000e26a0` (the buffer-request call inside
`FEStateMCOverlay_Construct`). It's **not** a direct allocator — it
dispatches through a vtable (`this+0x650`, slot `+0x3c`), the exact same
generic per-record/per-screen vtable convention already documented for the
unrelated Challenge system (see `RE_NOTES_challenge_system.md`), and only
caches the requested size at `this+0x20`; no buffer pointer is captured or
stored anywhere. This confirms `FEStateMCOverlay`'s buffer request and
`AggressionManager_AllocateReplayLoadBuffer`'s tagged-pool allocation are
**two structurally different, separate mechanisms** that merely happen to
request matching sizes — not the same buffer. This closes the "prove
they're the same buffer" gap with a definitive negative answer.

## Update (same session): searched for the replay recorder — exhausted via static xrefs, needs dynamic analysis

Tried to find what writes gameplay data into the AggressionManager host's
own `+0x48`/`0xe24`-stride record array *during a live race* — the natural
write-side counterpart to `AggressionManager_DeserializeStateFromBuffer`.
Two angles, both now exhausted:

1. **`xrefs_to` the record array's absolute start address** (`0x1dbf98` =
   `host+0x48`) — **zero hits**. Consistent with a recorder needing
   per-slot *variable*-index arithmetic (`this+0x48+i*0xe24`) rather than
   ever touching the literal start address directly — the exact same
   limitation already documented for the original `rider+0x5710`/`+0x5720`
   mystery, now confirmed to apply here too.
2. **Enumerated every xref to the host global itself** (`0x1dbf50`) — ~90
   hits, almost all `DATA` reads from UI screens (many `UI_Build*`
   functions display rivalry/relationship data for leaderboards, results,
   rider bios, etc. — matching the class's known role). Exactly **one**
   genuine write, which turned out to be a reset/teardown, not a recorder:

- **`AggressionManager_ResetOnRaceFinish`** (was `FUN_000cdf00`) — clears
  the singleton pointer (`DAT_001dbf50 = 0`), resets a few scratch fields,
  and (gated on a flag) queues a position-announcement commentary line via
  the current rider's team tag. Reads as a race-finish/results-transition
  reset of the rivalry/commentary singleton — moderate confidence, its own
  caller sits in an unbounded region not traced this pass.

**Concluding this specific search avenue for real**: finding the actual
replay-content-writer/recorder needs dynamic analysis or a fundamentally
different technique — the same conclusion this project already reached for
the original score-writer mystery. Static xref search on this object is
exhausted. 1 rename.

## Update (same session): mapped FEStateMCOverlay's own vtable — found how AggressionManager's state actually gets RESTORED

Read all 32 raw pointer slots of `FEStateMCOverlay`'s vtable (`0x00198c40`)
via `/read_bytes`. Found a mix of:

- **Genuinely shared/linker-folded methods** reused across *unrelated*
  classes — `Node_NoOpStub1`/`2`, `AudioSystem_NoOpStub`,
  `InputDevice_StubReturnFalse` (all already known), plus a large run of
  slots pointing at the *same addresses* as several `ChallengeMenu_Refresh*`
  functions. These are a shared base-class "refresh display text" vtable
  region both classes inherit unmodified — the same idiom already
  documented dozens of times for `NodeBase`/`Widget`-derived classes in
  this project, not evidence the two classes are otherwise related.
- **Genuinely unique `FEStateMCOverlay` methods** — named below.

Since every address came directly from vtable *data* (not a guess), each
was safe to `/create_function` on directly with no risky probing needed.
One (`FEStateMCOverlay_BeginBufferAllocationIfLoadMode`, `0x93ff0`) turned
out to fully contain the already-named
`AggressionManager_AllocateReplayLoadBuffer` thunk as its own tail-call
(`JMP` rather than `CALL+RET` — confirmed via disassembly, no corruption,
both symbols coexist fine). **Slot 2 resolves the class identity for the
button-check dispatcher deliberately left unnamed in the
`BoardSelectScreen_*` update above** — it's `FEStateMCOverlay`'s own input
handler, not part of the Board-Select screen after all.

**The major finding**: `FEStateMCOverlay_ValidateAndCommitBuffer` (vtable
slot 10) is how **AggressionManager's own persistent state gets RESTORED
from a save file**. Its save-mode branch checksums a `0x29a8`-byte block
and, on a match, **`CRT_MemCopy`s it directly onto `DAT_001dbf54` (=
`0x1dbf50+4`, the AggressionManager host)**, then applies audio-volume
settings and rebuilds the rivalry matrix via `FUN_00069bd0` (the same
function `AggressionManager_Init` calls). Its load-mode branch validates
the "Replay load" buffer's payload against a trailing checksum footer —
confirming that buffer's exact layout: `header (0x998 bytes) + payload +
trailing size/checksum footer (0x7fff8/0x7fffc)`.

This is the **load/restore** side of AggressionManager's persistence — a
real, concrete, disassembly-verified mechanism. It is explicitly **not**
the still-unresolved write-during-gameplay "recorder" (a distinct
question, confirmed exhausted via static analysis above) — but it closes
a closely-related, equally long-standing gap: how the rivalry/settings
data gets back into the live game state after loading a save.

**Named 14 functions this update**:
`FEStateMCOverlay_ScalarDeletingDestructor`/`EnterActiveState`/
`HandleButtonInput`/`InitScreenIfSaveMode`/`ApplySettingsIfSaveMode`/
`BeginBufferAllocationIfLoadMode`/`BeginBufferStreamRead`/
`TickBufferStreamRead`/`ValidateAndCommitBuffer`/`DiscardBufferIfLoadMode`/
`SetOverlayTitleText`/`SetDescriptionVariantA`/`SetDescriptionVariantB`,
plus `Widget_SetListRowsVisibility` (confirmed shared across
`FEStateMCOverlay` and `ChallengeMenu`, named without a class prefix
accordingly). 14 renames.

**This does NOT resolve** the original `rider+0x5710`/`+0x5720`
record-content-writer mystery (a separate question). What it does: closes
the "no deserialize/load counterpart found" gap (a load-side reader for the
shared record format now exists and is named), and — via the register-level
correction — establishes that this reader belongs to the AggressionManager
host, not the SaveOverlay save system. 4 renames (all corrected in place,
net rename count unchanged). **Lesson re-logged** (see
`feedback_no_guessing_verify_deeply`): a this-passthrough inference is not
confirmation; verify ECX at the call site.
