#!/usr/bin/env bash
# The game package for the SDK's Android app (a game repo's android/ project): the game recompiled from the player's
# own disc and built for Android as libraries (one lib<exe>.so per xbe), DXC for Android beside them, the disc's files
# (assets/: the extracted files or the .iso) and, if given, a texture pack. The player copies the zip to the phone and
# imports it in the app. The package holds the player's game: it is made on the player's machine, for the player
# alone; never commit it or upload it anywhere.
#
#   package.sh -r REPO [-a ABI] [-n NDK] [-d DXC_DIR] [-p PACK_DIR] [-o ZIP] [-j JOBS] [-s ASSETS]
#     REPO     the game repository, the player's disc in its assets/ (as for the desktop build)
#     ABI      arm64-v8a (phones, the default) or x86_64 (the emulator)
#     NDK      the Android NDK (default $ANDROID_NDK_HOME, else the newest under $ANDROID_HOME/ndk)
#     DXC_DIR  a folder with libdxcompiler.so for ABI (default: built by build_dxc.sh, once, about 20 minutes)
#     PACK_DIR a texture pack folder to carry (texture_pack/ beside the libraries)
#     ZIP      the package (default REPO/out/<Short>-android-package.zip, Short from launcher/game.json)
#
# Needs a host compiler (clang), CMake 3.25+, Ninja, git, python3 and zip. The libraries are built in
# REPO/out/build/android-ABI with xbe_recomp and fsr2_shaders from a host build in REPO/out/build/android-host.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=
ABI=arm64-v8a
NDK=${ANDROID_NDK_HOME:-}
DXC=
PACK=
ASSETS=
ZIP=
JOBS=$(nproc)
while getopts "r:a:n:d:p:o:j:s:" o; do
    case $o in
        s) ASSETS=$OPTARG ;;
        r) REPO=$OPTARG ;;
        a) ABI=$OPTARG ;;
        n) NDK=$OPTARG ;;
        d) DXC=$OPTARG ;;
        p) PACK=$OPTARG ;;
        o) ZIP=$OPTARG ;;
        j) JOBS=$OPTARG ;;
        *) exit 2 ;;
    esac
done
[ -n "$REPO" ] && [ -f "$REPO/CMakeLists.txt" ] || { echo "package: -r the game repository" >&2; exit 2; }
REPO=$(cd "$REPO" && pwd)
if [ -z "$NDK" ] && [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME/ndk" ]; then
    NDK=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)
fi
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "package: no NDK (-n, ANDROID_NDK_HOME or ANDROID_HOME)" >&2; exit 1; }
GAME_JSON=$REPO/launcher/game.json
read_json() { python3 -c "import json,sys; d=json.load(open(sys.argv[1])); v=d.get(sys.argv[2]); print(' '.join(v) if isinstance(v,list) else (v or ''))" "$GAME_JSON" "$1"; }
SHORT=$(read_json short)
TITLE=$(read_json title)
EXES=$(read_json exes)
[ -n "$EXES" ] || EXES=$(read_json exe)
ZIP=${ZIP:-$REPO/out/${SHORT// /}-android-package.zip}
[ -d "$REPO/assets" ] || { echo "package: no assets/ in $REPO (the disc's files or its .iso)" >&2; exit 1; }

ASSETS=${ASSETS:-$REPO/assets}
[ -d "$ASSETS" ] || { echo "package: no disc assets in $ASSETS" >&2; exit 1; }
ASSETS=$(cd "$ASSETS" && pwd)

# DXC for the ABI
echo "package: step 1 of 4: the DXC compiler (done once; it is kept for the next package)"
if [ -z "$DXC" ]; then
    bash "$HERE/build_dxc.sh" -n "$NDK" -a "$ABI"
    DXC=${HOME}/.cache/xbe-android/dxc/out/$ABI
fi
[ -f "$DXC/libdxcompiler.so" ] || { echo "package: no libdxcompiler.so in $DXC" >&2; exit 1; }

# the build-time tools for this machine
HOSTB=$REPO/out/build/android-host
mkdir -p "$REPO/out/build"
echo "package: step 2 of 4: the build tools for this PC"
cmake -S "$REPO" -B "$HOSTB" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CC:-clang} \
    -DCMAKE_CXX_COMPILER=${CXX:-clang++} -DXBE_DLSS=OFF > "$HOSTB.cfg.log" 2>&1 || { tail -20 "$HOSTB.cfg.log"; exit 1; }
cmake --build "$HOSTB" -j "$JOBS" --target xbe_recomp fsr2_shaders 2>&1 | tee "$HOSTB.build.log" || { tail -30 "$HOSTB.build.log"; exit 1; }

# the game for Android
echo "package: step 3 of 4: the game for $ABI (the longest step: a few minutes to over an hour on a slow PC)"
B=$REPO/out/build/android-$ABI
cmake -S "$REPO" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static -DXBE_HOST_TOOLS="$HOSTB" -DXBE_ANDROID_DXC="$DXC" \
    -DXBE_DLSS=OFF > "$B.cfg.log" 2>&1 || { tail -30 "$B.cfg.log"; exit 1; }
cmake --build "$B" -j "$JOBS" --target $EXES 2>&1 | tee "$B.build.log" || { grep -m 20 -B2 -A8 "error" "$B.build.log" | head -80; exit 1; }

# the package: libraries, DXC, the disc, a manifest the app checks
STAGE=$REPO/out/android-package-$ABI
rm -rf "$STAGE"
mkdir -p "$STAGE"
STRIP=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
LIBS=()
for e in $EXES; do
    "$STRIP" --strip-unneeded -o "$STAGE/lib$e.so" "$B/lib$e.so"
    LIBS+=("\"lib$e.so\"")
done
cp "$DXC/libdxcompiler.so" "$STAGE/"
ln -s "$ASSETS" "$STAGE/assets"
[ -n "$PACK" ] && [ -d "$PACK" ] && ln -s "$(cd "$PACK" && pwd)" "$STAGE/texture_pack"
SDL_TAG=$(grep -o 'GIT_TAG release-[0-9.]*' "$HERE/../../cmake/XbeRecomp.cmake" | head -1 | sed 's/GIT_TAG release-//')
SDK_REV=$(git -C "$HERE/../.." rev-parse --short HEAD 2>/dev/null || echo unknown)
GAME_REV=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo unknown)
cat > "$STAGE/package.json" <<JSON
{
    "format": 1,
    "title": "$TITLE",
    "short": "$SHORT",
    "abi": "$ABI",
    "sdl": "$SDL_TAG",
    "main": "lib${EXES%% *}.so",
    "libraries": [$(IFS=,; echo "${LIBS[*]}")],
    "game_revision": "$GAME_REV",
    "sdk_revision": "$SDK_REV",
    "built": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
JSON
mkdir -p "$(dirname "$ZIP")"
rm -f "$ZIP"
# the libraries and manifest compressed, the disc stored (it barely compresses and the phone unpacks it faster)
echo "package: step 4 of 4: packing the zip (the disc is about 2 GB and is stored, not compressed: a few minutes)"
(cd "$STAGE" && zip -q -9 "$ZIP" package.json lib*.so && zip -q -0 -r "$ZIP" assets $( [ -e texture_pack ] && echo texture_pack ))
rm -rf "$STAGE"
ls -la "$ZIP"
echo "package: $ZIP (the player's own game: keep it to yourself)"
