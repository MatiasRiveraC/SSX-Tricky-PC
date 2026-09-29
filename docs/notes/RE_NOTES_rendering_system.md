# Rendering system — RE Notes

## Starting point: the low-level D3D8 layer was already labeled

Before this thread started, ~61 functions matching the Xbox D3D8 API
(`D3DDevice_SetRenderState_*`, `D3DDevice_Present`, `D3DDevice_Clear`,
`D3DDevice_DrawVertices`, `D3DDevice_DrawIndexedVertices`,
`D3DDevice_SetVertexShader`, `D3DDevice_CreateVertexShader`,
`D3DResource_BlockUntilNotBusy`, etc.) were already named — almost certainly
auto-identified by Ghidra's function-ID matching against known Xbox XDK
library signatures, not manual work. **None of the game's own rendering
code built on top of this layer had been touched.** This thread found that
code by tracing callers of the per-frame `D3DDevice_Present`/`D3DDevice_Clear`
calls.

## What was found this pass

### Engine bring-up chain

- **`Application_InitPlatformAndDevice`** (`0x000b2910`) — the engine's very
  first bring-up function, earlier in the boot sequence than the
  already-documented `Application_InitSubsystems`. Calls
  `XAPILIB::XInitDevices` (gamepad + memory unit), reads a system-timestamp
  and seeds the RNG (`RNG_SeedGlobal`), calls
  `Renderer_InitializeD3DDevice`, then registers the tagged-allocator
  function pair (`FUN_00150d70`/`Heap_Free` — the exact allocator used by
  every `TaggedAlloc("SomeTag", size, ...)` call documented across this
  whole project) as engine-wide callbacks, and initializes the async file
  system.
- **`Renderer_InitializeD3DDevice`** (`0x000b2810`) — the actual device
  creation: builds D3D presentation parameters (640×480, 6 back buffers),
  calls `D3D8::D3D_SetPushBufferSize` + `D3D8::Direct3D_CreateDevice`, then
  clears+presents 3 times (the standard triple-buffer bring-up flush).

### The disc-read-error screen

A complete, self-contained fallback-screen cluster — the standard Xbox
"unreadable disc" dialog every retail game needs:

- **`ErrorScreen_RenderDiscReadError`** (`0x000b1ff0`) — content-confirmed
  via 3 hardcoded, localized wide strings (German/French/English) reading
  *"THERE'S A PROBLEM WITH THE DISC YOU ARE USING. IT MAY BE DIRTY OR
  DAMAGED."* Builds a hardcoded 5-quad icon layout, draws the message, and
  flushes.
- **`ErrorScreen_DrawMultilineMessage`** (`0x000b1df0`) — a standalone
  backslash-delimited (`\`) multiline text renderer with inline `#`/`$`
  scale-toggle control characters. Distinct from the already-documented
  `Text_DrawWordWrapped`/`Text_DrawCenteredVertically` (different module,
  different call shape) — plausibly deliberately self-contained since a
  disc-read error can happen before the main HUD/text system is up.

### A reusable 2D sprite-batch primitive

- **`SpriteBatch_FlushAndPresent`** (`0x000b1370`) — a self-contained
  immediate-mode 2D quad-batch renderer: sets render/viewport state, clears,
  draws an untextured quad batch then a textured quad batch (each via its
  own vertex/pixel shader pair read from a per-instance shader table),
  Presents, and resets the batch counts to 0. Used by the disc-error screen;
  the generic shape (arbitrary caller-owned batch buffer, not
  error-screen-specific data) suggests it's reused elsewhere for other
  full-screen 2D content, not yet traced.
- **`SpriteBatch_PushColoredQuad`** (`0x000b1060`) — the "push one quad"
  primitive: locks the batch vertex buffer (`D3D8::D3DVertexBuffer_Lock`)
  and writes 4 vertices with position, fixed Z/W, and a packed color from 4
  rounded float components.

### Loading screen

- **`LoadingScreen_BlitImageToBackBuffer`** (`0x00148b40`) — clears the
  screen, locks the back buffer surface directly
  (`D3D8::D3DSurface_LockRect`), and blits raw decoded pixel data into it
  via `FUN_00149830` (unread) before presenting — bypasses the vertex/shader
  pipeline entirely, consistent with a static loading-screen image blit
  (matches the earlier-found literal string `"dat\audio\loading screen.bnk"`
  from `AudioSystem`'s own vtable — a loading-screen audio+image pairing).
  Caller sits at `0x00149181`, in a never-analyzed region (no containing
  function yet) — not traced further this pass.

### A generic RNG, found via the boot chain

- **`RNG_Seed`** (`0x0012a690`) / **`RNG_NextUInt32`** (`0x0012a610`) — a
  6-word carry-propagating additive/lagged-Fibonacci-style generator.
  Seeding offsets each word by a distinct large magic constant (the
  standard "decorrelate state words from one seed" technique).
- **`RNG_SeedGlobal`** (`0x0012a4b0`) / **`RNG_NextGlobalUInt32`**
  (`0x0012a4c0`) — thin wrappers operating on a single global RNG instance.
  `RNG_NextGlobalUInt32` turns out to be the same "get a random number"
  utility already seen called from `AmbientZone_PlayRandomFromPrimaryGroup`/
  `SecondaryGroup` in the audio system (referenced there via a pre-existing
  auto-named thunk, `thunk_FUN_0012a4c0` at `0x0010df70`, which jumps here)
  — a genuinely generic, widely-used engine utility, not audio-specific.
  Seeded once at boot from a system timestamp in
  `Application_InitPlatformAndDevice`.

11 renames total this pass.

## The mesh-drawing layer

Traced `D3DDevice_DrawIndexedVertices`'s 5 remaining callers (the boot/UI
work above found the other one, inside `SpriteBatch_FlushAndPresent`/
`AmbientZone`-unrelated code). This is the real "draw a 3D mesh part" layer:

- **`BoardMesh_DrawAttachedPatches`** (`0x000f8f10`) — calls the already-named
  `BoardMesh_LockBuffers`/`BoardMesh_GetBufferAddr` directly (these two were
  named in an earlier, unrecorded session — first time this session
  confirmed their existence). Walks a hash-bucket array of "attached patch"
  records, claims/reuses LOD slots from a free-list via an intrusive
  doubly-linked active list (the same idiom used throughout the
  `Node`/`NodeBase` family all project), and draws each claimed slot's
  indexed vertices in two passes toggling Xbox `D3DFILLMODE` between
  wireframe/solid. Exact purpose of the two passes not resolved.
- **`MeshRenderer_DrawSkinnedMesh`** (`0x000ff3d0`) — called directly (not
  via a dispatch table) from `FUN_00100090`. Branches between single-stream
  and dual-stream (`D3DDevice_SetStreamSource` called twice) vertex setup —
  the classic shape of skeletal-blend/skinned-mesh rendering.
- **`MeshRenderer_DrawMultiTexturedParts`** (`0x00100570`) — the richest of
  the 5. Sets up to 4 texture stages per part, and notably uses
  `D3DDevice_GetBackBuffer` + `SetTexture(0, backbuffer)` at one point —
  **sampling the previous frame's back buffer as a texture input**, a
  screen-space effect technique (plausibly ice/reflection/refraction).
- **`MeshRenderer_DrawPartsList`** (`0x000ffdc0`) — walks a linked list of
  parts, each with its own batch array; conditionally rebinds textures
  (cached, only on change) and draws each batch.
- **`MeshRenderer_DrawIndexedBatch`** (`0x000ffee0`) — the simplest: 2
  stream sources, cached shader rebind, one draw call. Likely the base
  primitive the richer variants build on.

All 5 share the same per-instance "shader-cache context" field layout
(`+0x15770`/`+0x157cc` tracking the currently-bound vertex/pixel shader to
avoid redundant `SetVertexShader` calls) — strongly suggesting a shared
low-level mesh-draw utility library used by multiple mesh types (board,
rider body parts), not one owning class each.

**Found a 16-slot mesh draw-mode dispatch table** at `0x001a2a90` — 4 of the
5 newly-named functions sit at specific slots in it (confirmed via `[DATA]`
xrefs), alongside 11 more unread function-pointer entries (one of which,
slot 0, is the exact same shared no-op stub address already seen as
`AudioSystem_NoOpStub` — the project's recurring "generic empty stub reused
across unrelated tables" idiom). **The actual dispatcher that reads this
table by index was not found** — `/search_address_refs` on the table base
returned no hits, consistent with this project's known limitation that it
only catches literal absolute-address immediates, not register-relative
table indexing. Not resolved this pass.

16 renames total this rendering-system thread (11 boot/UI + 5 mesh-draw).

## The scene-render pipeline (the actual per-frame path)

Tracing `D3DDevice_SetTransform`/`SetViewport` callers reached the real
per-frame 3D render pipeline — the "top-level orchestrator" that was open at
the end of the previous pass:

- **`SceneRenderer_RenderAllPasses`** (`0x00104330`) — the master per-frame
  scene renderer. Runs 6 sub-passes across pass-priority groups 0-4, then a
  mid-frame fullscreen-quad effect (creates/locks a vertex buffer, draws a
  fullscreen quad — plausibly a color-grade or post overlay), then passes
  5-6. Uses register-relative dispatch tables (`&DAT_001a2728` etc.), which
  is exactly why its own callers couldn't be found by address-immediate
  search.
- **`SceneRenderer_RenderPassRange`** (`0x001046a0`) — a simpler variant
  running pass groups 7-0x17, with an optional per-view setup callback.
- **`SceneView_RenderPass`** (`0x000ffb50`) — renders one view for one pass:
  builds the camera projection (perspective for 3D views, orthographic for
  2D/HUD views), sets the D3D viewport, then **walks a render-command list**
  — 6-dword records holding a vtable-dispatched draw function pointer, a
  shader input, and a pass-priority — calling each command whose priority
  matches the current pass. **This is the render-command-list dispatcher**
  that ultimately invokes the mesh draw-mode handlers.
- **`Matrix_BuildPerspectiveProjection`** (`0x0017770d`) /
  **`Matrix_BuildOrthographicProjection`** (`0x001777a1`) — standard 4×4 D3D
  projection matrix builders (FOV/aspect/near-far, and L/R/T/B/N/F bounds
  respectively).
- **`Renderer_SetDefaultDeviceState`** (`0x0016e790`) — resets all 10 D3D
  transform slots to identity, blasts ~0x39 default render states, sets
  default texture-stage state across 4 stages.
- **`D3D_InitMiniportAndFrameBuffers`** (`0x0016ed10`) — the deep NV2A
  hardware bring-up below `Renderer_InitializeD3DDevice`'s D3D8-API wrapper:
  contiguous memory allocation, `CMiniport_CreateCtxDmaObject` GPU-channel
  DMA setup, `CDevice_KickOff`, framebuffer init.

## The 16-slot mesh draw-mode dispatch table (0x001a2a90) — fully mapped

Each slot is a handler for a different mesh render mode, all sharing the
per-instance shader-cache context (base `param_2`, shader ids around
`+0x1574c..+0x157d4`, cached-bound-shader tracking at `+/-0x1593c/+0x15940`
to skip redundant `SetVertexShader`/`SetPixelShader` calls). All 16 slots now
accounted for:

| Handler | Role |
|---------|------|
| `MeshDrawMode_DecalOrColoredQuad` (`0x00100c10`) | textured strip or flat quad |
| `MeshDrawMode_ColoredQuad` (`0x00100b60`) | single per-instance-colored quad |
| `MeshDrawMode_VisibilityTestedMesh` (`0x001000e0`) | Xbox occlusion-query-wrapped draw |
| `MeshDrawMode_SkinnedWithConstants` (`0x001001f0`) | uploads 0xe VS constant regs (bone block) |
| `MeshDrawMode_DualBufferBatch` (`0x001009d0`) | selects 1 of 2 vtx-buffer/shader pairs |
| `MeshDrawMode_LineListBatch` (`0x00100ac0`) | line-list (prim type 4) batch list |
| `MeshRenderer_DrawMultiTexturedParts`/`DrawPartsList`/`DrawIndexedBatch` | (named earlier this thread) |
| `MeshQueue_InsertPrimaryBucket` (`0x000f88b0`) | **enqueue**, not draw — hash-bucket insert |
| `MeshQueue_InsertSecondaryBucket` (`0x000f88e0`) | enqueue into 2nd bucket tier |
| `Render_NoOpStub` / `Node_NoOpStub2` / `Node_NoOpStub4` | shared empty-stub fillers |

Two of the slots being **enqueue** functions (not draws) reveals the system:
meshes are first collected into hash-bucketed lists (primary/secondary tier)
by these queue-inserts, then drained per-bucket by the draw handlers —
matching the collection/draw split already seen in
`BoardMesh_DrawAttachedPatches`. Also confirmed
**`BoardMesh_BuildAndUploadGeometry`** (`0x000f7c90`) — the dynamic snowboard
geometry build/upload step that runs before the board's own draw.

33 renames total across this whole rendering thread (11 boot/UI + 5
mesh-draw + 17 scene-pipeline/dispatch-table).

## The render-context helper layer

The recurring small `FUN_000fbxxx`/`FUN_000fcxxx` calls that appear (as noise)
in nearly every render decompile are now named — they're the per-object /
per-view render-context setup layer:

- **`RenderContext_SetMatrixSlotA`/`SlotB`/`SlotC`** (`0x000fbfb0`/`0x000fc2a0`/
  `0x000fc480`) — copy a 4×4-matrix source block into render-context matrix
  slots at `+0x1a540`/`+0x1a580`/`+0x1a600` (almost certainly a
  world/view/projection-style transform triple staged for the vertex shader).
- **`Render_SetTransformVertexRegisters`** (`0x000fc730`) /
  **`Render_SetLightingVertexRegisters`** (`0x000fc630`) — push the staged
  transform and lighting constant blocks into D3D vertex-shader data
  registers (7-0xe).
- **`Render_SetupFogAndShaderConstants`** (`0x000fcf90`) — unpacks fog color
  (packed RGBA × 1/255) and fog near/far/range into the global shader-constant
  staging block.
- **`RenderContext_CycleFrameBuffers`** (`0x000fbb80`) — rotates the
  multi-buffered-resource indices (mod 2/3/4) at pass boundaries.

7 more renames (40 total this rendering thread).

## THE TOP-LEVEL FRAME DRIVER — cracked (`SceneRenderer_RenderFrame`)

This was the last open gap: `SceneRenderer_RenderAllPasses`/`RenderPassRange`
had callers only *inside a never-analyzed code blob* with no containing
function, and the blob had no incoming address xrefs (reached only through a
function-pointer table). Solved by:

1. Scanning the raw bytes backward from the caller (`0x00105d0c`) for a
   `ret` + `INT3/NOP` padding boundary → found a function start at
   `0x00105ce0`.
2. `/disassemble_at` + `/create_function` at `0x00105ce0` → Ghidra defined a
   single function spanning `0x00105ce0-0x0010618a`, which **contains both**
   frame-caller sites. It's one big per-frame render function.
3. Confirmed it's reached only via a `[DATA]` xref from a graphics-context
   method-pointer table at `0x001a2b78` — exactly why no direct callers
   existed.

**`SceneRenderer_RenderFrame`** (`0x00105ce0`) structure, in order:
1. `SceneRenderer_RenderPassRange` — the world/scene passes (groups 7-0x17).
2. A conditional **reflection render block** (gated on `DAT_001e3c7c+0x72c`):
   sets up a mirrored camera via the `FUN_000e64xx` reflection-math cluster,
   calls `FX_SpawnTrailDecal`, re-renders. This is the water/ice reflection
   pass.
3. A **split-screen viewport handler**: when `param_1[0x14e]` (a view height)
   is less than full screen height, it issues **two stacked SetViewport+Clear
   pairs** — i.e. 2-player horizontal split-screen. Concrete confirmation of
   how SSX renders multiplayer.
4. Clear-color computation, final viewport + clear.
5. `SceneRenderer_SelectDetailLevel` (picks the frame's LOD level from a
   per-view threshold array) → `SceneRenderer_RenderAllPasses` (main scene,
   groups 0-6).
6. `D3DDevice_Present` + vtable finalize.

`param_1` is the graphics-device/context object (all the
`(**(*param_1+0xNN))()` indirect calls dispatch through its vtable).

Also named the render-context **matrix stack** ops used throughout:
`RenderContext_PushMatrixStack` (`0x000fe710`) /
`RenderContext_PopMatrixStack` (`0x000fe740`) — a 0x70-byte-frame matrix
stack at context `+0x220`.

**The render pipeline is now mapped end to end**: frame table entry →
`SceneRenderer_RenderFrame` → (reflection / split-screen / detail-level) →
`SceneRenderer_RenderPassRange`/`RenderAllPasses` → `SceneView_RenderPass`
(camera projection + viewport + render-command-list walk) → `MeshDrawMode_*`
handlers → `D3DDevice_DrawIndexedVertices` → GPU. 4 more renames (44 total
this rendering thread).

## The `GfxContext` class — the render-device object (`param_1` everywhere)

`SceneRenderer_RenderFrame` being a *virtual method* pointed straight at the
central rendering class. Its vtable is a large method table at **`0x001a2b38`**
(confirmed: `RenderFrame` sits at vtable+0x40, and `+0x44`/`+0x48` = the
already-named matrix-stack push/pop). The bytes just before it are a composite
data region: the 16-slot mesh draw-mode subtable (`0x001a2a80`), a block of
config floats, and two embedded debug-label strings — **"Grid indicies"** and
**"ShadowVolumeData"** — naming adjacent data sections (so the object also owns
a grid-index buffer and a shadow-volume buffer).

`GfxContext` is the same object the earlier `RenderContext_*` helpers operate
on — "GfxContext" and "render context" are one class. Methods mapped this pass
by cross-referencing the vtable offsets `RenderFrame` invokes:

- **View accessors**: `GfxContext_GetViewWidth`/`GetViewHeight` (vtable
  +0x28/+0x2c) — read the active view's dimensions from a 0x94-byte-stride
  view array; used everywhere for viewport + split-screen math.
- **Viewport**: `GfxContext_SetViewportFromFloats` (+0x50, rounds 4 floats),
  `GfxContext_ComputeViewportScale` (+0x54).
- **Two matrix stacks**: stack #1 at `this+0x220`
  (`RenderContext_Push/PopMatrixStack`, +0x44/+0x48, re-stages matrix slot C)
  and stack #2 at `this+0x224` (`GfxContext_PopMatrixStack2` +0x70,
  `DuplicateTopMatrixStack2` +0x6c, `LoadMatrixStack2` +0x74, re-stages slot
  B). Almost certainly world vs. view/projection transform stacks.
- **Render-state command builder** — the object builds render-state commands
  into a buffer at `this+0x156d0`: `GfxContext_PushRenderState`/`PopRenderState`
  (+0xd0/+0xd4, 0x14-byte state blocks), and bit-field setters
  `SetStateAlphaFlag` (+0xe0), `SetBlendModeAndAlpha` (+0xe4, packs blend mode
  + rounded alpha), `SetTextureStageMode` (+0xf0), `BindStateTexture` (+0xf8,
  writes a clamped texture index), `SetStateColorAndCommit` (+0x1dc, commits
  via the +0x1d8 flush slot seen throughout the mesh draw code).
- **Lifecycle/sync**: `GfxContext_Destruct` (frees owned buffers),
  `GfxContext_WaitGPUAndCheckIdle` (device flush + GPU-idle check via the
  separate `DAT_001e98c8` device singleton).

Also mapped the core vtable slots 0x00-0x24 — the **view-array accessors**,
which pin down the GfxContext view layout: view array base at `this+0x38`,
view count at `this+0x3c`, active view index at `this+0x40`, **0x94-byte
per-view stride** (consistent with `GetViewWidth`/`GetViewHeight` reading
fields `+0x80`/`+0x84` of a view record). Methods: `GfxContext_GetViewCount`
(+0x24), `GetActiveViewIndex` (+0x18), `GetViewByIndex` (+0x20, returns
base+index*0x94), `FindViewByKeys` (+0x1c, linear-scans for a matching
key pair), `SelectAndSubmitView` (+0x10, find→submit), and
`GfxContext_ScalarDeletingDestructor` (slot 0). 6 more renames.

This makes `GfxContext` the best-characterized rendering class — it owns the
views (a 0x94-stride view-record array), the two transform stacks, the
render-state command buffer, and the per-frame render method itself. 22
GfxContext renames this pass, 66 renames total across this whole rendering
thread.

## `GfxContext_Init` — the render-command-pool architecture revealed

`GfxContext_Init` (`0x00104880`, vtable+0x04) is the master render-context
setup and the single most architecturally revealing function in the render
system. It allocates a full set of **tagged, typed render-command-list
pools**, and — critically — pre-seeds each pool's elements with a specific
draw-mode handler function pointer. This confirms the handlers named earlier
this thread are the *per-pool defaults*, and names the whole command-pool
scheme:

| Pool tag | Element stride | Default handler |
|----------|---------------|-----------------|
| `blendedmats` | — | (material blend buffer) |
| `defPatchList` | 0x10 | `Node_NoOpStub2` |
| `defSpriteList` | 0xc | `MeshDrawMode_DecalOrColoredQuad` |
| `defTextureRectList` | 0x10 | `MeshDrawMode_ColoredQuad` |
| `defMeshList` | 0x60 | `MeshRenderer_DrawPartsList` |
| `defVisibilityRectList` | 0x20 | `MeshDrawMode_VisibilityTestedMesh` |
| `defEmitterList` | 0x120 | `MeshDrawMode_SkinnedWithConstants` |
| `defXformList` | 0x1c | `MeshRenderer_DrawIndexedBatch` |
| `defTriList` | 0x10 | `MeshDrawMode_DualBufferBatch` |
| `defLineList` | 0x10 | `MeshDrawMode_LineListBatch` |
| `defPlayerList` | 0x50 | `MeshRenderer_DrawMultiTexturedParts` |
| `defShdVolList` | 0x50 | (shadow-volume handler, `0x001a2ad4`) |
| `defCloudNodeList` | 0x30 | (cloud-node handler, `0x001a2ad8`) |

So the render architecture is: game systems append typed records to these
per-category pools during the frame; `SceneView_RenderPass` walks the
resulting command list; each record dispatches to its pool's draw handler.
This ties together the mesh draw-mode table, the render-command-list walk, and
the `MeshQueue_Insert*Bucket` collectors into one coherent design.

`GfxContext_Init` also builds:
- **The shader table** — ~24 vertex shaders (into `this+0x55ca..`) and ~26
  pixel shaders (into `this+0x55e1..`), each created from a data blob via
  `D3DDevice_CreateVertexShader`/`CreatePixelShader`. One references the asset
  path `a\textures\t05elis.xsh`. This is the `this+0xc0` shader table that
  `SpriteBatch_FlushAndPresent` and the mesh handlers index into.
- **4 `VideoPlayer` objects** (`VideoPlayer_Construct`, vtable `0x001a83fc`,
  tag "Video Player") — the FMV/replay video playback objects.
- An **"XBoxBezierMan"** tessellation/Bezier-patch object.
- A 3×4 grid of vertex buffers, a random dither/noise table (RNG-filled),
  default render/texture-stage state (~40 `Render_SetDeferredTextureStageState`
  calls), and the overlay ortho projection (`GfxContext_InitOverlayProjection`).

4 more renames. 70 renames total this rendering thread.

## The `VideoPlayer` class (FMV / replay video playback)

Surfaced from `GfxContext_Init` (which builds 4 instances). Small,
self-contained class — vtable at `0x001a83fc` (6 methods, immediately followed
by embedded tag strings `Video::alloc_m`, `audBoduffu` (audio buffer),
`streambuff`). Fully mapped:

- **`VideoPlayer_Construct`** (`0x00148d20`) — installs the vtable, grabs the
  movie subsystem (`DAT_001e3c7c+0x720`), creates a 640×480 output surface.
- **`VideoPlayer_Open`** (`0x00148e00`, vtable+0x04) — loads a video: sets
  frame dimensions (320×240 decode), allocates a tag-confirmed **1 MB
  `streambuff`**, opens a decode stream via the movie subsystem.
- **`VideoPlayer_UpdateSkipInput`** (`0x00148a20`, +0x08) — polls the
  connected controllers each frame for a skip button (codes 0xd/100 against a
  per-player mask) so the player can skip the FMV.
- **`VideoPlayer_Tick`** (`0x001490f0`, +0x0c) — advances playback, sets the
  finished flag at end-of-stream.
- **`VideoPlayer_IsFinished`** (`0x00148b30`, +0x10) / **`VideoPlayer_IsStopped`**
  (`0x00148860`, +0x14) — state getters (`this+0x97` / `this+0x95`).
- **`VideoPlayer_FlushPendingPackets`** (`0x00148870`, found while chasing a
  `VideoStreamMan` side-lead) — drains and discards every pending decode
  packet; called twice from `VideoPlayer_Tick`.
- **`VideoPlayer_FindNextChunkByMagic`** (`0x00148be0`, same lead) — reads
  packets one at a time checking for a 4-byte magic value (`0x6843504d`,
  `"MPCh"` as ASCII) at the start, discarding non-matching packets until it
  finds one or the stream ends. A core, heavily-used primitive (2 calls from
  `VideoPlayer_Open`, 4 from `VideoPlayer_Tick`) — not a rare edge case.

This is the system behind SSX's intro/attract-mode FMVs and likely the replay
theater. 6 renames (+2 more internal helpers found later, see below). Pairs
with the earlier-found `LoadingScreen_BlitImageToBackBuffer` (adjacent code,
same back-buffer-lock blitting approach).

## `VideoStreamMan` — a sibling multi-stream video-texture system (2026-07-20)

Found as a fresh direction (continuing the "map the untouched
`InGameState_LoadLevel` tagged objects" thread). `"VideoStreamMan"` (`0x28`
bytes, `NodeRegistry` type `2` — shared with `SnowFallMan`/`PowerFX Particles`)
is the **fifth** confirmed instance this session of the "`NodeBase`-derived
`InGameState` subsystem, shared no-op stubs, real logic in custom vtable
slots" architecture — but a genuinely new, previously-undocumented subsystem:
a small pool of **simultaneous in-game video-texture streams**, distinct from
(but built on the exact same low-level decode API as) `VideoPlayer` above.

- **`VideoStreamMan_TickActiveStreams`** (vtable slot 1, was `FUN_000f6980`,
  found in completely un-analyzed code) — walks a small slot array (`this+0xc`
  = count, `this+0x10` = array) and calls **`VideoStream_DecodeNextFrame`**
  (was `FUN_000f6630`) for each active slot. That function is frame-rate-
  paced (a frame-interval accumulator) and calls a small cluster of shared
  low-level video functions (`FUN_0014f170`/`f260`/`f000`/`f610`) — **the
  exact same address range/API `VideoPlayer` itself uses** (confirmed via
  `xrefs_to`: also called from `FUN_00148870`/`FUN_00148be0`, two more
  unnamed functions right next to `VideoPlayer_IsStopped`, likely more
  `VideoPlayer`-adjacent methods not yet mapped — a lead for later).
- **`VideoStreamMan_SubmitReadyFrames`** (vtable slot 2, was `FUN_000f6ac0`,
  also found in completely un-analyzed code) — for slots with a ready-frame
  flag pair set, calls the `GfxContext` singleton's vtable`+0xb4` (a
  texture-registration-shaped call) — submits a decoded video frame as a live
  texture for the renderer.

Reads as the mechanism behind in-level video billboards/screens (arena
jumbotron-style displays, maybe replay-preview thumbnails) rather than
full-screen FMV playback — multiple small simultaneous streams instead of one
full-screen one. Same honest "no static per-frame caller found" status as the
4 other instances this session (`SnowFallMan`/`LessonMan`/`PowerFX Particles`/
`TerrainNode`) — `xrefs_to` on both real methods shows only their own
vtable-slot data references. 5 renames.

## The `BezierMan` class (Xbox Bezier-patch tessellation)

Also surfaced from `GfxContext_Init` (which builds one instance, tag
`XBoxBezierMan`). This is the curved-surface tessellation manager — it drives
the NV2A's hardware tessellation of Bezier patches (used for smooth
terrain/character surfaces). vtable at `0x001a2a50` (12 slots, several shared
`Node_NoOpStub2` no-ops). Methods mapped:

- **`BezierMan_ScalarDeletingDestructor`** (`0x000fe5a0`, slot 0).
- **`BezierMan_SetDefaults`** (`0x000f7610`) — sets default tessellation
  params and enables all four tess-enable flags (`this+0x28..+0x2b`).
- **`BezierMan_ReleaseBuffers`** (`0x000f7640`) — GPU-waits then frees the
  tessellation buffers.
- **`BezierMan_GetTessFactorU`/`GetTessFactorV`** (`0x000f7760`/`0x000f7790`)
  — return the U/V tessellation factors, **doubled in split-screen mode**
  (gated on the multi-view count) to keep patches smooth at lower per-view
  resolution.
- **`BezierMan_SetTessEnableA`/`B`/`C`** (`0x000f77d0`/`e0`/`f0`) +
  **`GetTessEnableA`** (`0x000f7800`) — per-axis tessellation-enable
  flag accessors.

9 renames. 85 renames total this rendering thread.

## `GfxContext`'s real name, and two fresh systems: `LightManager` and `LensFX`

Reading the data block immediately after `GfxContext`'s vtable (`0x1a2b38`) —
already known to hold the mesh draw-mode pool tags from `GfxContext_Init` —
turned up more embedded tag strings further along: `"cXBoxGraphicsMan"`,
`"cXBoxGridMesh"`, `"LensFX"`, `"LightMan"`, `"LightObjs"`.

- **`GfxContext`'s real internal class name is `XBoxGraphicsMan`** —
  confirmed by tracing `"cXBoxGraphicsMan"`'s one allocation site
  (`GfxContext_ConstructSingleton`, a ~2.2MB tagged allocation) to
  `GfxContext_Construct`, which installs exactly the `0x1a2b38` vtable this
  project has been calling `GfxContext` for several turns. Kept the
  `GfxContext_*` naming already in wide use (still accurate) rather than
  mass-renaming ~30 existing symbols — but the real source name is now on
  record.
- **`LightManager`** (tag `"LightMan"`) — a brand-new, previously
  untouched lighting system. A light-object pool: array base at `this+0x10`,
  count at `this+8`, high-water mark at `this+0x14`, 0x40-byte (64-byte)
  stride per light. Vtable `0x1a2e90`, 10 methods mapped:
  - `LightManager_Construct`/`Destruct`
  - `LightManager_AllocateLightPool` (tag-confirmed `"LightObjs"` — a
    `(count+0x100)*0x40`-byte array, each slot's sort/distance field
    initialized to `0x7fffffff`) / `FreeLightArray`
  - `LightManager_GetLightByIndex` (the standard index-accessor idiom seen
    throughout this project — same shape as `IconAtlas_GetEntry`/
    `GfxContext_GetViewByIndex`)
  - `LightManager_AllocateNextLightSlot` (claims the next slot, tracks a
    high-water mark) / `SaveLightCursor` (checkpoint/restore, plausibly for
    scoped per-draw light lists)
  - **`LightManager_AddAmbientLight`** (type=0, scaled color, no position)
    and **`LightManager_AddPointLight`** (type=1, position + color) — the
    two light-submission entry points. Two more overload variants
    (`0x106870`/`0x1068a0`, chain-calling the base then extra processing)
    exist but weren't traced.
  - **CORRECTED (same session)**: the helper `0x000e25e0` was briefly named
    `Device_ConstructSingleton` on the theory that `DAT_001e98c8` was a
    separate device object. Reading `LightManager_Construct`'s **raw bytes**
    (`mov ecx, esi; call ...`) disproved it — the call receives the
    LightManager object itself as `this`. So **`DAT_001e98c8` IS the
    LightManager singleton**, and the helper is its base-class constructor
    (→ `LightManagerBase_Construct`, plus `LightManagerBase_Destruct` and
    the base vtable's `ScalarDeletingDestructor` at `0x19d208`, nearly all
    shared stubs). Consequence: `GfxContext_WaitGPUAndCheckIdle`'s call
    through `DAT_001e98c8+0x14` is actually `LightManager_SaveLightCursor` —
    a per-frame light-list cursor reset, not a "device flush" (annotation
    fixed), and `LightManager_AllocateLightPool`'s call through the same
    global is a virtual self-call to a retail-stripped stub.
- **`LensFX`** (tag `"LensFX"`, ~20.7KB allocation via a distinct tagged-pool
  allocator) — a lens-flare/lens-effect rendering system, **now fully
  vtable-mapped** (vtable `0x1a2e40`). The classic lens-flare design is
  plainly visible: an Xbox **occlusion query** measures how much of the
  sun/light source is unoccluded, and the visible-pixel ratio drives the
  flare sprites' intensity:
  - `Render_ReadVisibilityTestResult` (`0x000fbe90`) — generic
    occlusion-query readback (`D3DDevice_GetVisibilityTestResult`), with a
    blocking-poll mode (up to 0x100000 spins).
  - `LensFX_WaitVisibilityResult` — thin blocking wrapper over it.
  - `LensFX_UpdateCoreSpriteIntensity` — intensity = master × per-flare
    weight, gated on visible-ratio (`visible*2/(total+eps) ≥ 1`), written
    into 3 fixed sub-sprites' alpha fields.
  - `LensFX_UpdateFlareArrayIntensity` — same computation applied to a
    variable-length array of secondary flare elements (the row along the
    light-to-screen-center axis), additionally gated on the GfxContext fog
    field (`+0x15928`) — flares fade out in fog.
  - `LensFX_SetFlareRenderState` (additive-blend state submit),
    `LensFX_SetViewportAndViewMatrix` (per-pass viewport + matrix-slot-B
    staging), `LensFX_ScalarDeletingDestructor`, `LensFX_StubReturnZeroFloat`.
  - **`LensFX_ConstructBase` installs a shared base vtable (`0x19d1b8`),
    named `FXNode`** — confirmed by reading it directly: slot 0 is
    **`FXNode_ScalarDeletingDestructor`** (calls
    `NodeBase_UnlinkAndResetVtable`, confirming this FX base is itself
    `NodeBase`-derived), and slots 1-15 — including the already-named
    **`FX_TrailInstance_UpdateTransform`** at slot 2 and
    `InputDevice_StubReturnFalse` at slots 3/4 — are inherited **unmodified**
    into LensFX's own vtable (only slot 0 is overridden). This is an
    important correction-by-context: `FX_TrailInstance_UpdateTransform`
    (named in an earlier session for the rail-grind spark-trail system, see
    `RE_NOTES_ubertrick_fx_cluster.md`) turns out to be a **generic
    `FXNode`-family virtual method**, not something trail-specific — its
    existing name is left as-is (still correct for its own call chain) but
    should now be read as a shared base method, the same "shared Node-family
    vtable slot" pattern this project has found repeatedly elsewhere.
- ~~`cXBoxGridMesh`~~ **DONE** — its allocation site is `New_XBoxGridMesh`
  (`0x00103fb0`), found and named while reading `GfxContext`'s full vtable
  (see below); `/create_function` succeeded once the whole surrounding
  cluster was bulk-created together, resolving the earlier boundary-detection
  failure.

14 renames. See the vtable-slot tables above for the full mesh/scene mapping
this connects to.

## xbox.gdt real struct types applied (2026-07-20)

Now that `xbox.gdt` (2,084 real Xbox SDK types) is imported into the project as a
Data Type Archive, `set_local_variable_type`/`set_function_prototype` can pull real
struct types across the archive boundary (confirmed live, see "Round 7" in the
`reference_ghidra_mcp_connection` memory for the mechanism and a critical caveat: the
endpoint reports success even when it silently falls back to `int` on an unresolved
type name -- every application below was independently verified by re-decompiling and
confirming real field names/struct layout appeared, not just trusting the response).

Six sites all matched the exact 6-dword (24-byte) `D3DVIEWPORT8` layout
(`X, Y, Width, Height, MinZ, MaxZ`) built up in stack locals right before a
`D3DDevice_SetViewport` call -- applying the type merged the individual
`undefined4`/`int` stack slots into one real struct, confirmed via field access like
`uStack_30.X` appearing post-application:

- `LensFX_SetViewportAndViewMatrix` (`0x00106210`) -- `param_1`.
- `D3DDevice_SetViewport` (`0x00169460`) -- the function's own prototype, now
  `void D3DDevice_SetViewport(D3DVIEWPORT8 *param_1)`.
- `SceneView_RenderPass` (`0x000ffb50`) -- `local_18`.
- `D3DDevice_SetRenderTarget` (`0x001691c0`) -- `local_18` (the implicit
  full-target viewport reset it issues after rebinding).
- `SpriteBatch_FlushAndPresent` (`0x000b1370`) -- `local_18`.
- `SceneRenderer_RenderFrame` (`0x00105ce0`) -- `uStack_30` (reused across the
  split-screen top/bottom pass) and `uStack_18` (the final full-frame restore).

Checked for other struct-shaped D3D8 API opportunities and came up empty: no
`D3DDevice_SetLight`/`SetMaterial`/`GetDeviceCaps` calls anywhere in the codebase --
confirms the earlier finding that this game's lighting is entirely the custom
`LightManager`/shader-constant system, not D3D8 fixed-function lighting. Also tried
`D3DPRESENT_PARAMETERS` on `Renderer_InitializeD3DDevice`'s (`0x000b2810`) 48-byte
`local_34`/`local_30[7]`/`local_14`/`local_10`/`local_8`/`local_4` block (which is
genuinely a presentation-parameters struct by call-site evidence -- passed as the 5th
arg to `Direct3D_CreateDevice`) -- **the type isn't present in the built `xbox.gdt`**,
most likely because its `D3DSurface *BufferSurfaces[3]` field depends on a bare
forward declaration that never resolved when the archive was built. Reverted the
variable to its safe original `undefined4` rather than leave it silently mistyped as
`int`. Not a solvable problem from this side without rebuilding the `.gdt` itself --
left as `undefined4`/`undefined4[7]`/etc., documented rather than guessed.

**CORRECTION, same day, later**: the above diagnosis was wrong. A diagnostic
script (`scripts/ssx_diag_list_open_archives.py`, using the exact
`DataTypeManagerService` GhidraMCP itself calls) proved `D3DPRESENT_PARAMETERS`
was **never missing** from the original `xbox.gdt` -- it's right there at
`/xbox.h/D3DPRESENT_PARAMETERS`. The real cause: `set_local_variable_type` only
ever searches the *program's own* DataTypeManager (confirmed by re-reading the
plugin source), never other open archives directly -- unlike
`set_function_prototype`, which genuinely is cross-archive-capable via
`DataTypeManagerService`. `D3DVIEWPORT8` only ever worked directly because an
earlier `set_function_prototype` call had already "seeded" it into
`default.xbe`'s own DTM as a side effect; `D3DPRESENT_PARAMETERS` never got that
seeding step. **Fix, confirmed live**: applied the real, accurate Xbox D3D8 SDK
signature to `Direct3D_CreateDevice` (`D3DPRESENT_PARAMETERS *pPresentationParameters`
as its 5th param, matching the actual call site's argument shape) via
`set_function_prototype` -- this made Ghidra auto-infer real enum types
(`D3DFORMAT`, `D3DSWAPEFFECT_DISCARD`, `BOOL`, `UINT`) for the individual fields at
the `Renderer_InitializeD3DDevice` call site, and made `set_local_variable_type`
go from "Type not found directly" to actually finding the type (though applying
it to the specific 48-byte/12-slot local variable still failed -- **honestly**,
with an explicit error, not a silent `int` fallback -- likely an unrelated
stack-merge size limit, not a resolution issue). The `xbox_fixed.gdt` rebuild
(header reorder + `CParserUtils` rebuild) was chasing a misdiagnosis -- harmless,
but not the actual fix. Full corrected mechanism in the `reference_ghidra_mcp_connection`
memory's "Round 7 CORRECTION" entry.

## `GfxContext`'s method table at 0x001a2b60, fully characterized (2026-07-20)

The 16-entry method table (confirmed reached via a data xref, same table
`SceneRenderer_RenderFrame` sits in at slot 6) was fully read via
`/read_bytes` and every slot resolved. 5 of the other 15 slots pointed into
never-analyzed code (no containing `Function` object) -- same situation as
the original `SceneRenderer_RenderFrame` discovery, but this time every
address was a genuine, correctly-aligned function start (no backward
boundary-scan needed). All 5 created via `/create_function`; 4 of the 5 (plus
one pre-existing-but-unnamed `FUN_000fe940`) had clear enough bodies to name
with structural confidence:

- **`GfxContext_ApplyUniformGammaRamp`** (slot 3, `0x000fe4f0`) -- builds a
  256-entry gamma ramp, writing the *same* computed byte into all three
  R/G/B channel tables, then calls `D3D8::D3DDevice_SetGammaRamp(2, ramp)`.
  A uniform (not per-channel) hardware-gamma fade -- almost certainly the
  engine's screen fade-to-black/white transition effect.
- **`GfxContext_SetViewRectAndApplyViewport`** (slot 9, `0x000f9c70`) --
  copies a 4-float X/Y/W/H rect into the context's nested view object,
  assembles a `D3DVIEWPORT8` (now typed, confirmed by Ghidra auto-inferring
  `D3DVIEWPORT8 DStack_18;` in the decompile from the retyped
  `D3DDevice_SetViewport` prototype) and calls it directly, then sets a
  dirty/applied flag byte.
- **`GfxContext_GetViewRect`** (slot 14, `0x000f9d90`) -- the getter
  mirroring the setter below, reads 4 floats out of the view object at
  `+0x220`.
- **`GfxContext_SetOrthographicViewAndApply`** (slot 12, `0x000fe940`) --
  takes left/top/width/height + near/far + a split-screen-offset flag,
  builds an orthographic projection via `Matrix_BuildOrthographicProjection`
  into the `+0x220` object, applies a split-screen row-scale correction, and
  pushes it via `RenderContext_SetMatrixSlotC`. The 2D/UI orthographic setup
  entry point (HUD/sprite-batch rendering path).

Two slots were created but **deliberately left unnamed**: slot 2
(`FUN_000f9b80`, copies 3 floats from a param into `+0x44/48/4c`) and slot 15
(`FUN_000f9df0`, returns the float at `+0x220+0x60` as an x87 `float10` --
the same field `GfxContext_SetOrthographicViewAndApply` unconditionally
zeroes right after rebuilding the projection). Their table-slot role and
operand shape is documented, but the specific semantic meaning (near/far
params? a cached aspect correction? a dirty-version counter read back as a
float?) isn't determinable from the bodies alone -- left unguessed per
project convention. 4 renames.

## Open leads (rendering)

- ~~The `FUN_000e6xxx` cluster~~ **DONE, and NOT reflection-specific** — turned
  out to be a shared, emitter-agnostic `FXParticle_*` utility library (init
  transform/timing, size-range, color-gradient setup), reached from 3 distinct
  contexts: `Rider_UpdateSnowSprayFX` (rider velocity/terrain-driven spray),
  `FXParticle_SpawnFromDescriptor` (a generic config-table spawner), and
  `SceneRenderer_RenderFrame`'s reflection block (feeding the already-named
  `FX_SpawnTrailDecal`). All 15 functions in the cluster (including the
  `FUN_000e62b0`/`62c0`/`6560`/`6230`/`6530`/`6740` siblings) are now named —
  full writeup in `RE_NOTES_ubertrick_fx_cluster.md`.
- ~~`FUN_00104880` (vtable+0x04)~~ **DONE** — this is `GfxContext_Init`, fully
  read (see the "render-command-pool architecture revealed" section above).
- **`GfxContext`'s vtable: 46 of 120 slots still unnamed** (down from ~49
  never-analyzed + more found unread — 74 slots now resolved). All 46 are
  already created as functions (no more boundary-detection work needed), just
  not individually read/named yet. Known clusters within the remainder worth
  checking first: a tight `0x000fb0xx-0x000fb5xx` group (`FUN_000fb080`/
  `b110`/`b160`/`b250`/`b320`/`b3e0`/`b3f0`/`b560`/`b860`) and a
  `0x000fecxx-0x000fedxx` group (`FUN_000feb00`/`ecd0`/`ed00`/`ed50`/`ed80`/
  `edd0`) — both untouched, likely more init/reset or accessor families like
  the ones already characterized nearby. Also still open: 2 slots in the
  `0x00103d00-0x00104060` cluster that were missed when the rest of it was
  read (`FUN_00103d00`/`00103d20`, right before the cluster this session
  covered), and the 8 forwarding-wrapper functions tied to the `exUT_`/`exTW_`
  named-record-queue system (see above). Exact list of all 46 remaining
  addresses is reconstructable from a fresh `/read_bytes` at `0x001a2b38`
  (120 slots, 0x1e0 bytes) cross-referenced against `/list_functions` — same
  method used to find this batch.

## Next steps (not yet done)

- ~~The top-level per-frame frame driver~~ **DONE** — `SceneRenderer_RenderFrame`
  (see above). The render pipeline is now mapped end to end.
- ~~The `FUN_000e64xx` "reflection-math cluster"~~ **DONE** — see the
  correction in "Open leads" above; it's the shared `FXParticle_*` utility
  library, not reflection-specific. Full writeup in
  `RE_NOTES_ubertrick_fx_cluster.md`.
- ~~The graphics-context method table at `0x001a2b60`~~ **DONE** — all 16
  slots resolved (see the "fully characterized" section above); 4 new names,
  2 deliberately left unnamed (unclear semantics, not unclear location).
- ~~The dispatcher that reads the 16-slot mesh draw-mode table by index~~
  **DONE (it never existed as a separate function)**. Traced
  `SceneView_RenderPass`'s actual per-record dispatch
  (`(*(code *)**(undefined4 **)*puVar4)(param_2)`) -- it just calls through
  an already-resolved function pointer stored in the record, no index
  arithmetic at all. Checked `MeshQueue_InsertPrimaryBucket` (the enqueue
  step) too -- it indexes a hash-bucket array by a per-mesh draw-mode ID,
  also not the 16-slot table directly. The real answer was already sitting
  in the existing `GfxContext_Init` documentation below: each pool
  (`defPatchList`/`defSpriteList`/etc.) is pre-seeded with its own dedicated
  default handler *once*, at startup -- there's no runtime "read table[id]"
  step to find because the table is only ever consulted once, by
  `GfxContext_Init` itself, to seed the pools. The original open item's
  premise (assuming a hidden per-frame dispatcher must exist) was the actual
  mistake, not a search failure.

## `GfxContext`'s full vtable read (0x001a2b38, 120 slots) — texture pipeline found (2026-07-20)

Read all 120 slots (0x1e0 bytes) via `/read_bytes`, decoded and cross-referenced
against the live function list. ~49 slots pointed into never-analyzed code (the
largest batch of this kind found in the project so far). Bulk-created all of
them via `/create_function` -- 2 turned out to already be known functions
(`FX_SpawnTrailDecal`, `thunk_FUN_00101290`), confirming `GfxContext`'s own
vtable directly contains the already-documented `FX_SpawnTrailDecal`. Of the
remaining ~47 genuinely new functions, characterized 2 coherent clusters with
strong field-offset evidence (16 renames); roughly 27 slots remain unread
(mostly a `0x00103d00-0x00104060` cluster near `GfxContext_Init`, plus
scattered small getters) -- left for a future pass.

**Texture queue/upload pipeline** (5 renames): a complete "queue → swizzle →
register" pattern.
- **`GfxContext_ParseAndQueueTexture`** (`0x000fa1d0`) -- translates a compact
  on-disk format byte (`0x60`-`0x7e`) into a format enum and forwards to...
- **`GfxContext_QueueTextureFromRawData`** (`0x000fa2a0`) -- takes raw pixel
  data + width/height/mip-count/format, computes the mip-chain size,
  tag-allocates a buffer, either straight-copies (raw formats) or swizzles
  each mip level via `XGRAPHC::XGSwizzleRect`, appends the descriptor to a
  pending-texture table.
- **`GfxContext_SetPendingTextureCount`** (`0x000f9f10`) -- trivial setter,
  sets the tag value the registration step's allocator call reads.
- **`GfxContext_RegisterTextureTable`** (`0x000f9f20`) -- flushes the pending
  queue: builds a packed D3D texture resource header (power-of-2 mip-level
  encoding) per descriptor and calls `D3D8::D3DResource_Register`.
- **`GfxContext_UploadSwizzledTextureData`** (`0x000fa570`, +
  **`GfxContext_UploadTextureData_Thunk`** `0x000fa690`, a 2-arg forwarder) --
  a second, similarly-shaped swizzle-and-upload step operating on an already-
  registered texture slot (indexed via **`GfxContext_GetTextureHandleBySlot`**
  `0x000fa6b0`) -- likely a re-upload/update path distinct from the initial
  queue-and-register pipeline above, not fully disambiguated from it.

**Render-state-record decoder** (9 renames): the compact-record-to-individual-
setter-calls pattern already seen partially (`GfxContext_PushRenderState`/
`PopRenderState`/`SetStateAlphaFlag`/`SetBlendModeAndAlpha`/`SetTextureStageMode`/
`BindStateTexture` were already named from an earlier pass) now has its master
decoder identified:
- **`GfxContext_ApplyRenderStateRecord`** (`0x000fa760`) -- unpacks a compact
  3-dword bitfield record and dispatches each field to its own setter slot,
  then loops ~4 texture stages calling `BindStateTexture`/
  `GfxContext_SetStageFilterMode`/a shared no-op per stage.
- **`GfxContext_ApplyDefaultRenderStateBatch`** (`0x000fa730`) -- copies a
  fixed 5-dword default state block from a global (`&DAT_001ead20` — the same
  global `SceneRenderer_RenderFrame` passes directly to
  `GfxContext_SetStateColorAndCommit`) and forwards it into the decoder above.
- **`GfxContext_SetStateExtraFlags`**/**`GetStateExtraFlags`** (`0x000fa910`/
  `0x000fa930`) — a 5-bit field pair (structural confidence, exact D3D
  semantic not pinned down).
- **`GfxContext_SetStateModeAndColor24`** (`0x000fa960`) — a 2-bit mode +
  24-bit clamped-value (likely packed RGB) setter, called right after
  `SetTextureStageMode` in the decode sequence.
- **`GfxContext_SetStageFilterMode`** (`0x000faa40`) — a per-stage 2-bit
  field setter called inside the 4-stage loop alongside `BindStateTexture`.

**Matrix-stack-2 / orthographic-view getters** (3 renames): getter
counterparts to already-named setters.
- **`GfxContext_GetOrthographicViewParams`** (`0x000f9e20`) — reads the 3
  fields `GfxContext_SetOrthographicViewAndApply` writes.
- **`GfxContext_GetMatrixStack2Pointer`** (`0x000f9e90`) — trivial getter for
  the matrix-stack-#2 base pointer (`this+0x224`).
- **`GfxContext_SetBlendPresetByMode`** (`0x000f9ea0`) — a mode-dependent
  (0/1/2) set of blend/alpha-threshold float constants (structural
  confidence, exact preset semantics not pinned down).

16 renames total this pass.

**Follow-up, same day**: read the `0x00103d00-0x00104060` cluster flagged
above as unread -- ~15 small functions in a tight address range near
`GfxContext_Init`. Found **`New_XBoxGridMesh`** (`0x00103fb0`, tag-confirmed
"cXBoxGridMesh"), closing the previously-flagged open item (`/create_function`
had failed at this address in isolation; bulk-creating the whole surrounding
cluster together resolved the boundary-detection failure). Also named 6
structurally-confident getter/setter pairs: `GfxContext_SetBlendPresetRaw`/
`GetBlendPresetPointer` (a direct-value variant of the mode-selected blend
preset already named), `GfxContext_GetTextureStageMode`/`GetStateModeBits`
(getters for two already-named setters), `GfxContext_SafeDestructObject` (a
generic null-safe conditional-destruct helper -- calls a passed object's own
vtable slot 0 with delete-flag=1, not necessarily GfxContext-specific despite
living in its vtable), and `GfxContext_SetTailFlagByte` (a flag byte near the
tail of GfxContext's documented ~2.2MB allocation). 7 renames.

Found but left unnamed: 8 sibling forwarding-wrapper functions in the same
cluster all tail-call a small set of shared functions with an extra trailing
discriminator param. One of those shared functions (`FUN_00102360`)
references the same `"exUT_SIGMAR"`/`"exUT_OTMETHOD"` tagged-record-queue
strings documented in `RE_NOTES_ubertrick_fx_cluster.md`'s "candidate
Ubertrick/camera/track named-queue subsystem" bookmark -- ties this cluster to
that already-known system, but not enough evidence yet to name each variant
individually.

**Follow-up, same day**: read the `0x000fb0xx-0x000fb3xx` sub-cluster flagged
above. Found a genuine **clip-space frustum-test family**:
`GfxContext_ComputeClipOutcode` (`0x000fb0a0`) is a textbook Cohen-Sutherland-
style outcode test (checks x/y/z against `[-w, w]`, the standard post-transform
homogeneous clip test); `GfxContext_TestAABBVisibility`/
`ClassifyAABBAgainstFrustum` (`0x000fb250`/`0x000fb320`) both enumerate all 8
corners of an axis-aligned bounding box, transform each corner via the newly-
named `Matrix_TransformVector4` (`0x00025ab0`, a standalone 4x4-matrix*vec4
utility, not GfxContext-specific), classify via the outcode function, and
combine into a 0=outside/1=inside/2=partial result -- the standard bounding-
volume-vs-frustum visibility test used for culling. Also named
`GfxContext_ProjectPointToScreen` (`0x000fb160`, perspective divide + NDC-to-
screen remap), `GfxContext_ClassifyPointAgainstFrustum` (`0x000fb110`, the
single-point variant), and `GfxContext_TestAABBVisibility_Thunk` (`0x000fb3e0`,
a thin forwarder). 7 renames (2 outside the vtable itself:
`Matrix_TransformVector4` and the outcode function's callee chain).

Still unread in this sub-cluster: `FUN_000fb080` (a plain 4-float setter,
unclear target field), `FUN_000fb560`
(a double-indirection through two different objects' vtables, insufficient
evidence), and `FUN_000fb860` (sets 2 fields via the `"exUT_SKINDY"`/
`"exUT_MTSTALEFISH"` tagged-offset idiom -- a second confirmed connection to
the `exUT_`/`exTW_` named-record-queue bookmark, alongside the `FUN_00102360`
one found earlier).

**Resolved (2026-07-20, later session)**: `FUN_000fb3f0` — re-read while
mapping `FogMan` (`RE_NOTES_weather_effects.md`), which also references
`DAT_001faf90` (at a different sub-offset, `+0x38` for a fog-volume count vs.
this function's `+0x58`). Confirmed `DAT_001faf90` is a shared "level
geometry data" singleton with multiple independent sub-tables. Renamed
**`GfxContext_ExtractQuadVertexAttributes`** — given 4 vertex indices (a
quad), extracts position/normal/UV attributes for each from a `0x20`-byte-
stride indexed table. Mechanics clear; exactly which mesh/quad system owns
this table is still not pinned down.

**Follow-up, same day**: read the `0x000fecxx-0x000fedxx` sub-cluster flagged
above. Found the **matrix-stack-2 builder family**:
`GfxContext_ResetMatrixStack2ToIdentity` (loads a canned base matrix),
`GfxContext_ApplyMatrixToStack2`/`ApplyTranslationToStack2`/
`ApplyRotationToStack2` (concatenate a caller-supplied transform via the newly-
named `Matrix_Multiply4x4`), and `GfxContext_BuildTransformMatrixStack2` (the
full builder: 3 chained axis-angle Euler rotations + a point-delta translation)
-- all push the result to matrix slot B. Also found **`FX_SpawnRadialDecal`**
(`0x000fedd0`): transforms a point, lazily builds a large static table of
precomputed unit direction vectors (a circular/radial fan pattern), constructs
a vertex ring scaled by a radius param, and submits it through vtable+0x11c --
one of the forwarding-wrapper functions tied to the `exUT_` tagged-record-queue
system found earlier, and named to match the already-known `FX_SpawnTrailDecal`
(which sits in this same vtable at slot 0x140) -- likely a shockwave/burst/
landing-impact decal effect. 7 renames.

**Follow-up, same day**: swept the remaining scattered singles in the vtable.
Notable finds: **`GfxContext_ApplyRenderStateDelta`** (`0x000faae0`) -- a
dirty-state-caching variant of `GfxContext_ApplyRenderStateRecord`, XORing
cached vs. new state and only issuing D3D calls for what actually changed
(the render-state change-detection optimization); **`GfxContext_SetActiveViewAndResetDevice`**
(`0x000f9960`) -- switches the active view index and calls
`D3D8::D3DDevice_Reset` when the new view's presentation params differ,
the mechanism behind split-screen view switching; **`Skin_AccumulateWeightedBoneMatrices`**
(`0x000fdf30`) -- a shared skeletal-blend utility (short-integer bone-weight
normalization, the classic 0-32767 skinning idiom); plus
`GfxContext_ReleaseFrameResources`, `GfxContext_ResetRenderStateCache`,
`GfxContext_FormatDisplayModeLabels`, `GfxContext_StubReturnZero`, and 2
lower-confidence structural names (`GfxContext_DrawPartsWithFogCheck`,
`GfxContext_QueueBillboardDraw`). 9 renames.

## Cracked the "candidate Ubertrick/camera/track named-queue subsystem" bookmark (2026-07-20)

An earlier session found a dense cluster of `exUT_`/`exTW_`/`frL_`-tagged
field accesses (named-queue-style `"tag"+index*stride+field` reads) mixed
with camera-mode names ("chase near/board/eyes/reverse", "replay cam") and
truncated track names ("mesablan"/"snowdrea"), bookmarked it as a candidate
subsystem, and flagged 3 functions worth checking first without decompiling
them. This session finally read them:

- **`GfxContext_InitCameraModeRecords`** (`0x001041a0`, the bookmark's own
  "top candidate") — called directly from `GfxContext_Init`. Confirmed as a
  `this`-taking GfxContext method (via the shared `this+0x156d0`/
  `this+0x22c9xx` fields already characterized elsewhere). Zeroes the named
  camera-mode tag storage and the truncated-track-name fields, then calls
  **`CameraMode_InitRecord`** (`0x000fbdf0`) 3 times to initialize named
  camera presets ("replay cam", an unlabeled preset, "scripts") — each a
  0x68-byte record with a type field, position-ish and orientation-ish
  vec3s, and a fixed sentinel. **This solves the bookmark's "camera" half
  precisely**: these are camera-mode name/preset records set up as part of
  `GfxContext`'s own initialization, not a trick-record system.
- **`Level_GetCurrentTrackNameTag`** (`0x0007b800`) — a 12-case switch on a
  global track index returning each track's 8-char tag (garibald/snowdrea/
  elysium/mesablan/merqury/aloha/pipedrea/untracke/tokyo/trick/alaska) —
  matches `TrackTable`'s 12-track count exactly. The short-tag companion
  lookup to the already-documented `TrackTable`, explaining where the
  "mesablan"/"snowdrea" truncated names actually come from.
- **`GfxContext_SetPerPassCallback`** (`0x000fb860`, previously guessed at
  as tied to this system without being resolved) — confirmed by its
  consumer: the already-named `SceneRenderer_RenderPassRange` conditionally
  calls through the *exact same* field pair this function writes (a
  callback function pointer at `"exUT_SKINDY"+this+8`, a context value at
  `"exUT_MTSTALEFISH"+this`) once per pass-range invocation, right after
  cycling frame buffers. A genuine per-frame extensibility hook, not a
  named-record queue at all.

**Net result**: the bookmark's "Ubertrick/camera/track named-queue" framing
was half right (camera and track name lookups are genuinely there) but the
"named-queue" mechanism itself turned out to be simpler than hypothesized —
just tagged static-storage offsets used as compile-time-computed field
addresses (a linker/compiler idiom for named globals, not a runtime
queue/pool). The 8 forwarding-wrapper functions from the
`0x00103d00-0x00104060` GfxContext-vtable cluster (still unnamed) call into
a genuinely large, complex function (`FUN_00102850`, 13 params) that also
touches this tag family — left for a dedicated future pass rather than
forced open in this one. 4 renames this pass.

**Follow-up, same day**: pushed into `FUN_00102850` (13 params) rather than
deferring it again. It calls `Font_GetGlyphMetrics` directly and walks a
string building a perspective-projected textured quad per character --
**`Text_RenderGlyphString`**, the core glyph-by-glyph rasterizer underlying
the already-documented text system. This also retroactively confirms the
"exUT_"/"mesablan" tags seen throughout this whole investigation were never
a real named-queue mechanism -- `Text_RenderGlyphString` reuses
`"mesablan"+this+8` as a plain integer counter, nothing to do with track
names; it's just fixed-offset scratch storage that happens to alias with
unrelated string-literal addresses elsewhere in the same static data region.
Named 2 of its forwarding wrappers precisely (`Text_DrawGlyphStringByte`/
`Text_DrawGlyphStringWide`, matching the confirmed byte vs. wide-string walk
branches); the sibling shared functions (`FUN_00102360`/`00102180`/
`001025b0`/`00101a90`/`00101d00`) share strong structural resemblance
(one, `FUN_001025b0`, has an almost identical parameter signature) but
weren't individually confirmed this pass. 3 more renames.

**Full closure, same day**: read the remaining 5 shared functions
(`FUN_00102360`/`00102180`/`001025b0`/`00101a90`/`00101d00`) and all 6
remaining forwarding wrappers, closing out the whole exUT_-tagged submission
family completely. They all feed the same growing render-command-buffer
architecture as `Text_RenderGlyphString`:

- **`GfxContext_SubmitColoredQuad`** — a generic 4-vertex quad primitive.
- **`GfxContext_SubmitScreenRect`** — a simpler screen-space rect variant
  (raw x1/y1/x2/y2/u1/v1/u2/v2 + color, no pointer indirection).
- **`Text_RenderGlyphStringScreenSpace`** — the screen-space (non-3D-
  projected) sibling of `Text_RenderGlyphString`.
- **`FX_SubmitParticleBatch`** / **`FX_SubmitParticleBatchWithFog`** — take
  a pre-built vertex-record array and submit it as a batch, the fog variant
  using a fixed fog color instead of computing per-vertex color when
  GfxContext's fog field is active. Submitted via a *second*, separate
  growing buffer (`exUT_OTSTALEFISH`) from the one text/quads use
  (`exUT_SIGMAR`).
- 6 thin wrapper functions (`GfxContext_DrawQuad`/`DrawScreenRect`,
  `Text_DrawGlyphStringScreenSpaceByte`/`Wide`, `FX_DrawParticleBatch`/
  `WithFog`) — convenience entry points, all confirmed forwarders.

Also swept the last few scattered `GfxContext` vtable getters:
`GfxContext_GetActiveViewFlag1`/`2` (2 per-view flag bytes near the tail of
the 0x94-byte view record), `GfxContext_GetOrthoViewParamY`/`Z` (single-
field convenience accessors for fields `GfxContext_GetOrthographicViewParams`
already returns as a triple), `GfxContext_GetMatrixStack1Bounds` (a distinct
4-float block on the matrix-stack-1 object), and
`GfxContext_InitDefaultRenderStateRecord` (confirmed as the constructor for
the exact canned state block `GfxContext_ApplyDefaultRenderStateBatch`
reads).

Checked the 4 remaining `PixelBlit_ValidateAlignmentAndDispatch` format
handlers too — confirmed genuinely not worth naming: heavily register-
optimized (`unaff_EBX`/`unaff_EBP`/etc.), the same unreliable-parameter-
routing situation this project has correctly left alone before (`FUN_00106930`).
17 renames total this pass.

**Update (2026-07-21): named all 5 handlers after all.** Re-examined them —
the register-optimization made exact pixel-format semantics unrecoverable
(as noted above, correctly), but the *shared structural shape* is clear and
worth naming even without the exact algorithm: each is a row-blit loop over
`in_stack_0x3c` rows calling a per-row pixel converter, with formats 2/3,
4/5, and 6/7 sharing single handlers (matching the dispatch table's
duplicate slots) and formats 2-7 all XORing a pair of dither-pattern
globals (`DAT_001fd2b8/bc` against `DAT_001fd2a8/ac`) each row — a genuine
ordered-dithering accumulator. Format 6/7's handler is the most complex,
processing 2 output rows per pass with 2 alternating scratch line buffers.
Named `PixelBlit_ConvertRowsFormat0`/`Format1`/`Format2And3`/`Format4And5`/
`Format6And7Interlaced`, all explicitly hedged as "structural confidence,
algorithm not fully traced" in the script comments — the exact per-format
pixel math (likely a texture bit-depth/palette down-conversion library)
remains unrecovered, but the row-loop shape and dithering behavior are now
documented rather than left as 5 bare `FUN_` addresses. Also re-checked the
previously-flagged "0x00149921's function body overlaps the other 4
handlers" boundary bug and found it **no longer present** — all 5 bodies
are clean and contiguous now, likely fixed by a later auto-analysis pass in
an intervening session. 5 renames.

**GfxContext vtable status**: down to a small handful of genuinely stuck
singles (`FUN_000fb080`, `FUN_000fb3f0`, `FUN_000fb560`, `FUN_000f98e0`,
`FUN_00104760`, `FUN_001033d0`) — real diminishing returns, thread
functionally closed for now.

## Closed: the "shader-table management" mystery (2026-07-20)

Answered a genuinely old open item -- the `this+0xc0` shader-handle pointer
`SpriteBatch_FlushAndPresent` reads was never traced to its owning class.
Turned out to require raw disassembly, not decompiled C: both
`ErrorScreen_RenderDiscReadError` and its caller take an implicit `this`
parameter (`MOV ESI,ECX` / `MOV ECX, <global>` at entry) that Ghidra's
decompiler completely hid from their C signatures, showing them as
parameterless. The real caller sat in a never-analyzed region -- found its
boundary via the same backward RET+NOP-padding scan technique used earlier
for `SceneRenderer_RenderFrame`.

**Result: there is no general shader-table management class.** The chain is
entirely disc-error-screen-specific:

- **`ErrorScreen_TriggerDiscErrorFreeze`** (`0x000b2430`) -- checks a global
  disc-error flag (`DAT_001e3dd8`); if set, loads the `"ABORTFONT"` tagged
  font resource, sets up **`ErrorScreen_Context`** (the renamed
  `DAT_001ba6e0` global), and calls `ErrorScreen_RenderDiscReadError` in an
  unconditional infinite loop -- the classic Xbox retail "freeze until
  hardware reset" disc-error behavior.
- **`ErrorScreen_ParseGlyphTable`** (`0x000b22f0`) -- tag-confirmed
  (`"Excpt Glyph"`) -- parses glyph metrics out of the loaded font resource,
  the same shape `Font_GetGlyphMetrics`/`Text_RenderGlyphString` use
  elsewhere.
- **`ErrorScreen_BuildFontTextureAndShaders`** (`0x000b1840`) -- computes a
  power-of-2 texture atlas size, allocates a `"FontTemp"`-tagged scratch
  buffer, and rasterizes the glyph bitmaps -- the step that ultimately
  populates `ErrorScreen_Context+0xc0` with the font/shader resource
  `SpriteBatch_FlushAndPresent` reads.

So the "shader table" was never a shared rendering resource -- it's this one
fallback screen's own dedicated emergency font, entirely self-contained,
consistent with the disc-error path needing to work even if the main
rendering/font systems aren't fully up. 3 function renames + 1 data rename.
