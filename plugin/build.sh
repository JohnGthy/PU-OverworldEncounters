#!/bin/bash
# Builds plugin/build/OverworldEncounters.3gx.
# Needs devkitARM + libctru (devkitPro) and CTRPluginFramework (run ../scripts/get_ctrpf.sh once).
set -e
cd "$(dirname "$0")"
DKP=${DEVKITPRO:-/opt/devkitpro}
CTRPF=${CTRPF:-libctrpf}
[ -f "$CTRPF/lib/libctrpf.a" ] || { echo "CTRPluginFramework not found in $CTRPF - run scripts/get_ctrpf.sh first"; exit 1; }
CC=$DKP/devkitARM/bin/arm-none-eabi-gcc
CXX=$DKP/devkitARM/bin/arm-none-eabi-g++
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
FLAGS="$ARCH -Os -mword-relocations -fomit-frame-pointer -ffunction-sections -fno-strict-aliasing -D__3DS__ -I Includes -I $CTRPF/include -I $DKP/libctru/include -Wall"
mkdir -p build
OBJS=""
for f in Sources/*.cpp; do o=build/$(basename ${f%.cpp}).o; $CXX $FLAGS -fno-rtti -fno-exceptions -std=gnu++17 -c $f -o $o; OBJS="$OBJS $o"; done
for f in Sources/*.s; do [ -e "$f" ] || continue; o=build/$(basename ${f%.s}).o; $CC $ARCH -x assembler-with-cpp -c $f -o $o; OBJS="$OBJS $o"; done
$CXX -T 3gx.ld $ARCH -Os -Wl,--gc-sections,--strip-discarded,--strip-debug,--section-start,.text=0x07000100 -Wl,-Map,build/plugin.map $OBJS -L $CTRPF/lib -L $DKP/libctru/lib -lctrpf -lctru -lm -o build/plugin.elf
python3 mk3gx.py build/plugin.elf build/OverworldEncounters.3gx --title "Overworld Encounters" --author "Joko" --summary "Visible wild Pokemon in tall grass" --desc "SwSh-style overworld wild encounters for USUM" --target 0x1B5100
