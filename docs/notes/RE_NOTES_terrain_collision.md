# Terrain collision system — RE Notes

The core snowboarding mechanic: how the rider collides with and rides along the
terrain surface. Found by tracing the terrain-grid singleton `TerrainGrid`
(`DAT_001faf8c`) outward from the already-named `Rider_ComputeTerrainCellIndex`.

## The `TerrainGrid` singleton (`0x001faf8c`)

A 2D spatial grid that accelerates terrain-surface queries — the broad-phase
for terrain collision. Layout confirmed from the query code:

| Offset | Field |
|--------|-------|
| `+0x2c` | grid width (columns) |
| `+0x30` | grid height (rows) |
| `+0x48` | sub-object count per cell |
| `+0x4c` | pointer to the cell array (each entry → a cell's terrain object, 4-byte stride, `width*row + col` indexing) |

`Rider_ComputeTerrainCellIndex` maps a rider position to a single cell index
(`width*row + col`, clamped); the fuller queries below select a *range* of
cells covering an AABB.

## Query functions

- **`Terrain_QuerySurfaceContact`** (`0x0013dc00`) — the core collision query.
  Grid-accelerated broad-to-narrow phase:
  1. `Terrain_GetCellRangeForBounds` → the min/max cell col/row covering the
     query position expanded by a radius.
  2. For each cell in range: `Collision_TestAABBOverlapWithMargin` (broad
     reject), then iterate the cell's terrain triangles/sub-objects, testing
     each with heavy SSE triangle math for the actual surface contact.
  Returns a hit flag; writes the contact point/normal to an output param.
  Called from rider physics (`Rider_UpdateTerrainContact`, and `FUN_000287a0`).
- **`Terrain_SampleHeightAt`** (`0x0013f480`) — samples the ground height
  (returns `float10`) at a position, reading the same `TerrainGrid` cells.
  The "how high is the ground here" query.
- **`Terrain_GetCellRangeForBounds`** (`0x00030380`) — computes the min/max
  grid cell range covering a position expanded by a radius, clamped to grid
  bounds. The broad-phase cell selector.
- **`Collision_TestAABBOverlapWithMargin`** (`0x0013db10`) — generic
  AABB-vs-point-with-scalar-margin overlap test on all 3 axes (returns 1 on
  overlap). The reusable overlap primitive of the narrow phase.

## Rider-side contact tracking

The rider maintains a per-frame set of terrain cells it currently overlaps
(a fixed-size list at `rider+0xc`, a `0x1b0`-byte tracking buffer at
`rider+0x36c`):

- **`Rider_UpdateTerrainContact`** (`0x00037e70`) — rider physics; calls
  `Terrain_QuerySurfaceContact` and resolves the contact point/normal (SSE
  vector math) that drives the board-on-snow physics.
- **`Rider_CollectTerrainCellsInBounds`** (`0x000306a0`) — iterates every
  `TerrainGrid` cell within the rider's bounds and marks each active.
- **`Rider_MarkTerrainCellTouched`** (`0x00030560`) — adds/marks one cell in
  the rider's active-contact-cell list (touched flag / free-slot claim).
- **`Rider_RebuildTerrainContactSet`** (`0x000305e0`) — rebuilds the active
  set each frame (zeroes the tracking buffer, re-resolves touched cells).

## The rider physics-mode state machine (consumer of the terrain contact)

`Rider_DispatchPhysicsMode` (`0x00031370`) is the per-frame top of the rider
physics state machine — a switch on the rider's physics-state field
(`rider+0x484`) selecting one of 6 movement/collision models:

| Mode (rider+0x484) | Function | Terrain? |
|--------------------|----------|----------|
| 1 | `Rider_PhysicsMode1_GroundRide` (`0x00025f90`) | **yes** — `Terrain_SampleHeightAt` + `Rider_UpdateTerrainContact` (fullest; the normal riding/carving state) |
| 2 | `Rider_PhysicsMode2_GroundContact` (`0x000276a0`) | **yes** — `Rider_UpdateTerrainContact` only |
| 3 | `Rider_ResolveTerrainContactPhysics` (`0x000287a0`) | **yes** — the collision-response core (contact normal + velocity damping/friction) |
| 4 | `Node_NoOpStub1` | no-op (inactive) |
| 5 | `Rider_PhysicsMode5_NoTerrain` (`0x0002ba50`) | no — non-ground state (airborne / rail / wall) |
| 6 | `Rider_PhysicsMode6_NoTerrain` (`0x0002a920`) | no — non-ground state |

Cases 1-3 all interact with `TerrainGrid` (confirmed ground states); cases 5-6
don't (their exact identity — airborne vs. rail-grind vs. wall-ride — isn't
resolvable from the bodies alone and is left un-claimed). Mode 3
(`Rider_ResolveTerrainContactPhysics`) is where the board-on-snow contact is
resolved: it reads the rider velocity/position, queries the terrain, and
projects/damps the velocity against the surface normal — the physics that makes
the rider follow the slope.

## How it fits together

`Rider_ComputeTerrainCellIndex` (single cell) and
`Rider_CollectTerrainCellsInBounds` (cell range) locate the terrain near the
rider; `Rider_UpdateTerrainContact` calls `Terrain_QuerySurfaceContact` to get
the precise surface contact; `Terrain_SampleHeightAt` answers height queries
for physics/camera. This is the classic grid-broad-phase + triangle-narrow-phase
terrain collision design, spatially indexed by `TerrainGrid`.

## The level-asset loader — where the terrain data comes from

Chasing the `TerrainGrid` builder led to **`Level_LoadTrackAssets`**
(`0x00142e10`), called from both `FEInit_Boot` and `InGameState_LoadLevel`.
It resolves the current track's asset-name code (**`TrackInfo_GetRecordByIndex`**,
a 0x84-byte-stride per-track table; default `ssxfe` for the frontend), then
builds `|data/models/<track><suffix>` paths for the track's full asset set and
loads each. The suffixes name the track file types:

| Suffix | Content |
|--------|---------|
| `.aip` / `.sop` / `.adl` | geometry / AI-path sub-types |
| **`.ltg`** | **level terrain geometry — the likely collision-mesh source that populates `TerrainGrid`** |
| `.xbd` | Xbox model data |
| `.xsf` | level script (already decoded — see `RE_NOTES_archive_format_decoded.md`) |
| `.xsh` | shader / texture atlas (already known) |

Path variants switch on `GameMode_Current` (Race/Showoff). So the terrain
collision grid is built from the per-track **`.ltg`** file loaded here — a
concrete new lead tying the collision system to the on-disc asset set.

Also named **`Terrain_UpdateObjectVisibility`** (`0x001421b0`) — a per-frame
pass that builds each terrain object's AABB, runs the graphics-device
visibility test (device vtable+0x158), and marks the object visible/culled
(0xffff), cross-referencing `TerrainGrid` sub-objects. Terrain render culling,
distinct from the collision path.

## The `.ltg` file format — DECODED (end-to-end confirmation)

Extracted `gari.ltg` from `Game Data\data\models\gari.big` (via
`scripts\extract_big.py`) and decoded it. **The `.ltg` header maps directly
onto the runtime `TerrainGrid` struct** — the field offsets match *exactly*,
a strong cross-confirmation that the file is loaded (nearly) verbatim into the
singleton:

| `.ltg` offset | value (gari) | runtime field |
|---------------|-------------|---------------|
| `+0x2c` | 23 | `TerrainGrid+0x2c` (grid width / columns) |
| `+0x30` | 31 | `TerrainGrid+0x30` (grid height / rows) |
| `+0x48` | 16 | `TerrainGrid+0x48` (sub-objects per cell) |

Full header (0x4c bytes): `+0x00` size marker; `+0x04..0x0c` world bbox min;
`+0x10..0x18` world bbox max; `+0x1c..0x24` secondary extent; `+0x28` cell
size (10000.0 world units); `+0x2c` width (23); `+0x30` height (31); `+0x34`
cell count (713 = 23×31); `+0x38`/`+0x3c` tri/vertex counts; `+0x40` sub-cell
size (2500 = 10000/4); `+0x44` sub-division (4); `+0x48` sub-objs/cell (16).
Then a **713-entry cell offset table** (u32 each, relative to data base
`0x4c + w*h*4`; 0 = empty cell), followed by per-cell collision data.

**Per-cell triangle records — decoded.** Each cell is a sequence of **0x4c
(76) byte collision-triangle records**:

| Offset | Field |
|--------|-------|
| `+0x00` | u32 surface/material id (steps by 8 between adjacent patches) |
| `+0x04` | u32 (0) |
| `+0x08` | f32[3] vertex 0 (world x,y,z) |
| `+0x14` | f32[3] vertex 1 |
| `+0x20` | f32[3] vertex 2 |
| `+0x2c` | f32[2] (0 — padding/extra) |
| `+0x34` | u32 flags (observed `0x20000`) |
| `+0x38..+0x48` | (mostly 0 — normal / adjacency, not fully resolved) |

Confirmed by dumping the first cell: the triangle vertices tile the **2500-unit
sub-cell grid** (`cellsize/subdiv` = 10000/4) exactly — e.g. successive tris
step `(-20000,-30000)→(-17500,-30000)→(-15000,-30000)` in X, matching the grid.
This is a regular triangulated heightfield patch per sub-cell, with a per-patch
surface-material id (the field that drives snow-vs-ice-vs-rail surface physics
and particle types). Dump via `classify_ltg.py <file> tris N`.

Only **243 of 713 cells (34%)** are populated — the cells the track path
crosses. The decoder (`scripts\classify_ltg.py`) renders an ASCII map of the
populated cells that literally traces the Garibaldi run diagonally down the
grid. This closes the terrain-collision loop **end to end**: per-track `.ltg`
file → loaded by `Level_LoadTrackAssets` → header/grid into `TerrainGrid` →
queried by `Terrain_QuerySurfaceContact`/`SampleHeightAt` during rider physics.

Tool: **`scripts\classify_ltg.py`** (`python3 classify_ltg.py <file.ltg>`).

## Physics-mode state transitions — investigated, not resolved

Tried to trace what writes the physics-mode selector `Rider_DispatchPhysicsMode`
reads. Findings, all verified but not conclusive enough to rename anything
further:

- **The selector address is a computed indirection**, not a flat
  `rider+0x484`: `*(int*)(*(int*)(*(int*)(rider+0x30)+4) + 0x484 + rider)`.
  This matches the shape of this project's already-documented "component"
  architecture (`Component_UpdateAll` at `0x00060a90` iterates a circular
  linked list of component objects, calling each one's own vtable+0xc) —
  `rider+0x30` plausibly points at the physics component, whose `+4` field is
  a byte-offset added back into the rider to reach the mode field. Not
  independently confirmed.
- **`Rider_PhysicsMode5_NoTerrain` checks `RiderEvent_GetSubState()` against
  codes `0x2dc`/`0x2dd`** — a genuine, verified lead toward identifying that
  mode (plausibly rail-grind or a similar non-ground sub-state), but those
  event-code values aren't catalogued anywhere in
  `RE_NOTES_rider_event_system.md` (which explicitly notes the per-code
  semantic vocabulary was never exhaustively catalogued). Left un-named per
  the project's don't-guess discipline.
- **A naming error — investigated and CORRECTED (next session).** The
  function that calls `Rider_DispatchPhysicsMode` was mis-named
  `Rider_TeardownSubobjects` (`0x00036990`) by an earlier session. Reading its
  full body found **zero `Heap_Free`/`Pool_FreeSlot`/destructor calls** —
  instead 233 lines of dt-scaled per-frame velocity/physics math, HUD time-gap
  callouts, a `RiderEvent_DispatchTypeB` call, and a `TerrainGrid` read. The
  full resolution (verified live):
  - **The earlier session's premise was wrong.** `Rider_ScalarDeletingDestructor`
    (`0x36920`, the genuine destructor) does **not** call `0x36990` at all — it
    calls `Rider_DestructUnlinkFromRegistry` (`0x36970`, node-registry unlink)
    and the **real** `Rider_TeardownSubobjects` (`0x31f10`, which installs
    `Node_NullSubObject_Destructor` and tears down sub-objects). Independently
    confirmed: `Player_ScalarDeletingDestructor` (`0x5c0c0`) also calls
    `Rider_TeardownSubobjects` (`0x31f10`).
  - `0x36990` is reached **only** via the `0x370f0` adjustor thunk (Rider vtable
    `0x1886e4` slot 1) and is shared as a vtable slot across ~28 component
    classes (`0x00187c8c` family). It is the **per-frame physics/state update**,
    now renamed **`Rider_UpdatePhysicsState`**.
  - The two "BaseDestructor" functions that reach it (`0x370f0` Rider,
    `0x5be60` Player) were the same mislabel — they're the slot-1 **Update**
    overrides, renamed `Rider_UpdatePhysicsState_Thunk` and
    `Player_UpdatePhysicsState` (Player's calls the shared update then a
    game-phase-gated extra step; no free logic).
  - Net: 2 new correct names (`0x31f10`, `0x36970`), 3 corrected names, old
    entries preserved commented-out in `ssx_auto_rename.py` per convention.
    This is the rare case where the earlier "verified" claim was itself the
    error — the fix was made only after reading every function's actual body.

## The rider physics-component vtable (0x00187c8c family) — mapped

Following up on the correction above: the ~28-entry data table `0x36990`
turned out to sit in is one **shared physics-component vtable**, embedded
once per rider slot. Confirmed by comparing the first 6 dwords across 6
different table entries spanning the whole range — **byte-identical function
pointers in every entry**; only the trailing tuning-constant data differs per
slot. So this is a single small vtable, laid out repeatedly (one embedded
copy per rider/AI slot), not per-character overrides. Slots:

| Slot | Function | Role |
|------|----------|------|
| 0 | `Rider_UpdatePhysicsState` (already named) | per-frame physics/state update |
| 1 | `Rider_ResetPhysicsState` | construct/reset — zeroes ~20 physics fields incl. the state bitmask, fills an identity-matrix array |
| 2 | `Node_NoOpStub2` (shared) | — |
| 3 | `Rider_HandleComponentStateEvent` | event-code dispatcher (codes 0-0x18), edge-triggered on a prev/current state bitmask pair |
| 4 | thin forwarder (`0x311d0` → `FUN_00044160`) | not named — generic null-safe call forwarder, low value |
| 5 | `Node_NoOpStub2` (shared) | — |

**`Rider_HandleComponentStateEvent` is confirmed as the exact function
`Component_UpdateAll` calls** (vtable+0xc = dword slot 3, byte offset 0xc —
matches precisely) — closing the loop from `Rider_UpdatePhysicsComponents`
(loops `Component_UpdateAll` 3×) down to the actual per-component event
handler. The individual event codes' meanings aren't resolved (calls out to
~6 further unread handler functions).

## `Rider_HandleComponentStateEvent`'s handler cluster — investigated, deliberately not named

Traced the handlers `Rider_HandleComponentStateEvent` calls for event codes
`0xc`/`0x13`/`0x14`/`0x15` (`FUN_00125b10`/`FUN_00125830`/`FUN_00125c70`/
`FUN_00125ce0`), hoping to identify physics modes 5/6 or close another gap.
Found a real, self-contained pattern — each checks a "currently-focused rider"
condition (`FUN_00111940(0xffffffff)`), a replay/mode gate
(`FUN_001107c0`), then rolls a probability against a per-category threshold
table via `FUN_001238b0` (`RNG_NextGlobalUInt32() & 0x3ff` vs. a table entry)
before posting a numbered message (`0x1002051`/`0x1002053`-style IDs) through
a generic priority-queue message-post function (`FUN_00158630`).

**Initial hypothesis (announcer/commentary system) — investigated and ruled
out.** The threshold table sits immediately after a `"WorldTriggerManager"`
tag string and a `"data/config/nascript.inf"` path, which looked like strong
corroboration. But `WorldTriggerManager` is **already fully documented**
(`RE_NOTES_application_boot.md`) as the trick-action-to-sound-cue system
(`WorldTriggerManager_Update` → `WorldTriggerInstance_ProcessActionCode` →
`Trick_ResolveSoundCueID`, all keyed on the singleton `DAT_001f8908`) —
checked whether `FUN_00158630` (the message-post function these handlers
call) touches that singleton or any `WorldTriggerManager_*` function
anywhere in its body: **it doesn't**. It's a self-contained system using
entirely separate global state (`DAT_00201928`, `DAT_00203exx` family) — a
generic priority-sorted message/slot allocator, not obviously tied to
`WorldTriggerManager` despite the address-range proximity and the adjacent
tag string (which may just be unrelated data sharing the same memory page).

**Deliberately not renamed.** Whether this cluster is commentary, a HUD
notification system, an achievement/unlock trigger, or something else isn't
resolved by what's been read. Renaming it "Commentary_*" without that
confirmation would be a guess dressed as a finding — left as `FUN_`-prefixed
for a future session with a clearer angle (e.g. tracing what
`0x1002051`/`0x1002053`-style message-type IDs mean, or finding what actually
*consumes* messages posted through `FUN_00158630`).

## Next steps (not yet done)

- ~~Per-cell triangle encoding~~ **DECODED** (see above — 76-byte records,
  surface id + 3 float3 vertices, tiling the sub-cell grid).
- The **surface-material id** semantics: which id ranges mean snow / ice /
  rail / etc. Cross-reference with the rider surface-physics code (which reads
  the contact triangle's material and picks friction/particle behavior) would
  resolve it — a concrete link between the decoded `.ltg` and gameplay.
- The `+0x38..+0x48` per-triangle tail fields (likely face normal +
  edge-adjacency for collision resolution) not fully resolved.
- The exact runtime function that parses `.ltg` bytes into the live
  `TerrainGrid` cell array (`Level_LoadTrackAssets` loads the file; the
  header-copy/cell-pointer-fixup step isn't isolated).
- ~~`FUN_000287a0` (the second `Terrain_QuerySurfaceContact` caller)~~
  **STALE NOTE, ALREADY DONE in an earlier session** — this was renamed
  `Rider_ResolveTerrainContactPhysics` (physics mode 3) with a full
  structural-confidence comment; this bullet just hadn't been updated to
  match. Corrected here.
- `DAT_001a7b7c` — the scalar margin/radius passed to the AABB tests
  (collision epsilon), not yet identified.

## Surface-material lead: picked back up (2026-07-22, port-priority pass)

Per an explicit user request to prioritize core-gameplay/port-viability
work over frontend screens, came back to the highest-value open item from
this file: **which surface-material ids mean snow/ice/rail**, and how they
reach the rider's friction/particle behavior. Read
`Rider_ResolveTerrainContactPhysics` (`0x287a0`) and
`Terrain_QuerySurfaceContact` (`0x13dc00`) in full (both dense SSE vector/
Bezier-curve math — the collision system evaluates each terrain edge as a
**cubic Bezier curve**, not a straight segment, with an iterative
closest-point refinement loop, considerably more sophisticated than the
`.ltg` format notes above initially suggested).

**Found the exact write site of the "surface" value the rider caches**:
`Terrain_QuerySurfaceContact` writes `*(param_3+0x34) =
*(int)((int)local_1a0+0x24)`, and the caller
(`Rider_ResolveTerrainContactPhysics`) copies that same output-buffer slot
(`local_3c`, exactly `0x34` bytes into the same buffer — confirmed by
counting the stack-frame offsets, not guessed) into
`*(rider_component+0x2cc)`. This is a genuine, concrete finding: `rider+0x2cc`
(relative to the physics-component base, not the raw `Rider` object) is the
**live "current contact surface" cache**, refreshed every time
`Rider_ResolveTerrainContactPhysics` gets a hit.

**Correction to the assumed `.ltg` record layout**: the value copied is
**not** the 76-byte triangle record's own leading material-id dword (as the
format spec above implies). It's read from `pfVar1[0x16]` (byte offset
`0x58` within the referenced structure) `+0x24` — and `0x58` alone already
exceeds the documented 76-byte (`0x4c`) triangle-record size, so `pfVar1`
here must be a **larger enclosing structure** (a collision patch/quad
containing multiple triangle sub-elements, matching the surrounding code's
own per-triangle-loop-within-a-patch shape), not a single triangle record.
This means the real "surface material" lives on a **shared per-patch
object**, not per individual triangle — a materially different (and more
sensible, engine-wise) architecture than originally assumed: many
triangles likely share one patch-level surface/material descriptor rather
than each carrying its own copy.

**Still open from that pass**: the referenced patch object's own type/
layout (what `pfVar1[0x16]` actually points to), and the exact consumer of
the cached value — not found in that pass (checked
`Rider_UpdateSurfaceFrictionCueFX`/`FUN_0003ebd0`, neither read it).

## Immediate follow-up: found the consumer, and it revises the hypothesis

Rather than a blind binary-wide byte search (this project has learned to
distrust single scattered hits for small displacements like `0x2cc`),
searched for the raw `0x2cc` displacement bytes and specifically looked for
**clustering** — multiple hits inside one small function is a much stronger
signal than isolated ones. Found exactly that: 4 references inside one
231-byte, previously-unnamed function.

**`Rider_ComputeSurfaceCompressionResponse`** (was `FUN_00026da0`, sole
caller: `Rider_PhysicsMode2_GroundContact`) — **this revises the working
hypothesis from the immediately preceding update**. `rider_component+0x2cc`
and `+0x2d0` are **not** a discrete surface-material id/enum as first
suspected — they're a pair of continuous **float thresholds** (a min/max
compression range) read from the terrain collision-patch object, driving a
spring/damping-shaped response curve (`(1.0 - (2*depth)/(max-min)) *
velocity` — a classic suspension curve) as the surface gives under the
board. **This means each terrain patch's "material" is really a continuous
give/compression range, not a categorical snow/ice/rail id** — a more
physically real simulation than assumed, and arguably a more interesting
target for a faithful port than a simple enum would have been.

The same targeted search found 2 more genuine readers (same addressing
shape, not coincidental):

- **`Rider_PhysicsMode6_NoTerrain`** reads `+0x2cc` while airborne — likely
  carrying the last-known-surface compression value forward, plausibly for
  landing-impact prediction.
- **`RiderEvent_ProcessInputWithComponentDecay`** (already named) checks it
  as an int-zero test — plausibly an implicit "is currently on a valid
  surface" guard reusing the same cached field rather than a separate flag.

**This substantially closes the original question** this file has been
chasing since the `.ltg` decode: the answer to "what does surface material
affect" turned out to be "board compression/give response," not "friction
category" as originally framed — a genuine correction, not just an
elaboration. 1 rename this pass.

### Still open

- The collision-patch object's own type/layout (what `pfVar1[0x16]` in
  `Terrain_QuerySurfaceContact` points to) — not identified.
- Whether discrete surface *types* (snow/ice/rail) exist at all as a
  separate concept from this continuous compression range, or whether the
  compression range alone is how the game distinguishes them (e.g. ice ≈
  very stiff/low range, powder ≈ soft/high range) — plausible but not
  confirmed; would need real `.ltg` data cross-referenced against these
  two float fields' actual values per patch.
- `Rider_PhysicsMode6_NoTerrain`'s exact use of the carried-forward value
  — read but not traced to its effect.

## `TerrainNode` — the per-frame visibility/culling driver (2026-07-20)

Picked as a fresh direction (continuing the "map the untouched
`InGameState_LoadLevel` tagged objects" thread — `RE_NOTES_weather_effects.md`,
`RE_NOTES_tutorial_system.md`, `RE_NOTES_powerfx_particles.md` before this).
`"TerrainNode"` (`0xc` bytes, `NodeRegistry` type `0`) is the **fourth**
confirmed instance this session of the "`NodeBase`-derived `InGameState`
subsystem, shared no-op stubs at vtable slots 1/7, real logic in custom slots"
architecture — but this one plugs directly into the systems already documented
above.

**`TerrainNode_TickVisibilityUpdate`** (vtable slot 2, was `FUN_000de230`,
found in completely un-analyzed code) reads the current rider's position,
picks a visibility-radius constant based on race format (single- vs
multi-rider, from a level-config sub-object), then calls:

- **`TerrainNode_BuildVisibleCellList`** (was `FUN_001420b0`) — zeroes a
  scratch buffer and calls the already-named `Terrain_GetCellRangeForBounds`
  to find which grid cells fall within the visibility radius.
- The already-named **`Terrain_UpdateObjectVisibility`** directly.
- **`TerrainNode_UpdateTrackSegmentProps`** (was `FUN_000f70e0`, moderate
  confidence, 229 lines not traced field-by-field) — iterates track segments
  via the already-named `TrackSegment_GetByIndex` and processes flagged
  per-segment sub-items (decorations/props), reading as a track-side
  prop visibility/LOD pass. Has a second caller in an unanalyzed region
  (`~0x0007ccf4`), not chased further.

Same honest "no static per-frame caller found" status as `SnowFallMan`/
`LessonMan`/`PowerFX Particles` — `xrefs_to` on `TerrainNode_TickVisibilityUpdate`
shows only its own vtable-slot data reference.

**Correction to the "shared dispatcher" hypothesis** (raised after the second
instance, `RE_NOTES_tutorial_system.md`): checked `InGameState_LoadLevel`'s own
disassembly right at `TerrainNode`'s construction site, and its pointer is
**never stored anywhere** — unlike `SnowFallMan`/`LessonMan`/`PostAI`/`PREAI`,
which are all explicitly written to a field on the `InGameState` object
(`*(param_1+0x20)`, `+0x80`, `+0x34`, etc.) right after construction,
`TerrainNode`'s local variable is simply never assigned to any `InGameState`
field. It relies purely on `NodeRegistry` (type 0) for reachability. This
means the "shared `InGameState`-iterates-its-own-subsystem-pointers dispatcher"
theory does **not** hold for `TerrainNode` specifically — also checked
`InGameState`'s own vtable slots 2/3 (previously undocumented) hoping they'd
be the dispatcher: both are trivial, already-named stubs
(`InGameState_StubTiny`/`InGameState_StubTiny2`, a simple flag setter/getter
at `+0xc`), not it either. **The shared-dispatcher hypothesis is weaker than
previously stated** — these 4 systems may simply have 4 genuinely different
(and equally untraceable) invocation mechanisms, not one common one. Worth
keeping in mind rather than assuming a single unifying answer exists. 4
renames.

## `SkyNode` and `ModelsNode` — per-track skybox + track-side prop rendering (2026-07-20, "deep research" pass)

Continued the "map the untouched `InGameState_LoadLevel` tagged objects"
thread with patient, thorough tracing rather than a quick surface pass.

**`SkyNode`** (`0x10` bytes, `NodeRegistry` type `1`) is the per-track skybox
system. `SkyNode_Construct` builds a `"<track>_sky"` filename from the
already-named `TrackInfo_GetRecordByIndex` (the current track's own name),
resolves it via **`SkyNode_ResolveTrackSkyIndex`** against a fixed table of
12 known sky names, and loads the matching resource via
**`SkyNode_LoadSkyResourceForTrack`**. **`SkyNode_RenderSkyDome`** (vtable
slot 2, found in completely un-analyzed code) draws the sky dome centered on
the current rider's position — the standard "infinitely distant sky"
technique.

**A genuinely valuable side-effect**: reading `SkyNode_ResolveTrackSkyIndex`'s
12-entry table directly from memory (`0x001ba658`, string data at
`0x0019aa30`) gave the full list of sky-asset short-names:

> `alaska_sky`, `trick_sky`, `bigair_sky`, `megaple_sky`, `untrack_sky`,
> `pipe_sky`, `aloha_sky`, `merquer_sky`, `mesa_sky`, `elysium_sky`,
> `snow_sky`, `gari_sky`

This **cross-validates the already-documented `TrackTable` short-codes from a
completely independent angle** (`RE_NOTES_results_screen.md`): `gari` →
Garibaldi, `pipe` → Pipedream, `merquer` → Merqury City, `aloha` → Aloha Ice
Jam, `megaple` → Tokyo Megaplex, `bigair` → Big Air Dome, `alaska` → Alaska,
`untrack` → Untracked — all already-confirmed real track names, now
confirmed a second, independent way. `trick`/`mesa`/`elysium`/`snow` weren't
individually confirmed in the earlier `TrackTable` pass (rows 1-3/10 —
plausibly Mesablanca/Elysium Alps/a snow-themed track/possibly a non-track
special sky) — not asserted with certainty here, just a lead for whoever
checks those rows next.

**`ModelsNode`** (`0xc` bytes, `NodeRegistry` type `1` — shared with
`SkyNode`) turned out to be a genuine companion to `TerrainNode`: the actual
track-side prop/decoration **mesh draw** step. `ModelsNode_DrawTrackSegmentModels`
(reached via a thin wrapper, `ModelsNode_TickRenderTrackModels`) iterates
track segments via the already-named `TrackSegment_GetByIndex` with a loop
bound of `0xa2` (162) — **the exact same buffer size**
`TerrainNode_BuildVisibleCellList` allocates (`0xa2` dwords), confirming both
systems process the identical 162-segment cache. For each segment's flagged
sub-items with both a "visible" and "loaded" bit set, it calls the
`GfxContext` singleton's vtable`+0x168` (a mesh-draw-shaped call) — the real
draw call `TerrainNode_UpdateTrackSegmentProps`'s visibility/LOD pass feeds
into. Together they form the complete track-decoration rendering pipeline.

**Also closed, not new territory**: re-reading `InGameState_LoadLevel`'s
construction order for context found `"OverlayNode"` (constructed right
before `ModelsNode`/`SkyNode`) is built via the **already-named**
`OverlayManager_Construct(0xc)` — confirms `OverlayNode` is simply this
project's existing, fully-documented `OverlayManager` system under a
different tag name, not fresh territory. One less unknown to chase.

8 renames (5 `SkyNode` + 3 `ModelsNode`).

## CORRECTION (2026-07-27, M2 port pass): the 0x4c records are NOT triangles

Caught while porting the `.ltg` loader to C++ for milestone M2. The section
"Per-cell triangle records — decoded" above is **wrong** and is superseded by
this one. It was written from the file bytes alone; checking it against
`Terrain_QuerySurfaceContact` (`0x0013dc00`) and re-reading the data disproves
the triangle reading.

### What the runtime actually does
```c
iVar12 = *(int *)(*(int *)(grid + 0x4c) + (*(int *)(grid + 0x2c) * row + col) * 4); // cell
...
local_1cc = 0;
do {
    iVar14 = *(int *)(iVar12 + 0x38) + local_1c0;        // sub-object array, stride 0x4c
    if (((*(short *)(iVar14 + 0x2a) != 0 || *(short *)(iVar14 + 0x2c) != 0))
        || (*(int *)(iVar14 + 0x48) != 0)) {
        ...
        do { ... } while (local_1b8 < *(short *)(iVar14 + 0x2a));   // inner element loop
    }
    local_1c0 = local_1c0 + 0x4c;
    local_1cc = local_1cc + 1;
} while (local_1cc < *(int *)(TerrainGrid + 0x48));      // exactly 16 iterations
```
So the `0x4c` stride is the **sub-object** stride, and there are exactly
`TerrainGrid+0x48` (= 16 = `subdiv*subdiv`) of them per cell — one per sub-cell
of the 4x4 subdivision. Not a triangle list.

### The record is a sub-cell AABB, verified against gari.ltg
| Offset | Field |
|---|---|
| `+0x00` | u32 surface/material id (steps by 8 between adjacent patches) |
| `+0x04` | u32 (0) |
| `+0x08` | f32[3] **bbox MIN** |
| `+0x14` | f32[3] **bbox MAX** |
| `+0x20` | f32[3] **bbox CENTRE** |
| `+0x34` | u32 flags (`0x20000`, `0x30000` observed) |
| `+0x38`, `+0x48` | zero in-file; **filled in by the loader** at runtime |

Proof: for all 16 records of the first populated cell, `centre` is exactly
`midpoint(min, max)` and the span is exactly `2500 x 2500 x 0` — the sub-cell
size (`cellsize/subdiv` = 10000/4). The old note read `+0x08/+0x14/+0x20` as
"vertex 0/1/2" and mistook the 4x4 tiling of sub-cell **origins** for triangles
tiling the grid.

### Where the real geometry lives
`16 * 0x4c` = **1216** bytes of sub-object records per cell, but the gap between
consecutive cell offsets ranges **1428..11048** bytes — i.e. every cell carries
**212..9832 bytes of variable-size trailing data** after its 16 descriptors.
That trailing region is the actual per-sub-cell geometry, indexed at runtime
through the `+0x38`/`+0x48` pointers and counted by the `+0x2a`/`+0x2c` s16
fields — all four of which are **zero in the file**, so the loader builds them.

### Consequence for the port
The file format alone is not enough: `.ltg` cannot be ported from
`classify_ltg.py`, because that script never determined a per-cell element count
(it just dumps N records on request) and its record interpretation is the wrong
one. **The `.ltg` loader inside `Level_LoadTrackAssets` must be traced** to learn
how the trailing region is parsed into the runtime sub-object structure. That is
now the first real task of M2.

`scripts/classify_ltg.py` should be updated to print sub-cell AABBs rather than
"triangles" once the trailing format is known; it is left as-is for now so the
old output stays reproducible for comparison.

## M2a-pre: tracing the `.ltg` loader — progress and a hard blocker

Goal: learn how the per-cell trailing region becomes the runtime sub-object
structure, so the C++ `.ltg` loader can be written from code rather than guessed.

### The runtime sub-object layout, read out of `Terrain_QuerySurfaceContact`
```c
iVar14 = *(int *)(cell + 0x38) + local_1c0;        // sub-object, stride 0x4c
if (((*(short *)(iVar14 + 0x24) != 0) || (*(short *)(iVar14 + 0x26) != 0) ||
     (*(short *)(iVar14 + 0x28) != 0) || (*(short *)(iVar14 + 0x2a) != 0) ||
     (*(short *)(iVar14 + 0x2c) != 0)) || (*(int *)(iVar14 + 0x48) != 0)) {
    Collision_TestAABBOverlapWithMargin(iVar14, iVar14 + 0xc, pos, margin);
    ...
    do {
        pfVar1 = *(float **)(*(int *)(iVar14 + 0x3c) + local_1b8 * 4);   // ptr array
        ...
    } while (local_1b8 < *(short *)(iVar14 + 0x2a));
}
```

| Runtime offset | Meaning |
|---|---|
| `+0x00` / `+0x0c` | bbox min / max (the overlap test takes `sub`, `sub+0xc`) |
| `+0x24`,`+0x26`,`+0x28`,`+0x2a`,`+0x2c` | **five** s16 element counts |
| `+0x38` | pointer array, counted by `+0x28` (see the loader fixup) |
| `+0x3c` | pointer array, counted by `+0x2a` — the collision list |
| `+0x48` | further pointer (presence alone enables the sub-object) |

Each entry of the `+0x3c` array is a pointer to an object with its own bbox at
`+0x17`/`+0x1a` (float index), a sub-struct pointer at `[0x16]`, and a
material/flags byte at `+0x18` of that sub-struct — i.e. the terrain surface
patches, not bare triangles.

`Level_LoadTrackAssets`'s tail is a **post-load fixup** confirming the shape:
```c
iVar8 = *(int *)(cell + 0x38) + iVar6;             // sub-object, stride 0x4c
piVar11 = *(int **)(iVar8 + 0x38);                 // pointer array
while (iVar9 < *(short *)(iVar8 + 0x28)) {         // counted by +0x28
    *(uint *)(*piVar11 + 0x68) = ... | 0x1020000;  // set a flag on each target
}
```

### File layout != runtime layout — the loader rewrites
On disk the `0x4c` record has its bbox min at **`+0x08`**; at runtime the same
record has min at **`+0x00`**. The disk record's `+0x38`/`+0x48` (pointers) and
`+0x24..+0x2c` (counts) are all **zero**. So the loader does not memory-map the
file; it builds the runtime records.

### BLOCKER: nothing writes `TerrainGrid`
Exhaustive check — searched the whole image for the address bytes
`8c af 1f 00`: **all 22 occurrences are the known READ sites** (listed via
`xrefs_to`). No `A3`/`89 0d`/`89 15`/`89 05`/`89 35`/`89 1d`/`C7 05` store to
`0x001faf8c` exists anywhere. Disassembly confirms it is a genuine pointer
variable (`MOV EDX, dword ptr [0x001faf8c]`, not `MOV EDX, imm32`), and its
value in the static image is **0**.

The only consistent explanation: `TerrainGrid` is a **field inside a larger
global object**, written as `[base + disp]` with the base in a register — which
produces no absolute-address bytes and therefore no Ghidra data xref.

**Next step (do this first when resuming):** identify the containing global by
scanning for a global base `B` such that `B + disp == 0x001faf8c` for a
plausible `disp`, then find writes through `B`. Candidate approach: look at what
else lives immediately around `0x001faf8c` in `.data`/`.bss` and search for
stores to those neighbours, which will reveal the base register's origin.

Until then the `.ltg` runtime transformation is unknown and the C++ loader
cannot be written faithfully. `classify_ltg.py`'s file-level reading (header,
cell table, 16 sub-cell AABBs) is correct and reusable; the trailing-region
parse is not yet known.

## BLOCKER RESOLVED: `TerrainGrid` is a *field*, not a standalone global

The previous section's blocker ("nothing writes `TerrainGrid`") is now
explained, and it was a symbol artefact rather than a real mystery.

### The proof
`FUN_0013e650` (a world-object method, `__thiscall`):
```c
iVar10 = *(int *)(param_1 + 4);                       // <- this+4
iVar12 = *(int *)(iVar10 + 0x2c) + -1;                //    grid width
iVar12 = *(int *)(iVar10 + 0x30) + -1;                //    grid height
iVar12 = *(int *)(*(int *)(iVar10 + 0x4c) +           //    cell array
                  (iVar9 * *(int *)(iVar10 + 0x2c) + iVar13) * 4);
```
`this + 4` is indexed with exactly the grid fields. And the world object's
address is confirmed from `FEInit_Boot`'s call site:
```asm
6a ff              PUSH -1                ; param_2 = 0xffffffff
b9 88 af 1f 00     MOV  ECX, 0x001faf88   ; this
e8 2f 51 0c 00     CALL Level_LoadTrackAssets   (0x00142e10)
```
So **`TerrainGrid` (`0x001faf8c`) == `worldObject(0x001faf88) + 4`**. Ghidra
minted a `Label` for the field (`SourceType: USER_DEFINED` — an earlier session
named it), while the object base is only `DAT_001faf88`. Every write therefore
goes through `[reg+4]` and emits no absolute-address bytes, which is precisely
why the exhaustive byte search for stores came up empty. Not a mystery — a
naming artefact.

### The world object
A singleton at `0x001faf88`, `.bss` (all zeros in the static image, so
constructed at runtime). It is used as `this` in **60** places, every one of
them `MOV ECX, 0x001faf88` — it is never loaded into any other register. Its
methods include `Terrain_QuerySurfaceContact`, `Terrain_SampleHeightAt`,
`Terrain_GetCellRangeForBounds`, `Level_LoadTrackAssets`, the ScriptVM
entry points and `Script_PlayByName` — i.e. this is the **world/level
singleton**, and the terrain grid is one of its fields.

### The terrain builder is a virtual call
`Level_LoadTrackAssets` builds the `.ltg` path (the extension string
`0x001a7bd8 = ".ltg"` has exactly **one** reference in the whole binary, here),
loads it, and hands it to a virtual:
```c
uVar7 = FILE_loadpack(local_380, 0);              // the .ltg pack
iVar4 = FILE_loadpack(local_100, 0);              // the .xbd pack
(**(code **)(*param_1 + 0x14))(uVar7, iVar4);     // <- vtable slot 0x14
```
**`worldObj->vtable[0x14](ltgPack, xbdPack)` is the terrain/collision builder.**

### Also settled: `+0x4c` is fixed up, the file is not memory-mapped verbatim
The query does `*(int *)(*(int *)(grid + 0x4c) + idx * 4)` — an **extra
indirection**. If the file's cell-offset table (which begins at file `+0x4c`)
were used in place, the code would read `*(int *)(grid + 0x4c + idx*4)`. It does
not, so `+0x4c` holds a **pointer** to a relocated table. Combined with the
earlier finding that the on-disk sub-object bbox sits at `+0x08` but the runtime
one at `+0x00`, the loader definitively rebuilds rather than maps.

### Still open (next step)
Resolve vtable slot `0x14` concretely. The object is `.bss`, so its vtable is
installed at runtime, and no `MOV [0x001faf88], imm32` or
`MOV reg, 0x001faf88` (non-ECX) exists — the constructor must store it via
`MOV [ECX], imm32` inside a callee. Scanning the 60 `MOV ECX` sites for an
immediately-following `C7 01` store found none, so the constructor is reached
less directly. Two untried angles: (a) resolve the call target properly at each
of the 60 sites (the quick scan only took the first `E8` within 18 bytes, which
misfires where arguments are pushed in between); (b) find the vtable as a data
array by looking for a run of pointers into the `0x0013xxxx`/`0x0014xxxx` text
range, then check its slot `0x14`.

## THE `.ltg` LOAD CHAIN — fully decoded (2026-07-27, M2a-pre complete)

The whole chain from file to queryable grid, and a **second correction** to the
file format. Five functions newly identified/created and named.

### The world singleton's vtable
The object at `0x001faf88` uses vtable **`0x001a7ae8`** (found via the single
non-`MOV ECX` reference to the object, and confirmed by slot arithmetic):

| Slot | Address | Name given |
|---|---|---|
| `0x14` | `0x0013a7f0` | **`Terrain_BuildFromLtgAndXbd`** (created — was undefined) |
| `0x18` | `0x0013a910` | **`Ltg_RelocateHeaderPointers`** (created — was undefined) |
| `0x1c` | `0x0013a930` | **`Ltg_RelocateCellTable`** |
| `0x20` | `0x0013af90` | **`Ltg_FixupCellAndResolveIndices`** |
| `0x28` | `0x0013b130` | **`Resource_RelocateBlockPointers`** |

Both `create_function` calls were done **one at a time** after verifying the
boundary with `/read_bytes` (preceding `RET` + `90` padding), per the
prior-session lesson about fragmenting neighbours.

### Step 1 — `Terrain_BuildFromLtgAndXbd` (slot 0x14)
```c
if (ltgPack == 0) return 3;
if (xbdPack == 0) return 4;
if (4000 < *(int *)(xbdPack + 0x34)) return 6;
this[1] = ltgPack;      // this+4  == TerrainGrid
this[2] = xbdPack;      // this+8
if (vtable[0x28](...))  // Resource_RelocateBlockPointers
    if (vtable[0x18](...)) return 0;   // Ltg_RelocateHeaderPointers
```
**`TerrainGrid` IS the raw loaded `.ltg` buffer.** The original note's claim that
the header "maps directly onto the runtime struct" was right — it *is* the same
memory. It has a 20,000-byte stack frame (`MOV EAX,0x4e20; CALL __chkstk`).

### Step 2 — `Ltg_RelocateHeaderPointers` (slot 0x18)
```c
*(int *)(ltg + 0x4c) += ltg;    // cell-table offset -> pointer
*(int *)(ltg + 0x50) += ltg;    // cell-data-base offset -> pointer
return vtable[0x1c](ltg);
```

### CORRECTION #2 to the file format: the cell table is at `+0x54`, not `+0x4c`
`+0x4c` and `+0x50` are **pointer fields inside the header**, not the start of
the table. For `gari.ltg`:
* `+0x4c` = `0x54` -> the cell table
* `+0x50` = `0xb78` -> the cell data base
* and `0x54 + 713*4 = 0xb78` **exactly** — the table ends precisely where the
  data base begins. Arithmetically conclusive.

The earlier note (and `classify_ltg.py`) read the table starting at `+0x4c`,
i.e. shifted two entries early, and computed the data base as `0x4c + w*h*4`
= `0xb70` — 8 bytes low. Both are wrong; the correct values come from the two
header fields.

### Step 3 — `Ltg_RelocateCellTable` (slot 0x1c)
```c
n = *(int *)(ltg + 0x34);              // cell count (713)
p = *(int **)(ltg + 0x4c);             // relocated table
for (i = 0; i < n; i++)
    if (p[i] != 0) { p[i] += ltg; vtable[0x20](p[i]); }
```
Cell-table entries are **buffer-relative offsets**, relocated in place; zero
stays zero (empty cell).

### Step 4 — `Ltg_FixupCellAndResolveIndices` (slot 0x20)
Relocates the cell's own pointers (`+0x38` unconditionally, then `+0x3c`,
`+0x40`, `+0x44`, `+0x48`, `+0x4c`, `+0x50` if non-zero), then:
```c
p = cell + 0x54;  n = 0x10;
do { vtable[0x24](cell, p);  p += 0x4c; } while (--n);
```
**Sub-objects live at `cell + 0x54`, 16 of them, stride `0x4c`** — the exact
offset, which the file-only analysis never had.

Finally it converts six **index arrays into pointers**, each
`ptr = base + index * stride`, with the bases and limits read from
`this[2]` — **the `.xbd` pack**:

| cell ptr field | count field | stride | base | bound |
|---|---|---|---|---|
| `+0x3c` | `+0x24` | `0x2d0` | `xbd+0x50` | `xbd+0x08` |
| `+0x40` | `+0x26` | `0x90` | `xbd+0x5c` | `xbd+0x14` |
| `+0x50` | `+0x2e` | `0x70` | `xbd+0x60` | `xbd+0x18` |
| `+0x44` | `+0x28` | `0x80` | `xbd+0x74` | `xbd+0x2c` |
| `+0x48` | `+0x2a` | `0x5c` | `xbd+0x6c` | `xbd+0x24` |
| `+0x4c` | `+0x2c` | `0x5c` | `xbd+0x6c` | `xbd+0x24` |

Out-of-range indices become **null**, not clamped.

### The big structural conclusion
**The `.ltg` contains no geometry.** It is a spatial index: grid header,
cell table, per-cell AABBs, 16 sub-cell AABBs per cell, and *index lists*. The
actual surfaces live in the **`.xbd`**, in tables of stride `0x2d0`, `0x90`,
`0x70`, `0x80` and `0x5c`. That is why the query's inner loop dereferences
pointers to objects carrying their own bboxes and material bytes.

**Consequence for M2**: `.ltg` alone is not enough for collision — the `.xbd`
record tables are required too, which pulls part of the M4 `.xbd` work forward
into M2. This is a genuine, code-derived scope change, not a guess.

### Remaining in this chain
Vtable slot `0x24` (the per-sub-object fixup, called 16x per cell) has not been
read yet; it will give the sub-object's own pointer/count fields, matching the
`+0x24..+0x2c` / `+0x38`/`+0x3c`/`+0x48` set the query uses.

## `.ltg` FORMAT — FINAL, verified end to end (M2a-pre complete)

Slot `0x24` = `0x0013a970`, created (clean boundary: `POP EBX; RET 4` then `90`
padding) and named **`Ltg_RelocateSubObjectPointers`**:
```c
// called as vtable[0x24](cell, subObj), 16x per cell
if (sub->0x34) sub->0x34 += cell;     // NOTE: relative to the CELL, not the buffer
if (sub->0x38) sub->0x38 += cell;
if (sub->0x3c) sub->0x3c += cell;
if (sub->0x40) sub->0x40 += cell;
if (sub->0x44) sub->0x44 += cell;
if (sub->0x48) sub->0x48 += cell;
```
**Two different relocation bases**: cell-table entries are *buffer*-relative,
sub-object pointers are *cell*-relative.

### The complete layout, verified against `gari.ltg`

**Header (grid)** — this buffer *is* `TerrainGrid`:
| Offset | Field | gari |
|---|---|---|
| `+0x04..0x18` | world bbox min/max | |
| `+0x28` | cell size | 10000.0 |
| `+0x2c` / `+0x30` | grid width / height | 23 / 31 |
| `+0x34` | cell count | 713 |
| `+0x40` / `+0x44` | sub-cell size / subdiv | 2500.0 / 4 |
| `+0x48` | sub-objects per cell | 16 |
| `+0x4c` | **-> cell table** (buffer-relative) | `0x54` |
| `+0x50` | **-> cell data base** | `0xb78` |

`0x54 + 713*4 = 0xb78` exactly.

**Cell table**: `713` u32, buffer-relative offsets; `0` = empty cell (243/713
populated in gari).

**Cell (0x54-byte header)**:
| Offset | Field | first cell |
|---|---|---|
| `+0x00` / `+0x0c` / `+0x18` | bbox min / max / centre | span **10000x10000** = cell size |
| `+0x24`,`+0x26`,`+0x28`,`+0x2a`,`+0x2c`,`+0x2e` | s16 counts | `+0x2c`=36 |
| `+0x38` | **-> sub-object array** (cell-relative) | **`0x54`** |
| `+0x3c`,`+0x40`,`+0x44`,`+0x48`,`+0x4c`,`+0x50` | index arrays -> `.xbd` records | `+0x4c`=`0x514` |

The `+0x38` value being exactly `0x54` independently confirms the sub-objects
start immediately after the cell header.

**Sub-objects**: **16** at `cell+0x54`, stride `0x4c`, tiling the 4x4 sub-cell
grid — each bbox spans exactly **2500x2500** = `cellsize/subdiv`. Same shape as
the cell: bbox min/max/centre, five s16 counts at `+0x24..+0x2c`, pointer fields
at `+0x34..+0x48`.

### Everything the earlier notes got wrong, now corrected
1. The `0x4c` records are **sub-cell descriptors**, not triangles.
2. `+0x08/+0x14/+0x20` are **bbox min/max/centre**, not vertices 0/1/2.
3. The cell table is at header field `+0x4c`'s *value* (`0x54`), not at `+0x4c`.
4. The data base is header `+0x50` (`0xb78`), not `0x4c + w*h*4` (`0xb70`).
5. The `.ltg` holds **no geometry** — only the spatial index; surfaces live in
   the `.xbd`, reached by index-to-pointer conversion.

`scripts/classify_ltg.py` is now known-wrong on points 1-4 and should be
rewritten against this section before being trusted again.

## `.ltg` C++ loader written and validated across all 10 tracks (M2a done)

`port/src/assets/ltg.{h,cpp}`, exercised by `port/tests/asset_test.exe`.

### CORRECTION to the section above
That section says each cell's bbox "spans exactly one cell size" and each
sub-object's "exactly 2500x2500". **That is wrong** — it was generalised from
the first populated cell of `gari.ltg`. Measured over all 243 populated cells:
only **32** span a full 10000x10000; the rest are **tight bounds around actual
content**, with Z extents up to 31539, and only 40 of 243 stay inside their own
nominal grid column (content legitimately overhangs neighbours, which is normal
for a spatial index). The first cell simply happened to be a default-span one.

### The invariants that DO hold — verified on every track
| Track | grid | populated | `tableEnd==dataBase` | `cell+0x38 != 0x54` | `min>max` | `centre != midpoint` |
|---|---|---|---|---|---|---|
| alaska | 35x18=630 | 150 | yes | 0 | 0 | 0 |
| aloha | 10x10=100 | 63 | yes | 0 | 0 | 0 |
| elysium | 22x22=484 | 211 | yes | 0 | 0 | 0 |
| gari | 23x31=713 | 243 | yes | 0 | 0 | 0 |
| megaple | 6x6=36 | 34 | yes | 0 | 0 | 0 |
| merquer | 19x32=608 | 191 | yes | 0 | 0 | 0 |
| mesa | 13x16=208 | 114 | yes | 0 | 0 | 0 |
| pipe | 4x12=48 | 46 | yes | 0 | 0 | 0 |
| snow | 17x17=289 | 136 | yes | 0 | 0 | 0 |
| untrack | 28x12=336 | 216 | yes | 0 | 0 | 0 |

All ten use `cellSize` 10000, `subdiv` 4, 16 sub-objects/cell. That is
**1404 populated cells / 22464 sub-objects** with zero violations, so the three
invariants are properties of the format, not of one file:
1. `header[+0x4c] + cellCount*4 == header[+0x50]` (table ends where data begins)
2. `cell[+0x38] == 0x54` (sub-objects immediately follow the cell header)
3. `centre == midpoint(min, max)` and `min <= max` everywhere

The loader asserts exactly these and no more.

**Note on `classify_ltg.py`**: still the old, wrong decode. The C++ reader is now
the reference for this format; the Python script needs rewriting or deleting.

## M2b: TerrainGrid queries ported (`port/src/game/terrain.{h,cpp}`)

Three primitives ported from live Ghidra (none are in `default.xbe.c`):

**`Collision_TestAABBOverlapWithMargin`** (`0x0013db10`) — note the asymmetry,
which is in the original and is preserved: the min side is **inclusive**
(`min <= p + margin`), the max side is **strict** (`p - margin < max`).

**`Terrain_GetCellRangeForBounds`** (`0x00030380`) — cell range around a point
expanded by a radius, clamped to `[0, dim-1]` using grid `+0x2c`/`+0x30`.

**`Rider_ComputeTerrainCellIndex`** (`0x00031880`) — the position->cell formula,
recovered from the disassembly because the decompiler dropped the float args:
```asm
FLD  [rider+0x174]   ; pos.y        FLD  [rider+0x170]  ; pos.x
FSUB [grid+0x08]     ; - worldMin.y FSUB [grid+0x04]    ; - worldMin.x
FADD [grid+0x28]     ; + cellSize   FADD [grid+0x28]
FDIV [grid+0x28]     ; / cellSize   FDIV [grid+0x28]
CALL ftol ; DEC                     CALL ftol ; DEC
```
so `idx = ftol((v - worldMin + cellSize) / cellSize) - 1`, index =
`width*row + col`. **The ground plane is XY and Z is up** — established here,
not assumed: the rider's grid inputs are `+0x170`/`+0x174` and the grid's
`worldMin.x`/`worldMin.y`.

### Bug found in this project's own Ghidra database
`0x0015ca68` was named **`CRT_RoundFloatToInt64`**. Its disassembly is the
classic MSVC `_ftol`: `FISTP` followed by a sign-based correction that adjusts
**toward zero** — C truncation, not rounding. (An earlier note had already
called it "ftol-style" and another described a caller using it "as a
floor-divide", but the name said Round.)

Caught by the port: implementing the cell formula with round-half-to-even made
the cell-index round-trip fail on **521 of 713** cells, because a cell centre
lands exactly on `.5` where half-to-even alternates. With truncation it is
**0 of 713**.

Renamed to **`CRT_ftol_TruncateToInt64`** in Ghidra, and the 10 references in
`scripts/ssx_auto_rename.py` plus 2 in the notes were updated and re-synced to
`ghidra_scripts`. This mattered beyond the terrain: any future port of a
function using this helper would have silently rounded instead of truncated.

### Verification (`asset_test`)
* cell-index round-trip over all 713 nominal cell centres: **0 mismatches**
* clamping: far-negative -> 0, far-positive -> 712 (both correct)
* broad phase: probing 243 populated cells returns candidates in 180 of them
  (318 candidate sub-objects total)

## `Rider_UpdateTerrainContact` (0x00037e70) decoded — it is the LANDING path

Read for M2c. The function is not the continuous ground-following code (that is
modes 1/2); it is **landing detection, classification and scoring**. Its logic
only became readable once the float globals were resolved:

| Symbol | Value | Meaning |
|---|---|---|
| `DAT_001a9f30` | 1.0 | one |
| `DAT_001a9f34` | 0.0 | zero |
| **`Const_NegOneF`** (`0x001a9f38`) | -1.0 | normal flip (renamed) |
| `DAT_00187510` | 80 | |
| **`Math_DegreesPerHalfTurn`** (`0x00187514`) | 180 | renamed |
| `DAT_001877c8` | 30 | landing-angle band |
| **`Rider_MinLandingSpeed`** (`0x001877d8`) | 555.556 | renamed |
| `DAT_001877f0` | 0.1 | Z scale on the landing vector |
| `DAT_00187844` | 150 | landing-angle band |
| `DAT_00187848` | -30 | landing-angle band / velocity step |
| `DAT_001875cc` | 0.9 | threshold scale |
| `DAT_001876e0` | 50 | |
| **`Math_PI`** (`0x00188850`) | 3.14159 | renamed |
| `DAT_00188868` | -0.2 | contact-approach threshold |

### What it does
1. Loads four `vec4`s from `rider+0x4870/0x4880/0x4890/0x48a0` — an orientation
   basis plus position — and calls `Terrain_QuerySurfaceContact`.
2. Flips the surface normal by `Const_NegOneF` if it faces away.
3. Forms `delta = (contactPoint - pos) + velocity * (-30)` and requires
   `dot(delta, normal) >= -0.2` to treat it as a landing.
4. Gates on component category `0xe/0xf/0x10` plus a category flag.
5. Compares three axis magnitudes against thresholds **lerped by `rider+0x2b0`**
   and scaled by `0.9` — e.g. `|x| < (t*150 + (1-t)*80) * 0.9`.
6. Clamps the landing speed to a minimum of `Rider_MinLandingSpeed` (555.556).
7. Normalises the landing vector into `rider+0x180` (vec4), scaling Z by `0.1`.
8. **Classifies the landing angle** via `Math_Atan2`, with the bands expressed
   in degrees through `Math_PI / Math_DegreesPerHalfTurn`:

| Condition (degrees) | `rider+0x208` |
|---|---|
| `-30 <= angle <= 30` | **1** — square/clean landing |
| `30 < angle <= 150` | **3** — off-axis one way |
| `angle < -30` | **4** — off-axis the other way |
| `|angle| > 150` | **2** — reversed (switch/fakie landing) |

9. Feeds the class into `TrickCombo_ScoreAirCompletion`, then
   `Rider_SetPhysicsMode(3)` (the contact-resolution mode) and
   `RiderEvent_SetState(0xf)`.

So `rider+0x208` is the **landing-quality/orientation class**, and it is a
scoring input — consistent with the game's Switch/Fakie landing concepts
documented from the manual. The ±30 deg "clean" band is an exact constant, not
an estimate.

### Port status
The constants above are now known, so this function is portable. It is *not*
what a physics sandbox needs first, though: M2c's minimum is modes 1/2
(continuous ground contact) plus `Rider_ResolveTerrainContactPhysics` (mode 3).
This one lands on top of those.

## `Rider_ResolveTerrainContactPhysics` (0x000287a0) — mode 3 constant set

514 decompiled lines, the board-on-snow contact response. Its 26 float globals
resolved:

| Symbol | Value | Note |
|---|---|---|
| `DAT_00187558` | **0.0166667** | **= 1/60 — the fixed physics timestep** |
| `DAT_00187838` | 0.0333333 | = 1/30 |
| `DAT_00187834` | 0.0125 | = 1/80 |
| `DAT_00187544` | 277.778 | = 1000/3.6 exactly (a km/h conversion) |
| `DAT_00187840` | 0.92 | damping |
| `DAT_001876d0` | 0.6 | friction-ish |
| `DAT_00187828` | 0.998957 | |
| `DAT_00187824` | 1.81535 | |
| `DAT_00187820` | 2450.82 | |
| `DAT_0018782c` | 2.5 | |
| `DAT_00187830` | 3.14159 | second copy of PI |
| `DAT_0018784c` | 0.0009 | |
| `DAT_00187670` | -2 | |
| `DAT_0018783c` | 20 | |
| `DAT_00187550` | 40 | |
| `DAT_001a9f50` | 0.5 | |
| `DAT_001c6be0..bf0` | 0 | .bss runtime state, not constants |

**The 1/60 timestep is the significant one**: rider physics is a fixed 60 Hz
step, matching the frame timings found in the boot sequence.

### OPEN QUESTION: the world unit is not yet determined
`DAT_00187544` = 277.778 is *exactly* `1000/3.6`, the factor converting km/h to
"thousandths of a unit per second", and `Rider_MinLandingSpeed` = 555.556 is
exactly twice it. That is strong evidence of a km/h conversion but does **not**
by itself fix the unit.

Two readings, neither yet proven:
* **1 unit = 1 mm** -> 277.778 units/s = 1 km/h, min landing speed 2 km/h.
  But `gari.ltg`'s bbox is then only 227 m x 305 m with a **280 m vertical
  drop** -- a 43-degree average slope over a very short run.
* **1 unit = 1 cm** -> 277.778 units/s = 10 km/h, min landing speed 20 km/h
  (a much more sensible "must be moving to land a trick" threshold), and gari
  becomes **2.3 km x 3.1 km with a 2.7 km drop** -- a realistic mountain run.

Physical plausibility favours **cm**, but that is an argument, not a
measurement. A decisive test would be a gravity constant (9.81 m/s^2 = 981 in
cm, 9810 in mm). **Searched 9 x 2 KB blocks across the constant regions
(0x00186000-0x00189800, 0x001a9800-0x001aa800) for values near 9.81 / 98.1 /
981 / 9810: the only hit was `0x001888f8` = 98.0286, and its sole xref is
`Rider_UpdateBoardAttachmentTransforms`** -- a board transform, not gravity.

So the unit scale is **undetermined** and is recorded as such. Do not bake
either assumption into the port; keep physics in raw world units until a
gravity or jump-height constant settles it.

## Airborne modes 5/6 constants + the unit question, strengthened

`Rider_PhysicsMode5_NoTerrain` (0x0002ba50, 545 lines) and
`Rider_PhysicsMode6_NoTerrain` (0x0002a920, 152 lines).

**Mode 6 is tiny and uses only four constants**: `1/60` (timestep),
`+8.33333`, `-8.33333`, and `0.983333`. Two exact relationships:
* `8.33333 = 500/60` -- a symmetric +/- pair, i.e. a **rate clamp of 500 per
  second** applied per frame (most likely a spin/rotation rate limit; 500 deg/s
  is ~1.4 rotations/sec, a sensible cap for a snowboarder).
* `0.983333 = 1 - 1/60` exactly -- a **per-frame exponential decay**.

So mode 6 is a small rate-limited decay controller, not the gravity integrator.
**No gravity constant was found in modes 5 or 6 either.**

### The speed ladder -- strong circumstantial evidence for centimetres
Every speed-like constant found so far is an exact clean multiple of
`DAT_00187544` = 277.778 (= `1000/3.6`):

| Constant | value | x 277.778 | as km/h if unit = **mm** | if unit = **cm** |
|---|---|---|---|---|
| `DAT_00187544` | 277.778 | 1 | 1 | **10** |
| `Rider_MinLandingSpeed` | 555.556 | 2 | 2 | **20** |
| `DAT_001878a4` | 694.444 | 2.5 | 2.5 | **25** |
| `DAT_001875dc` | 833.333 | 3 | 3 | **30** |
| `DAT_001878a0` | 2222.22 | 8 | 8 | **80** |

The multiples being exactly 1, 2, 2.5, 3 and 8 shows these *are* speeds authored
in a round unit. Under **mm** they are 1-8 km/h -- absurd for downhill
snowboarding. Under **cm** they are 10-80 km/h, with a 20 km/h minimum landing
speed and an 80 km/h high-speed threshold -- exactly the range the game plays at.
Combined with the track-size argument (2.3 x 3.1 km vs 227 x 305 m),
**1 world unit = 1 cm is strongly indicated**.

**Still not proven.** No gravity constant has been located
(searched 0x00186000-0x00189800, 0x001a9800-0x001aa800, plus all constants of
modes 3/5/6). `Commentary_TryTriggerSpeedThresholdReaction`'s threshold turned
out to be a normalised rider stat (3.0 vs `rider+0x5604`), not a world speed, so
it does not settle it either. The port continues to work in raw world units;
this is a labelling question, not a behavioural one, so nothing is blocked.

## M2d: debug wireframe renderer (`port/src/game/debug_draw.{h,cpp}`)

`SSX_DEBUG_TERRAIN=<track>` replaces the boot sequence with a view of that
track's real `.ltg` grid; `SSX_DEBUG_VIEW=2|3` picks a side projection, and the
d-pad / A / B switch view and toggle bounds at runtime.

Everything drawn is parsed from the file:
* the nominal grid footprint (`worldMin + index*cellSize`), dim for empty cells
  and blue for populated,
* each populated cell's **content AABB** (orange),
* optionally each active sub-cell's AABB (green),
* a probe marker sweeping the world, with the **ported broad phase**
  (`terrain::querySurfaceCandidates`) run on it every frame so the M2b queries
  are exercised continuously, not only in the offline self-test.

### Visual confirmation of the format work
Top-down, gari's populated cells and content AABBs trace a winding run
diagonally across the 23x31 grid -- matching the ASCII map the original
`classify_ltg.py` produced, but now from the corrected parse. The **side XZ
view shows a continuous descent** from one corner to the other: a coherent
downhill mountain profile.

That is a meaningful check. A wrong stride, base or field offset would produce
scattered or overlapping boxes, not a smooth monotonic descent in one projection
and a connected path in the other.

## Rider field map used by the physics modes (groundwork for transcription)

Ghidra recovers no structs, so every ported function needs its field offsets
resolved by hand. Extracted mechanically from the decompiles of
`Rider_PhysicsMode1_GroundRide`, `..Mode2_GroundContact`,
`Rider_ResolveTerrainContactPhysics`, `..Mode5/6_NoTerrain`,
`Rider_UpdateTerrainContact`, `Rider_DispatchPhysicsMode`,
`Rider_UpdatePhysicsState` and `Rider_UpdateSubsystems`.

These use the **component-frame** access pattern
`*(T *)(componentBase + 0xOFF + param_1)` -- the same `+0x10`-shifted frame
documented elsewhere in this project -- not plain `param_1 + 0xOFF`. A naive
scan for the latter finds only unrelated locals, which is why the offsets below
matter.

**36 distinct component-frame fields**, with the modes that touch them:

| Offset | Uses | Writes | Seen in |
|---|---|---|---|
| `0x0048` | 2 | 1 | mode 1 |
| `0x017c` | 5 | 0 | modes 1, 2, 6 |
| `0x0180` | 2 | 0 | mode 3 |
| `0x018c` | 9 | 0 | modes 2, 6 |
| `0x01a0` | 5 | 0 | mode 1 |
| `0x01e0` | 4 | 0 | mode 5 |
| `0x0248` | 2 | 2 | mode 5 |
| `0x0250`, `0x0254` | 1 each | 1 each | mode 3 |
| `0x02dc` | 6 | 0 | modes 2, 6 |
| `0x02e4` | 2 | 0 | modes 2, 6 |
| `0x02f8` | 1 | 0 | mode 1 (gated against `DAT_00187548` = 0.7) |
| `0x0338` | 3 | 0 | mode 5 |
| `0x045c` | 2 | 1 | mode 2 |
| `0x5620`, `0x56c8` | | | mode 1 |
| `0x5628` | 1 | 0 | mode 5 |

(Full 36-row list produced by the extraction script; the above is the subset
with multiple uses or writes.)

`0x5628` is the same field `RiderEvent_UpdateRailRideMovement` reads, tying the
rail system and the airborne mode to one rider field -- consistent with modes
5/6 being the non-ground (airborne/rail) states.

### Why this is the next step, not the SSE math
Modes 1/2/3 total ~1600 decompiled lines dominated by SSE vector blocks with
Ghidra-mangled locals. Transcribing them "by eye" would produce plausible-looking
but unverifiable physics -- the exact failure mode this project has been
correcting all session. The tractable order is:
1. the field map above (**done**),
2. name the fields by observing which are read before/after `Terrain_Query*`
   and which the landing path writes,
3. then transcribe each mode function against named fields, verifying each
   against the debug sandbox.

## The rider world transform — `rider+0x4870`, a 4x4 matrix (identified)

The single most important field for the physics port, and it is now pinned.

`Rider_UpdateTerrainContact` reads **16 consecutive floats** at
`rider+0x4870 .. +0x48ac` — four `vec4`s, i.e. a **4x4 row-major matrix**:

| Offset | Row | Meaning |
|---|---|---|
| `+0x4870` | 0 | X axis (right) |
| `+0x4880` | 1 | Y axis (forward) |
| `+0x4890` | 2 | Z axis (up) |
| **`+0x48a0`** | 3 | **world position** |

Row 3 is what the function passes as the *position* argument to
`Terrain_QuerySurfaceContact`, and rows 0/1/2 are the basis it projects the
contact delta against.

### Corroboration (why this is identification, not inference)
`+0x4870` (the whole matrix) has exactly three users:
`Rider_UpdateTerrainContact`, `FUN_0003bd50`, and
**`Rider_UpdateBoardAttachmentTransforms`** — attaching a board to a rider
requires precisely the rider's world matrix.

`+0x48a0` (row 3) has six, including `Rider_ResolveTerrainContactPhysics`,
**`Rider_UpdateSurfaceParticleFX`** and **`Rider_UpdateSnowSprayFX`**. Snow
spray and surface particles spawn at the board's ground position — a
translation row, not an axis. Two independent subsystem families agreeing on
the same interpretation.

This makes the physics transcription tractable: the mode functions' SSE blocks
are dot products of the contact delta against these basis rows, which is exactly
what `Rider_UpdateTerrainContact`'s decoded body does.

## CORRECTION to the rider field map above — component slots differ per mode

The "Rider field map used by the physics modes" table earlier in this file is
**misleading and must not be used as written**. It merged offsets from
different component frames as though they were one address space.

### The real access pattern
```c
componentBase = *(int *)(*(int *)(rider + SLOT) + 4);   // SLOT varies!
field         = *(T *)(componentBase + OFFSET + rider);
```
and **each physics mode reads through a different slot**:

| Function | slot |
|---|---|
| `Rider_PhysicsMode1_GroundRide` | `rider + 0x20` |
| `Rider_PhysicsMode2_GroundContact` | `rider + 0x0c` |
| `Rider_ResolveTerrainContactPhysics` (mode 3) | `rider + 0x10` |
| `Rider_DispatchPhysicsMode` | `rider + 0x30` |

So the *same* numeric offset denotes **different memory** in different modes.

### Offsets that are genuinely ambiguous
Four appear under more than one slot and would be silently conflated by an
offset-only scan: **`0x17c`, `0x18c`, `0x2dc`, `0x2e4`**.

### A concrete instance of the trap
`0x19c` looks like one field but is two:
* `rider + 0x19c` **direct** — an **int**, the cached terrain cell index,
  written by `Rider_ComputeTerrainCellIndex`.
* `componentBase + 0x19c + rider` in modes 1/2/3 — a **float** that is
  integrated (`+= fStack_484`, squared, `+= fStack_c4 * local_d4`).

Different type, different meaning, different address. This is the same
"phantom field" trap recorded earlier in this project (`rider+0x5720` vs
`rider+0x5710` through a shifted frame) — it recurs, and an offset-only scan
walks straight into it.

### Corrected per-slot map
| Function | slot | offsets |
|---|---|---|
| mode1 | `0x20` | `0x48 0x17c 0x1a0 0x2f0 0x2f8 0x4b4 0x434c 0x5620 0x56c8` |
| mode2 | `0x0c` | `0x168 0x17c 0x18c 0x240 0x24c 0x2dc 0x2e4 0x2fc 0x35c 0x36c 0x45c 0x464 0x4a0` |
| mode3 | `0x10` | `0x180 0x218 0x250 0x254` |
| mode5 | (tbd) | `0x1e0 0x210 0x220 0x248 0x31c 0x330 0x338 0x4a8 0x5628` |
| mode6 | (tbd) | `0x17c 0x18c 0x2cc 0x2dc 0x2e4 0x39c` |

**Rule for the transcription**: never record a rider field as a bare offset.
Always record `(slot, offset)`, and resolve the slot before naming anything.

### What remains directly addressed (no slot involved)
These are true `rider + offset` fields, confirmed:
* `+0x019c` int — cached terrain cell index
* `+0x0180..0x018c` vec4 — landing direction (written by the landing path,
  Z component scaled by 0.1)
* `+0x0208` int — landing class 1/2/3/4
* `+0x02b0` float — threshold lerp factor
* `+0x4870..0x48ac` — the 4x4 world transform, row 3 = position

## SOLVED: what the component "slots" actually are — MI sub-object descriptors

The `rider + SLOT` indirection used by every physics mode is **C++ multiple
inheritance**, not a game-specific component system. This resolves the whole
field-addressing question.

### The mechanism
`Rider_ConstructBase` (`0x00036490`) stores pointers to a table of **8-byte
descriptors**:
```c
param_1[0xc]   = &DAT_001887f0;   // byte offset 0x30
param_1[0x138] = &DAT_001887e8;
param_1[0x13f] = &DAT_001887e0;
...                               // 12 in total
```
The descriptors live at `0x00188798 .. 0x001887f0`, 8 bytes apart:

| Descriptor | `[+0]` adjust | `[+4]` sub-object offset |
|---|---|---|
| `0x00188798` | 0 | `0x01d0` |
| `0x001887a0` | 0 | `0x01e0` |
| `0x001887a8` | -0x14 | `0x01ec` |
| `0x001887b0` | -0x10 | `0x0210` |
| `0x001887b8` | 0 | `0x0230` |
| `0x001887c0` | -0x0c | `0x0234` |
| `0x001887c8` | -0x60 | `0x02e0` |
| `0x001887d0` | 0 | `0x0350` |
| `0x001887d8` | -0x10 | `0x0360` |
| `0x001887e0` | -0x0c | `0x0374` |
| `0x001887e8` | -0x20 | `0x0390` |
| **`0x001887f0`** | 0 | **`0x0840`** (the dispatcher's, at `rider+0x30`) |

`[+0]` being small negatives is the giveaway: these are **`this`-adjustment
thunck records**, the standard MSVC layout for a class with multiple bases.
`[+4]` is the base sub-object's byte offset within the complete rider.

### The addressing rule for the port
```
fieldAddr = rider + descriptor[SLOT][+4] + OFFSET
```
So for `Rider_DispatchPhysicsMode`, the physics-mode selector is
`rider + 0x840 + 0x484` = **`rider + 0xCC4`**, not `rider + 0x484` as the
earlier note implied.

### Why this matters more than it looks
It explains the recurring "phantom field" trap exactly: two modes using the
*same* numeric offset are addressing **different base sub-objects**, so the
addresses differ by the descriptors' offsets. Any offset recorded without its
slot is meaningless.

**Every rider field in this project's notes that was recorded as a bare offset
from a physics function should be re-checked against this rule.** The five
direct fields confirmed earlier (`+0x19c`, `+0x180..018c`, `+0x208`, `+0x2b0`,
`+0x4870..48ac`) are unaffected -- they are read straight off `param_1` with no
slot indirection.

### Still to resolve
The descriptors for slots `rider+0x0c`, `+0x10` and `+0x20` (modes 2, 3 and 1)
are not assigned in `Rider_ConstructBase`'s visible body -- they come from a
base or derived constructor. Finding those three gives the exact sub-object
offsets for the three ground modes, which is the last thing needed before their
fields can be named.

## CORRECTION: the physics modes do NOT share a `this` — each is a different base

The "slot" table two sections up is **wrong about whose object the slot belongs
to**, and the error is worth spelling out because it invalidates a natural
assumption.

### The contradiction that exposed it
* `NodeBase_ConstructRoot` (`0x000aabf0`) sets `param_1[8]` -- byte **`0x20`** --
  to a *sequential instance ID* (an `int` from one of two global counters).
* `Rider_PhysicsMode1_GroundRide` does
  `iVar5 = *(int *)(*(int *)(param_1 + 0x20) + 4);` -- dereferencing `+0x20`
  as a **pointer**.

Both cannot be true of the same object. An `int` instance ID dereferenced as a
pointer would be a wild read.

### The resolution
`param_1` **is a different object in each mode**. These functions are methods of
*different base classes* of the rider, each receiving that base's own adjusted
`this`. Only `Rider_DispatchPhysicsMode`'s `param_1` is the complete rider --
its `+0x30` descriptor matches `Rider_ConstructBase`'s `param_1[0xc] =
&DAT_001887f0` exactly.

So the earlier per-slot table should be read as *"mode 1 uses a descriptor at
`+0x20` **of its own base's `this`**"*, not "of the rider". The offsets are
still correct relative to each mode's own `this`; what is wrong is treating
them as rider offsets.

### Supporting detail from the constructor tail
```c
*(int *)(*(int *)(*piVar1 + 4) + 0x2c + (int)param_1) = *(int *)(*piVar1 + 4) + -0x840;
*(undefined4 **)(*(int *)(*piVar1 + 4) + 0x5910 + (int)param_1) = param_1;
*(undefined4 **)(*(int *)(*piVar1 + 4) + 0x7e4  + (int)param_1) = param_1;
```
Sub-objects store **back-pointers to the complete rider** (at `+0x5910` and
`+0x7e4` within the sub-object) and a **negative delta** (`offset - 0x840`) at
`+0x2c` -- exactly the bookkeeping a base needs to recover the complete object.
That is how a mode function gets from its own `this` back to the rider.

Also from the same tail: the rider's broad-phase AABB is bound from
`subobj + 0x740` / `subobj + 0x750` via `SweepPrune_BindAxisBounds`.

### What this means for the port
The transcription cannot proceed field-by-field until, for each mode, we know
**which base sub-object its `this` points at**. The route is the back-pointer:
find where each mode's `this` is derived in `Rider_DispatchPhysicsMode`'s call
sequence (Ghidra drops the `ECX` adjustments, so this needs the disassembly).

**Status of the rider field work: the offsets are catalogued and the mechanism
is understood, but no rider field from a physics mode can be named yet.** The
five *direct* fields remain valid, since they are read off a pointer that is
demonstrably the rider (the same one `Terrain_QuerySurfaceContact` gets a
position from).

## SOLVED: rider field addressing for the physics modes (complete chain)

`Rider_DispatchPhysicsMode`'s disassembly gives the `this` adjustment for every
mode -- the decompiler drops these entirely:
```asm
0003138a: ADD ECX,0x4c0 ; JMP Rider_PhysicsMode1_GroundRide      (0x00025f90)
00031395: ADD ECX,0x4f0 ; JMP Rider_PhysicsMode2_GroundContact   (0x000276a0)
000313a0: ADD ECX,0x500 ; JMP Rider_ResolveTerrainContactPhysics (0x000287a0)
000313ab: ADD ECX,0x520 ; JMP Node_NoOpStub1                     (0x000b5c10)
000313b6: ADD ECX,0x530 ; JMP Rider_PhysicsMode5_NoTerrain       (0x0002ba50)
000313c1: ADD ECX,0x630 ; JMP Rider_PhysicsMode6_NoTerrain       (0x0002a920)
```

### The full address chain
```
thisPtr   = rider + delta                  (from the jump table above)
descPtr   = *(thisPtr + SLOT)              (SLOT differs per mode)
base      = descPtr[+4]                    (MI sub-object offset)
fieldAddr = rider + delta + base + offset
```

| Mode | delta | slot | descriptor at | matches ctor | base | **delta+base** |
|---|---|---|---|---|---|---|
| 1 GroundRide | `0x4c0` | `0x20` | rider+`0x4e0` | `param_1[0x138]` | `0x390` | **`0x850`** |
| 2 GroundContact | `0x4f0` | `0x0c` | rider+`0x4fc` | `param_1[0x13f]` | `0x374` | **`0x864`** |
| 3 ContactPhysics | `0x500` | `0x10` | rider+`0x510` | `param_1[0x144]` | `0x360` | **`0x860`** |

Every descriptor pointer lands **exactly** on an assignment in
`Rider_ConstructBase` (indices `0x138`, `0x13f`, `0x144`) -- three independent
confirmations, no slack.

The dispatcher's own `this` is the unadjusted rider, so the physics-mode
selector is `rider + 0x840 + 0x484` = **`rider + 0xCC4`** (not `rider+0x484`).

### The payoff: the naive scan was wrong in BOTH directions
Converting every per-mode offset to a true rider address shows the offset-only
approach made two opposite errors:

**(a) It merged fields that are different.** Offset `0x17c` is
`rider+0x09cc` in mode 1 but `rider+0x09e0` in mode 2 -- different memory.

**(b) It split fields that are the same.** Seven rider addresses are shared
across modes under *different* per-mode offsets:

| Rider address | reached as |
|---|---|
| `rider+0x09cc` | mode1+`0x17c`, mode2+`0x168` |
| `rider+0x09e0` | mode2+`0x17c`, mode3+`0x180` |
| `rider+0x09f0` | mode1+`0x1a0`, mode2+`0x18c` |
| `rider+0x0ab0` | mode2+`0x24c`, mode3+`0x250` |
| `rider+0x0b40` | mode1+`0x2f0`, mode2+`0x2dc` |
| `rider+0x0b48` | mode1+`0x2f8`, mode2+`0x2e4` |
| `rider+0x0d04` | mode1+`0x4b4`, mode2+`0x4a0` |

These seven are **shared rider physics state** -- the fields multiple movement
models read and write in common, which is exactly where velocity/speed/surface
state should live. They are the highest-value naming targets.

### Canonical rider offsets for the three ground modes
* mode 1: `0x0898 0x09cc 0x09f0 0x0b40 0x0b48 0x0d04 0x4b9c 0x5e70 0x5f18`
* mode 2: `0x09cc 0x09e0 0x09f0 0x0aa4 0x0ab0 0x0b40 0x0b48 0x0b60 0x0bc0 0x0bd0 0x0cc0 0x0cc8 0x0d04`
* mode 3: `0x09e0 0x0a78 0x0ab0 0x0ab4`

**Field addressing for the physics port is now solved.** Naming the seven
shared fields is the next step, and it is ordinary work rather than a blocker.

## First reads on the seven shared physics fields

Using the solved addressing, the shared fields can be characterised by how each
mode uses them. Three are already clear:

### `rider+0x09cc` — a per-second RATE (velocity-like scalar)
Both modes multiply it by the fixed timestep before use:
```c
mode1: local_4e8 = *(float *)(iVar2  + 0x17c + param_1) * DAT_00187558;  // *1/60
mode2: local_a0  = *(float *)(iVar11 + 0x168 + param_1) * DAT_00187558;  // *1/60
```
Multiplying by `1/60` converts a per-second quantity into a per-frame delta, so
this field is stored **per second**. Two independent modes agreeing on that
treatment makes it a solid read.

### `rider+0x0b40` — a VECTOR base
Taken as a `float *` (not read as a scalar) by both modes, then indexed:
```c
mode1: pfVar6  = (float *)(*(int *)(*(int *)(param_1 + 0x20) + 4) + 0x2f0 + param_1);
mode2: pfVar12 = (float *)(iVar11 + 0x2dc + param_1);   // and 3 more sites
```
Mode 2 touches it at four separate points, consistent with a vector that is
read, modified and written back across the step.

### `rider+0x0b48` — a scalar with normalised thresholds
```c
mode1: if (DAT_00187548 <= *(float *)(iVar5 + 0x2f8 + param_1))   // >= 0.7
mode2: if (*(float *)(iVar11 + 0x2e4 + param_1) < DAT_001a9f34)   // < 0.0
```
Compared against `0.7` and `0.0` — a normalised quantity (a dot product or a
0..1 blend), not a world-space magnitude.

**Caution recorded**: mode 2 also has `*(float *)(iVar3 + 0x2e4)` at lines
343/457/470, but those go through `iVar3`, a *different* base than `iVar11`.
Under the addressing rule just established, those are **not** `rider+0x0b48`
and must not be folded in. Exactly the mistake the offset-only scan made.

### Remaining four
`rider+0x09e0`, `+0x09f0`, `+0x0ab0`, `+0x0d04` still need the same treatment.

## All seven shared physics fields characterised

Each is corroborated by the two or three independent modes that reach it
through *different* per-mode offsets — so agreement between them is real
evidence, not one reading repeated.

| Rider address | Reached as | Type | Evidence |
|---|---|---|---|
| `+0x09cc` | m1+`0x17c`, m2+`0x168` | float, **per-second rate** | both modes multiply by `1/60` before use |
| `+0x09e0` | m2+`0x17c`, m3+`0x180` | **vector** | taken as `float *` by both |
| `+0x09f0` | m1+`0x1a0`, m2+`0x18c` | **vector** | taken as `float *` by both |
| `+0x0ab0` | m2+`0x24c`, m3+`0x250` | float scalar | read in m2; **zeroed** in m3 (`= 0`) |
| `+0x0b40` | m1+`0x2f0`, m2+`0x2dc` | **vector** | `float *` in both; 4 sites in m2 |
| `+0x0b48` | m1+`0x2f8`, m2+`0x2e4` | normalised scalar | thresholds `0.7` and `0.0` |
| `+0x0d04` | m1+`0x4b4`, m2+`0x4a0` | **pointer to an object** | dereferenced, then `+0x58` (char) in m1 and `+0x94` (int, `!= 1`) in m2 |

`+0x0d04` being a pointer is notable: the ground modes both consult another
object through it, and mode 2 gates on `that->[0x94] != 1`. That is a link to a
separate entity/state object, not physics scalar state.

The three **vectors** (`+0x09e0`, `+0x09f0`, `+0x0b40`) plus the **rate**
(`+0x09cc`) are the core of the rider's per-frame motion state — position
delta, velocity, surface normal and speed are the natural candidates, and
distinguishing them is now a matter of reading each mode's arithmetic rather
than hunting addresses.

### Non-shared field of note
`rider+0x0898` (mode 1 only, via `0x48`) is an **int counter that wraps mod 4**:
```c
uVar9 = *(int *)(iVar5 + 0x48 + param_1) + 1U & 0x80000003;
*(uint *)(iVar5 + 0x48 + param_1) = uVar9;
```
`& 0x80000003` keeps the sign bit and the low 2 bits — a 4-state cycle, most
likely a phase/step index rather than a physical quantity.

### Where M2c stands
* address model — **solved and verified**
* seven shared fields — **characterised by type**
* naming them to specific quantities, then transcribing modes 1/2/3 — remaining

## The three shared vectors, separated by their arithmetic

### `rider+0x09f0` — an INTEGRATED state vector
Mode 2 reads and writes it through the same address in one step:
```c
pfVar10 = (float *)(iVar11 + 0x18c + param_1);   // rider+0x09f0
pfVar1  = (float *)(iVar11 + 0x18c + param_1);   // same address
*pfVar1   = *pfVar10 + fVar24 * local_a0;        // v += delta * dt
pfVar1[1] = fVar28   + fVar25 * local_a0;
pfVar1[2] = fVar29   + fVar26 * local_a0;
pfVar1[3] = fVar31   + fVar27 * local_a0;
```
with `local_a0 = rider+0x09cc * (1/60)` — i.e. **`rate x timestep` = dt**. This
is a textbook explicit-Euler integration, so `+0x09f0` holds integrated motion
state (velocity or position; distinguishing the two needs the delta's origin).

Note this also confirms the earlier read of `+0x09cc`: it is consumed *as the
timestep multiplier*, so it is genuinely a per-second quantity.

### `rider+0x09e0` — a DIRECTION vector (dot-product operand)
Never written by the modes that read it; used as an operand in 4-component dot
products, e.g. against `rider+0x0b60`:
```c
pfVar1 = (rider+0x09e0); pfVar10 = (rider+0x0b60);
fVar27 = *pfVar1 * *pfVar10 + pfVar1[1] * pfVar10[1];   // ... SSE horizontal add
```

### `rider+0x0b40` — the other dot-product operand
Same treatment: read as `float *`, dotted against `+0x09f0`. Together with
`+0x09e0` these form the basis the integrated vector is projected onto.

### Bonus: a damped scalar accumulator at `rider+0x0a5c`
```c
*(float *)(iVar11 + 0x1f8) = ((float)local_90 - (float)local_70)
                           + *(float *)(iVar11 + 0x1f8);      // accumulate a dot difference
*pfVar10 = (DAT_001a9f30 - _DAT_001877b8 * local_a0) * *pfVar10;   // *= (1 - 4.684*dt)
```
An accumulator of a dot-product *difference*, then **exponentially damped at
4.684 per second**. That shape — accumulate an angular error, bleed it off — is
characteristic of a lean/spin accumulator rather than a linear quantity.

### Method note
None of these identifications came from guessing at names. Each is the
*arithmetic shape* the code applies: written-in-place with `+= d*dt` means
integrated state; read-only into horizontal-add pairs means a dot operand;
`x *= (1 - k*dt)` means exponential damping. Naming them to specific physical
quantities is the next step and needs the deltas' provenance.

## The integration delta is an ACCELERATION — and a gravity-shaped term

Tracing where mode 2's integration delta comes from settles what `+0x09f0`
holds. The delta is assembled as a **weighted sum of basis vectors**:
```c
fVar24 = fVar29 * local_ac + local_90 * local_b8 + *pfVar1 * fVar28
       + *pfVar10 * fVar27 + 0.0;                    // ... x4 components
```
i.e. `delta = A*w1 + B*w2 + C*w3 + D*w4` — several contributions with scalar
weights, which is the shape of an **acceleration**, not a velocity. So the
integration `*(+0x09f0) += delta * dt` makes **`rider+0x09f0` the velocity**,
not the position.

### A conditional -1000 along `rider+0x0b40`
```c
pfVar2 = (float *)(iVar11 + 0x2dc + param_1);   // rider+0x0b40
fVar29 = *pfVar2; fVar31 = pfVar2[1]; ...
if (bVar5) {
    fVar24 = fVar24 + fVar29 * _DAT_001aa420;   // _DAT_001aa420 = -1000
    fVar25 = fVar25 + fVar31 * _DAT_001aa420;
    fVar26 = fVar26 + fVar16 * _DAT_001aa420;
    fVar27 = fVar27 + fVar23 * _DAT_001aa420;
}
```
An acceleration of **-1000 units/s^2 along the `+0x0b40` direction**, applied
only under a condition. If `+0x0b40` is the surface normal / up axis, this is
gravity.

### Bearing on the unit question — a second independent line of evidence
`-1000 units/s^2` reads as:
* **cm**: -10 m/s^2 — the classic game-rounded gravity (9.81 rounded to 10)
* **mm**: -1 m/s^2 — a tenth of gravity, implausible

This **corroborates the centimetre reading** already indicated by the speed
ladder (1/2/2.5/3/8 x 277.778 giving 10-80 km/h). Two independent lines now
point the same way.

**Still short of proof**, and deliberately recorded as such: `DAT_001aa420` has
exactly **one** xref in the whole binary (`Rider_PhysicsMode2_GroundContact` at
`0x00027a2c`) and is gated on a condition. A global gravity constant would be
expected in the airborne modes too, and those contain no such value. So this may
be a mode-2-specific "stick to the surface" force rather than gravity proper.
The unit scale stays **strongly indicated (cm), not proven**.

## `rider+0x0b40` — written by mode 1, then dotted with velocity

Mode 1 conditionally **stores** a vector into it and immediately uses it:
```c
if (DAT_001a9f34 <= local_4e8) {                 // gated on a scalar >= 0
    pfVar6 = (float *)(... + 0x2f0 + param_1);   // rider+0x0b40
    *pfVar6 = local_400; pfVar6[1] = fStack_3fc;
    pfVar6[2] = fStack_3f8; pfVar6[3] = fStack_3f4;
}
pfVar6 = (float *)(iVar5 + 0x1a0 + param_1);     // rider+0x09f0 = VELOCITY
pfVar1 = (float *)(iVar5 + 0x2f0 + param_1);     // rider+0x0b40
fVar12 = *pfVar1 * *pfVar6 + pfVar1[1] * pfVar6[1];   // dot(0x0b40, velocity)
```
So `+0x0b40` is a **direction that mode 1 computes and stores, then uses to
decompose the velocity** — the component of velocity along it. Mode 2 applies
the `-1000` acceleration along this same direction.

That combination (store a direction, project velocity onto it, accelerate along
it) is the signature of the **surface/contact axis** the ground modes work in.

### But it is NOT the contact normal — that is a different field
The existing verified note on `Rider_ResolveTerrainContactPhysics` says it
"stores the contact normal (rider+0x310)". Under the now-known addressing that
offset is relative to **mode 3's base `0x860`**, so the contact normal actually
lives at:

    rider + 0x860 + 0x310 = **rider+0x0B70**

which is a *different* address from `rider+0x0b40`. The two must not be
conflated — and note that the older note's bare "rider+0x310" is itself an
instance of the offset-without-slot error catalogued earlier in this file.

### Honest limit on naming
`+0x0b40` is demonstrably "the axis the ground modes decompose motion along".
Whether it is the world up-axis or the surface normal is **not** settled by the
evidence to hand: gravity along a world up-axis and a sticking force along a
surface normal have the same arithmetic shape here. Recorded as the
**contact/vertical axis** without committing further.

## RESOLVED: `rider+0x0b40` is TERRAIN-DERIVED (the surface normal)

The previous section left open whether `+0x0b40` was a world up-axis or the
surface normal. Mode 1's producing code settles it: the value is stored **only
after a successful terrain query**.

```c
local_430 = *(float *)(iVar5 + 0x48f0);              // a rider axis vec4
...
local_3e0 = local_490 + local_430 * _DAT_001876f0;   // P + dir * (-100)
local_3d0 = local_490 + local_430 * _DAT_001876ec;   // P + dir * (+200)
FUN_001412b0(&local_3e0, &local_3d0, 0x3f000000, 1); // segment query, t = 0.5
fVar11 = Terrain_SampleHeightAt(local_3b0, local_420, ...);
local_4e8 = (float)fVar11;
if (DAT_001a9f34 <= local_4e8) {                     // >= 0  => a hit
    pfVar6 = (float *)(... + 0x2f0 + param_1);       // rider+0x0b40
    *pfVar6 = local_400; ...                         // store
}
```

A world up-axis would be constant and would not be gated on a terrain hit.
Being written only when `Terrain_SampleHeightAt` succeeds makes `+0x0b40`
**terrain-derived — the ground/surface normal under the rider**.

That also makes the rest consistent: mode 1 dots it with the velocity (approach
speed along the surface), and mode 2's `-1000` along it is a **into-the-surface
sticking force**, not gravity. Which in turn *weakens* the gravity reading of
`DAT_001aa420` — see the unit-scale caveat below.

`local_400` itself is never assigned in the decompile: it is an output written
through a pointer Ghidra did not model, from `FUN_001412b0` or
`Terrain_SampleHeightAt`.

### The ground probe — and a THIRD line of evidence for centimetres
The probe spans `-100 .. +200` units along the rider axis at `rider+0x48f0`
(the vec4 just past the transform block, which ends at `0x48b0`):

| Unit | Probe range | Sensible for a snowboarder? |
|---|---|---|
| **cm** | **-1 m to +2 m** | yes — exactly a ground-probe range |
| mm | -10 cm to +20 cm | no — too short to find terrain |

Three independent lines now point to **1 unit = 1 cm**: the speed ladder
(10-80 km/h), the `-1000` acceleration (-10 m/s^2), and this probe geometry.

**Caveat, stated plainly**: since `+0x0b40` is the surface normal rather than
world-up, the `-1000` term is a surface-sticking force, not gravity — so that
particular line of evidence is weaker than it first appeared. The speed ladder
and the probe geometry remain the strong ones. Unit scale: still **strongly
indicated (cm), not proven**.

## Mode 2's acceleration is a SUSPENSION + CARVE model (the core riding mechanic)

Tracing the four acceleration weights turns mode 2 from opaque SSE into a
recognisable physical model.

```c
fVar13 = Rider_ComputeSurfaceCompressionResponse(pfVar12, *(frame+500),
                                                 local_70, local_a0);
local_ac = (float)fVar13;                       // suspension force
...
if ((rider+0x0b48 < 0.0) && (local_ac < 0.0)) local_ac = 0.0;   // no negative force

fVar28 = _DAT_001877bc / _Math_DegreesPerHalfTurn;               // = PI/180
*(frame + 0x210) = fVar28 * pfVar12[5] * *(frame + 0x24c);       // an ANGLE, in radians
fVar29 = *(frame + 0x210);
fVar28 = FUN_0015c9b8(fVar29);                                   // sin
fVar29 = FUN_0015c908(fVar29);                                   // cos

local_b8 = *pfVar12 + *pfVar12;                 // 2x a limit
if (local_ac <= local_b8) local_b8 = local_ac;  // clamp to the available force
local_b8 = local_b8 / fVar29;                   // / cos(angle)
local_ac = local_ac - local_b8;                 // split the force in two

local_90 = *pfVar1 * fVar29 + *pfVar10 * fVar28; // basis rotated BY that angle
```

### What it is
* **Suspension**: `Rider_ComputeSurfaceCompressionResponse` (`0x00026da0`,
  already named in this project) produces a compression force from the board's
  penetration into the surface. It is clamped non-negative — a spring that
  pushes but never pulls.
* **Edge angle**: an angle is built in radians via `PI/180` from a stored value
  (`frame+0x24c` = `rider+0x0ab0`, the scalar mode 3 zeroes) and cached at
  `frame+0x210` = `rider+0x0a74`.
* **Carve**: `sin`/`cos` of that angle rotate a basis vector
  (`local_90 = normal*cos + tangent*sin`) — the **board's edge direction**.
* **Force split**: the suspension force is divided between the surface normal
  (`local_ac`) and the rotated edge direction (`local_b8 = force/cos`), with
  `local_ac = force - local_b8` conserving the total.

That is a board-on-snow **carving model**: tilt the board, and the reaction
force redistributes from "straight up out of the slope" toward "along the
edge" — which is exactly how a snowboard turns.

### The four acceleration terms, now identified
```c
accel = surfaceNormal(0x0b40) * local_ac      // suspension along the normal
      + rotatedEdge(local_90) * local_b8      // carve along the tilted edge
      + vec(rider+0x0BC0)     * fVar28
      + vec(rider+0x0BD0)     * fVar27        // from FUN_000274b0
      [+ surfaceNormal * -1000 when gated]    // surface stick
```

Note `DAT_001aa424 / DAT_001877c8` = `30 / 30` = **1.0** — a ratio of two
independently-authored 30s, so it is an identity today but was presumably a
tunable pair.

### Method note
`FUN_0015c9b8` and `FUN_0015c908` are unnamed in the database but take a
`double` and are used as a sin/cos pair on the same angle immediately after a
`PI/180` conversion — their roles are fixed by that usage, not by their names.

## Mode 2's full force model — four terms on an orthogonal basis

Completing the trace, mode 2's acceleration is a classic contact-patch force
model on three basis vectors plus a rotated edge:

```c
// the basis
pfVar2 = frame+0x2dc;   // rider+0x0b40 -- surface NORMAL
pfVar1 = frame+0x35c;   // rider+0x0BC0 -- tangent A
pfVar10= frame+0x36c;   // rider+0x0BD0 -- tangent B

// the rotated edge direction (carve)
local_90 = normal * cos(edgeAngle) + tangentB * sin(edgeAngle);

// the acceleration
accel = normal      * local_ac            // suspension along the normal
      + local_90    * local_b8            // carve along the tilted edge
      + tangentA    * (F1 + F2)           // one tangential axis
      + tangentB    * F3                  // the other tangential axis
      [+ normal * -1000  when gated]      // surface stick
```
with
```c
F1 = FUN_00026ef0(this, x);                                   // float10
F2 = FUN_00027230(this, x, ..., (30/30 * local_ac) / limit);  // scaled by the normal force
F3 = FUN_000274b0(this, x, local_90, ..., scalar);            // takes the edge direction
```

`F2` being scaled by the **normal force** (`local_ac`) is the signature of a
friction term — tangential force proportional to load, exactly the
friction-circle relationship a tyre or board model uses. `F3` consuming the
rotated **edge direction** makes it the lateral/grip term.

So the model is: **suspension (normal) + carve (edge) + longitudinal force +
lateral force**, i.e. a contact-patch model with load-dependent friction. That
is the heart of the riding feel and it is now structurally understood.

### The surface-stick gate — and confirmation of `rider+0x0d04`
```c
if ((DAT_001c6bc8 == '\0') &&
    (*(int *)(*(int *)(frame + 0x4a0 + param_1) + 0x94) != 1)) {
    bVar5 = false;   // -1000 NOT applied
} else {
    bVar5 = true;    // -1000 applied along the normal
}
```
`frame+0x4a0` is **`rider+0x0d04`** — the pointer field identified earlier.
This confirms it: it points at a state object whose `+0x94` gates whether the
rider is stuck to the surface. Together with a global flag at `DAT_001c6bc8`.

### Naming held back deliberately
`FUN_00026ef0`, `FUN_00027230` and `FUN_000274b0` all return `float10` and take
the same `this`. Their *roles* in the sum are clear (two tangential
contributions and a lateral one), but their internal formulas have not been
read, so they are left unnamed rather than christened from position alone.

## `Rider_ComputeThrustForce` (0x00026ef0) — decoded (was FUN_00026ef0)

The first of mode 2's three tangential force terms, and it is the rider's
**thrust/acceleration**. Renamed from its formula, not its position.

```c
if (frame+0x234 > 0.0) return 0.0;                       // gated off entirely

fVar4  = FUN_0001e600(frame+0x1ec - frame+0x3ac);        // heading vs. travel angle
band60 = (PI/180) * 60;                                  // DAT_001875c8 = 60
band30 = (PI/180) * 30;                                  // DAT_001877c8 = 30
align  = min(1.0, (band60 - |fVar4|) / band30);          // 1.0 within 30 deg, 0 at 60

speed  = sqrt( v.x^2 + v.y^2 + v.z^2 + v.w^2 );          // v = rider+0x09f0
headroom = frame->[0x2c] * 27.7778 - speed;              // DAT_00187614
if (headroom >= 1111.11) headroom = 1111.11;             // DAT_001877c0, cap
if (headroom <= 0) return 0.0;                           // at top speed -> no thrust

stat = *(byte *)(*(int *)(frame+0x4a0) + 0xe);           // rider+0x0d04 -> a STAT byte
k = (frame+0x45c == 2) ? lerp(1.205, 1.510, stat/255)    // DAT_00187714/718
                       : lerp(0.738, 1.015, stat/255);   // DAT_00187704/708
force = k * headroom * align;

if (RiderEvent_GetSubState(0) == 0x221)                  // a special state
    force = max(force, k * headroom * 0.2);              // DAT_001875d0

return force * frame->[0x30];
```

### What this establishes
* **`DAT_00187504` = 0.00392157 = 1/255.** It normalises `bVar1`, proving that
  byte is a **0-255 character stat**. So `rider+0x0d04` points at the rider's
  **character/stat record** — consistent with its other use, gating the surface
  stick via `+0x94`.
* Thrust is `characterStat x speedHeadroom x headingAlignment`, i.e. SSX's
  per-character acceleration stat feeding directly into the physics.
* The **alignment falloff** is exact: full thrust while heading is within
  **30 degrees** of travel, falling linearly to zero at **60 degrees**. Point
  the board sideways and you stop accelerating.
* The **speed limiter** is a headroom term, capped at `1111.11` (= 4 x 277.778).
* `frame+0x45c == 2` selects a *higher* stat range (1.205-1.510 vs
  0.738-1.015) — a boost/tricky state.
* It independently **re-confirms `rider+0x09f0` is the velocity**: its four
  components are squared and square-rooted to get speed.

### Remaining
`FUN_00027230` (the load-scaled friction term) and `FUN_000274b0` (the lateral
term) are decompiled but not yet read.

## Mode 2's force model COMPLETE — and the unit question effectively settled

The last two terms are decoded and named.

### `Rider_ComputeDragForce` (0x00027230, was FUN_00027230)
A **quadratic drag polynomial** opposing motion:
```c
return -( ((c1 * p2[0xc] * L) * (|v| * k) + (L * p2[8])) * (|v| * k)
        + ((k1 * frame+0x16c + 1) * frame+0x234^2 * k2 * c2)
        + ((1 - frame+0x240) * k3)
        + (c3 * p2[4] * L) ) * modifier * v;
```
i.e. `F = -(a*v^2 + b*v + c) * v` — the classic quadratic drag shape, negated
and scaled by the velocity component so it always opposes travel.
Three **character stats** feed it, from the record at `rider+0x0d04`:
`+0x19`, `+0x11` and `+0x0e`, each normalised by `1/255` and lerped between a
mode-dependent pair.

### `Rider_ComputeLateralGripForce` (0x000274b0, was FUN_000274b0)
A **piecewise-linear, speed-dependent grip curve** with a character grip stat
(`statRec+0x13`), and a `1/(1 + 3.5*x)` softening term.

| Speed (raw) | as x277.778 | **km/h (cm reading)** | grip |
|---|---|---|---|
| 0 | 0 | 0 | 0.201 |
| 555.556 | 2 | **20** | 0.695 |
| 1388.89 | 5 | **50** | 0.997 |
| 2222.22 | 8 | **80** | 0.910 |

Low grip at a crawl, **peak at 50 km/h**, slight falloff toward 80 — a textbook
tyre/board grip curve.

### The "mismatch" penalty, shared by both
Both functions compare `frame+0x1f0` against `statRec+0x58` and, when they
**differ**, scale their coefficients by `1 - penalty`, with the penalty chosen
by `frame+0x45c`: `0.0` (mode 1), `0.150` (mode 0), `0.300` (otherwise). A
stance/board-mismatch or switch-riding handicap.

### FOURTH line of evidence for centimetres — the strongest yet
The grip curve's breakpoints are **exact multiples of 277.778** (0, 2, 5, 8) and
its segment widths are exactly 3 each. Under the centimetre reading those land
on **0, 20, 50 and 80 km/h** — round, human-authored numbers — and the curve
shape is physically correct. Under millimetres they would be 0, 2, 5 and 8 km/h,
which is both absurd for the sport and would make "peak grip" occur at walking
pace.

Lines of evidence now: the speed ladder, the ground-probe geometry
(-1 m..+2 m), and this grip curve — three surviving, independent, all agreeing.
(The earlier `-1000` "gravity" argument was **withdrawn** once `+0x0b40` proved
to be the surface normal rather than world-up.)

**Verdict: 1 world unit = 1 cm.** Not a formal proof, but three independent
round-number agreements is as strong as this kind of evidence gets, and the
port can now label its units.

### Mode 2's complete force model
```c
accel = surfaceNormal * suspensionAlongNormal        // Rider_ComputeSurfaceCompressionResponse
      + rotatedEdge   * suspensionAlongEdge          // carve split via sin/cos of the edge angle
      + tangentA      * (Rider_ComputeThrustForce + Rider_ComputeDragForce)
      + tangentB      * Rider_ComputeLateralGripForce
      [+ surfaceNormal * -1000 when the stat record's +0x94 gate allows]
```
That is the whole of SSX's ground handling: suspension, carve, thrust, drag and
lateral grip, every term driven by per-character stats.

## OPEN: who produces the two tangent basis vectors

`rider+0x0BC0` and `rider+0x0BD0` are the tangential axes mode 2 projects its
forces onto, but **none of the three ground modes writes them** — checked at the
correct per-mode offsets:

| Mode | base | `0x0BC0` seen as | `0x0BD0` seen as |
|---|---|---|---|
| 1 | `0x850` | `0x370` | `0x380` |
| 2 | `0x864` | `0x35c` | `0x36c` |
| 3 | `0x860` | `0x360` | `0x370` |

No writes at any of those. They are produced upstream, before
`Rider_DispatchPhysicsMode` runs. Ruled out so far: `Rider_UpdatePhysicsState`
(0x00036990) writes none of them, and `FUN_00026eb0` — adjacent to
`Rider_ComputeThrustForce` in address space, so a natural suspect — is only a
`DebugBuffer_Write` marker.

**Next place to look**: `Rider_UpdateSubsystems` (0x00030da0) dispatches ~18
helpers before the physics modes; the basis builder is most likely among them.
A tangent basis is conventionally built by projecting the rider's forward axis
(`rider+0x4880`, transform row 1) onto the surface plane and taking a cross
product with the normal (`rider+0x0b40`) — so the producer should read both.
Searching those ~18 for one that reads the transform *and* the surface normal
and writes a pair of vectors is the targeted way in.

Until then the port's `step()` cannot assemble the full acceleration; the force
*terms* are all transcribed and unit-tested, but the basis they project onto is
missing.

## The tangent-basis trace hits dynamic dispatch (where it stops, and why)

Followed `Rider_UpdateSubsystems` (0x00030da0) properly. Its 18 calls are almost
all **instrumentation stubs** — each writes a `0xdeadbfXX` tag via
`DebugBuffer_Write` and returns:
```c
void FUN_00021630(undefined4 *param_1) {
    *param_1 = 0x14;  param_1[1] = 0xdeadbf07;
    DebugBuffer_Write(param_1, 0x14);
}
```
(Two have real tail calls — `FUN_000299f0` -> `FUN_0005c110` — but that one is
10 lines and touches neither the transform nor any vector, so the existing note
that "only `Rider_UpdatePhysicsComponents` has surviving real logic" is
essentially right.)

`Rider_UpdatePhysicsComponents` (0x00066c40) calls **`Component_UpdateAll` three
times** — a fixed 3-substep integration — but reads no transform (`0x487x/0x488x`)
and writes no 4-component vector itself.

### Conclusion
The tangent basis is built inside an **attached component's** update, reached
through `Component_UpdateAll`'s runtime list — i.e. **dynamic dispatch through a
linked list populated at construction**, not a static call the decompiler can
follow. This project already documented that list's layout (slot `+0x50` = head,
node `+0x24` = next, from `Component_InitEmptyList`).

That makes this a different kind of search: enumerate the component types, find
which one's update writes a vector pair, rather than following calls. It is
tractable but is its own thread, so it is recorded here rather than half-done.

### What this does NOT block
Every force *term* is decoded, transcribed and unit-tested. What is missing is
only the basis they project onto. A physics sandbox can proceed by deriving the
tangents the conventional way (forward axis projected onto the surface plane,
crossed with the normal) **clearly marked as a stand-in**, and swapped for the
engine's once the component is found — the same treatment the surface normal
already has pending M4.

## FINDING: SSX Tricky has no constant gravity term

Searched exhaustively; recording the negative result because it is a real
structural fact about the game, not a gap in the investigation.

### What was searched
* Function names: no `Gravity`, no `Fall*` physics function (only `SnowFallMan`
  and `Math_EvaluateFalloffCurve`).
* All constants of physics modes 1, 2, 3, 5, 6 and `Rider_UpdateTerrainContact`.
* A sweep of **ten 2 KB blocks** across `0x00186800`-`0x00188fff` and
  `0x001a9000`-`0x001ab7ff` for any float in `900..1100` (i.e. 9.81 m/s^2 in cm,
  and neighbours). **Seven** candidates exist, and every one is accounted for:

| Constant | Value | Sole user |
|---|---|---|
| `0x00187bd4` | 1000 | `Rider_ComputeAISteering` |
| `0x00188838` | 1000 | `Rider_ApplyCollisionImpulse` |
| `0x00188b8c` | 972.222 | `Rider_UpdateSurfaceSprayFX` |
| `0x00188d48` | 1050 | `FUN_000486c0` |
| `0x00186b60` | 1088.06 | (unrelated) |
| `0x001878cc` | 1000 | mode 5, multiplied by 250.951 -> a *torque*, not an acceleration |
| `0x001aa420` | **-1000** | `Rider_PhysicsMode2_GroundContact` — the surface stick |

Also searched for `9.81`, `98.1` and `9810` earlier: the only near-hit was
`98.0286`, used by `Rider_UpdateBoardAttachmentTransforms`.

### What the game does instead
Two mechanisms, neither a constant `a = g`:
1. **On the ground**: `-1000` along the **surface normal** — the rider is pulled
   into the mountain surface, not toward world-down. This is why SSX riders hug
   terrain through rollers and transitions instead of ballistically leaving it.
2. **Airborne (mode 5)**: the velocity update is **asymmetric on Z**:
```c
*pfVar10   = *pfVar10   + fVar21 * (...)   * fVar22;
pfVar10[1] = pfVar10[1] + fVar21 * fVar16  * fVar22;
pfVar10[2] = pfVar10[2] + (fVar21 - fVar15) * fVar22;   // extra -fVar15 on Z only
pfVar10[3] = pfVar10[3] + fVar22 * 0.0;
```
The `- fVar15` applied to Z alone is the descent term. It is *computed*, not a
constant, so the fall rate is state-dependent rather than a fixed `g`.

### Why this matters for the port
A port that adds a conventional `v.z -= 9.81 * dt` would feel wrong in a way
that is hard to diagnose: too floaty on the ground (missing the normal-directed
stick) and wrong in the air (fixed instead of state-dependent). The correct
behaviour is the two mechanisms above.

`rider.cpp` currently implements (1) and omits (2); `fVar15`'s provenance in
mode 5 is the remaining piece.

## CORRECTION: mode 5's Z-asymmetry is NOT the general descent term

The previous section called mode 5's asymmetric Z update "the descent term".
That is **wrong** — reading the surrounding context shows the whole block is
gated on a specific rider substate:

```c
iVar9 = RiderEvent_GetSubState();
if ((iVar9 == 0x2dd) || (iVar9 == 0x2dc)) {          // <- only these two substates
    pfVar10 = (float *)(*(int *)(iVar3 + 0x31c + param_1) * 100 +
                        *(int *)(*(int *)(DAT_001e3c7c + 0x72c) + 0x24));
    fVar21 = *(float *)(iVar3 + 0x338 + param_1) * *pfVar10;
    fVar15 = *pfVar10;
    ...
    pfVar10[2] = pfVar10[2] + (fVar21 - fVar15) * fVar22;
}
```

So it applies to **two specific substates only** (`0x2dc`, `0x2dd`), and its
values come from a **table**: `index * 100 + [DAT_001e3c7c+0x72c]->[0x24]`,
indexed by `frame+0x31c` — a 100-byte-stride array owned by a manager object,
not a physics constant. A per-substate, data-driven effect (an updraft, a
scripted path segment, or similar), not general gravity.

**Net position on gravity, unchanged and still honest**: no constant gravity
term has been found, and the general airborne descent has *not* been located.
Mode 5 is 545 lines and only a fraction has been read; the answer may be in the
rest of it, or in the component updates reached by dynamic dispatch (the same
place the tangent basis went).

Lesson repeated: reading a few lines around a striking expression is not enough
— the gate above it decides whether the expression means anything general. This
is the second time this session an "asymmetric term" turned out to be
conditional (the first was the `-1000` read as gravity before `+0x0b40` proved
to be the surface normal).

## Component dispatch confirmed exactly; insertion still not found

`Component_UpdateAll` (0x00060a90) read directly:
```c
for (piVar1 = (int *)param_1[0x14]; piVar1 != param_1; piVar1 = (int *)piVar1[9])
    (**(code **)(*piVar1 + 0xc))(param_2);
```
So, precisely:
* list head at **`this+0x50`** (`param_1[0x14]`),
* next pointer at **node+0x24** (`piVar1[9]`),
* **circular** — terminates on returning to the head, not on null,
* dispatch is **vtable slot `0xc`** on each node.

That pins the dispatch exactly, which the previous session's note left partly
open ("the object being walked isn't tracked by the decompiler").

### The insertion site remains unfound
Searched the export for functions writing **both** `+0x24` and `+0x50` — the
attach signature. 16 hits, all explicable as false positives (those offsets are
common) except the already-known `Component_InitEmptyList` and the teardown
`BdrSeqQueue_DestroyAllAndReset`. As the earlier session reasoned, the real
inserter almost certainly takes the list head as a *parameter*, so it would
write `paramA+0x24` and `paramB+0x50` — which an offset-only scan of `param_1`
cannot see.

**This is now the single highest-value unknown in the rider physics**, because
two separate threads both terminate here:
1. the **tangent basis** producer (`rider+0x0BC0` / `+0x0BD0`), and
2. the **general airborne descent** (no constant gravity exists; mode 5's
   Z-asymmetry is substate-gated).

A targeted approach for whoever picks this up: enumerate vtables whose slot
`0xc` points into the rider physics address range (`0x00025000`-`0x0004a000`),
rather than searching for the insertion. The component *types* are findable from
their vtables even if the attach site is not.

## The component hypothesis is WRONG — and a better one

Went after the components via their vtables (scanned `0x00186000`-`0x001a8000`
for vtables whose slot `0xc` lands in the rider-physics range). The scan worked
and found the family immediately — a vtable repeating at stride **`0x58`**,
matching `Rider_ConstructComponentSlots`' loop exactly. Resolved:

| Slot | Function |
|---|---|
| `+0x00` | `Rider_UpdatePhysicsState` |
| `+0x04` | `Rider_ResetPhysicsState` |
| `+0x08` | `Node_NoOpStub2` |
| **`+0x0c`** | **`Rider_HandleComponentStateEvent`** <- what `Component_UpdateAll` calls |
| `+0x10` | `FUN_000311d0` |
| `+0x18` | `Player_GetUnitConstant` |

**So `Component_UpdateAll` invokes an *event dispatcher*, not a physics
integrator.** The components handle state events; they do not produce per-frame
vectors. The hypothesis that the tangent basis and the airborne descent live in
component updates is therefore **wrong**, and both threads that pointed here
need redirecting.

`Rider_UpdatePhysicsState` (slot 0, 242 lines) was checked too: it does
dt-scaled decay/lerp over ~15 field *pairs* and dispatches
`Rider_DispatchPhysicsMode`, and writes **no** 4-component vector.

### Eliminated so far for the tangent-basis producer
1. the three ground modes (verified at each one's correct offsets),
2. `Rider_UpdateSubsystems`' 18 calls (all `DebugBuffer_Write` stubs),
3. `Component_UpdateAll` -> `Rider_HandleComponentStateEvent` (event dispatch),
4. `Rider_UpdatePhysicsState` (decay + dispatch only).

### Better hypothesis: they are rows of matrices, not standalone vectors
`rider+0x0b40` and `rider+0x0BC0` are plausibly the **bases of two 4x4
matrices** (rows `0x10` apart), not loose vectors:

| Base | rows |
|---|---|
| `0x0b40` | `0x0B40 0x0B50 0x0B60 0x0B70` |
| `0x0BC0` | `0x0BC0 0x0BD0 0x0BE0 0x0BF0` |

This fits the evidence: mode 2 reads `0x2dc` (= `0x0B40`, row 0) and `0x2fc`
(= `0x0B60`, row 2) of the first, and `0x35c`/`0x36c` (= `0x0BC0`/`0x0BD0`,
rows 0 and 1) of the second. And **`rider+0x0B70` — already identified as the
contact normal** — is exactly row 3 of the first matrix.

So the "tangent basis" is most likely **two rows of a contact/surface frame
matrix built as a unit**, the same way `rider+0x4870` is the world transform.
That reframes the search: look for a function writing a whole 4x4 at `0x0b40`
or `0x0BC0`, not one writing two separate vectors — which is why every search
so far has missed it.

## `rider+0x0b40` is a CONTACT RECORD, written by the terrain-query path

Refining the matrix hypothesis: mode 3 writes **two** of the four candidate
rows, both gated on the terrain query, and their contents identify the struct.

**Row 3 (`rider+0x0B70`, mode3 offset `0x310`)** — the contact normal:
```c
cVar6 = Terrain_QuerySurfaceContact(&local_30, &local_20, local_70);
if (cVar6 != '\0') {
    *(... + 0x2cc + param_1) = local_3c;
    pfVar10 = (float *)(... + 0x310 + param_1);   // rider+0x0B70
    *pfVar10 = local_60; pfVar10[1] = fStack_5c;
    pfVar10[2] = fStack_58; pfVar10[3] = fStack_54;
```

**Row 0 (`rider+0x0B40`, mode3 offset `0x2e0`)** — a vector *difference*:
```c
fStack_cc = pfVar10[1] - local_e4[1];        // ... a componentwise subtraction
pfVar10 = (float *)(... + 0x2e0 + param_1);  // rider+0x0B40
*pfVar10 = local_d0; pfVar10[1] = fStack_cc;
pfVar10[2] = fStack_c8; pfVar10[3] = fStack_c4;
```

So `0x0b40..0x0b70` is **not a transform matrix** — it is a **contact record**:
a delta/penetration vector at `+0x00` and the contact normal at `+0x30`, both
populated only when `Terrain_QuerySurfaceContact` succeeds. Mode 1 writes the
same `0x0B40` after its own terrain sample, so both ground modes maintain it.

That is consistent with everything found earlier (the field being terrain-gated,
being dotted with velocity, and carrying the `-1000` stick direction) and it
supersedes the "two 4x4 matrices" guess for this base.

### Status of the two remaining unknowns
* **`rider+0x0B40` — RESOLVED**: contact record, written by both ground modes on
  a successful terrain query. The port's stand-in (a normal from the `.ltg`
  sub-cell AABBs) stands in for exactly this.
* **`rider+0x0BC0` / `+0x0BD0` — still unfound**. Five locations eliminated:
  the three ground modes, `Rider_UpdateSubsystems`' stubs, the component
  dispatch (`Rider_HandleComponentStateEvent`), and `Rider_UpdatePhysicsState`.
  Whether they are rows of a separate frame at `0x0BC0` is untested — mode 2
  reads only rows 0 and 1 of it, so the 4x4 reading is unconfirmed for that base
  too.

## `Rider_EvaluateGroundMovementTransition` = the SWITCH-STANCE flip

A corrected scan (respecting Ghidra's variable *lifetime* — it recycles
`pfVar10` heavily, and an earlier scan matched a declaration against a later
unrelated write through the same name) found 13 true 4-float writes in the
rider range. Among them, `Rider_EvaluateGroundMovementTransition`
(`0x00025bd0`) writes **`frame+0x370` and `frame+0x380`**, and it uses **slot
`0x20`** — the same frame as mode 1 — so those are exactly
**`rider+0x0BC0` and `rider+0x0BD0`**, the two tangential basis vectors.

But it does not *build* them — it **negates** them:
```c
if (*(int *)(iVar4 + 0x470 + param_1) == 2) {
  if (bVar6) {
    *(bool *)(iVar5 + 0x204) = *(char *)(iVar5 + 0x204) == '\0';   // toggle stance flag
    fVar9 = DAT_00187698;                                          // = PI
    if (stanceFlag == '\0') fVar9 = 0.0;
    Math_WrapAngleToRange(fVar9);
    FUN_0003d1e0(... + 0x1f0, PI);
    FUN_00066b90(-PI);
    // rider+0x0BC0 = (0,0,0,0) - rider+0x0BC0     <- DAT_001fae10..1c are all 0.0
    // rider+0x0BD0 = (0,0,0,0) - rider+0x0BD0
    uVar12 = (stanceFlag == statRec[0x58]) ? 0x1ff : 0x240;
    RiderAnimation_TriggerByEventCode(uVar12, 0, 5);
```
`DAT_001fae10..1c` are **all zero**, so `v = 0 - v` is a plain negation.

### What it means
This is **riding switch / fakie**: toggle the stance flag at `+0x204`, rotate
the heading by **PI** (180 degrees), **negate both tangential basis vectors** so
forward and lateral flip, and fire an animation whose code depends on whether
the new stance matches the character's natural one
(`statRec+0x58` — **the same field the drag and grip functions use for their
mismatch penalty**). Everything ties together: flipping to your unnatural stance
both plays a different animation and costs you grip and drag coefficients.

### Consequence for the hunt
`rider+0x0BC0`/`+0x0BD0` are confirmed to be a **directional basis** (negating
them flips travel direction), and one *modifier* is now identified — but the
**producer is still unfound**. The corrected scan's other 12 hits are all
accounted for by fields already identified, so the constructor is not among the
rider-range functions in the export. Remaining possibilities: a function not in
the stale export, or construction via a memcpy/matrix helper rather than four
scalar stores.

## Two more angles closed on the `0x0BC0` producer

**Helper-fill ruled out**: searched every rider-range function for the basis
address being *passed* to a helper (`Foo(&rider[0xBC0], ...)`) under any of the
three known frame bases — **zero** sites. It is not filled by a callee.

**Reset-time init, inconclusive**: `Rider_ResetPhysicsState` (vtable slot 1)
initialises two consecutive `vec4`s to **`(0,0,0,1)`**:
```c
*(param_1 + 0x380) = 0;  *(param_1 + 0x384) = 0;
*(param_1 + 0x388) = 0;  *(param_1 + 0x38c) = 0x3f800000;   // 1.0
*(param_1 + 0x390) = 0;  *(param_1 + 0x394) = 0;
*(param_1 + 0x398) = 0;  *(param_1 + 0x39c) = 0x3f800000;   // 1.0
```
`(0,0,0,1)` is the **identity quaternion**, so these are more likely rotation
quaternions than a tangent basis — and the function never touches `0x370`.
Critically, `Rider_ResetPhysicsState` is reached as a *component* vtable slot,
so its `this` is a component object, **not** demonstrably mode 1's frame. The
offsets therefore cannot be equated without establishing that `this` first.

### Honest status
The producer of `rider+0x0BC0`/`+0x0BD0` is **not found** after seven distinct
approaches:
1. the three ground modes (per-mode offsets), 2. `Rider_UpdateSubsystems`'
stubs, 3. the component dispatch, 4. `Rider_UpdatePhysicsState`,
5. a lifetime-correct 4-float-write scan of all 419 rider-range functions,
6. helper-fill (address passed as an argument), 7. reset-time initialisation.

What *is* known: they are a **directional basis** (negated on a switch-stance
flip), they are read by mode 2 as the tangential axes, and nothing in the
export's rider range constructs them. The most likely remaining explanation is
that they are written by a function **absent from the stale `default.xbe.c`
export** — the same trap that hid the entire `StartScreen_*` family earlier in
this project. Re-running the search against a *fresh* export, or over live
Ghidra function-by-function, is the next concrete step.

## `rider+0x0BC0`/`+0x0BD0` — three more eliminations (2026-07-28, live Ghidra)

Re-ran the search against **live Ghidra** rather than the stale export, which
was the recorded next step.

### 1. Mode 2 never writes them — now PROVEN, not inferred
Full disassembly of `Rider_PhysicsMode2_GroundContact` (0x000276a0-0x0002870a).
Every one of the eight references to the tangent offsets is a **load**:
```
0002771a: MOVAPS XMM2, [EAX + 0x35c]
00027745: MOVAPS XMM0, [EAX + 0x36c]
000278a7: MOVAPS XMM1, [EAX + ESI + 0x36c]
000279ae: MOVAPS XMM4, [ECX + ESI + 0x36c]
000279b6: MOVAPS XMM5, [ECX + ESI + 0x35c]
00027dc7: MOVAPS XMM0, [ECX + 0x35c]
00027f04: MOVAPS XMM2, [EBX + 0x35c]
000286c4: LEA    ECX, [EDX + ESI + 0x35c]      <- the only non-load
```
No `MOVAPS [mem], XMM` anywhere. **The tangents are persistent state that
mode 2 only consumes**, which combined with the switch-stance negation writing
them *in place* means they survive across frames.

### 2. The one LEA site is a READ, so "helper-fill ruled out" survives
`LEA ECX,[EDX+ESI+0x35c]` then `CALL 0x0001e750`. That callee reads `this`,
computes `x^2+y^2+z^2+w^2` and `rsqrtps` — it is **Vec4_Normalize**, writing
its result to the *second* argument (a stack buffer), not back through `this`.
So a call site passing the address does exist (the earlier note said none did),
but it does not fill the field.

### 3. The 4x4-matrix hypothesis is DEAD
It was proposed that `0x0BC0` is the base of a 4x4 (rows 0x10 apart) holding
the rider's orientation. The rider's world-transform axes are at
**`+0x4870`/`+0x4880`** — nowhere near `0x0BC0`. Different structures.

### 4. Byte-search on mode 1's displacements: all false positives
Mode 1's frame reaches the pair at `+0x370`/`+0x380`
(`Rider_EvaluateGroundMovementTransition`). Searching those disp32 patterns
across the image and resolving containing functions gives
`Rider_FollowAIPath`, `Rider_SelectBestAIPathZone`,
`Rider_UpdateTrackPathPosition` and `FUN_00030f70` — but all of those use
**`rider+0x370`**, the spline-path distance field written from
`SplinePath_FindClosestPoint`, an entirely different structure that merely
shares the displacement. The remaining hits are table initialisers.

### Status: ~10 approaches, static analysis not converging
This now needs a watchpoint on `rider+0x0BC0` under a debugger (xemu), which is
outside what this project can do statically. What IS pinned down are the
constraints the pair must satisfy:

1. they form an orthogonal basis with the contact normal (the force model uses
   all three as one);
2. the carve direction is `normal*cos + tangentB*sin`, so **tangentB is the
   LATERAL axis** — the one you tilt toward;
3. therefore **tangentA is longitudinal**, along the board;
4. riding switch **negates both**, so they are board-aligned and flip when the
   rider reverses.

The port's derivation — forward projected onto the surface plane, then crossed
with the normal — satisfies all four. It is recorded as a **documented
deviation** rather than a stand-in: structurally consistent with every
statically observable constraint, but never compared numerically against the
engine, and derived per-frame where the engine's persist.
