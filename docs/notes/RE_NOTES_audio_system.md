# AudioSystem ("BXAudioSystem") — RE Notes

## Discovery path

The tag string `"BXAudioSystem"` (used in a tagged heap allocation) was found at
`0x001a5ddc` via `/search_bytes`. Its single xref led to a guarded
singleton-init function, `AudioSystem_ConstructSingleton` (`0x00116650`):

```c
if (DAT_001f82f4 == 0) {
    void *mem = TaggedAlloc("BXAudioSystem", 0x7df0, ...);   // 32,240 bytes
    DAT_001f82f4 = AudioSystem_Construct(mem, 0xe, 4, param_1);
}
```

`DAT_001f82f4` is the global singleton handle — it already had a very large
number of existing (unnamed-context) readers throughout the binary before this
investigation, consistent with an "audio system handle used everywhere"
global. `AudioSystem_ConstructSingleton` itself is called from
`Application_InitSubsystems`, plus two other call sites not individually
traced (`0x000afcd5`, `0x00084f1e`).

`AudioSystem_Construct` (`0x001159d0`) is the real constructor — confirmed via
`*param_1 = &PTR_FUN_001a5ae0;`, i.e. it installs the vtable at `0x001a5ae0`.

## Vtable

Base: `0x001a5ae0`. **Corrected on a follow-up full re-read: 16 real slots**,
not the ~15 assumed in the first pass (2 slots near the end, 13 and 15, were
skipped the first time and turned out to be genuine, previously-unanalyzed
code — created via `/disassemble_at` + `/create_function` like the other 8
`LAB_`-label slots found earlier). **All 16 real slots now named** (1 is a
pre-existing auto-named thunk, left as-is) — the full vtable is mapped.

| Slot | Address | Name | Confidence |
|------|---------|------|------------|
| 0 | `0x00111600` | `AudioSystem_Destruct` | Structural (standard destructor shape) |
| 1 | `0x0010e550` | `AudioSystem_SetCallbackContext` | Structural (type-gated setter into a field near the known callback-context pointer) |
| 2 | `0x000b6010` | `AudioSystem_NoOpStub` | Verified trivial (empty body) |
| 3 | `0x0010df70` | `thunk_FUN_0012a4c0` | pre-existing auto-named thunk, not renamed |
| 4 | `0x00116f60` | `AudioSystem_TickPendingRequests` | Structural, lower confidence (shape-inferred only). **Actually introduced at inheritance level 8 (`AudioChannelMixer`), inherited unchanged — see below.** |
| 5 | `0x0010df40` | `AudioSystem_StopSoundsMatchingTag` | Structural (selective stop by 4 tag fields). **Actually introduced at level 7 (`SoundGroupManager`), inherited unchanged.** |
| 6 | `0x00118680` | `AudioSystem_StopAllSounds` | Structural (unconditional stop + group-flag reset). **Actually introduced at level 7 (`SoundGroupManager`), inherited unchanged.** |
| 7 | `0x00116ee0` | `AudioSystem_UpdateChannelVolumes` | Structural (per-frame mute/volume/pan tick over 64 channels). **Actually introduced at level 8 (`AudioChannelMixer`), inherited unchanged.** |
| 8 | `0x00111790` | `AudioSystem_Compute3DSoundPosition` | Structural (listener-relative position + pan/distance calc). Genuinely new at this level. |
| 9 | `0x0010c1c0` | `AudioSystem_ReleaseSoundSlot` | Structural (refcount-gated free + state transition). Inherited unchanged from root level 1 (`AudioBankBase`). |
| 10 | `0x0010bc80` | `AudioSystem_AllocateDynamicSoundMem` | **Tag-confirmed** (`"DynamicSoundMem"` literal). Inherited unchanged from root level 1. |
| 11 | `0x0010bcd0` | `AudioSystem_ConfigureDynamicSoundSlot` | Structural (writes size/flag/name into the same slot array `AllocateDynamicSoundMem` uses). Inherited unchanged from root level 1. |
| 12 | `0x001118f0` | `AudioSystem_PlaySoundSimple` | Structural (active-sound-count bookkeeping). Genuinely new — overrides `AudioBank_RegisterSoundInstance` (level 2's own new slot). |
| 13 | `0x00111650` | `AudioSystem_PlayLoopedCue` | Structural (missed in the first pass — found via a fuller vtable re-read). Loop-position-aware "play" variant, likely consuming `MusicManager`'s LOOPDATA table. |
| 14 | `0x00115d60` | `AudioSystem_PlaySoundFull` | Structural (8-param variant, richer). |
| 15 | `0x00115e40` | `AudioSystem_ResolvePriorityTier` | Structural (missed in the first pass). Threshold-bucketing shape matching the already-documented `MedalTier_ResolveFromValue`, applied to audio (plausibly voice-stealing priority). |

15 of 15 real vtable slots now named (6 from the first pass this session, 9
more from a full sweep of the remainder). None of the 9 second-pass names are
tag-confirmed — all are structural/shape-based, and one
(`AudioSystem_TickPendingRequests`) is explicitly lower-confidence than the
rest since its purpose is inferred purely from control flow, not from any
recognizable string or well-known idiom.

## Two internal channel-array systems found

Reading the full vtable surfaced two distinct fixed-size arrays living inside
the `AudioSystem` instance, both indexed by a small integer and touched by
multiple vtable slots:

- **`this+0xb44`, stride `0xc0`, 64 slots** — the "live channel" array.
  Touched by `AudioSystem_UpdateChannelVolumes` (per-frame volume/pan tick),
  `AudioSystem_StopAllSounds`, and `AudioSystem_StopSoundsMatchingTag`
  (selective stop-by-tag, matching 4 tag fields at `+0x26..+0x29` per slot).
- **`this+0xca4`, stride `0x60`, indexed by an explicit slot number** — the
  "dynamic sound memory" array. Touched by `AudioSystem_AllocateDynamicSoundMem`
  (tag-confirmed) and `AudioSystem_ConfigureDynamicSoundSlot` (writes a name
  string + flags into the same slot).

These are almost certainly SSX's equivalent of a fixed-size mixer channel
pool + a separate dynamically-loaded-sound-buffer pool — a common shape for
console audio engines of this era.

## Function details

- **`AudioSystem_AllocateDynamicSoundMem`** (slot 10, `0x0010bc80`): allocates
  a tagged `"DynamicSoundMem"` buffer for one of a fixed array of
  sound-channel slots (stride `0x60`, indexed by `param_2`), lazily — only if
  that slot's buffer isn't already allocated.
- **`AudioSystem_PlaySoundSimple`** (slot 12, `0x001118f0`): increments an
  active-sound-count field (`+0x20`), stores 2 parameters (likely cue
  ID / volume-pitch) into parallel arrays indexed by that count, and a fixed
  callback-context pointer (`this+0x3af4`).
- **`AudioSystem_PlaySoundFull`** (slot 13, `0x00115d60`): 8-parameter
  variant. Resolves the sound via `FUN_0011d200` first (unread), then does the
  same active-sound-count bookkeeping as `PlaySoundSimple`.

Both `PlaySoundSimple` and `PlaySoundFull` were checked via `xrefs_to` — each
shows exactly one `[DATA]` xref (their own vtable slot). No direct static
caller found anywhere else in the binary. This matches the recurring
"reachable only via generic virtual dispatch" pattern seen throughout this
project (Node-family Update methods, OverlayManager panel methods, etc.) —
almost certainly called through a generic "play cue by ID" front-end that
itself dispatches virtually, or from data-driven script/event tables not
resolvable via static xrefs.

## Relationship to prior audio work

`Audio_PlayScaledCue` (named earlier in the project, before this class was
identified) and the `"BXAudioSystem"` tag mention in
`RE_NOTES_application_boot.md` were the only prior audio-system touches. This
session is the first time the owning class itself was traced, vtable-mapped,
and partially named.

## MAJOR FIND: the full 9-level inheritance chain

While tracing `DAT_001f82e4` (a global referenced via vtable calls from
`AudioSystem_ReleaseSoundSlot` and `AudioSystem_ConfigureDynamicSoundSlot`,
originally logged as "a separate device-level singleton, not traced"), it
turned out **not** to be a sibling system at all — it's the true root of
`AudioSystem`'s own class hierarchy. `AudioSystem` is a deep single-inheritance
stack, 9 levels, where each level's constructor calls the previous (base)
level's constructor first, then installs its own vtable:

```
AudioBankBase          (vtable 0x001a5934, 12 slots)  -- root
  -> AudioBank              (vtable 0x001a59d0, 13 slots)
    -> MidiBankManager          (vtable 0x001a5f18)
      -> AudioStreamManager        (vtable 0x001a62c4)
        -> MusicManager                (vtable 0x001a60c8)
          -> SpeechManager                  (vtable 0x001a61f8)
            -> SoundGroupManager                (vtable 0x001a5ea8)
              -> FUN_00116ea0's class                (vtable 0x001a5e00, unnamed)
                -> AudioSystem                            (vtable 0x001a5ae0) -- leaf
```

This also explains an earlier observation: 3 of `AudioSystem`'s own vtable
slots (`AudioSystem_AllocateDynamicSoundMem`, `AudioSystem_ReleaseSoundSlot`,
`AudioSystem_ConfigureDynamicSoundSlot`) share byte-identical function
addresses with 3 of `AudioBankBase`'s raw vtable slots — because they *are*
the same inherited implementation, not overridden anywhere between root and
leaf. Two more `AudioSystem` slots (`StopSoundsMatchingTag`/`StopAllSounds`)
are thin wrappers around two more `AudioBankBase` slots
(`FUN_0010d430`/`FUN_0010d390`), adding bookkeeping on top rather than
overriding outright. This "shared/inherited vtable slot" pattern matches the
Node-family sweep found earlier in this project (`RE_NOTES_rider_update_chain.md`)
— the same architectural idiom recurring in a completely different subsystem.

### Per-level identification confidence

| Level | Constructor | Vtable | Confidence |
|-------|-------------|--------|------------|
| 1 (root) | `AudioBankBase_Construct` (`0x0010bfd0`) | `0x001a5934` (12 slots) | **Tag-confirmed** (`"BankInstances"`) |
| 2 | `AudioBank_Construct` (`0x0010d7f0`) | `0x001a59d0` (13 slots) | **Tag-confirmed** (`"mBank"`/`"mPath"` — literal member-variable names used as debug tags) |
| 3 | `MidiBankManager_Construct` (`0x00118d80`) | `0x001a5f18` (13 slots, no new methods — data-only subclass) | **Tag-confirmed** (`"MIDIBANKS"`) |
| 4 | `AudioStreamManager_Construct` (`0x0011cf70`) | `0x001a62c4` (13 slots, no new methods) | **Tag-confirmed** (`"StreamArray"`) |
| 5 | `MusicManager_Construct` (`0x0011a260`) | `0x001a60c8` (14 slots, +1 shared stub) | **String-confirmed** — reads `data\config\music.inf` via a key-value parser, walking `GLOBAL`/`BASEPATH`/`LOOPDATA` sections into a per-track loop-data table |
| 6 | `SpeechManager_Construct` (`0x0011bfb0`) | `0x001a61f8` (15 slots, +1 shared stub) | **Tag-confirmed** (`"mSpeechInstance"`) |
| 7 | `SoundGroupManager_Construct` (`0x00118690`) | `0x001a5ea8` (16 slots) | Structural, medium — no tag, but its zeroed `this+0xe66`/`+0xe67` fields (group count/array) are read directly by `FUN_001185e0`. **Genuinely introduces 2 overrides here**: `AudioSystem_StopSoundsMatchingTag`/`AudioSystem_StopAllSounds` (both named under the `AudioSystem_` prefix before this deeper vtable comparison was done — left as-is since they're still correctly reachable through an `AudioSystem` instance, but architecturally they belong to this class). |
| 8 | `AudioChannelMixer_Construct` (`0x00116ea0`) | `0x001a5e00` (16 slots) | Structural — named after its 2 real overrides: `AudioSystem_TickPendingRequests`/`AudioSystem_UpdateChannelVolumes` (same naming caveat as level 7 — inherited unchanged into `AudioSystem`, left under that prefix). |
| 9 (leaf) | `AudioSystem_Construct` (`0x001159d0`) | `0x001a5ae0` (**16 real slots**, corrected from the first pass's assumed ~15) | Already established (see above) |

`AudioSystem_Construct`'s own body (now fully read) also references
`data\config\jukebox.inf` (via `FUN_00111110`) in addition to the
`music.inf` read one level down by `MusicManager_Construct` — two distinct
audio config files, consistent with SSX's actual on-disc DVD/jukebox music
feature.

### Why this matters

This single discovery is disproportionately valuable: 8 new constructors
named across a chain that was previously invisible (all were plain
`FUN_xxxxxxxx` labels reachable only by manually walking constructor call
chains, since none of them have external callers beyond the next link up —
each is only ever invoked from exactly one place, its immediate derived
class's constructor). It also retroactively explains the vtable-slot-sharing
oddity, and gives concrete identity to previously-anonymous globals
(`DAT_001f82e4`, `DAT_001f82e8`, `DAT_001f831c`, `DAT_001f88dc`, `DAT_001f88e8`,
`DAT_001f8314` — one singleton pointer per inheritance level, all left as
`DAT_` per this project's convention of not renaming raw data labels for
singleton handles).

## Public-API sweep (found via xrefs to each level's singleton, not just its constructor)

Reading who else references each inheritance level's global handle (not just
who constructs it) surfaced a real "public API" layer sitting beside the
vtables — plain (non-virtual) member functions that take the singleton
pointer as an implicit argument. This is a richer, more productive lead than
tracing more `AudioSystem_ConstructSingleton` call sites turned out to be
(those 2 remaining call sites, `0x000afcd5`/`0x00084f1e`, sit in
never-analyzed code with no containing function — low value relative to this).

- **`AudioBank_ResolveCueEntry`** (`0x0011d200`) / **`AudioBank_DispatchCueToDevice`**
  (`0x0011cef0`) — called from `AudioSystem_PlaySoundFull`. Confirms
  `AudioBank`'s cue system reads directly from `.big` sound-bank archives via
  the already-named `BIG_locateentry` — a concrete link between the audio
  class hierarchy and the archive format decoded earlier this project
  (`RE_NOTES_archive_format_decoded.md`). Bottoms out in generic
  critical-section-protected voice-pool plumbing (likely third-party audio
  middleware) — not chased further, low value.
- **`AmbientZone_UpdateBlendedPosition`** (`0x00117700`) — found via
  `SoundGroupManager`'s singleton. *Not* a `SoundGroupManager` method itself
  — a separate "ambient zone" object (up to 15 sub-emitters, weighted
  position blending) that registers playing instances through
  `SoundGroupManager`. Plausibly SSX's crowd/wind/ambient background-sound
  zone system. Complex function, medium confidence on exact semantics.
- **`MusicTrack_Construct`** (`0x00119bd0`) — found via `MusicManager`'s
  singleton. A per-track music object loader: allocates a tagged
  `"PathfinderStream"` buffer, parses a full adaptive-music beat-grid config
  (`BeatsPerMeasure`/`MeasuresPerBar`/`PhrasesPerbank`/`BeatsPerPhrase`/
  `PhraseAlign`/`DelayCount`/`DelayTime`/`DelayFeedback`/`DelayLevel`/
  `PathLevel`/`AsyncLevel`, plus note-length keywords). Very likely the
  loader for SSX Tricky's dynamic/layered music system.
- **`SpeechManager_TriggerLineByEventCode`** (`0x0011bcd0`) — found via
  `SpeechManager`'s singleton. Matches an event ID against a registered
  speech-line table, queries beat-timing (plausibly against a `MusicTrack`
  instance), and schedules playback via the already-known
  `Rider_ScheduleTimedCallback` — voice lines are triggered **beat-synced to
  the music**, not immediately. A genuine cross-system link between
  `SpeechManager`, `MusicTrack`, and `RiderEvent`
  (`RE_NOTES_rider_event_system.md`).
- **`MidiBankManager_Destruct`** (`0x00118de0`) / **`MidiBankManager_QueueSoundEvent`**
  (`0x00119050`) — `MidiBankManager`'s own destructor and its one other
  public method (the same "register a playing instance" bookkeeping idiom as
  `AudioBank_RegisterSoundInstance`, applied to MIDI-triggered events).

7 renames from this sweep. Confirms the inheritance chain isn't just
structural — each level genuinely owns distinct, non-trivial functionality
reachable from outside the class hierarchy (other game systems call into
these singletons directly, not just through the flattened `AudioSystem`
vtable).

## Full sweep completion: two new classes found (`AmbientZone`, `SpeechLineSet`)

Finished reading every remaining referencing function for
`SoundGroupManager`/`SpeechManager`/`AudioStreamManager`'s singletons (16 more
renames). This closed out the sweep and — more importantly — revealed that
most of what looked like "more `SoundGroupManager`/`SpeechManager` methods"
actually belongs to two previously-unseen classes that *use* those singletons
as shared registries, exactly the same relationship `AmbientZone` (found
earlier via `AmbientZone_UpdateBlendedPosition`) already had to
`SoundGroupManager`:

- **`AmbientZone`** (alongside `SoundGroupManager`) — now has 7 named
  methods: `AmbientZone_StopAndReset`, `AmbientZone_PlayRandomFromPrimaryGroup`/
  `PlayRandomFromSecondaryGroup` (random-weighted sub-sound picks from a
  `SoundGroupManager` group table, disambiguated by a `+n+1` vs `-1-n`
  encoding), `AmbientZone_ScalarDeletingDestructor` (a combined single/array
  destructor — the array path confirms `AmbientZone` instances live in the
  exact 400-byte-stride record array `SoundGroupManager_Construct`
  zero-initializes), `AmbientZone_AddEmitter` (up to 15 sub-emitters per
  zone, matching the array size seen in `AmbientZone_UpdateBlendedPosition`),
  and `AmbientZone_TickFadeTimer`. Also finally found **`SoundGroupManager`'s
  own destructor** (`SoundGroupManager_Destruct`, confirming its vtable
  address) — which chains down to `SpeechManager_Destruct`, directly
  confirming destructors walk the inheritance chain in reverse, mirroring
  the constructor chain.

  **Update (2026-07-20, much later session)**: found the actual runtime
  *discovery* mechanism these zones rely on — a genuine spatial-hash-grid
  proximity query, `AmbientZone_QueryInfluencesNearPosition`, called once
  per active local player's position every level load
  (`Rider_InitAmbientZoneInfluences`, found while closing out this session's
  `Rider_UpdatePhysicsState` sub-call sweep — see
  `RE_NOTES_rider_update_chain.md`). It computes a grid-cell index (the same
  floor-divide-by-cell-size technique `TerrainGrid` uses), walks nearby
  zones testing 4 shape types (sphere/ellipsoid/directional-cone/binary
  sphere), and registers any in-range zone's blended influence into a fixed
  40-slot tracking array via **`AudioSystem_RegisterActiveZoneInfluence`**
  (confirmed `this`=`AudioSystem` via a gate-byte offset matching its own
  ~32KB size) / **`AudioSystem_FindActiveZoneInfluence`**. This closes the
  gap between "here are `AmbientZone`'s own methods" and "here's how a
  rider's position actually finds and blends nearby zones" — the real
  runtime backing store for this whole class's influence.
- **`SpeechLineSet`** (alongside `SpeechManager`) — a per-instance object
  holding a set of speech lines, distinct from `SpeechManager` itself:
  `SpeechLineSet_Construct` (tag-confirmed via a shared, refcounted
  `"SpeechConfig"` buffer and reads `data\config\speech.inf`) /
  `SpeechLineSet_Destruct`. The actual `speech.inf` key-value parser was
  also found and named: **`SpeechManager_ParseSpeechConfig`** (same
  config-parser API as `MusicTrack_Construct`'s `music.inf` parsing,
  splitting `"name|file"`-shaped values). Plus `SpeechManager`'s own
  destructor (`SpeechManager_Destruct`, chains to `MusicManager_Destruct`),
  `SpeechManager_CanInterruptLine` and `SpeechManager_StopLine`/
  `UpdateActiveLines` (a small, fixed 2-slot "currently playing" array).
- **`MusicTrack_Destruct`**/**`MusicManager_Destruct`** — the destructor
  counterparts to `MusicTrack_Construct`/`MusicManager_Construct`, closing
  out the last 2 `AudioStreamManager`-area leads.

**Destructor chain now independently confirmed** (not just constructors):
`SoundGroupManager_Destruct` → `SpeechManager_Destruct` →
`MusicManager_Destruct` → (continues down through the rest of the chain,
not individually re-verified below `MusicManager`) — each level's destructor
calls the next base level's destructor before its own vtable is overwritten,
exact mirror of the constructor-chain discovery.

23 renames from this whole continuation (7 + 16).

## Next steps (not yet done)

- The `AmbientZone`/`SpeechLineSet` discovery suggests it's worth checking
  whether other "small satellite classes that register through a singleton"
  exist alongside the other inheritance levels (`AudioBank`, `MidiBankManager`)
  — the same xref-sweep technique (check callers of each singleton global,
  not just the constructor) keeps paying off.
- Each of the 9 inheritance levels' own vtables (beyond the root
  `AudioBankBase` and leaf `AudioSystem`, both fully read) likely still have
  slots worth reading in detail.
- The `AudioSystem` vtable itself is fully mapped; further depth there would
  mean reading the helper functions each slot calls (e.g. `FUN_0010ca30`'s
  full distance-attenuation math) — lower priority than new-class discovery.
- `FUN_001170c9` (one SoundGroupManager-area reference) was skipped — its
  decompile shows `unaff_EBX`/`in_EAX`/`in_SF`/`in_OF` register-recovery
  warnings, meaning Ghidra likely has the wrong function boundary here (the
  known "mis-placed entry point" issue documented in
  `reference_ghidra_mcp_connection.md` — needs `/delete_function` +
  recreate at the correct entry to fix, not attempted this pass).

## Update (2026-07-21): the master per-race audio-config loader, and a self-correction

Picked another genuinely untouched extracted file, `Game Data\data\config\chant.inf`
(crowd/character chant sound-bank config, previously unmentioned anywhere in this
project). Searching for its literal path string led to a real, valuable
**correction of an earlier mistake from this same session**: `FUN_00112870` had
been dismissed a few entries earlier (while chasing `TrackIntroMusic_SelectAndPlay`)
as "a much larger, unrelated audio-state reset ... genuinely tangential, not
pursued." Re-examining it via raw disassembly (not just re-reading the same
decompile) found it directly loads `chant.inf` — a real, concrete connection the
earlier pass missed.

Found a **7-entry master table of every `.inf` config path this project knows about**
(`0x001c1c74`-`0x001c1c8c`), and checking `xrefs_to` on each slot revealed a clean,
complete picture tying 3 already-named functions and 2 newly-found ones together:

| Config file | Loader |
|---|---|
| `jukebox.inf` | `AudioSystem_Construct` (already named) |
| `intromus.inf` | `TrackIntroMusic_SelectAndPlay` (already named) |
| `snow.inf` | **`AudioSystem_LoadSnowConfig`** (new — was `FUN_001123f0`) |
| `audio.inf`, `banks.inf`, `crowd.inf`, `chant.inf` | **`AudioSystem_LoadRaceConfigs`** (new — was `FUN_00112870`) |

**`AudioSystem_LoadRaceConfigs`** is the master per-race audio-config loader:
resets a large audio-state block, branches on `GameMode_Current`, reads the
current track's name (via the already-named `TrackInfo_GetRecordByIndex`), then
loads and parses 4 config files directly (each via the same generic INI section/
key parser shape — repeated `__stricmp` key-name comparisons) plus calls
`AudioSystem_LoadSnowConfig` for `snow.inf`. **Called directly from the
already-named `InGameState_LoadLevel`** — the same central level-loading function
that also loads `trickdef.dat` — a well-anchored, confirmed per-level-load
initialization, not a guess.

Also named **`AudioSystem_ReloadConfigs`** (was `FUN_00111a10`) — a sibling
sharing the identical parsing shape and touching the same `audio.inf`/`banks.inf`
table slots, likely an alternate entry point (re-entering from a menu, or a
"reload without full reset" variant); its exact distinguishing context from
`AudioSystem_LoadRaceConfigs` wasn't fully traced.

**Note on `banks.inf`/`musicmap.inf` and the `btnmap0.dat`/`btnmap1.dat` detour**:
while searching for genuinely untouched files, briefly re-investigated
`btnmap0.dat`/`btnmap1.dat` before realizing they were already fully documented in
`RE_NOTES_control_scheme.md` (same conclusion independently re-derived: zero
code references, a build-time/design-time config). No new information there —
flagged here so a future pass doesn't repeat the same detour.

3 renames total for this update.

### Immediate follow-up: pinned down `AudioSystem_ReloadConfigs`'s exact context

Chased its caller and found **`FEAudioConfig_Construct`** (was `FUN_00084540`) — a
tiny constructor wrapper that immediately calls `AudioSystem_ReloadConfigs`. This is
called from the already-named **`FEInit_Boot`** — confirming
`AudioSystem_ReloadConfigs` is the **FrontEnd/menu** audio-config entry point,
parallel to `AudioSystem_LoadRaceConfigs`'s **in-race** path (`InGameState_LoadLevel`).
Two clean, parallel entry points into the same config-loading system: one for
entering the menu, one for entering a race. Also named
**`FEAudioConfig_ScalarDeletingDestructor`** (was `FUN_00084cf0`, the standard
destructor pair). The second caller (`0x00084f8f`) sits inside a large (~0x680-byte)
unanalyzed code gap — not bisected, since the primary context is already confirmed
via the first caller and further exploration would be low-value blind guessing.

2 more renames (3 + 2 = 5 total for this whole audio-config-loader thread).

### Checked the remaining untouched `.inf` files — one already covered, one genuinely new

Checked `musicmap.inf`, `speech.inf`, and `music.inf` (the last 3 config files not
yet examined). `speech.inf`/`music.inf` turned out to already be thoroughly
documented earlier in this file (`SpeechLineSet_Construct`/`MusicManager_Construct`).
`musicmap.inf` (a per-track playlist file, e.g. `[GARI] SONG="System"/"Smart"/"Adam"`)
was genuinely new — named **`MusicManager_LoadTrackSongList`** (was `FUN_0011ae40`,
tag-confirmed via Ghidra's own auto-named symbol): looks up the section matching
the current track and builds a bitset of up to 4 available songs. **Called only
from `AudioSystem_LoadRaceConfigs`** — extends that per-race audio-config chain
one step further: `audio.inf`/`banks.inf`/`crowd.inf`/`chant.inf`/`musicmap.inf`
are all now direct calls from it, plus `snow.inf` via `AudioSystem_LoadSnowConfig`.

1 more rename (5 + 1 = 6 total for this whole audio-config-loader thread). This
now accounts for 9 of the 11 known `.inf` config files in this project
(`nascript.inf` still only passingly mentioned elsewhere).

### Closed the last one: `nascript.inf`, the narrator speech-concatenation script

`nascript.inf` had only been passingly mentioned across 3 other `RE_NOTES_*.md`
files, never individually traced. Its own header comment explains the format: each
numbered entry under a track section (e.g. `[ALASKA]`) holds a comma-delimited list
of numeric sample-segment IDs (e.g. `"5002, 1066, 5001"`), with a lone `b`/`B` token
marking a synchronization breakpoint — pre-recorded speech clips get concatenated at
runtime to build dynamic announcer commentary lines.

Found and named **`NarratorSpeech_LoadScriptData`** (was `FUN_00121cc0`,
tag-confirmed via the `"NarrSpeechBreakpoints"`/`"NarrSpeechSamples"` allocation
tags): parses exactly this format — walks each key's comma-delimited value string,
converts each token to an integer sample ID, and stores a `-1` sentinel for the `b`/
`B` breakpoint marker, matching the file's own documented convention exactly.
**Called only from `AudioSystem_LoadRaceConfigs`** — the same central per-race
audio-config loader every other `.inf` file in this thread traces back to.

1 more rename (6 + 1 = 7 total). **This closes out all 11 known `.inf` config
files in this project with a confirmed loader** — `audio.inf`/`banks.inf`/
`crowd.inf`/`chant.inf`/`musicmap.inf`/`nascript.inf` (all direct calls from
`AudioSystem_LoadRaceConfigs`), `snow.inf` (`AudioSystem_LoadSnowConfig`),
`jukebox.inf` (`AudioSystem_Construct`), `intromus.inf`
(`TrackIntroMusic_SelectAndPlay`), `music.inf` (`MusicManager_Construct`), and
`speech.inf` (`SpeechLineSet_Construct`). A genuinely complete subsystem map.

### Checked, not found: who actually plays the parsed narrator commentary lines

`NarratorSpeech_LoadScriptData` parses `nascript.inf` into 2 fields on the
`AudioSystem` singleton itself (`this+0x7dd0` array, `this+0x7dcc`/`this+0x7dd4`
count/breakpoint fields — confirmed within the already-known `0x7df0`-byte
`AudioSystem` allocation), but who *reads* that data to actually concatenate and
play a commentary line was checked and not found. Ruled out: the already-named
`SpeechManager_TriggerLineByEventCode` (a different mechanism — single `.dat`
file per line, not multi-segment concatenation) and `AudioSystem_DispatchAnimationCueEvent`
(voice-line requests route through different sub-functions, no match on the
narrator fields). A direct raw-byte search for `this+0x7dd0` across 6 common
x86 register encodings (`MOV reg,[reg+0x7dd0]`) found **zero matches anywhere**
in the binary — the field is written but, as far as static analysis can tell,
never read. Genuinely exhausted for this pass via multiple independent
techniques, not simply unchecked — a good candidate for dynamic analysis if
this thread is revisited.

---

## `.bnk` sound-bank format decoded (2026-07-28)

Banks live inside `data/audio/*.big` and are the container for all SFX.

```
+0x00  'BNKl'
+0x04  u16  version (5 in every shipped bank)
+0x06  u16  slot CAPACITY -- not the record count (zbxfe.bnk says 65, holds 13)
+0x08  u32  offset of the sample data
+0x0c  u32  a size field
+0x10  u32  sparse slot table (does not resolve to record starts; scan instead)
```

Each sound is an EA **`PT`** record: the tag `PT` + u16, then fields until key
`0xFF`. A field is `[key][len][BIG-endian value]`; keys `0xFD`/`0xFE` are
standalone markers with no length byte.

| key | meaning |
|-----|---------|
| 0x82 | channels (absent => mono) |
| 0x84 | sample rate |
| 0x85 | sample count |
| 0x88 | data offset |
| 0x0e | volume (two records can share one 0x88 and differ only here) |
| 0x89, 0x8a, 0x8b, 0x8c, 0x06, 0x0b, 0x11, 0xa0 | not identified |

### Bank sounds have NO 4-byte predictor-history header

This is the trap. The `.mpc` SCDl blocks `eaxa::decodeChannel` was written for
begin with a 4-byte history header; **bank sounds do not**. Verified on
`zbxfe.bnk` sound 5: reading EA-XA frame headers at `dataOffset+0` gives
coefficient indices 0 and 1, while starting 4 bytes in gives 14, 10, 9 --
impossible, since EA-XA defines only 4 coefficient pairs.

Skipping four bytes desynced every frame and made all five test banks decode to
**100% full-scale clipping** (peak pinned at 32768 in every sound). After the
fix, peaks are 28672 / 32012 / 31790 -- normal audio. `port/tests/asset_test.cpp`
now fails the build if more than 2% of samples clip, so the bug cannot return.

### Bank survey (via the new reader)

| bank | sounds | duration range | under 0.5 s |
|------|--------|----------------|-------------|
| `zbxfe.bnk` (front end) | 13 | 0.06 - 7.42 s | **1** |
| `zBxsfx.bnk` (general SFX) | 43 | 0.09 - 6.26 s | **13** |
| `zboard.bnk` | 64 | 0.32 - 6.23 s | 7 |
| `LoadingScreen.bnk` | 3 | 0.06 - 2.53 s | 1 |
| `tricky.bnk` | 3 | 1.26 - 2.42 s | 0 |

The UI blips are the short entries -- `zBxsfx.bnk` holds 13 of them.

**STILL NOT FOUND: which cue the front end plays on menu navigation.** The FE
input handlers (`FEStateMCOverlay_HandleButtonInput`) only return event
bitmasks, `Audio_PlayScaledCue` has only a ScriptVM caller, and both functions
that looked like the audio path turned out not to be audio (see the progress
notes: `Widget_PlaySound` is a render-layer scope, `Audio_PlayScaledCue` scales
camera distance). The port currently uses `zbxfe.bnk`'s single short sound,
which is a plausible candidate in the right bank, NOT a verified match.

Port: `port/src/assets/bnk.{h,cpp}`.

## `data/config/*.inf` -- the audio system is config-driven (VERIFIED)

The audio group names that show up as string constants around `0x001a5c00`
(`MAIN`, `BOARD`, `BANK`, `CROWD`, `TRICKY`, `SWAP`, `BASEPATH`, `CHARSPCH%d`,
`SFXLEVEL`, `MUSICLEVEL`, `MENU`, `FELEVELSCALING`, `IGLEVELSCALING`, ...) are
keys in the shipped `.inf` files, not hardcoded logic. The engine formats bank
paths with `"data/audio/%s"` (constant at `0x001a5b40`).

**`banks.inf` settles which bank the front end uses:**

```
[FE]
    MAIN = "zbxfe.bnk"
    BASEPATH = "|data\speech\"
    CHARSPCH0 = "charfe\feselect0.bnk"
    CHARSPCH1 = "charfe\feselect1.bnk"

[TRICK]
    MAIN = "zbxsfx.bnk"
    BOARD = "zboard.bnk"
    ...86 SWAP banks
```

So `zbxfe.bnk` being the front-end bank is now **the game's own statement**, not
an inference. `audio.inf` supplies the mixing levels
(`[FELEVELSCALING] SFXLEVEL = 100, MUSICLEVEL = 95`).

Section lookup uses a SUBSTRING match against the track file name -- banks.inf
says so in its own header comment ("the SECTION NAME must appear somewhere in
TRACK FILE NAME"). `garibald` -> `[GARI]`. Keys MAY REPEAT (`[TRICK]` has 86
`SWAP` lines), so values must be kept as a list, not a map.

Format: `#` comments, `[SECTION]` headers, `KEY = "quoted"` or `KEY = 100`.

Ported as `port/src/assets/inf.{h,cpp}`; the port now resolves its front-end
bank and SFX level through these files instead of hardcoding them.

`intromus.inf` and `musicmap.inf` have **no FE section**, so the front end has
no track song entry -- its audio is `zbxfe.bnk` itself.

**Still open:** the cue INDEX for menu navigation. `zbxfe.bnk` holds exactly one
sound under half a second (0.06 s mono) against twelve 1.5-7.4 s stings, which
is the port's choice, but the code path that selects it is still unfound.

### Decoder verification -- and a correction to my own measurement

The `.bnk` decode is **EA-XA, exactly as `eaxa::decodeChannel` already
implemented it**, with two bank-specific facts:

* bank sounds have **no** 4-byte predictor-history header (the `.mpc` SCDl
  blocks do) -- proven by frame-header coefficient indices: 0/1 at `+0`
  (valid, only 4 pairs exist) vs 14/10/9 at `+4` (impossible);
* a sound's channels are stored **consecutively**, not frame-interleaved --
  decoding sound 0 both ways gives peak 29718 consecutive vs a saturating
  32768 interleaved.

**Correction to an intermediate claim in this session.** I briefly concluded the
decode was still wrong because every sound showed a zero-crossing rate of
0.39-0.54. That measurement was invalid: I computed ZCR over the INTERLEAVED
stereo stream, which alternates between two different signals and inflates the
rate. Measured per channel, every real sound is 0.02-0.13 -- clean audio. Two
other blind alleys were also ruled out along the way: the data is not MS/Xbox
ADPCM (36-byte-block predictor bytes reach 0x17 and 0x2c, must be 0..6), and a
brute-force sweep of EA-XA shift/coefficient/nibble-order variants found nothing
better than the standard formula.

**`zbxfe.bnk` sound 5 is not audio.** It is the only record in the bank with no
`0x82` (channels) and no `0x89` (byte length), it carries key `0x11 = 30`, and
it decodes to broadband noise (ZCR 0.54 against 0.02-0.13 for the other twelve).
`bnk::Sound::looksLikeAudio()` now requires both keys, and the port will not
select such a record. This was the cue the port had been playing -- which is why
it sounded wrong.

**Shape of the problem that remains.** Every genuine sound in the front-end bank
runs **1.5-7.4 s**; there is no short blip in it at all. The only sounds short
enough to be a menu-navigation click (0.09-0.31 s) live in `zBxsfx.bnk`, which
banks.inf assigns to `[TRICK]`, not `[FE]`. So either the front end also loads
the general SFX bank through a path not yet found, or the navigation sound is
one of the longer FE stings. Unresolved.

**Tooling:** `port/build/bnkdump.exe <bank> <outDir>` exports any bank to .wav.
The port's cue is overridable at runtime with `SSX_FE_NAV_BANK` and
`SSX_FE_NAV_CUE` so candidates can be auditioned without a rebuild.
