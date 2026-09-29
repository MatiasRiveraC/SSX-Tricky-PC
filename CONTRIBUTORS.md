# Contributors

## MatiasRiveraC: project lead

- Started and directs the port.
- Decides what the port should do: the launcher, the display and graphics options, the controls, and what to fix next.
- Tests every build and plays it on their own PC, against the original running on xemu.

## Claude Code (Anthropic): AI coding assistant

This port was made with [Claude Code](https://claude.com/claude-code). Claude Code wrote most of this repository's code and notes, working under the project lead's direction:

- **Recompilation**: translating SSX Tricky's XBE with xboxrecomp, and finding and fixing the translation's bugs (x87 floating point, CPU flags, SIMD, switch tables, indirect calls, undiscovered functions).
- **Reverse engineering**: the game's boot, frame loop, frontend, rendering, audio, riders and file formats, recorded in `docs/notes/`.
- **Runtime**: the Xbox kernel, threads, timers, memory, disc image access, audio (APU to XAudio2) and input (XInput).
- **Graphics**: the NV2A push buffer to Direct3D 11 renderer, register combiners and vertex programs as shaders, mipmaps, resolution, native widescreen, anti-aliasing, frame pacing.
- **PC side**: the launcher, the settings file, the menu bar, control remapping, screenshots, logging.
- **Tools and docs**: the regression checks, profiler and verification tools in `xboxrecomp/tools/audit/`, and this README.

Commits made with Claude Code end with a `Co-Authored-By: Claude` line.

## sp00nznet: xboxrecomp

[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) is the static recompiler and Xbox runtime this port is built on. It's MIT licensed and vendored in `xboxrecomp/` with changes for this port. Its commit history, up to upstream commit `32da238`, is imported into this repository with every commit's original author, and this port's changes are in separate commits on top.

## Third-party work used

- [xemu](https://xemu.app): `xboxrecomp/src/nv2a/nv2a_psh.c` is a port of its register-combiner shader generator (LGPL-2.1-or-later), and its GPU emulation was the reference for the renderer.
- [Capstone](https://www.capstone-engine.org/) disassembly, and [Ghidra](https://ghidra-sre.org/) for reverse engineering.
