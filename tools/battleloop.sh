#!/bin/bash
# Stress test: N automatic battles (plugin built with Config::debugAutoBattle = true), fleeing each time.
# The RUN button coordinates (639,660) assume tools/run.sh's Xvfb layout.
cd "$(dirname "$0")"; export DISPLAY=:99
L=${SDMC:-$HOME/.local/share/azahar-emu/sdmc}/luma/plugins/00040000001B5100/owe_log.txt
N=${1:-6}
cnt() { grep -ac "$1" $L; }
alive() { pgrep -x AppRun.wrapped >/dev/null || { echo "EMU DIED ($1)"; grep -av "^ \|^f[0-9]" $L | tail -8; exit 1; }; }
while [ $(cnt "battle trigger") -lt $N ]; do
  b=$(cnt "battle trigger"); f=$(cnt "FieldRo at")
  if [ $f -le $b ]; then   # in battle: run
    xdotool mousemove 639 660 click 1; sleep 1; ./emu.sh key a 100; sleep 4; alive "battle $b"
  else                     # in field: wiggle until the next battle
    ./emu.sh key Left 150; sleep 1.5; ./emu.sh key Right 150; sleep 1.5; alive "field after $b"
  fi
  T=$((T+1)); if [ $T -gt 400 ]; then echo TIMEOUT; exit 2; fi
  nb=$(cnt "battle trigger"); nf=$(cnt "FieldRo at")
  [ "$nb:$nf" != "$lb:$lf" ] && echo "battles=$nb fieldloads=$nf"; lb=$nb; lf=$nf
done
echo DONE
