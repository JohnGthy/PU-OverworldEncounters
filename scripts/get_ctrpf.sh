#!/bin/bash
# Downloads CTRPluginFramework (ThePixellizerOSS, pinned commit) and builds libctrpf.a into
# plugin/libctrpf/{include,lib}. Needs git and devkitARM + libctru.
# (The old prebuilt AlolanCTRPF library loads in Azahar but never reaches main().)
set -e
COMMIT=96fd3d940be85f014fad21dc16c4fd305fa798ce
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DKP=${DEVKITPRO:-/opt/devkitpro}
BIN=$DKP/devkitARM/bin
WORK=${WORK:-$ROOT/.ctrpf}
OUT=$ROOT/plugin/libctrpf
if [ ! -d "$WORK/src/.git" ]; then
  mkdir -p "$WORK"; git clone https://gitlab.com/thepixellizeross/ctrpluginframework.git "$WORK/src"
fi
git -C "$WORK/src" -c advice.detachedHead=false checkout -q $COMMIT
git -C "$WORK/src" submodule update --init --recursive -q || true
L=$WORK/src/Library
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"
INC="-I$L/include -I$DKP/libctru/include -I$L/libcwav/include -I$L/libcwav/libncsnd/include"
CF="-Wno-psabi -mword-relocations -ffunction-sections -fdata-sections -Os -fomit-frame-pointer $ARCH $INC -D__3DS__ -DNDEBUG=1 -DCWAV_DISABLE_DSP"
VER=(-DCTRPF_VERSION_MAJOR='"0"' -DCTRPF_VERSION_MINOR='"8"' -DCTRPF_VERSION_BUILD='"0"' -DCOMMIT_HASH='"96fd3d9"' -DCOMPILE_DATE='"pinned"')
OBJ=$WORK/obj; rm -rf "$OBJ"; mkdir -p "$OBJ"
n=0
while IFS= read -r f; do
  o=$OBJ/$(echo "$f" | md5sum | cut -c1-10)_$(basename "$f").o
  case $f in
    *.cpp) $BIN/arm-none-eabi-g++ $CF "${VER[@]}" -fno-rtti -fno-exceptions -std=gnu++20 -c "$f" -o "$o" ;;
    *.c)   $BIN/arm-none-eabi-gcc $CF -std=gnu11 -c "$f" -o "$o" ;;
    *.s)   $BIN/arm-none-eabi-gcc -x assembler-with-cpp $ARCH $INC -c "$f" -o "$o" ;;
  esac
  n=$((n+1))
done < <(find "$L/source" "$L/libcwav/source" "$L/libcwav/libncsnd/source" \( -name "*.cpp" -o -name "*.c" -o -name "*.s" \) 2>/dev/null)
rm -rf "$OUT"; mkdir -p "$OUT/lib"
$BIN/arm-none-eabi-ar rcs "$OUT/lib/libctrpf.a" "$OBJ"/*.o
cp -r "$L/include" "$OUT/include"
cp "$WORK/src/LICENSE.txt" "$OUT/LICENSE.txt"
echo "built libctrpf.a from $n files -> $OUT"
