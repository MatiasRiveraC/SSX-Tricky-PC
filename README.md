# SSX Tricky: PC Port

This is a native Windows port of the 2001 Xbox game *SSX Tricky*. It isn't an emulator. The game's own code is translated to C with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and built into an ordinary Windows program. It uses Direct3D 11 for graphics, XAudio2 for sound and XInput for controllers.

It also adds:

- **Any resolution**, drawn natively (not stretched from 640×480): 720p, 1080p, 1440p, 4K and the 4:3 sizes, windowed or fullscreen.
- **Real widescreen**: 16:9 switches on SSX Tricky's *own* widescreen mode, the one an Xbox set to widescreen gets, so the game draws a wider view itself.
- **A steady 60 fps** in races.
- **Sharper textures**: full mipmaps, anisotropic filtering (up to 16×) and anti-aliasing (2×, 4×, 8×).
- **A launcher** with Start game and Settings, and a **menu bar** in the game window (screenshots, window size, graphics options, controls, exit).
- **Remappable controls** for the keyboard and for XInput controllers.

> [!NOTE]
> **Development has continued in [OpenTricky](https://github.com/GiZcesi/OpenTricky)** by GiZcesi, which picked this project up where I left off. For active development, newer versions and support, go there. This repository is kept as the original work.

> [!IMPORTANT]
> **This repository has no game in it, and it publishes no builds.** It holds no disc image, XBE, game data, audio, movies or translated game code. You need your **own copy of the Xbox game** (the USA release). Make a disc image of it (`.iso`), build the port from that image, and point the launcher at it. A build made this way contains code translated from the game, so it is for your own use and should not be shared.

> [!NOTE]
> **This port was made with [Claude Code](https://claude.com/claude-code)**, Anthropic's AI coding assistant. Claude Code wrote most of the port's code, tools and notes, working with the project's human author, who directed, tested and played it. See [Contributors](#contributors).

> [!WARNING]
> **Work in progress, and no longer developed here.** When development stopped in this repository, the game booted, played its intro movies with sound, ran the whole frontend, and raced against the AI at 60 fps through the finish and the results screen. Hard-disk saves worked. See [Status](#status).

---

## Contents

- [What you need](#what-you-need)
- [Build and play](#build-and-play)
- [Controls](#controls)
- [Settings](#settings)
- [Status](#status)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Troubleshooting](#troubleshooting)
- [Contributors](#contributors)
- [Legal and credits](#legal-and-credits)

---

## What you need

| | |
|---|---|
| **The game** | A disc image of your own *SSX Tricky* **Xbox** disc, **USA** release, as an `.iso`. The launcher checks that it is exactly the build this port was made from. The PS2 and GameCube versions won't work. |
| **PC** | Windows 10 or 11 (64-bit), with a graphics card that supports Direct3D 11. |
| **Controller** | Optional. Any XInput (Xbox-style) pad works, and so does the keyboard. |

---

## Build and play

There are no prebuilt downloads. The translated game code is generated on your own PC from your own disc, so you build the port yourself.

1. **Make a disc image** of your Xbox game disc, as an `.iso`.
2. **Build the port** from that image by following [Building from source](#building-from-source).
3. **Open `SSX Tricky.exe`** from your build output. The launcher opens with **Start game**, **Settings** and **Exit**.
4. In **Settings**:
   1. Under **Disc image**, choose your `.iso`. The launcher reads it (it never changes it) and checks it is the right game and version.
   2. Under **Save folder**, keep the default (`hdd\` beside the exe) or choose another folder. This is the Xbox hard disk: your saves and settings live here.
   3. Pick the **resolution**, **4:3 or 16:9**, **fullscreen**, **textures**, **anti-aliasing** and the other options. The launcher shows recommended values.
5. Press **Start game**. It stays greyed out until a valid disc image is set.

Settings are saved in `SSX Tricky.ini` beside the exe. Screenshots go to `Screenshots\`.

---

## Controls

Everything can be remapped: **Input → Controls…** in the game window's menu, or **Controls…** in the launcher's Settings.

| Xbox button | Controller | Keyboard |
|---|---|---|
| Start | Start | `Enter` |
| Back | Back | `Tab` |
| A / B / X / Y | A / B / X / Y | `Space` / `Esc` / `C` / `V` |
| Black / White | RB / LB | `R` / `F` |
| Left / right trigger | LT / RT | `Q` / `E` |
| Left stick | Left stick | Arrow keys |
| D-pad | D-pad | Arrow keys |
| Right stick | Right stick | (not mapped) |

**In the game window:**

- `Alt+Enter` or `F11` switches between windowed and fullscreen.
- `F12` saves a screenshot.
- The menu bar (hidden in fullscreen) has **Game** (screenshot, open the screenshots or save folder, open the log, exit), **Video** (fullscreen, window size, textures, anti-aliasing, frame rate in the title bar), **Input** (controls) and **Help**.
- Closing the window quits the game.

---

## Settings

| Setting | What it does |
|---|---|
| **Disc image** | Your SSX Tricky (USA) Xbox `.iso`. |
| **Save folder** | The emulated Xbox hard disk: saves, profiles, options. |
| **Resolution** | What the game is drawn at, from 640×480 up to 4K. |
| **Aspect ratio** | **4:3** is the original picture. **16:9** turns on the game's own widescreen mode (it draws a wider view, not a stretched one). |
| **Fullscreen** | Borderless fullscreen. |
| **Textures** | Mipmaps and anisotropic filtering (2×–16×), for sharp textures at a distance and at an angle. |
| **Anti-aliasing** | Smooths edges: off, 2×, 4× or 8×. |
| **Show frame rate** | Frame rate in the window title. |
| **Write a log file** | `SSX Tricky.log` beside the exe, for bug reports. |

There is no language option: the game always uses its American text, and the USA disc only carries that one.

---

## Status

**Works:** boot, the EA and attract movies with sound, the whole frontend (title, mode, character, outfit, board, event, difficulty and venue select), loading, and full races with the AI riders, HUD, race clock and speed effects at 60 fps, through the finish camera and the results screen. Saving and loading on the hard disk. Rider voices, the announcer and the countdown.

**Known differences from the Xbox** (as of the last version developed in this repository; see [OpenTricky](https://github.com/GiZcesi/OpenTricky) for current progress):

- The pre-race intro (course fly-over and rider cutscene) is often skipped.
- Character dialogue starts slightly late.
- The Uberboard renders black on the board screen, and the name boards behind the riders in character select are lit a little differently.
- Some particle effects (snow spray, trails) are smaller or dimmer than on the console, and the course mist sits wrong.
- Courses other than Garibaldi and Mesablanca, and the other modes, are lightly tested.

---

## Building from source

The translated game code is **generated on your machine from your own disc**. It isn't stored here, and the result is for your own use only. The short version, from an [MSYS2](https://www.msys2.org/) UCRT64 shell with GCC, CMake and Python (`pip install capstone numpy pillow`):

```bash
extract-xiso -x "path/to/SSX Tricky.iso" -d game_files
```

```bash
bash port/tools/regen.sh _local/gen_new
```

```bash
cmake -S port -B port/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build port/build -j
```

> [!WARNING]
> `regen.sh` produces the recompiler's base translation. The working tree the port is tested from also has fix-up passes, recovered functions and a few hand-verified bodies that are not yet reproduced by one command. **[docs/building.md](docs/building.md)** explains each step and exactly what is still missing.

---

## Repository layout

| Path | What's there |
|---|---|
| `port/src/` | The PC side: entry point and launch modes, launcher and settings (`launcher.c`), the game window's menu bar (`hostui.c`), control mapping (`controls.c`), XInput for the game's input library (`xapi_input_hle.c`). |
| `port/src/recomp/` | The register model and helpers the translated code is built on (`recomp_types.h`). `gen/` (generated, not in git) goes here. |
| `port/src/seeds.json` | Every function entry point found in the XBE, for the recompiler. Addresses only. |
| `port/tools/` | `regen.sh` (translate your XBE) and `package.sh` (assemble a runnable folder from your own build). |
| `xboxrecomp/` | The static recompiler and Xbox runtime, vendored from [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp), plus this port's changes: the NV2A → Direct3D 11 renderer, register combiners and vertex programs as shaders, the kernel, timers, audio. |
| `xboxrecomp/tools/audit/` | The tools the port was built and checked with: race captures, a sampling profiler, recovery and verification of translated functions, and frontend checks that need reference images you supply locally (not included). See its `README.md`. |
| `docs/building.md` | The full build guide: each step, and what is not yet reproduced by one command. |

Comments in the code that cite `RE_NOTES` refer to the project's engineering notes, which are not included in this repository.

---

## Troubleshooting

- **Start game stays greyed out.** Set a disc image in **Settings**. Only the **USA Xbox** release is supported, and the image must be the full disc.
- **Windows says `libwinpthread-1.dll` is missing.** Copy it from your MSYS2 `ucrt64\bin` folder into the same folder as `SSX Tricky.exe`.
- **Windows SmartScreen warns about the exe.** The build isn't code-signed. Choose **More info → Run anyway**, since you built it yourself.
- **The game closed unexpectedly.** Tick **Write a log file** in Settings, reproduce it, and keep `SSX Tricky.log` for your report.
- **Something is drawn wrong.** Press `F12` for a screenshot, and note where in the game it happened.

This repository no longer takes new work. Bug reports and contributions go to [OpenTricky](https://github.com/GiZcesi/OpenTricky).

---

## Contributors

| Who | What |
|---|---|
| [**MatiasRiveraC**](https://github.com/MatiasRiveraC) | Project lead: direction, testing and playing. |
| [**Claude Code**](https://claude.com/claude-code) (Anthropic) | AI coding assistant: wrote most of the port. That includes the recompiler fixes, the reverse engineering, the Direct3D 11 renderer, the runtime, audio and input, the launcher and menus, the tools and this README. |
| [**sp00nznet**](https://github.com/sp00nznet) | [xboxrecomp](https://github.com/sp00nznet/xboxrecomp), the static recompiler and runtime this port is built on. Vendored from upstream commit `32da238`; its full history is in the upstream repository. |
| [**GiZcesi**](https://github.com/GiZcesi) | [OpenTricky](https://github.com/GiZcesi/OpenTricky), which continues this port from where development stopped here. |

> **AI disclosure:** this port was developed with Claude Code. The commit in this repository carries a `Co-Authored-By: Claude` line. Every change was run and tested on the project lead's own PC.

See [CONTRIBUTORS.md](CONTRIBUTORS.md).

---

## Legal and credits

- This is an unofficial fan project. It isn't affiliated with or endorsed by Electronic Arts, EA Canada or Microsoft. *SSX* and *SSX Tricky* are trademarks of Electronic Arts, and Xbox is a trademark of Microsoft. The names are used only to identify the game this port is compatible with.
- **No game material is included**: no disc image, XBE, data, audio, video or translated game code. This repository publishes no builds. You must own the game and supply your own disc image, and a build made from it is for your own use.
- **Recompiler and runtime**: [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) by sp00nznet, MIT license. See `xboxrecomp/LICENSE`.
- **Pixel shaders**: `xboxrecomp/src/nv2a/nv2a_psh.c` is a port of [xemu](https://xemu.app)'s register-combiner generator (`hw/xbox/nv2a/pgraph/glsl/psh.c`), LGPL-2.1-or-later; see that file's header. xemu was also the reference for how the Xbox GPU behaves.
- **Disassembly**: [Capstone](https://www.capstone-engine.org/). **Reverse engineering**: [Ghidra](https://ghidra-sre.org/).
- **This port's own code**: MIT license. See [`LICENSE`](LICENSE). Files and folders that carry their own license notice (`xboxrecomp/`, `xboxrecomp/src/nv2a/nv2a_psh.c`) keep it. The license covers this repository's code only, not the game.
