# Per-game playability status

Last updated: 2026-09-11 (after PR #29/30 fixes).

## KKnD

**Working:** map load, unit movement, autonomous combat, production (12 Survivor + 12
Mutant products), harvesting, AI production and attack waves.

**Actor coverage:** all 57 `UNITS.CFG` entries are in `ACTOR_TYPES[]` and `mobjinfo[]`.
Coverage gaps vs. production (classified):

| Gap | Class |
|-----|-------|
| Bombers, Wasps | Aircraft combat now works (PR #30); not yet in production queue |
| Missile Crab | Combat fixed (PR #30); not in production queue |
| Advanced infantry (SWAT, Sapper, Saboteur, etc.) | Missing production entries |
| Turrets beyond Guard Tower / Machinegun Nest | Missing production entries |
| Clanhall Wagon, Mobile Outpost | Intentionally non-buildable (deployed forms) |
| Campaign-only actors | Not production items by design |

Resource vents and oil tankers are working.

## Dark Reign

**Working:** map load (2NIC SCN), construction crew movement and combat (allegiance
fix PR #30), HQ production, AI production and attack waves, resource vents.

**Coverage gaps:**
- Freedom Guard only; Imperium faction absent.
- No Togran/expansion content.
- Construction/deployment mechanics not implemented.
- Armor/target restrictions, upgrades, faction-specific abilities absent.

## 7th Legion

**Working:** map load, movement, production (Troopers), harvesting, AI combat.
Seven actors verified against native data.

**Coverage gaps:** 25+ actor names/assets exist; only 7 are confirmed from native
data. Do NOT add actors with guessed balance values. Decode the executable tables
and `PVStart` mapping first; document addresses in `docs/7LEGION_EXE_FINDINGS.md`.

Evidence classification for any 7L actor claim:
- **confirmed** — native data or executable behavior
- **inferred** — names, asset grouping, mission patterns
- **placeholder** — authored for current gameplay
- **unknown** — requires executable tracing

## Dark Colony

Fully playable campaign. Production, combat, harvesting, scripted missions, drop
ships, and day/night sight all work. Used as the reference implementation for new
engine features.
