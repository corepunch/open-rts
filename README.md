# open-rts

Small C/SDL2 base for an old-school grid-based RTS renderer/simulation,
currently wired to Dark Reign: The Future of War and Dark Colony data files.

## Build

```sh
make
make run
make dark-reign
make dark-colony
make build-dark-reign
make build-dark-colony
```

The default run expects the game data under the repository-local `data/`
directory:

```text
data/REIGN/dark
data/DCOLONY
```

You can override the data root, map, and unit sprite:

```sh
build/open-rts --game dark-reign /path/to/dark scenario/MULTI/8JUNGLE/8JUNGLE.MAP ucfcnst0.spr
build/open-rts --game dark-colony /path/to/DCOLONY SCENARIO/MPLAYER/D2PLAY01.MAP SPRITES/TROOPER1.SPR
```

For a non-interactive loader/renderer check:

```sh
env SDL_VIDEODRIVER=dummy build/open-rts --check
env SDL_VIDEODRIVER=dummy build/open-rts --check --game dark-colony
env SDL_VIDEODRIVER=dummy build/open-rts --screenshot /private/tmp/open-rts-smoke.bmp
```

## Controls

- Left click: select a unit
- Left drag: box select
- Shift + left select: add to selection
- Right click: order selected units to move with grid A*
- WASD/arrows: pan
- Middle drag: pan
- Mouse wheel: scroll camera
- `G`: toggle grid
- `Ctrl+A`: select all

## Shape

The code keeps the old-game-specific pieces as adapters:

- Core renderer, picking, selection, movement, and A* all use one orthogonal
  tile grid. Each game is registered as a client-side plugin that supplies
  defaults, map loading, terrain visuals, and sprites.
- `PALS` palette loader: 8-bit palette to ARGB.
- `TILE` tileset loader: Dark Reign `.TIL` terrain chunks, masks, shore tiles,
  generated transition frames, and shadow frames, decoded using OpenDR's frame
  layout instead of treating the file as a flat 576-byte tile array.
- `MAP_` + `.SCN` map loader: Dark Reign map dimensions, scenario terrain
  selection, and `PutUnitAt(...)` starting units. The base terrain uses the
  first two bytes of each 6-byte terrain record in the same shape as OpenDR's
  importer: byte 1 provides terrain type plus variation group, byte 2 selects
  the variation bank, and bytes 3-6 carry elevation-related data. Water and
  cliff terrain are blocked for A*. Dark Reign transitions use OpenDR's edge
  match table and generated `.TIL` mask frames instead of cross-fading
  neighboring tiles.
- Dark Reign `.SCN` `AddThingAt(...)` and `AddBuildingAt(...)` entries are
  resolved through the shipped definition files' sprite names for common map
  objects: cliffs, rocks, trees, plants, rubble, water doodads, water wells,
  and Taelon mines.
- `BOTG`/FTG archive loader: extracts contained files.
- `RSPR`/`SSPR` sprite loader: decodes paletted RLE sprite frames.
- Dark Colony `.SPR` loader: embedded palette, frame descriptors, and raw
  indexed pixels, with C sequence tables for stand/run frame ranges based on
  the game's `.FIN` animation labels.
- Dark Colony `.MAP` loader: width/height plus 6-byte map records. It uses the
  sibling `.O16` overview for first-pass terrain colors while the true terrain
  tile/remap resources are reverse engineered.

That gives a place to add sibling adapters later for Dark Colony, Warcraft II,
or other 8-bit paletted games without changing the simulation loop.

## Research Notes

- Architecture is intentionally command/simulation/render separated, following
  the Age of Empires networking paper's core idea that old RTS engines should
  keep a shared simulation driven by user commands rather than syncing every
  unit's position every frame.
- Grid navigation follows Red Blob Games' framing of grids as graph nodes for
  A*/Dijkstra-style search, with room later for waypoints, hierarchical grids,
  JPS, or flow fields when unit counts grow.
- Dark Reign map loading is based on the shipped `MAP_`, `TACTICS.MM`, and
  `.SCN` files plus OpenDR's public importer. The remaining packed terrain
  fields still need more reverse engineering for exact movement masks,
  elevation, and the original transition compositor.

Sources:

- Project reverse-engineering links and local data notes:
  [REFERENCES.md](REFERENCES.md)
- Paul Bettner and Mark Terrano, “1500 Archers on a 28.8: Network Programming
  in Age of Empires and Beyond”:
  https://zoo.cs.yale.edu/classes/cs538/readings/papers/terrano_1500arch.pdf
- Red Blob Games, “Grid pathfinding optimizations”:
  https://www.redblobgames.com/pathfinding/grids/algorithms.html
- Dark Reign Construction Kit notes:
  https://thevideogamedatabase.fandom.com/wiki/Dark_Reign:_The_Future_of_War
- drExplorer reference for Dark Reign FTG archives:
  https://github.com/btigi/drExplorer
- OpenDR importer and terrain renderer:
  https://github.com/drogoganor/OpenDR
