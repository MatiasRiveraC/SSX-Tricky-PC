# RE notes: the boot sequence and `cStartScreenSingle` start screen

**2026-07-22**, traced for the PC port (the user asked for a *faithful* boot
sequence, code-driven, not guessed). This maps the exact power-on flow:
the HDD save-autoload screens, their strings, timing, and the handoff into
the splash/videos/title.

## Top-level phase order (`Application_StateMachineTick`, state @ this+0x738)

- **state 0** -> `StartScreen_Create` allocates the boot state object
  `cStartScreenSingle` (0x3e98 bytes, vtable 0x0019a744) and makes it the
  app's current top-level state (this+4). This is the "Checking hard disk"
  boot screen.
- **state 1** -> allocates `"FrontEnd"` (the title/menu state,
  `FUN_0007d7b0`).
- **state 2** -> if flag `DAT_001df3f4 & 0x20` is set, allocates
  `"VideoPlayer Module"` (`FUN_000b0a00`) -- this is how the boot videos
  play; else allocates `InGameState`.
- **state 3** -> `InGameState`.

`TitleIntroSequence_QueueBootVideos` (a FrontEnd `f3stttl` widget) is what
sets flag 0x20 and queues **`eabig.mpc` (EA logo) then `ssxintro.mpc`
(intro)** -- so the videos play as part of the title-open sequence, after
the autoload screen and before the interactive title.

## `cStartScreenSingle` -- the HDD save-autoload screen

8-slot vtable (0x0019a744):

| Slot | Function | Role |
|---|---|---|
| 0 | `~cStartScreenSingle` (0xaee60) | destructor |
| 1 | **`StartScreen_Enter`** (0xad840) | one-time: sets gfx mode, loads `data/fonts/menu.ffn`, seeds fade timer (+0x3e7c=1.0 counting down by +0x3e78=1/60) |
| 2 | **`StartScreen_TickExitWhenDone`** (0xadfb0) | when done-flag +0x48 set, final flush, tears down (this+4=0), returns 1 -> app advances |
| 3 | isDone getter (0xaed00) | returns +0x3e74 |
| 4 | **`StartScreen_ResetState`** (0xaef20) | state +0x3e84 = 0 |
| 6 | **`StartScreen_Render`** (via 0xaf200) | per-frame: 2D viewport, draws `IconAtlas(0x28)` background, then calls slot 7 |
| 7 | **`StartScreen_RenderStatusText`** (0xaf210) | switch on +0x3e84 -> draws the localized status string |

### The status strings (verified in american.loc)

`StartScreen_RenderStatusText` maps the state field (+0x3e84) to a string id:

- states **0, 3, 4, 5, 9, 10, 11, 12** -> **0xba7 "Checking hard disk"**
- states **6, 7, 8** -> **0xba8 "Autoloading from hard disk"**
- state 0xd -> 0x176; 0xf -> 0x15c; 0x10 -> reading/error paths
  (0x15e/0x18f/0xde5 "Read error…"); 0x12 -> save; 0x13 -> 0x155;
  0x14 -> 0x150; 0x15 -> 0x151.

White text on a black screen, in `menu.ffn`. This is exactly the observed
"Checking hard disk -> Autoloading from hard disk -> Checking hard disk"
sequence.

### Timing (code-traced minimum on-screen durations)

`StartScreen_SetState(this, newState)` writes +0x3e84 and seeds a per-state
frame countdown (decremented by the save-autoload driver each frame;
`StartScreen_RenderStatusText` case 1 holds while +0x14c4 > 0):

- state 0 ("Checking"): +0x14c4 = **0xf = 15 frames (~0.25s)**
- state 8 ("Autoloading"): +0x14c4 = **0x78 = 120 frames (2.0s)** -- the
  longer hold the player notices
- states 1/9/0xb: +0x14b8 = 0x4b (75); states 0xd/0x11/0x1a/0x1b: 0xb4 (180);
  states 0xf/0x14: 0x78 (120)
- states 0xe/0x13 additionally reset the fade timer (+0x3e7c = 1.0)
- **terminal state 0x1e** sets the done-flag +0x48 = 1 -> exit.

The state advances as each async save operation completes (driver cluster
0xafae3-0xb018a, which calls `StartScreen_SetState` with the next state).
The frame counts above are genuine constants in the code -- the minimum
display time for each message -- so a faithful port uses them (a real HDD's
I/O latency adds to them; on a PC the minimums dominate).

## The fade transitions

Between major phases the engine fades to/from black. `TitleIntroSequence_
Render` drives `TransitionEffect_Update` (0xf5f90) every frame during the
title/boot cinematics (the "Transition Man" global allocated in
`Application_InitSubsystems`), and `StartScreen_Enter`/`SetState` seed the
per-screen fade timer (+0x3e7c/+0x3e78). So: autoload screen -> fade out ->
Basic Controls splash -> fade -> EA logo -> fade -> intro -> fade -> title.

## The "Basic Controls" splash

`splash.xsh`'s single entry is named **"cont"** (= controls) -- the 512x512
"Basic Controls" image. It's the loading/splash screen (its own picture-
screen state, async-loads `data/textures/splash.xsh`, shows the localized
"loading..." 0x16e -- see `RE_NOTES_loading_screen.md`), shown while the
FrontEnd's assets stream in, between the autoload screen and the videos.

## Port implementation

`port/src/game/boot_flow.cpp` reproduces this: the three StartScreen text
phases (real 0xba7/0xba8 strings in menu.ffn, the traced 15/120-frame
timings), fade-to-black between every phase, then the splash (+ real
"loading..."), the two video stages (real `.mpc` -- placeholder until the
MPEG-1 decoder is added), and the FrontEnd title. 7 renames.


## Title screen layout (measured, not guessed)

Earlier passes placed the title-screen elements by eye and got them wrong
(full-screen background, oversized text at scale 2-4, white menu items). The
layout below is measured off a capture of the original running in xemu, with
the 4:3 game frame identified inside the pillarboxed window and every position
converted to a fraction of the 640x480 render target.

| Element | Position (fraction of 640x480) |
|---|---|
| Art panel | x 0.045, y 0.146, w 0.908, h 0.619 (rest of frame is black) |
| SSX Tricky logo | x 0.155, y 0.190, w 0.380 (aspect-locked to the sprite) |
| "Start Game" | x 0.082, y 0.622 — inside the panel |
| "DVD Content" | x 0.082, y 0.680 — inside the panel |
| "Press START button" | x 0.098, y 0.804 — below the panel, on black |
| Copyright | x 0.077, y 0.839 — below the panel, on black |
| "select" + A glyph | right-aligned to x 0.895, y 0.839 |

Fonts/colours:
* Menu items use **title.ffn** (82 glyphs, 256x125 atlas, median glyph h=16),
  gold `0xE0B44C` selected / `0xA8823C` unselected, with a `0x3A2408`
  one-pixel drop outline. Not menu.ffn and not white.
* The two info lines and "select" use **menu.ffn** (104 glyphs, 128x92 atlas,
  median glyph h=12), white.
* **All text is scale 1.** Both faces contain a single 32px outlier glyph, so
  `lineHeight()` (font-wide max of h+oy) is useless as a leading proxy — line
  positions are the explicit fractions above.

### Known gap: the panel's contents
In the original the panel shows a snowflake backdrop with three rider
characters standing in front of it. That is **not** a 2D image: no such
texture exists in `data/textures/` (all 4 `fe_1.xsh` entries are `fe_1` UI
chrome, `fe_2` logo/medals/mode icons, `xbox` button glyphs, `dvd1` the orange
streak panel). The front end renders it as a 3D scene from the
`f3bigmod`/`f3bigtex`/`f3biglod` asset set in `models/mdlxbx.big`, which needs
the `.mxf`/`.xbd` geometry decode (milestone M4). The port currently stretches
`dvd1` into the panel as a stand-in; only the logo is genuine.

### Frame-capture harness
`SSX_START_PHASE=title` jumps straight to the front end and
`SSX_DUMP_FRAME=<path.bmp>` writes frame 60 of the render target to a 32bpp
top-down BMP and exits. Used to diff the port against captures of the original
instead of eyeballing it.

## FEInit_Boot — the front end, read from code (supersedes earlier guesswork)

`FEInit_Boot` (`default.xbe.c:81736`) is the authority for everything the
title screen does. Reading it invalidated several assumptions the port had
been built on.

### The title backdrop is a 3D level, confirmed
```c
FUN_001295d0("data/models/ssxfe.big", 0x10);
Level_LoadTrackAssets(0xffffffff);
```
The front end loads **`data/models/ssxfe.big` as a level** through the same
path the tracks use. `TitleIntroSequence_Tick` then renders it with the normal
world pipeline, gated on `TerrainGrid`:
`TerrainNode_UpdateTrackSegmentProps()` → `ModelsNode_DrawTrackSegmentModels()`
→ `NodeRegistry_RenderAllOfType(3)` → `NodeRegistry_RenderAllOfType(4)`.

Extracted (RefPack `c0fb`, 8 entries): `ssxfe.xbd` (1.20 MB geometry),
`ssxfe.xsh` (2.56 MB, **131** textures), `ssxfe_B.xsh` (42), `ssxfE_L.xsh`,
`ssxfe.map`, `ssxfe.ltg`, `ssxfe.xsf`, `tricky.ser`. The textures are 128x128
and 256x256 DXT1-with-mips — no single backdrop image exists. So the snowflake
scene and the three riders **cannot** be reproduced as a 2D blit at any
fidelity; they need the `.xbd` decode (M4). This is now proven, not suspected.

The character models are separate widgets built in the same function:
`f3bigmod` (:81847), `f3bigbrd` (big boards), `f3bigtex`.

### `.xsh` TOC record size is 8 bytes, not 16
4-char name + `u32` offset. Per-texture header at that offset:
`u32` = `(byteSize << 8) | fmt`, then `u16` width, `u16` height.
Sheets whose entries share one name (`ssxf` x131) are index-addressed.

### Font scales are real, and are a product of two factors
`FEInit_Boot:81806-81813` writes, immediately after each `Font_LoadAndParse`:

| Face | `+0x18` (x) | `+0x1c` (y) |
|---|---|---|
| `title.ffn` | **1.4** | **1.3** |
| `menu.ffn`  | **1.8** | **1.4** |

`Text_MeasureStringAnsi` (`:129663`) shows the consumption rule:
```c
fVar3   = param_4 * *(float *)(param_1 + 0x18);   // effective X
param_4 = param_4 * *(float *)(param_1 + 0x1c);   // effective Y
```
**effective = callerScale x fontScale.** The port's old "everything is scale 1"
was wrong; the font half is now correct, the per-widget caller half is still
outstanding (see below).

Same function confirms the glyph record layout the `.ffn` parser uses:
`+2` w, `+3` h, `+8` advance, `+9` ox, `+10` oy (all signed except w/h), and
font `+0x10`/`+0x14` as the base pen origin.

### Screen space
`TitleIntroSequence_Tick` sets the 2D ortho with
`(0, 0, 640.0f, 480.0f, 0, 1000.0f, 1)` — the port's 640x480 target matches.

### Boot video chain
`TitleIntroSequence_QueueBootVideos` allocates two `f3dstvid` widgets and links
`eabig->+0x154 = ssxintro`, so **eabig.mpc plays first and chains into
ssxintro.mpc**. Gated on `DAT_001df3f4 == 1`, sets it to `2` when queued. The
port's boot order was already right.

### Attract mode — MISSING from the port
`TitleIntroSequence_Render` (`:81141`) runs an idle counter at `this+0xec` and:
```c
if (... && 0xe10 < iVar3) {                    // 0xe10 = 3600 frames = 60 s
    puVar6 = FUN_00150d70("f3dstvid", 0x158, 0x10);
    pcVar7 = "data/video/ssxintro.mpc";
```
After **60 seconds idle on the title screen** the intro replays as attract
mode. Any input resets the counter (`this+0xec = 0`). Not yet implemented.

### Front end is script-driven
`Script_PlayByName("FEStartScript")` runs the front-end entry script, and
`FUN_0007d8b0` plays `FEGari2`-family scripts on teardown. The `.ser` file in
`ssxfe.big` (`tricky.ser`, 547 KB) is the likely script/serialised-object blob.

### Still outstanding
Per-widget caller scales for the title text. They live in the widget builders:
`f3stttl` (`:82124`), `f3buttns` (`:87438`), `f3frmtl` (`:87966`). Until those
are read, `text.h`'s `kCallerMenuItem` / `kCallerInfoLine` are fitted to a
capture and marked TODO in the source.

## UI_BuildTitleHelp (0x00099690) — the title screen's real layout

The `f3stttl` object's constructor (`FUN_00096f00`) only installs vtable
`PTR_FUN_00199498`; the layout lives in **slot 0x2c**, which the caller invokes
immediately (`(**(code **)(*DAT_001e37ac + 0x2c))()`).

Vtable dumped live via `POST /read_bytes address=0x00199498 length=80`:

| slot | target | | slot | target |
|---|---|---|---|---|
| 0x00 | `0x00096f20` | | 0x28 | `0x00096f70` |
| 0x1c | `0x00085cf0` | | **0x2c** | **`0x00099690` = `UI_BuildTitleHelp`** |

### Reading the constants out of a mangled decompile
Ghidra's stack analysis on this function is poor (register spills alias the
stack locals), so the positions arrive as nonsense expressions like
`(float)piVar2[0x1c] - (float)piVar2[0x1c]`. Two techniques recovered them:

1. **Denormal floats are integers.** Ids pushed as `int` get typed `float` by
   the decompiler and print as denormals. Reinterpreting the bits gives:
   `5.20863e-42` -> `0xe85`, `4.6439e-42` -> `0xcf2`, `1.42932e-43` -> `0x66`,
   `4.97041e-42` -> **`0xddb`**.
2. **Scan the function's raw bytes for float immediates.** The coordinates are
   `MOV dword ptr [ESP+n], <imm32>` before a `SUBPS`, so a scan of the 834-byte
   body for plausible screen-range floats recovers every one — including the
   pair the decompiler lost entirely.

### The layout (absolute 640x480 pixels)
Positions are pushed as a vec4 then `SUBPS`'d against the widget's own bounds
(`+0x1c/+0x1d/+0x1e`), so each constant is where the widget's top-left lands.

| Widget | Field | String id | Face | Position | Scale (`+0xf8`) |
|---|---|---|---|---|---|
| menu list (2 items) | `[0xd]`  | `0xe85` `kFEStartGame`, `0xcf2` `kFEDVDContent` | `title.ffn` (`+0x5c`) | **(20, 330)** | none -> 1.0 |
| "Press START button" | `[0x13]` | `0x66` `kFE_TitleScreen` | `menu.ffn` (`+0x60`) | **(38, 410)** @`0x000997e1` | **0.8** |
| `f3TitleHlp` | `[0x14]` | **`0xddb` `kFECopyright`** | `menu.ffn` | **(20, 428)** | **0.8** |
| button group | `[0x10]` | — | — | (x lost, **428**) | **0.8** |

`0x3f4ccccd` = **0.8f** written to `+0xf8` with `+0xf4 = 1` (the "use scale"
flag). Combined with the per-face scales this gives the true effective sizes:
menu items `1.0 x (1.4, 1.3)`, info lines `0.8 x (1.8, 1.4)`.

Colour for the info widgets is `(1,1,1,1)` — white — set through slot 0x24.

### Corrections this forced
* `kFECopyright` (`0xddb`) is a **real localized string**; the port had it as a
  hardcoded C literal, which would have broken every non-English build.
* All four string ids the port was using are now confirmed *from code* rather
  than from a `.loc` dump alone.
* Every fitted fraction in the title renderer was replaced with the real pixel
  constants. The one remaining fudge is the list's row spacing (leading), which
  lives in `UI_BuildListWidget_Alt` and is not yet extracted; it is marked in
  the source.
* The art-panel rect remains a stand-in for the 3D viewport (M4), now sized so
  the code-derived text lands on it the way it does in the original.

### Attract mode — implemented
60 s idle -> replay `ssxintro.mpc`, per `TitleIntroSequence_Render`'s `0xe10`
frame threshold. Any input resets the timer.

## List leading and the button group — the last two invented values

### Row pitch (UI_BuildListWidget_Alt, 0x00088158)
```c
puVar3[0x4c] = 1;                                   // byte offset 0x130 = vertical axis
*(undefined4 *)(list + 0x138) = 1;                  // "use spacing" flag
*(undefined4 *)(list + 0x13c) = 0x40800000;         // spacing = 4.0f
```
Consumed by the widget measure pass `FUN_000a78b0` case 4:
```c
if (*(int *)(param_1 + 0x130) == 0)                 // horizontal
    fVar1      = *(float *)(param_1 + 0x13c) + *param_2   + local_20;
else                                                 // vertical
    param_2[1] = fStack_1c + param_2[1] + *(float *)(param_1 + 0x13c);
```
So **pitch = the item's own measured height + 4.0**, not a font-wide constant.
`UI_BuildListWidget_Alt` also positions the list at (320, 93) by default —
`UI_BuildTitleHelp` overrides that to (20, 330) for the title screen.
`UI_BuildListWidget` (the non-`_Alt` sibling) uses (320, 63) and no spacing.

### Button group (UI_BuildButtonGroup, 0x00087438)
`UI_BuildTitleHelp` calls `UI_BuildButtonGroup(2, 0)` — a **bitmask**, not a
count. The descriptor table at `DAT_001b9d00` is 17 records of 3 dwords
`{mask, glyphId, stringId}`, dumped live via `/read_bytes`:

| mask | glyph | string | text |
|---|---|---|---|
| 0x0001 | 62 | 0x212 | `kSTRNext` "next" |
| **0x0002** | **62** | **0x218** | **`kSTRSelect` "select"** |
| 0x0004 | 62 | 0xcfd | `kSTRPlayVideo` "play video" |
| 0x0008 | 63 | 0x215 | `kSTRPrevious` "previous" |
| 0x4000 | 62 | 0x20c | `kSTRAccept` "accept" |

So the title screen shows exactly one button: **glyph 62 + "select"**. Buttons
use `menu.ffn` (`DAT_001e3c7c + 0x60`) and white `(1,1,1,1)`.

`FUN_000a4410` sizes each button: `FUN_000a41c0(0x41a00000, 0x41b00000, ...)`
= icon **20x22 px**, label scale `0x3f333333` = **0.7**, item gap
`0x40800000` = 4.0, and an offset `0x41b80000` = 23.0. The group's own
inter-button spacing is set to **0** (`+0x13c = 0`).

Glyph 62 is a sprite index, not a font character — `data/fonts/` holds only
`menu.ffn`, `title.ffn` and `smlfont.ffn`. It indexes the `xbox` atlas in
`fe_1.xsh` (the button-glyph sheet); the sprite table for that atlas has not
been decoded yet, so a green pip still stands in for the A button.

### Another hardcoded string caught
"select" is localized string `0x218`, exactly like the copyright was. The port
had it as a C literal too. Both now come from the `.loc` table.

## Hard-disk phase verified against code — timings were wrong

Checked at the user's request. The result corrected two of the three durations.

### These functions are NOT in default.xbe.c
`grep StartScreen_ default.xbe.c` returns nothing. The whole `StartScreen_*`
family was named after that export was taken (export dated 2026-07-22), so it
had to be pulled from **live Ghidra**:

| Function | Address |
|---|---|
| `StartScreen_Enter` | `0x000ad840` |
| `StartScreen_TickExitWhenDone` | `0x000adfb0` |
| `StartScreen_Render` | `0x000ae150` |
| `StartScreen_Create` | `0x000aeeb0` |
| `StartScreen_ResetState` | `0x000aef20` |
| `StartScreen_SetState` | `0x000aef50` |
| `StartScreen_RenderStatusText` | `0x000af210` |

**Standing lesson: `default.xbe.c` is a snapshot, not the source of truth.**
When a grep of it comes back empty for something that should exist, re-query
the live database before concluding the code isn't there.

Found them by byte-searching for the string ids as data
(`POST /search_bytes hex=a70b0000`), which hit `0x000af275` and `0x000af2b2`,
then `GET /get_function_containing`. The only *inline* `0xba7` reference in the
export is inside `Challenge_FormatObjectiveDescription` — an unrelated Career
Mode reuse, which is why grepping the export for it was misleading.

### State -> string (StartScreen_RenderStatusText, switch on `this+0x3e84`)
| States | String |
|---|---|
| 0, 1, 3, 4, 5, 9, 0xa, 0xb, 0xc | `0xba7` `kOVMCCheckingCard` "Checking hard disk" |
| **6, 7, 8** | `0xba8` `kOVAutoLoading` "Autoloading from hard disk" |
| 0xd | `0x176` |
| 0xf | `0x15c` |
| 0x10 | save-file name, `"%s\%s"` |
| 0x12 | `0xc69` `kOVMCGameCorrupt` |

State 1 carries an extra guard — `if (0 < *(int *)(param_1 + 0x14c4)) return;`
— so it draws **nothing** while the counter state 0 loaded is still running.

This confirms the observed Checking -> Autoloading -> Checking order: the state
index rises through the 0xba7 group, into the 6/7/8 group, then back into the
0xba7 group at 9.

### Frame counts (StartScreen_SetState) — two timers, `+0x14c4` and `+0x14b8`
| State | Field | Frames | Seconds |
|---|---|---|---|
| 0 | `+0x14c4` | `0x0f` = 15 | 0.25 |
| 1 | `+0x14b8` | `0x4b` = 75 | 1.25 |
| **8** | `+0x14c4` | `0x78` = 120 | **2.00** |
| 9 | `+0x14b8` | `0x4b` = 75 | 1.25 |
| 0xb | `+0x14b8` | `0x4b` = 75 | 1.25 |
| 0xd, 0x11, 0x1a, 0x1b | `+0x14b8` | `0xb4` = 180 | 3.00 |
| 0xf, 0x14 | `+0x14b8` | `0x78` = 120 | 2.00 |

### Corrections applied to the port
| Phase | Was (invented) | Now (from code) |
|---|---|---|
| Check1 | 0.60 s | **1.50 s** (states 0+1 = 15+75 frames) |
| Autoload | 2.00 s | **2.00 s** — was already right |
| Check2 | 0.55 s | **1.25 s** (state 9 = 75 frames) |

### Honest limit
The frame counts above are exact. Which states the real boot actually walks
is not fully pinned: the driver that calls `StartScreen_SetState` lives at
`0x000afae3`-`0x000b018a`, which `get_function_containing` reports as **not
inside any defined function**. Reading the `push imm8; mov ecx,esi; call`
argument at each of its 8 call sites shows it sets states **2, 1, 0xe, 0xb, 9,
9, 9, 0xb** — notably *not* 6/7/8, so the Autoloading states are set on the
async save-device path, elsewhere again. Defining a function over that region
was deliberately not attempted (a previous session fragmented existing
functions doing exactly that). The port's three-phase collapse is therefore the
best documented reading of the state path, with exact per-state durations.

## Controller/splash screen and status screens verified — 7 more corrections

Checked at the user's request, all against **live Ghidra** (these functions are
absent from the stale `default.xbe.c` export).

### Splash (Basic Controls) — `SplashScreen_*`, vtable `0x001a7730`
| Slot | Function | What it does |
|---|---|---|
| 5 (`0x14`) | `ScreenBase_DrawFrame` `0x00135a60` | shared per-frame draw (9 screens) |
| 6 (`0x18`) | `SplashScreen_DrawTexture` `0x00136270` | draws the texture |
| 8 (`0x20`) | `SplashScreen_LoadTexture` `0x0012f9c0` | async-loads the asset |
| 9 (`0x24`) | `SplashScreen_CreateTextureFromAsset` `0x0012f9e0` | builds the D3D texture |

The vtable is exactly 10 slots — the bytes after it are the literal
`"data/textures/splash.xsh"`, confirming the asset path.

`SplashScreen_DrawTexture`:
```c
DAT_001baf70..7c = 0x3f800000;                        // colour = (1,1,1,1) white
FUN_000c1ed0(0, 0, 0x44200000, 0x43f00000);           // 0,0,640.0,480.0
```
**A fullscreen 640x480 stretch, untinted.** The port had been letterboxing it
to preserve the source aspect — wrong.

`ScreenBase_DrawFrame` draws the status string:
```c
DAT_001baf70..7c = 0x3f800000;                        // white
if (param_1[0x30] == 4) {
    uVar6 = Localization_ResolveString(..., 0x16e);   // "loading..."
    HUD_DrawTextShadowed(param_1 + 4, 0x3f800000, 0x41e00000, 0x43d70000, uVar6);
}
```
`HUD_DrawTextShadowed`'s call convention is **(colour, x, y, stringHandle)**, so
this is white, drop-shadowed, at **(28, 430)**, caller scale 1.0. It is gated on
the screen's `+0x30 == 4` sub-state. `HUD_DrawTextShadowed` (`0x000c2700`)
draws the string twice: once offset in black, once at the true position.

`ScreenBase_DrawFrame` also reveals how **fades** work — a fullscreen quad
`FUN_000c1ed0(0,0,640,480)` with colour from `param_1[0x28]`/`[0x2c]` and alpha
`param_1[0x26]`, drawn when `param_1[0x26] >= DAT_001a9f34`. The port's
`darken()` is an acceptable equivalent for fades to black.

### "Checking hard disk" / "Autoloading" status text box
`FUN_000aed60` -> `FUN_000bddd0` computes the box from screen-relative globals:
```c
left  = DAT_0019a6f8 * 0.5 - DAT_0019a6f8 * 0.65 * 0.5   // 640*0.5 - 640*0.65*0.5 = 112.0
top   = DAT_0019a6f4 * 0.2                               // 480*0.2               =  96.0
width = DAT_0019a6f8 * 0.65                              // 640*0.65              = 416.0
```
(`DAT_0019a6f8` = 640.0, `DAT_0019a6f4` = 480.0, `DAT_0019a720` = 0.65,
`DAT_001a9f50` = 0.5, `DAT_001875d0` = 0.2, `DAT_001878c4` = 4.0.)
Colour is again `(1,1,1,1)` — white.

So the text is centred in a **416-wide box whose top is y=96**, not vertically
centred on screen.

### Corrections applied
| Item | Was (invented) | Now (from code) |
|---|---|---|
| Splash backdrop | aspect-fit + letterbox | fullscreen 640x480 stretch |
| Splash tint | — | (1,1,1,1), untinted |
| "loading..." x | 14 | **28** |
| "loading..." y | `height - 26` = 454 | **430** |
| "loading..." colour | `0x303030` dark grey | **white + black drop shadow** |
| "loading..." scale | integer 2 | caller 1.0 x font scale |
| Status text y | `height/2 - 8` = 232 | **96** (box top) |
| Status text box | full width | **416 wide, centred (x 112..528)** |
| Status text scale | integer 2 | caller 1.0 x font scale |

### Honest limits
* The `+0x30 == 4` gate on "loading..." is not reproduced — the port's splash
  phase has no real async load to be in state 4 for, so it always draws.
* `FUN_000bddd0`'s 4th argument is `*(int *)(param_1 + 0x14) * 4.0`; the meaning
  of `+0x14` (line count vs. line spacing) was not pinned down, so vertical
  alignment *within* the box is top-aligned in the port.
* Which `.ffn` face the screen-base text object uses is not confirmed; the port
  uses `menu.ffn`, which is what the boot path loads.

## Two rendering bugs found by comparing captures side by side

### 1. Uneven baseline in scaled text (port bug, not a game behaviour)
The non-uniform scaled draw rounded the glyph's destination *offset* and its
*size* independently:
```cpp
dy0 = y + round(oy * sy);
dh  = round(h * sy);          // bottom = round(oy*sy) + round(h*sy)
```
but `round(oy*sy) + round(h*sy) != round((oy+h)*sy)`, so glyphs sharing a
baseline landed up to a pixel apart — a visible wobble along every scaled line.

Fixed by rounding the destination **edges** and deriving the size from them:
```cpp
dy0 = y + round(oy * sy);
dy1 = y + round((oy + h) * sy);
dh  = dy1 - dy0;
```
Verified by measuring the rendered framebuffer rather than eyeballing it: for
"Autoloading from hard disk" every one of the 22 letter groups now bottoms at
**y=112** exactly, the sole exception being `g` at 116 (a real descender), with
tops splitting cleanly into 97 (ascenders) and 102 (x-height).

### 2. Drop-shadow offset is 2.0, not 1
`HUD_DrawTextShadowed`'s decompile hides the offset (Ghidra loses the args), so
it had to come from the disassembly at `0x000c2700`:
```asm
000c278a: FLD  float ptr [ESP + 0x1030]
000c2798: FADD float ptr [0x0018766c]      ; DAT_0018766c = 2.0
000c27b2: FLD  float ptr [ESP + 0x103c]
000c27b9: FADD float ptr [0x0018766c]      ; same constant on the other axis
```
So the shadow pass offsets **both x and y by +2.0**, with R/G/B forced to 0
(`MOV [0x001baf74/78/7c], EAX` where `EAX = 0`) and the alpha at
`DAT_001baf70` left untouched; the second pass restores the colour and draws at
the unmodified coordinates.

This also pins the colour-global layout: **`DAT_001baf70` = alpha,
`0x001baf74/78/7c` = R/G/B**, consistent with `ScreenBase_DrawFrame`'s fade
block writing the fade amount to `DAT_001baf70` and the colour components to
the other three.

The port had been using a 1px offset, which at these font scales hides almost
entirely behind the glyph — it read as "no shadow at all".

## Button-glyph lookup: the ShapeManager (A-button sprite NOT yet resolved)

Chased the title screen's `select` button icon (glyph index 62). Established
the system, but **did not** resolve the sprite rect — recorded here honestly so
the next pass starts from facts, not from the placeholder.

### It's the ShapeManager, not an "icon atlas"
`FUN_000a3e60` (the button's icon setter) is a one-line forwarder to what was
named `IconAtlas_GetEntry` (`0x000f24e0`):
```c
undefined * __thiscall ShapeManager_GetShape(int param_1, int param_2) {
    if (param_2 < 0) return &DAT_001bd0a0;   // sentinel for negative index
    return (undefined *)(param_2 * 0x1c + 8 + param_1);
}
```
`Application_InitSubsystems` allocates it as tag **`"shpMngr"`, size `0x1614`**,
storing it at `[DAT_001e3c7c + 0x724]` — the exact pointer the button path
loads into ECX. So `IconAtlas` and `ShapeManager` are one system.
**Renamed `IconAtlas_GetEntry` -> `ShapeManager_GetShape` in Ghidra** (the old
name was a misnomer coined before the two were connected).

### Layout, confirmed
`ShapeManager_Construct` (`0x000a9a90`) initialises **200 slots x 28 bytes** at
`this+8` (array spans `0x8`..`0x15E7`):

| Field | Init | Meaning |
|---|---|---|
| `[0]` | `0xffffffff` | texture id / handle (sentinel = unset) |
| `[1]`,`[2]` | `1.0`, `1.0` | UV scale (inferred) |
| `[3]`,`[4]` | `0`, `0` | UV offset (inferred) |
| `[5]`,`[6]` | `1.0`, `1.0` | (unidentified) |

Immediately after the array, `+0x15E8`..`+0x1600` is a **7-entry texture-handle
cache**, filled by `FUN_000f2550` from named `.xsh` entries:

| Slot | Name | | Slot | Name |
|---|---|---|---|---|
| `+0x15e8` | `gene` | | `+0x15f8` | `fe_2` |
| `+0x15ec` | `trik` | | **`+0x15fc`** | **`xbox`** |
| `+0x15f0` | `cons` | | `+0x1600` | `dvd1` |
| `+0x15f4` | `fe_1` | | | |

### Shape 62 is bound to the `xbox` sheet — proven
`FUN_000f2550` line: `*(undefined4 *)(param_1 + 0x6d0) = uVar1;` where
`uVar1 = *(param_1 + 0x15fc)` (= `xbox`), and `(0x6d0 - 8) / 0x1c = 62` exactly.
This confirms what was previously only inferred from "there is no button font".

### The unresolved part
`FUN_000f2550` and `FUN_000f29e0` write **only field `[0]`** of each shape —
verified by scanning every `param_1 + <off>` write in both and checking
`(off-8) % 0x1c`; the only non-zero-field hits are the texture cache above, not
array slots. And `searchFunctions query=Shape` returns exactly two functions.

So nothing found so far sets a shape's UV transform, which means shapes 61+ —
all bound to the same 256x256 `xbox` texture with identity UVs — would render
identically. That cannot be right, so **the per-shape sub-rect comes from
somewhere not yet located**. Candidates for the next pass: the `.xsh` sheet's
own per-entry sprite table (the `xbox` entry may itself carry sub-rects), or a
separate index->UV table consulted at draw time rather than at bind time.

Until that is found, the port keeps a green pip standing in for the A button.
It is the last placeholder on the title screen other than the 3D backdrop.

## Shape/sprite system fully decoded (except one gap) — A button now real

### Record layout — PROVEN, not inferred
`Sprite_DrawAligned` (`0x000f4380`) consumes a shape and settles the layout:
```c
(**(...+ 0xf8))(*param_1, 0);   // SetTexture(shape[0])
uVar2 = param_1[3];  uVar3 = param_1[6];   // V pair, swapped on vertical flip
uVar4 = param_1[5];  uVar5 = param_1[4];   // U pair, swapped on horizontal flip
```
and `Sprite_DrawIconByIndex` (`0x00056450`) uses `[1]`/`[2]` as size multipliers:
```c
Sprite_DrawAligned(x, y, w * *(float*)(shape+4), h * *(float*)(shape+8), flags);
```

| Field | Offset | Meaning | Default |
|---|---|---|---|
| `[0]` | +0x00 | texture handle | `0xffffffff` |
| `[1]`,`[2]` | +0x04,+0x08 | width, height | 1.0, 1.0 |
| `[3]` | +0x0c | **v0** | 0 |
| `[4]` | +0x10 | **u0** | 0 |
| `[5]` | +0x14 | **u1** | 1.0 |
| `[6]` | +0x18 | **v1** | 1.0 |

Defaults = whole texture, unit size.

### The draw path for a button glyph
`f3icon` widget (vtable `PTR_FUN_00197d38`, ctor `FUN_000858a0`, 0x100 bytes)
stores the shape index at **`+0xf0`** (init `-1`). `FUN_000a41c0` writes it
there along with a 20x22 size. Vtable **slot 11** (`FUN_000a56a0`) draws it:
```c
uVar3 = *(undefined4 *)(param_1 + 0xf0);       // shape index
FUN_000a3e60(uVar3, ...);                       // -> ShapeManager_GetShape
Sprite_DrawAligned(uVar3, x, y, ...);
```
`FUN_000a3e60` is a global accessor: `MOV EAX,[0x001e3c7c]; MOV ECX,[EAX+0x724];
JMP ShapeManager_GetShape`.

That same function re-confirms the colour globals: the widget's colour vec4 at
`+0x50..+0x5c` is copied straight into `DAT_001baf70/74/78/7c`, and vtable slot
9 (`FUN_000fb080`) is its setter — so `baf70` = alpha, `74/78/7c` = R/G/B.

### The atlas is genuinely subdivided — dumped and confirmed
Decoded `fe_1.xsh`'s `xbox` entry (fmt `0x7D` ARGB8888, 256x256) to PNG. It is a
sprite sheet: the whole controller, individual buttons/triggers, and the four
face buttons in a row at **y=230, each 17x22**:

| Button | x | avg RGB |
|---|---|---|
| Y | 6 | (193,186,25) |
| X | 32 | (27,130,157) |
| **A** | **57** | **(90,160,74)** |
| B | 81 | (152,32,33) |

Sprite height **22 exactly matches** `FUN_000a41c0`'s hardcoded `22.0` draw
height — independent corroboration that these are the button icons.

There is no sprite table in the `.xsh` itself: `fe_1.xsh` is `0x100210` bytes
and pixel data ends at `0x100200`, leaving only a 16-byte footer
(`70 00 00 00` + `"dvd1"`).

### THE REMAINING GAP: nobody writes the UV fields
Exhaustively checked:
* All **seven** texture binders (`FUN_000f2550`, `29e0`, `2ef0`, `3050`,
  `30b0`, `30e0`, `3180`) write **only field `[0]`** — 270 assignments, zero
  UV writes, verified mechanically by testing `(offset-8) % 0x1c`.
* Every function that calls `ShapeManager_GetShape` or the global accessor
  (~45 distinct) — **zero** writes through the returned pointer.
* `ShapeManager_Construct` sets all 200 slots to identity.
* Only two `ShapeManager_*` functions exist in the binary.

So with what is currently known, every shape sharing the `xbox` texture would
draw the *whole* atlas — which cannot be what the game does. **The per-shape UV
writer is still unlocated.** Byte-pattern searches for `FSTP [reg+0xc/0x10/
0x14/0x18]` produce only false positives (any float struct matches), and a
modulus scan over the whole export flags 664 functions, so neither discriminates.

Next ideas: the `arg` column of the pack table at `DAT_001bb948` (currently all
zero for the first 14 entries) may point at per-pack sprite metadata; or the
renderer's `vtable[0xac]` texture-create may itself carry sub-rect info from the
sheet entry.

### What the port does now
Draws the **real A-button sprite** — atlas rect (57, 230, 17x22) — instead of a
flat green pip. Sprite identity is a sound deduction, not a guess: the button
table at `DAT_001b9d00` uses glyph 62 for `0x1` "next", `0x2` "select", `0x4`
"play video" and `0x4000` "accept" (all A-button actions), while 63 is
"previous". **Documented caveat in the source**: the rect is measured from the
atlas, not read from the game's shape table, because that table's UV source is
the gap above.
