---
title: Maps and game data
parent: Rustfront
nav_order: 2
permalink: /rustfront/authoring/
---

# Maps and game data

Rustfront ships plain-text maps alongside its indexed art and other assets. Start with a copy of `games/rustfront/maps/skirmish.txt` for placed units or a copy of a lobby map for neutral starts. Map lines are parsed relative to the game's root.

```sh
build/bin/rustfront --map maps/skirmish.txt
env SDL_VIDEODRIVER=dummy build/bin/rustfront --map maps/skirmish.txt --check
```

## Map keys

| Entry | Meaning |
| --- | --- |
| Terrain grid | `.` sand, `:` silt, `*` ore, `#` gravel, `,` grass, `^` stone, `~` water, `R` rocks, `x` wreck, `=` concrete |
| `level ROW` | Optional relief rows. Heights are `0`–`9` and `/` marks a ramp. |
| `unit NAME OWNER X Y [FACING]` | Place an actor by map name. Coordinates are cells and a structure uses its top-left cell. Facing has 16 steps counterclockwise from east. Add `@TAG` to reference the actor from mission logic. |
| `camera X Y` | Opening camera position |
| `credits OWNER N` | Override the side's starting credits |
| `title NAME` | Display name in the map list |
| `start N X Y [FACTION]` | Neutral start for skirmish or multiplayer. Ensure each start has room for its starting force, access to ore, and a ground route to opponents. |

## Mission structure

A campaign scenario adds `mission`, `title`, and `briefing` metadata and one or more `objective` entries. Objectives can require destroying a tagged unit, protecting an asset, surviving, holding an area, banking credits, building a type, or defeating every enemy. The `tech` entries restrict available products. A `trigger` can react to elapsed time, entering an area, a death, or construction. Repeat the scenario in headless mode and verify both outcomes when adding objectives.

Use the [campaign format reference](https://github.com/corepunch/open-rts/blob/main/games/rustfront/docs/CAMPAIGN.md) for exact arguments and a worked example. [Terrain internals](https://github.com/corepunch/open-rts/blob/main/games/rustfront/docs/TERRAIN.md), the [Scener art notes](https://github.com/corepunch/open-rts/blob/main/games/rustfront/docs/SCENER.md), and the [sound reference](https://github.com/corepunch/open-rts/blob/main/games/rustfront/docs/AUDIO.md) cover the asset data behind a map.

## Roster source

Gameplay values are defined in `games/rustfront/g_game.c` and related rules files. `p_prod.c` defines production costs, timings, makers, and prerequisites. `info.c` binds artwork and states. The field manual reads a checked-in export of those tables. After changing values, refresh it with the catalog test and review the resulting diff.

```sh
make build/bin/tests/rustfront/test_catalog
env SDL_VIDEODRIVER=dummy OPEN_RTS_RF_CATALOG=site/_data/rustfront.json \
  build/bin/tests/rustfront/test_catalog
```

Add the matching portrait to `site/rustfront/img/units/` when adding an actor. Sprite effects, shells, and other transient engine actors are outside the faction roster.
