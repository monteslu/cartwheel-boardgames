#!/usr/bin/env bash
# Smokestack's launcher art (art/), from the game's own frames: the self-playing cart (four computer seats on the
# large valley) through romdev (scripts/romdev-shot.mjs, under the GPU lock), then cropped and composed with
# ImageMagick. The logo is the name in Bungee (tools/fonts, SIL OFL; not in the cart).
#   games/smokestack/tools/make-art.sh [--shots-only | --compose-only]
# Needs the cart built (scripts/build.sh smokestack_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/smokestack"; ART="$ROOT/games/smokestack/art"; FONT="$ROOT/games/smokestack/tools/fonts/Bungee-Regular.ttf"
CART="$ROOT/build/cart-wgpu/carts/smokestack_auto.wasc"
SEATS=SMOKESTACK_SEATS=c1:c2:c0:c1
mkdir -p "$OUT" "$ART"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$@"; }
  s ${F0:-350} "$OUT/play-0.png" $SEATS SMOKESTACK_SEED=5
  s ${F1:-650} "$OUT/play-1.png" $SEATS SMOKESTACK_SEED=5
  s ${F2:-1150} "$OUT/play-2.png" $SEATS SMOKESTACK_SEED=5
  s ${F3:-700} "$OUT/play-3.png" SMOKESTACK_SEATS=c2:c1 SMOKESTACK_SEED=9
  s ${FK:-950} "$OUT/key-0.png" --size 3840x2160 $SEATS SMOKESTACK_SEED=5 SMOKESTACK_CLEAN=1
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" smokestack-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Bungee, warm brass on a soot-black edge with a soft shadow, on transparency
convert -size 3000x700 xc:none -font "$FONT" -pointsize 380 -gravity center \
  -fill "#120c08" -stroke "#120c08" -strokewidth 40 -annotate +12+18 SMOKESTACK \
  -fill "#2a1a10" -stroke "#2a1a10" -strokewidth 40 -annotate +0+0 SMOKESTACK \
  -fill "#c98a3a" -stroke "#c98a3a" -strokewidth 14 -annotate +0+0 SMOKESTACK \
  -fill "#f6e2b4" -stroke "#f6e2b4" -strokewidth 2 -annotate +0+0 SMOKESTACK \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait of the valley's middle, track between the towns, the name across the top
convert "$OUT/key-0.png" -crop 920x1380+${COVER_X:-1440}+${COVER_Y:-250} +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
# the background: the table, darkened; the title: the same with the name over a band
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,390 1920,690" \
  \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a close square of a city with its track
convert "$OUT/key-0.png" -crop 560x560+${ICON_X:-2190}+${ICON_Y:-800} +repage -resize 512x512! "$ART/icon.png"
# the README screenshot: a mid-game frame with its HUD
mkdir -p "$ROOT/docs/screenshots"
convert "$OUT/play-1.png" -resize 1920x1080! -quality 88 "$ROOT/docs/screenshots/smokestack.jpg"
ls -la "$ART"
