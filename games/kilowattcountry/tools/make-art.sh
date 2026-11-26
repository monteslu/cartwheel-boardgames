#!/usr/bin/env bash
# Kilowatt Country's launcher art (art/) and the README's screenshot, from the game's own frames: the self-playing
# cart (four computer seats) on several maps and the setup card's map picker through romdev (scripts/romdev-shot.mjs, under the GPU lock), then
# cropped and composed with ImageMagick. The logo is the name in Oswald (tools/fonts, SIL OFL; not in the cart).
#   games/kilowattcountry/tools/make-art.sh [--shots-only | --compose-only]
# Needs the carts built (scripts/build.sh kilowattcountry kilowattcountry_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/kilowattcountry"; ART="$ROOT/games/kilowattcountry/art"; FONT="$ROOT/games/kilowattcountry/tools/fonts/Oswald-VF.ttf"
AUTO="$ROOT/build/cart-wgpu/carts/kilowattcountry_auto.wasc"; GAME="$ROOT/build/cart-wgpu/carts/kilowattcountry.wasc"
mkdir -p "$OUT" "$ART" "$ROOT/docs/screenshots"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$@"; }
  s "$GAME" ${F0:-150} "$OUT/play-0.png" KILOWATTCOUNTRY_SEATS=p,c1,c2 KILOWATTCOUNTRY_SEED=5 # the setup card
  s "$AUTO" ${F1:-1800} "$OUT/play-1.png" KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_MAP=usa
  s "$AUTO" ${F2:-3200} "$OUT/play-2.png" KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_MAP=central
  s "$AUTO" ${F3:-4200} "$OUT/play-3.png" KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_MAP=france
  s "$AUTO" ${F4:-3600} "$OUT/play-4.png" KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_MAP=lakes
  s "$AUTO" ${FK:-3800} "$OUT/key-0.png" --size 3840x2160 KILOWATTCOUNTRY_SEED=5 KILOWATTCOUNTRY_MAP=usa BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" kilowattcountry-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name in Oswald, warm lamplight yellow with a dark navy edge and an amber rim, on transparency
convert -size 3800x760 xc:none -font "$FONT" -pointsize 470 -gravity center -kerning 10 \
  -fill "#04070f" -stroke "#04070f" -strokewidth 34 -annotate +10+16 "KILOWATT COUNTRY" \
  -fill "#0e1a33" -stroke "#0e1a33" -strokewidth 44 -annotate +0+0 "KILOWATT COUNTRY" \
  -fill "#e08a20" -stroke "#e08a20" -strokewidth 14 -annotate +0+0 "KILOWATT COUNTRY" \
  -fill "#ffe6a0" -stroke "#ffe6a0" -strokewidth 3 -annotate +0+0 "KILOWATT COUNTRY" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3 4; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait of the lit map, the name across the top
convert "$OUT/key-0.png" -crop ${COVER_W:-860}x${COVER_H:-1290}+${COVER_X:-1490}+${COVER_Y:-330} +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -15x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -8x-5 -fill "rgba(0,0,0,0.45)" -draw "rectangle 0,400 1920,680" \
  \( "$ART/logo.png" -resize 1500x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a cluster of lit cities, close
convert "$OUT/key-0.png" -crop ${ICON_S:-560}x${ICON_S:-560}+${ICON_X:-1900}+${ICON_Y:-800} +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/play-${README_SHOT:-2}.png" -resize 1600x900 -quality 88 "$ROOT/docs/screenshots/kilowattcountry.jpg"
ls -la "$ART"
