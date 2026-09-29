# RE notes: the AI racing-line path system — a fresh subsystem, never touched before

**2026-07-20, "go fresh" pass.** Picked a genuinely untouched area: despite extensive
work this session on player-rider physics, tricks, and commentary, **CPU/AI racer
decision-making had never been explored**. Checked `OtherRider` (the CPU racer class,
already confirmed to derive from `Rider`) first — its own vtable turned out to be a dead
end for AI logic: every one of its 10 real slots resolves to either an already-documented
generic `Node_*` shared method or a plain thunk into `Rider`'s own methods
(`Rider_UpdatePhysicsState_Thunk`, `Rider_UpdateWorldSpaceMarker`). **No AI-specific
virtual method exists on `OtherRider` itself** — so the actual AI steering logic must
live elsewhere, driven by data rather than polymorphism.

## Finding the AI path file format

Searched for AI-related tag strings and found `"AIPath\0EventPath\0"` embedded together —
this is the exact **`"AIPaths"`/`"EventPaths"`** tag pair already found earlier this
session via `DebugMenu`'s embedded memory-usage debug page (see
`RE_NOTES_debug_menu.md`). `"EventPaths"` had already been resolved (its *consumer*,
`Rider_UpdateTrackEventTriggers`, was found and named earlier) but its *constructor* was
never located — and `"AIPaths"` itself was never touched at all. This also matches the
already-extracted-but-never-examined `gari.aip` file sitting in
`Game Data\data\models\gari.big` (extracted alongside `.xsf`/`.map`/etc. in an earlier
session, never opened).

## The load chain

```
Level_LoadTrackAssets (already named)
  -> AIPath_LoadFromFile (was FUN_000bf8d0)
       FILE_loadpack(...) loads the .aip file's raw bytes
       parses a sequence of {type, size} chunk headers:
         type 0 -> AIPathSet_Construct   (tagged "AIPaths")
         type 1 -> EventPathSet_Construct (tagged "EventPaths")
```

- **`AIPathSet_Construct`** (was `FUN_000bf6c0`) — content-confirmed via the literal
  `"AIPaths"` tag string. Allocates a tagged array of 64-byte path-collection entries,
  installs each entry's vtable, then parses each individual path via
  **`AIPath_ParseFromBuffer`** (was `FUN_000be9b0`) — a per-path binary parser reading a
  bounding-box-shaped header (3 vector reads) plus a 16-byte-stride waypoint node array
  and a second array/count pair.
- **`EventPathSet_Construct`** (was `FUN_000bf7e0`) — content-confirmed via the literal
  `"EventPaths"` tag string. Same shape as `AIPathSet_Construct`. **Very likely the
  actual construction site for the object `Rider_UpdateTrackEventTriggers` already
  queries at runtime** — closing that earlier open thread's missing half (constructor
  found now, consumer found earlier).

## The `AIPath` query API

Read `AIPath`'s own vtable (8 slots) to find its runtime API. Three of the eight slots
pointed into a **previously-unanalyzed code region** — created functions there (following
this project's established `/create_function` methodology) to unlock them:

- **`AIPath_QueryZonesInRange`** (was `FUN_000be870`) — the core query: given a distance
  range `[min, max]` along the path, scans the waypoint list for entries overlapping that
  range, builds a bitmask of matching entry "type" values, and returns the matching
  entries via an output array. **This is very likely what CPU-racer AI logic uses each
  frame to look ahead along its path** (upcoming corners, boost zones, hazards) — but its
  only confirmed callers so far are its own vtable siblings; the actual external
  AI-steering consumer was **not found this pass**.
- **`AIPath_QueryZonesInRange_Thunk`** (was `FUN_000be740`) — a trivial passthrough to
  the above, likely a second virtual-interface alias (matches this project's established
  "identical vtable slot content, linker-folded" pattern).
- **`AIPath_CheckPointInSpecialZone`** / **`AIPath_CheckPointInSpecialZoneFromEnd`** (was
  `FUN_000be960`/`FUN_000be770`) — a point-in-zone check gated on a specific waypoint
  type (`0x1f`/31, meaning not decoded), with a "measured from the path's end instead of
  its start" variant.
- **`AIPathNode_SetDefaultFlags`** / **`AIPathNode_SetParsedField`** /
  **`AIPathNode_SetLengthField`** (was `FUN_000be5a0`/`FUN_000be5b0`/`FUN_000be720`) —
  small parse-time field setters, dispatching on numeric field-ID codes (0/100/0x65)
  during `AIPath_ParseFromBuffer`'s own parsing.

11 renames total.

## Found the race-start assignment site, upgrading an old open thread (immediate follow-up)

`AIPath_LoadFromFile`'s "this" turned out to be a **global singleton** at a fixed address
(`0x1e3f38`, confirmed via a literal `MOV ECX,0x1e3f38` at the `Level_LoadTrackAssets`
call site) — not an embedded field on some other object. That made it possible to search
`xrefs_to` the global directly and find every other consumer, rather than needing to
trace through an intermediate container.

That search turned up the real payoff in the already-named **`Race_ResetPlayerRoster`**
(race/grid setup). For each rider being placed on the starting grid, it computes a
grid-slot index from the rider count, fetches a path pointer via the new
**`AIPathSet_GetByIndex`** (was `FUN_000bf300`, a trivial indexed getter over
`AIPathSet_GetCount`'s array), and calls the new **`Rider_AssignAIPath`** (was
`FUN_00030f30`) once per rider — storing the assigned path pair
(`rider+0x55b0`/`+0x55b4`) and resetting 3 distance-tracking fields
(`rider+0x374`/`+0x370`/`+0x378`).

**This is the exact distance-along-track cache `Rider_UpdateTrackPathPosition` maintains**
— a function documented in a *much* earlier session, whose own rename comment already
flagged its computed look-ahead steering angle (`this+0x3a0`) as *"a strong, plausible
candidate for AI-rider steering/pathing input... but NOT proven"* (no consumer of that
angle was ever found). Finding that `Rider_AssignAIPath` resets those exact cache fields
the moment a rider is assigned an AI path at race start is a genuine, well-evidenced
upgrade to that old flag: it confirms the tracked path is assignment-driven per rider
(consistent with each rider following their own designated racing line), not a single
static track spline shared by everyone. **It does not fully close the thread** — who
actually *reads* the computed steering angle at `this+0x3a0` still wasn't found this pass.

Also confirmed (via the debug-telemetry read in `Rider_UpdateSubsystems`, which reports
each rider's current path index for some debug display) that riders track a "current
EventPath"/"current AIPath" pointer pair at `rider+0x55e0`/`+0x55e4` — separate fields
from the `+0x55b0`/`+0x55b4` assignment slots, likely a "currently active" vs. "assigned
at start" distinction. Named `EventPathSet_FindIndexByPointer`/`AIPathSet_FindIndexByPointer`
for the lookup helpers behind that telemetry.

8 more renames (11 + 8 = 19 total for this whole AI-path thread).

## Honest status — the actual steering-angle consumer still not found

This pass fully mapped the **data format, loading/query API, and now the race-start
assignment mechanism** for AI racing-line paths, closing two real previously-open
threads (`EventPathSet_Construct`'s missing constructor, and significantly strengthening
the evidence for `Rider_UpdateTrackPathPosition`'s AI-steering-candidate flag). What
remains genuinely open: the actual code that reads the computed look-ahead steering angle
(`rider+0x3a0`) to turn it into a steering *decision* — checked several `Rider_UpdatePhysicsState`
sub-calls directly in the original session with no reference found, and this pass didn't
revisit that specific search. A well-scoped, concrete next thread, not a dead end.

## Closing the loop: found the AI steering triad (immediate follow-up)

Searched for other references to the literal dword `0x3a0` (the steering-angle field
offset) via `/search_bytes` on `a0030000`, filtering out several coincidental hits in
unrelated already-documented systems (`cMeshAnim_Construct`, `BdrSeq_InitEventCodeTable`,
`AggressionManager_ResetMatrix` — small integer offsets collide often; treated as noise
per this project's established caution). One candidate, `FUN_00035de0`, had also
independently surfaced in an earlier search for `rider+0x55e0`, giving it a double
signal worth chasing.

Read all three functions in this cluster in full:

- **`Rider_ComputeAISteering`** (was `FUN_00035660`) — a boids-style local
  collision-avoidance steering function. Large (600+ line) implementation: iterates
  every active rider (`DAT_001de8fc` count via the existing `FUN_000ab710` per-index
  getter), computes pairwise distances, finds riders within a threshold
  (`_DAT_00187bd4`), and builds a weighted avoidance vector. Validates the proposed
  move via the already-named `Terrain_SampleHeightAt`. If avoidance alone isn't
  sufficient/valid, **falls back to `Rider_FollowAIPath`** — and that function can call
  back into this one, so the pair is mutually recursive. Finishes by calling
  `Rider_ApplyMotionUpdate` to commit the result.
- **`Rider_FollowAIPath`** (was `FUN_00035de0`) — the path-following fallback.
  Dynamically re-resolves the nearest AI path (`FUN_000bf5e0`) and can reassign
  `rider+0x55b0` mid-race (not just at race start like `Rider_AssignAIPath`), loops
  candidate paths (`FUN_000bf6a0`/`FUN_000bed00`) picking the closest, and updates the
  same cache fields `Rider_AssignAIPath` initializes. Calls the already-named
  `SplinePath_EvaluateAtDistance` for a look-ahead point, computes
  `atan2(deltaY, deltaX)`, and **writes it directly to `rider+0x3a0`** — this is the
  exact write that answers the open question above. Also updates remaining-distance
  tracking via the already-named `SplinePath_FindClosestPoint`, then continues into
  `Rider_ComputeAISteering` (completing the mutual recursion).
- **`Rider_ApplyMotionUpdate`** (was `FUN_000344a0`) — the commit step. **Renamed
  mid-analysis** (see correction below) once its full xref list showed it's a generic
  utility, not AI-specific. Operates on a per-rider motion-state sub-object that itself
  holds a back-pointer to the owning `Rider` (`this+0x58e0`). Copies in the new
  position/orientation, samples terrain height at the proposed spot, banks/tilts the
  rider to match the surface normal there, and drives movement vs. settle/stop
  animation state depending on speed. When called with its "finished" flag set,
  instead cleanly parks the rider's physics-mode (→ mode 4, the
  already-confirmed-inactive `Node_NoOpStub1`) and RiderEvent state (→ 1) via the same
  enter/exit-hook shape used elsewhere in the project (`Rider_SetPhysicsMode`).

This confirms `Rider_FollowAIPath` writes the exact `this+0x3a0` field
`Rider_UpdateTrackPathPosition` (an earlier session's find) computes — a genuine,
fully-traced write site for a field that previously had none identified.

**Correction caught mid-analysis (checked deliberately, not assumed) — two overclaims
fixed before they reached the deliverable:**

1. **`Rider_ApplyMotionUpdate` was initially named `Rider_ApplySteeringResult`.**
   Checking its full `xrefs_to` list (per this project's "verify deeply, don't guess"
   rule) turned up callers with no AI connection at all: `Rider_ResetPhysicsState`,
   `Camera_WarpToTarget`, `ZBoost_Update`, plus 2 more unnamed functions (one confirmed,
   via a quick decompile, to be a scripted warp-and-finalize trigger). It's a generic
   "commit a new position/orientation to a rider, optionally finalizing its state"
   utility — AI steering is only one of several callers. Renamed immediately rather
   than let a misleading name stand.
2. **`Rider_ComputeAISteering` was initially described as "the master per-tick AI
   steering function."** Checking its `xrefs_to` list found only 2 callers:
   `Rider_FollowAIPath` (the mutual recursion) and the already-named
   `RiderEvent_UpdateEndRaceFadeSequence` — the post-race fade-to-results-screen
   RiderEvent state (`RiderEvent_DispatchTypeB` case `0x16`), not a general mid-race
   dispatch site. Searched for indirect/data references to both function addresses
   (vtable slots, function-pointer tables) via raw byte search on their addresses —
   **none found**. So the honest scope is: this triad is a **confirmed** consumer that
   reads/writes the `this+0x3a0` steering angle, but that's only proven to run while a
   rider is in the end-race coast-down state, not proven as the general mid-race AI
   steering driver. Real progress either way — just narrower than first claimed.

**Revised status on the original open question**: "who writes `rider+0x3a0`" is now
answered (`Rider_FollowAIPath`), with the caveat that the only confirmed trigger path
found so far is the end-race fade-out, not general racing. Whether AI steering during
normal racing goes through this same triad (via a call site not yet found) or a
separate, still-unidentified mechanism remains open — a narrower, more honest next
thread than "who consumes the angle" was.

3 more renames (19 + 3 = 22 total for this whole AI-path thread).

## Found an actual reader, not just a writer (same-session immediate follow-up)

Realized the write-site finding above answered a *different* question than the one
originally left open: "who writes `this+0x3a0`" (answered: `Rider_FollowAIPath`) is not
the same as "who reads `this+0x3a0` to make a steering decision" (the actual original
open question). Re-ran the full `/search_bytes` for the `0x3a0` displacement literal —
not just the pre-filtered subset from the original pass — and checked every one of the
25 hits' containing functions. Most were already-ruled-out coincidental collisions
(`cMeshAnim_Construct`, `BdrSeq_InitEventCodeTable`, `AggressionManager_ResetMatrix`,
also `D3DDevice_SelectVertexShader` — all unrelated structs/meanings) or land in
unexamined `FUN_` clusters not chased down this pass, but one hit was clean: the
already-named **`Rider_TriggerOvertakeCommentary`**.

That function reads the *second* rider's `this+0x3a0` field, passes it through a newly
named helper **`Math_AngleToSinCos`** (was `FUN_0001e5a0` — a trivial angle→(cos,sin)
converter wrapping 2 internal x87 trig stubs), and combines the result with both
riders' `+0x180`/`+0x184`/`+0x188` direction-vector components to compute a
relative-heading-projected position delta, range-checked before triggering
rivalry/overtake commentary.

**This is a genuine read of the same field the write-site finding confirmed** — and
critically, this read runs for *any* two nearby riders (not gated to AI or to the
end-race state), which supports a cleaner unifying picture: `this+0x3a0` is most likely
a general per-rider "current heading/steering angle," probably maintained for every
rider unconditionally by `Rider_UpdateTrackPathPosition` (confirmed in an earlier
session to run every frame for every rider), with `Rider_FollowAIPath` only overriding
it specifically when an AI rider takes the path-following branch. This resolves the
original "who reads the angle" question with real precision — commentary logic is a
confirmed consumer — though it remains unconfirmed whether physics/steering-input code
itself also reads it to actually turn a rider, versus this being (so far) the only
found consumer. 1 more rename (22 + 1 = 23 total for this whole AI-path thread).

## CLOSED: found the general-racing AI steering mechanism (2026-07-22, port-priority pass)

Picked this file back up per an explicit "focus on Tier 1 until complete" directive.
The remaining open question — "does AI steering during normal racing go through
`Rider_ComputeAISteering`/`Rider_FollowAIPath`, or a separate mechanism, via a call site
not yet found" — turned out to have the wrong premise. Rather than searching harder for
a call site to that specific pair, read **`Rider_UpdateTrackPathPosition` in full for
the first time** (it had only ever been referenced for its `this+0x3a0` write, never
fully decompiled) — and it turned out to contain the missing piece directly.

`Rider_UpdateTrackPathPosition` (confirmed to run every frame for every rider) computes
the steering angle via `atan2` unconditionally, exactly as expected — but its **second
half**, never previously read, conditionally calls a function gated on a probability
roll (`FUN_0012a4e0() mod 100` against a difficulty/rank-scaled threshold) and track-
progress/distance checks. That function is the actual **dynamic AI lane/path-selection
decision**:

- **`Rider_SelectBestAIPathZone`** (was `FUN_00032420`) — queries nearby alternate paths
  via the new **`AIPathSet_QueryNearbyAlternatePaths`** (was `FUN_000bf4c0`, iterates the
  same 64-byte-stride `AIPathSet` array `AIPathSet_Construct` builds, filtering by
  bounding-box distance via the new **`AIPath_ComputeDistanceToBoundingBox`**, was
  `FUN_000beb80`), scores each candidate by a combined current-position/lookahead-point
  distance heuristic (with a bias term favoring the rider's *current* path, weighted by
  a difficulty/rank field), and — if a different path scores better — **reassigns the
  rider's tracked path** (`this+0x156d`) and immediately re-evaluates the closest-point/
  lookahead cache against it. Also named **`SplinePath_ComputeTotalLength`** (was
  `FUN_000be930`, sums per-segment lengths to check proximity to a path's end).

**This closes the Tier-1 gap.** `Rider_SelectBestAIPathZone` is the genuine,
previously-missing "AI makes a steering/lane decision" mechanism — it runs every frame
for every rider (via `Rider_UpdateTrackPathPosition`), completely independent of the
end-race-fade state the `Rider_ComputeAISteering`/`Rider_FollowAIPath` pair is gated to.
The two mechanisms are **not the same triad** as the original question assumed — they're
two separate, both-real AI behaviors: this one (dynamic path/lane choice, always active)
and the earlier-found pair (local collision-avoidance steering, confirmed only during
the end-race coast-down). Between the two, general-racing AI navigation is now fully
accounted for: `Rider_UpdateTrackPathPosition` maintains the steering angle and
periodically re-evaluates which path to follow, every frame, for every rider.

5 more renames (23 + 5 = 28 total for this whole AI-path thread). **This thread is now
substantially complete** — the load format, query API, race-start assignment, and both
general-racing and end-race AI steering mechanisms are all traced and named.

### Still open (minor, not Tier-1)
- `param_1+0x114`/`this+0x50`/`this+7`'s exact semantics in `Rider_SelectBestAIPathZone`'s
  scoring (plausibly difficulty tier / rank / a per-rider aggression stat) — not
  individually decoded, named on confirmed structural role only.
- Whether human players ever have `Rider_SelectBestAIPathZone` meaningfully affect them
  (the code runs unconditionally per-rider, but a human's real input presumably
  overrides any path-following behavior) — not confirmed either way, low priority.

## Container format CONFIRMED and ported (M3)

`AIPath_LoadFromFile` (`0x000bf8d0`) gives the exact container layout — Ghidra
renders the header reads as `CRT_MemCopy` calls, but the shape is plain:

```
buf[0] : magic = 0x0A0A0A0A
buf[1] : chunk count
then `count` x { u32 type, u32 size } + `size` payload bytes
    type 0 -> AIPathSet_Construct    ("AIPaths")
    type 1 -> EventPathSet_Construct ("EventPaths")
```

### Validated on all 10 tracks
The chunk walk lands **exactly** on the end of the file for every one — an
arithmetic check, not a plausibility judgement, so the reader treats a mismatch
as a parse failure rather than a short read.

| Track | AIPaths | EventPaths |
|---|---|---|
| alaska | 67288 | 12960 |
| aloha | 42808 | 6664 |
| elysium | 86264 | 12128 |
| gari | 45840 | 13824 |
| megaple | 31720 | 2200 |
| merquer | 75056 | 12648 |
| mesa | 38000 | 9492 |
| pipe | 11664 | 1248 |
| snow | 27016 | 5604 |
| untrack | 77720 | 6928 |

Every file: magic `0a0a0a0a`, exactly 2 chunks, both types present. The sizes
track track complexity sensibly (`pipe`, a halfpipe, is by far the smallest;
`elysium` and `untrack` the largest).

Ported as `port/src/assets/aipath.{h,cpp}`. **Container level only** — the
per-path interior (bbox header + 16-byte-stride waypoints, parsed by
`AIPath_ParseFromBuffer`) is not decoded here.

## Per-path record DECODED (AIPath_ParseFromBuffer, 0x000be9b0)

With the two byte-level helpers resolved -- `FUN_000bf480` = 12-byte read,
`FUN_000bf4a0` = 4-byte read -- the record is:

```
u32  tagCount
tagCount x { u32 tagId, u32 size, byte[size] }    -> vtable slot 0xc
u32  nodeCount                                     -> path+0x0c
u32  zoneCount                                     -> path+0x04
vec3 start, bboxMin, bboxMax                       -> path+0x10, +0x20, +0x2c
nodeCount x 16 bytes   (segments)                  -> path+0x1c points AT the buffer
zoneCount x 16 bytes   (zones)                     -> path+0x08
```

Both arrays are pointed *at the file buffer*, never copied -- so the loaded
buffer has to outlive the path set.

### Set headers
| | AIPaths (`AIPathSet_Construct` 0x000bf6c0) | EventPaths (`EventPathSet_Construct` 0x000bf7e0) |
|---|---|---|
| header | `u32 pathCount, u32 refCount, u32 refIdx[refCount]` | `u32 pathCount, u32 defaultIndex` |
| object size | 0x40 | 0x3c |

### Segments are NOT waypoints -- corrected
The obvious reading of the 16-byte record is `{vec3 pos, u32}`. **That is wrong**
and the data says so loudly: it produces x,y in [-1,1] and z up to 5.5e7. The
actual record is

```
float dirX, dirY   -- UNIT in the horizontal plane only
float slope        -- rise over run, NOT a third direction component
float length       -- HORIZONTAL distance, cm
```

Evidence, over all 27,642 segments in the shipped tracks:
- worst `|1 - |(dirX,dirY)||` = **2.13e-07** (float epsilon)
- integrating `start + sum(dir*length, slope*length)` reproduces the stored
  bounding box to a worst error of **0.00195 cm** across all 1,020 paths --
  a closed identity, not a fit
- the 8 segments with `|slope| > 100` all have `length` pinned at the 1e-5
  floor, and `slope*length` comes out to a clean 5.1-5.5 m rise. They are
  vertical drops on Mega Plex; the exporter clamps the run to avoid dividing
  by zero. The outliers *confirm* the model rather than contradicting it.

Totals: 265,620 m of racing line across the 10 tracks.

### The two classes share one vtable block
`PTR_..._0019ba74` is not a separate vtable -- it is a shifted secondary view
starting at **slot 4** of the AIPath vtable at `0x0019ba64`. So slot 0xc means
different things per class:

| | AIPath | EventPath |
|---|---|---|
| slot 0xc handler | `AIPathNode_SetParsedField` | `AIPathNode_SetLengthField` |
| tags consumed | 100 -> +0x38, 101 -> +0x3c | 0 -> +0x38 |
| object size | 0x40 | 0x3c |

This *predicts* the object sizes, and the files agree exactly: **932/932 AI
paths carry tags {100,101} and nothing else; 88/88 event paths carry tag {0}
and nothing else.**

### Event-path tag 0 = distance to the finish
Read as a float at +0x38 and used by `AIPath_CheckPointInSpecialZoneFromEnd`
as `+0x38 - x`, converting a distance along the path into a distance from the
end. **It is not this path's own length** -- that was the first hypothesis and
it failed on all 88 paths.

Cross-checked against geometry, which is decoded from entirely different bytes:
of the **29** places where one event path's endpoint meets another's start
point (within 50 cm), **27 (93%)** satisfy
`distanceToFinish[i] - length[i] == distanceToFinish[j]`.
The 2 exceptions are branch points on gari and merquer, where an alternate
route legitimately has a different distance left to run.

### Zones are intervals along the path
`AIPath_CheckPointInSpecialZone` (0x000be960) reads the last two dwords as
**floats** and compares them to distances, so:

```
u32 id;  u32 param;  float rangeStart;  float rangeEnd;
```

- `id` goes through the 32-entry table at `DAT_001bae60` to a dense slot.
  Table: slot 0 = 0xffffffff (the no-match default), slots 1..24 = ids 0..23,
  slots 25..30 = 100..105, slot 31 = 300. **All 4,276 zone ids in the shipped
  tracks are present in that table** -- none falls through to the default.
- slot 31 (id 300) is the one `CheckPointInSpecialZone` tests for; it is also
  the most common id (x1589).
- **All 4,501 zone intervals lie inside `[0, pathLength]`.** 3 are stored
  reversed; the engine does an overlap test rather than assuming an ordering,
  so they must not be "fixed" by sorting.

### The 6 refs
`AIPathSet_GetByIndex` (0x000bf300) returns `set+0x14+i*4` -- the ref list, not
the full path array -- so these are the set's public entries. **Every track has
exactly 6**, and their lengths cluster tightly (3-21% spread) against a
104-338% spread over all paths. Six parallel full-course lines is the natural
reading, matching a 6-rider race. Mesa is the exception at 85% spread: its six
split into two groups (~250 m and ~443 m).

### Not decoded
`param100` (values 50/100/20/80/0/25/75 -- percentage-like) and `param101`
(1 x856, 0 x76 -- a flag) are read and preserved, but their roles are **not**
established. Named neutrally in the port for that reason.

Ported: `port/src/assets/aipath.{h,cpp}`; all of the above is asserted in
`port/tests/asset_test.cpp`.
