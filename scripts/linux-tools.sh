#!/usr/bin/env bash
# Configure for Linux and build the recompiler tools (into build/linux). SDL3 is configured without X11/Wayland so it
# works on a headless VM; the game's own Linux exes build with its CMakePresets instead (cmake --preset release, X11
# and Wayland on: the development packages in the game's BUILDING.md).
set -euo pipefail
. "$(dirname "$0")/common.sh"
[ -f "assets/$GAME_XBE" ] || { echo "assets/$GAME_XBE missing: extract the game first" >&2; exit 1; }
LAUNCH=(); command -v ccache >/dev/null && LAUNCH=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "${LAUNCH[@]}" \
  -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF
cmake --build build/linux --target xbe_symbols xbe_recomp
