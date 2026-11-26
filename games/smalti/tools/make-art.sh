#!/usr/bin/env bash
# Smalti's launcher art (art/), from the game's own frames:
# the self-playing cart (four computer seats) through romdev (scripts/romdev-shot.mjs, under the GPU lock), then
# cropped and composed with ImageMagick. The logo is the name in Cinzel (tools/fonts, SIL OFL; not in the cart).
#   games/smalti/tools/make-art.sh [--shots-only | --compose-only]
# Needs the cart built (scripts/build.sh smalti_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/smalti"; ART="$ROOT/games/smalti/art"; FONT="$ROOT/games/smalti/tools/fonts/Cinzel.ttf"
CART="$ROOT/build/cart-wgpu/carts/smalti_auto.wasc"
mkdir -p "$OUT" "$ART"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$CART" "$@"; }
  s 1500 "$OUT/play-0.png" SMALTI_SEED=3
  s 2700 "$OUT/play-1.png" SMALTI_SEED=3
  s 4200 "$OUT/play-2.png" SMALTI_SEED=3
  s 5600 "$OUT/play-3.png" SMALTI_SEED=3
  s 2700 "$OUT/key-0.png" --size 3840x2160 SMALTI_SEED=3 BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" smalti-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Cinzel, cream glass on a deep blue edge with a soft shadow, on transparency (1280+ wide)
convert -size 2600x760 xc:none -font "$FONT" -pointsize 440 -gravity center \
  -fill "#0b1220" -stroke "#0b1220" -strokewidth 34 -annotate +10+16 SMALTI \
  -fill "#14305e" -stroke "#14305e" -strokewidth 44 -annotate +0+0 SMALTI \
  -fill "#c8a24a" -stroke "#c8a24a" -strokewidth 16 -annotate +0+0 SMALTI \
  -fill "#f7efdc" -stroke "#f7efdc" -strokewidth 4 -annotate +0+0 SMALTI \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover (from the HUD-free frame): two mosaics of set glass, the name across the top
convert "$OUT/key-0.png" -crop 960x1440+${COVER_X:-2660}+${COVER_Y:-330} +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
# the background: the table, darkened; the title: the same with the name over a band
convert "$OUT/key-0.png" -crop 3840x2160+0+0 +repage -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a close square of glass set into a mosaic
convert "$OUT/key-0.png" -crop 520x520+${ICON_X:-3020}+${ICON_Y:-350} +repage -resize 512x512! "$ART/icon.png"
# the README screenshot: a mid-game frame with its HUD
mkdir -p "$ROOT/docs/screenshots"
convert "$OUT/play-1.png" -resize 1920x1080! -quality 88 "$ROOT/docs/screenshots/smalti.jpg"
ls -la "$ART"
