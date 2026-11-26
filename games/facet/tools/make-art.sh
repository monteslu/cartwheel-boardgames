#!/usr/bin/env bash
# Facet's launcher art (games/facet/art) and the README's screenshot, from the cart's own frames through romdev
# (tools/shot.mjs), cropped and composited with ImageMagick. The logo is the name set in the game's Cinzel, in gold.
#   games/facet/tools/make-art.sh [--compose-only]     (build the carts first: scripts/build.sh facet facet_auto)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/out/art/facet"; ART="$ROOT/games/facet/art"; FONT="$ROOT/games/facet/assets/fonts/Cinzel.ttf"
CARTS="$ROOT/build/cart-wgpu/carts"; SHOT="node $ROOT/games/facet/tools/shot.mjs"
COVER_NAME="${COVER_NAME:-$ROOT/scripts/cover-name.sh}"
mkdir -p "$OUT" "$ART" "$ROOT/docs/screenshots"
idle() { echo "[{\"input\":{\"ports\":[{}]},\"frames\":$1}]"; }
if [ "${1:-}" != --compose-only ]; then
  # three guilds mid-game, the table with its HUD
  $SHOT "$CARTS/facet_auto.wasc" "$OUT/play-0.png" --steps "$(idle 3000)"
  # a person's turn: a card chosen, Set aside lit on its plaque, the preview over the mat
  $SHOT "$CARTS/facet.wasc" "$OUT/play-1.png" FACET_SEATS=p:c0 --steps '[{"input":{"ports":[{}]},"frames":120},{"input":{"ports":[{"south":true}]},"frames":3},{"input":{"ports":[{}]},"frames":20},{"input":{"ports":[{"right":true}]},"frames":2},{"input":{"ports":[{}]},"frames":6},{"input":{"ports":[{"right":true}]},"frames":2},{"input":{"ports":[{}]},"frames":6},{"input":{"ports":[{"south":true}]},"frames":2},{"input":{"ports":[{}]},"frames":40}]'
  # early: the tray's stacks and counts, two piles that may still be taken two at a time
  $SHOT "$CARTS/facet_auto.wasc" "$OUT/play-2.png" --steps "$(idle 330)"
  # a finished match
  $SHOT "$CARTS/facet.wasc" "$OUT/play-3.png" FACET_SEATS=c1:c0 FACET_ONE_MATCH=1 --steps "$(idle 30000)" --stop facet.finished=1
  # the key frame: the table alone, at 4K
  $SHOT "$CARTS/facet_auto.wasc" "$OUT/key-0.png" --size 3840x2160 FACET_NO_HUD=1 --steps "$(idle 2400)"
  # and early, the tray still full: the cover and the icon
  $SHOT "$CARTS/facet_auto.wasc" "$OUT/key-1.png" --size 3840x2160 FACET_NO_HUD=1 --steps "$(idle 150)"
fi
convert -size 2400x720 xc:none -font "$FONT" -pointsize 440 -gravity center \
  -fill "#120a02" -stroke "#120a02" -strokewidth 30 -annotate +10+14 FACET \
  -fill "#2e1c06" -stroke "#2e1c06" -strokewidth 44 -annotate +0+0 FACET \
  -fill "#b07c22" -stroke "#b07c22" -strokewidth 20 -annotate +0+0 FACET \
  -fill "#f6e3a4" -stroke "#f6e3a4" -strokewidth 6 -annotate +0+0 FACET \
  -trim +repage -bordercolor none -border 24 "$ART/logo.png"
for i in 0 1 2 3; do convert "$OUT/play-$i.png" -resize 1920x1080! -quality 90 "$ART/shot-$((i + 1)).jpg"; done
# the cover: a portrait over the tray of stones and the decks, the name across the top
convert "$OUT/key-1.png" -resize 3840x2160! -crop 1440x2160+150+0 +repage -resize 1200x1800! -quality 92 "$ART/cover.jpg"
"$COVER_NAME" "$ART/cover.jpg" "$ART/logo.png"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -20x-5 -quality 90 "$ART/background.jpg"
convert "$OUT/key-0.png" -resize 1920x1080! -brightness-contrast -30x-5 -fill "rgba(0,0,0,0.6)" -draw "rectangle 0,380 1920,700" \
  \( "$ART/logo.png" -resize 1300x \) -gravity center -composite -quality 92 "$ART/title.jpg"
# the icon: the emerald stack in the tray, close
convert "$OUT/key-1.png" -resize 3840x2160! -crop 420x420+170+700 +repage -resize 512x512! "$ART/icon.png"
convert "$OUT/play-0.png" -resize 1600x900 -quality 88 "$ROOT/docs/screenshots/facet.jpg"
ls -la "$ART"
