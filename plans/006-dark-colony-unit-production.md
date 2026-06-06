# Implement Dark Colony Unit Production From Buildings

## Context

Once buildings exist in the model, production buildings need queues that train
units and spawn them into the world. This must be model-side so tests and the
client use the same behavior.

## Scope

- [x] Identify production building capabilities:
      Exo Center/Exploiter, Barracks/Trooper, Robot Factory/Reaper/Barrager,
      Science Pod/Sentinel/Osprey/Medi-craft where applicable.
- [ ] Add per-building production queue state.
- [x] Add model command to request a product from available building rules.
- [x] Spend resources when production succeeds.
- [ ] Advance queue progress during simulation ticks.
- [x] Spawn produced unit near the building with collision-safe placement.
- [ ] Add rally point support if scope allows, otherwise create a follow-up.

## Acceptance Criteria

- [ ] Selecting a production building shows valid unit products.
- [ ] Clicking a product enqueues it if prerequisites/resources allow.
- [ ] Queue completes after deterministic simulation time.
- [ ] Produced units appear owned by the player and are selectable/renderable.
- [ ] Spawn placement avoids blocked/occupied cells.

## Tests

- [x] Barracks trains a Trooper in headless simulation.
- [ ] Exo Center trains an Exploiter.
- [x] Insufficient resources prevent production.
- [ ] Queue progress advances only when simulation ticks.
- [x] Spawn fallback finds a nearby free cell.
