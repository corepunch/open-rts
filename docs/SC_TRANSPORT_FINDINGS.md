# StarCraft transport unload findings

## Evidence and scope

Behavior below is checked against the pinned OpenBW source at commit
`4b046d5f65302b10cb0a745f0fecd37ec85b20a8`, especially
[`bwgame.h::unit_unload`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L2957-L2982),
[`bwgame.h::order_Unload`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L4798-L4817), and
[`bwgame.h::find_unit_placement`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L15734-L15915).
OpenBW is behavioral reference evidence, not a new retail executable trace.

## Confirmed current behavior and corrections

- Mobile carrier unload now terminates its order if it is disabled or the
  passenger cannot be placed. The passenger remains aboard and may be
  unloaded by a later order. Previously, an impossible `drop()` was retried
  every tick.
- Placement first tests the container center. If terrain there cannot fit the
  passenger, OpenBW returns failure before displacement. If a unit blocks the
  center, it samples 8-pixel candidates inside the `[-128,+127]` pixel bounds.
  The local implementation applies the same box and terrain-before-unit rule
  when the origin bbox is inside the map. If it crosses the map boundary,
  placement proceeds to displacement search instead of failing at origin;
  Recall retains its separate eight-cell search in `p_spell.c`.
- Bunker unloading remains immediate, uses the Bunker center as its origin,
  and fails while disabled. Bunker passengers do not receive mobile-carrier
  cooldowns. The container is kept as an occupancy blocker during death
  ejection even after its HP reaches zero.
- Addressed passenger unloads use the same disabled-container checks as
  Unload All. A disabled Bunker destroyed before it can unload fails
  `unit_unload`; its cargo dies, matching the pinned OpenBW kill path.
- Displaced candidates are sampled on the global 8-pixel grid.
- Successfully unloaded units clear movement/attack targets and return to
  their spawn/idle state. Mobile transport passengers' weapon cooldown is
  reset through `G_WeaponCooldown`, which applies current speed effects and
  upgrades.
- OpenBW `unit_unload` sets the mobile container's main-order timer to 15.
  Protoss Reavers receive their own main-order timer of 30; other mobile cargo
  receive ground/air weapon cooldowns and a separate 30-frame spell cooldown.
  The local runtime has no serialized per-unit spell cooldown field, and the
  relevant save/hash layout is concurrently owned elsewhere, so these three
  timers remain incomplete. Do not emulate spell cooldown by borrowing a
  weapon or status timer. Follow-up files are `games/starcraft/mobj_data.h`
  (runtime/hash fields), `games/starcraft/p_save.c` and
  `tests/starcraft/test_save.c` (serialization/layout), `games/starcraft/p_units.c`
  (15/30-frame countdown), and `games/starcraft/p_spell.c` (spell gate).
- OpenBW's Bunker death path invokes `unit_unload` for each passenger and kills
  the passenger when placement fails (`bwgame.h` 15365–15369). The local
  Bunker death path uses the same center search and retains its existing
  kill-on-failure behavior; mobile transport death still kills cargo.

## Reproduction

`tests/starcraft/test_abilities.c::transport_unload_failures` covers disabled
carrier and invalid-origin-terrain failure despite nearby free cells,
passenger retention, global-grid displacement from an occupied origin, idle
state, and a modified weapon cooldown.
`tests/starcraft/test_units.c::bunker` covers disabled addressed Bunker unload,
center-origin placement, ordinary Bunker death ejection, and cargo death when
a disabled Bunker is destroyed.
Run `make test-starcraft` for the StarCraft suite.

The three unimplemented mobile timers above are pending a serialized-state
change and focused save/hash coverage. The current navigation API cannot
directly check reachability from an unloaded passenger's origin:
`P_NavReachable` rejects a Bunker center because the building blocks that cell,
while `P_NavPlan` performs a full plan and may relocate an unreachable goal.
A bounded connectivity query that handles a blocked container origin is still
needed; the current unload search does not reject a free placement isolated
from the passenger by terrain. Candidate ordering favors nearest valid spots;
OpenBW's exact collision-finder traversal order is not reproduced.
