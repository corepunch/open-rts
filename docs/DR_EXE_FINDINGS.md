# Dark Reign executable and AI findings

## Info-table generation audit (2026-09-10)

**Confirmed from retail definitions and OpenDR revision
`98079a904746440433795fe7f21c4b35eb6b3959`.** The 47-entry engine catalog uses
the same primary body sprite names represented in OpenDR's unit and structure
sequence files. `tools/dr_info_gen.c` now owns that mapping and validates every
sprite name against retail `deftxt/UNITS.TXT` or `deftxt/BUILD.TXT` before
emitting the Doom-style `sprnames[]`, `states[]`, and `mobjinfo[]` tables.

The audit found two stale engine names. The Phase Runner is
`ufphrst0.spr` in the `FGundergtunnel` retail definition, not
`ucphrst0.spr`; the Base Mover is `ufbamst0.spr` in `FGBaseMover`, not
`ucbmvst0.spr`. Both generated state identifiers and runtime actor sprite names
now use the retail spellings. Reproduce the check with `make test-info-gen`.

**Known limit.** This generator preserves the current Freedom Guard vertical
slice. It does not claim that the 47 entries cover every retail faction,
decoy, civilian, attachment layer, shadow, or animation sequence. OpenDR's
sequence metadata remains the reference for expanding each one-state body into
native run, fire, idle, and facing states.

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

### Unit shadow ownership

**Confirmed from `data/REIGN/dark/deftxt/UNITS.TXT`.** `SetShadowImage` is part
of each unit-type definition, not scenario-object state. Freedom Guard infantry
share `ucmensh0.spr`; the Construction Rig uses `ucfcnsh0.spr`; the Spider Bike
and Flak Jack use `ufspbsh0.spr` and `ufflksh0.spr`; several vehicles use their
body sprite as the shadow image. The engine therefore stores these names in
`actortype_t.shadow_name` and resolves them through `mobj_t.info`. A per-object
shadow-name buffer would duplicate immutable type configuration.

Reproduce the mapping with:

```sh
perl -0777 -ne 'while (/DefineUnitType\s*\(([^)]+)\)(.*?)(?=DefineUnitType\s*\(|\z)/sg) { my ($type,$body)=($1,$2); my ($shadow)=$body=~/SetShadowImage\s*\(([^)]+)\)/; my ($image)=$body=~/SetImage\s*\(([^)]+)\)/; print "$type\t$image\t", ($shadow // ""), "\n" if $image; }' data/REIGN/dark/deftxt/UNITS.TXT
```

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

## SPR/FTG and map-loader cleanup (2026-09-09)

**Confirmed by loader comparison to `46f826a`, not new executable tracing.**
All 82 installed SCN map results and 1,392 SPR archive-record results match,
including 1,388 successful sprite pixel/metadata fingerprints and four unchanged
rejections. Default screenshots match byte for byte. See
[loader verification](LOADER_REFACTOR_VERIFICATION.md) for reproduction.

`data/REIGN/dark/graphics/SPRITES.FTG` SHA-256:
`46d831f7e02e9803d7d2d304e64488f2dca6e869d3e8f75cae4c2c19068aa320`.
Its borrowed directory remains 36-byte records: 28 name bytes followed by
little-endian offset and length at +28/+32. The lookup preserves the previous
27-character terminated-name behavior, without copying the directory.

SPR section records remain 16 bytes (first/last animation at +0/+4). The loader
reads only fields needed for the existing section/rotation traversal; it no
longer copies unused framerate/hotspot fields. Full authored canvases, current
alpha-derived bounds, centered ground points, the quarter-turn rotation
ordering, and shadow index 47 all remain unchanged. Their fidelity is not
newly inferred from the simpler allocation strategy.

Unchanged rejected records in the shared `SPRITES.FTG` are `(offset,length)`:
`(2125350,17765)`, `(2446177,3248)`, `(2449425,6202)`, `(2455627,6202)`.
The reason each is unsupported remains outside this cleanup's investigation.
Map setup now borrows one SCN buffer for terrain, decorations, resources, and
team settings; the final team pass may tokenize it in place. The separate
initial-unit pass still owns its own SCN buffer because it also mutates text.

## OpenDR sidebar reference (2026-09-13)

**Confirmed from OpenDR source and original menu SPR loading**, not from a new
retail executable investigation. The pinned checkout and upstream license are
recorded in `REFERENCES.md`. `chrome/ingame-player.yaml` and `chrome.yaml` define
238-pixel sidebar chrome, a 220×220 radar, 64×49 production slots, category
buttons, and the bottom order bar. Native menu SPR names come from the
`icon` sequences in `sequences/{structures,infantry,vehicles}.yaml`; they load
through the existing FTG/SPR decoder with BARREN.PAL, independently of world
sprite IDs. All 45 current Free Guard products have their own menu images.

`structures-building.yaml` preserves base prerequisites when an HQ, barracks,
vehicle factory or phase facility is upgraded. Our higher-tier actor types
now satisfy the corresponding lower-tier prerequisite. Ready, living,
owner-matching units are required, as in the Dark Colony dependency path.
The sidebar uses the shared producer queue and rejects missing technology,
insufficient money, busy producers and enemy-owned producers. The two stale
menu labels for Field Hospital and Rearming Deck were corrected against the
OpenDR sequences/rules; their existing simulation actor IDs are unchanged.

Copied chrome fingerprints (SHA-256):

- `sidebar.png`: `c40c69b2cda5332a160ab834fe6b3b9ce71b70da7ccb068318cd6a055ec52840`
- `glyphs.png`: `0fdab62379e80bba508731569d11b1072b915d6eac4222627c0cefebe08dff3f`

Verification: `env SDL_VIDEODRIVER=dummy make test-dark-reign`. `test_palette`
loads every icon and verifies category clicks, dependencies, upgraded-HQ
compatibility, owner isolation, insufficient money and production dispatch.
The default screenshot and an open production-palette screenshot are checked
with the software renderer. Move, attack, stop, category selection, tooltips,
radar panning and the options popup are functional. Unsupported stance,
attack-move, deploy, repair, power, sell and beacon controls are visibly disabled;
this is not a complete OpenDR gameplay or menu-system port. Text uses the
engine's bitmap font, and radar terrain rendering remains unimplemented.
