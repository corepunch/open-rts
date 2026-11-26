#!/bin/sh
# Regenerates every piece of Rustfront's art from the Scener prefabs, in order:
#
#   1. layout.py + render.sh   units.blks and the review previews      (art/previews/*.png)
#   2. effects.py              explosions, shells, rockets, smoke, fire (sprites/fx_*.png)
#   3. sprites.py              every unit's game sprites: facings, animation frames,
#                              team masks, shadows, construction and damage (sprites/*.png, sprites.json)
#   4. icons.py                the sidebar's build icons                (sprites/icons.png)
#   5. terrain.py              ground tiles, model tiles, transitions, cliffs and ramps (relief.py)
#                              (sprites/terrain.png, terrain.json)
#   6. pack8.py                256-colour PCX sheets and the C tables   (assets/*.pcx, terrain.txt, sprites.inc)
#   7. contact.py              the composited check sheet               (art/previews/sprites_contact.png)
#
# Then `make build/bin/rustfront test-rustfront` relinks and tests the game. Needs scener on
# PATH (or SCENER=...), python3 with Pillow, and xmllint; see docs/SCENER.md. About 11 minutes for the three factions.
set -eu
cd "$(dirname "$0")"
SCENER=${SCENER:-scener}
command -v "$SCENER" >/dev/null || { echo "build_all.sh: scener is required (see docs/SCENER.md)" >&2; exit 1; }
command -v xmllint >/dev/null || { echo "build_all.sh: xmllint is required" >&2; exit 1; }
python3 -c "import PIL" 2>/dev/null || { echo "build_all.sh: python3 with Pillow is required (pip install Pillow)" >&2; exit 1; }
step() { printf '\n== %s\n' "$*"; }
step "previews (layout.py, render.sh)";   ./render.sh >/dev/null
step "effects (effects.py)";              python3 effects.py
step "game sprites (sprites.py)";         python3 sprites.py
step "build icons (icons.py)";            python3 icons.py
step "terrain (terrain.py)";              python3 terrain.py
step "8-bit sheets (pack8.py)";           python3 pack8.py
step "contact sheet (contact.py)";        python3 contact.py
printf '\nDone. Now: make build/bin/rustfront test-rustfront\n'
