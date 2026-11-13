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
| Creep | `zerg_building_start_construction` calls `add_creep_provider` (footprint only until `unit_type_spreads_creep` with completion); `set_creep_building_tiles` at completion; spreading by `secondary_order_SpreadCreep` | Differs: creep is a stateless radius, see the creep section below |
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
starcraft-maps test-starcraft`.
