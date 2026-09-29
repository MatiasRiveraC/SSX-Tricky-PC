#!/bin/bash
# frontier.sh N -- one step of the recovery frontier with the APU running:
# run 50 s with change-triggered frame dumps, list ICALL-miss targets that look
# like code (inside .text, not registered), and recover them as one batch.
S="C:/Users/MatiasPC/AppData/Local/Temp/claude/E--Emulators-Roms-Xbox-Original-Ssx-Decompiled/12c07aef-5cdd-4e2b-858a-9cf3e71e2c2e/scratchpad"
ROOT="E:/Emulators/Roms/Xbox Original/Ssx Decompiled"
N="$1"
cd "$ROOT/ssx_recomp/build" || exit 1
export XBOX_INPUT_HOST=0
mkdir -p "$S/fr$N"; rm -f "$S/fr$N/"*
XBOX_APU_RUN=1 XBOX_D3D_DUMP="$S/fr$N/f" XBOX_D3D_DUMP_ONCHANGE=1 XBOX_D3D_DUMP_MAX=100 \
    timeout 50 "./SSX Tricky.exe" > "$S/fr$N.log" 2>&1
e=$?
echo "exit=$e crash=$(grep -c CRASH "$S/fr$N.log") text=$(grep -c '\[TEXT\]' "$S/fr$N.log") $(grep -o 'draws=[0-9]*' "$S/fr$N.log" | tail -1) frames=$(ls "$S/fr$N" | wc -l) $(grep '\[XA2\].*buffers sent' "$S/fr$N.log" | tail -1)"
T=$(grep -o "unresolved target 0x[0-9A-F]*" "$S/fr$N.log" | awk '{print $3}' | sort -u | python3 -c "
import sys,re
reg=set()
for l in open(r'$ROOT/ssx_recomp/src/recomp/gen/recomp_dispatch.c',encoding='utf-8',errors='replace'):
    m=re.search(r'\{ 0x([0-9A-F]{8})u',l)
    if m: reg.add(int(m.group(1),16))
out=[]
for t in sys.stdin.read().split():
    v=int(t,16)
    if 0x11000<=v<0x187000 and v not in reg and v!=0x17FCEC: out.append('0x%08X'%v)
print(' '.join(out))")
echo "targets: $T"
if [ -n "$T" ] && [ "$2" != "--no-recover" ]; then
    cd "$ROOT/xboxrecomp/tools/audit" && python3 recover_batch.py --runs 2 --seconds 45 \
        --note "Part 178 frontier step $N (APU running)" $T 2>&1 | grep -E "lifted|could not|closure|skipping|link|run[0-9]|KEPT|revert"
fi
