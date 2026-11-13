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
