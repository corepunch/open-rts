# Implement Dark Colony Unit Production From Buildings

## Context

Once buildings exist in the model, production buildings need queues that train
units and spawn them into the world. This must be model-side so tests and the
client use the same behavior.

## Scope

- [ ] Identify production building capabilities:
      Exo Center/Exploiter, Barracks/Trooper, Robot Factory/Reaper/Barrager,
      Science Pod/Sentinel/Osprey/Medi-craft where applicable.
- [ ] Add per-building production queue state.
- [ ] Add model command to enqueue a product from a selected/target building.
- [ ] Spend resources at enqueue time or start time.
- [ ] Advance queue progress during simulation ticks.
- [ ] Spawn produced unit near the building with collision-safe placement.
- [ ] Add rally point support if scope allows, otherwise create a follow-up.

## Acceptance Criteria

- [ ] Selecting a production building shows valid unit products.
- [ ] Clicking a product enqueues it if prerequisites/resources allow.
- [ ] Queue completes after deterministic simulation time.
- [ ] Produced units appear owned by the player and are selectable/renderable.
- [ ] Spawn placement avoids blocked/occupied cells.

## Tests

- [ ] Barracks trains a Trooper in headless simulation.
- [ ] Exo Center trains an Exploiter.
- [ ] Insufficient resources prevent enqueue.
- [ ] Queue progress advances only when simulation ticks.
- [ ] Spawn fallback finds a nearby free cell if the preferred exit is blocked.
