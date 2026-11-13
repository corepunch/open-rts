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
- OpenBW `unit_unload` sets the container main-order timer to 15 native frames.
  The local runtime now stores and ticks `SC_TIMER_MAIN_ORDER`; mobile unload
  all and addressed unloads both respect it. The timer is part of the object's
  save data and consistency hash, and StarCraft save layout is now 16.
  When the last passenger unloads, the unload order ends immediately, so a
  queued move can start while the container timer continues to count down.
- Remaining passenger timers are not implemented: Protoss Reavers receive a
  30-frame main-order timer; other mobile cargo receive a separate 30-frame
  spell cooldown. OpenBW also sets independent modified ground and air weapon
  cooldowns where those weapons exist, while the local combat runtime currently
  has one shared cooldown. Do not emulate these by borrowing the container
  timer, weapon cooldown or status timer. Reaver's timer requires coverage of
  its main-order/attack lifecycle; spell cooldown needs a verified equivalent
  gate; independent weapon timers require combat-path changes. Save/hash tests
  must cover any further serialized fields and layout bumps.
- OpenBW's Bunker death path invokes `unit_unload` for each passenger and kills
  the passenger when placement fails (`bwgame.h` 15365–15369). The local
  Bunker death path uses the same center search and retains its existing
  kill-on-failure behavior; mobile transport death still kills cargo.

## Reproduction

`tests/starcraft/test_abilities.c::transport_unload_failures` covers disabled
carrier and invalid-origin-terrain failure despite nearby free cells,
passenger retention, global-grid displacement from an occupied origin, idle
state, a modified weapon cooldown, addressed unload rejection during the
15-frame container timer, and Unload All resumption at timer expiry.
`transport_unload_queue_completion` checks that the final passenger clears the
unload order and starts a queued move immediately without clearing the timer.
`tests/starcraft/test_units.c::bunker` covers disabled addressed Bunker unload,
center-origin placement, ordinary Bunker death ejection, and cargo death when
a disabled Bunker is destroyed.
Run `make test-starcraft` for the StarCraft suite.

The unimplemented Reaver/spell and independent weapon cooldowns above are
pending verified runtime gates and focused save/hash coverage. The current navigation API cannot
directly check reachability from an unloaded passenger's origin:
`P_NavReachable` rejects a Bunker center because the building blocks that cell,
while `P_NavPlan` performs a full plan and may relocate an unreachable goal.
A bounded connectivity query that handles a blocked container origin is still
needed; the current unload search does not reject a free placement isolated
from the passenger by terrain. Candidate ordering favors nearest valid spots;
OpenBW's exact collision-finder traversal order is not reproduced.
