#!/bin/bash
# Launch straight into a race, scripted through the Time Trial menus (development; run from the repository root):
#   race.sh OUTDIR LOCATION TRACK CAR PAINT LOOK FRAMES [EVERY] [extra arguments, e.g. --option draw_distance=extreme]
# LOCATION 0 Everglade, 1 Backwater, 2 Slaughter Yard, 3 Red Rock, 4 Campy, 5 Dead Man's Drop; TRACK 1-4;
# CAR 0 Highrider, 1 Blackfoot, 2 Marauder, 3 Polecat, 4 Mudcat, 5 Zambezi, 6 Bronco, 7 Rodeo, 8 Quasar, 9 Hornet, 10 Comanche,
# 11-12 ?, 13 Hurricane, 14 Firesole, 15 Mojo (all unlocked here); PAINT steps right from the first; LOOK day, sunset, night, day_rain, night_snow...; FRAMES counted from the race's first frame.
# Environment passes through: REAL=1 a real-time run (no --test), HOLD=<frames> of throttle (default all), BIN=<program> (default
# build/offroad: e.g. the installed one, to compare), any ORR_*.
out=$1; loc=$2; tr=$3; car=$4; pa=$5; look=$6; n=$7; ev=${8:-60}; shift 8 2>/dev/null || shift $#
mkdir -p "$out"
K='100:ESC,300:ESC,500:ESC,700:ESC,1000:UP,1040:UP,1080:UP,1200:ENTER,1500:ENTER'
f=1560
key() { K="$K,$f:$1"; f=$((f+${2:-50})); }
key UP; key UP
for ((i=0;i<car;i++)); do key RIGHT; done
key DOWN
for ((i=0;i<pa;i++)); do key RIGHT; done
key DOWN; f=$((f+30)); K="$K,$f:ENTER"; f=$((f+240))
key UP; key UP
for ((i=0;i<loc;i++)); do key RIGHT; done
key DOWN
for ((i=1;i<tr;i++)); do key RIGHT; done
key DOWN; K="$K,$f:ENTER"; f=$((f+1000))
start=$f
K="$K,$start:UP*${HOLD:-$n}"
# STEER="<frames from the start>:LEFT*<frames>,..." more keys, while it runs
for k in ${STEER//,/ }; do K="$K,$((start+${k%%:*})):${k#*:}"; done
rm -rf "$out.user"
test=--test; [ -n "$REAL" ] && test=
ORR_UNLOCK_ALL=1 ORR_RACE_LOOK=$look ORR_WIDESCREEN=1 ORR_GPU_OFFSCREEN=1 ORR_DUMP_FRAMES=$out ORR_DUMP_EVERY=$ev ORR_DUMP_FROM=$start ORR_KEYS="$K" \
  timeout 600 ${BIN:-build/offroad} $test --user "$out.user" --frames $((start+n)) --logfile "$out.log" "$@" > "$out.out" 2>&1
echo "$out exit $? started $start VUID $(grep -c VUID "$out.out") $(grep -h 'baked lighting: [a-z]* track' "$out.log" | tail -1 | cut -d: -f2- | cut -c1-70)"
