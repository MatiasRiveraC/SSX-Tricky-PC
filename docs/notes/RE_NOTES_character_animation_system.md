# RE notes: Character animation-set loading system (`data/char/anm.big`, `.afl`)

Fresh direction opened 2026-07-21 by surveying extracted game assets for
untouched ground. `data/char/anm.big` (one of the 4 per-character archives,
alongside `mdlxbx.big`/`brdxbx.big`/`texxbx.big`) had never been traced to a
specific loader — the generic `.big`/RefPack container format and the
level-script `cMeshAnim` skeletal-animation *object* were already documented,
but the actual per-character animation-set *file loading* system was not.

## The `.afl` format and the Gimex codec connection

Found via the format string `"|data/char/%s.afl"` — animation entries are
packed inside `anm.big` as individually-named `.afl` ("animation file list"?)
blobs, loaded through **the exact same Gimex bitmap codec** already documented
in `RE_NOTES_loading_screen.md` (`GimexBitmap_GetDecodedSize`/`DecodeDispatch`).
This confirms the Gimex codec isn't texture-specific either — it's the
engine's universal compressed-blob decoder, now confirmed used for at least
3 distinct data kinds: textures (`.xsh`), animation lists (`.afl`), and
whatever general pack-file entries `FILE_loadpack` handles.

## The loader chain

- **`CharacterAnimSet_LoadSlotSync`** (was `FUN_0005fdb0`) — builds the
  `.afl` path from a name-table entry, loads it via `FILE_LoadRawFileSync`,
  decodes it via the Gimex codec.
- **`CharacterAnimSet_QueueSlotAsync`** (was `FUN_0005f7d0`) — the async
  twin, issuing the same load through a virtual file-load call instead of
  blocking.
- **`CharacterAnimSet_PreloadSlot`** (was `FUN_00060130`) — dispatches a
  single slot to the sync or async loader, guarded against double-queueing.
- **`CharacterAnimSet_PreloadCommonSlots`** (was `FUN_00060ee0`) — calls
  `CharacterAnimSet_PreloadSlot` with ~37 fixed slot indices unconditionally
  — a bulk "load every common animation set for this character" preloader
  (the ~37 call sites are an unrolled loop over fixed indices, not a
  per-value dispatch, despite the large call-site count at first glance).
- **`CharacterAnimSet_ResetSlotState`** (was `FUN_00060090`) — buffer-size
  rounding + per-slot state reset, run at setup.
- **`CharacterAnimSet_SelectAndPreload`** (was `FUN_00062010`) — the
  top-level entry point. **Called directly from `FEInit_Boot`,
  `InGameState_LoadLevel`, and `Race_ResetPlayerRoster`** (all already-named
  core boot/level functions) — confirming this is a central, load-bearing
  subsystem, not a peripheral one.

## The animation-set name table — a third independent roster confirmation

The name table (`s_cmanim` symbol, base `0x001ac828`, **150+ entries**) is
extremely rich. First 16 entries are shared/intro sets:

```
cmanim, bxanim, exanim, franim, feanim, ed2anim, lu2anim, kr2anim, mr2anim,
ps2anim, br2anim, jp2anim, stg_com_1..4
```

Then a clean, repeating **character × venue** block — one 9-10-entry group
per character, each suffixed `_V<track>`:

```
Mac_Vgari, Mac_Vsnow, Mac_Velys, Mac_Vmesa, Mac_Vmerc, Mac_Vtoky,
Mac_Valoh, Mac_Valas, Mac_Vuntr, Mac_Vpipe
```

...repeated identically for **Edd, Psy, Mob, Mar, Eli, Kao, JP, See, Bro,
Zoe, Lut** — **12 three-letter character codes total, exactly matching**
the already-established roster (`LoadScreen_FormatRiderTextureName`,
`RE_NOTES_loading_screen.md`): Eddie, Kaori, Luther, **Mac**, Moby, Zoe, JP,
Elise, Psymon, Seeiah, Brodi, Marisol. **This is the third independent
confirmation this session that the 4th roster slot is `Mac`, not `Marty`**
(after `LoadScreen_FormatRiderTextureName`'s switch table and the earlier
manual-credits cross-check) — `Mar` here is **Marisol**, not Marty (which,
per the confirmed NTSC-build explanation in the official-manual-terminology
memory, doesn't exist in this binary at all).

The 10 venue suffixes (`gari/snow/elys/mesa/merc/toky/aloh/alas/untr/pipe`)
also cross-confirm the `TrackTable`/`LoadScreen_FormatTrackTextureName`
track list (10 of the 12 tracks — tutorial/trick modes apparently don't need
per-venue rider animation variants).

After the character×venue block (index 136+), the pattern shifts to
race-outcome animations, still keyed by character:
```
FL_COM_1, MAC_CON_1..4, MAC_LOS_1..4, MAC_RCON_1..4, MAC_WIN_1, ...
```
(`WIN`/`LOS` clearly win/lose animations; `CON`/`RCON` not decoded — possibly
"congratulate"/"race-continue" or similar). Table continues beyond what was
read this pass (150 entries dumped, more likely follow per remaining
characters) — full enumeration not completed, but the pattern is clear and
consistent.

## The storage structure, resolved via raw disassembly

Per this project's standing rule to verify ambiguous calling-convention
questions via disassembly rather than guess, traced exactly how
`CharacterAnimSet_PreloadSlot` calls `CharacterAnimSet_LoadSlotSync`:

```
LEA ECX,[EDI + EAX*8 + 0x1e4]   ; EAX = slotIndex*3, so this = base+0x1e4+slotIndex*0x18
CALL CharacterAnimSet_LoadSlotSync
```

This confirms a genuine **per-slot record array**: 24-byte (`0x18`) stride,
one record per `s_cmanim` table entry, based at `+0x1e4` within the owning
object. Each record's fields (populated by `CharacterAnimSet_LoadSlotSync`)
are: `+0x0` a pool/arena handle, `+0x4` the decoded-data base pointer,
`+0x8`/`+0xc`/`+0x10` sub-pointers computed from two offset fields inside
the decoded buffer's own 12-byte header (`buffer+4`/`buffer+8`).

**The owning object turned out to be a single global singleton**, not a
per-character instance — confirmed via disassembling `InGameState_LoadLevel`'s
call site: `MOV ECX,0x1cc520` (a fixed immediate) immediately before calling
`CharacterAnimSet_SelectAndPreload`. Named this global **`g_CharacterAnimBank`**
(`0x001cc520`). It's referenced from 20+ sites across the binary, including
the already-named `Application_Purge` (confirming it's torn down at
application shutdown, a core long-lived singleton, not peripheral).

## The real runtime consumer chain, found (with a correction along the way)

Systematically checked the remaining `g_CharacterAnimBank` reference sites
(rather than sampling, per the note that sampling hit a false lead) and
found the actual "once loaded, how does animation data get read back at
runtime" chain:

- **`CharacterAnimSet_FinalizeLoadedSlots`** (was `FUN_00060e30`) — called
  with `this=g_CharacterAnimBank` (confirmed via raw disassembly: `MOV
  ECX,0x1cc520; CALL 0x00060e30`). Polls
  **`CharacterAnimSet_IsSlotLoadReady`** across all 446 (`0x1be`) pending-
  load flags; once every one is ready, calls
  **`CharacterAnimSet_RegisterSlotAnimationIds`** for all 446 slots.
- **`CharacterAnimSet_RegisterSlotAnimationIds`** — for a finished slot,
  matches its internal sub-entries against a 1412-entry (`0x584`) ID table
  (**`g_AnimationIdTable`**) and records each match's `(slot, sub-index)`
  location into **`g_AnimationIdToSlotMap`** — building the runtime "given
  an animation ID, where is its data" index.
- **`AnimationLookup_GetSlotById`** / **`FindSlotIdFallback`** — read that
  index directly or via a linear/wraparound scan fallback.
- **`AnimationLookup_ResolveCurrentTrackId`** / **`AnimCurve_FindActiveSegmentIndex`**
  / **`SetActiveSegmentIndex`** — resolve which track/segment is currently
  playing for a given context.
- **`AnimCurve_SampleKeyframeValue`** — a genuine keyframe-curve sampler:
  walks a byte-encoded keyframe stream (a mode nibble selects interpolated
  vs. direct playback, entries store a shifted timestamp) and returns the
  value at a given time.
- **`Rider_SampleBoneRotationQuaternion`** — resolves the current track/slot,
  samples 3 curve values via `AnimCurve_SampleKeyframeValue`, scales the
  resulting vec4 by a rider-owned intensity field
  (`this+0x24`->`+0x494`->`+0x78`) — **the real per-bone rotation query**,
  the actual runtime consumer link between `g_CharacterAnimBank`'s loaded
  data and gameplay-visible rider animation.

**A correction made along the way**: `Rider_SampleBoneRotationQuaternion`
was initially sampled (while hunting `g_CharacterAnimBank`'s consumers) and
mischaracterized as *"an unrelated random-rotation-quaternion generator"* —
its 3-value curve read looked superficially like 3 independent random
numbers. Re-examined once `AnimCurve_SampleKeyframeValue` (one of its 3
callees) was confirmed via its own body to be a genuine keyframe-curve
sampler (not an RNG), which corrected the read: those 3 values are real
sampled animation-curve data, not random. Fixed immediately per this
project's standing correction rule — the function was never actually
renamed with the wrong name (it was still `FUN_00067290` at the time), so
this was a documentation fix, not a rename-then-fix.

## `Rider_SampleBoneRotationQuaternion`'s caller, found — a rail-grab pose snap

Its **sole caller** is the already-named `Rider_CheckRailAttachmentAlignment`
(`RE_NOTES_rider_update_chain.md`'s mode-5-to-6 free-flight-to-rail-attach
work), used at the exact moment a rider grabs onto a rail:

```
RiderAnimation_TriggerByEventCode(0x24b or 0x24c, 0, 5);  // trigger the rail-grab animation
Rider_SampleBoneRotationQuaternion(local_30, 0);          // sample its starting bone-0 pose
FUN_0001fde0(&local_40, local_30);                         // convert to a position offset
*pfVar1 += (local_20 - local_40);  // ... 4 components               // correct rider position by the delta
Rider_SetPhysicsMode(6);                                   // switch to rail-attached physics
RiderEvent_SetState(0x14);
```

This is a classic **animation-driven pose snap**: right after triggering the
rail-grab animation (via the already-documented `RiderAnimation_TriggerByEventCode`
event system), the code samples that animation's *starting* bone orientation
and uses it to smoothly correct the rider's world-space position so the
character visually lands in the animation's expected starting pose when
attaching to the rail — rather than snapping instantly or looking
disconnected from the rail geometry. This connects three previously-separate
systems in one concrete call sequence: the `RiderAnimation` event-trigger
system, this session's `CharacterAnimSet`/animation-curve-sampling system,
and the rail-physics mode transition (`Rider_SetPhysicsMode`).

Not confirmed to be `cMeshAnim`-specific — the connection is to rail-attach
*physics* (position correction), not necessarily the skeletal *mesh renderer*
directly, though the two are presumably closely related (the sampled pose
should match what the mesh will actually render). Left as-is rather than
force-connected to `cMeshAnim` without more direct evidence.

## THE `.afl` FORMAT — DECODED AND FULLY VERIFIED (2026-07-22, Tier-2 push)

Per an explicit user directive to finish the Tier-2 port-viability items,
decoded the `.afl` internal format end to end using the established
methodology: extracted all **446** `.afl` files from `data\char\anm.big`
(extract_big.py — the count exactly matches the 446 pending-load flags
`CharacterAnimSet_FinalizeLoadedSlots` polls, a nice cross-confirmation),
then cross-referenced their bytes against the runtime readers
(`CharacterAnimSet_LoadSlotSync`'s sub-pointer math,
`CharacterAnimSet_RegisterSlotAnimationIds`'s entry walk, and a raw
disassembly of the curve-sampling path to recover register-passed context
the decompiler hides). **Verification: every one of the 119,498 curve
streams across all 446 files parses cleanly with this spec — 0 failures.**

```
+0x00  u16  magic/version — 0x114C in every shipped file
+0x02  u16  entryCount
+0x04  u32  offset -> section B (curve-offset table, u32 each)
+0x08  u32  offset -> section C (curve-stream blob)
+0x0C  section A: entryCount × 36-byte entries:
         +0x00 u32  animation ID (matched against g_AnimationIdTable;
                    0 = companion entry)
         +0x04 u32  first curve index into section B
         +0x09 u8   companion count / redirect flag (see below)
         +0x0A u16  duration in frames (e.g. 301, 352)
         (remaining bytes mostly 0xFF fill, not individually identified)
```

Each ID'd entry is followed by `+0x09`-many `id=0` **companion entries** at
ascending curve-start indices — channel groups sharing one duration. The
rotation-sample path (`AnimEntry_SampleRotationCurves`, was `FUN_00060c80`,
named this pass) honors this: a nonzero `+0x09` on the resolved entry
redirects it to the *next* entry before reading 3 consecutive curves
(X/Y/Z) via the section-B offset table into section C.

**The curve codec** (`AnimCurve_SampleKeyframeValue` +
**`AnimCurve_EvaluateSegment`**, was `FUN_0005f980`, named this pass —
closing that open item): each curve is a u16 header (low nibble = mode,
high 12 bits = count/duration); mode 5 = multi-segment container; segment
modes 0–3 = constant/linear/quadratic/cubic polynomial pieces whose
coefficients are **3-byte floats** (the float32's low mantissa byte is
dropped and reconstructed as `0x80` — visible in the decompile as the
`CONCAT11(x,0x80)` idiom — a 25% size cut per value); modes 6/7 = u8/u16
quantized keyframe tables (base + scale 3-byte floats, then 1–2 bytes per
frame, lerped); mode 4 = raw per-frame keyframes — **defined in code but
never used in any shipped file** (0 of 119,498 curves; the same
dead-code-path pattern as DXT3 in `.xsh`). Shipped-data segment census:
constant 97k, cubic 62k, u8-quantized 63k, quadratic 44k, linear 43k,
u16-quantized 1.6k.

Deliverable: **`scripts\classify_afl.py`** (parses headers/entries/curves,
`--verify` re-runs the full-corpus check). 2 renames.

## Still open (none port-critical)

- The exact `CON`/`RCON` animation-category meanings — not decoded.
- The 36-byte entry's remaining fields (`+0x08`, `+0x18`, `+0x1C`) and the
  exact channel semantics of companion entries (which group = which bones).
- Whether `Rider_SampleBoneRotationQuaternion` (or a sibling) is also
  called from `cMeshAnim`'s own rendering code — unconfirmed either way.

18 function renames + 3 data renames total across this file's threads.
