# Dark Reign executable and AI findings

Investigation date: 2026-09-01

## Fingerprint

**Confirmed.** The reference executable is `data/REIGN/dkreign.exe`. `rabin2 -I`
reports PE32/i386, 2,478,592 bytes, Windows GUI, with a 1997-09-02 timestamp.
It is not the Dark Colony executable and must not share native offsets or data
layout assumptions with `DC.EXE`.

## Native AI configuration model

**Confirmed from shipped data.** `data/REIGN/dark/aip/*.AIP` files are readable
C-like source files loaded by the game. `AIPDEF.H` defines the native concepts:

- strategy recomputation period in simulation cycles;
- priority weights for threats, distance, building defense, enemy-base attacks,
  exploration, perimeter defense, resources, and danger;
- minimum/maximum matching-force ratios and force budgets;
- construction accounts with `NUMBER_TO_HAVE`, `NUMBER_TO_BUILD`,
  `RATIO_TO_BUILD`, and `RATIO_TO_HAVE` modes;
- force-matching multipliers and unit matchup rules;
- optional building repair.

This is a stronger source for AI balance than attempting to infer strategy from
unit combat alone. The populated `g_dark_reign_ai_profiles` table mirrors the
shipped Freedom Guard profiles: `easy` (`FDEASY2`), `medium` (`FDMED1`),
`defensive` (`FGDPER`), and `aggressive` (`FGEVEN1`). The profile names are
engine-side labels; the native files select them through conditional FSMs.

## Conditional strategy switching

**Confirmed from `FGATTACK.FSM` and `FGDEFEND.FSM`.** The native conditional FSM
can switch AIP files based on elapsed time and relative army size. The attack
tree uses a 14,000-cycle timer and switches between medium profiles when the
team is below 100 units or above 150 units relative to the enemy. The defend
tree switches between perimeter defense and base defense around a 100/120-unit
relative-force threshold.

**Inferred.** This means a faithful generic AI should not use one static
personality forever. It should periodically recompute a strategic profile,
maintain defensive forces before committing an attack wave, and use force
matching rather than simply sending every available unit toward the nearest
target.

## Executable evidence

**Confirmed.** Embedded strings in `dkreign.exe` include `$AIP`, `load_aip`,
`DefineAICondTree`, `SetAIPFile`, `CritLessUnitsThanEnemy`, `AIP Parameters`,
`account->priority_level`, `Loading Force Matchings`, and
`Loading Building Matchings`. These strings establish that the executable
parses and stores the AIP concepts above. The AIP debug/parameter routine is
referenced by `0x00472480` for `threat_priority`; source diagnostics identify
the implementation family as `C:\WinTactics\Aip.c`.

**Unknown.** The exact score formula that combines the native priority fields,
the full construction-account scheduler, and the runtime meaning of every
FSM condition have not yet been isolated to stable instruction ranges. The
current engine should therefore consume the recovered profile values but not
claim byte-for-byte behavioral parity.

## Balance data recovered from native definitions

**Confirmed from `deftxt/UNITS.TXT`.** Dark Reign unit definitions contain
authoritative cost, build time, health, physics speed, hit size, seeing range,
weapon, and prerequisite fields. Examples for Freedom Guard are:

| Unit | Cost / build time | HP | Physics speed | Sight |
|---|---:|---:|---:|---:|
| Construction Rig | 300 / 9 | 100 | 6 | 9 |
| Freedom Fighter | 150 / 5 | 100 | 8 | 8 |
| Mercenary | 300 / 9 | 125 | 8 | 8 |
| Sniper | 700 / 21 | 100 | 12 | 12 |
| Medium Tank | 600 / 18 | 133 | 16 | 9 |
| Tank Hunter Tank | 700 / 21 | 150 | 20 | 9 |
| Triple Rail Hover Tank | 1300 / 39 | 200 | 12 | 9 |
| Sky Bike | 800 / 24 | 100 | 28 | 9 |
| Shock Wave | 4000 / 120 | 166 | 8 | 9 |

These values explain the existing Dark Reign actor stats and provide the
source of truth for future speed/cost corrections. `deftxt/BUILD.TXT` likewise
contains building costs, build times, prerequisite types, and makers.

## Implementation consequence

Dark Reign now has a native-shaped AI configuration table ready for the shared
AI controller. The next integration step is to route the generic controller's
strategic profile, force matching, and construction-account scheduler through
these values for both games, with Dark Colony retaining its separate
scenario-driven profile and production goals.

## Reproduction commands

```sh
rabin2 -I data/REIGN/dkreign.exe
rabin2 -zz data/REIGN/dkreign.exe | rg -i 'AIP|AICond|SetAIP|priority|matching'
rg -n 'recompute_strategy_period|priority|matching_force|_force|repair_buildings' \
  data/REIGN/dark/aip/*.AIP
rg -n 'SetCost|SetStrength|SetPhysics|SetSeeingRange|SetRequirements' \
  data/REIGN/dark/deftxt/UNITS.TXT data/REIGN/dark/deftxt/BUILD.TXT
```
