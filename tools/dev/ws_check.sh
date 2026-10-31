#!/bin/bash
# (development script; run from the repository root) The same scripted race with widescreen on and off, on
# lavapipe with the validation layer: frames in work/ws_on, work/ws_off, logs beside them.
make JOBS=${JOBS:-14} build/offroad > work/ws_build.log 2>&1 || exit 1
K="100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER,1800:ENTER,2100:ENTER,2400:ENTER,2700:ENTER,3100:UP*1200,3400:RIGHT*25,3800:LEFT*25"
for m in ${MODES:-on off}; do
  O=work/ws_$m; rm -rf $O $O.user; mkdir -p $O $O.user
  ORR_WIDESCREEN=1 ORR_GPU_OFFSCREEN=1 ORR_GPU_OFFSCREEN_SIZE=1920x1080 ORR_DUMP_FRAMES=$O ORR_DUMP_EVERY=300 ORR_KEYS="$K" \
    timeout 1500 build/offroad --user $O.user --test --option widescreen=$m --frames ${FRAMES:-4300} --logfile $O.log > $O.out 2>&1
  echo "$m exit $?, frames $(ls $O | wc -l), FATAL $(grep -c FATAL $O.log), validation messages $(grep -c VUID $O.out)"
done
