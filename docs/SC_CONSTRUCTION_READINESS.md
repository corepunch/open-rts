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
| Terran add-on under construction | `secondary_order_BuildAddon` steps the add-on unit with `resume_building_unit` | Matches: the add-on is an incomplete site from the start (see the add-on section below) |
| Auto-target priority | `unit_target_attack_priority` ranks an incomplete target one step lower | Not modelled for any target (no priority model in `p_combat.c`, which another agent owns) |

Commands: `env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make -o
starcraft-maps test-starcraft` (25 passed, 0 failed).

## Terran add-on under construction (issue 99)

Pinned OpenBW `bwgame.h` and `actions.h` (commit 4b046d5f), confirmed by
reading the functions named here:

- `order_PlaceAddon`: once the building is grounded at its spot, the add-on
  must pass `player_has_supply_and_resources_for` and `can_place_building`;
  otherwise nothing is created and nothing is paid. Then `create_unit` makes
  it (an incomplete building: a tenth of its HP, `hp_construction_rate` from
  `initialize_unit_type`), the cost is taken, the building gets secondary
  order `BuildAddon` with the add-on as `current_build_unit`, and the add-on
  takes `IncompleteBuilding`.
- `secondary_order_BuildAddon`, each frame of the building: nothing while
  `unit_is_disabled`; if the building is no longer grounded and the add-on is
  incomplete, `cancel_building_unit(addon)`; otherwise
  `resume_building_unit(addon)` (HP step, then the timer; the frame that finds
  it at zero steps once more and completes) and, once complete,
  `connect_addon`. `building.addon` is set only there, so `unit_has_addon`
  (the tech checks) never sees an incomplete one.
- `unit_is_building_addon` (secondary BuildAddon, grounded, add-on incomplete)
  makes `unit_is_constructing` true: the building trains, researches and
  upgrades nothing meanwhile. Lift Off is not refused by
  `unit_can_receive_order`; it cancels the add-on through the rule above.
- Cancel: `action_cancel_addon` (action 52, the building selected alone,
  completed, grounded) and `action_cancel_building_unit` (action 24, the
  add-on itself) both call `cancel_building_unit`, which for a grounded
  Terran building is `partially_refund_unit_costs` (3/4 of each, rounded
  down) and `kill_unit`. stat_txt.tbl has the "Cancel Addon" label.
- Kill path (`destroy_unit_impl` area): a building dying with an incomplete
  add-on cancels it (3/4 back); a finished add-on is abandoned
  (`building_abandon_addon`: neutral, LiftOff animation). `make_unit_neutral`
  cancels the same way. `give_unit_to` passes the incomplete add-on to the new
  owner with the building.
- `order_InfestedCommandCenter` abandons only a finished add-on, then
  `set_secondary_order(Nothing)` clears `current_build_unit` without a
  cancel: the add-on stays its old owner's, incomplete, and nothing steps it.

Local model (`games/starcraft/p_prod.c` `sc_addon_ticker`, run from
`G_ProductionBegin`): the queued add-on order (paid at queue time, as before)
becomes a placed construction site on the next production tick, linked by
`sc.parent` and `sc.addon` from the start; `sc_addon_ready` is the completed
add-on for tech checks, `sc_addon_building` the one under construction. If the
place is taken by then, the full cost comes back (OpenBW never takes it).
`G_ModelCanProgressConstruction` runs the BuildAddon rules on the site, and
`G_ModelUpdateConstruction` steps it as an SCV building. No new saved fields:
the link, the site's production and `sc.construction` were already saved and
hashed, so `G_SaveLayout` stays 28; an older save with a queued add-on is
converted on its first tick. The Factory card shows only Cancel Addon.
`tests/starcraft/test_addon_construction.c` covers the ramp (Machine Shop
19200 + 288 per frame in 1/256 HP, 601 steps), damage, completion and tech,
both cancels (37/37 back), Lift Off, the building's death (incomplete
cancelled, finished one neutral), a destroyed site (no refund), a blocked
place (full refund), infestation leaving it frozen, and a save round trip.

Not modelled: the add-on's progress shown on the building's status panel
(the local panel shows it only when the add-on is selected); an SCV Repair
order on an incomplete add-on (OpenBW `order_Repair` falls into
`ConstructingBuilding`, which would also step it; the default right-click
excludes add-ons); the retail button set for a building whose add-on is
under construction beyond the Cancel Addon button (not traced in
starcraft.exe); infestation is not refused for a building putting up an
add-on in OpenBW either, and the frozen add-on cannot be finished locally.

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
save/load mid-growth and mid-recession, malformed state). Save layout 22 (now 26, see the larva section).

Commands: `env SDL_VIDEODRIVER=dummy TEST_JOBS=4 TEST_BUDGET=60 make -o
starcraft-maps test-starcraft`.

## Larva model versus OpenBW (issue 99 follow-up)

Code: `games/starcraft/p_larva.c`; tests: `tests/starcraft/test_larva.c`. Save
layout 25 to 26 (a hatchery's `larva_ms` now packs timer, run phase and morph
mark; a larva keeps phase, side and move target in `sc.order`; both are
validated in `G_SavedObjectValid`).

| Aspect | OpenBW | Local | Status |
| --- | --- | --- | --- |
| Spawn timer | `secondary_order_SpawningLarva`: runs only when `order_process_timer` is 0 (once per 9 frames), spawns at timer 0 then sets 37 (3 after a failed spawn) | same; spawns at native frames 1, 343, 685 | done; absolute phase per unit untraced (OpenBW re-phases every 150 frames from list order) |
| Reset rule | `initialize_unit_type` zeroes `larva_timer` on morph end; timer frozen at the cap and while the order is not set | morph mark resets timer at morph end; frozen at cap and while incomplete | done (old code reset the timer to 342 frames at the cap) |
| Cap | 3 larvae with `connected_unit == hatchery`, not hidden, found in a 256 px square | parent id, centre within 256 px | done |
| Spawn position | `get_spawn_larva_position`: sides 3,0,2,1 scored by `larva_spawn_side_values`, creep and occupied/walkable tests, fallback offsets | same with all side values 0: below the box centre, 10 px under it; larvae start stacked | side values NOT done (worker return needs a hook in shared play code) |
| Side | `spawn_larva` order_state 0-3 | same (`sc.order.time` bits 4-5) | done |
| Wander | `order_Larva` every 9 frames: `lcg_rand(20)`, bits 8 and 0x80 pick a +-10 px diagonal, target needs map bounds and creep, must stay on the larva's side of the hatchery box, else side point 10 px out | same, `sc_random` | done |
| Return to hatchery | beyond 10 px of the hatchery box (`units_distance`, `xy_length`) `move_to_target` | walks to the nearest point of the box grown by the larva's extent | stop position approximated |
| Movement | iscript/flingy movement | straight line at units.dat speed (2 px/frame), exact arrival | dynamics untraced |
| Death off creep | at a run, at its move target (or a creepless target not the hatchery), position tile without creep: `kill_unit` | same (`orphan` test) | done |
| Hatchery dies | `destroy_unit_impl` -> `remove_target_references` clears `connected_unit`; larvae live on and die by the creep rule | parent lookup fails, larva wanders unrestricted | done (old code killed them at once) |
| Hatchery morph | same unit, larvae stay | same | done |
| Larva used by a command | UI selection; no engine rule in OpenBW | `G_PlayerBuildProduct` takes the first free larva | unknown (needs a retail trace of the AI pick) |
| Cancelled egg | `cancel_building_unit`: egg refunded and killed (cocoon/lurker egg morph back) | same; larva is not returned | done (test_gameplay assertion changed) |
| Melee start | `create_starting_units`: two `spawn_larva`, third by the first run (frame 1) | three at load (shared lobby test pins 9 units at load); same count a frame later | unchanged on purpose |

Changed assertions: `test_zerg` first larva needs tick(2) (the first tic holds
no native frame) and the incomplete-hatchery check uses the packed timer;
`test_gameplay` cancel of an egg now expects the egg dead; `test_creep`
hatcheries are created without a larva order (larvae draw the shared LCG).

Item 3 (creep and walkability for non-Zerg) was not started: `is_walkable`
treats creep as walkable, `sc_ground_ok` already refuses non-Zerg buildings on
creep; no further change was traced.
