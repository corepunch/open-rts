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

## How a game object is defined

An object has three related definitions, plus the instance created at runtime.

```text
ActorType[]        gameplay/config values
      +
MobjInfo[]         original-style type/state entry points
      +
State[]            animation/action graph
      |
      +--> mobj_t   live simulation object
```

### `ActorType[]`: gameplay configuration

Each game plugin defines an `ActorType` table in its game implementation (the C
type is `actortype_t`). A row is the authoritative runtime configuration for a
type: numeric ID, name, base sprite/shadow, traits, speed, hit points, attack
range/damage/cooldowns, death timing, harvester state, and effect names.

These values are intentionally hardcoded C data, like Doom's `mobjinfo[]`, rather
than read directly from an extracted binary table on every spawn. Extraction
tools and original files establish the values; the checked-in table makes the
simulation deterministic, reviewable, and independent of an optional asset
probe. DC uses `games/dark-colony/g_game.c`; DR uses
`games/dark-reign/g_game.c`.

`traits` are capability flags, not animation states. For example,
`MF_MOBILE | MF_ATTACK` makes an object eligible for movement and attack orders,
while `MF_HARVESTER` enables resource behavior. A building can be renderable,
selectable, and a producer without being mobile.

### Trait flags (`MF_*`)

The trait enum is `mobjflag_t` in `play/actor.h`. Older notes and commits call
these flags `T_*` (`T_MOBILE`, etc.); the current names are `MF_*` following the
Doom naming convention. Traits are a bitmask stored in both `actortype_t.traits`
and the live `mobj_t.traits`. `apply_actor_type_defaults()` copies the authored
mask to a spawned object, so add or remove capabilities in the plugin's
`ActorType` table rather than in the renderer or command handler.

| Flag | Capability and consumers |
| --- | --- |
| `MF_SELECTABLE` | The player may select the object; selection markers and selection-dependent UI require it. Death clears it. |
| `MF_MOBILE` | The object participates in movement, pathfinding, and separation. Movement/harvest orders require it. Death clears it. |
| `MF_RENDERABLE` | The renderer draws the object's body and state overlay. It normally remains set on a dead object while its death animation plays. |
| `MF_ATTACK` | The object may receive attack orders and run attack behavior. Death clears it. |
| `MF_HARVESTER` | The object may execute resource harvesting. A harvest order requires both `MF_MOBILE` and `MF_HARVESTER`; death clears it. |

Use bitwise tests for individual capabilities and bitwise combinations for
requirements:

```c
if ((unit->traits & MF_ATTACK) != 0) { /* can attack */ }
if ((unit->traits & (MF_MOBILE | MF_HARVESTER)) ==
        (MF_MOBILE | MF_HARVESTER)) { /* can harvest */ }
unit->traits &= ~(MF_SELECTABLE | MF_MOBILE); /* remove capabilities */
```

Do not compare the complete mask when checking one capability: unrelated flags
may be present. Traits do not describe producer status, animation groups, or
death state; those come from product tables and `MobjInfo[]`/`State[]` data.
The renderer-neutral snapshot exposes the same bit values as
`RtsRenderTrait` in `game/g_game.h`.

### `MobjInfo[]`: type entry points

`mobjinfo_t` provides the classic state entry points and physical defaults:
spawn, see, missile/attack, pain, death, radius, height, mass, damage, and
flags. The model resolves an actor's type ID through the selected game's
`gameinfo_t` and applies the corresponding `ActorType` defaults before calling
`P_SpawnMobj`.

### `State[]`: animation and behavior graph

`state_t` is a data-driven state node. It names a sprite/frame, duration in
simulation tics, optional action callback, next state, state group metadata,
facings, per-facing frame/flag/offset/remap/intensity data, and optional overlay
data. State actions execute on state entry. Zero-tic states chain immediately;
nonzero states remain active until their tic count expires.

This means “walking”, “attacking”, “dying”, “deploying”, and “building” are
usually state chains rather than ad hoc renderer conditions. The model can detect
important transitions, such as entering an attack group, without making the
renderer responsible for gameplay.

### Generated `info.c` and `info.h`

For Dark Colony, `games/dark-colony/info.c` and `info.h` are generated from the
original sprite/frame tables by `tools/dc_info_gen`. The generated files contain
the sprite-name catalog, state rows, facing mappings, frame indices, animation
chains, offsets, flags, and overlay relationships. They are source artifacts
checked into the game directory so normal builds do not require generation.

The generator is a translation step, not a gameplay config loader. It converts
the original asset layout into the engine's `state_t` representation. When an
animation is wrong, inspect the source SPR/FIN frame table and generator output;
do not compensate by moving terrain or adding sprite-name-specific renderer
hacks.

## Sprite formats and cross-game layout

The engine keeps a common `spritesheet_t` interface, but does not assume that
every game packs frames the same way. A sheet contains decoded frame rectangles,
frame bounds, ground points, displacements, rotation counts, frames-per-rotation,
and optional named sequences. Each game loader fills this structure according to
its native format.

### Dark Colony

DC SPR files are palette-indexed sprite resources whose frame and animation
meaning is coupled to DC's FIN/state data. DC has both 8- and 16-direction
conventions depending on the asset/state. FIN data also supplies building
footprints, pivots, offsets, overlays, and multi-part placement. The DC loader
preserves bottom-up world coordinates and uses authored FIN offsets for building
and Barracks release placement.

DC's `gameinfo_t` selects the applicable direction mode and state coordinate mode
for each interpretation. The generated state rows carry direction codes and
per-facing frames rather than assuming frame `direction * N` universally.

### Dark Reign

DR sprite resources and scenario/map data use different naming, tile, facing, and
placement conventions. DR actors commonly use 8-facing gameplay orientation,
while some source data and visual tables expose 16-direction concepts that must
be mapped to the game's direction codes. Buildings use separate body/base/tile
visual pieces, and scenario placement is not interchangeable with DC placement.

The DR loader resolves scenario unit/building declarations into actor types and
visual specs, then the shared state/render path applies the DR `gameinfo_t`
direction and coordinate policy. The renderer never guesses a DC layout for a DR
sprite.

### Coordinate and facing normalization

The shared simulation stores world positions in game-cell coordinates. Native
asset coordinates remain explicit at the boundary:

- `direction_mode` selects the game's direction-code scheme;
- `state_coord_mode` identifies ground-offset versus FIN top-left coordinates;
- cell dimensions come from the game (`g_cell_w`, `g_cell_h`);
- state rows carry actual direction codes, frame indices, offsets, remaps, and
  intensities;
- renderer conversion applies the selected policy exactly once.

This is why a state can use a DC FIN top-left pivot while another state uses a
ground offset, without flipping the whole map or applying a global correction.
`play/p_facing.c`, `play/p_mobj.c`, and `render/r_draw.c` own the shared
conversion helpers; game loaders own native-file interpretation.

## Loading a game

The interactive and model loaders follow the same conceptual sequence:

1. Select the game implementation and establish its default root/map/sprite.
2. Call `G_DoLoadLevel` for map geometry, dimensions, tiles, resources, and
   game-specific map metadata.
3. Call `P_LoadThings` to decode initial object declarations into `mobj_t` rows.
4. Apply actor defaults and enter each object's spawn state with `P_SpawnMobj`.
5. Load optional mission/script state with `G_LoadMission`.
6. Load tiles and the fallback sprite through `W_LoadAssets`.
7. Resolve per-object and decoration sprite resources with `R_InitSprites`.
8. Initialize game UI/font resources when `gameui` is present.

The model path deliberately omits SDL asset textures but keeps the same map,
actor, state, mission, production, movement, and event behavior. This is what
makes scenario tests useful without a display.

## Input and command translation

SDL input is handled at the driver/game boundary. `G_Responder` interprets
mouse/keyboard events against the current camera, map, selection rectangle, and
UI layout. Screen coordinates are converted to world/grid coordinates by
`R_ScreenToMapGrid` and related helpers. Selection and camera state live in
`app_t`; simulation state remains in `mobj_t`/`RtsGameModel`.

The interactive path then invokes gameplay operations such as `P_MoveOrder`,
`P_HarvestOrderAt`, or UI product activation. The headless/API path expresses
the same intent as `RtsGameCommand` values:

```text
SDL event -> G_Responder -> gameplay command/order -> simulation tick
test/API  -> RtsGameCommand ----------------------^
```

Commands identify actors by stable ID where possible. Index fields are retained
as compatibility fallbacks, but indexes can change after death/removal or
production because the unit array uses swap-compaction.

Input does not directly mutate rendering fields or fabricate completion events.
The command changes simulation intent; the next tick updates movement, state,
combat, mission logic, and production, which then produces snapshots and events.

## Mission scripts and game-specific behavior

Mission code is owned by the game directory. `G_LoadMission` parses the relevant
scenario/script format, `G_MissionTicker` advances scripted spawns/objectives,
and `G_FreeMission` releases it. Mission code receives the map, actor array,
effect array, actor count, HUD, and elapsed time so scenario behavior can create,
remove, move, or animate objects using the same runtime representation.

The shared model ticks the mission between core simulation phases and then
reconciles stable IDs and transition state for event emission. This allows a
scripted enemy wave or release animation to be observed by exactly the same test
code as a player-issued order.

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
