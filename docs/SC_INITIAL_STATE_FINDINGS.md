# StarCraft initial cloak and burrow state

## Confirmed

- PyMS `CHKSectionUNIT.py` defines `CLOAK = 1 << 0` and `BURROW = 1 << 1`; its decoder reads `validAbilities` and `abilityStates` as separate 16-bit fields. `CHKSectionUPRP.py` serializes the same fields in the same 20-byte property record. The runtime applies an initial state only when the matching valid and state bits are both set. `In Transit` is bit 2 in both formats and is intentionally not interpreted here.
- OpenBW at the repository's pinned `bwgame.h` commit sets the Burrowed and passively-cloaked status together (`set_unit_burrowed`, lines 3330–34). Its energy update drains cloak/detector energy only when the unit is not passively cloaked (lines 1117–23). Therefore a burrowed spellcaster must not lose energy to cloak upkeep.
- `w_assets.c::animation()` returns the final compiled state. For Burrow, this is the authored hold state: the chain's last state points to itself and has one tic (lines 278–80). `sc_burrow_states[][0]` remains the animation head for actual Burrow orders; initial Burrow uses the returned final state instead. `R_InitSprites()` preserves that state after loading graphics.
- The focused test `tests/starcraft/test_initial_states.c` exercises UNIT cloak/burrow, a trigger-created UPRP-burrowed Defiler, the distinct head/final IDs for both units, passive cloak ticking, and save/restore of status flags and traits.

## Residual: manual toggle without research

OpenBW's `can_use_tech` check for `TechTypes::Burrowing` requires Burrowing researched for ordinary units, with exceptions for heroes and Lurkers (`bwgame.h`, lines 17879–82). It also has a separate `unburrow_unit()` path used by automatic reactions, which does not repeat that research check (lines 12906–11). These facts do not establish whether a player can manually unburrow or decloak a unit whose initial state came from UNIT/UPRP while the technology is unavailable. The implementation leaves the existing manual command gate unchanged pending a direct native trace or a narrower OpenBW command-path proof.

## Manual toggle gate (issue 99, item 4): documented, not changed

Pinned OpenBW checks that decide whether a unit may start Burrow or Cloak
(`unit_can_use_tech`): `u_completed`, not hallucinated, not disabled, tech
available to the player, then per tech: Burrowing needs the research unless the
unit is a hero or a Lurker, and the unit must be one of Lurker, Drone, Infested
Terran, Defiler, Zergling, Hydralisk or the Unclean One, Devouring One and Hunter
Killer heroes; Cloaking Field and Personnel Cloaking need the research unless the
unit is a hero, and the unit must be a Wraith or Ghost respectively. No check reads
the unit's current burrowed or cloaked state, its initial CHK state or its energy:
energy is paid by the cloak upkeep in `update_unit_energy`, which skips passively
cloaked and burrowed units. `unburrow_unit` (automatic reaction to damage) does
not repeat the research check.

Not in the pinned files: the player command handlers that turn a Burrow, Unburrow,
Cloak or Decloak click into an order. They (not `unit_can_use_tech`) decide whether
an Unburrow or Decloak command for a unit that has no research is accepted, and
whether a CHK-granted state counts as owned. `unit_can_receive_order` lets only
`Unburrowing` through for a burrowed unit, but `unit_order_allowed` has no entry for
it, so that function alone cannot be the gate. The local `sc_cast` requires the
research (or hero status) for both directions of both toggles and charges cloak
energy. Without a retail command trace or the OpenBW action source the local gate
is kept: changing it either way would be a guess. The trace to take: issue
Unburrow and Decloak on a trigger-created unit with no research and observe whether
the command card offers and executes them.
