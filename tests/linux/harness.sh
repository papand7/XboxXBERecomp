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
bash "$SDK/harness/build.sh" -r "$REPO" -g "$REPO/generated/game" -o "$OUT/build" -n harness_check
