#!/bin/bash
# capcmp.sh NAME -- capture presents 300..484 (every 8th) and diff against the
# part-177 reference set (cap9). Prints per-frame differing pixel counts and
# the settled-logo verdict (frames 412..484).
S="C:/Users/MatiasPC/AppData/Local/Temp/claude/E--Emulators-Roms-Xbox-Original-Ssx-Decompiled/12c07aef-5cdd-4e2b-858a-9cf3e71e2c2e/scratchpad"
N="$1"
cd "E:/Emulators/Roms/Xbox Original/Ssx Decompiled/ssx_recomp/build" || exit 1
export XBOX_INPUT_HOST=0
mkdir -p "$S/$N"; rm -f "$S/$N/"*
XBOX_D3D_DUMP="$S/$N/f" XBOX_D3D_DUMP_FROM=300 XBOX_D3D_DUMP_EVERY=8 XBOX_D3D_DUMP_MAX=24 \
    timeout 30 "./SSX Tricky.exe" > "$S/$N.log" 2>&1
echo "exit $? frames $(ls "$S/$N" | wc -l)"
cd "$S" && python3 - "$N" <<'EOF'
import sys, glob
import numpy as np
from PIL import Image
n = sys.argv[1]
fs = sorted(glob.glob(n + '/f*.bmp')); rs = sorted(glob.glob('cap9/f*.bmp'))
diffs = []
for a, b in zip(fs, rs):
    x = np.asarray(Image.open(a).convert('RGB')).astype(int)
    y = np.asarray(Image.open(b).convert('RGB')).astype(int)
    diffs.append(int((np.abs(x - y).sum(axis=2) > 30).sum()))
print(' '.join(str(d) for d in diffs))
tail = diffs[-10:]
print('settled logo:', 'CLEAN' if max(tail) < 500 else 'DAMAGED (max %d)' % max(tail))
EOF
