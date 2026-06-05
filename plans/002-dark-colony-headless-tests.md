# Add Dark Colony Headless Test Harness

## Context

Dark Colony currently has smoke checks through `SDL_VIDEODRIVER=dummy`, but
gameplay behavior should be testable directly through model state. This covers
existing features and future construction/production work.

## Scope

- [x] Add a test binary or test target for Dark Colony model tests.
- [x] Load Dark Colony data and maps without an SDL window.
- [x] Advance simulation with deterministic fixed ticks.
- [x] Expose enough model inspection to assert units, resources, effects, and
      mission script outcomes.

## Existing Feature Coverage

- [x] Human01 loads with expected player units.
- [x] Human01 dropship/beacon script spawns the dropship effect, beacon effect,
      and pending reinforcements.
- [ ] Human01 active beacon decorations are loaded at map-space coordinates.
- [x] Human02 loads with expected starting units, decorations, and vents.
- [ ] Petra-7 vents load with active/inactive state and remaining amount.
- [ ] Exploiter harvest/deploy path can add resources over fixed ticks.
- [x] Basic movement order updates selected unit path/goals deterministically.

## Future Feature Coverage

- [ ] Building actor load tests.
- [ ] Construction product availability tests.
- [ ] Building placement success/failure tests.
- [ ] Unit production queue/spawn tests.

## Acceptance Criteria

- [x] `make test` or an equivalent target runs Dark Colony headless tests.
- [x] Tests fail with useful messages when expected map/script state changes.
- [x] Tests do not require `SDL_VIDEODRIVER=dummy`.
- [x] CI-friendly command is documented in `README.md` or `AGENTS.md`.
