# StarCraft construction lifecycle

## Implemented model

Construction now creates a selectable, damageable StarCraft building object
after the worker reaches the chosen approach position. The object carries its
remaining build time in the existing `production_t`, and its frame countdown,
hand-over stage and Zerg Drone return HP in `sc.construction`. The latter is saved
and hashed with StarCraft's mobj state. StarCraft save layout is 18.

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

## Per-frame HP and shield update (OpenBW, implemented)

Confirmed in the pinned OpenBW source (function names, not line numbers):

- `initialize_unit_type`: a new unit starts at `hitpoints/10` (raw 1/256 HP
  truncating division; `create_unit` and `zerg_building_start_construction`
  set the same value). `hp_construction_rate = (hp - hp/10 + build_time - 1) /
  build_time` in raw 1/256 HP (a round-up division, minimum 1 raw). For grounded
  buildings with shields the start is `shield/10` and
  `shield_construction_rate = (shield - shield/10) / build_time` (truncating,
  floor of one whole point). Build time 0 uses remaining 1 and rate 1.0.
- Terran, `resume_building_unit` (called from `order_ConstructingBuilding`
  while the SCV is in its building states 4 and up): `set_unit_hp(hp + rate)`,
  then if time remains decrement it and return, otherwise `finish_building_unit`
  and `complete_unit`. So a Terran building takes `build_time + 1` steps.
- Protoss, `order_IncompleteWarping`: state 0 moves to state 1 on the frame that
  sees remaining time 0, and every state 0 frame then decrements remaining time and
  adds HP and shield steps (`build_time + 1` steps). States 1 to 3 add nothing and
  wait on IScript signals before `finish_building_unit`/`complete_unit`.
- Zerg, `order_IncompleteMorphing` (also used for the Extractor): remaining time and
  HP step together while time remains, plus the two frames of states 4 and 5
  (`build_time + 2` steps); state 6 waits for IScript signal 4. Building morphs
  (`unit_type_is_morphing_building`, Hatchery to Lair and so on) skip the HP step,
  and on completion add the new type's HP minus the old type's.
- `set_unit_hp` and `set_unit_shield_points` clamp at the maximum, so a step never
  overfills. `finish_building_unit` fills HP and shields only when time remains,
  which none of the three normal paths reach: a building completes with whatever it
  holds. Damage (`unit_deal_damage`) has no special case for incomplete units; it
  just subtracts. Early damage is therefore not healed, apart from the rounding
  overshoot of the last steps being clamped at the maximum.
- Shields also regenerate by 7 raw per frame in `update_unit_values`, for incomplete
  buildings too (HP regeneration and Terran burning need `u_completed`).

Local implementation: `G_ModelUpdateConstruction` in `games/starcraft/p_prod.c`
runs the steps once per 24 Hz frame the tic covers, using raw 1/256 HP held as
`hp` and `sc.wound`. Authoritative build progress is `sc.construction.frames`
(frames left) and `sc.construction.stage` (hand-over frames after the timer);
`production->time_left_ms` is rewritten from the frame count each tic so the
status bar and the saved-duration validation keep working. Both fields are hashed
and saved with the mobj (save layout 18). The shared ticker still subtracts the
tic from `time_left_ms` before the hook, which is why the frame count is kept
separately. A cancelled Zerg building returns its Drone with a zero wound;
cancelling an Extractor returns a fresh full-HP Drone, as `cancel_building_unit`
does (its previous HP is the geyser's, capped at a Drone's maximum).

### Script-signalled completion (Protoss warp-in and Zerg final state)

`sc.construction.stage` now counts the frames since T, the first frame on which
OpenBW's state machine sees the build timer at zero, and the foundation
completes after `sc_hand_over(type)` such frames (`games/starcraft/p_handover.c`,
read from `iscript.bin` when it loads). Each number below is a script wait count
combined with OpenBW's frame order: a unit's order runs first and its sprite's
image scripts after, in every frame; `sprite_run_anim` and `create_image` run
call 0 of a script at once, the sprite pass of the same frame runs call 1, and
call j (j >= 1) runs in the frame start + j - 1; an order sees a signal one
frame after the sprite pass that set it.

- Zerg (`order_IncompleteMorphing`): T is state 4 (timer zero), state 5 at T + 1
  runs AlmostBuilt on the construction image (units.dat `construction_animation`
  column, PyMS `UnitsDAT`), which signals `sigorder 4` on call 6 (every Zerg
  construction image 102 to 105 has it, the Extractor's 102 included; 101, the Infested Command Center's, has no animations). State 6 sees it
  in frame T + 1 + 6 = T + 7 and completes without a step (it returns). Steps stay
  on T and T + 1: `build_time + 2`. The first batch took the signal as instant and
  completed on T + 2: five frames early.
- Protoss (`order_IncompleteWarping`): the foundation sprite holds the Warp Anchor
  image (211, script 191). State 0 at T runs its SpecialState1, `sigorder 1` on
  call 4, which state 1 takes on T + 4. State 1 then starts the Warp Texture script
  193 Init (two calls in that frame; `sigorder 1` on call 19), which state 2 sees
  18 frames later, T + 22, when it runs WarpIn on the building. WarpIn overlays
  the warp-flash image (draw function 17: `image_update_warpin`, data1 48 to 63,
  data2 2 reloading to 2: increments on frames 2, 5, ... 44 after creation, the
  signal on frame 47, seen by state 3 on frame 48): T + 22 + 48 = T + 70 for
  every warped-in building (Nexus, Pylon, Assimilator, Gateway and so on; types
  whose WarpIn overlays nothing, such as the Stasis Cell and the Khaydarin
  Crystal, have none). States 1 to 3 return without a step, but state 3 falls
  through to the HP and shield step after `complete_unit`, so the completing frame
  steps once more: `build_time + 2` steps, not `build_time + 1`. The first batch
  completed on T + 1 and without that last step.

Without `iscript.bin` the table is empty and the first batch's instant signals
remain (Protoss T + 1, Zerg T + 2, no step on completion). Terran
(`resume_building_unit`) needs no signal. Save layout is unchanged by this: only
the meaning and range of the existing `stage` field grew.

Not modelled: the images the hand-over runs (construction graphic stages, the
warp-in picture), worker animations and exact SCV movement and placement timing;
which frame of the creating order the foundation's first step lands on is the
first batch's mapping. The frame order assumption (order, then sprite pass)
and the call counting are from reading `update_unit`, `iscript_execute_sprite`
and `sprite_run_anim` in the pinned OpenBW, not a retail trace.

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
The per-frame arithmetic is OpenBW's; presentation gaps are listed above.

## Verification

`tests/starcraft/test_construction_frames.c` pins exact integer values: Barracks
(start 25600 raw, +192 per frame) at frames 100 and 600 and over its whole life
(1201 frames), early and late damage (final 951 HP plus 64/256 and 1000 HP plus
64/256), SCV leave and replacement, 75 percent refund; Pylon (HP +154, shields
+153 and regeneration, 452 frames, Probe released at once, refund); Spawning Pool
(+144, 1203 frames, damaged-Drone restoration, refund) and Extractor cancel.
The Pylon completes on frame 521 (T + 70; 452 steps) and the Spawning Pool
1100 frames after the 100th step plus 8 (T + 7; steps unchanged), `hand_over_table`
checks 70 and 7 for every warped-in and Zerg building and none for Terran types,
`protoss_hand_over` checks the step on T, none on T + 1 to T + 69 and one on the
completing frame, and cancelling during the hand-over; `unmodelled` checks the
first batch's numbers without script data.
Commands run: `make build/bin/starcraft`; `env SDL_VIDEODRIVER=dummy
TEST_JOBS=4 TEST_BUDGET=60 make -o starcraft-maps test-starcraft` (31 passed,
0 failed).

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
