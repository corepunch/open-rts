# StarCraft and Warcraft II completeness audit — 2026-10-11

Neither game is complete. Warcraft II has the broader working base-game
mechanics, but only the first mission of each campaign has a victory objective.
StarCraft has substantial three-race gameplay, but still lacks essential orders,
several abilities, usable save/load, gameplay audio, and full scenario semantics.
Loading every unit graphic is not equivalent to implementing every unit.

## Scope and evidence

Audited open-rts `69ad9a2c` against these existing, unchanged local checkouts:

| Reference | Commit | Use |
|---|---|---|
| `reference/wargus` | `cde1a0718a0058cc651ecd56ff8149fc39f624e9` | PUD documentation, unit/command/spell definitions, campaign objectives and restrictions |
| `reference/stargus` | `2a4d54604949e4772f6638a8412c8ebd62444044` | CHK reader, IScript documentation, unit/command/audio definitions, documented omissions |
| `reference/stratagus` | `3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc` | Shared reference engine, previously documented movement/resource/UI rules |

This is a source and regression audit, not a retail playthrough, a new executable
disassembly, or certification of pixel/timing equivalence. No retail executable
was run. Existing native fingerprints and evidence remain in
[SC_EXE_FINDINGS.md](SC_EXE_FINDINGS.md) and
[WAR2_EXE_FINDINGS.md](WAR2_EXE_FINDINGS.md). External provenance is in
[REFERENCES.md](../REFERENCES.md#blizzard-completeness-recheck-2026-10-11).

**Confirmed** below means present or absent in the inspected implementation.
**Partial** means there is a working path with concrete missing semantics.
**Unverified** means existing evidence does not establish retail equivalence.
The priority order is an implementation recommendation, not an original-game rule.

## Warcraft II

| Area | Current coverage | Remaining work and evidence |
|---|---|---|
| Native assets and maps | WAR/PUD loading, native terrain, unit graphics, dialogs, fonts, sounds; base catalog and original heroes have focused tests | Expansion campaigns/assets are not certified. `w_pud.c` decodes selected UDTA/UGRD fields; this does not establish every PUD field's semantics. |
| Economy and production | Gold, lumber, oil, refiners, building construction/assistance/repair, unit production, research and transformations | Exact placement, congestion/exit behavior, limits and timing remain fidelity work; see the dated naval and gameplay findings. |
| Combat and abilities | Projectiles, splash, target domains, detection, mana, buffs and 19 base spell/action definitions | No attack-ground command in `sb_bar.c`/`g_hooks.c`; Wargus explicitly supplies it in both race button tables. Tile-wall/rock destruction and exact missile trajectories/anchors remain open. |
| Transport and orders | Boarding, six passengers, shore unloading, sinking casualties, patrol, stand ground and return goods | `p_transport.c` unloads the carrier's passengers together; `sb_bar.c` has no passenger-selection panel. Full inland-click shore selection and allied-owner boarding remain open. |
| Computer players | Shared economy/research planner plus `p_ai.c` naval targeting, ferrying, air waves, rallying and ten selected combat spells | This is broader than the October 7 audit. It does not execute each campaign's Wargus/retail AI, or cover every Wargus `ai-cast` policy. |
| Campaigns | Native loading, briefing/result transitions, first Human/Orc building objective, next-map flow | **Blocking:** `W2_CheckVictory` explicitly returns for campaign numbers other than 1. Human/Orc missions 2–14 therefore cannot achieve their intended victory through this path. Rescue/regions/targets, restrictions, scripted reinforcements, chapters and ending need implementation. |
| Saves and results | Engine saves, campaign identity/RNG extras, native result screens | Result statistics/rank/name fields remain unbound: `show_stats` selects scene 3059 and fills its outcome, not the full scoreboard. Native save compatibility is not established. |
| Multiplayer | Shared TCP session with native menu, discovery, race/speed/chat/readiness and map launch | Retail network compatibility, diplomacy/team-victory equivalence and cross-platform determinism are not certified. `living_sides` counts surviving player slots, not allied teams. |
| Audio/presentation | Native WAV bank and sound events tested | Music/CD playback, narration, exact animation cadence and full native scene behavior must not be inferred from functioning SFX or decoded dialogs. |

The campaign gap has direct reference examples:

- `reference/wargus/campaigns/human/level02h_c.sms` requires rescuing
  archers/rangers and returning them near a Circle of Power, and defines a
  mission-specific allowed-unit list.
- `reference/wargus/campaigns/orc/level02o_c.sms` has rescue and distinct defeat
  conditions; `level03o_c.sms` requires a shipyard and four oil platforms.
- `campaigns/human-exp/` and `campaigns/orc-exp/` are expansion references,
  separate from finishing the base campaigns.

An ALOW decoder does **not** close the campaign restriction gap.
`w_pud.c::w2_decode_allow` itself labels its layout as recalled and untested
against an original-editor map. Its synthetic regression establishes internal
behavior, not native fidelity. Keep this **unverified** until native evidence
or authoritative format documentation establishes the layout. Wargus's
`pud.cpp` ignores ALOW and leaves UDTA/UGRD handling as TODOs; those paths
cannot independently validate our decoder.

## StarCraft

| Area | Current coverage | Remaining work and evidence |
|---|---|---|
| Native assets | All 228 unit slots load; native GRP/PCX/FNT/DAT/TBL/terrain, dialog BIN and SMK portrait/menu support | Catalog coverage is graphic/data coverage, not 228 fully functional units. Runtime uses extracted files; original MPQ and retail save/replay compatibility are separate work. |
| Three-race economy/production | Mining/gas/supply, training/research, Terran add-ons, Zerg larvae/eggs/cocoons/building tiers, Protoss power, cancellation and rally | Worker-held construction sites are not selectable unfinished buildings. Race-specific construction lifecycle and exact placement/release timing remain partial. |
| Core orders | Move, stop, target attack, contextual gather, build/train/research, rally, bunker load/unload and implemented abilities | **Missing:** Repair, Hold Position, attack-move, Patrol, explicit Return Cargo and Terran lift/land behavior. Existing engine order enums or reference buttons do not implement these game paths. See `g_hooks.c`, `p_units.c::sc_order`, and `m_menu.c` command cards. |
| Abilities | `p_spell.c::spells` contains 19 entries including siege, cloak, nukes, Matrix, Lockdown, EMP, Irradiate, Yamato, Queen/Defiler spells, Storm, Hallucination and Archon Warp | **Missing:** Stim Packs, Spider Mines, Burrow, Recall, Stasis Field and Infestation. `p_prod.c::tech_research` explicitly omits the first five; no Infestation action exists. Exact spell geometry/timing and hero caster mapping are not comprehensively certified. |
| Special units | Carrier/Reaver hangars, Bunkers, add-ons, siege transformation, Archons; Shield Battery recharge exists | Dropship/Shuttle/Overlord transport is not complete. `sc_can_board` applies the Bunker infantry restriction to any carrier with capacity, and loading uses four cargo slots. This is an incomplete shared path, not zero transport code. Nydus is buildable but has no transit behavior. |
| Terrain and combat | Weapon/armor/shield model, cloak/detection, splash and psi checks | Creep is an instant radius test in `p_zerg.c`, not native tile growth/recession or drawn creep. Complete height/visibility/miss rules, native projectile behavior and precise pathing remain unverified. Shield Battery automatic nearby recharge is an engine implementation, not full retail order fidelity. |
| Animation | IScript-derived states, attack/death/work sequences, native direction frames and selected overlays | `w_assets.c::animation` skips sound, movement and most combat opcodes. Death overlays/remnants run sequentially. This is not a complete runtime IScript interpreter. |
| Scenarios/campaigns | CHK placements, trigger conditions/actions, briefing text/portraits, objectives, outcomes and next-mission flow | **Blocking:** only a trigger subset is executed; unhandled actions fall through silently. AI-script actions enable the shared AI rather than execute the script ID. No full campaign playthrough has been verified. Details below. |
| Map rules/research | UNIS/UNIx, UPGS/UPGx, TECS/TECx patches and PUNI build restrictions | `w_chk.c` does not consume UPGR/PUPx or PTEC/PTEx per-player research/technology restrictions and initial levels. Cost overrides and PUNI are different data; their tests do not cover this gap. |
| Computer players | Three-race doctrine/openings, research, selected caster tactics, siege, Bunker staffing and hangar refill | Retail campaign AI bytecode is absent. Matching selected opening purchases in `test_ai` is not executing a full retail AI script. |
| Saves/options/audio | Native save/load/options screens and gameplay sound call sites exist | **Missing:** save/load activation returns an unavailable message; speed/sound/video screens do not apply options. There is no StarCraft `game_info.sound` bank, so `S_Init` returns false and gameplay sound calls have no registered samples. Briefing/transmission WAV playback is absent. |
| HUD/results/multiplayer | Native command/status assets, queue controls, portrait playback, score and TCP lobby flows | Score uses engine counters/stock deltas; exact retail score, damaged wireframes, complete race panels, retail networking and cross-platform determinism remain unverified. |
| Expansion | Some expanded CHK rule chunks and all catalog slots are accepted | This is not Brood War gameplay coverage. Expansion production, abilities, campaigns and installation precedence need a separate audit after the base game. |

### Scenario semantics requiring focused implementation

`p_trig.c` demonstrates gaps beyond a generic statement that triggers are partial:

- `condition_ok` supports IDs 1–7, 11, 12, 14–17, 22 and 23. Other condition
  IDs return false. A mission that relies on another condition can stall.
- `run_actions` has no execution path for Play WAV (8), leaderboard actions,
  several score/property actions, or doodad state. Pause/Unpause (5/6) are
  skipped. Transmission (7) displays text/waits without its audio.
- Run AI Script (15/16) sets `ai_on`; it ignores the actual script program.
- Move Unit (39) and Order (47) share `P_MoveUnitTo`, so instant relocation
  and distinct patrol/attack order semantics are not fully represented.
- `apply_property` handles owner, HP, invincibility and hallucination, not the
  full UPRP shields/energy/resources/hangar/special-state record.
- Locations are capped at 64 and runtime player groups at slots 0–7.
  Extended locations and neutral slots must be checked before claiming
  arbitrary CHK/SCX scenario compatibility.

These are **confirmed source limits**; this audit does not claim to have counted
every affected retail mission or verified the correctness of every handled ID.

## What the references can and cannot supply

Wargus provides useful concrete checklists in `scripts/{human,orc}/buttons.lua`,
`units.lua`, `scripts/spells.lua`, `scripts/ai/`, `campaigns/` and
`doc/pud-specs.txt`. Its README describes gameplay enhancements, so a Wargus
rule must still be distinguished from a verified retail Warcraft II rule.

Stargus is not a completeness oracle. Its `doc/todo` lists spells, campaigns,
larvae, creep, shields, siege, flying buildings, burrow, add-ons and other gaps.
Some of these already work here. Its `src/Chk.cpp::SaveTrigger` emits commented
Lua descriptions rather than executable campaign triggers, and its CHK reader
skips UPGR/PUPx/PTEC/PTEx. Therefore matching Stargus would still leave scenario
behavior missing.

Use `scripts/{terran,zerg,protoss}/units.lua` for command inventories,
race `sound.lua` files for sound inventories, and `doc/iscript.txt` and the
CHK reader for format leads. Cross-check these against native data and the
already pinned PyMS definitions. Do not import unfinished Stargus values:
its Terran Liftoff button is ordinary `move`; its Shuttle definition includes
`Supply = 9`, `DetectCloak = true`, and `MaxOnBoard = 6`. Those are not proof
of StarCraft behavior.

## Corrections to older status descriptions

- Warcraft II naval/air/ferry tactics and selected AI spells now exist;
  the October 7 table saying they are wholly missing is superseded.
- Warcraft II oil construction/loaded tanker presentation and hero work have
  later findings; do not reuse the first audit's entire gap list unchanged.
- StarCraft larvae, morphs, shields, siege, add-ons and several spells already
  exist despite Stargus's historical TODO.
- StarCraft's old “add-on research left out” statement is superseded by
  `p_prod.c` research makers. The stale `rules.c` comment saying Lair/Hive
  are not simulated is also false: current `p_prod.c` research prerequisites
  require them and `p_zerg.c` implements the tiers.
- The October 8 trigger player-ID/outcome description was corrected on
  October 9; current player is 13 and outcomes are local-player scoped.

## Recommended completion order

1. StarCraft player-facing essentials: core orders, selectable construction,
   real transport, missing base abilities, save/load and audio. Test actual
   effect and lifecycle, not only a visible command or catalog row.
2. Campaign correctness: Warcraft II mission objectives/restrictions/AI in
   mission order; StarCraft trigger/research initialization and script support
   with mission-specific completion/failure regressions.
3. Remaining base-game simulation/presentation fidelity: creep, animation
   scripts, projectile/terrain behavior, full panels and score/briefing flow.
4. Full campaign playthroughs, sustained multiplayer and deterministic replay
   checks, then separately scope Beyond the Dark Portal and Brood War.

## Reproduction and verification

Run headless throughout:

```sh
make -j4
env SDL_VIDEODRIVER=dummy make -j4 test-starcraft test-warcraft-2
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
make -j4 build/bin/tests/starcraft/test_ai build/bin/tests/warcraft-2/test_ai build/bin/tests/warcraft-2/test_ai_game
env SDL_VIDEODRIVER=dummy build/test_runner -j 2 -t 600 build/bin/tests/starcraft/test_ai build/bin/tests/warcraft-2/test_ai build/bin/tests/warcraft-2/test_ai_game
```

Build and both smoke checks passed. The quick targets ran 15 StarCraft and
41 Warcraft II programs. Initially 13/15 and 38/41 passed; the remaining five
were blocked by sandbox loopback sockets. Rerunning exactly those programs
with local sockets enabled passed 5/5, yielding **56/56 quick programs** across
the initial run and retry. These are test-program counts, not feature counts.

All **3/3 separate AI soak programs passed** under the repository's 600-second
budget: Warcraft II tactics (57.8 s), Warcraft II's ten simulated minutes on
ALAMO (5.5 s; 32 purchases, 4 waves, 18 harvest orders), and StarCraft's
three-race/doctrine/duel suite (154.7 s). An initial 60-second cap timed out
StarCraft after its Zerg case; this was a test-budget limit, not a passing result.
The completed soak supersedes it. No rendering code changed, and no new claim
of screenshot/pixel equivalence is made.
