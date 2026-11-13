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
- Placement follows `find_unit_placement` literally in
  `games/starcraft/p_place.c` (`sc_unload_position`). It first tests the
  container center: with no unit of the passenger's layer in the way and the
  passenger's inclusive box inside `{0,0,W,H-32}` (the last 32 pixel rows are
  not playable), terrain that cannot hold it fails at once (`terrain_displaces_unit`
  is false for unloads), else the center is used. A blocker or an out-of-map box
  starts the ring search. Rings start at 8 pixels, or at the passenger's
  `from.x + blocker.to.x + 2` by `from.y + blocker.to.y + 2` when something
  blocks the origin, and grow by 16 until all four sides pass the `[-128,+127]`
  bounds (clamped to the map and to `H-32-1`); that last ring is not walked, so
  the farthest reachable row is -120, not -128 (this corrects an earlier note
  that the -128 edge is a reachable candidate). Each ring is the rectangle from
  `pos - (width,height)`, size `2*width` by `2*height`, floored/ceiled to the 8
  pixel grid and clamped to the bounds, walked bottom edge left to right,
  right edge upward, top edge right to left, left edge downward. A ring wider
  than twice the passenger's width skips the first
  `(from.x+to.x+1+7)/8*8` pixels of the bottom edge. A blocked spot is jumped
  to the far side of its blocker and back onto the 8 pixel grid, so scans are
  on the global grid only until a clamped bound or a jump re-phases them.
  The candidate must keep its box in the playable map, clear of blockers, fit
  on the ground and be reachable. The old nearest-first sort over a 33x33
  grid is gone. Unit tests hand-derive the first spots of a symmetric
  Marine-on-Marine ring (1000,1024), the jump to (1024,1024) and the turn
  onto the right edge (1024,1000), and pass.
- Reachability: OpenBW compares static region groups of the container's pixel
  and the candidate (`is_reachable`, only for ground passengers). Here that is
  the navigation component of the passenger's movement class from
  `P_NavRegions`, built once per terrain snapshot and read in O(1), never a
  path search. A container cell blocked only by a building (a Bunker) has no
  component, so the components around the building's footprint stand for it,
  because OpenBW's regions ignore buildings. Differences kept: terrain fit
  is the engine's 32 pixel cell test, not OpenBW's pixel contours; the
  first-found blocker that sets the initial ring size and jump distance is
  the first in thinker order, where OpenBW's is first in its spatial finder;
  OpenBW's no-collide units, power-ups and the Starport/Robotics/Stargate
  exceptions (flyers only) are not modelled.
- Cost: a search with nowhere to go walks 511 spots for a Marine over a
  Marine (the tests count them: identical on 128x128 and 256x256, against
  1089 for the old grid), each tested against the region table, plus one
  scan of the unit list per spot (512). `sc_placement_stats`
  exposes the counters.
- Bunker unloading remains immediate, uses the Bunker center as its origin,
  and fails while disabled. Bunker passengers do not receive mobile-carrier
  cooldowns. The container is kept as an occupancy blocker during death
  ejection even after its HP reaches zero.
- Addressed passenger unloads use the same disabled-container checks as
  Unload All. A disabled Bunker destroyed before it can unload fails
  `unit_unload`; its cargo dies, matching the pinned OpenBW kill path.
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
- Still not modelled: acid spores are now counted in the
  modified cooldown, but nothing applies a spore yet. A Warbringer's weapon timer uses
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
transports.

`tests/starcraft/test_placement.c` covers placement: hand-derived ring order
and blocker jumps, an isolated cell versus a connected one, a Bunker plugging
a one-cell gap, map edges and corners including the unplayable bottom 32
rows, and the failure-path candidate count on 128x128 and 256x256.
`test_abilities.c::transport_unload_failures` now expects the -120 row.

## Verification run

- `make build/bin/starcraft`, then `env SDL_VIDEODRIVER=dummy TEST_JOBS=4
  TEST_BUDGET=60 make -o starcraft-maps test-starcraft`: 25 tests, 0 failed
  (baseline 23; new binaries `test_transport` and `test_placement`).
- `env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make test-z`: 27 tests,
  0 failed (the shared `attack_blocked` hook is NULL there).
- `env SDL_VIDEODRIVER=dummy build/bin/starcraft --check`: smoke check OK.
- Mutation checks: with the hook unregistered, `reaver_delay` fails; with the
  building-region fallback removed, the Bunker connection test fails.

## Second batch: modified weapon cooldown

OpenBW `get_modified_weapon_cooldown` against local `sc_modified_cooldown_frames`
and `G_WeaponCooldown` (`p_combat.c`):

| Term | OpenBW | Local before | Local now |
| --- | --- | --- | --- |
| Acid spores | first: `cooldown += max(cooldown / 8, 3) * count` | absent | same (`sc.acid[9]`) |
| Stim Packs | `+1` to `mod` | `+1` | same |
| Cooldown upgrade | `+1` to `mod` (Adrenal Glands, Devouring One) | `+1` | same |
| Ensnare | `-1` to `mod` | `-1` | same |
| Application | `mod > 0`: `/ 2`; `mod < 0`: `+ cooldown / 4` | same, on milliseconds | same, on whole frames |
| Clamp | always `5..250` | only when `mod != 0` (plain weapons unclamped) | always, except Interceptor/Carrier pass |
| Jitter | attack path adds `(lcg_rand(12) & 3) - 1` after the clamp (`try_attack_something`/`attack_unit_fire_weapon`) | none | `(sc_random() & 3) - 1`, one roll per shot |
| Unload | `unit_unload` uses the modified value, no roll | modified, no acid | same, acid included |

- RNG source: `lcg_rand` is the shared synchronized LCG, which is `sc_random`
  here (`mission->rng`); a shot takes exactly one roll. The shared attack path
  (`play/p_mobj.c start_attack_cooldown`) used to call the hook twice per shot;
  it now calls it once (behavior-neutral for other games whose hooks are pure).
- Order difference: OpenBW rolls the cooldown before creating the bullet; here
  it is rolled after the hit roll of the attack hook. The streams are the same
  LCG but the interleaving differs; a retail replay comparison would need the
  attack path reordered, which is not done.
- The Interceptor pass (30 frames) and a Carrier's relaunch are this engine's
  own timings (weapons.dat gives 1 and 60), so they get neither clamp nor roll.
- Acid spore state: nine byte timers of 150 frames (`add_acid_spore` renews the
  one nearest to expiring when full), decayed per 24 Hz frame as in
  `update_unit_values`. Saved with the mobj and hashed; save layout 22.
- Not applied yet: nothing adds a spore (the Corrosive Acid bullet hit,
  `hit_type_corrosive_acid`, is projectile code owned elsewhere), the +1 damage
  per spore in `weapon_deal_damage`, spore detection, removal by Restoration.
- Tests: `tests/starcraft/test_cooldowns.c` (every term, the clamp boundaries at
  5/250, one/five/nine spores, Zergling with and without Adrenal Glands, the
  roll's range and single draw, shot-to-shot tics in the running attack path,
  unload value, spore decay, save/hash).

## Second batch: spell cooldown after a cast

- OpenBW `order_Spell` is the only reader of `spell_cooldown` (besides the
  per-frame decrement in `update_unit_values` and the reset in unit creation).
  When the caster is in range, the cooldown is 0, `unit_can_fire_weapon` holds
  and it is at its move target, the cast starts and sets
  `spell_cooldown = get_modified_weapon_cooldown(u, weapon) + (lcg_rand(49) & 3) - 1`,
  `weapon` being the order's own weapon row. The 30-frame value after a mobile
  unload (`unit_unload`) is the other writer.
- Orders that go through `order_Spell`: Fire Yamato Gun, Lockdown, Dark Swarm,
  Parasite, Spawn Broodlings, EMP, Psionic Storm, Irradiate, Plague, Consume,
  Ensnare, Stasis Field, Restoration, Disruption Web, Optical Flare, Maelstrom.
  Mind Control, Feedback, Hallucination, Defensive Matrix, Scanner Sweep, Recall,
  nukes, mines and the self-casts have their own order functions and ignore it.
  Restoration, Disruption Web, Optical Flare and Maelstrom are Brood War techs
  that this port does not model.
- Weapon per order (native `arr/orders.dat`, weapon column, checked by row):
  Yamato 30, Lockdown 32, EMP 33, Irradiate 34, Parasite 56, Spawn Broodlings 57,
  Ensnare 58, Dark Swarm 59, Plague 60, Consume 61, Stasis 83, Psionic Storm 84.
  weapons.dat cooldowns: Yamato 15, Storm 45, the rest 1 (so 5 after the clamp).
- Implemented in `p_spell.c` (`spell_weapon`, `rearm_spell_cooldown`): the
  re-arm happens right after `cast_now`, uses the same modified cooldown as
  weapons (Stim, Ensnare, spores, upgrades) and one `sc_random` roll, and adds
  back the frames the ticker is about to count off so the next cast waits the
  exact count. A command that casts at once (in range) is between two tics;
  the next cast is allowed on the tic that brings the frame total to the timer.
- Tests (`test_cooldowns.c`): two casts by one caster (second held, energy kept,
  fires on exactly `timer` frames), a second caster unaffected, Ensnare
  lengthening it, Hallucination/Defensive Matrix/cloak unaffected and not
  touching the timer, and a Yamato cooldown saved and loaded mid-way.
  Three AI tests in `test_units.c` that chain two spells from one caster in a
  single think now clear the timer between them.
- Observed, not investigated: a save taken while a Psionic Storm area exists
  failed `G_LoadGameBlob` in the first draft of the save test (the final test
  uses Yamato, which creates no area).
