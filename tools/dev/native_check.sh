#!/bin/bash
# The checks a native function must pass (DESIGN.md section 12): fncheck --native on each listed
# function, the lockstep race run, and presents with native code off and on. Writes work/native_check.txt.
#   tools/dev/native_check.sh 5aad50 5aabc0 ...      (SEEDS=200, LOCKSTEP=0 / PIXELS=0 to skip)
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; OUT=work/native_check.txt; mkdir -p work; : > $OUT
make JOBS=${JOBS:-16} build/ht2 build/ht2_ref build/fncheck > work/native_build.log 2>&1 || { tail -20 work/native_build.log >> $OUT; exit 1; }
for f in "$@"; do
    r=$(build/fncheck --game $G --native --only $f --seeds ${SEEDS:-200} 2>&1 | grep -E "^FAIL|^runs:" | head -4)
    echo "fncheck $f: $r" >> $OUT
done
if [ "${LOCKSTEP:-1}" = 1 ]; then
    mkdir -p work/seed
    sed -e "s/^fullscreen=off/fullscreen=on/" -e "s/^d3d=off/d3d=on/" $G/TRUCK.INI > work/seed/TRUCK.INI; rm -f work/seed/options.txt
    timeout 1800 .venv/bin/python tools/lockstep.py --game $G --seed work/seed --frames 1800 --no-bisect \
        --keys "200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200" 2>&1 | tail -1 | sed 's/^/lockstep: /' >> $OUT
fi
if [ "${PIXELS:-1}" = 1 ]; then
    tools/dev/compare_render.sh "env:HT2_NATIVE=0" "env:HT2_NOTHING=1" > /dev/null 2>&1
    tail -1 work/cmp/summary.txt | sed 's/^/pixels: /' >> $OUT
fi
