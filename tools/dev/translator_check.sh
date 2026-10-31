#!/bin/bash
# A translator change's checks: full fncheck against tests/fncheck_baseline.txt, the lockstep race
# run, optionally once more with extra environment (EXTRA="VAR=1"). Writes work/translator_check.txt.
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; OUT=work/translator_check.txt; mkdir -p work; : > $OUT
make JOBS=${JOBS:-16} build/ht2 build/ht2_ref build/fncheck > work/tc_build.log 2>&1 || { tail -20 work/tc_build.log >> $OUT; exit 1; }
echo "fpu_sw_cc sites: $(grep -o 'fpu_sw_cc(c)' gen/code_*.c | wc -l), fpu_sw sites: $(grep -o 'fpu_sw(c)' gen/code_*.c | wc -l)" >> $OUT
HT2_NATIVE=0 build/fncheck --game $G > work/tc_fncheck.log 2>&1
grep "^runs:" work/tc_fncheck.log >> $OUT
grep "^FAIL" work/tc_fncheck.log | cut -c1-14 | sort > work/tc_fails.txt
grep "^FAIL" tests/fncheck_baseline.txt | cut -c1-14 | sort > work/tc_base.txt
echo "fncheck FAIL functions not in the baseline: $(comm -23 work/tc_fails.txt work/tc_base.txt | tr '\n' ' ')" >> $OUT
mkdir -p work/seed
sed -e "s/^fullscreen=off/fullscreen=on/" -e "s/^d3d=off/d3d=on/" $G/TRUCK.INI > work/seed/TRUCK.INI; rm -f work/seed/options.txt
K="200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*4,1560:UP*200"
timeout 1800 .venv/bin/python tools/lockstep.py --game $G --seed work/seed --frames 1800 --no-bisect --keys "$K" 2>&1 | tail -1 | sed 's/^/lockstep: /' >> $OUT
if [ -n "$EXTRA" ]; then
    env $EXTRA timeout 1800 .venv/bin/python tools/lockstep.py --game $G --seed work/seed --frames 1800 --no-bisect --keys "$K" 2>&1 | tail -1 | sed "s/^/lockstep with $EXTRA: /" >> $OUT
fi
