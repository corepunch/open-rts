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
  save data and consistency hash. At the time this transport finding was
  recorded, StarCraft save layout was 16; that version number is historical.
  Construction data later advanced the current layout to 17; see
  [SC_CONSTRUCTION_FINDINGS.md](SC_CONSTRUCTION_FINDINGS.md).
  When the last passenger unloads, the unload order ends immediately, so a
  queued move can start while the container timer continues to count down.
- Passenger timers from `unit_unload` for a mobile container (a Bunker sets
  none; `u_grounded_building(container)` skips the whole block, so Bunker
  exits and Bunker death ejection give no cooldowns, which the tests pin):
  a Protoss Reaver (exactly `Protoss_Reaver`, not the Warbringer) gets a
  30-frame `main_order_timer`; every other passenger gets
  `ground_weapon_cooldown` and `air_weapon_cooldown` set to
  `get_modified_weapon_cooldown` of the weapons of its own hull
  (`unit_ground_weapon`/`unit_air_weapon`, so a Goliath or Siege Tank, whose
  guns sit on the turret subunit, gets none) and a 30-frame `spell_cooldown`.
  All four count down once per frame in `update_unit_values`.
- Where they bite in OpenBW: `main_order_timer` stops the Reaver/Carrier
  attack order (`order_AttackUnit` returns before reacquiring or firing);
  `attack_unit_fire_weapon` reads the ground or air cooldown by target
  flight; `order_Spell` waits in range on `spell_cooldown` (Yamato, Lockdown,
  Dark Swarm, Parasite, Spawn Broodlings, EMP, Psionic Storm, Irradiate,
  Plague, Consume, Ensnare, Stasis Field, Restoration, Disruption Web).
  Hallucination, Recall, Mind Control, Defensive Matrix, Scanner Sweep,
  Infest and Nuclear Strike have their own orders and ignore it.
- Implemented. The engine's single `attack.cooldown_left_ms` cannot hold
  these, and the earlier Reaver patch failed because it spent that cooldown
  and played the sound on a blocked shot. Instead the SC mobj has three more
  frame timers (`SC_TIMER_SPELL_COOLDOWN`, `SC_TIMER_GROUND_COOLDOWN`,
  `SC_TIMER_AIR_COOLDOWN`, beside the existing `SC_TIMER_MAIN_ORDER`; save
  layout 18) and a new optional `gameinfo_t.attack_blocked(attacker, target)`
  hook checked in `P_Attack` and `A_Look` before anything is spent. A blocked
  attempt returns without sound, Scarab, cooldown or loss of the target. The
  hook blocks a Reaver or Carrier on its main-order timer, others on the
  timer matching the target's flight, and any passenger still aboard a mobile
  transport (previously a loaded Marine could shoot from a Dropship once it
  saw an enemy). `step_cast` holds the listed spells in range on the spell
  cooldown without spending energy. The timers are set before the passenger
  enters its idle state, because that state's first action may already fire.
  A timer on its last frame no longer blocks, matching OpenBW's
  decrement-before-orders, so an explicit Reaver attack fires on exactly the
  frame the 30 run out; automatic acquisition adds the idle state's own look
  cadence (0 to 2 frames in the tests).
- Still not modelled: OpenBW sets `spell_cooldown` again after each cast
  (weapon cooldown plus a random 0..3 minus 1) and counts acid spores in the
  modified cooldown; neither exists locally. A Warbringer's weapon timer uses
  the DAT Scarab row, while its launch cooldown keeps the local 60 frames.
  Mobj fields are saved whole behind the layout number and checksum; no
  per-field range check of `sc.timers` exists for any timer.
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

`tests/starcraft/test_transport.c` covers the timers: Reaver exact frames
(explicit and automatic, Move/Stop, blocked-attempt cost, save/hash mid
delay), per-class cooldowns (shared-weapon Marine, ground-only Zergling,
turret-hull Goliath, independent ground/air gating), spell cooldown for
Psionic Storm versus Hallucination, Bunker exit and death ejection,
two passengers in sequence, and disabled, blocked and destroyed
transports. The current navigation API cannot
directly check reachability from an unloaded passenger's origin:
`P_NavReachable` rejects a Bunker center because the building blocks that cell,
while `P_NavPlan` performs a full plan and may relocate an unreachable goal.
A bounded connectivity query that handles a blocked container origin is still
needed; the current unload search does not reject a free placement isolated
from the passenger by terrain. Candidate ordering favors nearest valid spots;
OpenBW's exact collision-finder traversal order is not reproduced.
