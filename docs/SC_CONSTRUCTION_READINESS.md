# StarCraft construction readiness

StarCraft foundations now use the existing model readiness predicate for
per-building services. An incomplete Hatchery does not produce Larva, and an
incomplete Pylon does not contribute psi. In-place Zerg building morphs keep
their existing operational behavior: their production is not marked as a
placed foundation, so the Hatchery remains ready while it morphs into a Lair.

Incomplete armed foundations cannot acquire targets or fire. The StarCraft
combat target finder checks only the placed-building production signature; it
does not use the broader model-ready predicate, whose animation-group rule also
covers worker work poses. The guard blocks explicit and automatic attack paths
until the foundation's placed production record is removed on completion.
An unfinished grounded foundation still reveals sight at radius 4, as OpenBW
does, but contributes no detector coverage before completion.

Evidence is the pinned OpenBW source in `REFERENCES.md`. `secondary_order_SpawningLarva`
is the completed Hatchery's Larva production path (`bwgame.h` around 7899), and
`order_IncompleteWarping` keeps a Protoss foundation incomplete while updating
its HP/shields until `complete_unit` (`bwgame.h` around 6662–6708). OpenBW only
updates regeneration and Terran low-HP burn in its completed-unit branch
(`bwgame.h` around 1160–1178). Its `order_InitializePsiProvider` registers a
Pylon's psi provider (`bwgame.h` around 6710–6724); local foundations now wait
for completion before entering the psi lookup.

`tests/starcraft/test_zerg.c` checks Larva production and psi before/after
foundation completion, plus the existing in-place Lair morph. The dedicated
`tests/starcraft/test_foundation_readiness.c` checks that a Missile Turret
foundation neither auto-acquires nor fires, then can acquire and fire after
completion. It also checks that incomplete Hatcheries and Creep Colonies do
not provide creep, incomplete Command Centers do not accept an SCV's return
cargo, incomplete Missile Turrets do not detect a cloaked unit, and incomplete
Bunkers reject boarding. Each service is checked again after the construction
record is removed. The same test keeps a completed Hatchery operational during
its in-place Lair morph. Its sight fixture has no Marine: an incomplete turret
reveals the cell four tiles away, not the cell five tiles away. The separate
cloak fixture verifies detector coverage appears only after completion. The
resource fixture carries eight minerals through an SCV return trip and checks
they enter player stock after the depot is complete.

The additional source checks are OpenBW's completed-unit branches:
`secondary_order_SpawningLarva`/creep provider at `bwgame.h` 805–810,
`unit_returnCargo` at 2191–2198, detector update at 12672–12676, and
`unit_can_load_target` at 1789–1791. Local readiness guards are at
`games/starcraft/p_zerg.c` (creep), `play/p_mobj.c` (harvest return),
`play/p_sight.c` (unfinished-building radius and detector sight), and
`games/starcraft/p_units.c` (Bunker boarding). The unrelated shared
`sc_place_near` placement path is unchanged.

## Service audit against OpenBW (issue 99, item 2)

"Foundation" is a placed building under construction. "Morph" is an in-place
Zerg building morph (Hatchery to Lair or Hive, Creep Colony to Sunken or Spore),
where OpenBW clears `status_flag_completed` but keeps the old unit type.
Local status was checked by `tests/starcraft/test_service_audit.c`,
`test_foundation_readiness.c`, `test_zerg.c` and `test_construction.c`.

| Service | OpenBW function and rule | Local status |
| --- | --- | --- |
| Sight radius | `unit_sight_range`: a grounded, incomplete, non-morphing building sees 4 tiles; a morph keeps its normal radius | Matches (`p_sight.c`; foundation test; Lair morph keeps its sight set) |
| Detector | `unit_can_detect` needs `u_completed` | Matches (turret foundation) |
| Supply provided | `add_completed_unit` at `complete_unit`; a morph never removes it until `morph_unit` swaps the type | Matches (foundation excluded; supply unchanged through a Lair morph) |
| Psi power | `order_InitializePsiProvider` after completion | Matches |
| Creep | `zerg_building_start_construction` calls `add_creep_provider` (footprint only until `unit_type_spreads_creep` with completion); `set_creep_building_tiles` at completion; spreading by `secondary_order_SpreadCreep` | Matches: per-tile state in `p_creep.c` (see the creep section below); a foundation and a morphing Hatchery or Colony cover their footprint only |
| Weapons | main order `IncompleteBuilding`, `IncompleteWarping` or `IncompleteMorphing` never runs the attack order | Matches (turret foundation) |
| Resource depot | `unit_is_active_resource_depot`: completed, Lair, Hive, or a Hatchery that is morphing | Matches (foundation rejects return cargo; morph keeps it) |
| Gas harvesting | `unit_can_gather_gas` needs a completed refinery | Matches (nothing mined from an Assimilator foundation, gas flows after completion) |
| Bunker boarding | `unit_can_load_target` needs `u_completed` | Matches |
| Shield Battery | `unit_can_use_shield_battery` and `update_unit_energy` need `u_completed` | Was different: a foundation battery recharged shields and regenerated energy. Fixed (`sc_foundation`, two guards in `p_combat.c`) |
| Train, research, upgrade, tech, orders | `unit_can_build`, `unit_can_research`, `unit_can_upgrade`, `unit_can_use_tech`, `unit_can_receive_order` (rally excepted) all need `u_completed` | Matches: a foundation or a morphing Hatchery refuses a Queen and a research order, accepts them once the morph ends |
| Larva | `secondary_order_SpawningLarva` has no completion test | Matches (larvae continue through a Lair morph, none from a Hatchery foundation) |
| Terran add-on under construction | `secondary_order_BuildAddon` steps the add-on unit with `resume_building_unit` | Not modelled: the local add-on appears at completion, so it has no HP ramp and cannot be attacked or cancelled mid-build |
| Auto-target priority | `unit_target_attack_priority` ranks an incomplete target one step lower | Not modelled for any target (no priority model in `p_combat.c`, which another agent owns) |

Commands: `env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make -o
starcraft-maps test-starcraft` (25 passed, 0 failed).

## Creep growth and recession (issue 99, item 3)

Creep is per-map tile state in `games/starcraft/p_creep.c`, ported from the
pinned OpenBW functions named below. The old stateless predicate (instant radius
10 and 5 circles) is gone; placement (`sc_ground_ok`) reads `sc_creep_at`. The
renderer has no creep art to rewire: the world draws terrain tiles only, and the
MTXM creep bit stays masked, so nothing there reads the state.

Rules, all confirmed in `bwgame.h`:

- Tiles: bit `flag_has_creep` and `flag_creep_receding`. A map starts with creep
  on every tile whose CV5 group carries flag 0x40 (the MTXM bit 15 of `tile_id`
  is never set: `has_creep` shifts it out); the bottom row and the first and
  last five tiles of the row above it lose creep and become unbuildable.
- `tile_can_have_creep`: not unbuildable, not partly walkable, and the tile
  below is not unbuildable (the last row is exempt from that test).
- `get_max_creep_bb`, `unit_type_spreads_creep`: Hatchery (completed), Lair,
  Hive, Creep Colony (completed), Spore and Sunken Colony cover the ellipse
  `dx*dx*25 + dy*dy*64 <= 320*320*25` (pixels from the building centre to the
  tile centre, 10 by 6.25 tiles); any other Zerg building only its footprint.
- `secondary_order_SpreadCreep`: every 16 frames (timer 0 spreads at once, then
  15) `spread_creep` runs with the Hive's box for every provider, a Creep Colony
  included. It collects creep-free tiles that can hold creep, are not occupied
  (`flag_occupied`, the footprints of grounded buildings, resources included),
  lie in the ellipse and touch at least one creep tile, buckets them by creep
  neighbours (1 to 8), scans row by row, and takes
  `bucket[(lcg_rand(26) >> 4) % size]` from the fullest bucket. It ends only
  when nothing was found and no occupied candidate was skipped.
- Who runs it: `order_InitCreepGrowth` (the idle order of exactly Hatchery,
  Lair, Hive and Creep Colony) sets the secondary order on `complete_unit`.
  A foundation, a drone-built Hatchery or Colony not yet complete, and a Sunken
  or Spore Colony created whole never start it (`StoppingCreepGrowth` has no
  handler: the `default:` of the order switch ignores it). A morph keeps
  whatever secondary order the building had and resets `creep_timer` at the end
  of the morph (`reinitialize_unit_type`); during a morph the unit still has
  its old type and is not completed, so it spreads (the order runs regardless)
  but sustains only its footprint.
- Completion (`order_IncompleteMorphing` last state): `add_creep_provider`
  (receding tiles under the provider stop receding) and
  `set_creep_building_tiles`: the footprint, and for a unit type with flag
  0x80000000 (Hatchery, Lair, Hive) a ring two tiles wider left and right and one
  above and below without its four corners; tiles that cannot hold creep or
  are occupied by another building are skipped. A creep-requiring non-provider
  returns at once.
- Recession: `remove_creep_provider` (on `kill_unit`, after the unit left the
  finder and its tiles were freed) marks every creep, non-receding,
  non-occupied tile of the dead provider's area that `creep_is_being_sustained`
  does not cover (any live Zerg building, completed ones with the ellipse) as
  receding and files it in the list of its creep-neighbour count (0 to 8,
  `push_front`); `set_tile_creep` moves a receding neighbour to the front of
  its new list when its count changes. `recede_creep` runs first in
  `process_frame`: while its timer is 0 it sets the timer to
  `{1,3,5,6,7,8,9,9,9}[free_entries >> 7]` (free = 1024 minus receding tiles)
  and removes one tile, the entry `lcg_rand(27, 0, n - 1)` of the first
  non-empty list from list 0 up, so the fewest neighbours go first, ten frames
  apart while at most 256 tiles recede. An Extractor death clears its
  footprint at once (`remove_extractor_creep`). A building standing on receding
  creep takes no damage: the only readers of `flag_has_creep` are placement,
  `is_walkable`, larva movement (`order_Larva` kills a larva off creep),
  Spider Mine validity and larva spawning; none touches hit points.

The three points the earlier write-up left open, resolved from the source:

1. Dead-unit retry. `remove_creep_provider` returns false when the 1024 entries
   are used up and `kill_unit` sets `status_flag_4000`; the only reader is
   `update_dead_unit`, which calls `error("fixme some creep stuff?")`, and
   `check_dead_unit_timer` is only ever assigned 0. There is no retry to copy.
   Here the tiles that did not fit stay (a state OpenBW itself aborts on).
2. Pre-placed versus created. `create_initial_unit` (map start, CHK UNIT order,
   also melee start units) calls `spread_creep_completely` before the unit is
   placed: footprint, then `spread_creep` until it returns false, so a
   pre-placed Hatchery, Lair, Hive, Colony, Spore or Sunken Colony (and any
   creep-requiring building) has its full ellipse at frame 0, blocked only by
   buildings created earlier in the file. A building made by a trigger
   (`trigger_create_unit`) or by construction takes the footprint and ring and
   grows from there. Local: `sc_creep_initial`, called by `sc_spawn_things`;
   anything else is noticed by `sc_creep_ticker`.
3. RNG. `spread_creep` calls `lcg_rand(26)` and `recede_creep` calls
   `lcg_rand(27, 0, size - 1)` = `(lcg_rand(27) * size) >> 15`. The source id
   only feeds a statistics counter, so both draw from the one stream that
   `sc_random` implements.

Local differences that cannot be closed without a retail trace or per-frame unit
updates: buildings are noticed once per engine tic (a spread or recession starts
at the first native frame of the tic after a completion or death, with 24 native
frames mapped onto 30 Hz tics), the shared LCG sees the other local draws in a
different order than OpenBW's unit order, `is_walkable` for creep over partly
walkable tiles and `order_Larva` are not modelled (local larvae do not wander),
and the visibility tests in `can_place_building` are not applied to creep.

Tests: `tests/starcraft/test_creep.c` (hand-derived first steps, final
ellipse, blocked tiles, completion, morph, pre-placed state from code and from a
CHK, recession order and timing, sustained creep, placement, determinism,
save/load mid-growth and mid-recession, malformed state). Save layout 22.

Commands: `env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make -o
starcraft-maps test-starcraft`.
