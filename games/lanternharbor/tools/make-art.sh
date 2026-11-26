#!/usr/bin/env bash
# Lantern Harbor's launcher art (art/), from the game's own frames:
# the self-playing cart (four computer seats) through romdev (scripts/romdev-shot.mjs, under the GPU lock), then
# cropped and composed with ImageMagick. The logo is the name in Cinzel (tools/fonts, SIL OFL; not in the cart).
#   games/lanternharbor/tools/make-art.sh [--shots-only | --compose-only]
# Needs the cart built (scripts/build.sh lanternharbor_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/lanternharbor"; ART="$ROOT/games/lanternharbor/art"; FONT="$ROOT/games/lanternharbor/tools/fonts/Cinzel.ttf"
CART="$ROOT/build/cart-wgpu/carts/lanternharbor_auto.wasc"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$@"; }
  s ${SHOT0:-1200} "$OUT/play-0.png" LANTERNHARBOR_SEED=1
  s ${SHOT1:-2100} "$OUT/play-1.png" LANTERNHARBOR_SEED=1
  s ${SHOT2:-2900} "$OUT/play-2.png" LANTERNHARBOR_SEED=1
  s ${SHOT3:-3700} "$OUT/play-3.png" LANTERNHARBOR_SEED=1
  s ${KEY:-3600} "$OUT/key-0.png" --size 3840x2160 LANTERNHARBOR_SEED=1 BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" lanternharbor-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Cinzel, lantern amber on a deep harbour-blue edge with a soft shadow, on transparency
convert -size 3600x900 xc:none -font "$FONT" -pointsize 360 -gravity center -interline-spacing -40 \
  -fill "#070a14" -stroke "#070a14" -strokewidth 34 -annotate +10+16 "LANTERN\nHARBOR" \
  -fill "#16243f" -stroke "#16243f" -strokewidth 44 -annotate +0+0 "LANTERN\nHARBOR" \
  -fill "#c8702a" -stroke "#c8702a" -strokewidth 16 -annotate +0+0 "LANTERN\nHARBOR" \
  -fill "#ffd58a" -stroke "#ffd58a" -strokewidth 4 -annotate +0+0 "LANTERN\nHARBOR" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
# the README screenshot: round twelve, four seats mid-game
mkdir -p "$ROOT/docs/screenshots"
convert "$OUT/play-2.png" -resize 1920x1080! -quality 88 "$ROOT/docs/screenshots/lanternharbor.jpg"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait of the harbour, the boats and lanterns over a town board, the name across the top
convert "$OUT/key-0.png" -crop 1280x1920+${COVER_X:-2560}+${COVER_Y:-0} +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
# the background: the harbour, darkened; the title: the same with the name over a band
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,330 1920,750" \
  \( "$ART/logo.png" -resize 900x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a close square of a boat under the lit lanterns
convert "$OUT/key-0.png" -crop 620x620+${ICON_X:-1600}+${ICON_Y:-160} +repage -resize 512x512! "$ART/icon.png"
ls -la "$ART"
