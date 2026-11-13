# Z and StarCraft first-wave worklist

This is a scoped register of concrete follow-up work found in the current
source and local references. It is not a parity claim or a complete audit of
either retail game. “Confirmed” describes the inspected source/test state;
“Partial” means a working path has a documented gap; “Unknown” means the
available evidence does not establish the native behavior. Reference-source
behavior is identified separately from executable-confirmed behavior.

The worklist is a temporary snapshot checked against the current tree on 2026-11-13 and
reconciled after `cef9178e` / `1fab3bf8`. The broad
StarCraft rows in [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md)
are dated 2026-10-11 and should not be used without its later corrections or
newer focused findings. Per-player CHK research rules and a subset of
projectile visuals landed in `0746869e`, and Z Pyro fire landed in `5948c07b`.

## Closed in this wave

| Item | Evidence and verification | State |
|---|---|---|
| StarCraft score and inactive-slot variables | Commit `d08455b6`; see the focused findings and tests in [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md). | Done in commit. This closes that narrow score/slot issue; score parity as a whole remains unverified. |
| Z production window drag frame | Commit `571aad2b`; drag behavior is covered in [test_hud.c](../tests/z/test_hud.c). | Done in commit. This does not establish full factory UI or production parity. |
| StarCraft starting research and selected projectile visuals | Commit `0746869e`; see [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md#chk-per-player-research-rules-2026-11-13) and [SC_PROJECTILE_VISUALS.md](SC_PROJECTILE_VISUALS.md). Full StarCraft suite: 20/20. | The supported CHK rules and selected visual chains are implemented; expanded research rows, arbitrary IScript behavior and other projectile visuals remain open. |
| Z Pyro flame and ground fire | Commit `5948c07b`; see [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md) and [test_combat.c](../tests/z/test_combat.c). Full Z suite: 27/27. | Projectile flight, ground fire, selected unit damage and save state are implemented. Native queues, map-object interactions and other documented edges remain open. |

## Independent implementation packages

### Second audit, 2026-11-13

The second batch added extended StarCraft MRGN locations in `6aa9de99` and
the Glaive Wurm intermediate-bounce visual in `c2af8e2f`. The Z commander
building-attack task needs further work on coordinate-target firing.

- Z's native task 6 uses the first territory building and descriptor attack
  coordinates, including a destroyed first building; the current AI requires
  a live object target. Vehicle knockdown and avoidance, the APC's separate
  scanner and coordinate firing modes, zero-HP boundaries, native slot order,
  and some projectile interactions remain separate investigations; see the
  corresponding executable traces in [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md).
- StarCraft `MRGN` now accepts up to 256 locations; the test above location 64
  is synthetic, and the OpenBW and PyMS maximum counts differ. Trigger action
  42 (doodad state) occurs in bundled Terran04,
  Zerg05 and Protoss06, but its runtime behavior is absent. Actions 5/6
  (pause/unpause) remain unimplemented and are now reported once per mission;
  leaderboard actions and Talking Portrait store saved state without a display
  (see SC_EXE_FINDINGS "Trigger coverage, leaderboards, portrait and AI script
  state"). Campaign AI script IDs and locations are now preserved per player,
  but every script except shared vision still falls back to a shared planner. These are distinct from already supported
  Play WAV, score, resources and campaign transitions.
- Incomplete construction selection, transport placement, creep lifecycle,
  additional weapon behavior, general IScript playback and script-timed
  sounds remain separate StarCraft packages. Expanded upgrade rows 47–61 have
  no runtime definitions. Engine `.sav` loading exists; retail `.snx` does not.
  Consult [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) and
  [SC_PROJECTILE_VISUALS.md](SC_PROJECTILE_VISUALS.md) for evidence boundaries.

### Third audit, 2026-11-13

- Campaign actions 30/31 mute and unmute unit speech. This is implemented in
  `1a9565c1`, with per-SFX-row filtering and save/load coverage in
  `tests/starcraft/test_sounds.c` and `test_save.c`; trigger WAV playback stays
  separate. The actions occur 121 and 119 times respectively in 35 installed
  campaign CHKs. Set Doodad State occurs 69 times, targeting THG2 trap and door units. Its
  frequently occurring raw state 0 is not explained by the inspected PyMS,
  Stargus or OpenBW sources and must not be assigned a guessed meaning.
- This audit found that the worker-held production timer created a completed
  building only at the end. `cef9178e` replaced that path with incomplete
  sites and distinct SCV, Probe and Drone lifecycles; remaining limitations are
  recorded in [SC_CONSTRUCTION_FINDINGS.md](SC_CONSTRUCTION_FINDINGS.md).
- Mobile unload now ends the order when placement fails, implemented in
  `3f852201`; it no longer retries every tick. Placement reachability, boundary
  behavior, exact candidate traversal and passenger cooldowns still differ
  from the OpenBW reference. The older claim that no placement search exists
  is stale: the current code searches, but its range and Bunker handling differ.
- Z's task-6 coordinate path is traced further through map-effect, bridge and
  group checks, but its handoff to weapon dispatch is still unknown. APC
  scanner and coordinate firing modes require distinct attack provenance.
  Pyro ground-fire BUILD/bridge scaling was implemented in `8f54232b`, with
  the Repair Facility bounds corrected in `a3bf5972`.

The third batch implemented Pyro building/bridge scaling in `8f54232b`,
unit-speech mute/unmute triggers in `1a9565c1`, and a subset of transport
unload rules in `3f852201`. Transport reachability and passenger cooldowns
remain open; see [SC_TRANSPORT_FINDINGS.md](SC_TRANSPORT_FINDINGS.md).
The remaining Pyro map-effect and APC paths remain separate work.

The next batch added the 15-frame mobile unload timer in `ad8ec669` and
incomplete construction sites in `cef9178e`. It also corrected the previously
misread Z Repair Facility bounds in `a3bf5972`. StarCraft construction save
validation is implemented and covered by malformed-save and round-trip cases in
`tests/starcraft/test_save.c`; the current StarCraft save layout is 17. The
listed suite counts describe the earlier verification snapshot (StarCraft
22/22, Z 27/27), not a claim that every current host can run network tests.
These changes do not establish complete retail parity.

### Fourth batch, 2026-11-13 (issues #96–#103)

Seven packages ran in parallel worktrees and were merged together. Suites
after integration: Z 28/28, StarCraft 30/30, the other five games and the
network suite unchanged. Z save layout is 28 and StarCraft save layout is 21.
None of this establishes retail parity; see
[RETAIL_PARITY_MATRIX.md](RETAIL_PARITY_MATRIX.md).

| Issue | Landed | Still open (evidence needed) |
|---|---|---|
| #96 | Z units survive exactly zero health after bullet and shell hits (`games/z/p_damage.c`, `tests/z/test_damage.c`). Native zero is stored as engine hp 1 plus `z.zero_hp`; read retail health through `Z_NativeHp`. | All seven `0x86ca4` callers traced and ground fire and box blast converted (run-over is not a caller); byte wrap proven unreachable; knockdown continuation traced to `0x790c9` (see findings); movement-pattern lead and bullet slot order; Laser tracer tables. |
| #97 | APC scanner Pyro salvo: gate below 10, reload before ports, 12-attempt cap, travelling flames (`games/z/p_apc.c`). Command 5 dispatches to `0x7c817`, not `0x7c560`. | Scanner target acquisition bin lists; task 6 robot action 8 and vehicle actions `0x35..0x3c`; coordinate mode needs command 12. |
| #100 | Trace only: Fire Truck flame reaction handler `0x24c14`, the `0x191ba8` sprite pool (not placed map objects), display-list capacities. | Initial nozzle byte, action `0x3c` cadence, `0x59ffb` shot type; creators of pool types 5, 17, 34. |
| #98 | Unsupported trigger types are reported, not silently completed; leaderboard, Talking Portrait and AI script ID/location are stored, saved and hashed (`p_trigops.c`, `test_triggers.c`). | Pause/Unpause, Set Doodad State (raw state 0), AI script execution, leaderboard display: pinned OpenBW aborts on all of them, so a retail `starcraft.exe` dispatcher trace is required. |
| #99 | Per-frame integer construction HP/shields per OpenBW; unfinished Shield Battery no longer recharges (`test_construction_frames.c`, `test_service_audit.c`). | Creep growth/recession, IScript completion signals for Protoss/Zerg, manual cloak/burrow toggle handlers. |
| #101 | IScript runtime for waits, branches, calls and timed sounds with an opcode inventory over the real `iscript.bin` (`p_iscript.c`, `test_iscript.c`, `test_iscript_inventory.c`). | Delayed damage for appear-on-target weapons, bounce target order, sound priority policy (OpenBW `play_sound` is empty), idle-loop RNG draws. |
| #102 | Reaver 30-frame post-unload delay in the attack path, passenger weapon/spell cooldowns, OpenBW ring placement (`p_place.c`, `test_transport.c`, `test_placement.c`). | Pixel-contour terrain fit, spatial-finder blocker order, spell cooldown re-arm after cast. |
| #103 | Evidence ledger in `RETAIL_PARITY_MATRIX.md`. | No retail StarCraft run is recorded; most rows are reference-source only or unknown. |

### Z: commander, orders and object lifecycle

| Status | Work item | Evidence and next check |
|---|---|---|
| Partial | Complete remaining commander dispatch and object-search semantics, including conditional selectors, stale-slot delays, and less common effect/object types. | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#additional-traced-branches-and-explicit-remaining-scope) and the later “Commander passes and unit statistics” findings document traced branches and explicit unknowns. Existing regression areas include [test_commander.c](../tests/z/test_commander.c), [test_squad.c](../tests/z/test_squad.c), and [test_units.c](../tests/z/test_units.c). Extend a focused case for each confirmed missing branch; executable trace is required where the findings mark selection/reset behavior unknown. |
| Partial | Finish follower/capture, destination checks, and cooldown edge behavior beyond the fixes already landed. | The earlier flag-followers, local-flag destination and tick-cooldown threads have fixes in commits `5aa7c6d8`, `01074257`, and `f05b544f`. Do not refile those as open. Broader follower formation and command continuation are still recorded unknown in [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#native-squad-leadership). Verify any new edge with [test_capture.c](../tests/z/test_capture.c), [test_squad.c](../tests/z/test_squad.c) or [test_commander.c](../tests/z/test_commander.c); missing native continuation behavior requires trace. |
| Partial | Complete vehicle combat selection, shell flight/interception, and body degradation rules. | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#shell-blast-selection-2026-11-12), [shell flight](Z_EXE_FINDINGS.md#shell-flight-2026-11-12), and [vehicle degradation](Z_EXE_FINDINGS.md#vehicle-body-degradation-2026-11-12) distinguish implemented portions from unmodeled scatter, terrain/interception, update order, and state transitions. Tests are in [test_combat.c](../tests/z/test_combat.c). Trace each remaining retail-only unknown before choosing constants or heuristics. |
| Partial | Native artillery/weapon visuals and uncommon projectile/effect lifecycles. | The Z findings document unimplemented Laser drawing and effect/search cases; see [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#shell-flight-2026-11-12) and following weapon sections. `tests/z/test_combat.c` covers current projectile behavior. Separate visual/behavioral regression from trace work; do not infer retail presentation from Zod alone. |

### Z: production and native interface

| Status | Work item | Evidence and next check |
|---|---|---|
| Partial | Finish production commands, queue behavior, factory service and native panel actions. | `571aad2b` fixes dragging by the native frame. [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#verification-and-remaining-differences) and [test_production.c](../tests/z/test_production.c) are the current evidence. Add focused regressions for any remaining reproducible command/UI defect; native control semantics not yet traced require executable inspection. |
| Partial | Finish frontend/high-score and multiplayer setup actions. | [Z_EXE_FINDINGS.md](Z_EXE_FINDINGS.md#retail-main-menu-and-multiplayer-setup-2026-10-18) records supported layouts and remaining menu boundaries. The High Scores background can open/dismiss, but score population and some retail actions remain incomplete. Verify with [test_menu.c](../tests/z/test_menu.c) and [test_hud.c](../tests/z/test_hud.c); native binary/UI trace is needed for unknown transitions. |

### StarCraft: CHK scenario semantics

| Status | Work item | Evidence and next check |
|---|---|---|
| Partial | Implement missing trigger condition/action families and full property semantics; stop silently treating unsupported actions as completed. | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md#scenario-semantics-requiring-focused-implementation) identifies condition IDs, Play WAV/leaderboard/score/property/doodad/pause actions, AI-script execution, Move Unit versus Order, and UPRP fields. [test_mission.c](../tests/starcraft/test_mission.c) is the focused suite. Add mission fixtures for each supported opcode; determine retail semantics from native CHK documentation/data or executable trace for unresolved cases. |
| Partial | Expand location/player coverage and mission-level campaign verification. | Current runtime caps locations and player groups as described in the same scenario findings. `tests/starcraft/test_mission.c` checks selected trigger behavior, not complete campaigns. Add representative mission fixtures and a full campaign playthrough before making campaign-completion claims. |

### StarCraft: simulation and controls

| Status | Work item | Evidence and next check |
|---|---|---|
| Partial | Complete worker construction-site selection/lifecycle, transport placement, and less common queued/patrol/repair order edges. | [BLIZZARD_COMPLETENESS_AUDIT.md](BLIZZARD_COMPLETENESS_AUDIT.md#starcraft) gives a source-audit snapshot; newer details are in [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md#queued-player-orders). Relevant regressions: [test_orders.c](../tests/starcraft/test_orders.c), [test_gameplay.c](../tests/starcraft/test_gameplay.c), [test_units.c](../tests/starcraft/test_units.c). These are reference-source comparisons; exact native collision/placement timing needs retail trace. |
| Partial | Fill ability visual/status edges and special-unit lifecycle details. | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) ability and special-unit sections identify missing visual overlays, transport placement behavior, and approximated timing. Verify each with [test_abilities.c](../tests/starcraft/test_abilities.c), [test_units.c](../tests/starcraft/test_units.c), and [test_zerg.c](../tests/starcraft/test_zerg.c). The existence of a command or catalog entry does not establish full ability parity. |
| Partial | Complete combat terrain/visibility/pathing and non-covered projectile cases. | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) “Simulation terrain and random rolls” and “Moving-weapon investigation” document current source evidence and approximations. [test_combat.c](../tests/starcraft/test_combat.c) and [test_projectiles.c](../tests/starcraft/test_projectiles.c) cover selected cases. Per-weapon behavior not covered by those tests requires a native data/reference audit or executable trace. |

### StarCraft: interface, saves, audio and external compatibility

| Status | Work item | Evidence and next check |
|---|---|---|
| Partial | Add retail `.snx` load compatibility and finish unsupported options. The current UI supports engine `.sav` save/load; `.snx` entries can be listed but `G_SaveInfo` rejects them. Game speed and sound volume apply, while mouse/key scroll settings and video options remain unsupported/disabled. | [m_menu.c](../games/starcraft/m_menu.c) scans `.sav`/`.snx`, routes `.sav` through `G_SaveInfo`, and applies selected speed/sound settings; the precise documented boundary is [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md#options-and-saved-game-dialogs-2026-11-12). Verify engine save flows with [test_save.c](../tests/starcraft/test_save.c) and menu controls with [test_glue.c](../tests/starcraft/test_glue.c)/[test_gameplay.c](../tests/starcraft/test_gameplay.c). Retail `.snx` format remains unimplemented and needs format evidence. |
| Partial | Complete music, briefing speech, missing sound events and script-timed sound playback. | [SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md#completeness-recheck-2026-10-11) and the detailed audio inventory in the current findings list missing event classes. Verify event selection/timing against assets or executable before claiming native behavior; use [test_sounds.c](../tests/starcraft/test_sounds.c) for implemented sound paths. |
| Partial / native behavior incomplete | Implement StarCraft `sfxdata.dat` SoundPriority channel arbitration and `never_preempt` after resolving victim and tie policy. PyMS [Hints.txt](../reference/PyMS/PyMS/Data/Hints.txt) says higher numeric priority may override a lower-priority active sound when no channel is free; `SoundsDAT.py` defines the `never_preempt` flag at bit 5. Current `games/starcraft/sounds.c` does not load these fields and `sound/s_sound.c::start` drops requests when all mixer channels are occupied. Do not pass SoundPriority through `sfxinfo_t.priority`: `S_Bark` uses that field for DC's lower-number-wins bark selection. Exact victim choice, equal priority, non-preemptible channel handling, and retail channel count need authoritative trace/reference plus an exhaustion regression. Details: [SC_AUDIO_FINDINGS.md](SC_AUDIO_FINDINGS.md#rejected-interpretation-soundpriority-is-not-bark-priority). |
| Unknown | Establish retail network compatibility and cross-platform deterministic replay/resync. | Current [test_multiplayer.c](../tests/starcraft/test_multiplayer.c), [test_save.c](../tests/starcraft/test_save.c) and engine network tests establish selected internal paths only. Run sustained multi-peer and cross-platform deterministic sessions; retail wire/save compatibility requires protocol/format evidence. |
| Confirmed scope limit | Brood War gameplay and full campaign parity are not established by catalog/CHK loading. | The dated audit explicitly distinguishes asset coverage from functional unit coverage. No Brood War completion claim is made here; scope it as a separate audit after base-game scenario verification. |

## Shared validation and source limits

- Keep fixes in independent packages where possible: Z commander/combat, Z
  production/interface, StarCraft CHK triggers, StarCraft simulation/orders,
  and StarCraft glue/save/audio have separate focused tests and references.
- Run focused binaries headlessly with `SDL_VIDEODRIVER=dummy`. `make test-z`
  and `make test-starcraft` are generated by the `MODEL_TESTS_FOR_GAME` Makefile
  macro; `make test-ai` runs the longer AI soak. For the Z window drag change,
  use [test_hud.c](../tests/z/test_hud.c); [test_production.c](../tests/z/test_production.c)
  checks production simulation and rules.
- A passing regression shows the tested implementation path works. It does
  not prove retail equivalence. OpenBW, Stargus, PyMS, Zod and other references
  are useful source evidence with their own boundaries; only executable traces
  or observed retail behavior support claims about the retail executable.
- Recheck the current tree and focused tests before treating any broader row as
  open or complete; this register is a dated audit, not a live parity claim.
