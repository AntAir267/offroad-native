#!/bin/bash
# Renderer A/B check: the same scripted scenes run twice, with options A and B (e.g.
# "renderer=original" "renderer=native"), presents dumped at fixed frames and compared pixel for
# pixel ("env:VAR=value" sets a variable instead). Deterministic pixels need lavapipe (t7 run --vk-software). Writes work/cmp/summary.txt and
# work/cmp/<scene>_<frame>.png (A | B | difference) for frames that differ.
#   tools/dev/compare_render.sh "renderer=original" "renderer=native"
A=$1; B=$2
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; W=work/cmp
rm -rf $W; mkdir -p $W
# scenes: name, frames, dump frames, keys
scenes=(
  "menus|900|300,420,600,800|200:ESCAPE*4,300:UP*3,330:UP*3,360:UP*3,390:UP*3,420:RETURN*3,600:UP*3,640:RETURN*3"
  "yard|2000|1500,1700,1900|200:ESCAPE*4,300:RETURN*4,400:RETURN*4"
  "drive|2600|1900,2100,2300,2500|200:ESCAPE*4,300:RETURN*4,400:RETURN*4,1500:SPACE*8,1700:UP*250,2100:DELETE*200"
)
run() {   # label, option, scene line
    IFS='|' read -r name frames dumps keys <<< "$3"
    local d=$W/$1_$name; mkdir -p $d/user
    local opt=$2 envs=()
    if [[ $opt == env:* ]]; then read -ra envs <<< "${opt#env:}"; opt="renderer=${RENDERER:-original}"; fi   # "env:HT2_RENDER_THREAD=1"
    env "${envs[@]}" HT2_WIDESCREEN=1 HT2_GPU_OFFSCREEN=1 HT2_GPU_OFFSCREEN_SIZE=${SIZE:-1280x720} HT2_DUMP_PRESENT=1 \
    HT2_DUMP_FRAMES=$d HT2_DUMP_LIST=$dumps HT2_KEYS="$keys" \
        build/ht2 --test --user $d/user --option "$opt" --frames $frames --logfile $d/log.txt > /dev/null 2>&1
    echo "$1 $name exit $?" >> $W/summary.txt
}
for sc in "${scenes[@]}"; do run a "$A" "$sc" & run b "$B" "$sc" & wait; done
same=0; diff=0
for sc in "${scenes[@]}"; do
    IFS='|' read -r name frames dumps keys <<< "$sc"
    for f in ${dumps//,/ }; do
        fa=$(printf "$W/a_$name/present_%05d.ppm" $f); fb=$(printf "$W/b_$name/present_%05d.ppm" $f)
        if [ ! -f $fa ] || [ ! -f $fb ]; then echo "$name $f MISSING" >> $W/summary.txt; diff=$((diff+1)); continue; fi
        if cmp -s $fa $fb; then same=$((same+1)); continue; fi
        n=$(compare -metric AE $fa $fb null: 2>&1 | cut -d' ' -f1)
        n3=$(compare -metric AE -fuzz 3% $fa $fb null: 2>&1 | cut -d' ' -f1)
        n10=$(compare -metric AE -fuzz 10% $fa $fb null: 2>&1 | cut -d' ' -f1)
        pae=$(compare -metric PAE $fa $fb null: 2>&1 | sed 's/.*(\(.*\))/\1/')
        echo "$name $f DIFFERS: $n pixels, $n3 by more than 3%, $n10 by more than 10%, peak $pae" >> $W/summary.txt; diff=$((diff+1))
        compare $fa $fb -compose src $W/d.png 2>/dev/null
        convert $fa $fb $W/d.png +append -resize 1920x $W/${name}_$f.png; rm -f $W/d.png
    done
done
echo "A=$A B=$B: $same identical, $diff different" >> $W/summary.txt
