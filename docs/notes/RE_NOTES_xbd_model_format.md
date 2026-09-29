# RE notes: model geometry formats (`.mxf` rider/board, `.xbd` track) + residue closures

**2026-07-22**, per explicit user request to handle the port-plan gap list.
This file covers the render-geometry formats (the one real gap) and the
three minor residue items, each closed to the depth honestly achievable.

## `.mxf` — rider/board/head models (`char\mdlxbx.big`, `brdxbx.big`) — directory level DECODED

27 files extracted. Verified top-level layout (consistent across body/head/
board files; parser `scripts\classify_mxf.py` runs clean on all tested):

- Header: `u32 lodLevelCount` (4), `u32 version` (`0xC0004`–`0xC000A`
  observed), `u32 entryTableEnd` → entryCount = `(end-0xC)/0x18C`.
- `0x18C`-byte entries: `char name[16]`, `u32 dataOffset` (relative to
  table end, entries contiguous — verified: each offset equals the
  previous offset+size exactly), `u32 dataSize`, `u32 groupCount`
  (0xA0/0x80/0x20, scales with LOD — bone/vertex-group count,
  unconfirmed), packed small counts near the entry tail (tri/material
  shaped).
- The names alone are informative: bodies ship 3 LODs by polygon budget
  (`Body3000/1500/750`) + a shadow mesh (`BodyShdw750`, size 0 in the
  files checked — likely built at runtime); heads split into
  `Eyes/Face/Hair` sub-parts per LOD; the shared board file holds 3 board
  models × regular/goofy stance (`Al`/`AlGoofy`/`Bx`/`Fr`...).
- **No bone-name strings anywhere** — bones are index-based. So the
  `.afl` channel→bone question resolves structurally: companion channel
  groups map to bone *indices* in skeleton order; the concrete
  index→bone-position mapping lives in the geometry interior (below) and
  can also be recovered empirically at M4 by rendering candidate
  assignments (a standard step in every skeletal-format RE).

**Geometry interior deliberately not field-decoded this pass** — vertex/
skinning layouts are the port's M4 milestone task; the directory level
above is what M0's asset pipeline needs.

## `.xbd` — track model database (`models\<track>.big`) — header level

`gari.xbd` (5.97MB): header of count fields (0xF2D, 0xD41, 0x288, 0x3AE,
0xA9… — thousands-scale counts consistent with meshes/instances/materials)
plus **four ascending section offsets** at header dwords 20–23 (`0xA0`,
`0x2AAF30`, `0x3DA770`, `0x4C0FC0`). Loaded via `FILE_loadpack` from
`FUN_000b0660` (a 10-slot model-bank manager on `Level_LoadTrackAssets`'s
path), parsed by a vtable method, then patched by `FUN_000b03c0` — a
**texture-slot remap fixup** that rewrites 16-bit texture indices in two
already-loaded runtime tables through a remap array — confirming `.xbd`
references textures by index into the track's `.xsh` sheets. No strings
inside (pure binary geometry). Interior decode = M4, same as `.mxf`.

## Residue closures

- **Audio sample encoding — IDENTIFIED, standard format.** The audio
  `.big` archives are plain **BIGF** containers of **`.bnk` banks with
  the `BNKl` v5 magic** — EA's standard cross-title sound-bank format of
  that era, with `PT`/EACS platform headers (both found by direct byte
  inspection). Publicly documented and supported by vgmstream/sx — the
  port can use existing decoders. The earlier "known EA ADPCM family"
  assumption is now confirmed by inspection, not assumption.
- **`.xsh` format byte `0x7b` — RESOLVED.** The `spt1` spotlight entries
  (16×64) carry exactly `16·64·4 + 8·32·4 + 8` bytes = **32bpp with one
  mip level plus an 8-byte `0xFF` terminator** — arithmetically exact,
  two independent ways. The earlier "~10bpp discrepancy" came from
  measuring against the wrong denominator. Runtime loader for this
  lightmap path still untraced (unchanged); the data layout is no longer
  ambiguous.
- **`.cml` composite cells — CHARACTERIZED.** The 44-byte named cells are
  a **serialized dump of the authoring tool's live object graph**: inline
  ASCII names, `0xDEADC0ED` for never-written fields, and literal stale
  tool-side heap pointers (`0x7E74xxxx` values, far outside file bounds,
  with heap-like locality) that the runtime discards and rebuilds by
  name. This explains *why* the format looked like a "sparse runtime
  object template" — it literally is one. A port needs only the names +
  the already-decoded keyframe cells; the pointer fields are dead weight.

**Port-plan impact**: gap list narrows to a single well-scoped M4 task
(geometry interiors of `.mxf`/`.xbd`, with directory/count scaffolding now
in place) — everything else on the residue list is closed.

## `.xbd` TABLE DIRECTORY — decoded and arithmetically verified (2026-07-27)

Recovered from `Ltg_FixupCellAndResolveIndices` (`0x0013af90`), which converts
the `.ltg`'s index arrays into pointers using `base + index * stride`, with both
the base and the bound read from the `.xbd` header. That gives the exact
(count offset, base offset, stride) triples — previously the header was only
known as "count fields plus four ascending section offsets".

### The five tables, verified against `gari.xbd` (5,970,692 bytes)
Each row was confirmed by `base + count * stride == the next section offset`,
**exactly**, with no slack:

| Table | base field | base | count field | count | stride | end | == next |
|---|---|---|---|---|---|---|---|
| A | `+0x50` | `0xa0` | `+0x08` | 3885 | `0x2d0` (720) | `0x2aaf30` | `+0x54` ✓ |
| B | `+0x5c` | `0x4c0fc0` | `+0x14` | 3393 | `0x90` (144) | `0x538450` | `+0x60` ✓ |
| C | `+0x60` | `0x538450` | `+0x18` | 10 | `0x70` (112) | `0x5388b0` | `+0x64` ✓ |
| D | `+0x74` | `0x552eb0` | `+0x2c` | 542 | `0x80` (128) | `0x563db0` | `+0x78` ✓ |
| E | `+0x6c` | `0x53c1bc` | `+0x24` | 942 | `0x5c` (92) | `0x551444` | `+0x70` ✓ |

Five independent exact matches — the directory structure is certain. Sections
are contiguous, so the header's ascending offsets are section *boundaries*.

Which `.ltg` list maps to which table:
| `.ltg` cell field | count field | table |
|---|---|---|
| `+0x3c` | `+0x24` | A (`0x2d0`) |
| `+0x40` | `+0x26` | B (`0x90`) |
| `+0x50` | `+0x2e` | C (`0x70`) |
| `+0x44` | `+0x28` | D (`0x80`) |
| `+0x48` | `+0x2a` | E (`0x5c`) |
| `+0x4c` | `+0x2c` | E (`0x5c`) |

Out-of-range indices resolve to **null**, not clamped.

### Interior: still M4, and honestly so
`Terrain_QuerySurfaceContact` treats a resolved record as
`float *p` with `p[0x16]` (+0x58) a pointer to a sub-struct (material byte at
its `+0x18`) and an AABB at `p[0x17]`/`p[0x1a]` (+0x5c/+0x68). **Those do not
validate against the file**: in `gari.xbd` record 0, `+0x58` is a plain float
(921.47), and `+0x5c`/`+0x68` are not a valid min/max pair. A scan of every
4-byte offset in a 720-byte record found no position where `min <= max` holds
consistently (best: 12 of 40 records). So those fields are **populated at
runtime**, not read from disk — the record is built, not mapped.

Decoding that build step is the `.xbd` interior task, i.e. M4 as originally
scheduled. The directory above is the prerequisite and is now done.

## M4 IN PROGRESS — the collision chain decoded, and a CORRECTION (2026-07-28)

### CORRECTION: `Terrain_QuerySurfaceContact` walks table D, not table A
The previous section concluded the record fields "do not validate against the
file ... populated at runtime, not read from disk". **That conclusion was drawn
against the wrong table.** It tested table A (stride `0x2d0`); the query
actually walks table **D** (stride `0x80`).

The chain, traced rather than assumed:
* `Terrain_QuerySurfaceContact` reads `subObject+0x3c` with count
  `subObject+0x2a`, dereferencing each entry as a pointer.
* `Ltg_RelocateSubObjectPointers` only rebases sub-object pointers; it never
  resolves indices. So the entries must already be pointers, i.e. the array
  lies inside one of the CELL arrays that `Ltg_FixupCellAndResolveIndices`
  did resolve.
* Measured on `gari.ltg`: **all 238 sub-object `+0x3c` arrays lie inside the
  cell's `+0x44` array** — which resolves to table D (`index*0x80 + xbd[0x74]`).
  A first guess that they were slices of the cell's `+0x3c` (table A) array
  scored 0/238 and was discarded.

Against the file, table D now validates completely:

| Check | Result |
|---|---|
| AABB `min <= max` on all 3 axes | **542 / 542** (the table-A scan managed 12 of 40) |
| `w` components are `(1,0,0,0)` homogeneous | **542 / 542** |
| cubic curve lies inside the stored AABB | **542 / 542** (13 exceed by <= 0.021 cm, float noise) |

### The D record (stride `0x80`)
```
+0x00  float4 c3      cubic coefficient (t^3), w = 0
+0x10  float4 c2      quadratic          (t^2), w = 0
+0x20  float4 c1      linear             (t),   w = 0
+0x30  float4 c0      constant                  w = 1   <- a homogeneous POINT
+0x58  u32    groupIndex -> table F  (resolved to a pointer at load)
+0x5c  float3 aabbMin
+0x68  float3 aabbMax
```
`Terrain_QuerySurfaceContact` evaluates `p(t) = c0 + c1*t + c2*t^2 + c3*t^3`
4-wide in SIMD, sampling `t = i/DAT_001878c4` for `i = 1..`, i.e. **4 samples**
(`DAT_001878c4` = 4.0, the same constant TrickCombo uses for its big-air
threshold). This is the "Bezier-edge math" the M2 milestone anticipated.

For **416 of 542** records the AABB is the tight bound of that curve (<=0.05 cm);
the other 126 have a strictly larger box. What accounts for the excess is **not
established** — a surface width around the spine is the obvious candidate but
is not confirmed, so the port must not assume it.

### NEW: table F, the collision group (base `xbd+0x70`, count `xbd+0x28`, stride `0x28`)
Found via `Resource_RelocateBlockPointers` -> world-vtable slot `0x6c`
(`FUN_0013b440`), which walks `count` records of stride `0x28` converting each
`+0x20` field from an index into a table-D pointer.

```
+0x00  float3 aabbMin
+0x0c  float3 aabbMax
+0x18  u32    flags     <- bit 0 tested by the query; ZERO in every shipped
                           file, so it is set at runtime (see below)
+0x1c  u32    ?         (1 on the records inspected)
+0x20  u32    firstD -> table D (resolved to a pointer at load)
+0x24  u32    ?         (0xffffffff on the records inspected)
```

Verified on `gari.xbd` (169 groups, 542 D records):

| Check | Result |
|---|---|
| `F[+0x20]` in range `[0, Dcount)` | 169 / 169 |
| `D[+0x58]` in range `[0, Fcount)` | 542 / 542 |
| every group's D members form a **contiguous run** | 169 / 169 |
| that run starts exactly at `F[+0x20]` | 169 / 169 |
| every D record belongs to exactly one group | 542 / 542 |
| `F` AABB == union of member `D` AABBs | 167 / 169 (worst 52.65 cm) |

The 2 mismatching groups are the same phenomenon as the 126 loose D boxes.

Layout is contiguous: F ends at `0x552eac`, D begins at `0x552eb0` — a 4-byte
alignment gap.

### The one genuinely runtime-populated field
`F+0x18`'s bit 0 gates the whole collision test, and it is **0 in all 169
shipped records**. So it is set at load or per-frame (an activation/streaming
flag), not stored. This is the only field in the chain that is not in the file
— a port can simply default it to enabled. The prior "built, not mapped"
conclusion was right about *a* field, wrong about the record as a whole.

### Which table is the GROUND surface: table A
Reference density across `gari`'s 243 populated cells:

| Table | stride | records | refs | refs/cell |
|---|---|---|---|---|
| A | `0x2d0` | 3885 | 3885 | **16.0** |
| B | `0x90` | 3393 | 3393 | 14.0 |
| C | `0x70` | 10 | 10 | 0.0 |
| D | `0x80` | 542 | 542 | 2.2 |
| E (`+0x48`) | `0x5c` | 942 | 0 | unused in gari |
| E (`+0x4c`) | `0x5c` | 942 | 92,728 | 381.6 |

A, B and D are each referenced **exactly once** — they are partitioned across
cells, not shared. E is massively shared (92,728 refs to 942 records), the
signature of a palette rather than geometry.

**Table A is one record per `.ltg` sub-object**: 243 cells x 16 sub-objects =
3,888, against 3,885 records — exactly 16.0 refs per cell. That makes A the
per-sub-object surface patch, i.e. the ground. Decoding its 720-byte interior
is the remaining M4 step and the thing that unblocks M2's ground stand-in.

### Edge chains: `D+0x50`/`+0x54` are NEIGHBOUR LINKS, not floats
World-vtable slot `0x70` (`FUN_0013b490`, created this session after checking
the boundary — `RET 0xC` + nine `0x90` pad bytes precede it) walks the D table
and resolves three fields per record:

```
+0x50  i32  prev  ( -1 -> null, else index*0x80 + Dbase )
+0x54  i32  next  ( -1 -> null, else index*0x80 + Dbase )
+0x58  u32  group (           index*0x28 + Fbase )
```

**This corrects a reading error made earlier in this same session**: dumping
the record as floats showed `NaN` at `+0x50`/`+0x54`, which looked like unset
runtime data. They are `-1` sentinels in an integer field — `0xffffffff` read
as a float is a NaN. Always check the fixup before typing a field.

So a group is a **chain of cubic segments** — a rail/lip/edge, exactly the
"Bezier-edge math" the M2 plan anticipated.

### Verified across all 10 tracks (5,476 edges in 1,531 chains)

| Invariant | Result |
|---|---|
| `c0` is a point, `c1/c2/c3` directions (`w` = 1,0,0,0) | **5476 / 5476** |
| AABB `min <= max` | **5476 / 5476** |
| cubic curve lies inside its own AABB | **5476 / 5476** (worst excess **0 cm**) |
| `next`/`prev` back-links symmetric | **3945 / 3945** |
| linked pairs share a group | **3945 / 3945** |
| group members are one contiguous run | **1531 / 1531** |
| chain heads == chain tails == group count | on **every** track |
| `edges - chains == internal links` | **3945 == 3945**, exact |
| curve end -> next curve start | worst gap **0.035 cm** over 3,945 links |
| group flag bit 0 set in the file | **0 / 1531** (runtime, as expected) |

The chain arithmetic closing exactly (5476 - 1531 = 3945) plus sub-millimetre
geometric continuity across every link on every track makes this decode
certain, not merely plausible.

Per-track edge counts: alaska 213, aloha 454, elysium 442, gari 542,
megaple 341, merquer 980, mesa 481, pipe 571, snow 1088, untrack 364.

Ported: `port/src/assets/xbd.{h,cpp}`; asserted in `port/tests/asset_test.cpp`.

### Still open: table A (the ground)
Record 0's 720 bytes show a clear shape but the reader has not been found yet,
so nothing below is committed to:
* `+0x000..+0x04f` — 20 small floats (`0`, `0.0625` = 1/16, `±1.0`)
* `+0x050..+0x14f` — **16 consecutive `float4`s, every one with `w == 1`**
* `+0x150..+0x17f` — mixed floats and small ints (`4,4,4,0x17`)
* `+0x180..+0x1bf` — 4 more `w == 1` float4s, the first identical to the one
  at `+0x140`
* `+0x1c0..+0x2cf` — 136 packed `u16` values

Sixteen `w == 1` points is suggestive of a 4x4 bicubic control net, and 1/16
would fit a normalised step — but the D record proved that eyeballing field
types is unreliable, so the next step is to find A's reader, not to fit a
model to the dump.

## The sub-object -> table map, complete (2026-07-28)

Every `.ltg` sub-object pointer field, resolved by testing which cell array
each one points into across all 243 populated cells of `gari`. No ambiguity —
each field lands in exactly one array, 100% of the time:

| sub-object field | pointers | cell array | table | stride |
|---|---|---|---|---|
| `+0x34` | 2033 | `+0x3c` | **A** | `0x2d0` |
| `+0x38` | 1108 | `+0x40` | **B** | `0x90` |
| `+0x3c` | 238 | `+0x44` | **D** | `0x80` |
| `+0x40` | 0 | — | (unused in gari) | — |
| `+0x44` | 3888 | `+0x4c` | **E** | `0x5c` |
| `+0x48` | 10 | `+0x50` | **C** | `0x70` |

`+0x44` having exactly 3,888 pointers (243 cells x 16 sub-objects) means every
sub-object carries an E reference — consistent with E being the shared palette.

## `Terrain_SampleHeightAt` (0x0013f480) — the real ground sampler
Walks cells, then the 16 sub-objects per cell (`pfVar13 + 0x13` = stride
`0x4c`, bounded by `TerrainGrid+0x48`), then **two** inner loops:

1. `subObject+0x34` -> **table A**, count `subObject+0x24`
2. `subObject+0x38` -> **table B**, count from a separate local

### Table A is the file-resident ground surface — confirmed by its reader
| Field | Meaning | Verified |
|---|---|---|
| `+0x150` / `+0x15c` | world AABB min / max | **3885 / 3885** `min <= max` |
| `+0x168` | surface type id | 9 distinct (3, 1, 0, 9, 4, 5, 18, 10, ...) |
| `+0x2c8` | sign-tested gate | — |

The sampler skips records whose `+0x168 == 0x11` (17), and returns `+0x168` to
the caller as part of the contact result — so it is the **surface type** the
rider physics consumes.

### Table B is NOT file geometry
Its path is gated on `(*(byte*)(rec+0x68) & 0x20)` and dereferences
`*(int*)(rec+0x6c) + 0xc`. In `gari.xbd`: **bit 5 is set in 0 of 3393 records**
and `+0x6c` is **null in all 3393**. So B is populated by runtime registration
(dynamic props / moving geometry), not read from disk. Its AABB at
`+0x4c`/`+0x58` is nonetheless valid in-file (**3393 / 3393**).

This supersedes a mid-session misreading that briefly called B the ground on
the strength of the sampler reading `+0x38`; the gate values settle it.

### A's geometry payload: 16 vectors, space UNKNOWN
`FUN_0013a280(cache, aRecord, out)` reads the record at `+0x50 .. +0x14f` as
**exactly 16 vec3s** (48 float loads, the `w` of each `float4` discarded), and
memoises a **0x734-dword (7,376-byte)** derived structure per record — the
signature of a tessellation step.

`w == 1.0` on all **62,160** of those float4s across `gari`.

**What they are is NOT established.** Three hypotheses were tested and all
failed decisively:

| Hypothesis | Result |
|---|---|
| the 16 are world points bounded by the record AABB | **0 / 3885** |
| the AABB sits inside their hull (Bezier convex-hull property) | **110 / 3885**, worst breach 137 m |
| net corners 0/3/12/15 interpolate the surface corners | **0 / 3885** |
| the 4 float4s at `+0x180` are 4 of those control points | **0 / 3885** |

So the 16 vectors are not in the same space as the record's world AABB. A
4x4 bicubic control net remains the most natural reading of "16 vectors +
a 7 KB tessellation cache", but it is a guess and is recorded here as one.
The next step is to read `FUN_0013a280`'s arithmetic, not to fit more models
to the dump.

## TABLE A DECODED — the ground surface is a bicubic patch in the POWER basis

`FUN_0013a280`'s arithmetic settles it. The function builds a basis table
lazily (guarded by `DAT_001c23d4`, a one-shot init flag) and the stored
constants decode to **exactly `(u^3, u^2, u, 1)` for `u = i/9`, `i = 0..9`** --
worst deviation `3.3e-08`, i.e. float precision:

| i | u | stored | (u^3, u^2, u, 1) |
|---|---|---|---|
| 0 | 0.0000 | 0, 0, 0, 1 | 0, 0, 0, 1 |
| 3 | 0.3333 | 0.037037, 0.111111, 0.333333, 1 | same |
| 9 | 1.0000 | 1, 1, 1, 1 | same |

So the evaluation is a tensor-product bicubic:

```
S(u,v) = sum_j ( sum_i  P[j][i] * u^(3-i) ) * v^(3-j)
```

with the 16 vectors at `+0x50..+0x14f` read as 16 vec3s (48 float loads, `w`
discarded). **They are polynomial COEFFICIENTS, not control points** -- index 0
is the cubic term and index 3 the constant, the same descending order the Edge
record uses. That is precisely why the Bezier hypotheses failed: a power basis
has no convex-hull property and does not interpolate its corners.

### Why the tessellation resolution is certain
The memo cache is `0x734` dwords per record, and three independent boundaries
land exactly on it:
```
  4   + 10*10*4 =  404 = 0x194     the 10x10 point grid (w forced to 1.0)
  404 + 81*16   = 1700 = 0x6a4     the 9x9 quads between those points
  1700+  9*16   = 1844 = 0x734     == the declared cache stride
```

### Verified across all 10 tracks — 28,484 patches

| Check | Result |
|---|---|
| AABB `min <= max` | **28484 / 28484** |
| patch evaluated on the engine's own 10x10 grid lies inside its AABB | **28312 / 28484 (99.4%)**, worst breach **4.8 cm** |
| AABB tight on that grid (<= 1 cm) | 8547 / 28484 |

Per-track patch counts: alaska 3449, aloha 2871, elysium 4268, gari 3885,
megaple 730, merquer 2731, mesa 2448, pipe 2332, snow 1653, untrack 4117.

The residual 0.6% overshoot by at most 4.8 cm -- against patches tens of metres
across -- is consistent with the exporter computing the box at a different
tessellation. Compare the failed hypotheses, which missed by up to **137 m**.

### Surface type (`+0x168`)
18 distinct values across the shipped tracks:
`0(x2834) 1(x5343) 2(x1372) 3(x6281) 4(x2045) 5(x4792) 6(x187) 7(x39) 8(x42)
9(x1147) 10(x3187) 11(x11) 12(x223) 13(x104) 16(x10) 17(x80) 18(x767) 19(x20)`

`Terrain_SampleHeightAt` skips `0x11` (17) -- and exactly **80** patches carry
it, so the skip branch is real and exercised, not dead code. This is the field
the sampler returns to the caller as the contact's surface id, i.e. what the
rider physics reads as surface material.

Ported: `xbd::Patch` in `port/src/assets/xbd.{h,cpp}` with `eval(u,v)`;
asserted in `port/tests/asset_test.cpp`.

**This retires M2's ground-surface stand-in** -- the port can now sample the
engine's real surface instead of `.ltg` sub-cell AABBs.

## Table A record — the render-side fields (2026-07-28)

Three more fields resolved, each by a closed test rather than by eyeballing.

### `+0x00..+0x4f` — texture atlas mapping
```
+0x00  float2 uvOffset    always a multiple of 1/16  (3885/3885)
+0x08  float2 uvScale     ALWAYS exactly (1/16, 1/16) -- 1 distinct value
+0x10  float4[4]          corner UVs (0,0) (1,0) (0,-1) (1,-1), constant
```
256 distinct `uvOffset` values, each used ~16 times: a **16x16 texture atlas**,
with `uvScale` pinning the tile size. Note the negative v on two corners -- the
V axis is flipped relative to the U axis.

### `+0x17c` — the owning `.ltg` cell index
Range 23..712 against gari's 23x31 = **713**-cell grid. Verified:
* all 209 distinct values are **populated** cell indices (0 outside the set);
* **every patch's AABB lies inside its claimed cell's AABB in XY --
  3885 / 3885, worst overhang 0 cm.**

A zero-overhang result over 3,885 records is a geometric proof, not a
correlation. 34 populated cells are never referenced (edge-only cells).

### `+0x180..+0x1bf` — a cached copy of the patch corners
Four `float4` (w=1) that are exactly `S(u,v)` evaluated at the corners:
**3885 / 3885, worst error 0.023 cm.** Slot order is
`(0,0) (0,1) (1,0) (1,1)` -- u outer, v inner. Recovered by testing all 24
permutations; the winner beat the rest by three orders of magnitude.

### `+0x16c..+0x17b` — constant `(4,4,4,4)`
Identical in all 3,885 records. Structurally mapped, meaning unknown; a
per-edge tessellation level is the obvious guess and is NOT claimed.

### `+0x1c0..+0x2c7` — a 4-entry header plus TWO 8x8 grids
```
+0x1c0  u16  a        52 distinct, range 12..119
+0x1c2  u16  b        52 distinct, range 0..51   (same cardinality as `a`)
+0x1c4  u16  sub      16 distinct, range 0..15   -- the sub-object index
+0x1c6  u16  0xffff   sentinel, 3885/3885
+0x1c8  u16[64]       an 8x8 grid, values 0..4096
+0x248  u16[64]       a second 8x8 grid, values 0..4096
```
Both grids top out at exactly **4096 = 2^12**, so they are 12-bit fixed-point,
i.e. normalised to [0,1]. `sub` having 16 values complements `uvOffset`'s 256.

**What the two grids MEAN is not established.** A first read of record 0 looked
monotonic (one grid descending, the other ascending) but that is a property of
that record only: rows are non-increasing in just **406 / 3885** and
non-decreasing in **412 / 3885**. Recorded here so nobody repeats the mistake.

### Coverage
Structurally mapped: **716 of 720 bytes** (only `+0x2cc` is untouched).
Semantically resolved: about **452 of 720** -- the atlas mapping, the bicubic
coefficients, the AABB, the surface type, the cell back-reference, the corner
cache and the gate. The two 8x8 grids and the `(4,4,4,4)` block are mapped but
not understood.

### CORRECTIONS from validating across all 10 tracks
Two claims above were made from `gari` alone and did not survive. The port's
own test caught both.

1. **`+0x1c4` is NOT the sub-object index.** In gari it spans exactly 0..15
   (16 distinct), which made "which of the 16 sub-objects" irresistible.
   **Elysium spans 0..16 — 17 distinct values**, so it cannot index 16
   sub-objects. Per-track ranges: alaska 0..13, pipe 0..9, gari 0..15,
   elysium 0..16. Reverted to `unk1c4`, meaning unknown.

2. **The 8x8 grids' 12-bit reading SURVIVES, with a sentinel.** The naive
   maximum across all tracks is 65535, not 4096 — but excluding `0xffff` the
   maximum is **exactly 4096 on every track checked**. So `0xffff` is a
   "no value" sentinel (0 in gari, 6 alaska, 16 elysium, 152 pipe) and the rest
   is genuine 12-bit fixed point. Gari having zero sentinels is why the first
   pass missed this.

Everything else held across all **28,484 patches** in the 10 shipped tracks:

| Check | Result |
|---|---|
| `uvOffset` a multiple of 1/16 | **28484 / 28484** |
| `uvScale` exactly (1/16, 1/16) | **28484 / 28484** |
| cached corners == `eval()` at the corners | **28484 / 28484** (worst 0.031 cm) |
| patch AABB inside its `+0x17c` cell, in XY | **28484 / 28484** |
| 8x8 grid maximum, sentinel excluded | **exactly 4096** |

## Table E — field map, two fields resolved (2026-07-28)

Table E (base `xbd+0x6c`, count `xbd+0x24`, stride `0x5c`) is the **shared**
table: 942 records in gari carrying 92,728 references, i.e. a palette rather
than per-cell geometry. Every sub-object holds an E reference (`+0x44`, exactly
3,888 = 243 cells x 16).

Field map from a per-dword distinct-value census over all 942 gari records:

| Offset | Distinct | Reading |
|---|---|---|
| `+0x00` | 4 | type enum (0..3) |
| `+0x04` | 3 | flags: 0, 0x100, 0x200 |
| `+0x08` | 1 | constant `1.0f` |
| `+0x0c` | 1 | constant 0 |
| `+0x10..+0x18` | 27-30 | vec3, -363 .. 3500 |
| `+0x1c..+0x24` | 48-49 | **UNIT VECTOR** |
| `+0x28..+0x30` | 405-792 | **AABB min** |
| `+0x34..+0x3c` | 410-794 | a third vec3 (NOT the centre) |
| `+0x40..+0x48` | 1-794 | **AABB max** |
| `+0x4c` | 13 | scalar 0 .. 0.954, includes 0.70711 |
| `+0x50` | 3 | enum (0..2) |
| `+0x54` | 12 | scalar 0 .. 70 |
| `+0x58` | 1 | constant 0 |

### Verified across 4 tracks (1,579 records: gari 942, alaska 267, elysium 218, pipe 152)
* `+0x1c..+0x24` is unit length: **1579 / 1579**, zero exceptions.
* The AABB pairing is `+0x28..+0x30` (min) against `+0x40..+0x48` (max):
  **1579 / 1579**. The other candidate pairing, `+0x28` against `+0x34`,
  scores **0 / 1579** — so `+0x34..+0x3c` is a third independent vec3.

### Two hypotheses tested and REJECTED — do not retry
* `+0x34..+0x3c` as the AABB centre (the `{min, max, centre}` idiom the `.ltg`
  uses): **0 / 1579**, worst error 2.8e5 cm.
* the unit vector as a plane normal with `+0x4c` as its plane constant, tested
  through the third vec3: **0 / 1579**.

A unit vector plus an AABB plus two enums plus a cosine-shaped scalar reads
like an oriented volume or a directional trigger, but nothing here is claimed
beyond the two verified fields.

## THE RENDER MESH FOUND — and the `.xbd` header is fully regular (2026-07-28)

### The header enumerates 15 tables, not 7
Every table's base field is **`countField + 0x48`**. Counts run `+0x08..+0x44`,
bases `+0x50..+0x8c`. Solving `base[i] + count[i]*stride == base[i+1]` in
address order recovers each stride, and these come out **EXACT on every track**:

| count | base | stride | contents |
|---|---|---|---|
| `+0x08` | `+0x50` | `0x2d0` | **A** — ground patches |
| `+0x0c` | `+0x54` | *count 1* | **the render-mesh blob** (see below) |
| `+0x10` | `+0x58` | *count 1* | a second large blob (vertex-like, unit normals) |
| `+0x14` | `+0x5c` | `0x90` | **B** — runtime-registered |
| `+0x18` | `+0x60` | `0x70` | **C** |
| `+0x1c` | `+0x64` | `0x48` | **G** — materials |
| `+0x20` | `+0x68` | `0x8` | small paired records |
| `+0x24` | `+0x6c` | `0x5c` | **E** — the shared palette |
| `+0x28` | `+0x70` | `0x28` | **F** — edge groups |
| `+0x2c` | `+0x74` | `0x80` | **D** — edge curves |
| `+0x34` | `+0x7c` | `0x4` | a u32 index array, same count as `+0x20` |

### Table G = materials, with FOUR texture indices each
From `FUN_000b03c0`, the texture-index remap: it walks all `xbd[0x1c]` records
of the table at `xbd[0x64]` (via `FUN_0013adb0`, which returns
`base + i*0x48`) and rewrites the **four leading shorts** through a remap
array, skipping any that are negative. So they are `.xsh` texture indices with
**-1 = unused**. Across the 10 tracks: 1,159 materials, and **3,479 of 4,636
slots are -1** -- slot 3 is unused in **all 1,159**.

### The render mesh: a 4x4 BEZIER CONTROL NET per patch
The `+0x54` blob is sized **exactly `patchCount * 320`** on every track. Each
patch gets 16 vertices of 20 bytes: `{float3 pos, float u, float v}`, with
`u` in `{0, 1/3, 2/3, 1}` and `v` in `{-1, -2/3, -1/3, 0}` -- the same
flipped-V convention as the corner UVs at `+0x10`.

**It is the same surface as the power-basis coefficients.** Running the
standard cubic Bezier -> power conversion on the net reproduces `+0x50`
exactly:

| Check | Result |
|---|---|
| Bezier net -> power basis == stored coefficients | **28484 / 28484** (worst 1.5 cm) |
| net corner control points == the cached corners | **28484 / 28484** (worst **0 cm**) |

Note the index transpose: the blob's stored `u` (at +12) is the **v** parameter
of the power form and vice versa; `net[a][b]` is `net[v][u]`. That was
established by which orientation makes the conversion close, not assumed.

### This explains the earlier Bezier failure
An earlier pass tested the `+0x50` vectors as a Bezier control net and got
convex-hull containment 110/3885 and corner interpolation 0/3885. Correct
result, right conclusion at the time: those are **power coefficients**. The
actual control net was in a different table entirely. The two representations
coexist -- net for rendering, power form for fast evaluation.

## The second blob (`xbd+0x58`) = the OBJECT VERTEX BUFFER

Layout, fixed empirically rather than guessed:
```
u32 count
count x 32 bytes:
    +0x00  float2 uv       tiled -- ranges well outside [0,1] (up to ~47)
    +0x08  float3 pos      MODEL space
    +0x14  float3 normal   unit length
```
The stride and field offsets were found by scanning every (stride, start,
offset) combination for the one that makes a vec3 unit-length: **stride 32,
start +4, normal at +0x14 hits 100.0% on all four tracks tested**, and the
size then fits `count*32 + 4` with exactly **12 bytes** of trailing alignment
slack on every one.

`count` is the blob's own first dword: alaska 18,352, gari 29,506,
elysium 34,313, pipe 31,157.

**These are model-space vertices, not world.** Positions span roughly +/-70 m
while the tracks span hundreds of thousands of cm -- so this is prop/object
geometry that something else instances, not terrain. (The terrain is the
Bezier patch net in the `+0x54` blob.)

Verified across all 10 tracks: **260,000+ vertices, every normal unit length.**

## Table B = PLACED OBJECT INSTANCES

```
+0x00  float[4][4]  row-major affine transform; row 3 = world translation
+0x40  u32          object index -> the stride-8 table at xbd+0x68
+0x4c  float3       aabbMin
+0x58  float3       aabbMax
+0x70  u32          the SAME index again, identical in every record
```

Verified across all 10 tracks (**24,563 instances**):

| Check | Result |
|---|---|
| w column exactly `(0,0,0,1)` | **24563 / 24563** |
| rows 0..2 orthonormal | **24535 / 24563** (the rest carry a scale) |
| AABB `min <= max` | **24563 / 24563** |
| `+0x40 == +0x70` | all records, every track |

The object index's observed range matches the `xbd+0x68` table's count
**exactly** on every track: gari 0..647 against 648, alaska 0..215 against 216,
elysium 0..818 against 819, pipe 0..177 against 178. About 60% of the
transforms are pure rotations about Z -- props stood upright and spun.

### Refinement of an earlier statement
An earlier note said "table B is NOT file geometry ... populated by runtime
registration". That was about the *collision* path only: the ground sampler
reaches B through sub-object `+0x38` and gates on `rec+0x68 & 0x20`, which is
zero in every shipped file. The **instance data itself is fully on disk** --
transform, index and bounds all validate. Only the collision-enable flag is
set at runtime.

### `xbd+0x68` (stride 8) and `xbd+0x7c` (stride 4): NOT resolved
The two tables share a count (one entry per object). A natural hypothesis --
that the stride-8 records are `{firstVertex, vertexCount}` into the object
vertex blob -- **fails**: the counts sum to 13,957 against 29,506 vertices on
gari, and the offsets do not tile contiguously. Recorded so it is not retried.
The `+0x7c` values look like 16-byte-granular offsets (0, 720, 1024, 1712, ...)
whose maxima land near, but not inside, the `xbd+0x80` blob's size on every
track -- suggestive but unconfirmed.

## `xbd+0x68` = per-object MATERIAL LISTS (variable length)

`FUN_0013b3e0` (world vtable slot `0x48`, created this session) gives the real
shape -- it is **not** a fixed-stride table:

```c
for (count records) {
    n = *p;                                   // list length
    for (i = 0; i < n; i++)
        p[1+i] = materialBase + p[1+i] * 0x48;   // -> table G
    p += *p + 1;                              // variable-length advance
}
```

So the region is `count` consecutive records of `{u32 n; u32 materialIndex[n];}`.
The earlier "stride 8" reading came from dividing the region by the count, and
produced interleaved counts-and-indices that looked like small integers.

Verified on 4 tracks:

| Track | records | walk ends | material refs | max index | G count |
|---|---|---|---|---|---|
| alaska | 216 | **EXACT** | 255 | 105 | 106 |
| elysium | 819 | **EXACT** | 936 | 113 | 114 |
| gari | 648 | **EXACT** | 753 | 124 | 125 |
| pipe | 178 | **EXACT** | 183 | 35 | 36 |

Zero slack, zero out-of-range indices, and the maximum index is exactly
`count-1` every time. List lengths run 1..16, mean ~1.16.

Across all 10 tracks: **7,203 lists, 7,915 material references, 0 dangling.**

## CORRECTION: what table B's `+0x40` actually indexes

An earlier entry claimed `+0x40` indexes the `xbd+0x68` material-list region,
on the strength of its range matching that count exactly on every track. **That
was not decisive**: `xbd+0x68` and `xbd+0x7c` have the *same* count, so the
range match cannot separate them.

`FUN_0013b320` (world vtable slot `0x2c`, table B's own fixup, created this
session) settles it:
```c
if (rec[0x40] < 4000)
    rec[0x40] = ((u32*)xbd[0x7c])[ rec[0x40] ];    // through the +0x7c table
rec[0x44] = (idx < 0) ? 0 : idx*0x90 + xbd[0x5c];  // a SELF-reference into B
```
So `+0x40` resolves through **`xbd+0x7c`**, and `+0x44` is a link to another
table-B instance (negative = none). The port now carries that as `Object::link`
and bounds it against the instance count.

What the `+0x7c` values themselves mean is still open. They are 16-byte-granular
(0, 720, 1024, 1712, ...) and their maxima land close to the `xbd+0x80` blob's
size on each track, but they are **not** offsets into the `xbd+0x68` region --
that region is only ~5.6 KB on gari against `+0x7c` values up to 228,176.

## The object MESH blob (`xbd+0x80`), reached via `xbd+0x7c`

Table B's fixup resolves an instance's index through the stride-4 table at
`xbd+0x7c`; those values are **byte offsets into the `xbd+0x80` blob**. Each
mesh record is self-describing:

```
+0x00  u32     size of this record, in bytes
+0x04  u32     1        (constant on every record seen)
+0x08  u32     0x3c     (constant)
+0x0c  u32     this record's own index
+0x1c  float3  aabbMAX  <- max comes FIRST
+0x58  float3  aabbMIN
```

### Three independent self-checks, all closing
| Check | alaska | elysium | gari | pipe |
|---|---|---|---|---|
| offsets == running sum of sizes | 216/216 | 819/819 | 648/648 | 178/178 |
| `+0x0c` == record index | 216/216 | 819/819 | 648/648 | 178/178 |
| sizes sum to the blob span | EXACT | EXACT | EXACT | (region end differs) |

Across all 10 tracks: **7,203 meshes**, the offset chain and self-index verified
at parse time (a break is treated as a parse failure, not tolerated).

### AABB field order — a corrected guess
min@`+0x58` / max@`+0x64` scored **625/648** on gari. The right pairing is
max@`+0x1c` / min@`+0x58`, which scores **648/648**. Across all tracks
**7,201 / 7,203** records have an ordered AABB.

The 2 stragglers are a **real layout variant**, not noise: a minority of
records (26 of 648 on gari, all in a larger size class) hold small **integers**
at `+0x58` that read as denormal floats. What keys the variant is not
identified, so the port bounds the outlier count rather than claiming 100%.

### Mesh interior: NOT decoded
The records hold sub-mesh counts/offsets and what looks like a u16 index
buffer. **No field sums to the object vertex count on any track** (gari's best
candidate reaches 22,598 against 29,506), so meshes do **not** own disjoint
vertex ranges -- they share the buffer. Nothing further is claimed.

## Mesh record header: `blockCount`, and a CORRECTION (2026-07-28)

### Correction: there is no "+0x58 denormal variant class"
An earlier entry here claimed *"26 of 648 records on gari hold integers at
`+0x58` that read as denormals, so the layout is not uniform."* **That was an
artefact of the wrong AABB offsets.** With the corrected pairing
(max `+0x1c` / min `+0x58`) the AABB is ordered in **216/216, 819/819, 648/648
and 177/178** records on the four tracks probed -- a single outlier in total,
not a class. The claim is withdrawn.

### `+0x04` is the block count, and it IS a clean discriminator
Values: 1 (the large majority), 2, and occasionally 3 / 5 / 21 / 24.
`+0x08` is **60 on every record of every track**.

Records with `blockCount == 1` carry a single ascending run of section offsets
at `+0x2c` / `+0x34` / `+0x54`. Records with more never do:

| track | blockCount == 1 -> ascending | blockCount > 1 -> ascending |
|---|---|---|
| alaska | 199 / 199 | 0 / 17 |
| elysium | 796 / 796 | 0 / 23 |
| gari | 622 / 622 | 0 / 26 |
| pipe | 170 / 170 | 0 / 8 |

Across all 10 tracks: **6,896 / 6,896** single-block ascending,
**0 / 307** multi-block. 100% versus 0% -- so a multi-block record carries one
section set *per block*, and the single-block layout is the special case, not
the rule. Where the per-block sets live is **not** decoded.

Field classification, consistent on all four tracks probed:
* offset-like (always < record size): `+0x2c`, `+0x34`, `+0x54`
* count-like (always < 64): `+0x28`, `+0x30`, `+0x38`, `+0x4c`
* `+0x28 == +0x30` in every record inspected

### Why this stops here
The remaining interior -- the per-block section sets and the u16 index buffer --
needs the actual GPU draw path, which lives in the graphics module and is a
fresh RE thread. Three separate attempts to infer it from data shape failed
(no field sums to the vertex count; no field linearly predicts the record size;
the 60-byte-block hypothesis does not hold). Fitting structure to bytes has now
failed on this format often enough that the next attempt should start from the
draw call, not the dump.

## THE GPU DRAW PATH (2026-07-28) — found via `D3DDevice_DrawIndexedVertices`

Starting from the draw call's xrefs rather than the data, as the format has
repeatedly demanded. Callers: `MeshRenderer_DrawPartsList`,
`MeshRenderer_DrawSkinnedMesh`, `MeshRenderer_DrawIndexedBatch`,
`MeshRenderer_DrawMultiTexturedParts`, `BoardMesh_DrawAttachedPatches`.

### `MeshRenderer_DrawPartsList` (0x000ffdc0)
```c
SetVertexShader(ctx[0x15770]);  SetPixelShader(ctx[0x157cc]);
for (part = p; part; part = part[0x04]) {              // a LINKED LIST
    SetStreamSource(0, ctx + (part[0x50] + 0x1cb2)*0xc, 0x20);   // stride 0x20
    SetStreamSource(1, part[0x5c] + 200, 0);
    sub  = part[0x54][0x28];                            // sub-part pointer array
    n    = part[0x54][0x20];                            // sub-part count
    for (i = 0; i < n; i++) {
        tex = *(short*)( part[0x58][ sub[i][1] ] );      // texture index
        SetTexture(0, textures[tex]);
        DrawIndexedVertices(6, *(u16*)(sub[i][2] + 2), sub[i][2] + 4);
    }
}
```

Three things fall straight out:
* **The vertex stride is `0x20` = 32 bytes** -- an *independent* confirmation of
  the `ObjVertex` layout decoded earlier from the unit-normal scan. Two
  unrelated routes to the same number.
* **Primitive type 6** (`D3DPT_TRIANGLEFAN` in the D3D8 enum).
* **Index blocks are `{.., u16 count at +2, u16 indices at +4}`** -- the count
  and data pointers come directly out of the `DrawIndexedVertices` arguments.

### The `.xbd` image IS the runtime mesh-set object
`Mesh_RegisterVertexBuffers` (0x000fe640) is called with the `.xbd` base and
reads it at `+0x10`, `+0x34`, `+0x58`, `+0x7c` -- exactly the header's
`CountV`, `CountO`, `BaseV`, `BaseO`. Verified on four tracks:

| track | `xbd[0x10]` (vertex-buffer count) | `xbd[0x34]` (mesh count) | mesh `+0x10` |
|---|---|---|---|
| alaska | 1 | 216 | 0 |
| elysium | 1 | 819 | 0 |
| gari | 1 | 648 | 0 |
| pipe | 1 | 178 | 0 |

The single vertex blob is registered as one D3D vertex buffer, and each mesh
record's `+0x10` -- **0 in every record of every track** -- is the buffer slot
index the registration rewrites. That is why the header is so regular: it is a
live object layout, not merely a file directory.

### Section `+0x54` holds the index data
Its offset is **even in 100% of records** (216/216, 819/819, 648/648, 178/178),
which a byte offset would only be half the time. The data shows textbook
strip/fan repeats and its values climb across records -- e.g. gari record 0
starts at 139, record 1 at 185, record 2 at 333 -- which is exactly why no
field summed to the vertex count: meshes index a SHARED buffer at increasing
offsets rather than owning disjoint ranges.

### Still open, with a much narrower next step
The in-file index layout is **not** resolved. Walking the tail as a chain of
`{u16, u16 count, u16 idx[count]}` blocks consumes the record exactly in only
6 of 648 records, so the runtime "part" objects the draw path walks are
**built** from the record rather than mapped onto it -- the same pattern the
D-records showed. The next step is the mesh-record -> part-object constructor,
not further scanning: everything the draw path needs (`part+0x04/+0x50/+0x54/
+0x58/+0x5c`, sub-part `+0x04/+0x08`) is a runtime pointer that something
fills in.

## Vertex strides, both confirmed from the draw calls
* **Static/prop geometry: `0x20` = 32 bytes** --
  `MeshRenderer_DrawPartsList` -> `SetStreamSource(0, ..., 0x20)`.
  Independently confirms the `ObjVertex` layout.
* **Skinned (rider/board) geometry: `0x40` = 64 bytes** --
  `MeshRenderer_DrawSkinnedMesh` (0x000ff3d0) ->
  `SetStreamSource(0, ..., 0x40)`. This is the `.mxf` vertex size, and it is
  the first hard number for that format's interior.

## The part constructor: NOT found, after five angles
The one remaining `.xbd` gap is the function that builds the runtime "part"
objects the draw path walks. Angles tried and exhausted:

1. xrefs from `D3DDevice_DrawIndexedVertices` -- gives the draw path
   (`MeshRenderer_*`), not the builder.
2. xrefs from `Mesh_RegisterVertexBuffers` -- a single **DATA** ref; it is
   reached through the graphics-device vtable at slot `+0x194`
   (base `0x001a2b38`, confirmed by `FUN_000b0660`'s
   `device_vtable[0x194](slot[2])` call with the `.xbd` base).
3. The `.xbd` load chain: `Level_LoadTrackAssets` -> `FUN_000b07a0` ->
   `FUN_000b0660`, which does `FILE_loadpack` then calls the **model-slot
   object's own vtable at `+0x10`** to parse. That vtable is written by a
   constructor not yet located, so the parse method's address is still unknown.
4. Walking the mesh-record tail as `{u16, u16 count, u16 idx[count]}` blocks --
   succeeds in 6 of 648 records, so the parts are built, not mapped.
5. Byte-searching the record's section displacements -- lands on constants
   initialisers, as every such search on this format has.

**The precise remaining question**: what writes the vtable into the 0x14-byte
model-slot objects at `modelBank + 4 + i*0x14`? That gives the parse method,
which gives the part layout, which gives the index buffers.

## THE `.map` FILE IS A ROSETTA STONE (2026-07-28)

Each track's `.big` ships a `<track>.map` alongside the `.xbd` -- and it is
**human-readable text**, the ColdFusion toolchain's own manifest:

```
ColdFusion
TGroup Version (0.21) Revision (30)
XBOX   Version (0.24) Revision (30)
Compiled (Oct 29 2001 :: 17:05:35)
## START [D:\ssxdvd\art\FrontEnd\NorthAmerica\SSXFE]
### MODELS BEGIN
### Name                          UID   Ref   HashValue
Mdl_Speakers_OnPole_1000          0     19    3377af0
```

Sections: MODELS, PATCHES, INTERNAL INSTANCES, SPLINES, LIGHTS, MATERIALS,
CONTEXT BLOCKS, PARTICLE MODELS/INSTANCES, TEXTURES, LIGHTMAPS, CAMERAS,
PLAYER STARTS.

### It matches the binary table-for-table
| `.map` section | rows | `.xbd` table | count |
|---|---|---|---|
| PATCHES | 3885 | A | **3885** |
| INTERNAL INSTANCES | 3393 | B | **3393** |
| MODELS | 648 | mesh table (`+0x7c`/`+0x80`) | **648** |
| CONTEXT BLOCKS | 648 | material lists (`+0x68`) | **648** |
| SPLINES | 169 | F | **169** |
| LIGHTS | 942 | **E** | **942** |
| PARTICLE MODELS | 10 | C | **10** |

Seven exact matches. The one that does NOT match is MATERIALS: 219 rows against
table G's 125 -- so G is not simply "the materials list", and that stays open.

### The decisive validation
The MODELS section's **Ref** column is each model's instance count. Summed:
**3393 -- exactly table B's record count.** And comparing per-model:
**648 of 648 models have an instance count matching the manifest.**

That is an independent check against the original tool's output, produced in
2001, against a decode derived years later purely from the binary. Nothing was
fitted to it.

### NEW: table E is the LIGHT table
Artist names carry the type -- `DY_Am_Ambient_1000`, `SD_Di_Directional_1000`,
`SD_sp_*`, `SD_pt_*`. Correlated against the `+0x00` enum over all 942:

| enum | meaning | count | prefix |
|---|---|---|---|
| 0 | directional | 1 | `Di` |
| 1 | spot | 842 | `sp` |
| 2 | point | 31 + 67 | `pt`, and 67 named `sp_` |
| 3 | ambient | 1 | `Am` |

The 67 records named `sp_` but stored as type 2 are the artist's label
disagreeing with the data; **the enum wins**.

This retro-explains every field that was only structurally known:
* `+0x1c` the unit vector -> the light **direction**;
* `+0x28`/`+0x40` -> the **influence AABB**;
* `+0x4c` the cosine -> the **spot cone**: 0.70711 (cos 45 deg) on spots, 0 on
  ambient and directional;
* `+0x04` flags -> 0 directional, 0x100 spot, 0x200 ambient.

### NEW: table C is PARTICLE MODELS
10 rows, names like `Fog_Sphere_A_0`. Matches C's 10 records.

### SPLINES confirms the edge decode
Names are `Spline_FenceRail_1010` -- fence rails. That is exactly what the
D/F cubic-curve chains were deduced to be from geometry alone.

Ported: `xbd::Light` + `LightType`. Across all 10 tracks, **4,026 lights**:
unit direction 4026/4026, ordered AABB 4026/4026, type in 0..3 4026/4026
(directional 10, spot 2693, point 1313, ambient 10).

### Also found: `tricky.ser` is a leftover PS2 artefact
`ssxfe.big` contains `tricky.ser`, itself a **big-endian BIGF** holding
`icon.sys`, `nhl.ico`, `pstats.ps2`, `sched_[sm].dat`, `audname[sg].dat` and a
`peanut.txt` hash blob -- PS2 memory-card icon data shipped unused in the Xbox
build. Not script data; recorded so it is not chased again.

## Header count -> `.map` section, cross-checked on four tracks

| count field | `.map` section | gari | alaska | elysium | pipe |
|---|---|---|---|---|---|
| `+0x08` | PATCHES | 3885 | 3449 | 4268 | 2332 |
| `+0x14` | INTERNAL INSTANCES | 3393 | 1477 | 3933 | 576 |
| `+0x18` | **PARTICLE INSTANCES** | 10 | 27 | 59 | - |
| `+0x20` | MODELS / CONTEXT BLOCKS | 648 | 216 | 819 | 178 |
| `+0x24` | LIGHTS | 942 | 267 | 218 | 152 |
| `+0x28` | SPLINES | 169 | 84 | 296 | 108 |
| `+0x34` | MODELS / CONTEXT BLOCKS | 648 | 216 | 819 | 178 |
| `+0x38` | **PARTICLE MODELS** | 10 | 17 | 19 | - |

### CORRECTION: table C is PARTICLE INSTANCES, not PARTICLE MODELS
The previous entry called table C (`+0x18`) "PARTICLE MODELS". On **gari both
sections happen to have 10 rows**, so that track alone cannot tell them apart.
Alaska (27 instances vs 17 models) and elysium (59 vs 19) separate them
cleanly: `+0x18` tracks **instances**, `+0x38` tracks **models**.

Gari has now been unrepresentative four separate times in this format -- no
grid sentinels, a sub-index that fills 0..15 exactly, zero AABB outliers, and
now equal particle counts. Cross-track checks are not optional here.

### MATERIALS has no matching header count -- and that is consistent
219 / 222 / 176 / 81 rows against no table of that size. Table G is **not** the
material list: its four leading shorts are texture indices, and their maxima
(120 / 193 / 107 / 72) fall **inside** the corresponding MATERIALS row counts on
every track. So G is a per-object material **binding** (4 slots) and MATERIALS
is the palette those index into. The palette itself is not a `.xbd` table --
most likely it lives with the `.xsh` texture sheets.

### Still unidentified
`+0x1c` (table G: 125/106/114/36), `+0x2c` (the D edge segments -- SPLINES are
the chains, D the segments, so no 1:1 section is expected), `+0x30`
(16/8/16/1) and `+0x3c` (121/194/108/73).

## TASK #31 SOLVED — the mesh build chain, found end to end

The question was "what writes the vtable into the 0x14-byte model-slot
objects". Answer: **`FUN_000b05d0`**, the model-bank constructor, which loops 10
slots writing `PTR_FUN_0019aac0` into each. From there the whole chain opens:

```
FUN_000b05d0        bank ctor -> slot vtable = PTR_FUN_0019aac0
  slot vtable +0x10 = FUN_0013a8a0      the parse method
    -> stores the pack at slot+8, then calls vtable[0x28]
       = Resource_RelocateBlockPointers, which fans out per table
  vtable[0x34] = FUN_0013aa20           walks the mesh table
    -> per record, advancing by the record's own size field
  vtable[0x38] = FUN_0013aa50           PER MESH RECORD
    rec[+0x08] += rec                   -> the sub-block array
    rec[+0x0c] = table[ rec[+0x0c] ]    -> an index, not a self-id
    calls vtable[0x3c]( rec[+0x04], rec[+0x08] )
  vtable[0x3c] = FUN_0013aa80           SUB-BLOCKS, stride 0x18
    +0x04 / +0x08 / +0x0c -> MeshGroups, and they FREQUENTLY ALIAS
                             (the engine explicitly skips duplicates)
    +0x10 -> a secondary structure
    +0x14 -> a matrix; **-1 means use the default at DAT_001fadd0**
  vtable[0x40] = FUN_0013ab20           PER GROUP
    count at +0x20, pointer array at +0x28, entries relative to their
    OWN address
  vtable[0x44] = FUN_0013ab70           per sub-part: a NO-OP STUB
```

The group's `count@+0x20` / `array@+0x28` is **exactly** what
`MeshRenderer_DrawPartsList` reads as `part[0x54][0x20]` and `part[0x54][0x28]`.
Build side and draw side agree independently.

### Corrections this forces to the earlier record layout
* `+0x04` -- I called it "blockCount". Right idea, real name: the **sub-block
  count**.
* `+0x08` -- I recorded "constant 0x3c". It is not a constant, it is the
  **byte offset to the sub-block array**, which happens to be 0x3c on every
  record of every track (**7203/7203** verified).
* `+0x0c` -- I called it "this record's own index". It IS an index, but the
  loader resolves it through a table (`this[3]`); equalling the record ordinal
  is a property of the data, not its meaning.

### Verified across all 10 tracks
`7,203 mesh records: subBlockOffset == 0x3c in 7203; 8,615 sub-blocks;
8,405 geometry groups; 8,405 with a resolvable part array.`

The three aliased group pointers and the -1 matrix default were checked
directly on four tracks and behave as the engine's own duplicate-skip and
default-substitute logic predicts.

### What remains
The sub-part records themselves. `vtable[0x44]` is a stub, so they are **not**
relocated -- yet `MeshRenderer_DrawPartsList` uses `subpart[+0x08]` as a
pointer to an index block (`count@+2`, `u16 indices@+4`) and `subpart[+0x04]`
as a texture index. Either they are relocated on another path or the draw-time
`part` differs from the file record; that is the last link, and it is now a
single well-posed question rather than "the interior".

## Why the last link resists static analysis (2026-07-28)

Chased the sub-part index-block pointer through every remaining relocation
callback. `Resource_RelocateBlockPointers` calls exactly 13 vtable slots
(0x2c, 0x30, 0x34, 0x48, 0x54, 0x58, 0x5c, 0x6c, 0x70, 0x74, 0x78, 0x80, 0x88);
all are now decoded, and **none of them touches a sub-part**:

* `0x30` = `FUN_0013aca0` -- adds the blob base to each mesh offset-table entry;
* `0x4c` -> `FUN_0013ac40` -> `0x50` = `FUN_0013ac90` -- relocates a single
  `+0x04` field on the sub-block's secondary chain;
* `0x5c`/`0x78` = size-walked list drivers (`FUN_0013ab80`/`FUN_0013acc0`);
* `0x44` = `FUN_0013ab70` -- the per-sub-part hook, a **no-op stub**.

### The reason: the draw-time "part" is NOT the file record
`MeshRenderer_DrawPartsList` reads its part at `+0x04` (next), `+0x50`, `+0x54`,
`+0x58`, `+0x5c` -- but a file sub-block is only `0x18` bytes and a geometry
group's known fields stop around `+0x2c`. The offsets do not fit.

`GfxContext_Init` (0x00104c40) settles it: each draw mode is a small **object**
whose vtable slot 0 is the draw function (`PTR_MeshRenderer_DrawPartsList_001a2ab8`
and eleven siblings). So the thing `DrawPartsList` walks is a **render-queue
node**, populated at submission time -- not a relocated file structure.

**So the file side is complete and the missing step is a runtime one**: whatever
fills a queue node's `+0x50..+0x5c` from a mesh group. That is a different
chain (render submission), and it is the only thing between the decoded file
data and drawn prop geometry.
