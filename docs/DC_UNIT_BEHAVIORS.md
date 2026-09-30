# Dark Colony unit behavior comparison

Audit date: 2026-09-30. Engine baseline: `ce24ce5`.

Compared local retail `data/DCOLONY/DC.EXE` and its GAMESTAT/FIN assets with
the current C simulation. This is a documentation audit, not a behavior patch.
Bombs includes aircraft bombs, Atril projectiles, and Sentinel/Slom mines.
Flying units includes Osprey, Ortu, Medi-craft, and Zisp; dropships are addressed
separately below because their cargo lifecycle is different.

**Result:** artillery and deployed mines implement substantial native rules,
but are not exact retail simulations. Aircraft movement and animation work;
bomber attacks and specialist healing are missing. Passing existing tests does
not establish that all these units behave like DC.EXE.

Instruction addresses, formulas, fingerprints, corrections, and reproduction
commands are in [the accompanying executable findings](DC_EXE_FINDINGS.md#bombs-flying-units-and-artillery-comparison-2026-09-30).
“Confirmed” below means instructions/assets or directly inspected engine code;
“unknown” means the audit did not establish the complete retail behavior.
No retail gameplay session was recorded for this audit.

## Comparison by unit

| Unit | Confirmed retail contract | Current implementation | Assessment |
|---|---|---|---|
| Osprey / SCGM / native 5 | HP 800, damage class 2, flight flag, speed field 47, sight 8/8; weapons 37/44/45 | Actor defaults give HP 600, class 2, speed `47/32`, sight 8/8; attack range 5, damage 80, cooldown 600 ms, no projectile or attack state | Bombing absent; HP and weapon defaults differ |
| Ortu / ORTU / 13 | HP 800, class 2, speed 47, sight 8/8; same three weapon IDs as Osprey | HP/class/sight match; speed `47/32`; range 2 and damage 100 match base weapon, but cooldown 500 ms, no projectile or attack state | Bombing absent despite attack capability flag |
| Medi-craft / BEON / 49 | HP 400, class 2, flight flag, speed 47, sight 5/3, weapons -1; dedicated healing handler | HP 400, class **0**, sight **8/8**, speed `47/32`; no healing action | Flying movement exists; healing and two stat fields differ |
| Zisp / ZISP / 50 | HP 400, class 2, flight flag, speed 47, sight 3/5, weapons -1; same healing handler | HP/sight match; class **0**, speed `47/32`; no healing action | Healing absent; damage class differs |
| Barrager / BARR / 3 (`MT_THUNDERBOLT`) | HP 400, class 3, speed 15, sight 7/4; weapons 10/11/12 | HP/class/sight/base weapon match; speed `15/32`; timed arcing shell and square blast | Partial native implementation; targeting, launch, upgrades and rounding differ |
| Atril / ATRIL / 11 | HP 400, class 3, speed 15, sight 4/7; weapons 24/25/26 | HP/class/sight/base weapon match; speed `15/32`; animated PUS shot and same arc/blast as Barrager | Same limitations; upgraded reload differs from Barrager in retail |
| Sentinel / ENGI / 43 → HMINE / 45 | Mobile unarmed form; deployed class 7, weapon 38, three charges from full HP | Deployment, ground trigger, charge HP cost and 7×7 blast work | Concealment/detection and retraction incomplete |
| Slom / SLOM / 44 → HMINE / 46 | Alien counterpart of Sentinel | Same charge/impact machinery; native deployment sequence | Same remaining gaps |

Actor defaults in [g_game.c](../games/dark-colony/g_game.c) control ordinary
spawning. `info.c` still says Osprey `spawnhealth=800`, but
`P_ApplyActorTypeDefaults` supplies 600 first, so that fallback does not correct
it. Scenario loading subsequently assigns authored/default scenario HP and can
therefore differ from fresh-spawn HP while retaining the 600 maximum. Do not
infer actual runtime stats from `info.c` alone.

Movement speeds above deliberately retain the distinction between raw retail
fields and engine cells/second. This audit does not certify the `/32` conversion
or continuous movement timing against the complete retail movement ticker.

## Aircraft bombs

Both bombers select the following native weapons through their team's upgrade
index. These are not three projectiles emitted simultaneously by one call.

| Weapon | Prefix | Damage | Range | Speed, 8.8 cells/tick | Delay after ordinary / third firing event | Trajectory / boom |
|---|---|---:|---:|---:|---|---|
| 37 | SPAK | 100 | 2 | 15 | 10 / 30 native ticks | 0 / 5 |
| 44 | SPIKE | 125 | 2 | 15 | 10 / 30 native ticks | 0 / 5 |
| 45 | SPIKE | 150 | 2 | 15 | 10 / 30 native ticks | 0 / 5 |

At the default 66 ms clock, the assigned delays are 660 ms and 1,980 ms.
The shot counter resets after the third firing event; the caller's action and
timer dispatch can also affect observed launch times. Exact first-shot latency
and attacking while moving remain unverified.

Trajectory 0 advances constant velocity, including vertical velocity toward the
target height. Because boom 5 is nonzero, the projectile uses a flight countdown
and detonates at its destination rather than colliding with intervening units.
It does **not** use artillery's trajectory-1 height curve. Boom 5 has a 3×3
matrix with only the center cell nonzero, selects SMAY, and uses center-only aim
scatter. Its damage search visits ground occupancy, not aircraft.

Weapon class 2 damage percentages by victim class are:

| Victim | Infantry | Reaper/Sy-Demon | Aircraft | Artillery | Sarge/Gorrem | Worker | Tower | Mine | Invulnerable | Building |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Percentage | 7 | 25 | 0 | 8 | 25 | 45 | 5 | 25 | 0 | 5 |

These are fixed-point multipliers, not literal damage. Before defense and owner
adjustments, a base bomb produces 6 against class-0 infantry and 7 against
class-3 artillery after native truncation. The nominal “100 damage” is not 100
HP against every unit.

The current bombers have `missilestate=S_NULL`. `A_Look` can find a target but
has no attack state to enter. Adding a call to the existing `P_Attack` alone
would produce immediate direct damage from their placeholder attack fields,
not native bombs, because `projectile_type=0`. A complete correction needs the
weapon, burst counter, flight, impact, targeting and presentation together.

SPAK.FIN has no `SPAKBULLET0`. SCGM.FIN contains `SPIKEBULLET0`, and ORTU.FIN
contains `EGGBULLET0`; neither aircraft FIN has a FIRE label. These facts do not
justify inventing a missing FIRE sequence, loading a nonexistent SPIKE.FIN, or
silently assigning EGG to weapon 37. The verified loader uses global labels;
an additional race-specific EGG substitution remains unknown. The encyclopedia's
“napalm canisters” description also does not establish persistent burning:
weapon 37 uses boom 5, whereas the separate napalm effect weapon 50 uses boom 10
and trajectory 4.

## Flight, sight, animation and support behavior

| Behavior | Retail evidence and current comparison |
|---|---|
| Air/ground distinction | Numeric field 12 after the GAMESTAT sprite token becomes type `+0x60`; all four aircraft set it. Engine uses `MF_FLY`. Field 10 is the damage class, despite the old `GAMESTAT_UNIT_FLY` enum name. |
| Route | Retail aircraft ignore terrain families on the main route, move diagonally first then along the remaining axis, and clamp destination Y to `height-3`. `DC_FindPath` / `DC_MoveUnitTo` implement these rules. |
| Occupancy | Retail air and ground use separate grids; occupancy reserves the next step. Engine pathing separates aircraft from ground occupants and uses stable mobjs to resolve reservations. |
| Sight | Aircraft bypass ground sight pruning. Engine does this, but Medi-craft's radius remains 8/8 instead of native 5/3. |
| Altitude | Retail spawn writes object `+0x02=600`. Engine uses `50*FIXED_ONE/32=102400` for the four aircraft. The existing 50 px policy is not a demonstrated conversion of 600 or proof of retail projection. |
| Hover | Osprey's four-frame STAND and Ortu's direction-0 seven-frame STAND retain authored bobbing. No extra sine-wave bob is needed. Ortu direction 8 actually has five STAND/MOVE frames: not every facing is seven frames long. |
| Turning/firing | Retail common fire waits for its heading routine to finish. Engine mobile attackers snap their angle in `A_Look`; only turrets use its turning gate. Exact native aircraft attack/movement interaction remains unknown. |
| Death | Engine clears flight/movement/attack and enters FIN death states. Pixel/timing tests cover SCGMDIE0 and ORTUDIE14. Retail crash descent, corpse altitude and directional death selection were not established by those tests. |
| Healing | Retail explicitly dispatches native types 49/50 to `0x412f74`, scans nearby air/ground occupancy, and uses MBULLET row 7. Engine has no equivalent specialist action. Recharge, exact activation cadence and complete selection rules need further tracing. |

Dropships already use ordinary thinker-owned mobjs and native FIN states in
[p_drop.c](../games/dark-colony/p_drop.c). Their cargo/release comparison is
documented in [DC_DROPSHIP_ANIMATION.md](DC_DROPSHIP_ANIMATION.md) and the
earlier dropship sections of [DC_EXE_FINDINGS.md](DC_EXE_FINDINGS.md).
This audit does not certify transport, abduction, environmental flying creatures,
or artifact air drones as implemented. They must not inherit bomber behavior
merely because they fly.

## Artillery and mines

| Rule | Retail | Current engine |
|---|---|---|
| Artillery weapon | BARR 10: damage 250, range 12, speed 60, ROF 75; PUS 24 has the same base fields | Matches base C definitions |
| Upgrades | BARR 11/12 range 14/16, ROF 75; PUS 25/26 range 14/16, ROF **150** | Base weapon only |
| Target selection | Common automatic search rejects zero weapon-class multipliers | Generic search only gives mines an air exclusion; artillery can acquire and waste shots at aircraft |
| Launch point | FIN channel 7, potentially multiple attachment records | Actor position, one missile |
| Aim | BOOMSTAT weighted scatter for artillery; center-only for mines | Exact target position; no scatter |
| Heading/velocity | Quantized native heading/trig and integer 8.8 velocity | Planar normalization and 16.16 velocity |
| Flight duration | Dominant-axis signed delta/velocity, plus native limiting branch | Planar distance/step, minimum one tick |
| Arc | Native 17-entry table, with `>>6` before storing height | Same table/shape, but conversion preserves fractional precision discarded by retail |
| Collision | Timed blast shots bypass intervening object collisions | Implemented for artillery and mine projectiles |
| Blast | Artillery 5×5, mines 7×7 weighted cells; ground grid with fallback layer | Same weights, but scan all eligible mobjs by current position rather than native occupancy |
| Friendly splash | Same owner gets quarter blast factor | Implemented; an allied different owner is not the same-owner case |
| Defense/daylight | Defense upgrade factor in damage routine; area branch passes daylight flag zero | Class/weight/owner factors implemented, defense upgrades absent; do not add a daylight penalty to area blasts without new evidence |
| Explosion | Select one NUKE or GASY for artillery; NUKE for mines | Implemented; exact global RNG consumption differs |
| Mine charges | Subtract 300 HP, clamp negative remainder to 1, then own splash; three charges from undamaged 800 HP | Implemented; damage to mine can reduce surviving charges |
| Mine concealment/retraction | Native text promises buried concealment; detector behavior needs a complete executable trace | Visible under ordinary sight; no retraction |

The old Barrager “MISA, speed 8, damage 180” explanation is superseded.
The stale launcher `mobjinfo.damage=180` is not the active shell damage:
`MT_CANNONBALL.damage=250` and its class factors control the impact.
Tower rockets are also a separate family: human trajectory 2 weaves and emits
SMOK; Xenowort trajectory 0 is straight. Neither is the artillery curve or an
aircraft bomb.

## Verification and remaining work

Fresh headless diagnostics at `ce24ce5` placed an enemy Trooper one cell away,
disabled the target's attack, and ran 300 simulation tics. Enemy ownership and
allegiance were both set. Target HP stayed 800 for Osprey and Ortu; Barrager and
Atril reduced it to 426. A separate Barrager-versus-Osprey check acquired the
aircraft and assigned a firing cooldown while the aircraft took no blast damage.
The temporary diagnostic source/logging was removed; the reproducer is preserved
in the executable findings.

Six existing checks passed: `test_projectiles`, `test_native_pathfinding`,
`test_multiplayer_units`, `test_actor_lifecycle`, `test_drop_fin_states`, and
`test_dark_colony_sprite_layout`. The Dark Colony headless smoke check also
passed (HUMAN01, 34 units). The path test's invalid-PTH message is its deliberate
malformed-input case. These validate current contracts, not retail equivalence.

Concrete follow-up order:

1. Implement complete native bomber weapons and three-event reload behavior;
   retain the unresolved animation/attack-dispatch questions explicitly.
2. Correct Osprey defaults, support-aircraft classes and Medi-craft sight;
   reconcile scenario HP versus maximum HP.
3. Apply weapon-class eligibility during automatic target acquisition and
   cover artillery versus aircraft with a regression.
4. Reproduce native attachment placement, scatter, quantized launch/duration,
   8.8 arc truncation and occupancy-based impact with numeric reference vectors.
5. Trace and implement weapon/defense upgrades, healing/recharge, mine detection
   and retraction; audit exact flight altitude, speed and death behavior.

No gameplay changes are included in this comparison.
