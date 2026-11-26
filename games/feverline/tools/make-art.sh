#!/usr/bin/env bash
# Fever Line's launcher art (art/) and the README's screenshot, from the game's own frames: the dual cart through
# romdev (scripts/romdev-shot.mjs for the self-playing team, tools/run-cart.mjs --host romdev for a person's turn,
# under the GPU lock), then cropped and composed with ImageMagick. The logo is the name in Fira Sans Bold (the
# engine's font, OFL), a cool glow on a dark rim.
#   games/feverline/tools/make-art.sh [--shots-only | --compose-only]
# Needs the carts built (scripts/build.sh feverline feverline_auto) and the romdev server on 127.0.0.1:7331.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/feverline"; ART="$ROOT/games/feverline/art"
FONT="$ROOT/deps/cartwheel-engine/samples/ui_assets/fonts/FiraSans-Bold.ttf"
AUTO="$ROOT/build/cart-wgpu/carts/feverline_auto.wasc"
mkdir -p "$OUT" "$ART" "$ROOT/docs/screenshots"
if [ "${1:-}" = --shots-inner ]; then # (inside the GPU lock)
  s() { node "$ROOT/scripts/romdev-shot.mjs" "$AUTO" "$@"; }
  # a person's turn: the card closed, the cursor on a city, its ghost pawn and the line that says what it does
  printf '%s' '[{"frame":200,"buttons":["south"]},{"frame":203,"buttons":[]},{"frame":240,"buttons":["right"]},{"frame":243,"buttons":[]}]' > "$OUT/turn.json"
  CW_PROJECT_ROOT="$ROOT" node "$ROOT/deps/cartwheel-engine/tools/run-cart.mjs" --host romdev --app feverline --frames 300 \
    --shot "$OUT/play-0.png" --env '{"FEVERLINE_SEATS":"p:c1:c1","FEVERLINE_SETUP":"1","FEVERLINE_SEED":"21"}' --gpu webgpu --input "$OUT/turn.json" > /dev/null
  s "${SHOT1:-2300}" "$OUT/play-1.png"
  s "${SHOT2:-3300}" "$OUT/play-2.png"
  s "${SHOT3:-1500}" "$OUT/play-3.png"
  s "${KEY:-2400}" "$OUT/key-0.png" --size 3840x2160 BK_NO_HUD=1 # (no HUD: cover, background, title, icon)
  for f in play-0 play-1 play-2 play-3 key-0; do [ -s "$OUT/$f.png" ] || { echo "make-art: no frame $f" >&2; exit 1; }; done
  exit 0
fi
if [ "${1:-}" != --compose-only ]; then
  node "$ROOT/deps/cartwheel-engine/tools/with-gpu-suite.mjs" feverline-art bash "${BASH_SOURCE[0]}" --shots-inner
fi
[ "${1:-}" = --shots-only ] && exit 0
# the logo: the name, thick, pale ice blue over a teal glow with a dark rim, on transparency (1280 wide or more)
convert -size 3000x720 xc:none -font "$FONT" -pointsize 400 -gravity center \
  -fill "#03070c" -stroke "#03070c" -strokewidth 34 -annotate +12+16 "Fever Line" \
  -fill "#0b2a33" -stroke "#0b2a33" -strokewidth 46 -annotate +0+0 "Fever Line" \
  -fill "#2ab8c8" -stroke "#2ab8c8" -strokewidth 16 -annotate +0+0 "Fever Line" \
  -fill "#e8fbff" -stroke "#e8fbff" -strokewidth 4 -annotate +0+0 "Fever Line" \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait slice of the map at night, the name across the top
convert "$OUT/key-0.png" -crop "${COVER_W:-900}x1350+${COVER_X:-1500}+${COVER_Y:-290}" +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$ROOT/scripts/cover-name.sh" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.55)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1400x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: a city with its glowing cubes, close
convert "$OUT/key-0.png" -crop "${ICON_S:-560}x${ICON_S:-560}+${ICON_X:-2150}+${ICON_Y:-560}" +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/play-${README_SHOT:-1}.png" -resize 1600x900 -quality 88 "$ROOT/docs/screenshots/feverline.jpg"
ls -la "$ART"
