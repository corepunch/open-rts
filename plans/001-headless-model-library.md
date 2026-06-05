# Refactor Simulation Model Into A Headless Loadable Server Library

## Context

Dark Colony construction, production, mission scripting, resources, and unit
state need to be testable without creating an SDL window or renderer. The
target shape is similar to Quake 2's `gamex86.dll`: the model/server side is a
loadable library with a stable API, and both the game client and tests can
drive it.

## Scope

- [ ] Extract model/server responsibilities from client runtime code into a
      library target.
- [ ] Keep rendering, window creation, input translation, and UI drawing in the
      client layer.
- [ ] Provide a headless API for loading a plugin, loading a map/mission,
      ticking simulation time, issuing commands, and inspecting state.
- [ ] Make Dark Colony plugin logic usable from this model/server API without
      depending on SDL renderer objects.
- [ ] Preserve existing playable client behavior.

## Suggested Shape

- `server/` or `model/` exposes a small C API for tests and the client.
- Client loads/links the model library and passes translated user commands into
  it.
- Tests can create a model instance, load `data/DCOLONY`, tick deterministic
  frames, and assert units/resources/effects without a window.

## Acceptance Criteria

- [ ] There is a build target for the model/server library.
- [ ] `build/bin/open-rts` still runs through the client path.
- [ ] A headless executable or test binary can load Dark Colony through the
      model/server API.
- [ ] No SDL window/renderer is required to load a Dark Colony mission and
      advance simulation ticks.
- [ ] The API is documented enough for new tests and client integration.

## Tests

- [ ] Add a smoke test that loads Dark Colony Human01 through the model/server
      API and advances a fixed number of ticks.
- [ ] Add a test that verifies no renderer/window symbols are required by the
      simulation-only path.
- [ ] Existing dummy-video smoke checks continue to pass.
