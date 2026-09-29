# Building SSX Tricky PC from source

The port is two things built together:

- **The translated game**: SSX Tricky's Xbox executable (`default.xbe`) turned
  into C by [xboxrecomp](https://github.com/sp00nznet/xboxrecomp). This is
  generated from **your own copy of the game** and is never stored in this
  repository. It lands in `port/src/recomp/gen/` and
  `port/src/recomp/recomp_funcs.h`, both ignored by git.
- **Everything else**: the Xbox runtime (kernel, GPU, audio, input) in
  `xboxrecomp/src/`, and the PC side (launcher, menu bar, controls, settings)
  in `port/src/`.

## What you need

| | |
|---|---|
| **Compiler** | [MSYS2](https://www.msys2.org/) **UCRT64** with GCC 13 or newer: `pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake make` |
| **CMake** | 3.20 or newer (the MSYS2 one is fine) |
| **Python** | 3.10 or newer, with `pip install capstone numpy pillow` |
| **The game** | Your own **SSX Tricky (USA) Xbox** disc image |

## 1. Get `default.xbe` out of your disc image

Extract the disc image with any XDVDFS tool, for example
[extract-xiso](https://github.com/XboxDev/extract-xiso):

```bash
extract-xiso -x "path/to/SSX Tricky.iso" -d game_files
```

`game_files/` needs at least `default.xbe`. It is ignored by git.

## 2. Generate the translated code

```bash
bash port/tools/regen.sh _local/gen_new
```

This runs the recompiler over every function the project has found
(`port/src/seeds.json`, 12,581 entry points) and takes under a minute.

> [!WARNING]
> **Regeneration is not yet complete for SSX Tricky.** `regen.sh` produces the
> base translation. The tree the port is actually built and tested from
> also has:
>
> - fix-up passes applied to the generated C (`xboxrecomp/tools/audit/fix*.py`:
>   x87 memory operands, flags across split functions, SIMD, `rep` strings, ...),
> - functions recovered after the first pass (`recover_batch.py`,
>   `vtsweep.py`, `switchtargets.py`),
> - a small number of bodies fixed by hand and checked against the XBE with
>   `xverify.py` (for example the CRT float-to-int helper at `0x0015CA68`).
>
> Folding each of these into the recompiler itself, so that one command
> reproduces the working tree, is ongoing work; `docs/notes/` records every
> one of them. Until then a from-scratch regeneration builds and boots less
> far than the released executable.

Copy the result into place when you are happy with it:

```bash
mkdir -p port/src/recomp && cp -r _local/gen_new port/src/recomp/gen
```

## 3. Build

From an MSYS2 UCRT64 shell:

```bash
cmake -S port -B port/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-O3 -g -DNDEBUG"
```

```bash
cmake --build port/build -j
```

`-g` keeps line information in the optimised build, which is what lets
`addr2line` and the crash reporter name a generated line. A full build takes a
few minutes; the generated files are large.

## 4. Run

```bash
"port/build/SSX Tricky.exe"
```

With no arguments it opens the launcher: set your disc image in **Settings**,
then **Start game**. Developers usually skip the launcher:

```bash
"port/build/SSX Tricky.exe" --direct
```

`--direct` (or redirected output) starts straight away at 640x480 using
`port/build/game/` or a disc image passed on the command line, with the save
folder `hdd/` beside the executable.

To share a build, `bash port/tools/package.sh` makes `dist/SSX-Tricky-PC/` with
the executable and `libwinpthread-1.dll`, and no game data.

## Checking a change

Every renderer or runtime change is checked against the menus and a race
before it goes in:

```bash
python xboxrecomp/tools/audit/menucheck.py
```

```bash
python xboxrecomp/tools/audit/walkcap.py _local/cap 95 --direct
```

```bash
python xboxrecomp/tools/audit/xemucompare.py _local/pairs.png _local/cap
```

`menucheck.py` compares the frontend against reference frames;
`walkcap.py` + `xemucompare.py` put a race next to screenshots from real
hardware emulation (`docs/reference/xemu_frames/`). The diagnostic switches
(`XBOX_FPS_LOG`, `XBOX_PROFILE`, `XBOX_NV2A_DRAWLOG`, ...) are listed in
`xboxrecomp/tools/audit/README.md`.
