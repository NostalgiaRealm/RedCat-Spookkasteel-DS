#!/bin/bash
# Usage: run-melonds.sh <rom> <seconds> <out.png> [input script]. Runs the host's melonDS Flatpak on a private
# virtual display with a private config, screenshots it, then stops only the instance it started.
# The input script (optional) runs after <seconds>/2 with DISPLAY set, for xdotool keys.
rom=$(realpath "$1"); secs=${2:-10}; out=${3:-/tmp/claude-1000/ds.png}; D=${DISP:-97}
here=$(cd "$(dirname "$0")" && pwd); cfg="$here/out/melonds-config"; mkdir -p "$cfg/melonDS"
pgrep -a -x Xvfb | grep -q ":$D " || { rm -f /tmp/.X11-unix/X$D; nohup Xvfb :$D -ac -screen 0 600x900x24 >/dev/null 2>&1 & sleep 1; }
before=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}')
distrobox-host-exec env -u WAYLAND_DISPLAY DISPLAY=:$D XAUTHORITY=/dev/null flatpak run --socket=x11 --env=HOME="$cfg" --env=XDG_CONFIG_HOME="$cfg/.config" --env=QT_QPA_PLATFORM=xcb --nosocket=wayland --command=melonDS net.kuribo64.melonDS "$rom" > "$here/out/melonds.log" 2>&1 &
sleep 4; own=$(distrobox-host-exec flatpak ps --columns=instance,application | awk '/melonDS/{print $1}' | grep -vxF "${before:-none}"); half=$((secs/2)); sleep $half; [ -n "$4" ] && DISPLAY=:$D bash "$4"; sleep $((secs-half))
DISPLAY=:$D import -window root "$out"
for i in $own; do distrobox-host-exec flatpak kill "$i"; done
wait 2>/dev/null
