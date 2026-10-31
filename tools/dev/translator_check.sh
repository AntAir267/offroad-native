#!/bin/bash
# (development script; run from the repository root) The project's checks: full fncheck against
# tests/fncheck_baseline.txt, and the lockstep run of a Time Trial's menus, loading and driving.
# Writes work/translator_check.txt.
G=${GAME:-$HOME/.local/share/offroad-native/game}; OUT=work/translator_check.txt; mkdir -p work; : > $OUT
make JOBS=${JOBS:-16} build/offroad build/offroad_ref build/fncheck > work/tc_build.log 2>&1 || { tail -20 work/tc_build.log >> $OUT; exit 1; }
build/fncheck --game $G > work/tc_fncheck.log 2>&1
grep "^runs:" work/tc_fncheck.log >> $OUT
grep "^FAIL" work/tc_fncheck.log | cut -c1-14 | sort > work/tc_fails.txt
grep "^FAIL" tests/fncheck_baseline.txt | cut -c1-14 | sort > work/tc_base.txt
echo "fncheck FAIL functions not in the baseline: $(comm -23 work/tc_fails.txt work/tc_base.txt | tr '\n' ' ')" >> $OUT
K="100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*400"
timeout 3000 .venv/bin/python tools/lockstep.py --game $G --frames ${FRAMES:-3600} --no-bisect --keys "$K" 2>&1 | tail -15 >> $OUT
cat $OUT
