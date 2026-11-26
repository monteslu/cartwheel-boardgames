#!/usr/bin/env bash
# First Hearth's launcher art (games/firsthearth/art/) and the README's screenshot, from the game's own frames: the dual
# cart run through romdev (tools/run-cart.mjs --host romdev, deterministic), then cropped and composited with
# ImageMagick. The logo is the name in Fira Sans Bold (the engine's font, OFL), warm ochre in a dark rim.
#   games/firsthearth/tools/make-cart-art.sh [--shots-only | --compose-only]
# Needs the carts built (scripts/build.sh) and the romdev server running.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/firsthearth"; ART="$ROOT/games/firsthearth/art"
FONT="$ROOT/deps/cartwheel-engine/samples/ui_assets/fonts/FiraSans-Bold.ttf"
mkdir -p "$OUT" "$ART" "$ROOT/docs/screenshots"
shot() { # app frames png env-json [input]
  local extra=()
  [ -n "${5:-}" ] && extra=(--input "$5")
  CW_PROJECT_ROOT="$ROOT" node "$ROOT/deps/cartwheel-engine/tools/run-cart.mjs" --host romdev --app "$1" --frames "$2" \
    --shot "$3" --env "$4" --gpu webgpu "${extra[@]}" > /dev/null
  [ -s "$3" ] || { echo "make-cart-art: no frame $3" >&2; exit 1; }
}
if [ "${1:-}" != --compose-only ]; then
  # a person's turn: the cursor on a place, its ghost kin and the line that says what it gives
  printf '%s' '[{"frame":30,"buttons":["south"]},{"frame":33,"buttons":[]},{"frame":200,"buttons":["right"]},{"frame":203,"buttons":[]}]' > "$OUT/turn.json"
  shot firsthearth 215 "$OUT/play-0.png" '{"FIRSTHEARTH_SEATS":"p,c1,c2","FIRSTHEARTH_SEED":"5"}' "$OUT/turn.json"
  shot firsthearth_auto 1500 "$OUT/play-1.png" '{}'
  shot firsthearth_auto 2700 "$OUT/play-2.png" '{}'
  shot firsthearth_auto 4300 "$OUT/play-3.png" '{}'
  shot firsthearth_auto 2700 "$OUT/key-0.png" '{"FIRSTHEARTH_NO_HUD":"1"}'
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name, thick, ochre on a dark rim with a drop shadow, on transparency (1280 wide or more)
convert -size 3000x720 xc:none -font "$FONT" -pointsize 380 -gravity center \
  -fill "#120a04" -stroke "#120a04" -strokewidth 34 -annotate +12+16 "First Hearth" \
  -fill "#2c1608" -stroke "#2c1608" -strokewidth 46 -annotate +0+0 "First Hearth" \
  -fill "#b85a26" -stroke "#b85a26" -strokewidth 18 -annotate +0+0 "First Hearth" \
  -fill "#f4d9a0" -stroke "#f4d9a0" -strokewidth 4 -annotate +0+0 "First Hearth" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: the village board seen from the table, portrait
convert "$OUT/key-0.png" -crop 760x1080+580+0 +repage -resize 1200x1705! -gravity south -background "#2a1a10" -extent 1200x1800 \
  -quality 92 "$ART/cover.jpg"
"${COVER_NAME:-$ROOT/scripts/cover-name.sh}" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: the hearth, close
convert "$OUT/key-0.png" -crop 300x300+710+255 +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/play-2.png" -resize 1600x900 -quality 88 "$ROOT/docs/screenshots/firsthearth.jpg"
ls -la "$ART"
