# Doom-heritage architecture constraints

GZDoom is the authoritative reference for the simulation core. Any design that
diverges from Doom's thinker/state model must be redone before merging.

## What must stay Doom-shaped

- **mobj_t** individually heap-allocated; no pooling, no flat arrays of structs.
- **thinkercap** doubly-linked list; `P_MobjThinker` is the one lifecycle function.
- **Deferred removal** — set `mobj->remove = true`, call `P_RemoveMobj` next tick.
- **State machine** — `state_t states[]` array; `P_SetMobjState` chains zero-tic
  states immediately on entry; action functions have the `void action(mobj_t *)`
  contract.
- **State groups** — group 2 = walk, group 3 = attack. `tick_actor` in `p_mobj.c`
  uses group to drive seestate/spawnstate transitions during movement.
- All actor categories (units, buildings, towers, aircraft, effects) are ordinary
  mobjs. No separate pools or entity types.

## Combat flow (p_mobj.c)

1. `tick_actor` calls `attack_target_in_range` every tick while unit is moving.
2. If target found (within `attack.range` AND `P_VisibleTo`), unit stops and state
   transitions to `missilestate`.
3. `A_Attack` (called from missilestate) calls `P_Attack` which applies damage.
4. `A_Look` (idle/walk state action) scans by distance only; `P_Attack` rescans
   with visibility check.
5. Cooldown tracked in `mobj->attack.cooldown_left_ms`; `A_Look` skips while > 0.

## What must NOT be added

- Separate unit/building/aircraft/effect pools.
- Synchronization wrappers that copy data between ACTOR_TYPES[] and mobjinfo[].
- Per-category tick loops outside `P_RunThinkers`.
