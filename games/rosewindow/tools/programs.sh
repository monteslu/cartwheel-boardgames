#!/bin/sh
# Rose Window's program table (programs/rosewindow.txt): survey both apps natively and as carts, with the newcomer test
# player on screen (a person's ghost previews and highlights) and without. Run under the GPU lock after a build.
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
export CW_PROJECT_ROOT="$ROOT"
T="$ROOT/deps/cartwheel-sdk/linux-x64/tools/programs.mjs"
node "$T" rosewindow rosewindow_auto --build "$ROOT/build/native" --frames 1500 --scenes ROSEWINDOW_NEWCOMER=1
node "$T" --carts rosewindow rosewindow_auto --build "$ROOT/build/native" --frames 1500 --scenes ROSEWINDOW_NEWCOMER=1
