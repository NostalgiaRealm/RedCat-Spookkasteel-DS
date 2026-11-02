#!/bin/bash
# Runs a benchmark ROM and keeps a screenshot every N seconds. Usage: tour.sh <rom> <seconds> <outdir> <every>
out=$3; mkdir -p "$out"; D=${DISP:-96}
( for i in $(seq 1 $(( $2 / $4 ))); do sleep $4; DISPLAY=:$D import -window root -crop 256x500+0+30 "$out/s$(printf %03d $i).png" 2>/dev/null; done ) &
DISP=$D "$(dirname "$0")/run-melonds.sh" "$1" "$2" "$out/final.png"
