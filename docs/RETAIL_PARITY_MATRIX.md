# Retail parity evidence and verification matrix

Scope: issue #103, "Validate full retail parity for Z and StarCraft with
reproducible comparisons". This file is a ledger of what this repository
records today. It is not a parity claim. A row marked "Implemented, unverified"
or "Reference-source only" says nothing about whether the retail game behaves
the same way.

Every status below is backed by a findings-doc section or a test name that was
read while writing this file. Where no backing was found, the status is
**Unknown**.

## Status legend

| Status | Meaning in this file |
|---|---|
| Verified against retail | A findings doc records a retail execution trace (DOSBox-X run and debugger) or observed retail output (a user-supplied retail screenshot) for that exact point. Narrow points only; the row says which. |
| Reference-source only | Evidence is retail data files, static disassembly of the retail executable, or pinned reference source (OpenBW, Stargus, PyMS, Zod). No retail run of that behaviour is recorded. |
| Implemented, unverified | open-rts code and tests exist for the area. No retail comparison is recorded. |
| Different (known) | A findings doc or completeness audit states that open-rts differs from retail or from the native rule for that area. |
| Unknown | The repository does not establish the native behaviour, or no evidence was found. |
| Out of scope | Excluded by an explicit scope statement in the repository. |

Issue column: #96 to #102 are the existing issues named in the task for their
areas. "none filed" means no issue in that list covers the row. #104 is an
umbrella tracking issue; it is not linked row by row because its body was not
part of this review.

## Environment and reproducibility

- Host used for the repository tests: macOS (Darwin 27.0.0). Headless runs use
  `SDL_VIDEODRIVER=dummy`.
- Z retail execution: DOSBox-X 2026.10.01, build `f1c4414`, on a disposable
  copy of the game data. Recorded in
  [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md), section "Indirect writes and retail
  runtime follow-up". No other Z section records a retail run.
- StarCraft retail execution: none recorded. [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md)
  states in several sections that "no retail executable was run". Retail
  observations are limited to user-supplied screenshots that the doc cites;
  those screenshots are not in this repository.
- Retail data and executables are proprietary. This file records only file
  names, sizes and SHA-256 values. It copies no game content.

## Fingerprints

Computed with `shasum -a 256` at this revision (`49b5bfc0`, 2026-11-13).

### Z

| File | Size (bytes) | SHA-256 | Reference |
|---|---|---|---|
| `data/Z/ZED.EXE` | 2320417 | `3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676` | Pinned in [ZOD_STARGUS_HANDOVER.md](ZOD_STARGUS_HANDOVER.md) "Native trace continuation" and repeated in [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md). Matches. |
| `data/Z/WARDATA.PAC` | 6959242 | `3a17f168f1b7231be3bac5d324afb1311451aeb4da224b998754851aa035e035` | Archive. Not pinned in the docs. |
| `data/Z/SPRITES.RSC` | 3893081 | `56b79cfaf489bdbcd9fd658c583d01ed31dd4e7b8575042b4fd8c216a3f5f6e8` | Matches the SPRITES.RSC fingerprint in [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md), section "Building windows and sidebar correction (2026-10-15)". |

The Z "Reference pins" in the handover (`reference/zod` at `5ccb43ab76ee08c9f624906fb465dc3ba76a7a35`,
`reference/stargus` at `2a4d54604949e4772f6638a8412c8ebd62444044`) are source
references only. They are not retail fingerprints.

### StarCraft

| File | Size (bytes) | SHA-256 | Reference |
|---|---|---|---|
| `data/StarCraft/StarCraft.iso` | 667961344 | `4f7455c8ea55ca3b7d49209bc25cf9edb62f40a7e06015701159ae47cbd0033b` | Matches [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md), section "Reproducible inputs". |
| `data/StarCraft/INSTALL.EXE` (identical to `data/StarCraft/disc/INSTALL.EXE`) | 601389682 | `ba155a30ca11a57a2ea917cb4f25715f79bee7397ebb16db4816e7725395e58d` | Installer. Contains the MPQ archives. |
| `data/StarCraft/install/files/starcraft.exe` | 979456 | `16cd8f0d1098f8bcd0625d5e13964c2facf2df4874636acb01c2e0bfb062b2f5` | Matches [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md), section "Reproducible inputs". |
| `data/StarCraft/install/files/stardat.mpq` | 60998354 | `4a503190f79289d827bb7ea99f06b8b9f18a120e360f8442e10933939eafb525` | Matches [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md), section "Reproducible inputs". |
| `data/StarCraft/disc/SETUP.EXE` | 25088 | `d82ad1081dfe2e318b6d888666acf1f5edde7d6dfbfebfc53ffaf52dcff90a0b` | Installer front end. Not referenced by the findings docs. |

`data/StarCraft/native/` and `data/StarCraft/maps/` hold extracted data. They
are not fingerprinted here.

## Z: evidence and coverage matrix

Findings docs are in `docs/`. Tests are in `tests/z/`. The `make test-z` target
runs every `tests/z/test_*.c` binary except `test_ai` (13 binaries), plus the
Z-applicable shared suites under `tests/shared/` (14 binaries). `test_ai` runs
only in the soak target.

| Area | Retail evidence (doc + section) | Automated coverage (file: test name) | Status | Linked issue | Next check |
|---|---|---|---|---|---|
| Campaign missions and transitions | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Level loading"; "Native computer scoring and campaign settings (2026-10-18)", "Confirmed settings and clock" | [test_commander.c](../tests/z/test_commander.c): `all 20 campaign intelligence, reserve and territory settings`. [test_ai.c](../tests/z/test_ai.c) plays LEVEL01, LEVEL02 and LEVEL05 (soak only). | Reference-source only | none filed | Run LEVEL01 under DOSBox-X to victory and record the next level chosen. No retail progression is recorded. |
| Unit commands | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Units" (record fields, direction 0 to 7); "Flags, vehicles and grenades"; "Unit statistics" (in "Commander passes and unit statistics (2026-11-06)") | [test_units.c](../tests/z/test_units.c): `selects, walks and moves`, `vehicles compose eight directions and a wreck`. [test_squad.c](../tests/z/test_squad.c): `native eight-way formation rotation` (fail label; the file prints no PASS line). | Reference-source only | none filed | Trace the native move and stop handlers and compare one fixed move in DOSBox-X. Typed routes are a Zod reference, see the next Z row for routes. |
| Building commands | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Buildings" (74-byte type table); "Building windows and sidebar correction (2026-10-15)"; "Production window dragging (2026-11-13)" | [test_hud.c](../tests/z/test_hud.c): `native level-1 options, 17 product previews at 1x/2x, production and draggable window` | Reference-source only | none filed | Compare sidebar hit regions in DOSBox-X at the same resolution. |
| Production and construction | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Production availability thresholds (2026-10-15)"; "Native production timing for all units (2026-10-15)", which states "No original executable was run" | [test_production.c](../tests/z/test_production.c): `territory gun stock, production cap and mixed placement`. [test_commander.c](../tests/z/test_commander.c): `native initial manufacturing selector follows army strength, not unit counts` | Reference-source only | none filed | Time one Grunt and one Jeep in DOSBox-X at two territory counts and compare with the traced formula. |
| Building reconstruction and crane service (Task 7) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Task 7, automatic bridge reconstruction and crane poses (2026-11-11)", subsection "Indirect writes and retail runtime follow-up". DOSBox-X runtime: a stock Crane assigned task 9 when its radar died; a Fire Truck fixture was skipped at `0x1843e`. The section says a gameplay trigger for task 7 "remains unverified". | [test_commander.c](../tests/z/test_commander.c): `native bridge timer, crane reservation, segment effects and save/load`, `task 7 retains damaged owned buildings and returns when healed`, `native building reconstruction and wreck state` | Verified against retail (narrow: damaged-list membership, task dispatch at radar death, Fire Truck skip; not the task 7 producer) | none filed | Find the code that produces task 7 with a DOSBox-X write watchpoint during a normal campaign turn. |
| Economy (territory capacity, flags, stored guns) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Stored guns and territory capacity (2026-11-12)"; "Flags, vehicles and grenades"; "Confirmed territory scoring and allocation" | [test_capture.c](../tests/z/test_capture.c): `flags change hands with their buildings`. [test_production.c](../tests/z/test_production.c): `territory gun stock, production cap and mixed placement` | Reference-source only | none filed | Record territory-capacity effects in DOSBox-X at two territory counts. |
| Combat | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Unit statistics"; "Vehicle body degradation (2026-11-12)"; "Not decoded yet" (robot +0x24, weapon +0x21/+0x24, body +0x18/+0x19 unknown) | [test_combat.c](../tests/z/test_combat.c): `vehicle damage scales its native speed at health thresholds`, `Fire Trucks resist flame but not grenades`, `malformed crews cannot affect combat`. [test_units.c](../tests/z/test_units.c): `units carry their retail statistics, bullet armour and driver kills` | Reference-source only | #96 | Decode the listed unknown fields, then compare one fixed fight at zero-health boundaries in DOSBox-X. |
| Projectiles (shells, grenades, missiles) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Shell blast selection (2026-11-12)"; "Shell flight (2026-11-12)"; grenade launch and scatter in "Unit statistics" | [test_combat.c](../tests/z/test_combat.c): `shell blast hits enemies inside native bounds once`, `shell flight uses native major-axis speed`, `shells retain their aim when targets move or are removed`, `shell travels and survives save and shooter removal` | Reference-source only | #96 | Capture one Medium Tank shell in DOSBox-X and compare its flight frames with the test timing. |
| Abilities (driver ejection, APC crew, Pyro flame) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" (driver ejection at `0x7d0a2`, `0x648e2`; grenade burst radius and damage). "Zod reference: APC crew and ejection (2026-11-12)" is a remake policy, not a retail finding. | [test_capture.c](../tests/z/test_capture.c): `APC boards and ejects its squad`, `a Grunt gets into an empty Jeep and drives it`. [test_combat.c](../tests/z/test_combat.c): `Pyro flame damage uses native BUILD and bridge scaling` | Unknown | none filed (flame lifecycle is #100) | Decode `0x7d0a2` and `0x648e2` and compare an APC ejection in DOSBox-X. |
| Pathing and terrain | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Flags, vehicles and grenades" (terrain codes `0x23`, `0x24`); "Cliffs, vehicle objectives and per-level computer orders (2026-10-18)", "Confirmed: damageable cliff cells"; "Movement modifier investigation" | [test_cliffs.c](../tests/z/test_cliffs.c): `faces block movement`. [test_terrain_speed.c](../tests/z/test_terrain_speed.c): `native terrain modifiers, clamping and damaged vehicle speed`. [test_detour.c](../tests/z/test_detour.c): `moving units divert to flags, crates and empty vehicles` | Reference-source only | none filed | Trace the movement height-transition and path-node rules that the cliff section lists as unknown, then test a ramp crossing in DOSBox-X. |
| AI (scripted and autonomous) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Native computer scoring and campaign settings (2026-10-18)"; "Commander service and defensive responses (2026-11-11)"; "Commander passes and unit statistics (2026-11-06)". [ZOD_STARGUS_HANDOVER.md](ZOD_STARGUS_HANDOVER.md) records Zod commander conflicts. | [test_commander.c](../tests/z/test_commander.c): `native integer ranking and reserve arithmetic`, `a reserve retakes a flag lost in its territory`, `bridge search stops at first unsuccessful crane`. [test_ai.c](../tests/z/test_ai.c) soak (not in `test-z`): `computer takes the player's fort in` | Reference-source only | #97 | Compare the LEVEL01 flag and territory choices of the computer with the traced ranking in DOSBox-X. The task 6 coordinate-target handoff is still unknown. |
| Triggers and objectives | No trigger interpreter is documented in [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md). The task 7 section says its gameplay trigger "remains unverified". Fort capture is the only objective the docs describe ("Forts"). | [test_fort.c](../tests/z/test_fort.c): `the player takes the computer's fort and wins`, `the computer takes the player's fort and the player loses` | Unknown | none filed | Search ZED.EXE and LEVEL data for scripted objective records. If none exist, document fort capture as the only Z objective. |
| UI and HUD | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Building windows and sidebar correction (2026-10-15)", including "Confirmed: sidebar coordinates and portrait composition"; "Verification and remaining differences" (retail behaviour at viewport edges is unknown) | [test_hud.c](../tests/z/test_hud.c): `native level-1 options, 17 product previews at 1x/2x, production and draggable window`. [test_panel.c](../tests/z/test_panel.c): `panel and production portrait art decode from SHEADFX.PAC/HEADFX.PAC` | Reference-source only | none filed | Capture the level 1 HUD in DOSBox-X and diff it with the `--screenshot` BMP. Resolve the viewport-edge drag question. |
| Menus and front end | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Retail main menu and multiplayer setup (2026-10-18)"; "Implementation boundary and remaining unknowns"; "Not decoded yet" (T, D and Z buttons unported) | [test_menu.c](../tests/z/test_menu.c): `retail menus, native pixels, replay, load, scaling, LAN browse and 2/3/4-player launch` | Reference-source only | none filed | Trace the T, D and Z button handlers and compare the main menu in DOSBox-X at 640x480. |
| Audio | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet": "Audio sample rates and the front end ... remain unported". No audio section exists. | None in `tests/z/`. The shared engine suite `tests/shared/test_sound.c` (`engine sound loading, channels, barks, visibility and lifecycle without retail data`) runs under `test-z` but does not test Z retail audio. | Unknown | none filed | Trace the sound-driver calls and sample rates in ZED.EXE; use the DIG.INI settings from the DOSBox-X run. |
| Animation and assets | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Drawing"; "The sprite list" (8-byte entries sorted by y); "Not decoded yet" (per-sprite trailing byte, DOOR.PAC members, HEADFX BOTTOM members) | [test_units.c](../tests/z/test_units.c): `vehicles compose eight directions and a wreck`. [test_panel.c](../tests/z/test_panel.c): `panel and production portrait art decode from SHEADFX.PAC/HEADFX.PAC` | Reference-source only | none filed | Decode the per-sprite trailing byte and compare the sprite list drawn in DOSBox-X for one frame. |
| Save and load | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) records versioned save layouts in several sections (for example the APC section and the shell section). No retail save-file format is documented. | [test_commander.c](../tests/z/test_commander.c): `native bridge timer, crane reservation, segment effects and save/load`. [test_combat.c](../tests/z/test_combat.c): `shell travels and survives save and shooter removal`. [test_capture.c](../tests/z/test_capture.c) and [test_cliffs.c](../tests/z/test_cliffs.c) round-trip saves (fail labels `save flag and crate`, `load rubble`) | Implemented, unverified | none filed | Decide whether retail save interop is in scope. If it is, add a retail save round-trip check. |
| Multiplayer and determinism | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Random numbers" (network games reset the seed to `0x3039` at `0x49c77`; single player does not); "Retail main menu and multiplayer setup (2026-10-18)" | [test_menu.c](../tests/z/test_menu.c): `2/3/4-player launch`. Shared, run under `test-z`: [test_consistency.c](../tests/shared/test_consistency.c) `consistency vector names the diverging subsystem and ignores selection`; [test_net_menu.c](../tests/shared/test_net_menu.c) `shared network menu creates, browses, joins and starts`. Loopback protocol: [test_network.c](../tests/network/test_network.c) (`test-network`). | Unknown | none filed | Determine whether retail multiplayer can run as two DOSBox-X instances. If it cannot, record retail interop as out of scope. |
| Malformed input | No Z malformed-asset trace is documented. Handling is engine-level. | [test_capture.c](../tests/z/test_capture.c): `malformed crew records are rejected without changes`. [test_combat.c](../tests/z/test_combat.c): `malformed crews cannot affect combat`. [test_capture.c](../tests/z/test_capture.c): fail label `zero player count rejects without mutating level`. `tests/loader_catalog.c` has no Z entry. | Implemented, unverified | none filed | Add Z loader fixtures (truncated and corrupt ZED-derived data) to the loader catalog. |
| Restarts, ownership and cross-game regressions | None in the Z findings. `make test-model-commands` covers Dark Colony and Dark Reign only (Makefile). | Shared suites run under `test-z`: [test_retaliation.c](../tests/shared/test_retaliation.c) `shared retaliation, pursuit, return fire and target cleanup`; [test_waypoints.c](../tests/shared/test_waypoints.c) `shared route traversal, cancellation, ownership, checksums, atomic commands and editing` | Unknown | none filed | Add a Z load, quit and reload test and check object ownership after restart. |

Z detail for the Task 7 row: the doc's own conclusion is that the damaged-list
path is "consistent with a service path whose producer is absent or disabled in
this build". That is an inference, not an observed retail trigger.

## StarCraft: evidence and coverage matrix

`make test-starcraft` runs every `tests/starcraft/test_*.c` binary except
`test_ai` (23 binaries). `test_ai` runs only in the soak target. The shared
`tests/shared/` suites are not part of `test-starcraft`.

| Area | Retail evidence (doc + section) | Automated coverage (file: test name) | Status | Linked issue | Next check |
|---|---|---|---|---|---|
| Campaign missions and transitions | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Campaign triggers and results (2026-10-08)"; "Per-player trigger outcomes (2026-11-12)" (OpenBW source). [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Scenarios/campaigns" and "Scenario semantics requiring focused implementation". No retail campaign run is recorded. | [test_mission.c](../tests/starcraft/test_mission.c): `campaign advance, triggers, elimination, supply and Terran 01` (also checks the Terran 01 to 02 transition and the Terran 12 end) | Different (known) | #98 | Enumerate the trigger condition and action IDs of every installed campaign CHK and mark the unhandled ones, then check each mission's victory trigger. |
| Unit commands | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Core orders, first implementation (2026-10-11)"; "Queued player orders" (OpenBW `issue_order`); "Cloaked Ghost acquisition" (OpenBW). "Completeness recheck (2026-10-11)": exact retail semantics remain unverified. | [test_orders.c](../tests/starcraft/test_orders.c): RTS_CHECK label `StarCraft orders` (Hold, attack and move-only checks). [test_play.c](../tests/starcraft/test_play.c): `shared move/attack, mineral hauling, worker construction, unit training, fog and CHK economy`. [test_initial_states.c](../tests/starcraft/test_initial_states.c): `initial CHK cloak and burrow state flags` | Implemented, unverified | none filed | Compare queued order sequencing and Hold and Patrol edge cases against a retail run. |
| Building commands | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Production pipeline and building controls (2026-10-11)" (assets and reference layout; no executable instruction inspected); "Resource strip and command-card input (2026-10-11)" | [test_construction.c](../tests/starcraft/test_construction.c): `StarCraft Terran, Protoss and Zerg construction foundations`. [test_zerg.c](../tests/starcraft/test_zerg.c): `larvae and eggs, hatchery tiers, colonies, guardians, creep and psi`. [test_units.c](../tests/starcraft/test_units.c): `interceptors, scarabs, siege mode, add-ons, Bunkers, Archons, research, every spell` | Implemented, unverified | #99 (selectable construction sites) | Compare the command card for each building type with a retail screenshot series. |
| Production and construction | [SC_CONSTRUCTION_FINDINGS.md](SC_CONSTRUCTION_FINDINGS.md) "Evidence and limits": linear progress, and "instead of claiming exact retail timing"; [SC_CONSTRUCTION_READINESS.md](SC_CONSTRUCTION_READINESS.md) | [test_construction.c](../tests/starcraft/test_construction.c): `StarCraft Terran, Protoss and Zerg construction foundations`. [test_foundation_readiness.c](../tests/starcraft/test_foundation_readiness.c): `StarCraft incomplete buildings wait for operational readiness`. [test_gameplay.c](../tests/starcraft/test_gameplay.c): `native animations, gathering, construction, training, research, combat, deaths` | Different (known) | #99 | Time one Terran build and one Protoss warp-in in a retail run and compare with the build timer. |
| Economy (mining, gas, supply) | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Repair bounds, Follow, flight and gathering waits (2026-10-11)": "no retail executable was run"; inputs are DAT data and OpenBW and Stargus source. | [test_mining.c](../tests/starcraft/test_mining.c): RTS_CHECK label `StarCraft mining` (cargo and return checks; the file prints no PASS line). [test_play.c](../tests/starcraft/test_play.c): `mineral hauling` | Implemented, unverified | none filed | Time one mineral trip in a retail run (cargo count and walk time) and compare with the gathering waits. |
| Combat | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Combat model from the DATs (2026-10-09)" ("No executable was disassembled"); "Regenerating hit points (2026-11-12)"; "Terran building fire (2026-11-12)"; "Completeness recheck" says damage timing is not verified against the executable | [test_combat.c](../tests/starcraft/test_combat.c): `ground and air weapons, damage types, shields and Shield Battery, hits, splash, glaive bounces` | Reference-source only | none filed (timing gaps are in #101) | Record HP per tick for one Marine against one Zergling in a retail run and compare. |
| Projectiles | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Moving-weapon investigation (2026-11-12)" ("executable behavior not independently verified"); [SC_PROJECTILE_VISUALS.md](SC_PROJECTILE_VISUALS.md) "Limits": does not reproduce "arbitrary script branches, per-command timing gaps" | [test_projectiles.c](../tests/starcraft/test_projectiles.c): `native flight delay, acceleration, fixed aim, upgrades, dead shooter and save` | Different (known) | #101 | Record one Glaive Wurm projectile lifetime in a retail run and compare with the flight-delay test. |
| Abilities | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Unit mechanics and spells" (`p_spell.c`, `p_tactics.c`); [SC_INITIAL_STATE_FINDINGS.md](SC_INITIAL_STATE_FINDINGS.md) "Confirmed". Creep is an instant radius test per [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Terrain and combat". | [test_abilities.c](../tests/starcraft/test_abilities.c): `Stim Packs, Spider Mines, Burrow, Recall, Stasis Field, Infestation, invincible status effects, Psionic Storm`. [test_initial_states.c](../tests/starcraft/test_initial_states.c): `initial CHK cloak and burrow state flags` | Implemented, unverified (creep part: Different) | #99 (creep) | Compare one ability's duration and cooldown in a retail run. Replace the creep radius test with native tile growth when #99 is addressed. |
| Transport and unloading | [SC_TRANSPORT_FINDINGS.md](SC_TRANSPORT_FINDINGS.md) "Confirmed current behavior and corrections" (Bunker unload is immediate); "Remaining passenger timers are not implemented" (Reaver timers) | [test_units.c](../tests/starcraft/test_units.c): `interceptors, scarabs, siege mode, add-ons, Bunkers, Archons, research, every spell` | Different (known) | #102 | Trace Reaver passenger cooldowns and Bunker placement candidates in a retail run. |
| Pathing and terrain | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Simulation terrain and random rolls (2026-11-12)" (OpenBW source); "MTXM bit 15 and creep (2026-11-13)"; "CV5 terrain placement flags (2026-11-13)"; "Fog: shared semantics, unverified retail geometry" | [test_map.c](../tests/starcraft/test_map.c): `CV5 terrain flags, dynamic creep, refinery exceptions and partial VF4 constrain placement`, `MTXM bit 15 is masked for graphics without granting Zerg creep placement` | Different (known) | #99 (creep); fog geometry: none filed | Compare one Dragoon shot across cover and high ground in a retail run. Resolve the fog geometry question. |
| AI (scripted and autonomous) | [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Computer players": "Retail campaign AI bytecode is absent". [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Campaign triggers and results": AI script actions are not executed. | [test_ai.c](../tests/starcraft/test_ai.c): `unit roles and strengths from DAT, retail openings, race doctrines for supply, workers, scouting`, `Zerg AI builds the Evolution Chamber and Spore Colony after seeing enemy air`. [test_tech_path.c](../tests/starcraft/test_tech_path.c) and the Protoss and Zerg variants via [tech_path_regression.h](../tests/tech_path_regression.h): `tech paths follow the requirement vectors and the AI buys along them` | Different (known) | #98 | Execute retail campaign AI scripts or document their absence per mission. Soak results do not establish parity. |
| Triggers and objectives | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Per-player trigger outcomes (2026-11-12)", "Any Unit trigger counts", "Trigger energy and shield actions (2026-11-12)", "Trigger resource amounts (2026-11-12)" (OpenBW source); [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Scenario semantics requiring focused implementation" | [test_mission.c](../tests/starcraft/test_mission.c): `campaign advance, triggers, elimination, supply and Terran 01` | Different (known) | #98 | Build a per-mission action coverage table for the 35 installed campaign CHKs, starting with actions 5/6, 15/16, 39, 42 and 47. |
| UI and HUD | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Shared gameplay and single-player flow (2026-10-08)" (HUD tip position and multi-selection wireframes, from user-supplied retail screenshots) and "Selected-unit status, equipment and compact font (2026-10-11)" (Kerrigan and SCV panel layout, from user-supplied screenshots). These are narrow points. [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "HUD/results/multiplayer": score, damaged wireframes and complete race panels are not exact. | [test_native.c](../tests/starcraft/test_native.c): `native dialogs, portraits, direction and movement verified` (the printed line includes "malformed assets rejected") | Different (known) | none filed | Compare each in-game panel with a retail screenshot set. Specify which narrow points are already matched. |
| Menus and front end | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Single-player glue screens (2026-10-09)"; "Native multiplayer screens (2026-10-09)" (Create screen matched to a user-supplied screenshot) and "Games browser versus map selection (2026-10-09)"; "Explicit remaining unknowns and limits" (race popup, Terran-only briefing art, synthetic score screen); "Options and saved-game dialogs (2026-11-12)" | [test_glue.c](../tests/starcraft/test_glue.c) (no PASS line; decodes `palnl/backgnd.pcx` and writes a BMP). [test_multiplayer.c](../tests/starcraft/test_multiplayer.c) includes [net_lobby_regression.h](../tests/net_lobby_regression.h): `native multiplayer discovery, map-title server, player count, race, speed, chat, readiness and map launch` | Different (known) | none filed | Compare each glue screen with a retail screenshot set, then implement the race popup and per-race briefing art. |
| Audio | [SC_AUDIO_FINDINGS.md](SC_AUDIO_FINDINGS.md) "Mute Unit Speech trigger actions" and "Confirmed gaps outside this package"; [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Saves/options/audio": `game_info.sound` is unset and sound event calls alone do not play audio | [test_sounds.c](../tests/starcraft/test_sounds.c): `StarCraft sfxdata bank, unit responses, annoyed clicks, iscript weapon and death sounds, advisor lines` | Different (known) | #101 | Trace script-timed sound events in a retail run and compare the played sample names with the sfxdata rows. |
| Animation and assets | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Confirmed: UI assets and layout are external"; "Unit image chain and terrain" (native GRP, FNT, DAT, TBL chain); "Animations, research, triggers and the in-level screens (2026-10-09)" ("IScript compiler corrections"). [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Animation": the runtime skips sound, movement and most combat opcodes. | [test_native.c](../tests/starcraft/test_native.c): `228 units, %d GRPs, %u native frames; malformed assets rejected`. [test_gameplay.c](../tests/starcraft/test_gameplay.c): `native animations, gathering, construction, training, research, combat, deaths` | Different (known) | #101 | Compare one unit's IScript sequence (the `SCVWalking` moving-glow case in "2026-10-11: moving SCV engine glow") frame by frame with a retail capture. |
| Save and load | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Options and saved-game dialogs (2026-11-12)": Load Saved lists retail `.snx` files and loading is not implemented. [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "StarCraft: interface, saves, audio and external compatibility": `G_SaveInfo` rejects `.snx`. [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Saves/options/audio" covers the engine `.sav` path. | [test_save.c](../tests/starcraft/test_save.c): `StarCraft mission save round trip and malformed runtime rejection` | Different (known) | none filed | Decide whether retail `.snx` import is in scope. If it is, add a parser fixture from a native save. |
| Multiplayer and determinism | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Native multiplayer screens (2026-10-09)": the session under those screens is the engine TCP lobby. "Establish retail network compatibility" is listed as Unknown in [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md). | [test_multiplayer.c](../tests/starcraft/test_multiplayer.c) via [net_lobby_regression.h](../tests/net_lobby_regression.h): `native multiplayer discovery, map-title server, player count, race, speed, chat, readiness and map launch`. Loopback protocol: [test_network.c](../tests/network/test_network.c) `deferred orders, selection independence, owner checks, stable IDs and production` (`test-network`). | Unknown | none filed | Determine the retail network protocol path. Until then, retail interop is unverified and only the engine protocol is tested. |
| Malformed input | [SC_FORMATS.md](SC_FORMATS.md) (format reference "checked against the pinned schemas and current C readers"); [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Complete glue asset and shortcut audit (2026-10-09)" (a decoder defect was exposed by the scan). No retail malformed-input trace is recorded. | [test_map.c](../tests/starcraft/test_map.c): `reordered/unknown CHK chunks, truncated sections, invalid dimensions, missing MTXM and invalid tile IDs`. [test_save.c](../tests/starcraft/test_save.c): `StarCraft mission save round trip and malformed runtime rejection`. [test_native.c](../tests/starcraft/test_native.c): `malformed assets rejected`. [test_ruleset_patch.c](../tests/starcraft/test_ruleset_patch.c): `CHK UNIx, UPGx, TECx and PUNI overlay the checked-in tables` | Implemented, unverified | none filed | Run a headless pass over all installed campaign CHKs and record accepted and rejected counts. |
| Restarts, ownership and cross-game regressions | None in the StarCraft findings. `make test-starcraft` runs only `tests/starcraft/` binaries. | [test_play.c](../tests/starcraft/test_play.c): `fog and CHK economy`. No restart test. Cross-game: `make test-model-commands` covers Dark Colony and Dark Reign only. | Unknown | none filed | Add a load, quit and reload test to `test-starcraft`, with an ownership check after the reload. |

## Test-file inventory

Counts are the number of `PASS` output sites in each file (`grep -c PASS`).
They are not runtime pass counts. The runtime count used for the baseline is the
number of test binaries, not PASS lines.

### tests/z

| File | PASS sites | Areas covered |
|---|---|---|
| [test_ai.c](../tests/z/test_ai.c) | 2 | Soak: computer plays LEVEL01, LEVEL02, LEVEL05 (`test-ai` only, not in `test-z`) |
| [test_capture.c](../tests/z/test_capture.c) | 6 | Flags, Jeeps, grenades, crates, APC boarding and ejection, malformed crews |
| [test_cliffs.c](../tests/z/test_cliffs.c) | 1 | Cliff blockers, rubble, save and load |
| [test_combat.c](../tests/z/test_combat.c) | 11 | Pyro flame, Fire Trucks, shells, vehicle thresholds, robots, squads |
| [test_commander.c](../tests/z/test_commander.c) | 41 | AI settings, ranking, reserves, garrisons, defence, bridges, cranes, task 7, reconstruction |
| [test_detour.c](../tests/z/test_detour.c) | 1 | Diversion to flags, crates and empty vehicles |
| [test_fort.c](../tests/z/test_fort.c) | 3 | Forts, win and loss |
| [test_hud.c](../tests/z/test_hud.c) | 1 | HUD, product previews, production window drag |
| [test_menu.c](../tests/z/test_menu.c) | 1 | Front end, replay, load, LAN browse, 2/3/4-player launch |
| [test_panel.c](../tests/z/test_panel.c) | 1 | Panel and portrait art decode |
| [test_production.c](../tests/z/test_production.c) | 1 | Gun stock, production cap, mixed placement |
| [test_squad.c](../tests/z/test_squad.c) | 0 | Formation grids and rotation (fail-only labels; exit status is the result) |
| [test_terrain_speed.c](../tests/z/test_terrain_speed.c) | 1 | Terrain speed modifiers and damaged vehicle speed |
| [test_units.c](../tests/z/test_units.c) | 6 | Level 1 loading, selection and movement, Grunts, Medium Tank, vehicles, statistics |

### tests/starcraft

| File | PASS sites | Areas covered |
|---|---|---|
| [test_abilities.c](../tests/starcraft/test_abilities.c) | 1 | Stim, mines, burrow, Recall, Stasis, Infestation, Psionic Storm, status effects |
| [test_ai.c](../tests/starcraft/test_ai.c) | 2 | Doctrines, openings, Zerg tech (`test-soak` only, not in `test-starcraft`) |
| [test_combat.c](../tests/starcraft/test_combat.c) | 1 | Weapons, damage types, shields, splash, glaive bounces |
| [test_construction.c](../tests/starcraft/test_construction.c) | 1 | Construction foundations for the three races |
| [test_foundation_readiness.c](../tests/starcraft/test_foundation_readiness.c) | 1 | Incomplete buildings and operational readiness |
| [test_gameplay.c](../tests/starcraft/test_gameplay.c) | 1 | Animations, gathering, construction, training, research, combat, deaths |
| [test_glue.c](../tests/starcraft/test_glue.c) | 0 | Glue palette and dialog decode (exit status is the result) |
| [test_initial_states.c](../tests/starcraft/test_initial_states.c) | 1 | Initial cloak and burrow flags |
| [test_map.c](../tests/starcraft/test_map.c) | 5 | CHK loading, MTXM, CV5, malformed CHK, upgrade and tech levels |
| [test_mining.c](../tests/starcraft/test_mining.c) | 0 | Mining cycle (RTS_CHECK labels) |
| [test_mission.c](../tests/starcraft/test_mission.c) | 1 | Campaign advance, triggers, elimination, supply, Terran 01 |
| [test_multiplayer.c](../tests/starcraft/test_multiplayer.c) | 0 | Lobby launch (through the shared header `net_lobby_regression.h`) |
| [test_native.c](../tests/starcraft/test_native.c) | 1 | Native unit assets, GRPs, malformed assets, dialogs |
| [test_orders.c](../tests/starcraft/test_orders.c) | 0 | Hold, attack, move-only orders (RTS_CHECK labels) |
| [test_play.c](../tests/starcraft/test_play.c) | 1 | Move and attack, mineral hauling, construction, training, fog, CHK economy |
| [test_projectiles.c](../tests/starcraft/test_projectiles.c) | 1 | Projectile flight, aim, upgrades, dead shooter, save |
| [test_ruleset_patch.c](../tests/starcraft/test_ruleset_patch.c) | 1 | CHK UNIx, UPGx, TECx and PUNI overlays |
| [test_save.c](../tests/starcraft/test_save.c) | 1 | Mission save round trip and malformed saves |
| [test_sounds.c](../tests/starcraft/test_sounds.c) | 1 | sfxdata bank, unit responses, weapon and death sounds, advisors |
| [test_tech_path.c](../tests/starcraft/test_tech_path.c) | 0 | Terran tech paths (shared header [tech_path_regression.h](../tests/tech_path_regression.h)) |
| [test_tech_path_protoss.c](../tests/starcraft/test_tech_path_protoss.c) | 0 | Protoss tech paths (shared header) |
| [test_tech_path_zerg.c](../tests/starcraft/test_tech_path_zerg.c) | 0 | Zerg tech paths (shared header) |
| [test_units.c](../tests/starcraft/test_units.c) | 1 | Interceptors, scarabs, siege, add-ons, Bunkers, Archons, research, spells |
| [test_zerg.c](../tests/starcraft/test_zerg.c) | 1 | Larvae, eggs, hatchery tiers, colonies, creep, psi |

## Known differences without an issue

Only differences that no issue in #96 to #102 names are listed here. Each is
taken from the section named. None has an issue filed. Filing is not done by
this file.

| Difference | Source (doc + section) | One-line proposed investigation |
|---|---|---|
| StarCraft retail `.snx` load is not implemented | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Options and saved-game dialogs (2026-11-12)" | Build a parser fixture from a native `.snx` and compare its load with the engine path. |
| Race selection cycles a button instead of the retail popup; briefing always uses Terran art | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Explicit remaining unknowns and limits" | Capture the retail race popup and per-race briefing art and implement the popup. |
| Score screen is a synthetic engine result menu | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Explicit remaining unknowns and limits" | Capture the retail score screen after a campaign victory and compare its fields. |
| MRGN location count: open-rts accepts up to 256; OpenBW and PyMS maximums differ | [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "Second audit, 2026-11-13" | Confirm the retail maximum from sample CHK files and the pinned sources before choosing a cap. |
| Sound priority channel arbitration is not implemented | [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "StarCraft: interface, saves, audio and external compatibility" | Establish the victim and tie policy from retail sound traces, then implement arbitration. |
| Upgrade rows 47 to 61 have no runtime definitions | [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "Second audit, 2026-11-13" and "Completeness recheck (2026-10-11)" in [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) | Check which PUPx rows the installed campaign CHKs reference. |
| Z T, D and Z front-end buttons are unported | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Trace each button's handler in ZED.EXE and compare the click result in DOSBox-X. |
| Z grenade tracked-scatter radius (`+0x67` bit `0x80`) is not modelled | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Unit statistics" | Trace the radius branch at `0x75d04` and compare a tracked grenade landing. Adjacent to #96 but not named there. |
| Z mobile missile type `0x18` secondary state is not modelled | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Unit statistics" | Trace the type-0x18 branch and record one volley in DOSBox-X. Adjacent to #96 but not named there. |
| Z robot hospital and ICBM launcher sprites are not used by any retail level | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Confirm no LEVEL file places them, then record them as out of scope. |

## Unknowns needing an investigation plan

| Unknown | Source (doc + section) | One-line investigation |
|---|---|---|
| Meaning of CPUPLR setting byte `0x8a3` | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Cliffs, vehicle objectives and per-level computer orders" and "Native computer scoring and campaign settings" | Trace every reader of `0x8a3` in ZED.EXE with radare2 and test one changed value in DOSBox-X. |
| How the fort releases its starting robots (`do_fort_sequence`) and how CPUPLR groups are chosen | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Break at the fort start in DOSBox-X on LEVEL01 and record the released robot groups. |
| Grenade burst sprites, radius and damage per unit; throw range limit | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Decode the burst routine and compare one grenade hit against a Grunt in DOSBox-X. |
| Robot `+0x24`, weapon `+0x21`/`+0x24`, body `+0x18`/`+0x19` fields | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Map each field's reads and writes in ZED.EXE and set one value in a fixture. |
| `BRIDGEnn.DAT`, `MULT.DAT`, the five unknown object-record bytes, and the Z `DOOR.PAC` members | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet"; MULT.DAT is covered in "Production availability thresholds" | Diff the files across two levels and correlate changes with the object list. |
| Source of the task 7 service request (the producer) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Indirect writes and retail runtime follow-up" ("A gameplay trigger for task 7 remains unverified") | Set a DOSBox-X write watchpoint on the task byte during a full campaign turn. |
| Movement height transition and path-node rules | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Cliffs, vehicle objectives and per-level computer orders" | Trace the transition routine and test a ramp crossing for one unit in DOSBox-X. |
| Native missile reaction (action `0x2b`): displacement and damage immunity | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Commander passes and unit statistics" and [ZOD_STARGUS_HANDOVER.md](ZOD_STARGUS_HANDOVER.md) "Native trace continuation" | Trace action `0x2b` (`0x62cec`) and record a unit under a missile burst. |
| Retail view behaviour at viewport edges for the production window drag | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Verification and remaining differences" | Drag the window to each edge in DOSBox-X and record the clamp. |
| Z audio: sample rates and the sound-driver calls | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Not decoded yet" | Trace the audio calls in ZED.EXE and record the rates used with DIG.INI. |
| Z retail multiplayer and seed behaviour (`0x49c77`) | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Random numbers" | Decide if two DOSBox-X instances can run retail multiplayer; if not, mark interop out of scope. |
| Zero-survivor presentation of a captured fort | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) "Forts" ("native zero-survivor presentation is not established") | Empty a fort in DOSBox-X and record the screen. |
| Campaign AI script execution and sequencing per mission | [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Computer players" | Inventory the AI script actions in each installed campaign CHK and trace one in the retail data. Covered by #98. |
| Trigger action 42 (Set Doodad State): raw state 0 meaning | [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "Third audit, 2026-11-13" | Find a retail run or reference that gives state 0 a meaning. Do not assign one by guess. Covered by #98. |
| StarCraft fog geometry | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Fog: shared semantics, unverified retail geometry" | Compare the fog boundary for one unit in a retail screenshot with the engine result. |
| Most and Least trigger conditions (score ranking) | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) "Most Kills", "Most Resources" and "Highest/Lowest Score" sections | Compare one trigger's result with a retail run of the same map. |
| Retail network compatibility and cross-platform deterministic replay | [Z_STARGUS_WORKLIST.md](Z_STARGUS_WORKLIST.md) "StarCraft: interface, saves, audio and external compatibility" | Establish the retail protocol path and record one replay hash comparison. |

## Out of scope (stated in the repository)

- StarCraft Brood War gameplay and expansion campaigns. The completeness audit
  says catalog coverage "is not Brood War gameplay coverage"
  ([BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md) "Expansion").
- Z DOS demo and the Z95 replay edition. [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md)
  analyses them, but the Replay file is not matched to a physical pressing and
  no parity claim is made for it.
- Native StarCraft `.snx` loading is listed as a known difference above, not as
  out of scope.

## Runnable commands and dated baseline

Commands from issue #103 (run from the repository root; the Z and StarCraft
suites need the native data under `data/`, and the network suite needs
loopback permission):

```sh
make
env SDL_VIDEODRIVER=dummy TEST_JOBS=8 TEST_BUDGET=30 make test-z
env SDL_VIDEODRIVER=dummy TEST_JOBS=8 TEST_BUDGET=30 make test-starcraft
make test-network
```

Other targets named in the docs: `make test-ai` (soak, long), `make test-model-commands`
(Dark Colony and Dark Reign), `make test-loaders`.

**Dated baseline, not parity evidence.** Issue #103 records that at commit
`49b5bfc0` on 2026-11-13, `make test-z` passed 27 test binaries and
`make test-starcraft` passed 23. These counts were taken from the issue text.
They were not re-run for this file. A passing headless test shows that the
open-rts path works. It does not show retail equivalence.

## Acceptance checklist for this file

- Every linked path exists (checked with `ls` before commit).
- Every test name in an "Automated coverage" cell appears in the named file
  (checked with `grep -F`).
- `git diff --check` is clean.
- The file contains no retail game content. Only names, sizes and hashes are
  recorded.
