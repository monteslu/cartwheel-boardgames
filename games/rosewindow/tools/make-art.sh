#!/usr/bin/env bash
# Rose Window's launcher art (art/), from the game's own frames:
# the self-playing cart (four computer seats) through romdev (scripts/romdev-shot.mjs, under the GPU lock), then
# cropped and composed with ImageMagick. The logo is the name in Uncial Antiqua (tools/fonts, SIL OFL; not in the cart).
#   games/rosewindow/tools/make-art.sh [--shots-only | --compose-only]
# Needs the cart built (scripts/build.sh rosewindow_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/rosewindow"; ART="$ROOT/games/rosewindow/art"; FONT="$ROOT/games/rosewindow/tools/fonts/UncialAntiqua-Regular.ttf"
CART="$ROOT/build/cart-wgpu/carts/rosewindow_auto.wasc"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$@"; }
  s ${SHOT0:-900} "$OUT/play-0.png" ROSEWINDOW_SEED=3
  s ${SHOT1:-2000} "$OUT/play-1.png" ROSEWINDOW_SEED=3
  s ${SHOT2:-3000} "$OUT/play-2.png" ROSEWINDOW_SEED=3
  s ${SHOT3:-4000} "$OUT/play-3.png" ROSEWINDOW_SEED=3
  s ${KEY:-3600} "$OUT/key-0.png" --size 3840x2160 ROSEWINDOW_SEED=3 BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" rosewindow-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in an uncial hand, warm amber glass edged in lead, on transparency (1280+ wide)
convert -size 3000x760 xc:none -font "$FONT" -pointsize 400 -gravity center \
  -fill "#120a06" -stroke "#120a06" -strokewidth 34 -annotate +10+16 "Rose Window" \
  -fill "#2b2b30" -stroke "#2b2b30" -strokewidth 40 -annotate +0+0 "Rose Window" \
  -fill "#c22a3c" -stroke "#c22a3c" -strokewidth 14 -annotate +0+0 "Rose Window" \
  -fill "#ffe7b0" -stroke "#ffe7b0" -strokewidth 3 -annotate +0+0 "Rose Window" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
# the README screenshot: the third labelled shot, mid-game
mkdir -p "$ROOT/docs/screenshots"
convert "$OUT/play-2.png" -resize 1920x1080! -quality 88 "$ROOT/docs/screenshots/rosewindow.jpg"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait of two lit windows, the name across the top
convert "$OUT/key-0.png" -crop 960x1440+${COVER_X:-1870}+${COVER_Y:-560} +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
# the background: the table, darkened; the title: the same with the name over a band
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a close square of a lit window
convert "$OUT/key-0.png" -crop 700x700+${ICON_X:-1975}+${ICON_Y:-1050} +repage -resize 512x512! "$ART/icon.png"
ls -la "$ART"
