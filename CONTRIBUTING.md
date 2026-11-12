# Contributing to Open RTS

Choose a classic RTS you want to work on. You can improve a game already in
the repository or bring another game to the shared C engine.

## Improve an existing game

1. Start in `games/<id>/` and read the game's notes linked from the
   [README](README.md#the-six-games). Pick a behavior to reproduce, a feature
   to implement, or a bug to fix.
2. Build and run that game with your retail data. For a fidelity fix, record
   the retail version, map or mission, and the steps that show the behavior.
3. Make the change and run the game's headless tests. For a visual or input
   change, also try the affected screen or map in the game window. In your
   pull request, include what changed, how to reproduce it, and which checks
   you ran.

## Build and test your game

From the repository root, use a C11 compiler, Make, and SDL2 development files
that `pkg-config` can find. Put the files from your own legitimate copy in
the directory below. Keep the retail files out of commits and pull requests.

| Game | ID | Retail data root |
| --- | --- | --- |
| Dark Reign | `dark-reign` | `data/REIGN/dark` |
| Dark Colony | `dark-colony` | `data/DCOLONY` |
| 7th Legion | `7legion` | `data/7LEGION` |
| KKnD | `kknd` | `data/KKND` |
| Warcraft II | `warcraft-2` | `data/WAR2` |
| StarCraft | `starcraft` | `data/STARCRAFT` (unpacked assets) |
| Z | `z` | `data/Z` |

Set `game` to the ID you chose. This example builds and runs Dark Colony
and runs its headless suite.

```sh
game=dark-colony
make "$game"
"build/bin/$game"
make "test-$game"
```

For StarCraft, follow the [unpack instructions](README.md#starcraft) first.
Its build and test targets also invoke the map importer. Tests that load
retail data need the files in the expected locations. Run `make test` for
the broader suite when changing shared code and you have the required data.
See [Tests](README.md#tests) for network checks and longer simulations.

## Work within the engine and game boundaries

The shared engine handles the main loop, simulation ticks, movement and
pathfinding, common combat and production, indexed rendering, UI widgets,
and networking. Each game compiles those sources together with its
`games/<id>/` directory into a separate executable.

Keep retail format loaders, actor and animation tables, game-specific rules,
missions, and screens in `games/<id>/`. Use the shared interfaces in
[`include/engine.h`](include/engine.h) and put game-specific headless tests
under `tests/<id>/`. When a change belongs in the engine, check the other
games that rely on that behavior. See [ARCHITECTURE.md](ARCHITECTURE.md) for
the engine interfaces and lifecycle.

## Propose another classic RTS

Open an issue or draft pull request describing the title and retail edition,
the files and formats you expect to load, and what the first playable map
would demonstrate. Call out any shared engine changes you expect. A small
first milestone is a map with terrain and starting units where you can
select a unit and issue a move order.

- Start in a new `games/<id>/` directory and use an existing game as a
  reference for the loader and engine interfaces.
- Follow the existing per-game targets in the [Makefile](Makefile) to build
  a separate binary from shared sources and the new game's directory.
- Start with `G_DoLoadLevel` and `P_LoadThings` for the map and starting
  objects. Supply the game's identity, actor and state tables, and the asset
  loaders for the terrain and sprites used on that map.
- Document the retail files contributors need, where to put them, and the
  commands to build, launch, and test the first map. Add a headless check
  under `tests/<id>/` for map loading or the first supported command.
