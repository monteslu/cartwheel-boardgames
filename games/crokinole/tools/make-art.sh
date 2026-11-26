#!/usr/bin/env bash
# crokinole's launcher art (games/crokinole/art/) and the README's screenshot, from the game's own frames: the frames
# come from the real cart through romdev (tests/crokinole-art.mjs: CPU matches stepped to good moments, with the HUD
# for the labelled screenshots and without it for the key art), then ImageMagick crops and composites them. The logo
# is the name in Fira Sans Bold (the engine's UI font family, SIL Open Font Licence), maple cream inside a walnut rim.
#   games/crokinole/tools/make-art.sh [--shots-only | --compose-only]   (the frames and the compositing can run apart)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art"; ART="$ROOT/games/crokinole/art"
FONT="$ROOT/deps/cartwheel-engine/samples/ui_assets/fonts/FiraSans-Bold.ttf"
mkdir -p "$OUT" "$ART" "$ROOT/docs/screenshots"
if [ "${1:-}" != --compose-only ]; then
  (cd "$ROOT" && node tests/crokinole-art.mjs gl)
fi
[ "${1:-}" = --shots-only ] && exit 0
for f in play-1 play-2 play-3 play-4 key-1 key-2; do [ -f "$OUT/$f.png" ] || { echo "make-art: no $OUT/$f.png (run without --compose-only)" >&2; exit 1; }; done
# the logo: the name, thick, on transparency (it must read at a launcher grid's size over any picture)
convert -size 2600x700 xc:none -font "$FONT" -pointsize 400 -gravity center \
  -fill "#0d0603" -stroke "#0d0603" -strokewidth 34 -annotate +10+14 CROKINOLE \
  -fill "#3b1f0c" -stroke "#3b1f0c" -strokewidth 46 -annotate +0+0 CROKINOLE \
  -fill "#8a5a2b" -stroke "#8a5a2b" -strokewidth 18 -annotate +0+0 CROKINOLE \
  -fill "#f7e6c4" -stroke "#f7e6c4" -strokewidth 4 -annotate +0+0 CROKINOLE \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
# the screenshots, with the HUD
for i in 1 2 3 4; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$i.jpg"; done
# the cover: the board from the clean four-seat frame, upright 2:3, the name across the top
convert "$OUT/key-2.png" -resize 1920x1080! -crop 720x1080+600+0 +repage -resize 800x1200! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -18x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-1.png" -resize 1920x1080! -brightness-contrast -28x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: the hole, the pegs and the discs round them, close
convert "$OUT/key-1.png" -resize 1920x1080! -gravity center -crop 560x560+0-20 +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/play-2.png" -resize 1600x900! -quality 88 "$ROOT/docs/screenshots/crokinole.jpg" # four seats mid-round, discs of both teams about
ls -la "$ART"
