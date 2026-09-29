# SSX Tricky PC Port — Full Viability Audit & Preparation Plan

**Created 2026-07-22**, per explicit user request, after Tier 1 + Tier 2 +
the tractable Tier 3 items were all closed. This is the "full check of
everything" plus the process plan for the actual port work.

---

## PART A — Full audit: what we have, verified

### A.1 Asset/data formats (the port's input pipeline) — ALL DECODED

Every format was re-verified **this session** by running its decoder
against real shipped game data:

| Format | Content | Decoder (scripts\) | Verification level |
|---|---|---|---|
| `.big` (c0fb TOC) + RefPack | all archives | `extract_big.py` + `refpack.py` | used successfully on anm/psym/gari/models archives |
| `.afl` | character animation curves | `classify_afl.py` | **TOTAL: 119,498/119,498 curves, 0 failures** |
| `.ltg` | terrain collision grid + triangles | `classify_ltg.py` | header→triangle decoded, runtime-struct cross-confirmed |
| `.xsh` | textures (raw/swizzled/DXT1/DXT5) | `classify_xsh.py` | census of all 112 files/515 entries; 1 unmapped byte (0x7b, 2 uses) |
| `.ffn` | bitmap fonts (4bpp alpha) | `classify_ffn.py` | byte-perfect on all 3 files |
| `.loc` | all 3,794 localized strings | `parse_loc.py` | full dump regenerated cleanly |
| `.inp` | tutorial recorded-input playback | (spec in RE_NOTES) | 52B header + 1042×44B frames; full runtime loop traced |
| `.cml` | camera keyframes/staging | `classify_cml.py` + spec | Location/Moment/Transition layouts; composites partially (cells) |
| `.xsf` | level script/event tables | (spec in RE_NOTES) | opcode vocabulary == ScriptVM's own, no separate bytecode |
| `.inf` | all 11 config files | (loaders all named) | every file has a confirmed in-binary loader |
| `.aip` | AI racing-line paths | (spec via parser fns) | load chain + query API + both steering consumers traced |
| `trickdef.dat` | trick input-combo table | (spec in RE_NOTES) | 30×12×28B, accessor + all consumers named |
| `.mpc` / `SaveImage.xbx` | video / save icons | n/a | standard MPEG / DDS — use existing libraries |
| `.xbd` models / `.xss` audio-adjacent | mesh + one unknown | — | **NOT decoded — see gaps** |

### A.2 Core gameplay systems (what must behave identically) — MAPPED

- **Physics**: 6-mode rider physics dispatcher, terrain contact (cubic-
  Bezier edge collision + iterative closest-point), surface
  compression/give response, collision impulse, rail attach/ride/exit
  state machines, landing resolution — all named and documented
  (`terrain_collision`, `rider_update_chain`, `rider_event_system`).
- **Scoring**: complete formula (base points, streak bonus, repeat-trick
  penalty, über bonuses), the single score field + its writer, medal
  thresholds (250k/500k/799999 + per-track time tables), personal-best
  recording, standings comparator (`trickcombo_scoring_resolved`).
- **AI**: path loading, per-frame path selection
  (`Rider_SelectBestAIPathZone`), end-race collision-avoidance steering,
  race-start path assignment (`ai_path_system`).
- **Frame loop**: `Application_RunMainLoop` → `InGameState_TickFrame` →
  `InGameState_TickSubsystemsByTypeOrder` (NodeRegistry type tables) +
  per-viewport render loop — the whole per-frame architecture is now
  explicit (`node_base_class`).
- **Game flow**: boot, mode dispatch (`GameMode_*` jump tables), race
  phases (countdown/clock/outcome vtable), venue staging, results,
  replay/ghost system, save/load (17-state pipeline + checksums),
  career/challenge records, AggressionManager rivalry persistence.
- **Input**: full controller pipeline, normalized bitmasks, per-frame
  edge-trigger derivation, 8-frame history buffer (`control_scheme`).
- **HUD/frontend**: race HUD, menus (board select, profile editor,
  options, pause), tutorial (LessonMan end to end), loading screens.
- **Audio (logical layer)**: 9-level class map, all config loaders,
  commentary dispatch, speech/jukebox/music managers (`audio_system`).

### A.3 Integrity checks (re-run this session)

- `ssx_auto_rename.py`: **1,324 renames (1,310 functions + 14 data)**,
  0 duplicate addresses/names, both copies byte-identical.
- Full-coverage function audit (all functions vs. fresh decompiles):
  completed 2026-07-21 with 0 issues; spot-audits since then clean.
- ≈28.7% of all functions named ≈ every architecturally significant
  system; the unnamed remainder is engine/math/D3D plumbing by census.

---

## PART B — What is genuinely missing (honest gaps)

**Port-relevant, real work items** (updated 2026-07-22 after the
follow-up pass — see `RE_NOTES_xbd_model_format.md`):

1. **Model geometry interiors (`.mxf` riders/boards, `.xbd` tracks)** —
   the *directory level* of both is now decoded (LOD/sub-part tables,
   contiguous data extents, section offsets, texture-index remap
   confirmed; parser `classify_mxf.py` verified on all model kinds).
   What remains is the vertex/skinning interior — a single well-scoped
   task, scheduled as milestone M4. Does not block M0–M3.
2. ~~`.afl` channel→bone mapping~~ **resolved structurally**: models
   carry no bone names — bones are index-based, so channel groups map by
   skeleton order; the concrete visual assignment is recoverable
   empirically at M4 (standard skeletal-RE step).
3. ~~Audio sample banks~~ **CLOSED — identified by inspection**: standard
   EA `BNKl` v5 banks (BIGF containers, PT/EACS headers), publicly
   documented, vgmstream-supported. Use existing decoders.
4. ~~`.xsh` byte `0x7b`~~ **CLOSED**: 32bpp + one mip + 8-byte terminator
   (arithmetically exact). ~~`.cml` composite cells~~ **CLOSED**:
   serialized authoring-tool object graph (names + stale tool pointers +
   `0xDEADC0ED` fill); a port needs only the names + decoded keyframes.

**Explicitly NOT gaps (rewritten for any port, by design):**
- D3D8/NV2A GPU code, XAPI/kernel calls, CRT/allocators, Gimex codec
  internals beyond behavior (RefPack already reimplemented in Python).

**Accepted unknowns that do not affect a port:**
- The in-binary `.cml` loader's static call site, `.xss` video-adjacent
  format (zero code references — dead data), narrator-script field reader
  (statically unreachable), a handful of "structural confidence" D3D
  state setters.

**Verdict: the port is viable now for game logic; render geometry
(`.xbd`) is the one asset decode that must land before visuals.**

---

## PART C — Port process & preparation plan

### C.1 Strategy decision (recommended)

**Reimplementation port** (SM64/OoT-style "ship of Theseus" is not
available — we have no compiler-matching setup for MSVC/Xbox and don't
need one): write a modern C/C++ codebase that reimplements each system
*to the documented behavior*, using the 1,324-name annotated decompile
(`default.xbe.c` + live Ghidra) as the authoritative spec, and the
original data files as-is via the decoded formats. Game data ships
unconverted (users supply their own ISO extract; `extract_big.py` logic
becomes the loader).

### C.2 Platform-layer replacement map

| Original | Replacement |
|---|---|
| D3D8/NV2A fixed-function + swizzled textures | modern API (SDL_GPU / OpenGL / D3D11); de-swizzle at load (`classify_xsh.py` logic); DXT1/5 upload natively |
| XAPI file I/O (`FILE_loadpack*`) | stdio/mmap VFS over extracted `.big` contents |
| Xbox controller (`XGetInput`-family) | SDL gamepad, mapped to the documented normalized bitmasks |
| DirectSound voices | SDL_audio/OpenAL mixer honoring the documented AudioSystem channel model |
| Xbox save-content packages | plain files, keeping `Data.ssx`'s documented chunk/checksum format for save compatibility |
| Gimex/RefPack decompression | `refpack.py` logic in C |
| 60Hz frame pacing (`Application_RunMainLoop` timer event) | fixed-timestep loop preserving the documented tick order |

### C.3 Milestones (each independently verifiable)

- **M0 — Toolchain**: repo scaffold, C loaders for `.big`/RefPack/`.loc`/
  `.inf`/`trickdef.dat` (ports of the Python decoders + verification
  against them), asset VFS. **DONE (2026-07-22)** — see `port/`.
- **M1 — Window + boot**: **DONE (2026-07-22)**. The `port/` directory
  builds `ssxtricky.exe`, launches a 640×480 windowed app, and renders the
  real `splash.xsh` "Basic Controls" image with a localized prompt. The
  asset pipeline (RefPack, c0fb + BIGF `.big`, `.loc`, `.xsh` incl.
  DXT1/3/5, `.ffn`) is verified byte-identical to the Python reference
  decoders via `port/tests/asset_test.exe`. **Key correction discovered
  during M1**: `.xsh` pixel data is stored **LINEAR** in the file; the
  "swizzled" flag in the RE table refers to the Xbox engine's upload-time
  `XGSwizzleRect` step, NOT the on-disk layout — a PC port omits swizzling
  entirely (verified: splash decodes coherently only when read linearly).
  This is now reflected in `port/src/assets/xsh.cpp`.
- **M1 — Headless core**: NodeRegistry + tick-order tables, GameMode
  state machine, timer/race phases. Verifiable via logging.
- **M2 — Physics sandbox**: `.ltg` collision + rider physics modes on a
  flat/real terrain grid, debug-line renderer only. The suspension-curve
  and Bezier-edge math ports directly from the documented functions.
- **M3 — Full gameplay, debug visuals**: input pipeline, RiderEvent state
  machines, scoring/combo, AI paths, race flow end to end — the game is
  "playable" with placeholder graphics. **This is the go/no-go
  demonstrator for behavioral fidelity.**
- **M4 — Assets**: `.xbd` decode (the one open RE task) → real meshes;
  `.xsh` textures; `.afl` skeletal animation; `.ffn` text.
- **M5 — Presentation**: HUD/menus (all screens documented), `.cml`
  cameras, audio banks, `.inp` tutorial, replay/ghost, save/load.
- **M6 — Polish/parity**: side-by-side comparison vs. the original
  running in xemu (same inputs → same outcomes; the `.inp` files
  double as ready-made deterministic input-replay test vectors — a
  genuinely lucky asset for regression testing).

### C.4 Immediate preparation actions (pre-coding)

1. **Decode `.xbd`** (fresh RE thread, methodology proven).
2. **Freeze the spec**: re-export `default.xbe.c` with all 1,324 names
   (run `ssx_auto_rename.py` then `ssx_export_c.py` in Ghidra — user
   action, GUI) so the port repo can vendor a current annotated decompile.
3. **Vendor the knowledge**: copy `RE_NOTES\` + `scripts\` into the port
   repo as `docs/re/` — they are the spec.
4. **Set up xemu with this ISO** as the behavior reference target.
5. Pick the stack (recommendation: C++17 + SDL3 + SDL_GPU or OpenGL 4.x,
   CMake, single repo, per-system directories mirroring the RE_NOTES
   subsystem map 1:1 — every source file cites its RE_NOTES file).

### C.5 Risks

- `.xbd` complexity (skinned meshes) — highest-uncertainty decode left.
- Float determinism (x87 vs SSE) — behavioral, not bit-exact, fidelity is
  the realistic target; `.inp` replay vectors keep this honest.
- Untraced per-frame details (structural-confidence renames) will surface
  as small behavior diffs in M3/M6 comparison — budget iteration time.
- Legal: ship no EA assets/code; the port must require the user's own
  game data (loader-only distribution, as all fan ports do).
