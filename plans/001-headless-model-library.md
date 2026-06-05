# Refactor Game Model Into A Renderer-Detached Loadable Library

## Context

Dark Colony construction, production, mission scripting, resources, and unit
state need to be testable without creating an SDL window or renderer. The
target shape is similar to Quake 2's `gamex86.dll`: the game/model side is a
loadable library with a stable API. The renderer/client loads that library,
sends input commands to it, and receives presentation-neutral data describing
what should be rendered. The game library owns simulation state and gameplay
rules, while tests can load and drive the same library without a window.

## Scope

- [ ] Extract game/model responsibilities from client runtime code into a
      library target.
- [ ] Keep rendering, window creation, SDL event handling, texture ownership,
      and final drawing in the client/renderer layer.
- [ ] Define a command API for player intent: selection, movement, attack,
      build placement, production, ticking simulation time, and mission/control
      actions.
- [ ] Define a render snapshot API that returns only what the renderer needs to
      draw: map references, entity sprites/positions, effects, selection
      markers, health bars, command panels, and other UI-visible state.
- [x] Provide a headless API for loading a game plugin, loading a map/mission,
      ticking simulation time, issuing commands, and inspecting deterministic
      state.
- [ ] Make Dark Colony plugin logic usable from this model/server API without
      depending on SDL windows, renderers, textures, or events.
- [ ] Preserve existing playable client behavior.

## Suggested Shape

- `game/`, `server/`, or `model/` exposes a small C API for tests and the
  renderer/client.
- The renderer/client loads or links the game library, creates a game instance,
  translates SDL input into game commands, ticks the model, and requests a
  render snapshot each frame.
- The game library does not call rendering APIs. It returns stable ids,
  asset/sprite references, coordinates, animation frame ids, colors, text, and
  command-panel state in renderer-neutral structs.
- Tests can create the same game instance, load `data/DCOLONY`, send commands,
  tick deterministic frames, and assert model state or render snapshots without
  a window.

## Acceptance Criteria

- [x] There is a build target for the game/model library.
- [ ] `build/bin/open-rts` still runs through the client path.
- [x] A headless executable or test binary can load Dark Colony through the
      model/server API.
- [x] No SDL window/renderer is required to load a Dark Colony mission and
      advance simulation ticks.
- [x] The game/model library has no dependency on SDL renderer, SDL window, or
      texture objects.
- [ ] The client/renderer sends commands into the game/model library instead of
      directly mutating gameplay state.
- [ ] The client/renderer draws from render snapshots instead of reading
      renderer-owned gameplay internals.
- [ ] The API is documented enough for new tests and client integration.

## Tests

- [x] Add a smoke test that loads Dark Colony Human01 through the game/model API
      and advances a fixed number of ticks.
- [x] Add a command-in/render-snapshot-out test that selects units, issues a
      move command, ticks simulation, and verifies both state and snapshot
      contents without a window.
- [x] Add a test that verifies no renderer/window symbols are required by the
      simulation-only path.
- [ ] Existing dummy-video smoke checks continue to pass.
