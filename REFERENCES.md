# Reverse Engineering References

Keep these links handy when touching loaders, tile animation, map objects, or
plugin-specific behavior.

## Dark Colony

- DarkColony.pl downloads:
  https://www.darkcolony.pl/downloads.php?cat_id=2
  - Community downloads and historical Dark Colony material. Check here when
    looking for tools, map/editor notes, or the Polish community project files.

- endotermic/Dark-Colony:
  https://github.com/endotermic/Dark-Colony
  - Open-source Dark Colony reference lead. Use it when validating `.MAP`,
    `.BTS`, `.SPR`, object placement, palette cycling, and faction/unit logic.

Local game-data files that have already been useful:

- `data/DCOLONY/GAMESTAT/GAMESTAT.TXT` for unit IDs, names, health, and weapon
  IDs.
- `data/DCOLONY/GAMESTAT/WEAPSTAT.TXT` for weapon range, damage, and rate of
  fire. Trooper/Grey weapon rows use range `4`, damage `100`, and rate `15`.
- `data/DCOLONY/GAMESTAT/DEPEND.TXT` for unit/building dependency names.
- `data/DCOLONY/SCENARIO/*.MAP`, `*.SCN`, and `*.BTS` for maps, starting
  objects, tilesets, and water palette bands.
- `data/DCOLONY/SPRITES/*.SPR` for unit sprites.
- `data/DCOLONY/ANIMATE/*.FIN` for sprite animation labels and frame ranges.
  For example, `TROOPER1.FIN` maps `TROOPER1MOVE6` to frames `8..9`, while
  `GRAY.FIN` maps `GRAYMOVE0`, `GRAYMOVE14`, etc. to the walking/fire blocks.
  Dark Colony `.FIN` labels are useful for identifying blocks, but their
  direction suffixes can be misleading. Infantry walking and firing for
  `TRSC.SPR`, `GRAY.SPR`, and `TROOPER1.SPR` are phase-major: one phase contains
  all eight rotations, so each facing advances with stride `8`. For example,
  `GRAYFIREA0 = 80..87` is a row of rotations for one firing phase, not eight
  animation frames for direction `0`. The runtime shoot sequence uses the full
  eight `FIREA` phases, and the death strips for `TRSC.SPR` and `GRAY.SPR`
  are directional strips rather than single static corpse frames.
  Later sparse/recovery poses in the block read like disappearing or hit/death
  motion when looped at weapon speed.

## Dark Reign

- OpenDR:
  https://github.com/drogoganor/OpenDR
  - Primary Dark Reign reference for map import, `.TIL` frame layout, generated
    transition masks, resource handling, and OpenRA-style plugin structure.

- drExplorer:
  https://github.com/btigi/drExplorer
  - Useful reference for Dark Reign FTG archives.

Local game-data files that have already been useful:

- `data/REIGN/dark/deftxt/*.TXT` for unit, building, overlay, and animation
  definitions.
- `data/REIGN/dark/scenario/**/*.MAP` and `*.SCN` for terrain and placed
  objects.
- `data/REIGN/dark/graphics/**/*.TIL`, `*.PAL`, and `SPRITES.FTG` for terrain,
  palettes, and sprites.
- OpenDR sequence YAML and `DrSprLoader.cs` show the OpenRA-style unit
  animation model: each sequence has a `Start`, `Facings`, `Length`, and
  `Tick`; rendering chooses a facing frame offset from the unit direction and
  then advances within that sequence for walking/firing.
