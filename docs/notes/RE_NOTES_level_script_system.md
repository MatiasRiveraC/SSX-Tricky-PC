# RE notes: the level scripting / trigger-node system (the real "Script" interpreter)

**This is the most important finding of the session.** Chasing `FUN_0007daa0` (front-end
init) led to `FUN_0004a5a0("FEStartScript")`, which led to the actual opcode dispatcher —
`FUN_00049960` — the function flagged all the way back in the very first mining pass
(Part 1) as touching 22 different allocator tags. That early flag was correct; this is
what it was pointing at. This single function is the **runtime factory for every
scripted/triggered object type in an SSX Tricky level**: rail rollers, boost pads,
trick-trigger zones, particle emitters, fences, animated textures, crowd boxes, movie
triggers, and more, all created from one data-driven opcode stream.

## The chain that led here

1. `FUN_0007daa0` (renamed `FEInit_Boot` below) — the front-end boot function from
   `RE_NOTES_frontend_menu_map.md`. Loads fonts, the `ssxfe.big` archive, sets up 4
   controller-input slots, then near the end calls:
   `FUN_0004a5a0("FEStartScript")` — [default.xbe.c:81148](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:81148).
2. `FUN_0004a5a0(param_1)` ([default.xbe.c:47830](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:47830)) — resolves `param_1` (a script
   name, e.g. `"FEStartScript"`) to a resource ID via `FUN_00141ed0`, then allocates a
   `"Script"`-tagged instance (0xf0 bytes, matches Part 1's tagged-allocator find) and
   constructs it via `FUN_0004a530`. Reads as `Script_PlayByName(name)` — load a named
   script asset and spawn a running instance of it.
3. `FUN_0004a530`/`FUN_0004a470`/`FUN_0004a310`/`FUN_0004a610` all share the same vtable
   (`&PTR_FUN_001891f0`) — different constructor entry points for the same underlying
   "running script instance" class, just wired up from different sources (by name, by
   resource ID, by raw handle, etc).
4. Somewhere in the running-script's per-frame update, it calls **`FUN_00049960`** for
   each command record it processes — this is the actual opcode interpreter.

## `FUN_00049960(int param_1, int param_2)` — the opcode dispatcher

- `param_1` = the script/node execution context. `param_1+100` (0x64) holds the
  **currently active node** for this context/slot; `param_1+0xe4`/`+0xe8` are also
  read/passed through to every constructor (likely "owner"/"parent" references, e.g. the
  triggering entity or scene).
- `param_2` = the command record; `*(int*)(param_2+8)` is the **opcode**.
- Before dispatch, if a node is already active in the slot and its state (`piVar1[5]`)
  isn't `5` ("dead"), the interpreter compares the active node's own type against the
  incoming opcode: same type → calls a per-type "refresh/re-trigger" method (vtable+0x34)
  instead of reallocating; different type/opcode 0x14 → tears down the old node first.
  This is a **node-replacement state machine**, not a one-shot allocator.
- Every case follows the same shape: `FUN_0012a250(size, flags, "Tag", 0, 0)` (the Part-1
  tagged pool allocator) → `FUN_00150db0` (commit/register the allocation) → a dedicated
  constructor function `FUN_0000xxxx(3, ownerCtx, param_2, ...)` that fills in the new
  node's fields from the command record and returns.

### Full opcode table

| Opcode | Tag / type | Size | Constructor | Likely role |
|---|---|---|---|---|
| 0x00 | `Roller` | 0x350 (848B) | `FUN_00053eb0` | Rolling/pipe hazard or rail-roller object |
| 0x02 | `Debounce` | 0x30 (48B) | `FUN_0004b0d0` | Input/trigger debounce logic |
| 0x05 | *(sub-dispatch)* | — | `FUN_00049360` | **Destroy/replace current node** — see below |
| 0x06 | `Counter` | 0x3c (60B) | `FUN_000491a0` | Scripted counter (combo count? lap count?) |
| 0x07 | `Boost` | 0x100 (256B) | `FUN_00050b20` | Generic speed-boost pad/zone |
| 0x08 | `Timer` | 0x3c (60B) | `FUN_0004b210` | Scripted timer |
| 0x09 | *(unnamed, `&DAT_001891b0`)* | 0x40 (64B) | `FUN_00050950` | Unknown — tag string not resolved from text export |
| 0x0a | `UVScroll` | 0x68 (104B) | `FUN_00051090` | Scrolling texture (waterfalls, banners) |
| 0x0b | `TexFlip` | 0x2d4 (724B) | `FUN_000514c0` | Animated texture-flip (frame sequence) |
| 0x0c | `Fence` | 0x1ac (428B) | `FUN_00055e30` | Collision fence / boundary |
| 0x0d | *(unnamed, `&DAT_0018917c`)* | 400B | `FUN_00055870` | Unknown — tag string not resolved |
| 0x0e | `Cracked` | 0x2b8 (696B) | `FUN_00052c30` | Breakable/cracking ice or surface object |
| 0x0f | `LapBoost` | 0x100 (256B) | `FUN_00050c90` | Boost variant tied to lap progress |
| 0x10 | `RandomBoost` | 400B | `FUN_00054ff0` | Randomized boost pad |
| 0x11 | `CrowdBox` | 0x104 (260B) | `FUN_000558e0` | Spectator/crowd volume |
| 0x12 | `ZBoost` | 0x100 (256B) | `FUN_00050f60` | Vertical/height boost (jump pad?) |
| 0x13 | `UVScrollTexFlip` | 0x32c (812B) | `FUN_00052f00` | Combined scroll+flip texture effect |
| 0x14 | `cMeshAnim` | 0x6e0 (1760B) | `FUN_00053130` | Animated mesh object (also has a fast-path check earlier in the function for re-triggering) |
| 0x15 | `TrickTrigger` | 0xd8 (216B) | `FUN_0004f7c0` | **Trick zone trigger** — gated on `param_1+0xe8 != 0` |
| 0x16 | `Particle` | 0x1a0 (416B) | `FUN_0004fab0` | Particle emitter |
| 0x17 | `Movie` | 0x30 (48B) | `FUN_00053c40` | In-level movie/cutscene trigger |
| 0x18 | `TubeEndBoost` | 0x1c0 (448B) | `FUN_00055110` | Boost at the end of a half-pipe/tube section |
| 0x100 | `AnimObject` | 0x60 (96B) | `FUN_000c0620` | Generic animated object |
| 0x101 | `AnimDelta` | 100B | `FUN_000c0a20` | Delta/offset animation track |
| 0x102 | `AnimCombo` | 0x80 (128B) | `FUN_000c0ab0` | Animation tied to trick combos |
| 0x103 | `AnimTexFlip` | 0x2f0 (752B) | `FUN_000c0c40` | Animation-driven texture flip |

### Opcode 5 — the destroy/replace sub-dispatcher (`FUN_00049360`)

Not an object type — a **second-level opcode** read from `*(undefined4*)(param_2+0xc)`
(a sub-command). Every case first checks the currently active node at `param_1+100`: if
present and not already dead (`puVar1[5] != 5`), it calls the node's own vtable slot 0
(destructor) with argument `1`. Cases 2 and 3 then allocate a `"DeadNode"` placeholder
(0x2c bytes) and call `FUN_000490d0(param_1, 0 or 1)` — the `0`/`1` argument likely
distinguishes "cleanly finished" vs. "forcibly cleared." This is the **node lifecycle
teardown path** — matches the `DeadNode`/`RestoreNode` tags flagged in Part 1's
tagged-allocator sweep (`FUN_00049360` was already on that list).

## Two unresolved tags — confirmed hard boundary, structural read instead

Opcodes 0x09 and 0x0d pass `&DAT_001891b0` / `&DAT_0018917c` instead of a string
literal. Checked directly: both addresses are declared in the file as bare
`undefined DAT_001891b0;` / `undefined DAT_0018917c;` (a single untyped byte) — Ghidra's
C-exporter never resolved these to string content, meaning the text itself is genuinely
absent from this export, not just hard to grep for. This needs a live Ghidra read
(Listing view at that address) to ever get the literal name; **not achievable from the
flat text file**, confirmed rather than assumed.

That said, both constructors were read to recover their *behavior* even without a name:

- **Opcode 0x09** (`FUN_00050950`) — reads an ID (`param_4+0xc`) and a boolean
  (`param_4+0x10`) from the command record, sets the same collision-trigger flag bits
  seen elsewhere (`param_1[10]+0x68` bits 2 and 6), then looks up another object by that
  ID via `thunk_FUN_0013add0` and toggles bit 0 of a flag field on it based on the
  boolean. Reads as **"toggle some property (visibility? collision?) on another
  referenced level object by ID"** — a switch/trigger-linkage type, not a new visual
  effect. Sits between `Timer` (0x08) and `UVScroll` (0x0a) in the opcode sequence.
- **Opcode 0x0d** (`FUN_00055870`) — much shorter: stores the raw command payload
  pointer directly (`param_1[0x12] = param_4`) and delegates to a shared setup routine
  (`FUN_00051880`). Too little unique logic in the constructor alone to infer its
  purpose confidently; sits between `Fence` (0x0c) and `Cracked` (0x0e), and is the
  same size class (400 bytes) as `RandomBoost`.

Renamed both by behavior rather than by guessed tag name: `FUN_00050950` →
`UnknownOpcode09_TogglePropertyByID`, `FUN_00055870` → `UnknownOpcode0d_Construct` (kept
generic — deliberately not guessing a specific gameplay name here since the evidence
doesn't support one).

## What this means for the port

This opcode table **is the level file format's event/trigger vocabulary**. Any level
loaded by the game produces a stream of these opcodes (almost certainly from the
`nascript`-style data referenced in `data/config/nascript.inf`, found in the very first
session's global-string sweep) to place every interactive/animated element in a course.
Reimplementing this dispatcher against a modern engine — even with completely
reimplemented per-type logic — would let a port load and place all of a level's
interactive geometry correctly without having reverse-engineered a single one of the 24
target constructor functions yet. **That's the natural next unit of work**: pick one
constructor (e.g. `FUN_0004f7c0` for `TrickTrigger`, since that's the type most central
to actual gameplay) and read it fully.

## First constructor read: `TrickTrigger` (`FUN_0004f7c0`, opcode 0x15)

[default.xbe.c:50044](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:50044):

```c
undefined4 * __thiscall
FUN_0004f7c0(undefined4 *param_1,undefined4 param_2,undefined4 param_3,int param_4,int param_5)
{
  FUN_00048fc0(param_2,0x15,param_3);      // base-class init, tags this node as type 0x15 (== its own opcode)
  *param_1 = &PTR_FUN_001894f8;            // TrickTrigger's own vtable
  FUN_00059f80();
  param_1[0xd] = (int)*(short *)(param_4 + 0xc);   // trick/zone ID from the command record
  param_1[0xe] = *(undefined4 *)(param_4 + 0x10);  // duration or score-multiplier value
  param_1[0x12] = FUN_0015ca68();          // timestamp/tick or similar (seen elsewhere too)
  param_1[0x13] = param_5;                 // cache the owner/scene context
  param_1[0x10] = *(undefined4 *)(param_5 + 0x490);   // pulled from owner context
  param_1[0xf] = *(undefined4 *)(param_5 + 0x5710);   // pulled from owner context
  param_1[0x35] = param_1;                 // self-pointer (userdata for a callback?)
  FUN_0005a070(param_1[10] + 0x4c, param_1[10] + 0x58); // registers something using an embedded sub-object at [10]
  FUN_00059ee0(param_1 + 0x14);             // final setup on another embedded field
  return param_1;
}
```

**Confidence: structural only.** Confirmed shape: base-class construction records this
node's own type as `0x15` (matching its opcode — this is exactly the `piVar1[5]`
type-tag the dispatcher checks to decide "same type, just refresh" vs. "different type,
tear down and rebuild"). Two fields come straight from the trigger's command record
(likely trick-ID and duration/score), two more from the owner/scene context (likely
player or game-mode state), plus an embedded sub-object at `param_1[10]` that gets
registered with `FUN_0005a070` — almost certainly a **collision volume/bounding box**,
since that's what a trigger zone fundamentally needs. Haven't traced `FUN_0005a070`,
`FUN_00059ee0`, or `FUN_00048fc0` themselves yet — that's the natural next step if
someone wants the exact field semantics rather than the shape.

**Correction (confirmed live this session):** the "`param_1[10]` — almost certainly a
collision volume/bounding box" guess above is superseded by the later, verified base-class
finding: `param_1[10]` is the **raw level-authored command-record pointer** (see
"Base-class architecture" below). `TrickTrigger_Update` (next section) uses `param_1[10]`
as exactly that — it's passed straight into `ResourceContext_GetTableField(param_1[10],
4)` (was `FUN_00141970`, see below) to resolve column 4 of a 7-column table row keyed
off the command record, not touched as spatial/collision data anywhere.

## `TrickTrigger`'s vtable methods (found live this session — resolves the long-standing open thread)

Found by reading `TrickTrigger_Construct`'s `*param_1 = &PTR_FUN_001894f8` against the
live Ghidra database and walking `xrefs_from` on each 4-byte slot of that table — this is
exactly the boundary the project notes always flagged as needing GhidraMCP rather than the
flat text export (vtable *contents*, not just the assignment site, aren't visible in
`default.xbe.c`).

**Slot 0 — `TrickTrigger_Destruct`:** ordinary destructor. Tears down the embedded
tracking list (`FUN_00059910`), calls the base-class destructor (`FUN_00049040`), frees
the pool object back if its address falls inside the live pool's bounds
(`DAT_001fad60`/`DAT_001fad64`).

**Slot 1 — `TrickTrigger_Update`** (was misleadingly auto-tagged `New_Script_9` by the
earlier statistical pass, since it does allocate a tagged `Script` object — but only as
one side effect of a much larger method, not its purpose):

```c
void __fastcall TrickTrigger_Update(int *param_1)
{
  if (param_1[0x13] == 0) {
    // not currently tracking a target: run down a cooldown, notify at zero
    if (0 < param_1[0x12] && --param_1[0x12] == 0)
      (**(code **)(*param_1 + 0x54))(0);
    return;
  }
  // tracking a target: walk an embedded intrusive doubly-linked list at param_1+0x14
  // looking for a rider/player object (type ID 0x3ef) that matches the tracked target
  for (node in list-at(param_1[0x14])) {
    obj = node's owning object (via +0x84 backreference);
    if (obj's type == 0x3ef && obj matches param_1[0x13]) {
      if (obj->flags(+0x454) == 1) return;               // already-fired guard
      if (param_1[0xe] <= obj->stat(+0x5710) - param_1[0xf]) {  // threshold check
        handle = FUN_00141970(param_1[10] /* command record */, 4);
        if (handle != -1) {
          alloc tagged "Script" object;
          ScriptVM_CreateByHandle(3, handle, param_1[10], 0, 0, 0);  // fire the payload script
        }
        param_1[0x13] = 0;  // one-shot: clear tracking
        return;
      }
    }
  }
  (**(code **)(*param_1 + 0x54))(1);  // reached end of list, no match: notify(1)
}
```

**This is the actual "trick complete → fire scripted event" mechanism**, and it connects
every major subsystem documented in this file: the base-class node registry (the tracked
target lookup), the command-record pointer (`param_1[10]`, used to resolve the payload
script), and the outer script VM (`ScriptVM_CreateByHandle`, see below) — a level's
`TrickTrigger` doesn't just detect a condition, it can launch an arbitrary script (camera
cut, HUD popup, particle burst, chained sub-scripts...) as its payload, all through the
same mechanism `Script_PlayByName` uses.

**Update — the NodeRegistry bucket-iteration thread is now resolved.** After patching
GhidraMCP with `/disassemble_at` and `/create_function` endpoints (see "GhidraMCP patched"
in `RE_NOTES_INDEX.md`) and restarting Ghidra, created a real function over
`NodeRegistry_Remove`'s one non-destructor caller (`0x0002cbcc`, body ends `0x0002cd2f`).
It's not a "tick every live X" per-frame update loop as originally guessed — it's
**`Timer_RebuildPlayerRegistry`**: bulk-destroys every node of 3 type IDs (3, 4, 0xd —
none in the documented opcode table, likely internal/special IDs) via a newly-found
primitive `NodeRegistry_DestroyAllOfType` (walks a bucket, calls each node's own
destructor-vtable-slot-0 with the "free back to pool" argument), then drains the entire
`Timer` (type 8) bucket via another newly-found primitive `NodeRegistry_PeekHead` (read,
don't unlink, the bucket head — paired with an explicit `NodeRegistry_Remove` call to pop)
in a pop-loop, then for each active player re-inserts `*(playerPtr+0x58e0)` as a fresh
type-8 node — the same player-HUD-struct-pointer offset `HUD_ShowTimeGapCallout` touches.
Reads as "give every currently active player a fresh personal race/lap Timer node,"
likely run at race start or a checkpoint, though the exact trigger point isn't pinned
down. **The NodeRegistry primitive set is now complete**: `Insert`, `Remove`, `PeekHead`,
`DestroyAllOfType`.

Chasing the same undefined-code region further to resolve the `Script_PlayByName`
caller cluster (open thread 6) hit a different, more fundamental wall: several of those
call sites (`0x0002cdcd`, `0x0002cddb`, `0x0002cde9`, `0x0002d3bf`, `0x0002d475`,
`0x0002ea9e`) turned out to be **fragments of larger functions, not standalone ones** —
their disassembly references `unaff_EDI`/`unaff_EBX` (registers the decompiler flags as
used-but-never-set locally, meaning they're set by code that jumps in from further back).

Ran `ssx_analyze_gap.py` (`AutoAnalysisManager.reAnalyzeAll`) — added zero new functions,
proving the region had no other hidden gaps; that wasn't the problem. `xrefs_to` on every
fragment address also came back empty, which first looked like "referenced as raw data
somewhere" (a function-pointer table) — **that theory turned out to be wrong for the
`cdcd`/`cddb`/`cde9` trio specifically**, and the real answer was much better:

## RESOLVED: `cdcd`/`cddb`/`cde9` are a real x86 jump table — `GameMode_PlayStartupScript`

Patched GhidraMCP a second time with `/search_bytes`/`/search_address_refs`/`/read_bytes`.
The address-search hits were false positives (coincidental `E8`/`E9` relative-branch
displacement bytes that happen to numerically equal the target address — a real risk of
naive absolute-address byte search, worth remembering). But reading the *actual bytes*
immediately before `0x0002cdcd` (via `/read_bytes`) found the true story: a compiled x86
`switch` statement, `0x0002cdb0`–`0x0002cdef`, now named **`GameMode_PlayStartupScript`**:

```c
void GameMode_PlayStartupScript(void)
{
  switch(GameMode_Current) {          // was DAT_001dec94, renamed live via /renameData
  case 0: case 2: case 4: case 7: case 9:
    Script_PlayByName("RaceMode");
    return;
  default:
    Script_PlayByName("FreerideMode");
    return;
  case 3: case 5:
    Script_PlayByName("ShowoffMode");
    return;
  }
}
```

**This nails down `GameMode_Current`'s exact value table** — a variable referenced
constantly all session as a vague "game mode" gate (originally guessed "cutscene/replay"
for the `== 3 || == 5` case, e.g. in `Camera_AddShake`, `Rumble_TrackMaxA`/`B`). It's
actually the **SSX game-mode selector**: `0`/`2`/`4`/`7`/`9` = Race, `3`/`5` = **Showoff**
(SSX's solo trick-attack/big-air mode — which is *why* camera shake, force-feedback, and
HUD callouts gate on it, not some vague replay state), default (`1`/`6`/`8`/etc.) =
Freeride. Every earlier "gated on game mode 3/5" note in this file has been corrected to
say ShowoffMode.

My original `0x0002cdcd`/`cddb`/`cde9` function boundaries were wrong — 5 bytes too late,
right after each case's `PUSH <string address>` instruction, not at the true jump-table
target. Needed one more GhidraMCP patch (`/delete_function`, to remove a function
definition and let `create_function` re-derive the correct merged body) to fix. Corrected
functions, all clean single units now:

- `0x0002cdc8` → **`GameMode_PlayRaceModeScript`** — `Script_PlayByName("RaceMode")`
- `0x0002cdd6` → **`GameMode_PlayShowoffModeScript`** — `Script_PlayByName("ShowoffMode")`
- `0x0002cde4` → **`GameMode_PlayFreerideModeScript`** — `Script_PlayByName("FreerideMode")`

The three string literals (`"RaceMode"`/`"ShowoffMode"`/`"FreerideMode"`) live packed
consecutively at `0x001878fc`–`0x00187923` — worth defining as a proper string array/table
in Ghidra if continuing this thread.

## RESOLVED: `d3bf`/`d475`/`ea9e` — countdown gating, a matched NoCountdown/StartCountdown pair

Applied the same "read the bytes immediately preceding, look for the true entry" method
to the other fragment trio. All three turned out to be reached by fall-through from a
**recurring compare-chain idiom** (not a jump table this time — a straight-line
`CMP EAX,N; JE ...` chain) testing `DAT_001dec90 == 7` first, then `GameMode_Current`
against the exact same `{2, 7, 4, 3, 5, 0, 9}` set `GameMode_PlayStartupScript`'s jump
table uses — independent cross-confirmation of `GameMode_Current`'s value table from a
second call site.

- **`0x0002d290` → `Level_PreloadAndCheckCountdown`** (was `FUN_0002d3bf`'s containing
  function). Track/level preload setup — touches a global camera/context struct, branches
  on two flags on its context param to set up either a per-entity loop or a simpler init
  path, calls `"PreLoad"`-tagged setup helpers, then — if `DAT_001dec90==7` or
  `GameMode_Current` isn't in the race/showoff set — plays
  `Script_PlayByName("NoCountdown")`. (Ghidra's *stored* function body stayed
  conservative, `0002d290`–`0002d2a6`, even after fixing the entry point — but the
  decompiler still traces the full control flow correctly regardless of the stored body
  size, so the finding stands.)
- **`0x0002d440` → `GameMode_CheckAndPlayNoCountdown`** (was `FUN_0002d475`'s containing
  function). A standalone, simpler version of the same check — no preload logic, just
  "not a real race/showoff mode → play NoCountdown, notify via vtable+0x30."
- **`0x0002ea50` → `GameMode_CheckAndPlayStartCountdown`** (was `FUN_0002ea9e`'s
  containing function). The **inverse** counterpart: "IS a real race/showoff mode → play
  `Script_PlayByName("StartCountdown")`, notify via vtable+0x30, and (if
  `GameMode_Current != 6`) set a field and call `FUN_000cf390(4,-1)`" — then
  unconditionally loops `FUN_00034160` once per active entity, the same
  per-active-entity notification idiom seen in `Timer_RebuildPlayerRegistry` and
  `HUD_ShowTimeGapCallout`.

All three original mis-placed function boundaries were deleted (`/delete_function`, the
round-3 GhidraMCP patch) and recreated at the correct entry points. **Both fragment
clusters that motivated the round-2/round-3 GhidraMCP patches are now fully resolved** —
open thread 6 is done. The method that worked both times: when `xrefs_to` comes back
empty for a suspicious function, don't assume a function-pointer table — read the raw
bytes immediately before it first (`/read_bytes`) and look for a switch/compare-chain
whose fall-through or jump-table target lands exactly at the address in question. Only
reach for `/search_address_refs` (data-table search) after that comes up empty too.

**Follow-on question resolved this session:** what populates `param_1+0x14`'s tracking
list turns out to be a much bigger discovery than TrickTrigger itself — see "Broad-phase
collision: sweep-and-prune spatial system" below. `param_1+0x14` isn't a TrickTrigger-
private list; it's this object's slot in a shared spatial-partition structure that
essentially every trigger-volume type participates in.

Still-open question, now much better understood across two later sessions:
- **`+0x5710` is the same field the in-race HUD draws directly as the on-screen score**
  (`HUD_DrawRaceOverlay`'s bit-8 panel: `CRT_FormatString(buf, "%d", *(rider+0x5710))`
  — see `RE_NOTES_race_hud.md`) and the same field `Race_ComputeRankings` uses as the
  Showoff-mode sort key (initializing it to fixed medal-time-target constants in that
  mode). `TrickTrigger_Update`'s check —
  `param_1[0xe] <= *(int*)(rider+0x5710) - param_1[0xf]` — reads as "has the score
  increased by at least a threshold amount (`param_1[0xe]`, the trick-ID/duration
  value from construction) since a baseline (`param_1[0xf]`, captured once at
  construction time)" — i.e. **this trigger fires its script event once the rider has
  accumulated enough score/points since being armed**, which is exactly what a "do a
  trick worth N points to unlock this event" gate would look like. This is now a
  well-supported interpretation, not just a guess.
- **Still not found: who actually increments `+0x5710` during gameplay.** A dedicated
  follow-up session (see `RE_NOTES_rider_update_chain.md`) tried and ruled out several
  hypotheses via direct evidence: not a literal-offset write anywhere in
  `default.xbe.c` (grepped exhaustively), not in `Rider`/`Player`/`OtherRider`'s C++
  update-vtable chain (traced fully — 16 of 18 subsystem stubs are stripped no-op
  profiler markers in this retail build, the 2 survivors don't touch this field), and
  `ScriptVM_Tick` (the per-script-instance ticker) doesn't reference it either. Most
  likely location if continuing: one of the 3 dynamically-attached component sub-lists
  found on the Rider object (`rider+0x28`/`+0x80`/`+0xd8`, each walked once per frame
  via the newly-named `Component_UpdateAll`) — the actual node types attached to those
  lists were not identified.
- **RESOLVED this session (live Ghidra):** `FUN_00141970` → **`ResourceContext_GetTableField`**.
  Confirmed sibling of `FUN_00141ed0`/`FUN_00141f30` — all three read the same shared
  field, `param_1+0x14` (a "resource context" pointer), just different sub-tables hanging
  off it (`+0x2c`/`+0x30` for the script name/ID/data tables the other two use, `+0x10`
  for this one). Its actual contract: given a command record (`param_2`) with a non-null
  pointer at `+0x6c`, reads a 16-bit row index from `+0x14` of *that*, then returns
  `table[rowIndex * 7 + columnIndex]` (4-byte fields, `columnIndex` = `param_3`) from the
  table at `resourceContext+0x14+0x10`. **Update: confirmed NOT the same table as
  `TrackTable` (`0x001b6474`)**, despite the matching 7-dword stride — `TrackTable` is
  read via `xrefs_to` as a hardcoded literal address directly, 13+ times, all from
  `UI_BuildResultsScreen` (see `RE_NOTES_results_screen.md`, now fully resolved); nothing
  reaches it through the resource-context indirection. `ResourceContext_GetTableField`'s
  own table is a different 7-column table, whereabouts and contents not identified this
  pass.

## Broad-phase collision: sweep-and-prune spatial system (found this session, live Ghidra)

Followed `TrickTrigger_Construct`'s calls to `FUN_0005a070`/`FUN_00059ee0` (right before
the field that `TrickTrigger_Update` later walks as its "tracked rider" list) and checked
their live xrefs. **These aren't TrickTrigger-specific** — they're called from at least 12
different node constructors: `Boost_Construct`, `LapBoost_Construct`, `ZBoost_Construct`,
`Roller_Construct`, `TrickTrigger_Construct`, `New_AnimOverlapInfo`, plus several
unidentified siblings (`FUN_000559c0`, `FUN_00050bf0`, `FUN_00050d80`, `FUN_00051020`,
`FUN_00036490`). Every spatial/trigger-volume type in the engine participates in the same
shared structure.

**The shape is an unambiguous, textbook 3-axis sweep-and-prune (sort-and-sweep) broad-
phase collision system:**

- **`SweepPrune_InitNode`** (`FUN_00059f80`) — initializes 3 embedded circular-list
  sentinel sub-nodes within the object (classic empty-list init: prev=next=self), one per
  axis.
- **`SweepPrune_BindAxisBounds`** (`FUN_0005a070`) — wires up 6 pointer fields on the
  object from the level command record's embedded bounding sub-fields — the (min, max)
  bound pair per axis that the sort keys read.
- **`SweepPrune_Register`** (`FUN_00059ee0`) — allocates a unique object slot ID from a
  free-index pool, then inserts the object's 3 sub-nodes into 3 shared sorted linked
  lists (one per axis) owned by a global spatial-partition context.
- **`SweepPrune_Unregister`** (`FUN_00059910`) — the mirror-image teardown, called from
  `TrickTrigger_Destruct` (and presumably every other registrant's destructor).
- **`SweepPrune_MaintainAxis`** (`FUN_00059cb0`) — re-sorts an object's position within
  one axis's list when its bound may have moved, bubbling it past neighbors via float-key
  comparison and calling `SweepPrune_ToggleAxisOverlap` on every neighbor swapped past.
  Dense pointer algebra (4 near-identical unrolled blocks, one per axis-direction) —
  shape is unambiguous, exact field semantics not byte-verified.
- **`SweepPrune_ToggleAxisOverlap`** (`FUN_00059b10`) — the actual pair-tracking core.
  Looks up a per-object-pair 3-bit mask (one bit per axis, keyed by both objects'
  allocated slot IDs) and toggles the bit for the axis just swept. Losing the last bit of
  a full `0b111` (all 3 axes overlapping) tears down a broad-phase pair record; gaining
  the 3rd bit allocates a new pair record from a free-list and links it into an
  active-pairs list. **This is the exact moment a genuine broad-phase AABB overlap is
  detected or lost** — the standard sweep-and-prune "pair management" step.

**This is very likely the actual "player enters a trigger volume" detection mechanism**
for the entire spatial/trigger family (`Boost`, `TrickTrigger`, `Roller`, `LapBoost`,
`Fence`, `Cracked`, ...), not just TrickTrigger — `TrickTrigger_Update`'s walk of
`param_1[0x14]` is walking this object's slot in one of these shared axis lists, filtering
the neighbors it finds there for rider-type (`0x3ef`) objects. Confidence: high on the
overall algorithm shape (multiple independent lines of evidence — 12-constructor call-site
survey, textbook sort-and-sweep pointer patterns, an explicit 3-bit-mask overlap-pair
state machine); medium on exact field-level semantics (not byte-verified line by line).

**RESOLVED (much later session, live Ghidra):** found the actual pair-processing
consumer. The "free-list"/"pair record" system `SweepPrune_ToggleAxisOverlap` allocates
from turns out to be a distinct, fully-mapped subsystem: **`OverlapManager`**, allocated
as a tagged `"OverlapMan"` object (`0x44` bytes) by `InGameState_LoadLevel`'s
level-setup sequence (see `RE_NOTES_application_boot.md`), owning a **`"OverlapFlags"`**
buffer (`0x1fe00`/130,560 bytes — confirmed exactly `256*255/2*4` bytes: a packed
upper-triangular pairwise flag matrix for up to 256 tracked objects, one 4-byte flag
word per unique pair). `OverlapManager_ComputePairIndex` (was `FUN_00059760`) computes
the flat array offset for any object pair via the standard triangular-matrix-index
formula. When `SweepPrune_ToggleAxisOverlap` flips the 3rd axis-overlap bit to `1`
(all 3 axes now overlapping — a genuine AABB overlap begins), it calls
**`OverlapManager_LinkOverlapRecord`** (was `FUN_00059fb0`) **twice** — once per
object — inserting the new pair record into *each object's own* per-object linked
list of active overlaps (head/tail fields per object, not a single global list).
Losing the 3rd bit calls **`OverlapManager_UnlinkOverlapRecord`** (was `FUN_0005a000`)
twice, symmetrically.

**This means overlap notification is poll-based, not callback-based**: there's no
direct virtual dispatch from `OverlapManager` back into `TrickTrigger`/`Boost`/etc. when
an overlap begins. Instead, each object is expected to walk its own per-object overlap
list during its own `Update()` — exactly matching `TrickTrigger_Update`'s already-
documented behavior (it walks a list looking for a tracked rider each frame, rather than
reacting to an explicit "you've been entered" event). The full pipeline, now mapped
end-to-end: `SweepPrune_MaintainAxis` (3-axis interval sort) →
`SweepPrune_ToggleAxisOverlap` (per-axis-crossing bit toggle) → `OverlapManager`
(persistent pairwise flag + per-object linked-list bookkeeping) → each trigger-family
object's own `Update()` method polling its list. 7 renames for the `OverlapManager`
cluster (`OverlapManager_Construct`/`Destruct`/`UnlinkNode`/`ComputePairIndex`/
`ReorderBySize`/`LinkOverlapRecord`/`UnlinkOverlapRecord`).

**Update — the 5 unnamed call sites, checked (live Ghidra):**
- **`0x00050bf0` → `Boost_Construct_Alt`**, **`0x00050d80` → `LapBoost_Construct_Alt`**,
  **`0x00051020` → `ZBoost_Construct_Alt`** — all three are alternate-overload
  constructors (different entry point, same shared vtable and construction sequence) for
  types already fully documented above. `0x189a58` (ZBoost's vtable) was already
  identified in the `ZBoost` constructor notes below, confirming the match. Same pattern
  already noted for `TrickTrigger` (`FUN_0004f860`, a sibling overload never separately
  read) — evidently common across this whole family, not a one-off.
- **`0x000559c0` → `Roller_Construct_Alt` — RESOLVED, not a new type.** Chasing its one
  caller found a whole second dispatcher (`Script_DispatchOpcode_Alt`) mirroring
  `Script_DispatchOpcode`'s exact type-ID switch but routing to `_Alt` constructors — case
  0 ("Roller", same tag/size as the known `Roller_Construct`) calls this one. See "RESOLVED
  — per-type bucket iteration + the `_Alt` constructor family's real purpose" below for the
  full chain (this is the same investigation that resolved all three `_Alt` siblings above,
  plus the long-open bucket-iteration question).
- **`0x00036490` — substantial, different in kind.** Writes ~29 pointer fields (looks
  like bone/joint name bindings — very plausibly tied to the skeletal `cMeshAnim`
  system, opcode 0x14) before reaching `SweepPrune` registration much further down (not
  fully read). Has real xrefs (2 callers: `FUN_00048890`, `FUN_0005bee0`), so unlike the
  `GameMode_*`/FX-cluster fragments earlier, this one's function boundary is genuinely
  correct — a decompiler `unaff_retaddr` flag here is just local-variable confusion, not
  a boundary problem. Good candidate for a full read next session; likely connects the
  `cMeshAnim` skeletal system to the sweep-and-prune broad-phase, which would be a
  meaningful architectural link not yet documented anywhere in this file.

**Sibling constructors right next to it worth noting for later:** `FUN_0004f860`
(shares the same vtable `PTR_FUN_001894f8` — likely an alternate TrickTrigger
constructor overload), and `FUN_0004fab0` (`Particle`, opcode 0x16, immediately
following in the file with a near-identical shape — `FUN_00048fc0(param_2, 0x16,
param_3)` then its own vtable `PTR_FUN_00189590`). The whole `0x00048xxx`-`0x0004fxxx`
address range is clearly this constructor family, laid out in the same order as the
opcode table — reading them in sequence would be efficient if continuing this thread.

## Constructor survey (round 2): two distinct node families emerge

Read five more constructors (`Boost`/`RandomBoost`/`TubeEndBoost`, `LapBoost`, `Fence`,
`Cracked`, `Counter`). A clear split in shape emerged:

### Spatial/trigger nodes (`TrickTrigger`, `Boost` family, `LapBoost`, `Fence`, `Cracked`, likely `Roller`)
All share this pattern:
- Base-init via `FUN_00048fc0(param_2, <type-id>, param_3)` — note `Boost`'s constructor
  (`FUN_00050b20`) takes the type ID as a *parameter* rather than hardcoding it, and is
  reused for **three opcodes**: `0x07` (Boost), `0x10` (RandomBoost), `0x18`
  (TubeEndBoost) — confirmed by call sites at [default.xbe.c:53596](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:53596) (`FUN_00050b20(param_2,0x10,...)`
  = RandomBoost) and [default.xbe.c:53641](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:53641) (`,0x18,` = TubeEndBoost). One shared
  implementation, three data-driven flavors.
- Own vtable pointer (each type distinct: `PTR_FUN_00189880` for the Boost family,
  `PTR_FUN_001898f8` for LapBoost, `PTR_FUN_00189b38` for Fence, `PTR_FUN_00189ba8` for
  Cracked, `PTR_FUN_001894f8` for TrickTrigger).
- An **embedded collision/physics-volume sub-object at `param_1[10]`**, consistently
  referenced across every type in this family (`FUN_0005a070(param_1[10]+0x4c,
  param_1[10]+0x58)` to register it; `FUN_00051310(param_1[10], 0xffffffff)` in
  Cracked to reset/attach it; direct flag manipulation `*(uint*)(param_1[10]+0x68)` in
  Cracked). This is almost certainly the shared base class every spatial trigger type
  inherits from — a positioned, bounded volume in the level.
- Fields pulled from the command record convert design-tool units to game units, e.g.
  Boost: `*(float*)(param_5+0x18) * 100.0`; Fence: a 4-float transform combined via
  what looks like a quaternion/matrix multiply (the "redundant-looking" `fVar1 =
  a+b; fVar2 = b+a` pairs at [default.xbe.c:54163](E:\Emulators\Roms\Xbox Original\Ssx Decompiled\default.xbe.c:54163)-54170 are very likely cross-terms of a
  quaternion multiply that just happen to read as simple sums in decompiled form, not
  actually redundant computation).
- **`LapBoost` is genuinely per-player**: its constructor (`FUN_00050c90`) loops over
  the active player count (`*(int*)(...+0x88)`) and stashes a 2-byte per-player value
  from `FUN_000ab710(playerIndex)+0x144` — confirms lap-boosts are tracked
  independently per racer (each player can trigger it once per lap), unlike the shared
  single-instance Boost/RandomBoost/TubeEndBoost.
- **`Fence` links to neighbors**: fields at `param_1[0x69]`/`[0x6a]` (if non-zero) each
  trigger a call into `FUN_000555f0` (the very same function Part 1 flagged as a
  `Fence`-tagged allocator) with a direction flag (1 vs 0) — fences are chained
  segments, and constructing one updates its linked neighbor(s).
- **`Cracked`** sets a countdown-ish field to `0x1e` (30) — likely a hit-count or
  frame-delay before the crackable surface breaks.

### Logic nodes (`Counter` confirmed; likely `Timer`, `Debounce` — same shape, not yet read line-by-line)
Different, lighter-weight pattern seen in `Counter` (`FUN_000491a0`, opcode 6):
- Base-init via a **different** function, `FUN_000aabf0(param_2, 6)` (not the
  `FUN_00048fc0` family the spatial types use).
- No collision-volume sub-object. Instead: `param_1[10] = param_3` (stores the owner
  context directly), and critically `*(undefined4**)(param_3 + 100) = param_1` — this
  **writes the new Counter's own pointer back into the owner/script-context's "active
  node" slot** (`+100`/`0x64`, the exact field `Script_DispatchOpcode` reads as
  `piVar1` at the top of the dispatch loop). This confirms logic nodes occupy the
  single "active node" slot per context (only one can be active at a time — matches the
  dispatcher's same-type-refresh / different-type-teardown logic), whereas spatial
  trigger nodes appear to persist independently as level geometry rather than
  competing for that one slot.

**Correction after reading `Timer`/`Debounce` (round 3):** the clean two-family split
above doesn't hold up. `Debounce` (`FUN_0004b0d0`, opcode 2) and `Timer`
(`FUN_0004b210`, opcode 8) both use `FUN_00048fc0` — the *same* base-init as the
spatial/trigger family, not `Counter`'s `FUN_000aabf0`. But neither has a `param_1[10]`
collision volume, and neither shows the "+100 self-register" write `Counter` does —
they're just minimal objects storing 1-2 timestamps (`FUN_0015ca68()`, called twice in
`Timer`, once in `Debounce`) behind their own tiny vtables (`PTR_FUN_00189298` for
Debounce, `PTR_FUN_00189308` for Timer). So it's better described as a **spectrum, not
two families**: base-init function and vtable are per-type as expected, but "has a
spatial volume" and "self-registers into the +100 active-node slot" are independent,
type-specific choices, not a clean split. `Counter`'s self-registration into `+100` may
be a `Counter`-specific behavior (e.g. it needs to be pollable as "the current active
counter" for some other system) rather than evidence of a general "logic node" class.
Treat the "+100 active-node slot" purely as what the dispatcher itself checks at the
top of `Script_DispatchOpcode` (same-type refresh vs. teardown-and-replace), not as a
broader architectural pattern until more types are read.

## Constructor survey (round 4): UVScroll, TexFlip, CrowdBox

- **`UVScroll`** (`FUN_00051090`, opcode 0x0a) — like `Boost`, takes its type ID as a
  parameter (`FUN_00048fc0(param_2, param_3, param_4)`), so it's plausibly shared with
  another opcode too, though no second caller found yet. Reads two command-record
  fields (`+0x18`/`+0x1c`, likely scroll-speed U/V) and sets a countdown-ish field to
  `0x1e` (30, the same constant `Cracked` used) — a recurring "30" constant across
  types worth treating as a named constant once more context surfaces (30 frames? 30
  ticks at some fixed timestep?).
- **`TexFlip`** (`FUN_000514c0`, opcode 0x0b) — own dedicated constructor (type 0xb
  hardcoded, not shared). Notable: supports a **randomized start offset** — if the
  command record's timing field is negative, it generates a random float via a classic
  IEEE-754 mantissa trick (`(randomBits & 0x7fffff | 0x3f800000) - 1.0`, giving [0,1))
  scaled by 8.0, then seeds a timer from it. A sentinel value of exactly `-2.0` sets a
  distinct flag (`param_1[0x15] = 1`) — likely "loop forever" vs. "flip once" vs.
  "randomized start" as three distinct timing modes for the same effect type. Useful
  for a port: texture-flip animations (e.g. blinking lights, animated signage) can
  desync their start times by design, not just by accident.
- **`CrowdBox`** (`FUN_000558e0`, opcode 0x11) — short. Reads two command-record fields
  (`+0x10`/`+0x14`, likely density/count and spawn radius), delegates member-slot setup
  to `FUN_00052da0` (zero-initializes a 0x44-byte block, consistent with an array of
  crowd-member slots). Spectator/crowd volumes are populated from level-authored
  density parameters, not hardcoded.

- **`ZBoost`** (`FUN_00050f60`, opcode 0x12) — checked whether it shares `Boost_Construct`
  (they sit near each other and follow the identical `*100.0` scale-field pattern) — it
  does **not**; it's a separate but near-identical constructor (own vtable
  `PTR_FUN_00189a58`), with two extra fields (`param_4+0x20`/`+0x24`) beyond what plain
  `Boost` reads — plausibly a Z-axis direction/height vector, fitting "vertical boost."
  So the earlier assumption that near-identical constructors imply shared code was
  wrong for this pair — `Boost`'s sharing across 3 opcodes is confirmed real (same
  function address called from 3 sites), but that's not a general rule; each
  near-duplicate needs its own check.

- **`UVScrollTexFlip`** (`FUN_00052f00`, opcode 0x13) — genuine code reuse confirmed:
  it calls `UVScroll_Construct` directly (`FUN_00051090(param_2, 0x13, param_3,
  param_4)`, passing its own opcode as the type ID) for its base setup, then inlines
  the same randomized-start-offset timing logic seen in `TexFlip_Construct` (identical
  IEEE-754 mantissa RNG trick, identical `-2.0` sentinel check). So it's UVScroll's
  fields plus TexFlip's timing behavior, composed by calling one and duplicating the
  other's logic rather than calling both — a real, confirmed instance of the
  constructor-sharing pattern first seen with `Boost`, just partial (base call +
  inlined extension) rather than a full shared function.
- **`Particle`** (`FUN_0004fab0`, opcode 0x16) — short. Reveals the **common
  command-record layout**: every opcode's data starts with a 12-byte header (opcode at
  `+8`, a sub-field at `+0xc` used differently per type), and type-specific payload
  data follows immediately after (`param_4 + 0xc` is passed to the per-type unpacker —
  here `FUN_000e77e0`, "parse particle params"). Also disables the emitter
  (`FUN_000e62d0(0)`) if a computed field comes out negative — likely a validity/rate
  check.

**Running count: 15 of 24 opcodes read** (`Roller` partial, `Debounce`, `Timer`,
`Counter`, `Boost`/`RandomBoost`/`TubeEndBoost`, `LapBoost`, `Fence`, `Cracked`,
`Movie`, `UVScroll`, `TexFlip`, `UVScrollTexFlip`, `CrowdBox`, `ZBoost`, `Particle`).
Remaining: `cMeshAnim` (0x14), `TrickTrigger`'s sibling overloads, and the four `Anim*`
types (0x100-0x103, all in the `0x000c0xxx` range, separate from the rest which cluster
in `0x00048xxx`-`0x00055xxx` — worth checking whether that's a genuinely different
subsystem/author before assuming they follow the same conventions).

## Constructor survey (round 5, final): the `Anim*` family and `cMeshAnim` — opcode table now complete

- **`cMeshAnim`** (`FUN_00053130`, opcode 0x14) — the largest object (0x6e0/1760 bytes).
  Read at the structural level: walks a bone/joint hierarchy (`*(int*)(iVar1+4)` =
  bone count, `*(int**)(iVar1+8)` = bone array), copying a 16-dword (64-byte, i.e. a
  4x4 matrix) block per bone from either a default bind-pose constant table
  (`_DAT_001fadd0...`) or an override pose if present, then combining it with a parent
  transform via `FUN_0004af10`. This is a **skinned/skeletal mesh animation instance**
  — confirms "cMeshAnim" = "class MeshAnim," a full animated character/object skeleton.
  Not traced to full field-level precision (heavy matrix math, would need
  `FUN_0004af10` traced too) — structural understanding only, which is the right depth
  for something this math-heavy without a specific reason to go further.

- **`AnimObject`** (`FUN_000c0620`, opcode 0x100) is the **shared base constructor for
  the entire `Anim*` family** — `AnimDelta`, `AnimCombo`, and `AnimTexFlip` all call it
  directly, passing their own opcode as the type ID (exactly like the `Boost` family's
  pattern, confirmed again here). It:
  - Reads a min/max frame range from the command record and converts both to seconds
    by dividing by **30.0** — this resolves the recurring unexplained "30"/"0x1e"
    constant seen throughout the whole opcode survey (`Cracked`'s countdown, `UVScroll`
    and `TexFlip`'s deadline field, `AnimCombo`'s frame conversion): **the engine runs
    a fixed 30fps simulation rate**, and frame-count fields are consistently divided by
    30 to get seconds. Worth treating as a named constant (`FRAMES_PER_SECOND = 30`)
    in any reimplementation.
  - Supports **randomized duration** (`FUN_0012a4f0`, if a flag at `+0x24` is set) and
    **randomized start time within a range** (`FUN_0012a530`, if `+0x1c != 0`) — two
    different range-random helpers, distinct from the mantissa-trick RNG used
    elsewhere.
  - Supports a **reverse-playback flag** (`param_5+0x28 == 4` negates the rate).
  - Allocates mesh-animation-frame data via `FUN_000c0400` — confirmed in Part 1 as the
    `MeshAnimFrames`-tagged allocator call site.
- **`AnimDelta`** (`FUN_000c0a20`, opcode 0x101) — thin subclass of `AnimObject`, adds
  almost nothing beyond its own vtable and two small fields. Likely a simple
  offset/delta-track player.
- **`AnimCombo`** (`FUN_000c0ab0`, opcode 0x102) — subclass of `AnimObject`, additionally
  allocates a `ComboXform`-tagged buffer sized `frameCount << 6` (i.e. one 64-byte —
  4x4 matrix — slot per frame), and reads two extra timing fields (also `/30.0`). This
  is the trick-combo animation type: a per-frame keyframe transform track, matching its
  name.
- **`AnimTexFlip`** (`FUN_000c0c40`, opcode 0x103) — subclass of `AnimObject`, plus the
  **exact same randomized-start-offset idiom** (IEEE-754 mantissa RNG trick, `-2.0`
  sentinel) seen verbatim in `TexFlip_Construct` and inlined again in
  `UVScrollTexFlip_Construct`. This makes **three separate occurrences of identical
  inlined code**, never factored into a shared helper in the original — a good
  candidate to actually factor into one function in a clean-room reimplementation, even
  though the original doesn't.

## Base-class architecture (traced after the opcode survey — this corrects earlier notes)

Read the shared base-class chain that ~20 of the 24 constructors funnel through:
`FUN_00048fc0` → `FUN_000aabf0` → `FUN_000aa730`. This is a significant correction to
things stated earlier in this document, so read this section before trusting the
per-opcode notes above on the meaning of `param_1[10]`.

### `FUN_00048fc0(this, ownerOrRecord, typeID, commandRecordPtr)` — renamed `NodeBase_Construct`

```c
FUN_00048fc0(undefined4 *param_1,undefined4 param_2,undefined4 param_3,int param_4)
{
  FUN_000aabf0(param_2,param_3);           // -> deeper base init, see below
  param_1[10] = param_4;                    // ** CORRECTION: this is the raw command-record
                                             //    pointer, NOT a separate collision-volume object **
  *param_1 = &PTR_FUN_00188ec8;             // the shared base vtable used by every node type
  *(undefined4 **)(param_4 + 100) = param_1; // writes the new node's address into the
                                             // COMMAND RECORD's own +100/0x64 field
  return param_1;
}
```

**Correction to every earlier note in this document calling `param_1[10]` a "collision
volume" or "physics-volume sub-object":** it is not a separate embedded component —
it's a direct pointer to the same command-record data the level format stores on disk.
Fields like `*(uint*)(param_1[10]+0x68)` that every constructor pokes bits into are
editing the **level-authored trigger/placement record directly**, not a runtime-only
copy. This is actually a cleaner design than what was assumed: position, bounding
volume, and flags live once, in the data the level ships with; runtime objects just
keep a pointer to it rather than duplicating storage.

**Also corrects the earlier "logic node vs. spatial node" / "`Counter` uniquely
self-registers into `+100`" theory:** the write to `commandRecord+100` happens
**universally**, for every type that goes through `NodeBase_Construct` — it isn't
`Counter`-specific behavior. `Counter` looked different only because it calls the
deeper base function directly instead of through this wrapper, but does the equivalent
thing. This means the dispatcher's `+100` check (`piVar1 = *(int**)(*(int*)(param_1+0xe4)+100)`
at the top of `Script_DispatchOpcode`) is reading **the specific command record's own
"currently live instance" backreference** — each individual scripted trigger placement
in a level has exactly one live runtime instance tracked this way, not a single shared
per-context slot as originally guessed. That's what makes the same-type-refresh /
different-type-teardown logic make sense: re-processing the *same* placed command
finds its own still-alive instance and refreshes it; a *different* opcode at that
record's slot means the level script changed what this placement should be, so the old
instance is torn down first.

### `FUN_000aabf0(this, ownerOrRecord, typeID)` — renamed `NodeBase_ConstructRoot`

```c
FUN_000aabf0(undefined4 *param_1,undefined4 param_2,int param_3)
{
  *param_1 = &PTR_FUN_0019a328;        // transient vtable during construction
  FUN_000aa730(param_1,param_2,param_3);  // ** inserts this node into a global hash registry, see below **
  param_1[5] = param_3;                // stores the TYPE ID -- this is exactly the field
                                        // Script_DispatchOpcode reads as piVar1[5] for
                                        // same-type-vs-different-type comparison
  *param_1 = &PTR_FUN_0019a34c;        // the real root-base vtable
  if (param_3 == 5) {                  // note: type 5 here is DeadNode's type (constructed
    iVar1 = DAT_001e3c80;              // with literal type=5 from Script_DestroyOrReplaceNode) --
    DAT_001e3c80 += 1;                 // dead/placeholder nodes get their own instance counter,
  } else {                             // separate from every "real" object type, which all
    iVar1 = DAT_001e3c84;              // share the second counter. Reads like a debug/profiling
    DAT_001e3c84 += 1;                 // split: "N live objects" vs. "M dead/pending-cleanup nodes."
  }
  param_1[8] = iVar1;                  // sequential instance ID from whichever counter
  param_1[9] = <context value or 0>;   // conditional on a global chain being initialized
  return param_1;
}
```

This is the **true root base class for every script node in the entire system**,
confirming the field layout: `[5]` = type ID, `[8]` = sequential instance ID, `[9]` =
a context value. Every single one of the 24 opcode types ultimately passes through
this (or the near-identical destructor-path variant `FUN_00049010`/`FUN_000aacc0` seen
throughout, not yet traced).

### `FUN_000aa730(registryContext, newNode, typeID)` — renamed `NodeRegistry_Insert`

```c
FUN_000aa730(int param_1,int param_2,int param_3)
{
  param_3 = param_3 * 0x34;                          // bucket index * 52 bytes/bucket
  pbVar1 = (byte*)(*(int*)(param_1 + 4) + param_3);  // bucket base + this type's bucket
  if ((*pbVar1 & 1) != 0) {
      /* insert at one linked-list anchor point within the bucket */
  } else {
      /* insert at a different anchor point within the same bucket */
  }
  // classic intrusive doubly-linked list: param_2+4 = next pointer,
  // param_2+8 = address of the pointer-that-points-to-this-node (enables O(1) unlink)
}
```

**This is a genuine architectural discovery, not just a naming exercise:** every
constructed script node is inserted into a **global hash table/bucket array keyed by
type ID** (52-byte buckets, `param_1+4` = bucket array base — `param_1` here being the
overall script-system context, the same value threaded through `Script_DispatchOpcode`
as its own `param_1`). Combined with the per-command-record `+100` backreference from
`NodeBase_Construct` above, the system has **two independent ways to reach any given
node**: by the level-script command that spawned it (one instance per placement,
supporting re-trigger/refresh), and by its type (supporting efficient "update every
active Boost" / "update every active TrickTrigger" style per-frame iteration without
scanning unrelated types). This is a sensible, deliberate design for a data-driven
level-trigger system, not incidental — worth treating `param_1` (the script-system
context passed to `Script_DispatchOpcode`) as **the node registry / manager object**
for the whole level, not merely "an opaque context."

**Removal counterpart confirmed**: `FUN_000aa7a0(node)` —

```c
void FUN_000aa7a0(int param_1) {
  if ((*(int*)(param_1+4) != 0) && (*(int*)(param_1+8) != 0)) {
    *(int*)(*(int*)(param_1+8) + 4) = *(int*)(param_1+4);       // prevPtrAddr's target = this->next
    *(undefined4*)(*(int*)(param_1+4) + 8) = *(int*)(param_1+8); // next->prevPtrAddr = this->prevPtrAddr
  }
}
```

Textbook O(1) unlink from an intrusive doubly-linked list using the pointer-to-pointer
back-reference `NodeRegistry_Insert` set up — completes the insert/remove pair exactly
as predicted. Renamed `NodeRegistry_Remove`.

## RESOLVED (live Ghidra, this session): per-type bucket iteration + the `_Alt` constructor family's real purpose

Two long-open threads — "what walks a NodeRegistry bucket each frame" and "what are the
`_Alt` sibling constructors (`Boost_Construct_Alt` etc.) actually for" — turned out to be
the same discovery. Found by chasing `NodeRegistry_Remove`'s one non-destructor caller
(`Timer_RebuildPlayerRegistry`, see above) one step further: **`NodeRegistry_GetNext`**
(was `FUN_000aa800`) completes the iteration primitive set alongside `NodeRegistry_PeekHead`
— given a current node and its type, returns the next node in that bucket (same
empty-list-sentinel check as `PeekHead`), or 0 at the end. Together they let code walk
every live node of a type, not just insert/remove one.

The actual walker: **`GameState_ResetAndRebuildTransientNodes`** (was `FUN_000bc590`) —

```c
void GameState_ResetAndRebuildTransientNodes(undefined4 *param_1)
{
  NodeRegistry_DestroyAllOfType(3);
  NodeRegistry_DestroyAllOfType(0xd);
  NodeRegistry_DestroyAllOfType(4);        // same 3 special types Timer_RebuildPlayerRegistry bulk-destroys
  ...
  for (each of 4 hardcoded type IDs {2, 5, 7, 8}) {   // Debounce, DeadNode, Boost, Timer
    for (node = NodeRegistry_PeekHead(type); node != 0; node = NodeRegistry_GetNext(node, type))
      (**(code **)(*node + 0x20))(param_1);            // per-instance vtable notify
  }
  BatchConstruct_NodesOfType(param_1, 3);
  BatchConstruct_NodesOfType(param_1, 4);
  New_Dead_Object(param_1);
}
```

Reads as a genuine **"race restart / checkpoint" reset routine**: bulk-destroy the same 3
special/internal type IDs `Timer_RebuildPlayerRegistry` also clears, walk every live
`Debounce`/`DeadNode`/`Boost`/`Timer` instance and notify it (vtable slot `+0x20` —
plausibly "OnReset"), then batch-reconstruct fresh types 3 and 4 from a command stream via
**`BatchConstruct_NodesOfType`** (was `FUN_000bbb20` — reads a count, then constructs that
many nodes back-to-back by repeatedly calling **`Script_DispatchOpcode_Alt`**, was
`FUN_000b9e00`).

`Script_DispatchOpcode_Alt` is the actual answer to the `_Alt` mystery: it's a **complete
second copy of `Script_DispatchOpcode`'s type-ID switch**, byte-for-byte the same type IDs
and tagged-pool sizes/names, except every case calls that type's **`_Alt`** constructor
variant instead of the normal one (confirmed live — `Boost_Construct_Alt` appears by name
in its decompiled case 7, proving the naming loop closed correctly). It also found a new
member of the family in the process: **`Roller_Construct_Alt`** (was `FUN_000559c0`,
previously one of the 2 "new node type" leads — resolved as Roller's alternate
constructor, case 0, not a new type after all).

**Two registries, two purposes, now both fully mapped:**
- `NodeRegistry_*` (type-keyed) — `Insert`/`Remove`/`PeekHead`/`GetNext`/`DestroyAllOfType`
  — used for bulk lifecycle operations (destroy-all-of-type, walk-all-of-type-and-notify),
  confirmed here.
- `SweepPrune_*` (position-keyed) — broad-phase "what's near what" collision detection,
  documented separately above. Not the same mechanism, don't conflate them (this was
  flagged as a risk earlier and holds up: `GameState_ResetAndRebuildTransientNodes` only
  ever touches `NodeRegistry_*`).

## The player/rider object itself, and how a race actually starts

The last remaining `SweepPrune`-family lead (`0x00036490`, previously guessed as a
possible `cMeshAnim` link) turned out to be much bigger: it's **`Rider_ConstructBase`**,
the shared base constructor for the player character object itself —

```c
Rider_ConstructBase(param_1, param_2, param_3) {
  if (param_3 != 0) {
    // ~29 embedded sub-object vtable pointers assigned (param_1[0xc], [0x138], [0x13f], ...)
    FUN_00033b20();
  }
  // ~28 writes of the form: *(vtable[N]+4 + offset + param_1) = &PTR_FUN_xxxxx
  //   -- initializing ~28 embedded animation/mesh sub-component vtables
  NodeBase_ConstructRoot(this, 0x3ef);   // type 0x3ef -- confirmed the "rider" type ID
                                          // TrickTrigger_Update checks for
  *param_1 = &PTR_FUN_001886e4;
  SweepPrune_BindAxisBounds(...);
  SweepPrune_Register(...);              // the player participates in the SAME broad-phase
                                          // system as trigger volumes
}
```

Type `0x3ef` is the exact type ID `TrickTrigger_Update` checks for when scanning its
tracked-rider list (see above) — this confirms, definitively, what a "rider" object is in
this codebase. The `param_3 != 0` branch (skippable) lets callers either set up their own
character-specific vtables first and pass `param_3 = 0` to skip re-initializing them, or
let this function do full first-time setup.

**Two thin subclass constructors call it**, both found via live xrefs:
- **`Player_Construct`** (was `FUN_0005bee0`) — calls `Rider_ConstructBase(param_2, 0)`
  after stashing its own 28 character vtables, then (gated on a camera-follow-state check)
  calls `New_PlayerSnapShot` (an already-known tagged allocator from the very first
  session's string-mining pass) — replay/instant-replay snapshot support. **This is the
  human-controlled player.**
- **`OtherRider_Construct`** (was `FUN_00048890`) — identical shape, no snapshot logic.
  **This is an AI/CPU-controlled rider.**

**And the function that calls both of them is the actual race-setup routine** — found by
applying the same "read bytes before it" boundary-fixing method one more time (both
callers lived in the same undefined region as the `GameMode_*` cluster, right next to it):

**`Race_SpawnRidersAndLoadAssets`** (was `FUN_0002dd40`) —
```c
Race_SpawnRidersAndLoadAssets(raceContext) {
  allocate 4 tagged "Controller" objects (0x274 bytes each)
  for each active participant (DAT_001de8fc count, DAT_001de900 array, stride 0x98 --
                                the SAME array Timer_RebuildPlayerRegistry/
                                HUD_ShowTimeGapCallout/Level_PreloadAndCheckCountdown use):
    if participant's +0x7d flag byte is negative:   // the SAME sign-bit flag
                                                      // Level_PreloadAndCheckCountdown checks
      allocate tagged "Computer" object (0x61c0 bytes), call OtherRider_Construct(8, 1)
    else:
      allocate tagged "Player" object (0x6190 bytes), call Player_Construct(8, 1)
  load "data/char/texxbx.big" and "data/char/mdlxbx.big"  // shared character texture/model archives
}
```

This is the actual capstone that ties the whole session's `GameMode_*`/`Timer_*` work
together: **the same per-participant array and the same sign-bit flag drive race-mode
selection, countdown gating, Timer re-registration, AND rider spawning** — one coherent
race-setup subsystem, not several unrelated mechanisms that happened to share a struct.

## The complete race/tutorial lifecycle state machine

`Race_SpawnRidersAndLoadAssets`'s address is itself referenced as a data value at
`0x0019a574` — pulling on that thread found the whole containing table
(`0x0019a560`–`0x0019a58c`, 12 dwords) and, from there, **the master state machine
driving the entire race and tutorial flow**, complete with debug-name strings for every
state:

**`RaceState_SetState`** (was `FUN_0002ce10`) — given a state ID, sets the state number,
a debug-name string, and a per-state handler-context pointer:

| ID | State name | ID | State name |
|---|---|---|---|
| 1 | `PreRace` | 7 | `PreLesson` |
| 2 | `StartRace` | 8 | `RestartLesson` |
| 3 | `Countdown` | 9 | `SelectLesson` |
| 4 | `Race` | 10 | `EndLesson` |
| 5 | `EndRace` | 11 | `ReplayEndRace` |
| 6 | `LoadLesson` | | |

Two parallel flows, sharing one state machine: the **race** flow (`PreRace` →
`StartRace` → `Countdown` → `Race` → `EndRace`/`ReplayEndRace`) and the **tutorial**
flow (`LoadLesson` → `PreLesson` → `SelectLesson`/`RestartLesson` → `EndLesson`) — SSX
Tricky's in-game tutorial system, not previously documented anywhere in this project.
This also confirms a loose end from the very first session: the `"ReplayEndRace"` /
`"EndRace"`-looking string fragment noticed in passing near the `GameMode_*` string table
(`0x00187918` area) months of sessions ago is exactly these two state names.

**Update — the entire 12-slot table is now mapped.** One slot turned out not to be a
function pointer at all (see the caution below); every other slot is now read and named:

| Address | Slot | Function | Role |
|---|---|---|---|
| `0x0019a560` | 0 | `RaceState_NullHandler` (was `FUN_000b6140`) | trivial `return 0` — the "no active state" placeholder |
| `0x0019a564` | 1 | `Race_WriteDebugMarkerAndResetCounters` (was `FUN_0002cfa0`) | writes a tagged 24-byte debug record (`0xdeadbef1` sentinel, same style as the pool allocator's corruption-detection canaries) via `DebugBuffer_Write`, then `NodeBase_ResetInstanceCounters` |
| `0x0019a568` | 2 | `RaceState_TransitionAndRestore` (was `FUN_0002cfe0`) | save current state → transition to a target state (from `+0x20`) → restore the original state — a "peek at another state's config" utility |
| `0x0019a56c` | 3 | `RaceState_TransitionIfChanged` (was `FUN_0002ea30`) | a state-change guard: only stashes/transitions if the incoming state ID differs from the tracked one |
| `0x0019a570` | 4 | `Race_ResetCountersAndDispatch` (was `FUN_0002dc80`) | resets per-race counters/flags, resets the two `NodeBase` instance-ID counters (`DAT_001e3c84`/`DAT_001e3c80`) to `1` — fresh race, fresh node IDs. Ends with `JMP [EDX+0x2c]` (a **tail call through a vtable slot**, confirmed via raw disassembly — Ghidra's jump-table heuristic misfired on it and printed a spurious "too many branches" warning, but this was never actually unresolved; the decompiled pseudocode already showed it correctly as `(**(code**)(*param_1+0x2c))()`) |
| `0x0019a574` | 5 | `Race_SpawnRidersAndLoadAssets` (was `FUN_0002dd40`) | the race-setup routine (documented above) |
| `0x0019a578` | 6 | `GameMode_PlayStartupScript` (was `FUN_0002cdb0`) | the jump table dispatching `RaceMode`/`ShowoffMode`/`FreerideMode` startup scripts (documented above) |
| `0x0019a57c` | 7 | `Race_DestroyRidersAndUnloadAssets` (was `FUN_0002cac0`) | the exact teardown counterpart to slot 5 — destroys every spawned rider, frees the 4 `Controller` objects, unloads the character texture/model archives |
| `0x0019a580` | 8 | `Race_ResetPlayerRoster` (was `FUN_0002e040`) | resets fields, then (gated on a flag byte) loops over active participants doing per-player setup — a `StartRace`-entry handler |
| `0x0019a584` | 9 | `Race_ClearSpecialNodeTypes` (was `FUN_0002cb80`) | bulk-destroys the same 3 special type IDs (3/0xd/4) `Timer_RebuildPlayerRegistry` clears, with inverted flag values compared to it — likely a paired prepare/finalize step around the same cleanup |
| `0x0019a588` | 10 | `Timer_RebuildPlayerRegistry` (was `FUN_0002cbcc`/mis-scanned as `0002cbd0`) | per-player Timer-node re-registration (documented above) |
| `0x0019a58c` | 11 | `Race_ComputeRankings` (was `FUN_0002e2d0`) | full standings/medal-time computation, see below |

### `Race_ComputeRankings`, read in full — medal time targets and final standings

This is the single largest function found this session (200+ lines), and it's genuinely
the capstone of the whole race-lifecycle picture: it computes final race standings,
**including the actual medal (gold/silver/bronze) time targets for every difficulty
level**, and writes each rider's final rank into `+0x140` — the exact field
`HUD_ShowTimeGapCallout` reads for "next-ranked player." Read in full rather than left
structural, because the medal-time table alone is high-value, concrete data:

- **Per-mode rival-finding**: in `RaceMode` (`GameMode_Current` 2 or 4), finds each
  rider's nearest two rivals by calling `Race_RecordRivalComparisonByte` (was
  `FUN_00031be0`) across every rider pair, then a tie-break (`Team_FindSlotByRiderTag`,
  was `FUN_000697f0`) if exactly 2 candidates are found. In `ShowoffMode`/other modes
  (3/5/7), instead just enumerates active riders directly (`Team_CountActiveTeams`/
  `Team_FindSlotByRiderTag`, were `FUN_0006a520`/`FUN_000697f0`).
- **A concrete medal-target table** — see the CORRECTION immediately below, which
  supersedes the original reading of this bullet. The constants are right; what they
  mean was wrong.

> ### CORRECTION (later session): these are SHOWOFF SCORES, not times
>
> This section originally described the table as "medal **time** targets ...
> (milliseconds — 55s/40s/25s)" and `DAT_001dec90` as a "difficulty flag". Both
> are wrong, and three independent lines of evidence say so:
>
> 1. **The destination field is the score, not the time.** The writes go to
>    `rider+0x5710`, which this project separately established *is* the running
>    trick score (TrickCombo's `this+0xe0` with `this=rider+0x5630`;
>    `0x5630+0xe0 = 0x5710`) and which `RE_NOTES_INDEX.md` records as being
>    "drawn directly as the on-screen score". The race **time** is
>    `rider+0x448` — and the very same function reaches for `+0x448` in its
>    other branch, so it clearly distinguishes the two.
> 2. **The gate is ShowoffMode.** The writes only happen for
>    `GameMode_Current` 3 and 5, which this file independently identifies as
>    ShowoffMode (via `Rider_UpdateCueTimer` and `Camera_AddShake`, both gated
>    on 3||5). Showoff is the trick-scoring mode.
> 3. **Pipedream carries the highest targets.** As par times, 800000/500000/250000
>    would be 13/8/4 minutes on a halfpipe. As trick scores on the game's pure
>    trick venue, they are exactly right.
>
> **`DAT_001dec90` is the current track index, not a difficulty.** Its readers
> include `Level_GetCurrentTrackNameTag`, `Level_LoadTrackAssets`,
> `InGameState_LoadLevel` and `TrackIntroMusic_SelectAndPlay`.
> `Level_GetCurrentTrackNameTag` (0x0007b800) switches on it and returns the
> asset tag:
>
> | idx | tag | track | idx | tag | track |
> |---|---|---|---|---|---|
> | 0 | `garibald` | Garibaldi | 6 | `pipedrea` | Pipedream |
> | 1 | `snowdrea` | Snowdream | 7 | `untracke` | Untracked |
> | 2 | `elysium` | Elysium Alps | 8 | `tokyo` | Tokyo Megaplex |
> | 3 | `mesablan` | Mesablanca | 9 | `pipedrea` | (second Pipedream slot) |
> | 4 | `merqury` | Merqury City | 10 | `trick` | — |
> | 5 | `aloha` | Aloha Ice Jam | 11 | `alaska` | Alaska |
>
> Cross-check: the 10 tracks that ship a `.aip` are exactly these tags minus
> `trick` (and with 6/9 collapsed) — two unrelated sources agreeing.
>
> **The table, as showoff score targets** (gold/silver/bronze = `param_2` 0/1/2):
>
> | Track | Gold | Silver | Bronze |
> |---|---|---|---|
> | 0 Garibaldi | 55,000 | 40,000 | 25,000 |
> | 1 Snowdream | 95,000 | 65,000 | 35,000 |
> | 2 Elysium Alps | 225,000 | 150,000 | 75,000 |
> | 3 Mesablanca | 225,000 | 150,000 | 75,000 |
> | 4 Merqury City | 275,000 | 175,000 | 125,000 |
> | 5 Aloha Ice Jam | 175,000 | 115,000 | 75,000 |
> | 6 Pipedream *(mode 3 only)* | 800,000 | 500,000 | 250,000 |
> | 8 Tokyo Megaplex | 350,000 | 225,000 | 100,000 |
> | 11 Alaska | 500,000 | 300,000 | 150,000 |
>
> Tracks 7, 9 and 10 have no `if` of their own — the engine leaves the field
> untouched, which is an absence, not a zero. Note also two constants Ghidra
> mistyped as pointers: `&DAT_0001c138` is the integer **115,000**, and the
> `(param_2 != 2) - 1 & X` tail is a branchless "X if medal==2 else 0".
>
> Ported as `port/src/game/race.{h,cpp}`, asserted in `port/tests/asset_test.cpp`.

- **Two special-case branches**: `GameMode_Current == 5` uses the table above via a
  shared label (`LAB_0002e518`); `GameMode_Current == 3` has its own separate literal
  table only for `DAT_001dec90 == 6` (`800000`/`500000`/`250000`), otherwise falls
  through to the same shared table.
- **Writes per-rider results into several large parallel global arrays**
  (`DAT_001df410`, `DAT_001df428`+stride `0xbc`, `DAT_001df4c4` region) — a results/HUD
  data table, plausibly consumed by a post-race results-screen function (candidate link
  to `UI_BuildResultsScreen` in `RE_NOTES_results_screen.md`, not cross-checked this
  pass).
- **Final sort and rank assignment**: builds a sort key per rider (`+0x5710` stat for
  Showoff modes, negated time for others, raw index for Lesson mode), sorts via
  **`Utility_SortArray`** (was `FUN_0014b250` — a generic array sort, heap-like index
  pattern, not race-specific), then writes each rider's sorted position into `+0x140` —
  **confirmed, this is exactly the rank field `HUD_ShowTimeGapCallout` reads** for its
  "next-ranked player" branch, closing that loop from earlier in this file.

**Update: all the sub-helpers have now been read and renamed.** They resolve into three
related pieces, not random small helpers:

- **Rival head-to-head comparison grid**: `Race_RecordRivalComparisonByte` (was
  `FUN_00031be0`, thiscall(this=riderRecordGrid, rivalRiderPtr)) reads the rival's slot
  index off the rival's own struct (+0x490), rounds an FPU-stack value via
  `CRT_ftol_TruncateToInt64` (was `FUN_0015ca68`, a plain compiler-generated ftol-style
  round helper, not game logic), and stores it as a byte into
  `this[(rivalIndex*5+10)*8]` — a 5-rival-slot comparison-outcome grid per rider.
  `Race_CollectRivalComparisonBytes` (was `FUN_00073cc0`) later copies bytes out of the
  same grid (confirmed via the identical `(slot*5+10)*8` stride, reached through a
  global registry pointer `DAT_001e3c7c+0x72c`) into an output buffer in `GameMode ==
  4`, likely feeding the results/leaderboard screen (see
  `RE_NOTES_results_screen.md`). `Race_GetClampedRoundedTime` (was `FUN_00031bb0`) and
  `Race_ComputeRoundedTimeGap` (was `FUN_000339e0`) are small getters/fallbacks around
  the same cached-time-at-`+0x448` pattern, the second snapping to zero when a computed
  time gap is within a small threshold of a target.
- **Team assignment subsystem** (used by the Showoff/team branch, `GameMode` 3/5/7): a
  4-slot team-tag array at `this+0x3344` (stride `0x44`) with an associated team-pointer
  array at `this+0x3358`. `Team_GetRiderTagByte` (was `FUN_000697d0`) reads a rider's own
  tag byte; `Team_FindSlotByRiderTag` (was `FUN_000697f0`) matches a rider to a team
  slot; `Team_GetPointerByTag` (was `FUN_0006a570`) resolves a tag value to its team
  pointer; `Team_CountActiveTeams` (was `FUN_0006a520`) counts how many of the 4
  canonical team tags currently have an active (non-null) team.
- **Post-race highlight/award records**: `Race_UpdateHighlightRecords` (was
  `FUN_00069df0`, thiscall(this, recordSlot)) is a running-max tracker — 4 int-valued
  stats and 3 float-valued stats each compared against a stored record, overwriting both
  the value and an owner tag (a rider id) whenever the new value is larger, plus a
  6-slot generic best-value+owner array. Called once per race for the top-2 ranked
  riders. Reads as the underlying data for a post-race "highlights" or "bragging rights"
  screen (biggest air, longest grind, etc. — a real SSX Tricky feature); the actual UI
  consumer hasn't been cross-checked yet.

All 10 renames applied live and added to `ssx_auto_rename.py`.

**Caution on how this table was mapped — a real mishap, caught and fixed:** slot 10's raw
dword pointed to `0x0002cbd0`, which turned out to be **4 bytes into the middle of**
`Timer_RebuildPlayerRegistry` (right after alignment padding that happens to look like a
plausible entry point on its own). Blindly running `/create_function` on every dword in
the table — without first checking whether each address already fell inside a *different*
already-known function's range — briefly truncated `Timer_RebuildPlayerRegistry`'s stored
body down to 4 bytes. Caught immediately (a `get_function_by_address` spot-check right
after) and fixed (`/delete_function` the erroneous entry, re-disassemble, recreate at the
function's true entry `0x0002cbcc`). **Lesson: check a candidate table-entry address isn't
already covered by a known function before creating a new one there.**

## Opcode table survey: complete

All 24 opcodes now have at least structural documentation; 20 of them read in enough
detail to describe their actual behavior (only `Roller` and `cMeshAnim` stopped at
structural-level due to heavy 3D/matrix math, and `TrickTrigger`'s sibling overloads
weren't separately read). The two unresolved tag strings (opcodes 0x09, 0x0d) were named
by behavior instead (`UnknownOpcode09_TogglePropertyByID`, `UnknownOpcode0d_Construct`) —
their tag strings are confirmed genuinely absent from both the text export and the live
Ghidra database (bare untyped bytes), not just hard to find. **This is very likely enough
detail to reimplement the entire level-trigger placement system in a port** even before
the individual per-type gameplay *behavior* (as opposed to construction) is reverse-
engineered — the opcode table plus command-record format tells you exactly how to parse a
level's script data and instantiate placeholder objects of the right type at the right
position with the right parameters.

## Outer script VM (found this session, live Ghidra)

`Script_PlayByName` doesn't hand off to `Script_DispatchOpcode` directly — there's a whole
separate bytecode interpreter layered in between, invisible to the flat-text export
because the decompiler's call-site argument/type propagation for it was incomplete (the
callees only resolved once read live against the actual Ghidra database). This was the
payoff of finally using GhidraMCP's REST API (`http://127.0.0.1:8080`) as the original
project notes always intended, rather than text-mining `default.xbe.c` alone.

**Two-layer architecture, now confirmed:**

- **Outer layer — `ScriptVM_*` (this section).** A generic event/flow-control bytecode
  VM. `Script_PlayByName(name)` → `ScriptVM_CreateByName` allocates a 240-byte (`0xf0`)
  tagged `"Script"` pool object (type ID `0x3e9`), resolves the name to a bytecode
  pointer, and ticks it once immediately. Every subsequent frame, `ScriptVM_Tick` runs a
  fetch-execute loop: count down a wait-timer against a per-frame delta; while not
  waiting, call `ScriptVM_DispatchOpcode` once per command and advance a byte-offset
  cursor (sizes read from a per-command length table), until the dispatcher signals
  "yield this frame" (return 1), "terminate" (return -1), or the command stream is
  exhausted — then fires a completion vtable callback. Confirmed used for **both** the
  front-end/menu flow (`FEInit_Boot` → `Script_PlayByName("FEStartScript")`) **and**
  general event sequencing — `Script_PlayByName` has 18 live call sites, clustered in the
  known menu code (`0x0007cxxx`–`0x0007dxxx`) plus a second, currently-unanalyzed cluster
  around `0x0002cxxx`–`0x0002exxx` (see the NodeRegistry note above — same undefined-code
  region, likely the actual event/movie-trigger-driven call sites; worth a "create
  function" pass to identify what's calling `Script_PlayByName` there).
- **Inner layer — `Script_DispatchOpcode`** (documented above). The 24-opcode
  level-object placement dispatcher. **Opcode 0 of `ScriptVM_DispatchOpcode` is a direct
  call into `Script_DispatchOpcode`** — this is the missing link connecting the two
  systems: the event-script VM can spawn/place level-script objects as just one of its
  ~27 command types.

**`ScriptVM_DispatchOpcode`'s own opcode table** (0–0x1a, structural pass only — read
enough to classify each, not full behavioral depth):

| Opcode | What it does |
|---|---|
| 0x00 | Delegates to `Script_DispatchOpcode` (spawn a level-object node) |
| 0x01 | Spawns a tagged `"Camera"` object (`New_Camera` → `FUN_00053990`) — cutscene camera |
| 0x02 | `ScriptVM_SpawnEmitterOp` — spawns one of 3 particle-emitter variants (`Emitter`/`SplinePath`/`CollideEmitter`), branch on a sub-type field |
| 0x03, 0x09, 0x0a, 0x0b | Call a vtable method (offsets `+0x54`/`+0x58`/`+0x5c`) on the currently active owner node — event notifications to whatever object this script is attached to (this is the exact mechanism `TrickTrigger_Update` calls into via `vtable+0x54`) |
| 0x04 | Stores an operand into a local result slot (`param_1+0x40`) |
| 0x05 | Multi-way conditional/branch (4 sub-cases) — looks like a wait-until/condition-check opcode, reuses the same IEEE-754-mantissa RNG idiom seen elsewhere in the codebase |
| 0x06 / 0x0f | **RESOLVED — `Rider_UpdateCueTimer`** (was `FUN_000329e0`, the last genuinely opaque `ScriptVM` opcode). Both gated on `GameMode_Current == 3 or 5` (**ShowoffMode**). Tracks a per-rider timer, and when it crosses certain thresholds, conditionally calls one of three sibling helpers — `Rider_TriggerCueIfFlagSet`/`Rider_TriggerTrackedCue`/`Rider_TriggerTrackedCueWithSetup` — which all funnel into **`Rider_ScheduleTimedCallback`** (was `FUN_001194b0`): a generic 48-slot timed-callback scheduler embedded in the rider object (duration + callback pointer + params, classic "do X after N ms" pattern — durations of 3000ms/500ms and a `0x7f` (127, max-volume-scale) literal seen at call sites strongly suggest an audio cue, but not confirmed enough to commit to "Audio_*" naming — could also be a VFX or haptic cue). 0x06 passes a float (probably a cue-specific parameter), 0x0f passes none (stop/reset). |
| 0x08 | **`ScriptEvent_QueuePush`** (was `FUN_00113280`, resolved live this session) — pushes a scripted event onto a per-object queue backed by a genuine ring buffer (**`RingBuffer_Push`**, was `FUN_0010f410` — confirmed via the classic `(write+1) % capacity == read` full-check idiom). Copies a 16-byte payload (position/quaternion, read from the command record's `+0x30..+0x3c`) plus a `(30.0, -1.0)` duration-like float pair (`30.0` = the engine's established 30fps constant). Has a bypass: under a specific sub-mode (`DAT_001dec90==4`) and message ID (`param_3==0x7a`), skips the queue entirely and dispatches immediately via a vtable call instead — a priority/synchronous-handling special case. Exact consumer/purpose of the queue not identified (candidate: subtitle or generic per-object scripted-cue queue), but the mechanism itself is now fully confirmed, not opaque. |
| 0x0d | `HUD_ShowTimeGapCallout` (was `FUN_00035450`, fully read) — **not** "signal script complete" as originally guessed. Gated on a not-yet-fired flag (`+0x448`) and a readiness check. In single-player mode, averages a per-player stat (`+0x3a4`) across the field (classic racing "time gap" calc); in versus modes, finds the next-ranked player instead. Writes the result into a HUD struct (`+0x58e0`) and fires two UI-trigger calls. Reads as "show a time-gap-to-opponent popup." |
| 0x0e | `FUN_00031a50` — opaque. Gated on game mode (3/5) *and* the owner's `+0x454` flag (1 or 3): calls `FUN_0005ec70`. Separately, if `+0x44c==1`: calls `FUN_0010fac0`. Same flag fields TrickTrigger uses (`+0x454` as an "already-fired" guard there) — likely a generic per-node notification, not renamed pending clearer evidence. |
| 0x10 | `Camera_AddShake` (was `FUN_00031aa0`, fully read) — gated on `GameMode_Current` 3/5 (**ShowoffMode**), accumulates an amount into a field on the global active-camera struct, then unconditionally calls a second helper. Camera-shake accumulator, specific to trick-attack mode. |
| 0x11 / 0x12 | `Rumble_TrackMaxA`/`Rumble_TrackMaxB` (were `FUN_00031af0`/`FUN_00031b20`, fully read) — confirmed running-maximum accumulators (only update if the new value is larger) for two separate force-feedback channels, each paired with an apply call (`FUN_00122e80`/`FUN_00122f20`) right after in the dispatcher. |
| 0x17 | `FUN_000317b0` (unrenamed — the decompiler's call-site type propagation for this one is ambiguous, mixing a possible 0-arg and 2-arg call to what looks like the same address; needs `set_function_prototype` to pin down before trusting a name) resolves a script-handle field, gating a call into `Audio_PlayScaledCue` (`FUN_00078430`, fully read — scales a volume/pitch float by a per-player constant, then calls into the audio subsystem). |
| 0x18 | `CmdTable_LookupByIndex` (was `FUN_0013ad90`) → `Camera_WarpToTarget` (was `FUN_00035fd0`, fully read) — heavy quaternion math (reuses the `FUN_00025ab0` quaternion-multiply helper seen elsewhere in the codebase) that builds an offset transform from a mode-selected quaternion and the target's stored orientation, then flags a global camera-slot table entry dirty. Reads as a cutscene camera warp/teleport-to-target command. |
| 0x19 | `RailMan_SetSegmentState` (was weakly tagged `New_RailMan`, now fully read) — maintains a 128-entry rail-grind segment ID→state table, lazily allocating its backing pool object, and toggles a flag bit on the resolved object via `thunk_FUN_0013add0` (the same ID→object resolver `UnknownOpcode09_TogglePropertyByID` uses). Connects to the rail-grind FX cluster in `RE_NOTES_ubertrick_fx_cluster.md`. |
| 0x07, 0x15, 0x1a | Spawn a **nested child `Script` instance** (`ScriptVM_CreateByHandle`/`ScriptVM_CreateByID`) — a subroutine-call-like opcode, letting one script launch another |

**Opcode table survey status: ALL ~27 opcodes now have at least one confirmed behavioral
detail — 0x06/0x0f, the last genuinely opaque one, is resolved** (`Rider_UpdateCueTimer`,
see the table row above). The exact real-world effect (audio vs. VFX vs. haptic cue)
isn't pinned down with certainty, but the mechanism — a per-rider timer feeding a generic
timed-callback scheduler — is fully understood, unlike the earlier "genuinely opaque"
state. GhidraMCP's REST plugin originally exposed 26 endpoints, none of which could
create a function or disassemble at an arbitrary address — that's since been fixed by
patching the plugin directly (see `RE_NOTES_INDEX.md`, "GhidraMCP patched").

## Renames applied

- `FUN_00049960` → `Script_DispatchOpcode`
- `FUN_00049360` → `Script_DestroyOrReplaceNode`
- `FUN_0004a5a0` → `Script_PlayByName`
- `FUN_0007daa0` → `FEInit_Boot` (supersedes the "Character/board/track select" guess in
  `RE_NOTES_frontend_menu_map.md` — correct that file's entry for this address)
- `FUN_0004a730` → `ScriptVM_DispatchOpcode` (was guessed `New_Script_5`)
- `FUN_00049890` → `ScriptVM_Tick`
- `FUN_0004a470` → `ScriptVM_CreateByHandle`
- `FUN_0004a530` → `ScriptVM_CreateByName`
- `FUN_0004a610` → `ScriptVM_CreateByID`
- `FUN_000496d0` → `ScriptVM_SpawnEmitterOp`
- `FUN_00053740` → `TrickTrigger_Destruct`
- `FUN_000537b0` → `TrickTrigger_Update` (was guessed `New_Script_9`)
- `FUN_00031aa0` → `Camera_AddShake`
- `FUN_00031af0` → `Rumble_TrackMaxA`
- `FUN_00031b20` → `Rumble_TrackMaxB`
- `FUN_00078430` → `Audio_PlayScaledCue`
- `FUN_0013ad90` → `CmdTable_LookupByIndex`
- `FUN_00035fd0` → `Camera_WarpToTarget`
- `FUN_00035450` → `HUD_ShowTimeGapCallout`
- `FUN_00050270` → `RailMan_SetSegmentState` (was guessed `New_RailMan`)
- `FUN_0005a070` → `SweepPrune_BindAxisBounds`
- `FUN_00059f80` → `SweepPrune_InitNode`
- `FUN_00059ee0` → `SweepPrune_Register`
- `FUN_00059910` → `SweepPrune_Unregister`
- `FUN_00059cb0` → `SweepPrune_MaintainAxis`
- `FUN_00059b10` → `SweepPrune_ToggleAxisOverlap`
- `FUN_000aa7d0` → `NodeRegistry_PeekHead`
- `FUN_000aa8d0` → `NodeRegistry_DestroyAllOfType`
- `FUN_0002cbcc` → `Timer_RebuildPlayerRegistry` (created via the new `/create_function`
  endpoint — didn't exist as a function in Ghidra's database before this session)
- `DAT_001dec94` → `GameMode_Current` (data rename — the SSX game-mode selector)
- `FUN_0002cdb0` → `GameMode_PlayStartupScript`
- `FUN_0002cdc8` → `GameMode_PlayRaceModeScript`
- `FUN_0002cdd6` → `GameMode_PlayShowoffModeScript`
- `FUN_0002cde4` → `GameMode_PlayFreerideModeScript`
- `FUN_0002d290` → `Level_PreloadAndCheckCountdown`
- `FUN_0002d440` → `GameMode_CheckAndPlayNoCountdown`
- `FUN_0002ea50` → `GameMode_CheckAndPlayStartCountdown`
- `FUN_00141970` → `ResourceContext_GetTableField`
- `FUN_00050bf0` → `Boost_Construct_Alt`
- `FUN_00050d80` → `LapBoost_Construct_Alt`
- `FUN_00051020` → `ZBoost_Construct_Alt`
- `FUN_00113280` → `ScriptEvent_QueuePush`
- `FUN_0010f410` → `RingBuffer_Push`
- `FUN_000aa800` → `NodeRegistry_GetNext`
- `FUN_000bc590` → `GameState_ResetAndRebuildTransientNodes`
- `FUN_000bbb20` → `BatchConstruct_NodesOfType`
- `FUN_000b9e00` → `Script_DispatchOpcode_Alt`
- `FUN_000559c0` → `Roller_Construct_Alt`
- `FUN_000f6f50` → `FX_TrailManager_ScanTrackSegments` (see `RE_NOTES_ubertrick_fx_cluster.md`)
- `FUN_00141770` → `TrackSegment_GetByIndex` (see `RE_NOTES_ubertrick_fx_cluster.md`)
- `FUN_000e0770` → `FX_TrailSlot_ClaimByProximity` (see `RE_NOTES_ubertrick_fx_cluster.md`)
- `FUN_00036490` → `Rider_ConstructBase`
- `FUN_0005bee0` → `Player_Construct`
- `FUN_00048890` → `OtherRider_Construct`
- `FUN_0002dd40` → `Race_SpawnRidersAndLoadAssets`
- `FUN_0002ce10` → `RaceState_SetState`
- `FUN_0002cac0` → `Race_DestroyRidersAndUnloadAssets`
- `FUN_000aa6d0` → `NodeBase_ResetInstanceCounters`
- `FUN_0002dc80` → `Race_ResetCountersAndDispatch`
- `FUN_0002e040` → `Race_ResetPlayerRoster`
- `FUN_0002e2d0` → `Race_ComputeRankings`
- `FUN_000b6140` → `RaceState_NullHandler`
- `FUN_0002cfa0` → `Race_WriteDebugMarkerAndResetCounters`
- `FUN_000bb060` → `DebugBuffer_Write`
- `FUN_0002cfe0` → `RaceState_TransitionAndRestore`
- `FUN_0002ea30` → `RaceState_TransitionIfChanged`
- `FUN_0002cb80` → `Race_ClearSpecialNodeTypes`
- `FUN_000b9010` → `Buffer_Zero`
- `FUN_000aa700` → `NodeBase_ClearInstanceCounters`
- `FUN_000329e0` → `Rider_UpdateCueTimer` (the last opaque `ScriptVM` opcode, 0x06/0x0f)
- `FUN_00113f70` → `Rider_TriggerCueIfFlagSet`
- `FUN_00115870` → `Rider_TriggerTrackedCue`
- `FUN_00115520` → `Rider_TriggerTrackedCueWithSetup`
- `FUN_001194b0` → `Rider_ScheduleTimedCallback`
- `FUN_0014b250` → `Utility_SortArray`

See `ssx_auto_rename.py` for the actual Ghidra rename commands. All six `ScriptVM_*`
renames were also applied live to the current Ghidra project this session (via the
GhidraMCP REST API directly, since the MCP tool itself wasn't connected this session —
see the project index for the connection notes) — they don't need a Script Manager run to
take effect, but the script stays the source of truth for re-creating them from scratch.

## M5: the outer ScriptVM, read and PORTED (2026-07-28)

The front end runs on this VM (`FEInit_Boot` -> `Script_PlayByName`
("FEStartScript")), so porting the menus means porting the sequencer first.

### `ScriptVM_Tick` (0x00049890)
```c
if (0 < wait) {                       // +0x40
    bool stillWaiting = (0 <= wait - delta);
    wait -= delta;
    if (stillWaiting) return;         // yield the frame
    wait = 0;
}
if (!script || script[0] <= index) { completionCallback(); return; }
do {
    r = ScriptVM_DispatchOpcode();
    if (r == -1) { completionCallback(); return; }
    cursor += *(u32*)(stream + cursor + 4);   // the command's own length
    index  += 1;
    if (r == 0) break;                        // yield
} while (index < script[0]);
```

**Subtlety worth keeping**: the guard is `0 <= wait - delta`, so a timer that
lands *exactly* on zero still costs a frame -- `wait = 2` with `delta = 1`
blocks two frames and executes on the third tick after. The port reproduces
this; a test that asserted the intuitive off-by-one behaviour was the thing
that was wrong.

### Object and command layout
```
Script object: 0xf0 bytes, tagged "Script", type id 0x3e9
  +0x38 i32   command index
  +0x3c i32   byte cursor
  +0x40 float wait timer
  +0xe0       -> resource: [0] command count, [1] command stream base
  +0xe4/+0xe8 -> context objects (nullable)

Command:  +0x00 u32 opcode   +0x04 u32 byte length   +0x08 operands
```
The length field being *in* each command is what makes the stream walkable
without a separate table.

### The opcode table (`ScriptVM_DispatchOpcode`, 0x0004a730)
| op | handler | op | handler |
|---|---|---|---|
| 0x00 | `Script_DispatchOpcode` (the inner 24-op level table) | 0x0d | context flag |
| 0x01 | `New_Camera` | 0x0e | `FUN_00031a50` |
| 0x02 | `ScriptVM_SpawnEmitterOp` | 0x0f | `FUN_00033590` |
| 0x03 | context op | 0x10 | `Camera_AddShake` |
| 0x04 | **Wait** -- sets `+0x40`, returns yield | 0x11 | `Rumble_TrackMaxA` |
| 0x05 | **Conditional** -- sub-switch on operand 0 | 0x12 | `Rumble_TrackMaxB` |
| 0x06 | `FUN_00033550` | 0x15 | guarded op |
| 0x07 | `CmdTable_LookupByIndex` | 0x17 | `Audio_PlayScaledCue` |
| 0x08 | `ScriptEvent_QueuePush` | 0x18 | `Camera_WarpToTarget` |
| 0x09 | context op | 0x19 | `RailMan_SetSegmentState` |
| 0x0a / 0x0b | guarded context ops | 0x1a | **spawns a sub-script** via `ScriptVM_CreateByID` |

Return values: **1** continue, **0** yield this frame, **-1** terminate.

Ported as `port/src/game/scriptvm.{h,cpp}` -- the sequencer only; opcode
behaviour is supplied by the host through a dispatch callback, since most
handlers reach into systems the port has not built yet. Asserted in
`asset_test`: the run/yield/terminate paths, the exact-zero timer rule, and a
zero-length-command guard so a malformed stream cannot spin.
