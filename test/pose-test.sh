#!/bin/bash
# Steps a BENCH=3 ROM through test/poses.txt with the A key and saves each view. Usage: pose-test.sh <rom> <outdir>
rom=$(realpath "$1"); out=$2; D=${DISP:-97}; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd); cfg="$here/out/melonds-config-$D"; mkdir -p "$cfg"
pgrep -a -x Xvfb | grep -q ":$D " || { rm -f /tmp/.X11-unix/X$D; nohup Xvfb :$D -ac -screen 0 600x900x24 >/dev/null 2>&1 & sleep 1; }
export DISPLAY=:$D
before=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}')
distrobox-host-exec env -u WAYLAND_DISPLAY DISPLAY=:$D XAUTHORITY=/dev/null flatpak run --socket=x11 --env=HOME="$cfg" --env=XDG_CONFIG_HOME="$cfg/.config" --env=QT_QPA_PLATFORM=xcb --nosocket=wayland --command=melonDS net.kuribo64.melonDS "$rom" > "$out/melonds.log" 2>&1 &
sleep 4; own=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}' | grep -vxF "${before:-none}")
sleep ${BOOT:-10}
win=$(xdotool search --name "melonDS" | tail -1); xdotool windowfocus "$win" 2>/dev/null; xdotool mousemove 100 100
last=-1
while read -r level file; do
  [ "$level" != "$last" ] && sleep ${LOAD:-14}; last=$level
  sleep 1.2
  import -window root -crop 256x${CROP:-192}+0+30 "$out/$file"
  xdotool keydown d; sleep 0.2; xdotool keyup d; sleep 0.3
done < "$here/poses.txt"
for i in $own; do distrobox-host-exec flatpak kill "$i"; done
wait 2>/dev/null
