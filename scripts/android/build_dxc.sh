#!/usr/bin/env bash
# DXC (the HLSL compiler the Vulkan renderer makes its SPIR-V with) built for Android, as Microsoft publishes no Android
# release: the same tag as the desktop builds' (v1.8.2407), its tablegens built for this host first, then
# libdxcompiler.so cross-compiled with the NDK. About 20 minutes on 12 threads; the result is kept and reused.
#
#   build_dxc.sh [-n NDK] [-a ABI] [-w WORK] [-o OUT]
#     NDK   the Android NDK (default $ANDROID_NDK_HOME, else the newest under $ANDROID_HOME/ndk)
#     ABI   arm64-v8a (phones) or x86_64 (the emulator); default arm64-v8a
#     WORK  where the source and the builds go (default ~/.cache/xbe-android/dxc)
#     OUT   where libdxcompiler.so goes (default WORK/out/ABI)
set -euo pipefail
TAG=v1.8.2407
ABI=arm64-v8a
WORK=${HOME}/.cache/xbe-android/dxc
NDK=${ANDROID_NDK_HOME:-}
OUT=
while getopts "n:a:w:o:" o; do
    case $o in
        n) NDK=$OPTARG ;;
        a) ABI=$OPTARG ;;
        w) WORK=$OPTARG ;;
        o) OUT=$OPTARG ;;
        *) exit 2 ;;
    esac
done
if [ -z "$NDK" ] && [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME/ndk" ]; then
    NDK=$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)
fi
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "build_dxc: no NDK (-n, ANDROID_NDK_HOME or ANDROID_HOME)" >&2; exit 1; }
OUT=${OUT:-$WORK/out/$ABI}
JOBS=$(nproc)
mkdir -p "$WORK" "$OUT"
if [ -f "$OUT/libdxcompiler.so" ] && [ "$(cat "$OUT/tag" 2>/dev/null)" = "$TAG" ]; then
    echo "build_dxc: $OUT/libdxcompiler.so ($TAG) already built"
    exit 0
fi
SRC=$WORK/src-$TAG
echo "build_dxc: the DXC compiler for Android (about 20 minutes once; the build's progress follows)"
if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "build_dxc: downloading the DXC source ($TAG)"
    git clone -q --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
        https://github.com/microsoft/DirectXShaderCompiler.git "$SRC"
fi
COMMON=(-G Ninja -C "$SRC/cmake/caches/PredefinedParams.cmake" -DCMAKE_BUILD_TYPE=Release -DHLSL_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF -DLLVM_ENABLE_TERMINFO=OFF
        -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF)
HOST=$WORK/host
if [ ! -x "$HOST/bin/llvm-tblgen" ] || [ ! -x "$HOST/bin/clang-tblgen" ]; then
    cmake -S "$SRC" -B "$HOST" "${COMMON[@]}" -DCMAKE_C_COMPILER=${CC:-clang} -DCMAKE_CXX_COMPILER=${CXX:-clang++} > "$WORK/host.cfg.log"
    echo "build_dxc: building the build tools for this PC"
    ninja -C "$HOST" -j "$JOBS" llvm-tblgen clang-tblgen 2>&1 | tee "$WORK/host.build.log"
fi
BUILD=$WORK/build-$ABI
cmake -S "$SRC" -B "$BUILD" "${COMMON[@]}" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
    -DLLVM_TABLEGEN="$HOST/bin/llvm-tblgen" -DCLANG_TABLEGEN="$HOST/bin/clang-tblgen" \
    -DLLVM_TARGET_ARCH="${ABI%%-*}" -DLLVM_DEFAULT_TARGET_TRIPLE=dxil-ms-dx > "$WORK/$ABI.cfg.log"
echo "build_dxc: compiling the DXC compiler for $ABI (the longest step: a single file can take several minutes)"
ninja -C "$BUILD" -j "$JOBS" dxcompiler 2>&1 | tee "$WORK/$ABI.build.log"
"$NDK"/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-unneeded -o "$OUT/libdxcompiler.so" "$BUILD/lib/libdxcompiler.so"
echo "$TAG" > "$OUT/tag"
ls -la "$OUT/libdxcompiler.so"
