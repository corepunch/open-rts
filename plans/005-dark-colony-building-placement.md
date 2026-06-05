# Implement Dark Colony Building Placement Flow

## Context

The Dark Colony sidebar already draws a BUILD plate and command buttons, but
there is no interactive construction mode. Building placement needs to be
model-driven and testable headlessly, with client UI translating clicks into
model commands.

## Scope

- [ ] Add BUILD sidebar mode.
- [ ] Show building product buttons in the sidebar.
- [ ] Select a product and enter placement mode.
- [ ] Draw a placement ghost in the client.
- [ ] Validate placement against map bounds, terrain, vents, units, and
      building footprints.
- [ ] Spend resources on successful placement.
- [ ] Insert the building into model state.
- [ ] Support cancel/refund behavior if a queued or ghost placement is
      cancelled.

## Acceptance Criteria

- [ ] User can select Exo Center/Barracks/etc. from the sidebar.
- [ ] Valid placement creates an owned building.
- [ ] Invalid placement is rejected without resource loss.
- [ ] Footprint blocks movement/other placement after construction.
- [ ] The system is driven by model commands, not direct renderer-side mutation.

## Tests

- [ ] Headless valid placement succeeds and subtracts cost.
- [ ] Headless insufficient-resource placement fails.
- [ ] Headless blocked-terrain placement fails.
- [ ] Headless occupied-footprint placement fails.
- [ ] Client dummy-video smoke check still renders after placement.
