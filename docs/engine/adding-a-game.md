---
title: Adding a game
permalink: /engine/adding-a-game/
---

# Adding a game

Bring up one small map with two sides, a movable unit, a visible attack, and a reproducible win condition. That exercises the same binary, map and asset loading, command path, and thinker lifecycle the rest of the game will use.

## Set up the game boundary

1. Create `games/<id>/` with `game_config.h`, `mobj_data.h`, `info.h`, and `info.c`. Use the local game include path for the compile-time headers. Put engine-facing game declarations in `include/<id>.h` if needed.
2. In the root Makefile, collect the directory's C files with `$(sort $(shell find games/<id> -name '*.c'))`. Instantiate `GAME_TARGET`, choose the appropriate world-axis convention, and add the binary to `all`. Register the game's headless tests in the existing test rules.
3. Bind `gameinfo_t` and the ruleset. Add actor rows, type entry states, and a minimal sprite/state graph. Begin with stand, travel, attack, and death states as applicable. Give each object a stable type ID.
4. Implement the game's linked entry points. `G_InitGame` initializes game tables, `G_DoLoadLevel` creates the active level, `W_LoadAssets` decodes assets, and `P_LoadThings` places starting objects. Read the declarations and default hooks in [engine.h](https://github.com/corepunch/open-rts/blob/main/include/engine.h) before adding hooks.
5. Define any products with their cost, build time, maker, prerequisites, and placement rules. Bind objectives to the level and report victory or defeat through the engine outcome API. Add menu and HUD tables once the first level is playable.
6. Build `make build/bin/<id>`, run the new binary with `SDL_VIDEODRIVER=dummy` and `--check`, then exercise selection, movement, combat, economy, production, and win/loss through the normal entry points. Add a headless fixture for the map and its declared object placements.

## Original game walkthrough using Rustfront

[Rustfront's source](https://github.com/corepunch/open-rts/tree/main/games/rustfront) is a complete example of an original title authored for this engine.

| Step | Rustfront example |
| --- | --- |
| Author a small map | `maps/skirmish.txt` describes terrain, credits, and placed units. `w_rustfront.c` and `w_terrain.c` interpret that format. |
| Define the roster | `g_game.c` supplies gameplay values, `info.c` supplies the state graph, and `factions.c` maps each faction to shared roles. |
| Make a unit available | Add its product in `p_prod.c`, including its faction's producer and prerequisites. Give it packed indexed artwork, a valid spawn state, and a test. |
| Add mission behavior | `g_campaign.c` runs objectives and triggers. `g_mission.c` applies world and production hooks. Each level owns the running mission. |
| Connect the front end | `m_menu.c` describes campaign and skirmish screens, `hu_rustfront.c` describes the HUD, and `w_starts.c` translates lobby seats into starts. |

```sh
make build/bin/rustfront
env SDL_VIDEODRIVER=dummy build/bin/rustfront --check
env SDL_VIDEODRIVER=dummy make test-rustfront
```

See the [map and mission authoring guide]({{ '/engine/rustfront-maps/' | relative_url }}) for Rustfront's map keys and authoring workflow.

## Retail game integration

Begin with a legitimate copy of the exact retail data version you want to support. Keep the payload local. Record format provenance and the input version, and implement or extend C readers that validate file spans and convert the native representation into common engine data. Reuse shared indexed rendering and widget behavior, then add specific rules based on verified retail evidence.

| Title | Trace the first playable map | Reference |
| --- | --- | --- |
| StarCraft | `g_game.c` selects the scenario. `w_chk.c` reads CHK sections and placed units. `w_assets.c` resolves native assets. The game implements IScript, production, and scenario trigger handling in its own directory. | [StarCraft formats](https://github.com/corepunch/open-rts/blob/main/docs/SC_FORMATS.md) and [game source](https://github.com/corepunch/open-rts/tree/main/games/starcraft) |
| Dark Colony | `w_map.c` reads the map, `w_bts.c` reads terrain, and `w_fin.c` and `w_spr.c` supply sprite definitions and images. `g_game.c` owns native startup and level attachment, with mission script work in `p_script.c`. | [Dark Colony architecture](https://github.com/corepunch/open-rts/blob/main/docs/DC_ARCHITECTURE.md) and [game source](https://github.com/corepunch/open-rts/tree/main/games/dark-colony) |

For a loader, compare decoded names, dimensions, frames, palettes, and pivots against known native records. For simulation parity, compare a focused action or scripted event with the retail executable and document the evidence, unknowns, and test that reproduces your result. See the repository's [reverse-engineering workflow](https://github.com/corepunch/open-rts/blob/main/REVERSE_ENGINEERING.md) and [retail parity matrix](https://github.com/corepunch/open-rts/blob/main/docs/RETAIL_PARITY_MATRIX.md).
