#!/bin/bash
# (development script; run from the repository root)
# usage: shot.sh OUTDIR FRAMES EVERY KEYS [extra args]: an off-screen --test run on the GPU
# (SIZE=1280x960 by default) with the presented frames dumped into OUTDIR
out=$1; frames=$2; every=$3; keys=$4; shift 4
rm -rf "$out" "$out.user"; mkdir -p "$out" "$out.user"
ORR_GPU_OFFSCREEN=1 ORR_GPU_OFFSCREEN_SIZE=${SIZE:-1280x960} ORR_DUMP_FRAMES=$out ORR_DUMP_EVERY=$every ORR_KEYS="$keys" \
  timeout 900 build/offroad --user "$out.user" --test --frames $frames --logfile "$out.log" "$@" >/dev/null 2>&1
echo "exit $?"; ls "$out" | wc -l
