#!/bin/bash
# Real key input in melonDS on a private display. Usage: play-test.sh <rom> <level 1-5> <outdir> [script of "key seconds" lines]
rom=$(realpath "$1"); lvl=$2; out=$3; D=${DISP:-98}; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd); cfg="$here/out/melonds-config-$D"; mkdir -p "$cfg"
pgrep -a -x Xvfb | grep -q ":$D " || { rm -f /tmp/.X11-unix/X$D; nohup Xvfb :$D -ac -screen 0 600x900x24 >/dev/null 2>&1 & sleep 1; }
export DISPLAY=:$D
before=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}')
distrobox-host-exec env -u WAYLAND_DISPLAY DISPLAY=:$D XAUTHORITY=/dev/null flatpak run --socket=x11 --env=HOME="$cfg" --env=XDG_CONFIG_HOME="$cfg/.config" --env=QT_QPA_PLATFORM=xcb --nosocket=wayland --command=melonDS net.kuribo64.melonDS "$rom" > "$out/melonds.log" 2>&1 &
sleep 4; own=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}' | grep -vxF "${before:-none}")
sleep ${BOOT:-40}
win=$(xdotool search --name "melonDS" | tail -1); xdotool windowfocus "$win" 2>/dev/null; xdotool mousemove 100 100
shot(){ import -window root -crop 256x500+0+30 "$out/$1.png"; }
shot 0-menu
if [ "$lvl" != none ]; then
for i in $(seq 1 $lvl); do xdotool keydown Down; sleep 0.25; xdotool keyup Down; sleep 2; done
shot 1-select
xdotool keydown d; sleep 3; xdotool keyup d
fi
sleep ${LOAD:-60}
shot 2-level
n=3
while read -r key secs; do
  [ -z "$key" ] && continue
  if [ "$key" = shot ]; then shot "$n-$secs"; n=$((n+1)); continue; fi
  if [ "$key" = wait ]; then sleep "$secs"; continue; fi
  xdotool keydown "$key"; sleep "$secs"; xdotool keyup "$key"
done < "${4:-/dev/null}"
for i in $own; do distrobox-host-exec flatpak kill "$i"; done
wait 2>/dev/null
