#!/usr/bin/env bash
# Mandible's launcher art (art/), from the game's own
# frames: the self-playing cart (two computer seats) through romdev (scripts/romdev-shot.mjs, under the GPU lock),
# then cropped and composed with ImageMagick. The logo is the name in Oswald (tools/fonts, SIL OFL; not in the cart).
#   games/mandible/tools/make-art.sh [--shots-only | --compose-only]
# Needs the cart built (scripts/build.sh mandible_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/mandible"; ART="$ROOT/games/mandible/art"; FONT="$ROOT/games/mandible/tools/fonts/Oswald-VF.ttf"
CART="$ROOT/build/cart-wgpu/carts/mandible_auto.wasc"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$@"; }
  s 600 "$OUT/play-0.png"
  s 1500 "$OUT/play-1.png"
  s 2600 "$OUT/play-2.png"
  s 4000 "$OUT/play-3.png"
  s 1500 "$OUT/key-0.png" --size 3840x2160 BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" mandible-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Oswald, ivory with a dark walnut edge and a gold rim, on transparency (1280+ wide)
convert -size 2800x760 xc:none -font "$FONT" -pointsize 520 -gravity center -kerning 18 \
  -fill "#0d0805" -stroke "#0d0805" -strokewidth 34 -annotate +10+16 MANDIBLE \
  -fill "#3a2414" -stroke "#3a2414" -strokewidth 44 -annotate +0+0 MANDIBLE \
  -fill "#d6a93e" -stroke "#d6a93e" -strokewidth 14 -annotate +0+0 MANDIBLE \
  -fill "#f3ead8" -stroke "#f3ead8" -strokewidth 3 -annotate +0+0 MANDIBLE \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait of the group of pieces below the score panels, the name across the top
convert "$OUT/key-0.png" -crop 1180x1770+${COVER_X:-1560}+380 +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-0.png" -crop 3060x1720+390+400 +repage -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -crop 3060x1720+390+400 +repage -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" \
  -draw "rectangle 0,380 1920,700" \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: one carved piece, close
convert "$OUT/key-0.png" -crop 440x440+${ICON_X:-2268}+${ICON_Y:-1420} +repage -resize 512x512! "$ART/icon.png"
# the README screenshot: a mid-game frame with its HUD
mkdir -p "$ROOT/docs/screenshots"
convert "$OUT/play-2.png" -resize 1920x1080! -quality 88 "$ROOT/docs/screenshots/mandible.jpg"
ls -la "$ART"
