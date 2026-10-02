# Dark Reign development status

Updated September 30, 2026. Runtime baseline: `047477a`.
This is a navigation and verification handoff. The
[disassembly index](DR_DISASSEMBLY.md) links the executable fingerprint and
topic reports; [DR_EXE_FINDINGS.md](DR_EXE_FINDINGS.md) retains the dated evidence.

## Implemented and verified

| Area | Supported behavior | Verification |
|---|---|---|
| Campaign start data | Default FG M01F map, credits, starting actors, technology and camera | `test_mission_hud`, `test_playable` |
| Ground/hover resource transport | All four faction transporter types; exact native bays, 135-degree facing, 15-frame/51-tic one-shot passes and native batches | `test_harvest_build` |
| Collision | Native overlay masks preserve walkable docking bays | `test_harvest_build` footprint and blocked-destination checks |
| Flight capability | Five mapped Fly types retain the trait after actual spawning | `test_unit_traits` |
| Human/manual-target traits | Native IsHuman and NoAutoTarget declarations agree with runtime/generated tables | `test_unit_traits` audits 65 native definitions |
| Support | Medic/Karoch heal damaged allied ground humans; Mechanic repairs allied ground nonhumans; amounts, cap and minimum cooldown | `test_unit_traits` |
| Construction rigs | Move and build, with invented offensive weapons removed | `test_unit_traits`, `test_playable` |
| Production | Authored faction products, makers, prerequisites, resources, queueing and AI production | `test_production`, `test_playable` |
| Native HUD presentation | Retail chrome, fonts, unavailable-red icons, slot geometry and minimap rectangle | `test_mission_hud` |
| PATHS | Native Basic/Advanced controls, draft editing, save/reload, Go, three traversal modes and radar routes | `test_mission_hud`, shared `test_waypoints` |
| Engine routes | Shared DC/DR storage and thinker execution; atomic network orders and deterministic route checksums | Shared `test_waypoints`, `test_network` |
| Both faction HUD catalogs | One icon per authored product; either selected rig exposes buildings | `test_mission_hud`, including Imperium HQ debit |
| Model/input | Selection, movement, attack, harvest, production and command integration | `test_input`, model-command tests |

Final verification for `047477a`: `make`, generated-table comparisons,
`make tags`, 58 test executables across Dark Reign/Dark Colony/7th Legion,
both model-command tests and all four games' dummy-video `--check` passed.
Dark Reign world/HUD and isolated Imperium HUD captures were visually inspected.
That does not claim a complete retail gameplay session or full test-suite parity
for KKnD/networking; prior baseline failures are recorded in the findings.

## Remaining native work

| Area | Unimplemented or incompletely verified contract | Evidence/navigation |
|---|---|---|
| Catalog | Six non-T internal unit types: 4100,4101,4102,1026,11103,3006 | [Unit coverage](DR_UNIT_BEHAVIORS.md#catalog-coverage) |
| Transports | Passengers, boarding, release, destruction and Phase Runner boomerang | [Unit comparison](DR_UNIT_BEHAVIORS.md#comparison-by-unit-family) |
| Special actions | Phasing, camouflage/morphing, spy entry/discovery, sabotage, Hostage Taker capture/conversion | Unit comparison; retail `Can*` definitions |
| Boosting/shield/charge | Amper boost/fade/death, SCARAB alternate, Fortress/Rift charging | Unit comparison; relevant weapon/unit definitions |
| Combat | Full projectile, blast, suicide, armor and target-class rules | [Unit report](DR_UNIT_BEHAVIORS.md) |
| Support fidelity | Native projectile, H1/R1 defense factors, search/orders, cadence and boost cancellation | [Support limits](DR_UNIT_BEHAVIORS.md#support-path-and-its-limits) |
| Economy/pathing | Extractor stock/regeneration, receiver storage, launching and local traffic arbitration | [Transport report](DR_TRANSPORTER_ANIMATION.md#superseded-rules-and-remaining-unknowns) |
| HUD | COMMS/ORDERS/SPECIAL, the HUD's own in-game menu (MENU opens the shell's options screen), upgrade/decoy, radar terrain and resource gauge inputs; PATHS target nodes, names/persistence, unbounded native routes and exact text states | [HUD report](DR_HUD_DISASSEMBLY.md) |
| Shell | Construction kit, movies, saves, mission-map nodes, archive/story/debrief, ActiveNet/modem/serial; setup options other than slots, sides, teams and credits | [Native shell](DR_EXE_FINDINGS.md#native-shell-2026-10-02) |
| Campaign/AI | Full mission FSM execution and native AIP scheduling/scoring | [Architecture](DR_ARCHITECTURE.md), findings |
| Training/expansions | T-prefix definition swapping and additional rosters | Findings' catalog sections |

The former “all 71 units represented” statement is superseded: the focused
native audit finds 65 mapped definitions and six missing internal types.
Loading a body sprite or setting a capability bit is not proof that a special
unit's native behavior works.

## Reproduce the handoff

```sh
make
env SDL_VIDEODRIVER=dummy make test-dark-reign
env SDL_VIDEODRIVER=dummy make test-model-commands
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --check
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --screenshot /private/tmp/open-rts-dark-reign.bmp
```

Use [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) for new investigation.
Record the executable hash, controlling instructions, native inputs, edge cases
and focused tests before implementing a rule. Preserve disproven hypotheses
and unknowns; avoid guessed aliases or compensating visual/timing constants.

The PATHS follow-up also integrates completed DC HUD work `05f753d`, with
route ownership moved into the shared engine. Verification: 62 test executables
across DR/DC/7th Legion, both model-command tests, native DC sprite-layout
validation, all four games' waypoint tests and headless smoke checks, plus the
full direct/hosted network suite (including packet loss/duplication/reordering).
Native PATHS and M01F HUD captures were visually inspected. This establishes
implemented route behavior; the remaining native work above is still open.
