#!/bin/bash
# Test rig (Linux): (re)start Azahar headless under Xvfb with the freshly built plugin.
#   AZAHAR = extracted Azahar AppImage folder (squashfs-root)   ROM = your own Ultra Moon dump
#   SDMC   = Azahar's sdmc folder (default ~/.local/share/azahar-emu/sdmc)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
: "${AZAHAR:?set AZAHAR to the extracted Azahar AppImage folder}"; : "${ROM:?set ROM to your game dump}"
SDMC=${SDMC:-$HOME/.local/share/azahar-emu/sdmc}
pkill -9 -x AppRun.wrapped || true; sleep 2
if ! pgrep -x Xvfb >/dev/null; then rm -f /tmp/.X11-unix/X99 /tmp/.X99-lock; (nohup Xvfb :99 -screen 0 1280x960x24 >/tmp/xvfb.log 2>&1 &); sleep 2; fi
P=$SDMC/luma/plugins/00040000001B5100; mkdir -p "$P"
cp "$HERE/../plugin/build/OverworldEncounters.3gx" "$P/"
rm -f "$P/owe_log.txt"
cd "$AZAHAR" && (DISPLAY=:99 QT_QPA_PLATFORM=xcb nohup ./AppRun "$ROM" > /tmp/azahar.log 2>&1 &)
