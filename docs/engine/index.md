---
title: Engine architecture
permalink: /engine/
---

# Engine architecture

open-rts compiles the shared engine together with one game directory for each executable. The selected game supplies its data formats, object definitions, rules, missions, and screens. That boundary is established at link time. Start Rustfront with `build/bin/rustfront`, StarCraft with `build/bin/starcraft`, and Dark Colony with `build/bin/dark-colony`.

## Ownership

| Source | Responsibility |
| --- | --- |
| `driver/`, `interface/` | Application entry point, input and platform integration, clock, file I/O, and network transport |
| `game/` | Headless model API, shared command entry points, events, and snapshots |
| `play/` | Thinkers, states, movement, pathfinding, sight, combat, production, and shared AI |
| `render/` | Indexed maps and sprites, palette translation, composition, and rendering caches |
| `hud/` | Shared menu, HUD, widget, and lobby behavior |
| `sound/` | Channels, listener, mixer, and playback |
| `games/<id>/` | Native loaders, object and state tables, game rules, missions, and menu declarations |

## A command through the engine

1. Input resolves to a game command with a player, selected object IDs, and a target such as a cell or another object. Headless callers can submit model commands directly.
2. The game loop applies commands on simulation tics. In multiplayer, each peer waits for the commands for that tic and applies them in the same order.
3. `P_Ticker` advances the thinker list. `P_MobjThinker` runs common object behavior and decrements state tics. `P_SetMobjState` invokes the new state's action and follows zero-tic transitions immediately.
4. Production, mission logic, AI, and game hooks apply the title's rules. The model exposes events and snapshots for callers, while the desktop renderer and HUD read the current level and present it independently of tic cadence.

## Objects and level lifetime

The active `level_t` owns map arrays and any game-specific mission data. A game can attach that data with its cleanup callback during level load. `P_FreeLevel` releases it. Individually allocated `mobj_t` objects are linked through the global `thinkercap` list, with deferred removal during thinker processing. Units, structures, projectiles, and effects use this lifecycle. Game-specific object fields come from the selected game's `mobj_data.h`.

| Definition | What it controls |
| --- | --- |
| Actor type (`mobjtype_t`) | HP, footprint, traits, speed, weapons, resource behavior, and asset names |
| `mobjinfo[]` | Entry states for a type, including spawn, movement, attack, and death |
| `states[]` | Sprite and frame, duration in tics, C action, and next state |
| `mobj_t` | Live health, position, momentum, target, owner, and current state |
| `gameinfo_t` | Game-wide table bindings, palette and presentation policy, sound, and drawing hooks |

Simulation positions and momenta use 16.16 fixed-point values. State actions accept the actor pointer and can consult the active level. Spawn initializes the starting state without invoking its action. Later state entries can move, attack, or spawn other objects.

## Native assets and presentation

Game loaders resolve native paths and decode their data into engine-owned indexed images, sprite frame and rotation definitions, map data, and palettes. The renderer owns the drawing resources derived from those images. A state chooses a sprite and frame, while the body's facing, native pivots, and palette translation determine how it appears.

Games provide menus and sidebars as `menuitem_t` tables. Shared HUD code handles input, focus, layout, and widgets. A game can attach an owner draw function for art that the table cannot describe. Runtime asset loaders use native formats such as PCX, SPR, and GRP. Website images are separate from the runtime asset pipeline.

## Determinism and verification

Lockstep exchanges commands and compares consistency data to detect divergence. Put durable simulation state in the level or objects, preserve it across saves where applicable, and include game-specific state in consistency checks when it affects future simulation. Use engine RNG and tic counts for decisions that peers must reproduce.

Use headless tests for loaders, movement, production, and mission logic. Native asset tests exercise the local data the title expects. A minimal smoke check runs the title under SDL's dummy video driver.

- [Adding a game]({{ '/engine/adding-a-game/' | relative_url }})
- [Detailed architecture and API notes](https://github.com/corepunch/open-rts/blob/main/ARCHITECTURE.md)
- [Network protocol](https://github.com/corepunch/open-rts/blob/main/docs/NETWORK.md)
- [Production runtime](https://github.com/corepunch/open-rts/blob/main/docs/PRODUCTION_RUNTIME.md)
- [Test suite](https://github.com/corepunch/open-rts/blob/main/tests/README.md)
