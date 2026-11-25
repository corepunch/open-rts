#!/bin/sh
# Check the unit kit and render every sprite-preview camera of art/units.blks.
#   games/rustfront/art/render.sh                 # all cameras -> art/previews/
#   games/rustfront/art/render.sh BattleTank      # one camera
# SCENER overrides the binary (default: scener on PATH).
set -eu
cd "$(dirname "$0")"
SCENER=${SCENER:-scener}
command -v "$SCENER" >/dev/null || { echo "scener is required (see docs/SCENER.md)" >&2; exit 1; }
for f in units.blks prefabs/*/*.blk; do xmllint --noout "$f"; done
"$SCENER" --check units.blks
if [ $# -gt 0 ]; then
  for cam in "$@"; do "$SCENER" --render units.blks --camera "$cam" --format png --output-dir previews; done
else
  "$SCENER" --render units.blks --format png --output-dir previews
fi
