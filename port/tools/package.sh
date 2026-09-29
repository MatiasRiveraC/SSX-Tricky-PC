#!/usr/bin/env bash
# Make a shareable folder from a build: the executable and the one runtime DLL
# it needs, with no game data in it.
#
#   bash port/tools/package.sh          # -> dist/SSX-Tricky-PC/
#
# Run from an MSYS2 UCRT64 shell (or set MINGW_BIN to its bin folder), so
# libwinpthread-1.dll can be found.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXE="$ROOT/port/build/SSX Tricky.exe"
DIST="$ROOT/dist/SSX-Tricky-PC"
MINGW_BIN="${MINGW_BIN:-$(dirname "$(command -v gcc || echo /ucrt64/bin/gcc)")}"

[ -f "$EXE" ] || { echo "Build first: $EXE not found"; exit 1; }
rm -rf "$DIST"
mkdir -p "$DIST"
cp "$EXE" "$DIST/"
cp "$MINGW_BIN/libwinpthread-1.dll" "$DIST/" || { echo "libwinpthread-1.dll not found in $MINGW_BIN"; exit 1; }
cp "$ROOT/README.md" "$DIST/README.md"
echo "packaged: $DIST"
ls -la "$DIST"
