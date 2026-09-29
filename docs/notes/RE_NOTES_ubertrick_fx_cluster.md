# RE notes: Ubertrick / trick-trail particle-FX cluster (0x000df840–0x001046a0)

Working notes from manually decompiling functions in the cluster first surfaced by
debug-tag mining (`exUT_*`, `exTW_*`, `frL_*`, plus `"snowdrea"`/`"mesablan"` — truncated
`Snowdream`/`Mesa Blanca` track names). Corrects and sharpens the earlier "named queue
tag" hypothesis with function-level evidence. Cross-reference: `ssx_auto_rename.py`,
`name_candidates.json` in this folder.

## Correction to the earlier theory

The initial read (previous session) guessed each `exUT_SIGBRO`-style string was a
deliberate debug tag naming its own memory pool, mirroring the confirmed
`FUN_0012a250(size, flags, "Tag")` allocator elsewhere in the binary. Having now read
several full function bodies, that's not quite it:

- These strings are **never passed as a call argument anywhere** in the file (checked
  exhaustively) — they only ever appear as `"tag" + expr`.
- Ghidra prints `"tag" + expr` when it needs to display a computed address for which it
  has no proper global symbol, and the *nearest preceding* symbol happens to be that
  string literal. The string's content is very likely **coincidental linker placement**
  (debug/level-name strings and unrelated engine bookkeeping fields end up in the same
  data segment), not evidence the field is "named" after that string.
- So the correct reading of e.g. `*(int *)("exUT_SIGBRO" + param_1 + 4)` is: some fixed
  global address (which Ghidra can only describe relative to the string) plus a
  runtime-variable `param_1`, plus a small field offset. The interesting part isn't the
  string — it's what the surrounding code *does* with that address.

## What's actually confirmed (read line-by-line)

**`FUN_00100ea0(int param_1, int *param_2, float param_3)`** — [default.xbe.c:160548](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:160548)
Spawns one billboard quad into a particle/effect system:
- Advances a write-cursor (`+4` field) by 4 per call, clamped against a capacity field
  (`+0x157f8`) and a high-water-mark field (`+0x157e8`); bails out (`local_3c = NULL`) if
  the ring is full.
- On success, computes a vertex-array slot (`base + cursor*0x20`) and writes 4 corners
  of a quad, each an 8-float (32-byte) record: position (x, y, z from `param_2[0..3]`),
  `w = 1.0`, with the last float of each corner group set to `-NAN` (likely a "not yet
  finalized" or "invalid normal" sentinel the renderer checks for).
- Separately submits a 6-DWORD command into what's clearly an **NV2A GPU push-buffer**
  (`*(uint**)(iVar2+0x808)`), copying a template header from `DAT_001ead20` and patching
  bits (`& 0x7ffffff | 0x18000000`) — textbook Xbox D3D8 push-buffer command patching for
  a draw-primitive-with-vertex-offset call.
- **Read as:** "spawn one billboard sprite for a signature-trick visual effect and queue
  its draw command."

**`FUN_00101070(int param_1, ushort *param_2)`** — [default.xbe.c:160637](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:160637)
Same base object, a *different* ring buffer (`+8` field, stride `0x120` = 288 bytes vs.
the first function's stride 0x20) — richer per-effect record: color channels scaled by
`0.007843138` (= 1/127.5, the standard byte→float color normalize), a size/scale field
scaled by `256.0`, a frame/palette index clamped to `0x7ff`→`0x800` (2048), and a
`param_2+0x14` field that also updates a separate global high-water mark
(`DAT_001eac8c`). Reads like "spawn a colored/textured decal or bigger FX record," a
sibling of the plain billboard spawn above.

**`FUN_000fe100/…fe260/…fe2d0/…fe370`** — [default.xbe.c:158608](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:158608) onward
Manage **3 D3D8 vertex buffers per group** (`ARMS`, `TAIL`, `ATOMIC2` — likely
board/rider mesh part names), through the game's own resource wrapper:
`D3D8::D3DResource_Register`, `D3D8::D3DResource_BlockUntilNotBusy`,
`D3D8::D3DVertexBuffer_Lock`. Confirms the game has a custom resource-tracking layer on
top of raw D3D8 (matches the port notes' expectation). The "3 buffers" pattern is
consistent with triple-buffering for streamed/dynamic geometry (likely swappable board
graphics or rider costume parts).

## What's still open

The exact meaning of `param_1` (used as a `__thiscall`-style leading argument
throughout) isn't fully resolved. Two candidate readings, both partially supported:

1. **Per-instance object pointer.** At [default.xbe.c:139150](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:139150), `FUN_00100ea0` is called as
   `FUN_00100ea0(puVar1 + 0x24, *(undefined4*)(puVar1 + 0x20))`, where `puVar1` is a
   sizeable per-instance object (fields observed up to `+0x35c`) being built by a loop
   that walks up to `0x24` (36) path segments and stores the returned effect-handle back
   into itself — this is very likely a **rail-grind or trick spline-trail object**,
   tying this whole cluster concretely to grind-trail spark/particle rendering.
2. **Fixed global-region slot offset.** Elsewhere (`FUN_000fe100(0x2000, 0, 4)` at
   [default.xbe.c:137690](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:137690)), the same argument slot receives a small constant, which reads
   more like a player-slot stride into shared global tables than a heap pointer.

These aren't necessarily contradictory (a per-instance pointer whose value happens to be
small/offset-like at some call sites). I traced one level further: `puVar1` (the object
passed into `FX_SpawnTrailQuad` at line 139150) is computed inside
`FUN_000e08c0(float param_1)` at [default.xbe.c:138796](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:138796) as
`(undefined1*)(iVar19*0x420 + 0x4040 + (int)param_1)` — note `param_1` is declared
`float` but immediately cast to `(int)` and used as a base address. **That's a Ghidra
mistyping**, not real behavior: this parameter is a pointer/base value threaded in via
`__fastcall`'s ECX slot, which Ghidra's type recovery guessed wrong (likely because the
function also does unrelated floating-point work and the heuristic picked the wrong
type for the first register). This confirms the whole cluster shares **one root base
value passed down through several layers of calls** rather than each function
independently owning a small object — consistent with a single shared "world/rider
render context" pointer, reinforcing reading #2 (huge fixed offsets into one shared
context) more than #1. Fixing `FUN_000e08c0`'s first parameter type from `float` to a
pointer type in Ghidra would likely make its own decompilation much more readable, and
is a good next step before going further up the call chain to find where that root
pointer ultimately originates (its caller, and *its* caller, etc.).

**Dead end in the text export — RESOLVED (live Ghidra, later session).** `FUN_000e08c0`
had zero textual references in `default.xbe.c` — no direct call, no `&FUN_000e08c0` in a
table. The original guess was a function-pointer table Ghidra hadn't typed as such;
**that guess was wrong** (same lesson learned elsewhere this session — see
`RE_NOTES_level_script_system.md`'s "GhidraMCP patched" section for the general method).
The real story: `xrefs_to` on the live database actually shows one caller,
`0x000e25ba` — but that address wasn't a recognized function either (same
mis-placed/undiscovered-boundary problem as the `Script_PlayByName` fragment clusters).
Traced it by scanning backward for `RET`/NOP-padding boundaries (`/read_bytes` in a loop,
looking for `0xC3` and NOP runs) through **three** successive undefined regions before
landing on real function boundaries:

- **`0x000e2450` → `FX_TrailInstance_UpdateTransform`** (was the container of the
  original `0x000e25ba` call site). Reads the active camera/context struct
  (`DAT_001e3c7c` indirection — the same base used by `Camera_AddShake`/
  `Camera_WarpToTarget` in `RE_NOTES_level_script_system.md`), copies two 16-byte
  transform chunks into a per-instance ring-buffer slot via vtable calls, computes 5
  camera-relative scaled position values from a 4×4-ish transform matrix, sets two
  buffer-size constants (`0x400` = 1024), then calls `FUN_000f6f50`, **`FX_TrailSegment_Process`
  (was `FUN_000e08c0`)**, and `FUN_000e1670` (still unread) in sequence.
  **Later-session correction/context (see `RE_NOTES_rendering_system.md`)**: this
  same function address is also inherited unmodified as slot 2 of the shared
  `FXNode` base vtable (`0x19d1b8`, installed by `LensFX_ConstructBase`) — i.e.
  it's a generic `FXNode`-family virtual method, not exclusively a trail-instance
  method. The name/analysis above is still accurate for its role in this trail
  system's own call chain; just note it's a shared base method reused elsewhere,
  the same "shared Node-family vtable slot" idiom documented throughout this
  project.
- **`0x000e13f0` → `FX_TrailManager_Tick`**, found one level up: iterates a cluster of
  trail instances (nested loops with strides matching large per-instance structs),
  conditionally invoking a vtable callback (offset `+0x24`) on flagged sub-entries, then
  a second vtable call (`+0x2c`) — reads as the actual per-frame driver that walks every
  active trail/ubertrick-spark instance and ticks it.

**Also fixed `FX_TrailSegment_Process`'s (was `FUN_000e08c0`) first parameter type** via
`set_function_prototype` (`float` → `void *`, exactly the mistyping this file predicted
earlier) — the call site (`FX_TrailInstance_UpdateTransform`) now shows the argument
explicitly instead of an empty `()`. This confirms the whole cluster's shared "one root
base pointer threaded through several call layers" reading from earlier in this file:
`FX_TrailManager_Tick` → `FX_TrailInstance_UpdateTransform` → `FX_TrailSegment_Process` →
(down further, not re-traced this pass) `FX_SpawnTrailQuad`/`FX_SpawnTrailDecal`.

**Method note, same as the `Script_PlayByName` clusters:** when `xrefs_to` is empty and
the natural instinct is "must be a function-pointer table," check first whether the
"caller" address itself is simply an unrecognized function boundary — scan backward for
`RET`/NOP-padding with `/read_bytes` before reaching for a memory search. Worked for a
3-deep chain of undefined regions here, same as it did for the `GameMode_*` cluster.

## The full call chain, now traced to actual quad-spawning

`FX_TrailInstance_UpdateTransform`'s third call — `FUN_000e1670`, previously unread — is
**`FX_TrailSegment_SpawnGeometry`**: a huge (250+ line), heavily float/vector-math
function that Ghidra's own decompiler flags with `/* WARNING: Type propagation algorithm
not settling */` — genuinely at the edge of automated analysis, so this is read at
**structural confidence only**, not byte-verified. What's clear regardless: it computes
billboard quad corners with a rotation (`fsin`/`fcos` on a computed angle), reads
per-instance color/position data, and calls **`FX_SpawnTrailQuad`** directly (the
already-known billboard spawn from the very first pass on this cluster), then calls a
newly-found sibling **`FX_SpawnColoredQuad`** (was `FUN_00101e90`, right next to
`FX_SpawnTrailDecal` in the address space) three times with different rect/color
arguments — reads as spawning 3 additional colored quad primitives per trail segment
(likely the "glow"/gradient layers around the core spark billboard).

**Full call chain, this session's live-Ghidra work, top to bottom:**
```
FX_TrailManager_Tick (0x000e13f0)          -- per-frame driver, walks active trail instances
  -> FX_TrailInstance_UpdateTransform (0x000e2450)  -- per-instance camera-relative transform setup
       -> FX_TrailManager_ScanTrackSegments (0x000f6f50)  -- LOD/culling pass, see below
       -> FX_TrailSegment_Process (0x000e08c0)       -- was the original text-export dead end
       -> FX_TrailSegment_SpawnGeometry (0x000e1670) -- structural confidence only
            -> FX_SpawnTrailQuad (0x00100ea0)        -- known from the first pass
            -> FX_SpawnColoredQuad (0x00101e90) x3   -- new this pass
```
This is very likely the complete rail-grind/ubertrick spark-trail rendering pipeline, top
to bottom, for a port to reimplement.

## A real LOD/performance system: only 10 trail slots active at once

**`FX_TrailManager_ScanTrackSegments`** (was `FUN_000f6f50`) iterates up to **162** track
segments (`TrackSegment_GetByIndex`, was `FUN_00141770` — trivial `param_1[index]` array
accessor, but the 162-entry bound and per-segment structure strongly suggest a fixed
per-track checkpoint/segment table), checks a per-segment visibility bitmask, and for
segments with objects nearby, computes a distance (reusing `FUN_00025ab0`, the same
quaternion-multiply helper seen elsewhere in the codebase) and calls
**`FX_TrailSlot_ClaimByProximity`** (was `FUN_000e0770`) with that distance.

`FX_TrailSlot_ClaimByProximity` manages a **fixed 10-slot pool with distance-based
eviction**: if fewer than 10 slots are in use, the new object just takes the next free
one. Once the pool is full, it finds the *currently occupied* slot with the **largest**
stored distance (i.e. the least-relevant, farthest-away active trail) and — only if the
*new* object is actually closer than that — evicts it and takes the slot. Otherwise the
new object is silently dropped (no slot, no trail this frame).

**This is a genuine, load-bearing performance system**, not incidental: SSX only ever
renders spark trails for the 10 closest/most-relevant rail-grind objects to the player at
once, chosen every frame by scanning nearby track segments and running a
proximity-priority eviction over a fixed slot pool. Important for a port to reimplement
faithfully (or deliberately relax, if targeting more capable hardware) rather than just
"spawn a trail per grindable object" — the original never did that.

## Practical takeaway for Ghidra work

Don't try to define a struct at the string literals' addresses — they're not real
headers. Instead:
- Rename `FUN_00100ea0` → something like `FX_SpawnTrailQuad` (billboard spawn, high
  confidence).
- Rename `FUN_00101070` → `FX_SpawnTrailDecal` (richer colored record, high confidence).
- Rename the `FUN_000fe1xx/2xx/3xx` group → `BoardMesh_*` (vertex-buffer lock/register/
  release for the 3-buffer part groups).
- Leave `param_1`'s type as a plain `int` (not a struct pointer) until the allocation
  site is traced — mistyping it now would make the decompiler's future output worse, not
  better.

## The shared `FXParticle_*` utility library (found 2026-07-20)

Found while investigating what `RE_NOTES_rendering_system.md` had filed as a
"reflection-math cluster" (`FUN_000e6420` and 6 siblings, first noticed being
called from `SceneRenderer_RenderFrame`'s reflection block, right before the
already-named `FX_SpawnTrailDecal`). Turned out to have 2 other, unrelated
callers -- meaning it was never reflection-specific at all, just a generic,
emitter-agnostic particle-setup library reused across at least 3 systems:

- **`Rider_UpdateSnowSprayFX`** (`0x00040ed0`) -- reads the rider's position/
  velocity (`rider+0x180`/`+0x4870`/`+0x4880`/`+0x4890`, matching the
  already-documented vec4 fields from `RE_NOTES_terrain_collision.md`), a
  surface/physics-mode-like field at `rider+0x454` gated against terrain
  surface-type IDs (9/0xd/0x12/0x13 excluded -- almost certainly rail/ice/
  no-spray surfaces), and drives a 20-slot RNG-selected particle ring buffer.
  The rider's snow-spray/powder-trail emitter. **Correction (2026-07-21,
  found while stirring for a fresh thread): the "zero static callers,
  likely vtable-dispatched" claim below was wrong and has been fixed.** A
  live `xrefs_to` check (not re-verified since the original claim was made)
  found a perfectly ordinary direct-call chain: `Rider_UpdatePhysicsState`
  -> `Rider_UpdateUberTrickGlowFX` (one of its already-documented 15 direct
  sub-calls) -> the newly-named **`Rider_UpdateAmbientFXBatch`** (was
  `FUN_00044b40`, called unconditionally every frame, gated only on
  `GameState_ShouldSkipGameplayTick` -- entirely independent of
  `Rider_UpdateUberTrickGlowFX`'s own Uber Trick glow rising/falling-edge
  logic, just placed in the same caller) -> `Rider_UpdateSnowSprayFX`. No
  vtable dispatch involved at all. `Rider_UpdateAmbientFXBatch` batches 5
  rider ambient-FX sub-updates in sequence (`FUN_0003fb70`/`FUN_000443e0`/
  `Rider_UpdateSnowSprayFX`/`FUN_0003f750`/`FUN_00040060`, plus a
  conditional `FUN_00041da0` gated on level/quality-tier checks) -- at
  least 2 of the unconditional 5 share the same quality-tier gate and one
  (`FUN_0003f750`) reads the exact same rider velocity fields, confirming
  this whole batch is a family of velocity/quality-tier-driven ambient
  particle effects (snow spray plus siblings, likely dust/wind-trail
  effects). The 4 sibling functions weren't individually named this pass --
  a well-scoped next thread if revisited. 1 rename this pass.

  **Immediate follow-up ("keep going"): named all 4 siblings.** All share
  `Rider_UpdateSnowSprayFX`'s general shape (gated on the level's quality-
  tier field, RNG-selected particle ring buffers keyed off rider surface/
  velocity state) — structural confidence throughout, since surface-type
  numeric IDs aren't decoded anywhere in this project so exact per-surface
  visual distinctions aren't claimed:
  - **`Rider_UpdateSurfaceParticleFX`** (was `FUN_0003fb70`) — gated on
    `rider+0x454==2` (a specific surface type) plus a distance-moved
    threshold, or alternately `rider+700==3||4` (a boost/special-state
    code). 20-slot ring buffer, same idiom as the snow-spray emitter but
    keyed to one specific surface rather than an excluded set.
  - **`Rider_UpdateSurfaceFrictionCueFX`** (was `FUN_0003f750`) —
    speed + surface-gated timer that triggers `FUN_0003f520` with
    `rider+0x58e0+0x4b0` (offset from the already-confirmed
    back-pointer-to-owning-Rider field seen in `Rider_ApplyMotionUpdate`,
    likely a distinct audio/FX-cue sub-object) — reads as a friction cue
    (sound and/or spark effect), not fully identified. Manages its own
    smaller 8-slot decay-timer ring buffer.
  - **`Rider_UpdateSurfaceSprayFX`** (was `FUN_00040060`) — projects rider
    velocity onto the plane perpendicular to the contact-normal vector
    (`rider+0x2d0..+0x2dc`, matching the already-documented terrain-contact
    normal shape) — the "strip the along-normal component, keep the
    tangential component" idiom, producing a spray direction that skims
    along the surface.
  - **`Rider_UpdateAmbientParticleFX`** (was `FUN_000443e0`) — structural
    confidence only: the largest/most complex sibling (a much bigger
    0x4b-slot ring buffer), confirmed same-family membership but not traced
    field by field — named generically rather than overclaiming a specific
    visual effect.

  4 more renames (1 + 4 = 5 total for this correction).

  **Pushed through the 6th call too ("keep going hard"): `Rider_UpdateGrindTrailFX`**
  (was `FUN_00041da0`, full 640-line body read). Distinct in *kind* from the
  4 burst-particle siblings above — this maintains a **persistent 100-slot
  fading position-history ring buffer** (a continuous trail/ribbon data
  source, not a one-shot spawner). Gated on a trick/animation-state code
  (`rider+700`) and the same surface-type field the siblings use
  (`rider+0x454`), here restricted to exactly `{2,5,6}`. **Notable
  cross-reference, not conflated with the physics-mode field**: those
  values happen to match the still-unresolved rail/wall-attachment
  physics-mode hypothesis (`Rider_PhysicsMode5_NoTerrain`/`PhysicsMode6_NoTerrain`,
  `Rider_CheckRailAttachmentAlignment`) — plausible, not proven, that
  surface-type 5/6 mean "on a rail"/"on a wall", which would make this a
  genuine rail-grind trail effect. On surface 2, seeds from a directly-
  embedded transform block on the rider; on surface 5/6, instead chases a
  pointer at `rider+0x42e8` (a plausible "currently-attached rail/object"
  pointer, not cross-confirmed against any other documented field) to
  source the same data from a different object. Sets an active/inactive
  flag byte, almost certainly consumed by a separate (not located this
  pass) trail-mesh rendering step. 1 more rename (5 + 1 = 6 total for this
  correction thread).

  **Refined the `rider+0x42e8` hypothesis with better evidence (immediate
  follow-up).** A byte search for other references to the same field
  offset found the identical null-check-then-deref-+4 idiom reused in the
  already-named `Rider_ResetPhysicsState` and in a new function
  (`FUN_0003c7e0`, called conditionally from `Rider_ApplyMotionUpdate`) —
  and critically, `FUN_0003c7e0` uses **both** `rider+0x42e8` and
  `rider+0x42ec` together, back to back, the same way. That's a confirmed
  *pair* of fields, not a single pointer — matching
  `Rider_UpdateUberTrickGlowFX`'s own already-documented "two limb/board
  attachment-point positions" far better than the earlier "attached rail"
  guess. Named **`Rider_BuildGlowTrailBasisFromAttachment`** (was
  `FUN_00042f40`, called from `Rider_UpdateUberTrickGlowFX`'s tail): reads
  the attachment sub-object via `rider+0x42e8` and builds a small local
  reference frame (4 derived position variants) from its transform —
  consistent with constructing a glow-trail's positional basis from a
  single limb/board attachment point. **Note**: Ghidra's registered
  function body for this one is mis-bounded (reports only 22 bytes) but
  `decompile_function` correctly follows the full control flow anyway — a
  known quirk, not a re-created-function case this time. **False lead
  caught and discarded**: the "mirror" branch call, `FUN_00043340`, looked
  like it should be a symmetric twin (same if/else calling shape) but
  turned out to be a completely unrelated GameMode/Team-slot function —
  not forced into this narrative. Updated the earlier "attached rail"
  guess in `Rider_UpdateGrindTrailFX`'s comment to point here rather than
  silently editing it, per this project's disclosure rule. 1 rename (6 + 1
  = 7 total for this correction thread).

  **Found the actual producer of the attachment-point data (immediate
  follow-up, 1000th rename milestone).** Full 380-line read of
  `FUN_0003c7e0` (the function that uses both `rider+0x42e8`/`+0x42ec`
  together). Named **`Rider_UpdateBoardAttachmentTransforms`**: computes
  the rider's board world-space transform at 2 attachment/binding points,
  driven by a genuine board-flex/bend model — reads a per-bone transform
  from an embedded skeleton-bone array, builds rotation matrices via 3
  axis-angle-style constructors combined with the already-named
  `Matrix_Multiply4x4`/`Matrix_TransformVector4`, and branches on the
  already-documented surface-type field plus `RiderEvent_GetSubState`/
  `ComponentSlot_ResolveCategoryFromType` (gated on category `0x10`, one
  below the already-documented rail-attachment category `0x11`) to choose
  between a simplified and a full board-bend transform path. **This is
  the confirmed producer of the data `rider+0x42e8`/`+0x42ec`'s
  dereferenced sub-objects expose** — fully closing the loop:
  `Rider_UpdateBoardAttachmentTransforms` writes the front/back binding
  world transforms, and `Rider_ResetPhysicsState`/
  `Rider_BuildGlowTrailBasisFromAttachment`/`Rider_UpdateGrindTrailFX` all
  read them back. Confirms, rather than just plausibly suggests, that this
  is a board bend/attachment-point system, not a rail-object pointer. The
  underlying bone-transform helper cluster
  (`FUN_0010a1b0`/`FUN_0010aad0`/`FUN_0010a200`/`FUN_00030990`/`FUN_0003aff0`)
  wasn't individually named — a well-scoped next thread (this project's
  skeleton/bone-attachment subsystem) if revisited. 1 rename (7 + 1 = 8
  total for this correction thread). **This rename was the project's
  1,000th total rename.**
- **Named 3 of the underlying bone-transform helpers ("cover as much as
  possible" pass).** `Matrix3x3_Multiply` (was `FUN_00030990`, plain
  3x3*3x3 matrix multiply, padded-row input/packed output),
  `Matrix3x3_CopyPackedToPadded` (was `FUN_0003aff0`, the unpacking
  counterpart), and `SkeletonBone_GetTransformedPoint` (was `FUN_001093e0`,
  indexed local-point lookup + `Matrix_TransformVector4`). Left
  `FUN_0010a1b0`/`FUN_0010aad0` (trivial forwarders into a deeper,
  unexplored skeleton/animation subsystem around
  `0x00109xxx`-`0x0010axxx`) unnamed rather than guess —
  `FUN_0010a2b0`/`FUN_001092f0` were peeked at and are substantial
  standalone functions, a genuine "skeleton animation system" next thread
  if ever prioritized. 3 renames (8 + 3 = 11 total for this correction
  thread).
- **`FXParticle_SpawnFromDescriptor`** (`0x000e77e0`) -- takes a generic
  indexed config-table entry (position, texture ids, size/lifetime scalars,
  no Rider involved at all) and drives the exact same utility calls. Proves
  the library is emitter-agnostic, not rider-specific.
- **`SceneRenderer_RenderFrame`**'s reflection block -- calls the utilities
  directly on fields embedded in `GfxContext` itself (`this+0x5658..0x56a8`),
  then calls `FX_SpawnTrailDecal`. A third, render-pipeline-embedded spawn
  path into the same system (plausibly a splash/ripple decal tied to the
  water/ice reflection pass, not verified further).

The utility functions themselves (structural confidence -- consistent,
well-understood write patterns, but exact field-level semantics inferred from
shape rather than an owning struct definition):

- **`FXParticle_InitTransformAndTiming`** (`0x000e6420`) -- particle record
  init: rounded-int fields, an ease-curve-scaled ratio pair (Newton-Raphson-
  refined SSE `rcpps` reciprocal -- the standard one-iteration precision fixup
  for the approximate reciprocal instruction), a duration field, default unit
  scale/rotation.
- **`FXParticle_SetSizeRange`** (`0x000e6250`) / **`SetScaledSizeRange`**
  (`0x000e6280`) -- min/delta size-range pairs (`delta=b-a`, `bias=a-delta`),
  the second scaled by the particle's own duration field (`+0x3c`).
- **`FXParticle_SetColorGradientFromBlend`** (`0x000e67b0`) -- a 3-way color
  gradient: base color minus a blended (averaged) pair, plus the two inputs
  individually scaled. The "base minus blended-midpoint, plus scaled
  endpoints" idiom recurs across this whole cluster.
- **`FXParticle_SetScaledColorComponent`** (`0x000e6830`) -- one gradient
  endpoint, ease-refined-reciprocal-scaled.
- **`FXParticle_SetMultiColorGradient`** (`0x000e68a0`) -- the most elaborate:
  4 ease-refined color endpoints plus a final blended-average gradient across
  3 of them, 5 sub-blocks written. Likely a multi-stop lifetime color fade
  (start/mid/end).
- **`FXParticle_SetColorAndVelocityGradient`** (`0x000e6a00`) -- a blended
  base color (same idiom as `SetColorGradientFromBlend`), a raw positional
  copy, a raw 16-byte SIMD vector copy, and a `(target-current)/duration`
  delta -- a rate-of-change vector for animating toward a target over the
  particle's lifetime.

**DONE, same pass**: the 6 siblings called alongside this cluster inside
`Rider_UpdateSnowSprayFX` turned out to be the emission-timing half of the same
system, and cross-referencing their field offsets against the setup functions
above pins the whole mechanism down precisely:

- **`FXParticle_TickEmissionAndAdvance`** (`0x000e6560`) -- the master
  per-period tick. Advances an emission accumulator (`this+8`) by
  `rate*duration` (`this+0x3c`), and while it exceeds a threshold
  (`this+0 * this+0xc`), repeatedly: decrements the accumulator by the
  period, steps a position field (`this+0xa0..+0xac` -- the exact field
  `FXParticle_SetColorGradientFromBlend` writes) by a delta field
  (`this+0xd0..+0xdc` -- the exact field `FXParticle_SetScaledColorComponent`
  writes and `FXParticle_ClearStepDelta` zeroes), and advances an 11-bit
  wraparound phase counter (`this+0x28`, sign-extended via a `0x800007ff`
  mask trick). Spawns/advances one trail segment per whole period of
  accumulated emission time -- this is the actual "emit particles over time,
  not all at once" driver.
- **`FXParticle_SetEmissionAccumulator`** (`0x000e62b0`) / **`AccumulateEmissionTime`**
  (`0x000e62c0`) -- overwrite vs. increment `this+8` by `param_2 * duration`.
  The increment variant is called in a `*param_1`-times loop at the very top
  of `Rider_UpdateSnowSprayFX` with the `this` receiver not reliably
  recoverable from the decompile (thiscall-with-optimized-out-ECX ambiguity)
  -- likely a catch-up mechanism for multiple elapsed periods, not fully
  resolved.
- **`FXParticle_RandomizeRotationPhase`** (`0x000e6230`) -- seeds the same
  11-bit phase field with `RNG_NextGlobalUInt32() & 0x7ff`.
- **`FXParticle_ClearStepDelta`** (`0x000e6530`) -- zeroes `this+0xd0..+0xdc`.
- **`FXParticle_SetColorGradientTwoInput`** (`0x000e6740`) -- a 2-input
  variant of `SetColorGradientFromBlend` (scaled-A-minus-B / plain-delta pair
  instead of a 3-way blend), used as `Rider_UpdateSnowSprayFX`'s alternate
  path specifically for terrain-surface-type 3.

6 more renames (15 total across the whole `FXParticle_*` cluster).
