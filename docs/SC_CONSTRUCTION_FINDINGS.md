# StarCraft construction lifecycle

## Implemented model

Construction now creates a selectable, damageable StarCraft building object
after the worker reaches the chosen approach position. The object carries its
remaining build time in the existing `production_t` and construction-produced
HP/shields plus Zerg Drone return HP in `sc.construction`. The latter is saved
and hashed with StarCraft's mobj state. StarCraft save layout is 17.

The shared game layer exposes construction lifecycle hooks, but their weak
defaults preserve other plugins' existing worker-build behavior. StarCraft
revalidates placement, requirements and resources when the worker arrives; it
debits costs only when construction starts. A now-invalid site or unavailable
resources ends the pending order without charging, following the order paths
in OpenBW. A site reserves its footprint by existing building collision and
remains incomplete until its build timer reaches zero.

Terran construction keeps the SCV linked to the site. Construction pauses if
the SCV dies, leaves, or loses its construction order. A new SCV can resume a
same-owner unfinished site by issuing the construction order at the same cell.
Right-clicking an unfinished same-owner foundation with an SCV also resumes
it. An accepted immediate replacement order cancels a worker's pending order
before the foundation starts; invalid commands leave it intact. An explicit
repair order is accepted while an SCV has a pending placement, and an invalid
Harvest on an empty cell leaves the placement and its Shift queue intact.
Harvest requests are committed per worker only after that worker passes vent,
reachability and approach checks; a failed member of a mixed selection keeps
its construction order. A successful direct Harvest proceeds through travel
to mining, becomes the active queue order, and keeps subsequent Shift orders
pending. If a Terran SCV leaves an active foundation to harvest, the SCV and
site links are cleared immediately and the site pauses at its current progress.
A Terran SCV that finishes a Refinery is handed to the remapped gas vent, as
OpenBW's `order_ConstructingBuilding` completion path does. A failed arrival-time
placement or resource check discards the active build but keeps queued orders.
Queued orders remain behind an active construction and resume after completion.
A queued order added while the SCV walks to the site survives foundation
creation. A Protoss Probe is released with its queued orders as soon as the
foundation appears. If a foundation dies, its linked SCV immediately loses the
stale target and construction order. Incomplete buildings do not contribute
supply until completion.
Protoss releases the Probe immediately after placing the foundation. Zerg
morphs the Drone in place and restores its pre-morph HP if the unfinished
building is cancelled. Cancelling an incomplete building refunds 75 percent
of its mineral and gas cost, matching OpenBW's `partially_refund_unit_costs`.
Cancelling a worker's pending order before a foundation exists charges nothing.

Construction-produced HP and shields increase with elapsed build time. These
counters are separate from current vitals so damage received during
construction is not healed by later progress. Current HP/shields are not
filled on completion. The implementation uses a linear time interpolation;
OpenBW instead increments per-frame using an integer construction rate, so
per-tic rounding is an engine approximation. Native construction image stages,
worker construction animations, creep growth/decay, and exact movement/placement
timing are not implemented here.

The command card status panel shows the site's queue and progress bar and
offers cancellation for the site. Incomplete sites are excluded from
`G_ModelHasActorType`, so building prerequisites only become available once
construction finishes. `sc_start` runs before construction shields are
reduced to their initial partial value, preventing the first unit tick from
restoring full shields.

## Evidence and limits

The pinned OpenBW reference is documented in `REFERENCES.md` and its source is
[`bwgame.h`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h).

- `order_PlaceBuilding` rechecks location, affordability and placement after
  movement; on success it creates an incomplete building and links the SCV
  (`bwgame.h`, around lines 4312–4355). `order_ConstructingBuilding` maintains
  the SCV connection and calls `resume_building_unit` while construction is
  active (around 4383–4475); on completion, it sends an SCV that built a
  Refinery into gas harvesting (around 4385–4395). The local model approximates this with a linked
  SCV and one progress update per game tick; it does not reproduce the worker's
  random construction animation positions.
- `order_Repair` rejects incomplete, Stasis, loaded, non-mechanical, and
  non-Terran targets (around 4656–4668). Local repair now checks construction
  readiness and Stasis again while the order is active.
- `order_PlaceProtossBuilding` revalidates after movement, spends resources,
  creates the incomplete foundation, then releases the Probe (around
  6603–6660). `order_IncompleteWarping` advances remaining time, HP, and
  shields (around 6662–6708).
- `order_DroneLand`/`order_DroneBuild` revalidate after movement and charge
  only after a valid site is reached; then the Drone morphs into the building
  (around 6075–6181). The local implementation preserves the Drone mobj and
  restores its saved HP on cancel, but does not reproduce all Zerg birth art
  and special Extractor visuals.
- `cancel_building_unit` and `cancel_morphing_building` handle unfinished
  objects, including the 75-percent refund and Drone restoration (around
  2171–2228).

These are OpenBW behavioral references, not a new retail executable trace.
The code intentionally documents its linear progress and presentation gaps
instead of claiming exact retail timing or construction art.

## Verification

`tests/starcraft/test_construction.c` covers Terran SCV attachment, damage,
pause on builder loss or movement, contextual and explicit resume, queued Move
before foundation creation for Terran and Protoss, right-click ground/mineral
replacement, empty-cell Harvest preserving a pending placement, accepted
repair replacing a pending placement, pending-order Stop/Move/cancel, mixed
reachable and isolated SCV Harvest selection, empty-cell Harvest preserving
Shift orders, direct Harvest reaching the mining phase and retaining a queued
Move, direct Harvest detaching a Terran SCV from a paused foundation, queued
Move surviving a failed site revalidation, refinery gas handoff after
completion, site death cleanup, Terran refund,
supply exclusion and invalidated placement without charge. It also
covers Protoss Probe release, partial shields and cancellation, plus Zerg
Drone morph/cancel with damaged-worker HP restoration. `tests/starcraft/test_save.c`
verifies construction production, builder references, progress fields and
construction HP survive save/load and contribute to the consistency hash.
`tests/starcraft/test_orders.c` checks that repair orders reject unfinished
mechanical foundations and Stasis targets and stop if a target enters Stasis.
