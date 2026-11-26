#!/usr/bin/env bash
# The game list (games.txt, the one place games are listed) for scripts and CI.
#
#   scripts/games.sh            the games, one per line
#   scripts/games.sh targets    the ninja targets CI builds: each game, and its <game>_auto where it declares one
#   scripts/games.sh release    the games a release ships (and the art check reads): those not marked dev
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
games=$(grep -vE '^[[:space:]]*(#|$)' "$ROOT/games.txt" | awk '{print $1}')
for g in $games; do
  [ -f "$ROOT/games/$g/CMakeLists.txt" ] || { echo "games.txt lists $g but games/$g/CMakeLists.txt is missing" >&2; exit 1; }
done
case "${1:-}" in
  "") printf '%s\n' $games ;;
  targets)
    for g in $games; do
      echo "$g"
      grep -qE "(cw|bk)_add_app\(${g}_auto[[:space:]]" "$ROOT/games/$g/CMakeLists.txt" && echo "${g}_auto" || true
    done ;;
  release) grep -vE '^[[:space:]]*(#|$)' "$ROOT/games.txt" | awk '{ dev = 0; for (i = 2; i <= NF; i++) if ($i == "dev") dev = 1; if (!dev) print $1 }' ;;
  *) echo "usage: scripts/games.sh [targets|release]" >&2; exit 2 ;;
esac
