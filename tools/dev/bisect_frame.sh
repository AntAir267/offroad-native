#!/bin/bash
# Lockstep follow-up: the first runtime call in frame N after which the translated and reference
# builds differ, and the memory that differs there. Needs work/lockstep/nat.hash from a
# lockstep run and work/seed (as lockstep.py --seed). Writes work/bisect_N.txt.
#   tools/dev/bisect_frame.sh N "200:ESCAPE*4,..."
N=$1; K=$2
G=${GAME:-$HOME/.local/share/hardtruck2-native/game}; W=work/lockstep; OUT=work/bisect_$N.txt
lo=$(sed -n "$((N-1))p" $W/nat.hash | cut -d' ' -f2); hi=$(sed -n "${N}p" $W/nat.hash | cut -d' ' -f2)
run() {
    b=$1; shift; bin=ht2; [ $b = ref ] && bin=ht2_ref
    rm -rf $W/user_$b; cp -r work/seed $W/user_$b
    HT2_CALLS_ONLY=${CALLS_ONLY:-} HT2_KEYS="$K" build/$bin --game $G --user $W/user_$b --test \
        --frames $((N+1)) --logfile $W/$b.log "$@" > /dev/null 2>&1
}
run nat --hash-calls $lo $hi $W/nat.calls & run ref --hash-calls $lo $hi $W/ref.calls & wait
first=$(paste -d' ' $W/nat.calls $W/ref.calls | awk '{ if ($2!=$5 || $3!=$6) {print $1; exit} }')
echo "frame $N calls $lo..$hi: first difference after call ${first:-none}" > $OUT
[ -n "$first" ] || exit 0
paste -d' ' $W/nat.calls $W/ref.calls | awk -v f=$first '$1>=f-4 && $1<=f+2' >> $OUT
run nat --dump-memory-call $first $W/nat.mem & run ref --dump-memory-call $first $W/ref.mem & wait
.venv/bin/python tools/memdiff.py $W/nat.mem $W/ref.mem | head -60 >> $OUT
