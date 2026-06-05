# Load Dark Colony Buildings As Model Entities

## Context

Dark Colony `.SCN` files contain buildings and map objects, but the current
loader mostly maps mobile actors and special decorations/vents. Construction
and production need buildings to exist in the model as owned, selectable,
renderable, non-mobile entities.

## Scope

- [x] Add Dark Colony building actor types for human rows 16-22:
      `EXCOPOD`, `BRRKPOD`, `ROBOPOD`, `ROBOPOD2`, `SCNCPOD`, `SCNCPOD2`,
      `RSCHPOD`.
- [ ] Add alien building rows later or split into a follow-up if this gets too
      large.
- [x] Load starting building rows from `.SCN` into `Unit` or a dedicated model
      entity type.
- [ ] Apply owner, health, sprite, footprint, and render traits.
- [ ] Ensure buildings are selectable but not mobile.
- [ ] Ensure buildings block placement/movement according to footprint.

## Acceptance Criteria

- [x] Human02 starting buildings appear in model state.
- [x] Buildings render in the client with correct sprites.
- [ ] Buildings can be selected but cannot receive move orders.
- [ ] Enemy buildings remain enemy-owned and attackable when relevant.
- [ ] Existing mobile-unit selection/movement behavior is unchanged.

## Tests

- [x] Human02 headless test asserts expected building count/types/owners.
- [ ] Selection test distinguishes mobile units from buildings.
- [ ] Movement-order test proves buildings do not move.
- [ ] Footprint/blocking test proves buildings occupy map space.
