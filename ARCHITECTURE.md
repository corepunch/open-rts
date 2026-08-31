# open-rts architecture

## Purpose

open-rts is a small C engine for reproducing classic real-time strategy games. The
engine owns the common loop, map primitives, actor state machine, movement,
combat, rendering, and headless model API. Each game supplies its original-data
loaders, actor definitions, animation tables, mission logic, UI definition, and
asset conventions.

The project follows the Doom/Heretic/Hexen family of designs: simulation objects
advance from a single thinker-style tick, state actions run when states are
entered, and presentation reads a snapshot of simulation state.

## Layers

```text
driver/       application, SDL window, command-line selection, main loop
interface/    SDL/video integration
game/         shared game-model API and command/event bridge
play/         simulation: actors, states, movement, pathfinding, combat
render/       map, sprite, effect, and decoration rendering
hud/          fonts, messages, and game UI presentation
games/<game>/ game-specific plugin, loaders, data tables, missions
tests/        headless model, data-layout, and per-game command tests
data/<game>/  original game assets and scenario files
```

The engine is statically linked with one game directory at a time. The selected
game is chosen at build/link time for the binary and at runtime with `--game` for
the multi-game executable layout. The game interface is deliberately a set of
externs and functions rather than a dynamic plugin registry; this keeps the
reproduction path close to the original game and makes each game self-contained.

## Runtime flow

The interactive loop is:

1. Parse the selected game and data paths.
2. Load the map/scenario through the game implementation.
3. Load initial actors, map tiles, decorations, sprites, and optional mission
   state.
4. Read SDL input and translate it into game commands.
5. Advance the deterministic simulation by fixed simulation time.
6. Run mission scripts, production, effects, and HUD timers.
7. Build the render plan and draw the map, decorations, actors, effects, and UI.

The headless path uses the same model and simulation code without creating a
window or renderer. `RtsGameModel` loads a scenario, accepts commands, advances
ticks, exposes a renderer-neutral snapshot, and exposes simulation transition
events for tests or another frontend.

## Simulation objects

`mobj_t` is the common actor representation. It contains:

- stable identity (`id`), type, owner, health, and gameplay traits;
- world position, movement goal, path, facing, and arrival state;
- state-machine fields (`state_id`, `tics`, frame, render flags);
- attack target, cooldown, animation/death state, and effect metadata;
- production actor/product, queue, timers, and release state.

Actors use `gameinfo_t`, `mobjinfo_t`, and `state_t` tables. `P_SetMobjState`
enters a state, applies its visuals, runs its action callback, and follows zero-tic
next states immediately. `P_Ticker` is the central actor update. Dead actors are
removed using swap-compaction, so callers must use stable actor IDs rather than
array positions across ticks.

Movement uses map-cell coordinates and pathfinding from `play/p_map.c`. Commands
set goals; ticking advances actors along paths. The model reports `UNIT_ARRIVED`
when an ordered actor reaches its goal.

## Commands and events

Commands are intent coming into the simulation. They are defined in
`game/g_game.h` and currently include selection, movement, harvesting, UI button
activation, attack, and product construction.

Events are transitions coming out of the simulation. They are delivered through a
bounded FIFO with `rts_game_model_poll_event`:

| Event | Meaning |
| --- | --- |
| `UNIT_ARRIVED` | A movement order reached its goal. |
| `BUILD_QUEUED` | A valid product order was accepted. |
| `BUILD_STARTED` | Production timing began for a queued product. |
| `UNIT_BUILT` | A unit actor was created and placed. |
| `BUILDING_BUILT` | A building actor was created and placed. |
| `BUILD_FINISHED` | Compatibility/general production completion event. |
| `BUILD_BLOCKED` | Production could not release or continue; emitted once per blocked queue state. |
| `ATTACK_STARTED` | An actor entered an attack state. |
| `UNIT_DIED` | An actor crossed into death/removal. |

Events carry the simulation tick, subject and target IDs/types, product class and
type where relevant, and subject position. IDs are stable for the life of an
actor; snapshot indexes are only current-frame lookup conveniences.

This separation is intentional: commands are deterministic inputs, while events
are observable consequences. It also matches the useful OpenRA distinction
between orders/activities entering the simulation and notifications or state
transitions coming out of it.

## Production

Product definitions are authored in each game plugin as C tables. They describe
the original UI ID, product row/type, cost, faction, prerequisites, and producer
types. Runtime game data may be used to discover and verify values, but the
authoritative gameplay balance and product mapping is the checked-in C table.

The normal lifecycle is:

```text
command accepted
      -> BUILD_QUEUED
      -> BUILD_STARTED
      -> training/release or placement
      -> UNIT_BUILT / BUILDING_BUILT
```

Dark Colony units use producer queues and preserve the Barracks release animation
and FIN-authored exit point. Dark Colony buildings currently use immediate
placement after a valid producer/order. Dark Reign products use the production
queue and construction-crew producer path.

## Game implementations

### Dark Colony

`games/dark-colony/` contains the DC map, scenario, sprite, binary-table, actor,
state, mission, and interaction code. Its coordinate and animation behavior is
driven by MAP, SCN, SPR, FIN, and related original assets. Buildings are modeled
as actors so selection, health, rendering, and production use the same object
path as units.

The plugin provides hardcoded `ActorType` gameplay values and generated
`MobjInfo`/`State` animation data. `tools/dc_info_gen` and
`tools/dc_gamestat_gen` regenerate derived tables from `data/DCOLONY`.

### Dark Reign

`games/dark-reign/` contains the REIGN map/scenario, tile, sprite, mission, actor,
and product implementation. Scenario scripts and original map data determine
team setup, buildings, construction crews, and resources. The plugin supplies
the FG actor/product tables and the Dark Reign-specific 16-direction and tile
conventions.

Dark Reign reproduction takes precedence over generic abstraction when the
original scenario or binary behavior requires a game-specific path.

### Other games

`games/7legion/` and `games/kknd/` follow the same game interface where their
current implementations support it. They share map/render/play infrastructure
but are not required to expose every production or model feature yet.

## Rendering and UI

Rendering consumes loaded map data and actor/effect state. It does not decide
gameplay outcomes. State tables provide sprite, frame, facing, offsets, remap,
intensity, and overlay data; render code turns those values into screen-space
draw calls.

The model snapshot contains presentation-neutral actor/effect/decoration values
and a declarative UI script. This lets the interactive renderer, tests, or a
future network/client frontend consume the same simulation without reaching into
plugin internals.

## Determinism and future networking

Simulation state is advanced from commands and ticks. The intended multiplayer
shape is lock-step: exchange commands for a simulation tick, apply the same
commands on every peer, and never use rendered frames as simulation input.
Stable IDs, fixed simulation ticks, state-machine transitions, and model events
are the foundation for replay and network synchronization.

The current public model accepts floating-point `dt`; a future network/replay
layer should quantize this to a fixed tic rate and move remaining timing fields
fully into tic/state chains where needed.

## Testing and build structure

`make` builds each game binary. Source lists are collected with `find`, so adding
a source file inside an engine or game directory does not require editing the
Makefile.

Headless coverage includes:

- DC scenario and sprite/FIN layout checks;
- DC and DR command acceptance and event-lifecycle checks;
- movement, attack command, production queue, unit completion, building
  completion, stable IDs, resources, and producer/product event payloads.

Use `make test` for the model tests and set `SDL_VIDEODRIVER=dummy` for binary
smoke checks. Reverse-engineering notes and stable asset/binary findings belong
in `REFERENCES.md`; generated reverse-engineering dumps remain ignored.
