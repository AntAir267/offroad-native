#!/bin/bash
# CPU/GPU cost of two option settings in the same real-time off-screen scene (parked in the yard,
# 60 fps pacing), e.g. tools/dev/perf_ab.sh renderer=original renderer=native. Writes work/perf_ab.txt.
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; OUT=work/perf_ab.txt; : > $OUT
for opt in "$@"; do
    rm -rf work/perfuser; mkdir -p work/perfuser
    envs=(); o=$opt                                 # "env:VAR=value" sets a variable instead
    if [[ $opt == env:* ]]; then read -ra envs <<< "${opt#env:}"; o="renderer=${RENDERER:-original}"; fi
    /usr/bin/time -f "%U user s, %S sys s, %e wall s" -o work/perf_time.txt env "${envs[@]}" HT2_VK_STATS=1 HT2_CPU_FROM=1500 \
        HT2_PACE=60 HT2_GPU_OFFSCREEN=1 HT2_GPU_OFFSCREEN_SIZE=${SIZE:-2560x1440} HT2_WIDESCREEN=1 \
        HT2_PAD="300:START*6,420:START*6,700:A*6,850:A*6" \
        build/ht2 --game $G --user work/perfuser --option "$o" --frames 3300 --logfile "work/perf_$opt.log" > /dev/null 2>&1
    echo "== $opt: $(cat work/perf_time.txt)" >> $OUT
    grep "game thread CPU" "work/perf_$opt.log" | tail -1 | sed 's/.*\] //' >> $OUT
    grep "per frame over" "work/perf_$opt.log" | tail -1 | grep -o "[0-9.]* ms in all\|vertex processing\|[0-9.]* vertex processing\|game thread CPU [0-9.]* ms\|GPU [0-9.]* ms" | tr '\n' ' ' >> $OUT; echo >> $OUT
done
