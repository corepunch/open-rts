# Universal computer-player AI

Engine code: `play/p_ai.{h,c}`. Game hooks: `G_AiInterface()` (`game/game.h`).
All four games are clients: Dark Colony (`games/dark-colony/p_ai.c`), Dark Reign,
KKnD and 7th Legion (each in its `p_prod.c`). The old per-game
`G_ModelAIProduction` goal tickers are gone.

## Architecture

The engine owns *when* and *how*; the game answers *who, what and with which
verbs* through an `AiGameInterface`:

| Hook | Purpose |
|---|---|
| `player_level(map, owner)` | `AI_LEVEL_NONE / NORMAL / PLUS`. `NONE` owners are skipped entirely (humans, empty slots, scripted missions). |
| `plan(map, owner, level, &AiPlan)` | Ordered goal ladder: keep `count` of `product` once `after_ms` has passed, plus wave interval/min/max size. Product ids are opaque to the engine. |
| `owned`, `can_purchase`, `purchase` | Count (alive + queued), classify (`OK / BLOCKED / NEED_CREDITS`) and buy. |
| `develop(map, owner, product)` | Optional. For a goal that reports `AI_BUY_NEED_TECH`, start the tech-up that unlocks it. Returning true makes the AI wait (like saving credits). |
| `is_base(unit)` | Resource drop-off that harvesters return to; default `MF_RESOURCE_BASE`. |
| `is_anchor(unit)` | Structures that anchor defense and are wave targets; default `is_base`. Dark Reign and KKnD use `G_AiIsStructure` because their drop-offs are a small subset of their buildings. |
| `features` | `AI_FEATURE_ECONOMY / PRODUCTION / DEFENSE / ATTACK / RESEARCH` mask; also `P_AiSetFeatures()` at runtime. |

### Shared adapters (`game/g_ai.c`, `P_Ai*` helpers)

A game whose purchases go through `G_FindProducer` / `G_QueueProduct` does not
write `owned / can_purchase / purchase`: it points them at `G_AiCatalogOwned`,
`G_AiCatalogCanPurchase` and `G_AiCatalogPurchase` (goal product ids are catalog
`ui_id`s). `P_AiLevelNonHuman` is the default `player_level` and `P_AiPlanAdd`
builds ladders. Dark Colony keeps its own hooks (custom purchase queue).

### Per-game configuration

| | features | ladder | notes |
|---|---|---|---|
| Dark Colony | all (no `develop` hook, so RESEARCH is inert) | race ladder, AI vs AI+ income | skirmish slots only |
| Dark Reign | economy, production, defense, attack | one table of `{Freedom Guard id, Imperium id, count}`; faction read from the owner's crews/HQs | Imperium previously had no AI at all; applies in campaign missions too |
| KKnD | all, including research | one table of `{Survivor id, Mutant id, count}` (ids do not pair by parity past row 43); faction read from the owner's units | research hook only fires for products with a tech level, none in `products.inc` today |
| 7th Legion | economy, production, defense, attack | single ladder off the Mobile Base | |

Scheduling mirrors DC.EXE: an owner thinks once per `AI_THINK_INTERVAL_TICKS`
(4) ticks, staggered per owner. Per think: census → economy (idle harvesters to
free vents) → production (ladder; a goal that is merely short of credits stops
the scan so cheap goals cannot starve it; at most two goals bought per think) →
defense (rally idle fighters on intruders within `AI_DEFENSE_RADIUS`) → attack
(wait for the plan's minimum idle army and the wave interval, then send up to
the cap at the nearest enemy base, falling back to any enemy object).

Inspection for tests and tools: `AiStats` per owner (`P_AiStats`), an event ring
(`P_AiPollEvent`: purchase, harvest assignment, defense rally, wave) and
`rts_game_model_ai(model)`. `RtsGameEvent` now carries `subject_owner` /
`target_owner`.

With no game attached (`P_AiInit` only) the pre-existing generic
`MF_RESOURCE_BASE` behavior is unchanged, so legacy callers and
`test_ai_team_aware` are unaffected.

## What was wrong before (Dark Colony skirmish)

- The interactive binary never ran the AI: with the custom sidebar only
  `G_UpdateProduction` ran, and `DC_UpdateAI` only ticks for scripted missions.
  A custom game against AI players did nothing for 10 simulated minutes (0
  purchases, 0 harvesters, frozen credits).
- In the headless model `G_ModelAIProduction` hard-coded owner 1.
- The AI and AI+ slot types were indistinguishable.
- Objects at x >= 128 on wide maps (D4PLAY02 is 160 cells) were cast through
  `int16_t` and wrapped off-map (city at x = -122), so any player starting there
  could not produce. Fixed in `spawn_object` (`w_map.c`); affects humans too.

## DC.EXE comparison

**Confirmed in the executable**

- Launch `0x401210`: lobby type AI=0 / AI+=1 / Human=2 / None=3 becomes player
  `+0x24` = 3 (AI and AI+), 0 (human), 4 (none). AI+ stores multiplier `0x200`
  at player `+0xe20`, AI stores `0x100`.
- Credit routine `0x412d8e`: for owners whose `+0x24` is non-zero, credits are
  `amount * [+0xe20] >> 8` before being added. So **AI+ earns double the credits
  of AI**; this is the only AI/AI+ difference found and it is what is
  implemented (`level_t.income_scale`, `P_ScaleIncome`, `DC_ApplyAiIncome`).
  Caveat: a second loop at `0x401741` rewrites `+0xe20` for all eight players
  from setup `+0x14c4 + 4*i` (x256/100); that array's writer was not traced (it
  is reached through a field-offset table near `0x485b54`). If it holds 100 for
  every slot in single-player war, retail AI+ would not get the bonus; this needs
  a dynamic check or finding the writer.
- Dispatcher `0x419a54`: runs only when `(tick & 3) == 0`; when the speed word
  is 4 every AI player runs, otherwise one player per call round-robin
  (`0x419960`). Adopted as the 4-tick cadence.
- `0x419960` indexes per-type tables at `0x474360` (4 types, `ai.c`
  `ai < MAX_AI` assert). Type 3 (skirmish AI/AI+) has one score/action pair,
  `0x447670` (constant 1) and `0x447854`, which drives four squad objects per
  think (vtable calls at `+0x3168/+0x316c/+0x3170/+0x3174`, stride 4860).

**Not ported**: the squad objects' decision code (build order, targeting,
retreat) is large and untouched. The ladder, waves and defense here are an
original policy with DC.EXE's structure, not a transcription. Native AI also
reasons from local search rings, not global knowledge; ours targets the nearest
enemy base regardless of fog. Research upgrades, aircraft and medics are not in
the ladder. AI and AI+ currently share one plan.

## Does a singleplayer custom game work?

Yes, on the retail two-player maps tested (29 maps: D2, J2 and D4 sets): the AI
buys a harvester first, tech-ups, raises an army and launches waves; against a passive human it reduced the
human to a handful of units in 20 simulated minutes (alien AI: eliminated). Campaign
missions are untouched (`player_level` is `NONE` outside skirmish).

Known weak points: no scouting or retreat, no expansion beyond the start city,
no aircraft, upgrades unused, economy capped at three harvesters.

## Tests

- `tests/dark-colony/test_ai_interface.c`: engine rules through a mock game
  (cadence, stagger, ladder priority, saving, blocked goals, timing, caps,
  toggles, replanning, event ring overflow, waves, defense).
- `tests/dark-colony/test_ai_skirmish.c`: fast-forwards real skirmishes and
  searches the event streams: both races build/attack, AI+ income is exactly
  twice AI's, feature toggles, defense, allied AIs never target each other,
  determinism (lockstep), campaign isolation, empty slots, the interactive
  production loop, and a 29-map sweep that also checks every start is on the map.

## Migrating a game

Implement `player_level` (or use `P_AiLevelNonHuman`), a `plan` that fills the
ladder, and either the catalog adapters or your own `owned / can_purchase /
purchase`. Return the interface from `G_AiInterface()`; `d_main.c` and
`rts_game_model_load` attach it and tick `P_AiTick` for every game. A plan that
returns false (for example because the owner has no unit to read a faction
from yet) is simply retried on the next think.

## Dark Reign, KKnD, 7th Legion status

Engine changes made while porting them, all shared by Dark Colony:

- A harvester tries free vents nearest-first and skips one it cannot path to
  (previously one unreachable vent idled it forever).
- Wave objective tiers: enemy anchor, else any enemy structure, else anything.
- A separate `is_anchor`, because widening `is_base` would have sent freighters
  to arbitrary buildings.

Findings and gaps:

- **KKnD has no economy yet.** The map loader creates no resource vents (the
  mission test reports `0 vents`), so oil tankers have nowhere to go. The AI
  spends the starting oil on its ladder; income waits on the loader.
- KKnD's first mission gives the enemy loose units and no buildings, so its AI
  has nothing to produce from and no structure to anchor waves on; it does not
  invent a base. `tests/kknd/test_ai.c` exercises the ladder with real bases for
  both factions.
- The previous KKnD goal list used ids that no longer matched `products.inc`
  (for example 25 and 126), so it built nothing; the stale test that asserted an
  empty product table is replaced by `test_ai.c`.
- On the default 7th Legion map the enemy's start cannot path to any vent (the
  player's can), so its Slaves never harvest there; the AI still buys and
  attacks from its starting credits.
- Dark Reign and KKnD ladders are an original policy, not a transcription of the
  retail AI; their executables were not disassembled.
- The per-faction ladders use fixed counts. No scouting, retreat, expansion or
  aircraft anywhere.

## Tests (added with the port)

- `tests/shared/test_ai_game.c`: ten simulated minutes on each game's default
  map (owner 0 untouched, enemy buys and launches waves).
- `tests/shared/test_ai_vents.c`: unreachable nearest vent falls through to the next.
- `tests/kknd/test_ai.c`: both factions, exact unit counts, loss replacement.
- `tests/production_regression.h`: Dark Reign and 7th Legion ladders through `P_AiTick`.
- `tests/dark-colony/test_ai_interface.c`: research wait and feature gating, anchors.
