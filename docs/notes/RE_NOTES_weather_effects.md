# RE notes: `SnowFallMan`/`FogMan` — the weather-effect systems

Picked as a fresh, previously-unexplored direction (2026-07-20). `InGameState_LoadLevel`
allocates a long list of tagged subsystem objects (`RE_NOTES_application_boot.md`'s
"followed the AIWorld object" section lists them: `"VideoStreamMan"`, `"PostAI"`/
`"PREAI"`, `"PowerFX Particles"`, `"SnowFallMan"`, `"OverlayNode"`, `"ModelsNode"`,
`"SkyNode"`, `"TerrainNode"`, `"FogMan"`, `"LessonMan"`, `"DebugMenu"`) — every one
of these except `AIWorld`/`PREAI`/`PostAI` had never been individually opened.
`SnowFallMan` (`0x4d0`/1232 bytes) turned out to be a genuinely substantial,
self-contained falling-snow particle-effect system.

## Architecture

`SnowFallMan_Construct` (was `FUN_000ded50`) calls the same `NodeRegistry_Insert`
wrapper (`FUN_000aa940`) that `PREAI`/`PostAI` use — passed literal type **`2`**.
This closes one small piece of this session's earlier `{2, 5, 7, 8}` type-ID-array
investigation (`RE_NOTES_rider_update_chain.md`): **type 2 is `SnowFallMan`.**
Its own `NodeBase` `Update` slot (vtable slot 7, offset `0x1c`) resolves to the
shared **`Node_NoOpStub2`** stub — so when `GameState_ResetTransientTriggerNodes`
calls it, nothing happens. The real logic lives in two *other* vtable slots
entirely:

- **`SnowFallMan_UpdateScrollPosition`** (vtable slot 1, was `FUN_000de990`) —
  advances a camera-relative scroll position by velocity fields scaled by frame
  delta time, then calls **`SnowFallMan_WrapScrollPosition`** (was `FUN_000de8d0`)
  to keep the coordinate wrapped within one cell-width — the standard technique
  for an infinite-scrolling tiled effect volume.
- **`SnowFallMan_RenderSnowfall`** (vtable slot 2, was `FUN_000dee40`) — gated on
  the current level's own `+0x78` flag ("snow enabled for this track"). Reads the
  current rider's position (the same 4-rider-array/`+0x290`-index/`0x80`-stride
  pattern used throughout this whole project), recomputes the scroll-to-rider
  offset, calls **`SnowFallMan_ResetEmitterTimers`** (was `FUN_000de710`) if the
  track's height reference changed, sets up 4 `GfxContext` render-state calls
  (alpha-test-shaped, ref ≈0.01, texture-stage switches) bracketing a call to
  **`SnowFallMan_DrawSnowVolume`** (was `FUN_000dea40`) — the real particle-quad
  draw call, using a wrapped 3D grid position per emitter (`0x80000001` masking +
  wraparound arithmetic — the classic "infinite tiling volume" technique).

`SnowFallMan_InitEmitterSlots` (was `FUN_000de5b0`, called at the end of
construction) initializes **8 fixed particle-emitter sub-structures** (`0x90`
bytes each) with randomized per-emitter offsets (`RNG_NextGlobalUInt32 & 0x7ff`),
velocity fields, and default size/alpha/color constants — `SnowFallMan` manages
exactly 8 snow emitters.

`SnowFallMan_ScalarDeletingDestructor` (vtable slot 0, was `FUN_000dedf0`) is the
standard `NodeBase`-unlink-then-free destructor pair, nothing unusual.

## Honest status — real per-frame caller not found

Checked `xrefs_to` for both `SnowFallMan_UpdateScrollPosition` and
`SnowFallMan_RenderSnowfall` directly: only their own vtable-slot data
references, no static code callers. Checked the obvious candidates
(`InGameState_TickFrame`, `InGameState_LoadingDispatch`, `SceneRenderer_RenderFrame`,
`SceneRenderer_RenderAllPasses`) for a `+0x80` reference (the field
`InGameState_LoadLevel` stores the `SnowFallMan` pointer at) — none found. Not
chased further with a blind byte sweep (this project's established lesson: a
generic-offset search without another constraint is too noisy to be worth it).
**Confirmed real, substantial, working logic — exact per-frame invocation site
not pinned down** — the same honest state several other systems in this project
have been left in (`Rider_UpdateSnowSprayFX`, `AIWorld`'s remaining slots, etc.).
Plausible candidate: a dedicated "render weather/environment effects" pass inside
`SceneRenderer_RenderAllPasses` not yet read in full, or a level-script opcode
(`Script_DispatchOpcode`'s table) that ticks it — not checked this pass.

**Re-verified, not stale (2026-07-21, "cover as much as possible" pass).** Prompted
by discovering the analogous `Rider_UpdateSnowSprayFX` "zero callers" claim was
actually *wrong* (a stale claim never re-checked — see `RE_NOTES_ubertrick_fx_cluster.md`),
re-ran the same `xrefs_to` checks here. Unlike that case, this one holds up: both
`SnowFallMan_UpdateScrollPosition` and `SnowFallMan_RenderSnowfall` still show only
their own vtable-slot data references, no static callers. Also fully read
`SceneRenderer_RenderAllPasses` (now named at "high confidence" rather than
"not yet read in full") — no `SnowFallMan` reference anywhere in its body — and
grepped `Script_DispatchOpcode`'s known opcode table for any `SnowFallMan` tie-in —
none. Both of this note's own suggested next steps are now dead ends, not just
unchecked possibilities. Cross-referencing `RE_NOTES_node_base_class.md`'s
`GameState_ResetTransientTriggerNodes` bucket-walk (`DAT_0019b994 = {2,5,7,8}`,
confirmed type 2 = `SnowFallMan`) doesn't resolve it either: that function calls
vtable+0x1c (the `Update` slot, which resolves to the no-op `Node_NoOpStub2` for
`SnowFallMan`), not the 2 slots holding the real scroll/render logic — and its own
name ("reset transient trigger nodes") reads as an event-driven reset, not a
per-frame tick, which wouldn't explain continuously-scrolling snow anyway. This
mystery remains genuinely open and dynamic-analysis-only, consistent with (not
contradicting) the file's original conclusion.

8 renames.

## `FogMan`/`FogVolume` — volumetric cloud rendering (2026-07-20, later session)

The last untouched tag from the original `InGameState_LoadLevel` allocation
list — completes the entire sweep this file started. `"FogMan"` (`0x110`
bytes, `NodeRegistry` type 1) turned out to be a genuine **volumetric cloud/
fog-volume rendering system**, confirmed via the `"cloud"` tagged allocator.

`FogMan_Construct` calls `FogMan_ConstructVolumesFromLevelData`, which reads
a fog-volume count from `DAT_001faf90+0x38` (a global also referenced by a
previously-"stuck" `GfxContext` vtable slot — see the correction in
`RE_NOTES_rendering_system.md`, now resolved as a shared "level geometry
data" singleton with multiple sub-tables) and, for each volume, allocates a
**`"FogVolume"`**-tagged instance and attaches it to a resolved level
sub-object.

Each `FogVolume` instance:

- **`FogVolume_BuildCloudPuffs`** (vtable slot 1) — sums a per-sub-item count
  across a segment/data array to size an allocation, then allocates a
  **`"cloud"`**-tagged buffer and calls **`FogVolume_BuildCloudNodeArray`**
  per item to populate it — building the visual cloud-puff/billboard
  representation for one fog volume.
  - **`FogVolume_BuildCloudNodeArray`** — allocates a **`"cloudnodes"`**-
    tagged buffer, seeds initial nodes from input data via
    **`FogVolume_BlendCloudNodes`**, then **procedurally generates
    additional puffs** beyond the initial count by repeatedly picking 2
    random existing nodes and blending them at a random ratio — a classic
    "synthesize more puffs by interpolating existing ones" technique.
  - **`FogVolume_ResetCloudNode`**/**`FogVolume_BlendCloudNodes`** — trivial
    reset and the core position/color interpolation primitive.
- **`FogVolume_DrawCloudPuffs`** (vtable slot 3) — gated on NOT being in
  split-screen and a quality flag — calls **`FogVolume_TransformAndSortCloudNode`**
  per node (transforms through the view/projection matrix via the
  already-named `Matrix_TransformVector4`, computes a depth-based sort/fade
  value) to prepare and draw each cloud puff. Single-player/high-quality
  only.

`FogMan_RenderFogVolumes` (vtable slot 2, the top-level per-frame dispatcher)
sets up fog-blend-shaped `GfxContext` render state, then iterates track
segments via the already-named `TrackSegment_GetByIndex` — the same
segment-iteration technique `TerrainNode`/`ModelsNode` use.

Same honest "no static per-frame caller found" status as every other
instance of this architecture this session.

7 renames (`FogMan_Construct`/`ScalarDeletingDestructor`/
`ConstructVolumesFromLevelData`/`RenderFogVolumes`,
`FogVolume_ScalarDeletingDestructor`/`BuildCloudPuffs`/`DrawCloudPuffs`) + 1
more resolving the connected `GfxContext` stuck item
(`GfxContext_ExtractQuadVertexAttributes`).

**This closes the entire "map every `InGameState_LoadLevel` tagged object"
sweep this session started with `SnowFallMan`** — every tag from the
original allocation list (`SnowFallMan`, `LessonMan`, `PowerFX Particles`,
`TerrainNode`, `VideoStreamMan`, `DebugMenu`, `SkyNode`, `ModelsNode`,
`OverlayNode` [= already-known `OverlayManager`], `FogMan`) has now been
individually explored.
