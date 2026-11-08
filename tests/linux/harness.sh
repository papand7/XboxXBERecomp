#!/bin/bash
# harness.sh: builds the headless harness (harness/build.sh) against the stub image and the game template's sources,
# no game needed: a compile and link check for the Linux CI.   tests/linux/harness.sh <out folder>
set -e
SDK=$(realpath "$(dirname "$0")/../..")
OUT=${1:?out folder}; mkdir -p "$OUT"; OUT=$(realpath "$OUT")
REPO=$OUT/repo
mkdir -p "$REPO/generated"
ln -sfn "$SDK" "$REPO/sdk"
ln -sfn "$SDK/templates/game/src" "$REPO/src"
ln -sfn "$SDK/tests/linux/stub/generated/game" "$REPO/generated/game"
# toml++ (the version cmake/XbeRecomp.cmake fetches), header only
TOML=$OUT/deps/tomlplusplus-3.4.0
if [ ! -f "$TOML/include/toml++/toml.hpp" ]; then
    mkdir -p "$OUT/deps"
    curl -sSL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz | tar xz -C "$OUT/deps"
fi
export CXXFLAGS="${CXXFLAGS:-} -I$TOML/include"
bash "$SDK/harness/build.sh" -r "$REPO" -g "$REPO/generated/game" -o "$OUT/build" -n harness_check
