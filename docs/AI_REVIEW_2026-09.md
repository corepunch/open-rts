# Shared AI review and KKnD economy notes (2026-09-30)

Companion to [AI.md](AI.md). Records what was established while reviewing
ef4af80 (universal AI, Dark Colony) and eb4af64 (port to Dark Reign, KKnD,
7th Legion) and while giving KKnD an income. Follow-ups are tracked in the
GitHub issue "Shared computer-player AI: structure follow-ups".

## AI vs AI+ (Dark Colony)

AI+ earns double credits; nothing else differs. Evidence in DC.EXE: launch
(`0x401210`) stores multiplier `0x200` for AI+ and `0x100` for AI at player
`+0xe20`; the credit routine (`0x412d8e`) scales every non-human owner's
credits by it (`amount * mult >> 8`). We reproduce it with
`level_t.income_scale[owner]`, written by `DC_ApplyAiIncome` from the lobby
slot type and applied on every harvest deposit by `P_ScaleIncome`.
`test_ai_skirmish` asserts AI+ income is exactly twice AI's.

Open caveat: a second loop at `0x401741` rewrites `+0xe20` from a setup array
whose writer was never traced. If that array holds 100 for every slot in
single-player war, retail AI+ would not get the bonus. Needs a dynamic check.

## KKnD oil loop

Retail: Mobile Derrick deploys on an oil patch into a Drill Rig; Oil Tankers
load at the rig and unload at the Power Station. Our level loader finds no oil
patches: the `CPLC` unit list carries none and `BOXD` / `TRPS` are undecoded.
So the rig is the deposit.

Engine (shared, no game ifdefs):

- `MF_RESOURCE_SOURCE` trait plus `mobjtype_t.deposit {amount, rate,
  resource_type}` marks a structure as a deposit.
- `P_SyncDepositStructures` (every `P_Ticker`) opens a 3x3 resource vent on
  each living source, bound by `resourcevent_t.source_id`, and closes it when
  the source dies. Vent slots are never removed because harvesters hold vent
  indices; closed slots are reused.
- `P_VentOpenTo(map, vent, unit)`: a vent with a source is open only to units
  allied with that source. Checked by the AI vent picker, `P_HarvestUnitsAt`
  and the mining loop.
- `P_MobjById(id)` for the lookups above.

KKnD:

- Drill Rig: `MF_RESOURCE_SOURCE`, deposit `KK_DRILLRIG_OIL` /
  `KK_DRILLRIG_RATE` (`kknd.h`; gameplay values, not retail).
- Power Station: `MF_RESOURCE_BASE` (the drop-off). Mobile Derrick is no
  longer a harvester. `tools/kknd_info_gen.c` emits the same flags.
- "Drill Rig" product (`products.inc` ids 55/56, cost 0, 3 s) made by the
  Mobile Derrick. When its time runs, KKnD's `G_ModelStartProductionRelease`
  turns the derrick into the rig in place (the `A_Deploy` pattern), so the rig
  sits wherever the derrick was driven.
- AI: a Drill Rig goal buys a Mobile Derrick when there is none to deploy;
  `kk_ai_owned` counts rigs alive or deploying plus derricks still in the shop
  queue, but not an idle derrick, so the goal then orders the deployment.
  Ladder: outpost, machine shop, power station, rig, tanker, then army.

Verification: `tests/kknd/test_ai.c` fast-forwards the ladder, then runs real
ticks with spending off and checks both computer players' oil grows while the
human's does not, that vents track rigs, and that a dead rig closes its vent.

## Review findings (shared AI)

1. `AiHarvestAssignment.slug_unit_index` indexes the per-tick unit array,
   which shifts on removal; the pruning pass can compare the wrong unit.
   Store the mobj id.
2. Legacy no-interface path duplicates waves and defense behind `ctx->game ?`
   forks. Make it a built-in default interface and keep one code path.
3. Default the catalog trio and `G_AiIsStructure` anchors when hooks are
   NULL, so catalog games supply only a ladder.
4. `plan` runs once per owner; goals cannot depend on state, AI and AI+ share
   one ladder. Add a replan trigger or per-level ladders.
5. `driver/d_main.c` and `rts_game_model_tick` each attach and tick the AI;
   share one simulation tick.
6. A per-slot skirmish setup (Human / AI / AI+ / None, feature toggles) shared
   by every game's `player_level` is cheap: `AiLevel`, `P_AiSetFeatures` and
   `income_scale` already exist.

## Environment notes

- Another session edits this checkout concurrently (menu / HUD / video
  refactor). Do not `git stash` or reset; to compare against HEAD use
  `git worktree add <scratch>/head HEAD` with a `data` symlink.
- `cmp` is broken on this machine ("Bad CPU type in executable"), so
  `make test-info-gen` fails; use `diff -q` on `build/info-check`.
- Pre-existing failures at HEAD from the renderer refactor:
  `tests/kknd/test_combat` (sprite frame check),
  `tests/kknd/test_combat_rendering`, `tests/kknd/test_runtime_sprites` and
  `tests/production_regression.h` (`SB_Init` signature).
