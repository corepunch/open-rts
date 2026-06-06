# Implement Dark Reign Model Products

## Context

Dark Reign is useful for verifying the plugin/model architecture because its
rules expose both prerequisites and makers in source data:
`SetPrereqs(...)` and `SetMaker(...)`. The model should support those concepts
without duplicating renderer-side gameplay logic.

## Scope

- [x] Preserve Dark Reign SCN team ownership for starting units.
- [x] Load `SetCredit(...)` into model player resources.
- [x] Add actor ids for Freedom Guard construction rig and headquarters.
- [x] Add a first product slice from original rules:
      `FGConstructionCrew` and `fh1`.
- [x] Track `SetMaker(...)` separately from prerequisites in model products.
- [x] Add model command support for Dark Reign product creation.
- [x] Emit a Dark Reign model UI script with `btn`, `pic`, and `text` nodes.
- [x] Add headless tests for building an HQ and producing a construction rig.
- [ ] Parse product definitions from `deftxt/UNITS.TXT` and `deftxt/BUILD.TXT`
      instead of keeping this initial slice static.
- [ ] Add production queues and build times.
- [ ] Add explicit building placement commands and footprint validation.
- [ ] Add more Freedom Guard and Imperium product definitions.

## Acceptance Criteria

- [x] Dark Reign products are available through `rts_game_model_products`.
- [x] Dark Reign product availability respects makers and prerequisites.
- [x] Dark Reign production uses the same `RTS_GAME_COMMAND_ACTIVATE_UI_BUTTON`
      path as Dark Colony.
- [x] Headless tests prove the plugin architecture works for a second game.
- [ ] Renderer clicks are routed through the model command API.
