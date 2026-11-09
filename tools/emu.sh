#!/bin/bash
# helper: emu.sh key <xdotool-key> [holdms] | shot <file> | mash <key> <count> <delay>
export DISPLAY=:99
W=$(xdotool search --name "Ultra Moon$" | head -1)
case "$1" in
  key) xdotool windowactivate --sync $W 2>/dev/null; xdotool windowfocus --sync $W 2>/dev/null; xdotool keydown $2; sleep $(echo "${3:-120}/1000" | bc -l); xdotool keyup $2;;
  mash) xdotool windowactivate --sync $W 2>/dev/null; xdotool windowfocus --sync $W 2>/dev/null; for i in $(seq $3); do xdotool keydown $2; sleep 0.08; xdotool keyup $2; sleep $4; done;;
  shot) import -window root /tmp/full.png; convert /tmp/full.png -crop 768x576+256+180 -resize 70% $2;;
esac
