# Dark Reign executable and AI findings

Topic navigation: [disassembly index](DR_DISASSEMBLY.md),
[unit comparison](DR_UNIT_BEHAVIORS.md),
[transporter animation](DR_TRANSPORTER_ANIMATION.md),
[HUD disassembly](DR_HUD_DISASSEMBLY.md),
[module boundaries](DR_ARCHITECTURE.md),
[state generation](DR_INFO_GEN.md), and
[development status](DR_DEVELOPMENT_STATUS.md).

## Unit movement speed (2026-10-02)

Reference: retail `data/REIGN/dkreign.exe` (SHA-256 above).

**Confirmed: SetPhysics storage.** The `SetPhysics(mass speed)` branch of the
unit-definition parser (`0x004464e7`) stores mass at unit type `+0x1ac` and
maximum speed at `+0x1ad`, both bytes.

**Confirmed: per-step speed.** `0x004c00c0` (Unitmove.c), called from
`0x004c2419` when a unit starts a tile step, computes
`maxspeed * terrain_percent * 200`; for move modes other than 0 and 4 it
multiplies by `(17 - |height delta|) * 0x3d70f0f1` (about 1/17). It divides by
20000 (`0x004c0186`) and clamps to 1..59. With 100% terrain on level ground
the result is exactly `maxspeed`. The caller stores it at unit `+0x1d1`.

**Confirmed: per-tic advance.** The move thinker (`0x004c1272`,
`0x004c131d`) moves current speed `+0x1d0` toward `+0x1d1` by at most 3 per
tic, then adds it to step progress `+0x1d2`. A step completes when progress
reaches 100 (`0x004c1267`, `0x004c130a`), keeping the remainder. Tile
positions are multiplied by 24 pixels. A unit therefore covers `maxspeed/100`
tile steps per simulation tic.

**Confirmed: tic rate.** `GameSpeed` (`UserPreferences`, read at
`0x004a3383`, default 20, range 0..60) goes through `0x004048b0` into
`0x006ccac0`. The tic period is `1000 / GameSpeed` ms (`0x00402669`,
`0x0040429c`). The retail default is 20 tics per second.

**Implementation.** Following the existing convention of one retail tic per
engine tic (weapon `firedelay*33ms`, 51-tic transfer animation), Dark Reign
actor speeds use `DR_SPEED(maxspeed) = maxspeed * RTS_TICRATE / 100` tiles
per second. At 30 tics/s this corresponds to retail GameSpeed 30, not the
default 20; relative unit timing is preserved. The Construction Rig is now
1.8 tiles/s (`SetPhysics(1 6)`), the Raider 2.4 and the Freighter 3.0. The
old authored speeds (rig 5.5, raider 5.0, freighter 4.5) were invented.

**Corrected defect.** `load_dark_reign_initial_units` set every
scenario-placed unit to 5.5 tiles/s. Because `P_ApplyActorTypeDefaults()`
keeps a non-zero speed, every unit placed by `PutUnitAt` moved at the same
speed regardless of type. That override is removed.

**Unknown.** Diagonal steps: `0x004c00c0` has no diagonal factor, so a
diagonal step may take as many tics as an orthogonal one. The engine moves
Euclidean distance. The acceleration limit of 3 per tic, the 1..59 clamp and
the slope factor are not ported. Scenario units are resolved by sprite name,
so a placed IMPSuicideZombie (speed 6) shares `ufmtrst0.spr` with the Martyr
(speed 16) and takes the Martyr's type.

Reproduce:

```sh
make dark-reign-units
r2 -q -c 'pd 12 @ 0x4464e7' -c 'pd 60 @ 0x4c00c0' -c 'pd 40 @ 0x4c125d' \
   -c 'pd 30 @ 0x4a3356' -c q data/REIGN/dkreign.exe
```
## Runtime capability and HUD audit (2026-09-30)

Reference: retail `data/REIGN/dkreign.exe`, SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`.
The existing ignored `reverse/dr-hud/dkreign.c` broad r2ghidra dump was
searched first; controlling parser routines were checked against r2
disassembly. This follows the same broad-to-narrow workflow as DC.EXE, without
copying Dark Colony object offsets or capability meanings into Dark Reign.

**Confirmed: movement and human metadata.** Unit-definition parser
`0x00445c90` stores movement at unit type `+0xf0`: Fly=0 at `0x004462d3`,
Hover=1 at `0x004462f6`, Fixed=2 at `0x00446319`, Ground=3 at `0x00446339`,
Tunnel=4 at `0x00446359`. `IsHuman()` sets type `+0x53c` bit `0x100`
(`0x00446935`). `NoAutoTarget()` sets that field's bit `0x40000`
(`0x00446f80`). These are native bits, not engine flag numeric identities.
The shipped definitions assign Fly to Sky Bike, Outrider, Recon Drone,
Cyclone and Sky Fortress. Runtime `P_ApplyActorTypeDefaults()` replaced the
generated flags with the authored actor flags, which lacked Fly for all five.
Generated flags alone were therefore insufficient evidence that flight worked.
Both layers now agree. Ground/hover resource transporters remain ground-bound.

**Address correction (2026-09-30).** The original report mistakenly attributed
the human bit to `0x00446b3c`. Fresh instruction windows establish the
`IsHuman` string comparison at `0x00446901`, the `0x100` OR at
`0x00446935`, and its store at `0x0044693a`. `0x00446b3c` instead belongs
to `CanSpy`: it ORs `0x10` and stores the three arguments at
`+0x560/+0x564/+0x568`. The human classification and implementation remain
correct; the cited instruction address was wrong. Reproduce with
`r2 -q -e bin.cache=true -c 'pd 4 @ 0x44692f' -c 'pd 8 @ 0x446b3c' -c q data/REIGN/dkreign.exe`.

**Confirmed: rigs are unarmed.** Neither faction's Construction Crew definition
has `AddWeapon`. The old range-nine, damage-20 actor defaults and generated
attack actions were invented offensive capabilities and are removed. The 2NIC
combat test previously depended on armed starting rigs; it now explicitly
spawns a Raider and refreshes the model snapshot before exercising combat.

**Confirmed: support data and target classifiers.** `WEAPON.TXT` gives
MedicHeal range 1, firing delay 10 cycles, offense H1 strength -20,
`CanOnlyShootHumans()` and `CanShootGroundUnit()`. MechanicRepair gives the
same range/delay, R1 strength -5, `CanOnlyShootNonHumans()` and
`CanShootGroundUnit()`. FGMedic and Karoch use MedicHeal; FGMechanic uses
MechanicRepair. Weapon parser `0x00483e70` stores the human-only classifier
at weapon `+0xa1` (`0x0048433b`) and nonhuman-only at `+0xa2`
(`0x004843d7`). These are weapon properties, distinct from building
`CanHeal`/`CanRepair` service flags.

**Implementation, with fidelity boundary.** Engine `MF_HUMAN`, `MF_HEAL`,
`MF_REPAIR` and `MF_NOAUTOTARGET` preserve those capabilities independently of
state presentation. Support uses ordinary thinker/state-entry dispatch and
negative authored attack strengths, restricts pulses to damaged allied ground
units of the appropriate class, and caps restored health at max HP. Ten
cycles become a minimum cooldown of ceil(10000/30)=334 milliseconds under the
existing engine timer contract. Support target acquisition and instantaneous
restoration are engine behavior inferred from the definitions, not a literal
port of the retail support projectile, armor-factor calculation, or automatic
support-order dispatcher. Existing generated animation timings can space
state-entry pulses farther apart. Their exact retail cadence, H1/R1 defense
factors and boosting cancellation are still unported. `NoAutoTarget` disables
fallback scanning while preserving an assigned valid target; the distinction
between retail explicit orders and retaliation has not been traced.

**Confirmed: HUD catalog omissions.** The production table already included
both factions, but the menu-image table contained only FG products and the
structure-list selector only recognized the FG rig. All production-table
entries now have exactly one native menu icon from `SetMenuImage` or the third
`SetBuildingImages` argument. The Imperium rig also selects structures. The
generic HUD's unrelated 64-icon ceiling is removed; icon ownership is already
dynamically allocated and freed by its definition count. The DR slot-index
arrays now use the actual icon count. Native per-mission technology lists
remain separate from the combined image catalog.

**Correction to the earlier catalog claim.** The executable/data audit test
matches 65 non-training `UNITS.TXT` definitions to runtime actors. The earlier
statement that all 71 non-T units have runtime entries was incorrect. Six
remain unmapped: FG attached weapons 4100/4101/4102, shielded SCARAB 1026,
Temporal Rift weapon 11103 and Camera Tower attachment 3006. Their definitions
are confirmed, but adding ordinary standalone actors would not establish the
retail attachment/alternate-state behavior. Do not hide these omissions with
aliases to similarly named building actors.

**Remaining capability work, confirmed from authored definitions.** RAT,
Invader, civilian convoy and Phase Runner carry units; Phase Runner boomerangs.
Raider, Mercenary and Phase Tank can phase. Sniper, Scout and Saboteur can
morph into overlays; both faction spies morph into units and spy with
`(300,1200,500)` timings. Saboteur has `CanSabotage(120,1,0)`. Amper has
`CanBoost()` with weapon booster `(200,200,0.5,1)`. Hostage Taker grabs into
IMPSuicideZombie after 300 cycles. SCARAB alternates into shielded type 1026;
Sky Fortress charges for 700 cycles and Temporal Rift for 5000. Their retail
dispatch, payload ownership, damage/projection and command/UI behavior are
not implemented by this capability correction. Martyr/Zombie suicide blast,
weapon target classes and native projectile behavior also remain incomplete.
COMMS/ORDERS/PATHS/SPECIAL pages, upgrade/decoy interactions, resource gauges,
radar terrain and native menu behavior remain unfinished. Passing the audit
is a metadata/capability check, not evidence that all units or HUD pages work.

**Verification.** `test_unit_traits` reads retail definitions in C and checks
Fly, human, harvesting and NoAutoTarget in both runtime and generated tables.
It exercises support amount, cooldown, cap, enemy/full-health/aircraft rejection,
ordinary thinker execution including Karoch, unarmed rigs, and assigned versus
automatic sniper targeting. `test_mission_hud` verifies every production entry
has one icon, loads the combined catalog, clicks the selected Imperium rig's
HQ slot and checks the 750-credit debit. Headless BMPs are written under
`/private/tmp`; 24-bit conversion for local visual inspection is not a game
asset or PNG loading dependency. Temporary unmapped-type diagnostics exposed
all six omissions and were removed after recording them here.
Temporary `OPEN_RTS_DEBUG_DR_TRAITS` logging at `P_ApplyActorTypeDefaults`
also recorded actual spawned type/traits against generated flags, including
Medic `0xc007`, Mechanic `0x14007`, Karoch `0xc007` and Sniper `0x2400f`.
It was removed before the final rebuild. Final verification passed `make`,
generated-table comparisons, `make tags`, 58 test executables across Dark
Reign/Dark Colony/7th Legion, both model-command tests and all four game
binaries' headless `--check`. The Dark Reign world/HUD and isolated Imperium
HUD screenshots were visually inspected.

Reproduce:

```sh
shasum -a 256 data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x445c90' -c 'pdf @ 0x445c90' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x483e70' -c 'pdf @ 0x483e70' -c q data/REIGN/dkreign.exe
make dark-reign-info
env SDL_VIDEODRIVER=dummy make test-dark-reign
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --check
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --screenshot /private/tmp/open-rts-dr-traits.bmp
```

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

**Known limit.** All 71 unique non-T unit types and 86 unique non-T building
types from retail `UNITS.TXT` and `BUILD.TXT` now have mobjinfo entries,
`ACTOR_*` enum values, and runtime actor stats. T-prefix training mirrors,
expansion/addon units, and full per-state animation are not yet covered.
OpenDR's sequence metadata remains the reference for expanding one-state
bodies into native run, fire, idle, and facing states.

Investigation date: 2026-09-01

## Fingerprint

**Confirmed.** The reference executable is `data/REIGN/dkreign.exe`. `rabin2 -I`
reports PE32/i386, 2,478,592 bytes, Windows GUI, with a 1997-09-02 timestamp.
It is not the Dark Colony executable and must not share native offsets or data
layout assumptions with `DC.EXE`.

**PE metadata rechecked September 30, 2026.** `rabin2 -H` confirms seven
sections, raw timestamp `0x340c98ca`, image base `0x00400000`, entry-point
RVA `0x000ccb20` (VA `0x004ccb20`), and linker fields 4.20. These are header
facts, not proof of an exact compiler version or universal calling convention.
The full fingerprint and commands are in
[the disassembly index](DR_DISASSEMBLY.md#executable-fingerprint).

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

These values provide the source of truth for speed/cost corrections. **Correction
(2026-10-02):** the earlier claim that they explained the existing actor stats
was wrong; actor speeds were invented until the movement-speed audit above.
`make dark-reign-units` prints every definition. `deftxt/BUILD.TXT` likewise
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

## Mission 01 and retail HUD correction (2026-09-13)

Reference: the two screenshots supplied by the user in this session (retail
Mission 01 and the open-rts three-rig start). Executable SHA-256:
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`.
This investigation uses the same PE32 executable described above. Its routines
use ECX/EDX as well as stack arguments; decompiler signatures alone are not
sufficient to identify draw coordinates. Addresses below were cross-checked
against disassembly and the shipped assets.

### Mission identity and objects

**Confirmed.** The wrong start was the Makefile convenience target's explicit
`scenario/MULTI/2NIC/2NIC.SCN`, not a replacement of M01F's file. 2NIC has
`SetTechLevel(100)`, three Construction Rigs per side and 12,000 player credits.
The binary's existing default is `scenario/FIXED/M01F/M01F.SCN`. `make dark-reign`
now uses that default too. M01F is the Freedom Guard first campaign mission,
SNOW, 60×60, `SetCredit(4000)`, `SetTechLevel(0)`, and
`SetStartLocation(225 1160)` (world pixels, divided by the native 24-pixel cell).
Its team-zero building records are:

| SCN object | Native type | Cell | BUILD.TXT SetType |
|---|---|---|---|
| 152 | fh1 | 12,44 | 10001 |
| 149 | fglp | 4,53 | 10019 |
| 146 | fgpp | 5,39 | 10020 |

These were previously decorations only. Known building types now become
ordinary mobjs, resolved through the native SetType and mobjinfo.doomednum.
Their original blocked footprints remain marked; the scenery copies are
removed. This gives the HQ ownership, sight and production. All three definitions
specify SetSeeingRange(8). The launch pad and generator use `bclncsh0.spr` and
`bcpowsh0.spr`; using their menu icons as shadows was wrong.

**Presentation preserved, not a new animation claim.** Completed building
images retain the existing three-layer choice: terrain-archive underlay, shared
archive body, shared archive top, native frame 1 where present. Their authored
canvas top-left stays at AddBuildingAt's coordinates. The loader combines these
into one engine indexed image with exact colors and a zero ground point, with
no native-format renderer callbacks. The regression compares every output pixel
against the original layers: **40,320 matching pixels** across the three buildings.
It deliberately does not claim to recover full native building animation.

**Still unverified.** The preexisting AssociatedUnit freighter-at-start behavior
is retained. BUILD.TXT confirms the association; the retail release timing and
placement have not been traced. M01F still loads twelve enemy mobile records,
the three player buildings and that freighter. Other unsupported factions'
buildings still use the prior scenery path.

### HUD assets, coordinates and draw order

**Confirmed.** The image descriptor table starts at `0x005be94c`: 29 records,
12 bytes each (16-bit width/height, ID, filename pointer). Loader `0x004957b0`
iterates those records, and `0x00495660` checks the loaded BMP size. Examples:
TOPBTNS 882×32, TOPBITS 154×32, MFDBTNS 576×64, MINIMAP 140×138,
RESOBARS 104×104, BUISOBOX 192×50. Asset names begin at `0x005beac4`.

The static HUD frame at `0x00494d70` places the minimap chrome at (448,342),
TOPBITS' six-pixel left edge at (0,0), and its seven-pixel right edge at
(441,0). Zone setup uses 49×32 top buttons at x=6,55,104 and x=294,343,392.
The money panel occupies x=153..293, using TOPBITS source x=6,width=141.
`0x004947b0`, BUILD case, clears (448,64,192,250) and draws BUBLDBIT at
(448,314). This is not the MFDBAC1 communications background.

**Confirmed.** `0x00468300` creates production lists with row height 50 and
`0x004b4990(64)` column width. List drawer `0x004b4fa0` invokes callbacks for
empty slots too. Unit callback `0x0048d900` and building callback `0x0048dd30`
place menu images at slot+(9,2), without fitting/stretching to the slot. They
then draw BUISOBOX's 64×50 frame over the icon, using source x=0,64,128 for
normal/hover/pressed. The 250-pixel viewport therefore contains five rows and
three columns. The former four 62×61 cells, black rectangles over slot chrome,
and SCROLL label over the second tab row are disproven substitutes.

MFDBTNS drawer `0x00490770` selects source x=`column*64 + state*192`, y=`row*32`.
Normal, hover and active use state 0,1,2; active BUILD uses source x=384.
COMMS/MENU are actual pages, not infantry/vehicle categories. The current MENU
opens the engine resume/quit popup. Unimplemented COMMS/ORDERS/PATHS/SPECIAL
pages consume their clicks without issuing unrelated stop/move/attack commands.
The BUILD list switches to structures when a rig is selected, otherwise units;
it follows native definition order and scenario tech limits. M01F's four unit
entries are rig, freighter, Freedom Fighter (engine label Raider), Spider Bike.
The rig is initially available through the HQ; the others lack producers.

**Confirmed.** `0x00490900`, money case at `0x00490f13..0x00490fae`, formats
`%0.9d`, replaces leading zeroes with ':' (up to eight), and draws font 2 at
panel+(25,6), hence (178,6). `0x0049594a..0x00495979` loads FONT16.PCX as font 2;
FONT12W/FONT12T supply the other fonts. Font loader `0x00469530` uses the first
row's first pixel as a delimiter, records starts at x=1, and stores the width
between delimiters for successive byte character codes. ':' is a dark digit
placeholder, not punctuation in this font. The PCX palette is not the game's
active GUI palette; using it produced white money digits. Remapping its indices
through the chrome palette restores cyan. `0x00477e00` initializes the
translation tables from `0x005cc9c0`: entries are 2,1,4,3,5,6,7,0,2,
and indices 32..41 receive `entry*8`. Normal HUD font text therefore maps
32..41 to 48..57; retaining the unmodified PCX indices incorrectly made
the footer labels purple. `0x00495a30` initializes the small SBTNS geometry
to width 71 and height 22, so its disabled state starts at source x=213. BMP/PCX decoding belongs to the
engine (`W_LoadImage`); game HUD code owns glyph interpretation and drawing.

**Confirmed.** Palette loader `0x0048b030` reads PALS version 0x102: six
256-byte channels followed by a 32,768-byte RGB555 lookup at file offset 1544.
`0x0048ad10` builds unavailable-red translation `0x006d6a00` using
`lookup[((R[i]+G[i]+B[i])/6)<<10]` from the first three native channels.
`0x0048d900` selects this translation when the unit is unavailable. Menu images
now use this table rather than a gray rectangle or an arbitrary RGB tint.

**Confirmed.** Minimap setup `0x0048fac0` takes w=min(map width,130),
h=min(map height,127), then centers the one-cell-per-pixel image at
x=453+(130-w)/2, y=348+(127-h)/2. M01F's rectangle is (488,381,60,60).
The prior 128×126 stretch covered the native minimap housing. The HUD now keeps
that housing and uses this native rectangle for markers and camera interaction.

## Imperium faction implementation (2026-09-19)

**Confirmed from retail `deftxt/UNITS.TXT`, `deftxt/BUILD.TXT`,
`deftxt/WEAPON.TXT` and OpenDR `sequences/units.yaml` (pinned
`98079a9`).** Imperium reuses six Freedom Guard bodies
(`ucfcnst0`, `ucfrgst0`, `uchfrst0`, `ucinfst0`, `ufmtrst0`,
`ucwcost0`) under its own SetType IDs (1001/1005/1006/1007/1099/1016)
and adds 15 unique mobiles plus 15 unique `ni*` buildings and three
shared common structures (camera, launch pad, generator).

Animation steps use the same `Start/Facings` formula as FG. Weapon
range/damage come from `SetAttributes` max and `SetOffense`
strength; cooldown is `firedelay*33ms` (one 30Hz cycle). Examples:
LaserRifle 4/11/8cy, PlasmaRifle 4/18/8cy, TachyonCannon 8/30/20cy,
IMPArtilleryShell 45/30/80cy, FortressCannon 7/650/20cy. Shredder has
`0 0 0 0` attributes, so it uses melee range 2 with a 500ms fallback.
Amper (AmperAmp, zero offense), Recon Drone and Hostage Taker have no
ranged weapon and stay support-only.

`tools/dr_info_gen.c` gains `MOBILE_ASSET`/`BUILDING_ASSET` for the
shared bodies: unique `SPR_*`/`S_*` IDs loading the same retail file,
so Imperium makers (1005 rig) and map things resolve. The Freighter
harvest suit carries over: group-5 `HARVEST` states from the same RSPR
sections, capacity 100, delivery to the launch pad.

Reproduce: `make dark-reign-info`, `make test-info-gen`,
`env SDL_VIDEODRIVER=dummy make test-dark-reign`.

**T-prefix types (training mirrors).** `UNITS.TXT` defines 41 T-prefix unit
types (SetType 40500–40644) and `BUILD.TXT` defines 30+ T-prefix building
types (SetType 40001–40052). These are alternate definitions for tutorial and
training missions — they share sprites with their non-T counterparts but have
their own SetType IDs, stats, and prerequisites. Binary `.SCN` map files do
**not** reference T-prefix type names; the names appear only in per-scenario
copies of the DEFTXT files (e.g. `scenario/FIXED/M10I/UNITS.TXT`). T-prefix
types are therefore not needed for map loading and would only be required to
implement the training mission mode where the game swaps in alternate unit stats
at runtime.

**Decoys and civilians now covered.** IMP decoy mobiles (SetType 1109–1117),
FG/IMP building decoys, IMP walls (11044–11047), IMP bridges (11040–11042),
civilian buildings (50005–50071), and Togran bridges (55500–55504) all have
`ACTOR_*` entries, mobjinfo rows, and runtime actor stats. Expansion units
(`units-addon.yaml`, `units-expansion.yaml`) remain unmapped.

## Freighter harvest and resource pits (2026-09-18)

**Confirmed from retail RSPR headers and OpenDR sequences/units.yaml.** The
Freedom Guard Freighter (`ucfrgst0.spr`) has 18 logical animations and 16
facings. Section 0 is the three-frame run cycle (anims 0..2). Section 1 is the
fifteen-frame harvest cycle (anims 3..17), matching OpenDR `harvest` /
`dock-loop` at Start=48, Facings=16, Length=15, Tick=100. The Hover Freighter
(`uchfrst0.spr`) stores a one-frame body plus harvest anims 1..15.

**Confirmed from BUILD.TXT.** `impmn` is the Common Taelon Mine
(`SetBuildingImages(ncmin1l0.spr ...)`, 3×3, passable). `impww` is the Water
Extractor (`SetResource(0 20 10000 10000)`). Both are pits the Freighter drives
into. Campaign M01F places four `impmn` and six `impww` stamps.

**Confirmed from OpenDR `sequences/misc.yaml`.** Resource sprites use
`ZOffset: -8196`, so the pit draws under the harvester standing on it.

**Implementation.** Harvest states `S_UCFRGST0_HARVEST1..15` / hover equivalent
use gameplay group 5, the same group Dark Colony uses for Exploiter DEPLOY/WORK.
Water wells are resource vents with the native 3×3 footprint. Passable
decorations sort at the north of their footprint so units in the pit are visible.
The Freighter parks on the attachment while mining, then hauls cargo to the
Water Launch Pad (`fglp` / `MT_FG_LIFE_PLANT`, OpenDR `DockHost`) or Taelon
Power Generator. The HQ is not a configured drop-off.

**Confirmed from `dkreign.exe` (SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`, PE32,
1997-09-02).** The `SetBuildingSrcAndDst` parser at `0x0044661d` accepts
source/destination building type pairs. M01F's `UNITS.TXT` configures
`(impmn,fgpp)` and `(impmn,tfgpp)` for Taelon, and `(impww,fglp)`,
`(impww,tfglp)`, `(impww,implp)` for water. `SetResourceTransport` at
`0x00446745` stores up to three per-resource transport records; M01F declares
resource 0 with max 750 and resource 1 with max 50. The runtime matcher at
`0x0046b580` contains the error “Unit and building unmatched for transporting
!” at `0x0046b5c2`, confirming that unit/building compatibility is a native
rule. The remaining two values in each transport record's tuple are not yet
interpreted.

**Implementation consequence.** A game-specific drop-off matcher now carries
the retail source/destination rule (`DR_HarvestDropoffMatches` in
`games/dark-reign/p_harvest.c`). For every transporter definition, it maps
resource 0 to Life Plants and resource 1 to Power Generators; the engine then
chooses the nearest allied compatible building. Other games can leave this
callback unset and use the generic `MF_RESOURCE_BASE` behavior. Removed the
prior three-cell distance exclusion: the executable evidence describes typed
source and destination matching, not a distance rule. `impmn` yields resource
1 while `impww` yields resource 0.

**Transporter and flight support.** All four mapped FG/Imperium ground and
hover transporter types carry `MF_HARVESTER`. Retail calls the hover units
`SetMoveMode(Hover)`, so they remain ground-path units; only native
`SetMoveMode(Fly)` types get `MF_FLY`. All five mapped flying types (FG Sky
Bike, FG Outrider, IMP Recon Saucer, IMP Cyclone, IMP Sky Fortress) have that
trait. `P_MoveUnitTo` and `P_TryMove` support direct flying movement that skips
ground walkability. This is 2-D movement support; altitude/elevation behavior
is not represented.

**Test coverage and limit.** `test_harvest_build` runs a real `TC_ORDER` through
the simulation without launching the app. Its water case covers approach,
pit attachment, all 15 harvest frames, full-cargo return, launch-pad unloading,
and repeated trips funding construction. Its Taelon case checks typed return
to the power generator and resource-1 unloading. The M01F Taelon pit at
`(3,38)` overlaps the power-plant footprint at `(5,39)`; that case begins at
the attachment point to isolate typed destination selection. Taelon approach
pathing through this overlap remains unverified.

**Reproduce.** `env SDL_VIDEODRIVER=dummy make test-dark-reign` includes
`test_harvesting` ( Freighter harvest animation and cargo on M01F ) and
`test_harvest_build`: credits are drained so a Construction Rig is unaffordable,
the Freighter is ordered onto a pit corner, attaches, plays at least two harvest
frames, cargo flows, three 100-credit deliveries fund the HQ, and the Rig trains.

### Freighter delivery-point investigation (2026-09-26)

Historical investigation; the implementation and resolved unknowns are recorded
in “Retail transporter docking correction” below.

**Confirmed engine defect.** `send_harvester_home` in `play/p_mobj.c`
requests the compatible building's position through `P_MoveUnitTo`. That
general movement function substitutes a nearby walkable position for blocked
goals. The substituted goal becomes `harvest.return_position`, and
`update_unit_harvest` credits the cargo as soon as `movement.order_arrived`
becomes true. There is no destination-building reference, authored bay,
delivery-facing check, or unloading state in this path. Reaching the movement
fallback therefore counts as delivery regardless of its relation to a bay.

Temporary env-gated diagnostics at return-goal assignment and crediting,
running `test_harvest_build` on M01F, recorded:

| Resource | Building position | Movement goal and actual unloading position |
| --- | --- | --- |
| Water | Launch Pad `(4,53)` | `(3.5,52.5)` |
| Taelon | Power Generator `(5,39)` | `(4.5,38.5)` |

All three existing test cases pass, including three water deliveries funding
a Construction Rig. **Disproven:** these passing tests establish correct
docking. They establish arrival at the substituted movement goal and a
destination within six cells of the building, not arrival at its native bay.
Diagnostics were removed after the investigation.

**Confirmed OpenDR configuration**, revision
`98079a904746440433795fe7f21c4b35eb6b3959` in `reference/OpenDR`:
`mods/dr/rules/structures.yaml` gives `WaterLaunchPad` a `DockHost` with
`Type: Unload`, `DockOffset: -1c0,1c0,0`, and `DockAngle: 0` (with a commented
`32` alternative). `vehicles.yaml` gives both Freighters `Harvester`,
`StoresResources`, `DockClientManager`, and `WithDockingAnimation`. Its
`Power` entry has the old refinery configuration commented out and no active
`DockHost`. `DrRefinery.cs` calls `AddWater(value)` for accepted resources in
both storage branches. Thus this checkout is evidence for an explicit dock
model, not a verified complete retail water/Taelon implementation.
`mod.config` pins OpenRA to `playtest-20260222`; its engine checkout is absent
locally, so the shared C# docking activity was not inspected here.

**Confirmed native asset and parser evidence.** Retail
`dark/deftxt/BUILD.TXT` describes `SetBay` as the bay location within the
building type and declares `SetBay(3 2)` for `fglp`, `SetBay(1 3)` for `fgpp`.
The current Dark Reign loader does not consume `SetBay`. In
`data/REIGN/dkreign.exe`, SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`
(the PE32/i386 executable fingerprinted above), the parser compares the
`SetBay` string at `0x005c42dc` from `0x004a04e5`, reads two integers, and
stores them in the building-type record at `+0x22c`:

```
bits 0..3 = first argument & 15
bits 4..7 = second argument & 15
```

Instructions `0x004a056c..0x004a059c` confirm the masks and second-argument
shift; higher bits are preserved. Broad discovery used the existing
`reverse/dr-hud/dkreign.c`, then these stores were checked in disassembly.
**Unknown:** the runtime transformation from these local bay coordinates to
the transporter's destination, the required delivery facing, and the
unloading handoff/timing. No OpenDR offset has been substituted for those
unknown retail rules.

**Implementation consequence.** Keep return travel, arrival validation, and
cargo transfer in the shared simulation, with game-owned building/resource
compatibility and native docking data. The existing shared distinction is
`harvest.capacity > 0` for cargo trips versus zero for automatic credits;
Dark Colony's Exploiter and Slug use zero. A return trip and a final facing
requirement are separate behaviors. Trace readers of the bay field and the
retail transport state machine before implementing exact Dark Reign docking.
This investigation changes documentation only; the delivery defect remains.

Reproduce the current behavior and parser evidence:

```sh
make build/bin/tests/dark-reign/test_harvest_build
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_harvest_build
rg -n 'SetBay' data/REIGN/dark/deftxt/BUILD.TXT
r2 -q -e bin.cache=true -c 'pd 85 @ 0x004a04dc' -c q data/REIGN/dkreign.exe
```

For the position trace, temporarily print `base->core.position` and
`unit->movement.goal` just after `harvest.return_position` is assigned, and
`unit->core.position` immediately before cargo is credited.

### Remaining fidelity limits

The radar terrain-color path is identified but not ported: `0x0048fac0` calls
`0x0047ad10`, which combines tile information from `0x004190c0`/`0x00413d20`
with brightness lookup offsets at `0x005b9410` into the palette's lighting
lookup. Current radar contents are object markers and the camera outline.
No guessed terrain color or sample pixel was substituted.

Resource gauges still lack the retail economy inputs. `0x0048f340` smooths team
supply/demand and maximum per-building water stock toward displayed values by
signed delta/16 with a minimum one-unit step. It selects a dynamic power scale
in five-step increments, fills x=596,width=17 with height `supply*81/scale`
bottom-aligned at y=473, and draws a demand tick. Colors depend on supply versus
demand (native indices 0x16,0x18,0x8a). The controlling scale and full economy
inputs are not yet implemented; the frame remains visible without a fabricated
fill. Upgrade/Decoy labels and controls are presentation only. Full campaign
FSMs, communications, order/path/special pages, and native popup behavior remain
outside this correction. Footer text uses the native font; its full native
state-dependent color translations have not been ported.

### Reproduction

```sh
shasum -a 256 data/REIGN/dkreign.exe
rabin2 -z data/REIGN/dkreign.exe | rg -i 'buisobox|mfdbtns|font16|topbits'
r2 -q -e bin.cache=true -c 'af @ 0x48d900' -c 'pdg @ 0x48d900' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'pd 75 @ 0x490ec0' -c q data/REIGN/dkreign.exe
env SDL_VIDEODRIVER=dummy make test-dark-reign
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --screenshot /private/tmp/open-rts-mission01.bmp
```

This supersedes the earlier OpenDR sidebar implementation description above:
no OpenDR PNG chrome is loaded or bundled. The shared generic palette has moved
into `games/dark-reign/hud/`, with retail asset selection and draw procedures.

## Resource-extractor side and FIRE-state coverage (2026-09-20)

**Confirmed from retail `data/REIGN/dark/deftxt/BUILD.TXT`.** `SetSide(0)` is
Freedom Guard (`fh1` FG Headquarters 1), `SetSide(1)` is Imperium (`ih1`
Imperium Headquarters 1), and `SetSide(2)` is civilian/neutral (`ce` Civilian
Entertainment Facility). Both extractor buildings are side 2 with authored
hitpoints and harvest parameters: `impww` (Water Extractor, type 14005,
`SetResource(0 20 10000 10000)`, 500 HP) and `impmn` (Taelon Extractor, type
14006, `SetResource(1 1 500 40)`, 600 HP).

**Implementation consequence.** Pre-placed extractors are neutral map features
backed by the ownerless vent list, never garrisoned mobjs. Spawning them as
team-owned buildings (as the full-coverage batch did) both marks the 3x3 pit
footprint blocked, so harvesters circle the pit instead of attaching, and hands
nearby attack-capable units a hostile target that cancels their move orders.
`games/dark-reign/w_map.c` keeps extractor coverage tables but loads `impmn` /
`impww` as passable decorations and skips their mobj spawn.

**Corrected generator regression (inferred from test evidence, restored to the
pre-refactor emission).** The per-sprite `.inc` refactor dropped the `S_*_FIRE`
row that the monolithic writer emitted for attacking actors without a native
shoot cycle (`{ SPR, stand_start, 1, A_Attack, S_STND, 3 }`). The dangling enum
left a zero-filled state, so the first `A_Look`/`A_Chase` acquisition removed
the unit via the `S_NULL` path in `P_SetMobjState`. Bisected to `c2b9338`;
`tools/dr_info_gen.c` `write_inc` now emits the FIRE row again. Reproduce with
`make test-dark-reign` (`test_playable` move/combat, shared `test_retaliation`).

Investigation date: 2026-09-20

## Retail transporter docking correction (2026-09-26)

This implementation supersedes the unresolved runtime questions and the
documentation-only outcome in “Freighter delivery-point investigation” above.
The user's September 26 open-rts screenshot shows the Freighter stopped
northwest of the Launch Pad; it is evidence of the engine defect, not a retail
reference image. The executable fingerprint remains SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`
(`data/REIGN/dkreign.exe`, PE32/i386, September 2, 1997).

**Confirmed: destination and arrival.** `0x0049bbb0` builds transport routes
from source and destination building origins (`+0x70`, `+0x74`) plus the signed
four-bit coordinates in building type `+0x22c`. It inserts waypoints through
`0x004754d0` and starts order 6 through `0x004b7b10`. `0x00422550` resolves a
building by its exact bay cell. Order 6 in `0x004baac0` uses that lookup and
calls the resource-transfer routine `0x0049c010` on arrival. Proximity to an
arbitrary edge of a building is not the arrival condition.

Both factions' `BUILD.TXT` Launch Pads (`fglp`, `implp`) have `SetBay(3 2)`;
Power Generators (`fgpp`, `imppp`) have `SetBay(1 3)`. Both extractor types
(`impww`, `impmn`) have `SetBay(1 1)`. Native unit placement uses `cell * 24`
(`0x00448860`, `0x00445180`); native building region bounds at `0x004968d0` and
`0x00496a10` begin at `cell * 24 - 12`. The same half-cell base appears in
attached-part placement at `0x00510390`. Our building canvas starts at the stored building
anchor and our unit positions use cell centers. Therefore the corresponding
engine destination is `building anchor + cell_center(SetBay)`. In M01F this is
`(7.5,55.5)` for water and `(6.5,42.5)` for Taelon. The half-cell follows the
coordinate representation; it is not a tuned sprite offset.

**Confirmed: the bay must be walkable.** `deftxt/OVLEFF.TXT` contains authored
footprint dimensions followed by row-major `(effect, altitude)` pairs. Effect
`-1` leaves the underlying terrain unchanged, `2` is a walkable building bay,
and `3` is solid. Parser `0x0042ca50` reads the pairs and encodes `-1` as
`0x1f`; `0x00481090` owns the effect definitions. `SetBuildingImages` goes
through `0x0049eda0` and sprite-name lookup `0x00481120`. The relevant masks
are:

```
nclnc1l0.spr (5 x 4)      ncpow1l0.spr (4 x 5)
-1 -1 -1 -1 -1           -1 -1 -1 -1
-1  2  3  3  2           -1  2  2 -1
 3  3  3  2  2            2  3  3  3
 2  3  3  2 -1            2  2  3  2
                          2  2  2  2
```

Both extractor masks (`ncwel1l0.spr`, `ncmin1l0.spr`) are 3 x 3, all effect 2.
**Disproven:** the old guessed solid rectangles (Launch Pad 4 x 3, Power
Generator 3 x 4) describe native collision. They blocked the authored docking
cell. Merely adding `SetBay` while retaining those rectangles would still send
the transporter to the movement system's fallback location. The loader now
uses native masks for every resolved building, preserving walkable bays for
all ground units. Altitude pairs are consumed but their separate height effects
remain outside this collision correction.

**Confirmed: facing, animation, and transfer batches.** `0x0049c010` uses
unit-type `+0x608/+0x60c` for load facing/section and `+0x610/+0x614` for unload
facing/section. It compares the part's facing (`+0x80 >> 16`), turns through
`0x004a8480`, starts the section through `0x004a8920` with multiplier `0x10000`
and one-shot mode 2, and waits for `0x004a8aa0` before transferring resources.
`UNITS.TXT` gives all four mapped ground/hover transporters, across both
factions, `SetTransportLoadAnimation(135 1)` and
`SetTransportUnLoadAnimation(135 1)`. These are 135 degrees in the native
east-zero, counterclockwise frame system, corresponding to engine
`ANG90 + ANG45`, disk rotation slot 6. Degree conversion is visible at
`0x004467e8..0x00446807`, using float `0x47360b61` at `0x005901f4` (approximately
`2^24 / 360`). The native direction calculation uses `atan2(-dy, dx)`.

Transporter part `SetRotationRate(10)` is converted with that same factor at
`0x00447ab4` and stored at part-type `+0x54` at `0x00447ac4`.
`0x004a8e00` advances along the shorter direction and clamps to the target.
The four transporter definitions now use the native truncated 24-bit step
`floor(2^24 * 10 / 360)`, shifted eight bits into the engine's 32-bit angle,
per simulation tic. Other unit turn rates have not been inferred from this.

The transfer records have stride 12: capacity `+0x500`, load batch `+0x504`,
unload batch `+0x508`, indexed by resource. All four mapped transporter types
declare water `(750,270,270)` and Taelon `(50,10,25)`. Each completed animation
transfers a batch limited by cargo capacity, available resource, and receiver
storage. The last water batch of a full load is therefore 210; Taelon takes two
25-unit unload cycles. The former universal capacity 100 and continuous looping
harvest animation were not native transporter behavior.

**Confirmed: native section timing.** In `ucfrgst0.spr`, section 0 is frames
0..2 at rate 65536 and section 1 is frames 3..17 at rate 19660, with 16
rotations. In `uchfrst0.spr`, section 0 is frame 0 at rate 65536 and section 1
is frames 1..15 at rate 19660, also with 16 rotations. Both sections report
six hotspots. Section startup `0x004a8920` multiplies the shifted fixed-point
operands: `(65536 >> 8) * (19660 >> 8) = 19456`. `0x004a96f0` advances the
cursor and completes at `(last_frame + 1) << 16`. Thus one 15-frame pass takes
51 simulation tics. Frame durations are consecutive differences of
`ceil(n * 65536 / 19456)`, starting at n=0. Generated one-shot state chains
encode those durations and terminate at standing; the shared thinker starts
another pass only when another batch is required. Retail game speed is
configurable from 1 to 60 (`0x004048b0`, global `0x006ccac0`), so 51 tics is
not a claim of identical wall-clock duration at every retail speed setting.

**Implementation and verification.** The shared thinker owns travel, exact
arrival, turning, one-shot unloading, and credit transfer. Game definitions
supply resource capacities/batches, states, facing, and compatibility; the
level-owned Dark Reign mission stores authored bays. Cargo type and a stable
destination-mobj reference persist during the trip. Removal clears references
through the thinker lifecycle. Destroyed, incompatible, or blocked destinations
cannot receive cargo. New harvest orders preserve retained cargo. Dark Colony
retains automatic credits with zero cargo capacity; KKnD and 7th Legion retain
their previous capacities and delivery behavior.

Docking units cannot be displaced by ordinary unit separation. A transporter
approaching a shared bay yields to one leaving it, using an unoccupied walkable
neighbor before resuming its exact destination. **Engine behavior, not a
verified retail algorithm:** this traffic procedure prevents opposing flow
goals from pushing indefinitely against each other; the retail local avoidance
algorithm remains unported. No distance-based fallback counts as unloading.

`test_harvest_build` now drives all four transporter types through two complete
water trips and two complete Taelon trips each. It checks the exact bay and
135-degree pose, all 15 frames and 51 tics before every credit, batch sizes,
resource conservation, return to the source, both factions' native bay data,
the Launch Pad's full collision mask, destroyed/replaced/blocked destinations,
shared-bay traffic, and stop/resume without cargo loss. Temporary diagnostics
recorded every credited amount, position, facing, and the opposing-traffic
stall; they are removed from the final implementation. A headless rendered
M01F run, without removing other units, reached `(7.5,55.5)` with full cargo
750 and unloading frame 10 (25 tics into the first pass). Visual inspection
confirmed the unloading transporter on the Launch Pad's bay, instead of its
previous northwest stopping point.

**Remaining fidelity boundary:** exact native docking/transfer sequencing does
not establish a complete retail economy or pathfinder. Extractor regeneration
and stock, per-building receiving storage, water launch/economy timing, and
retail local traffic arbitration are not fully implemented. Our existing
resource balance receives delivered batches directly. The old six-cell
proximity test is superseded by exact-coordinate and animation checks.

Verification completed with `make`, `make tags`, generated-table comparison,
53 test executables across `test-dark-reign`, `test-dark-colony`, and
`test-7legion`, both model-command tests, and all four game binaries' headless
`--check`. KKnD's `test_combat` death-frame assertion, `test_playable` assertion
that production is unimplemented, and `test_production` zero-product assertion
also fail in a clean archive of pre-change commit `efbac35`. The network suite
passes command/session setup tests but fails its four-peer initial-state
handshake both here and on that same clean baseline. These are recorded as
existing failures, not reported as passing regressions. The local `/usr/local/bin/cmp`
has an incompatible CPU architecture; generated comparisons pass using
`/usr/bin/cmp` by putting `/usr/bin` first on PATH.

Reproduce:

```sh
rg -n 'SetBay|SetBuildingImages' data/REIGN/dark/deftxt/BUILD.TXT
rg -n 'SetTransport|SetRotationRate' data/REIGN/dark/deftxt/UNITS.TXT
rg -n 'nclnc1l0|ncpow1l0|ncwel1l0|ncmin1l0' data/REIGN/dark/deftxt/OVLEFF.TXT
r2 -q -e bin.cache=true -c 'af @ 0x49bbb0' -c 'pdf @ 0x49bbb0' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x49c010' -c 'pdf @ 0x49c010' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x4a96f0' -c 'pdf @ 0x4a96f0' -c q data/REIGN/dkreign.exe
make build/bin/tests/dark-reign/test_harvest_build
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_harvest_build
```

## PATHS controls and shared engine routes (2026-09-30)

Executable: `data/REIGN/dkreign.exe`, SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`,
PE32 i386, 2,478,592 bytes, image base `0x00400000`. The broad r2ghidra dump
was used to locate controls, then focused native instruction/data reads
verified their coordinates, node layout, selector values and traversal.
The complete control table and implementation limits are retained in
[DR_HUD_DISASSEMBLY.md](DR_HUD_DISASSEMBLY.md#paths-controls-and-traversal).

**Confirmed:** `0x00467d60` constructs PATHS; callbacks `0x00460c40`,
`0x00460c70`, `0x00460db0`, `0x00460df0`, `0x004616a0`, `0x004610d0`
implement Add/Clear/Delete/Go/De-Select/Save. Native rectangles derive from
constructor arguments, not visual estimates. SBTNS type 0 is 71x22/source 0,
type 1 is 103x22/source 284 (`0x00495a30`); label offsets are (7,5)/(9,5).
BASADV draws at (448,64), with source 0/192 (`0x00494700`). Header positions
are Basic (461,75)/(462,78), Advanced (560,77)/(561,78). The native header
uses text translation `0x006d6600` (table entry 7, indices 32..41 shifted by
56); the engine loads this native FONT12T variant separately. Caption anchors are (544,205), (468,258), (555,257),
using FONT12W, subtracting glyph height and 2. Saved rows are 12 pixels high
(`0x00468300`), text begins at left+2 (`0x0048d8c0`). Native name field at
(468,258,80,17) has two-pixel internal padding; editable names remain unknown
in the engine implementation.

**Confirmed:** `0x005beab0` stores selector x/width pairs (0,24), (24,23),
(47,24); `0x00490900` advances state source by 71/142. `0x00461070` maps
buttons through values 2,0,1. Default is 2. `0x00475c10` proves 0 backtrack,
1 loop and 2 one pass: the first reverses on linked-list endpoints, the second
wraps to head, the third terminates at tail. A singleton backtrack terminates.
`0x004755b0` allocates six dwords: kind, x, y, auxiliary, next, previous at
+0/+4/+8/+12/+16/+20. `0x004754d0` appends; `0x00475d00` checks existing
points. These are dynamic lists; **unknown** native point-count limit.

**Confirmed:** Add enables input mode 6 without issuing movement. Go sends
one complete free route through `0x0046d1d0` (event 10); saved Go dispatches
through `0x0046cc30` (event 13), whose index check permits 0..29. Retail
HELP.TXT supplies B/O/P/C page shortcuts and M/A/S orders. Local PathT3 text
explains plotting, patrols and saved paths. These native text assets support
intent, while controlling executable branches establish the traversal rule.

**Confirmed:** radar route drawer `0x0048f990` walks next pointers at +16,
connects kind-0 cells with palette index 22 (24 selected), and centers a 3x3
index-138 marker. Kind 1 uses index 22 and does not replace the previous
kind-0 connection origin. **Unknown/unported:** target/building node semantics
and the world-overlay dispatch chain.

**Disproven/corrected:** an early case-number reading assigned case 4 to
ORDERS and case 5 to PATHS. Page drawer `0x004947b0` actually assigns case 2
ORDERS, 3 PATHS, 4 COMMS and 5 MENU. `0x00467b60` is ORDERS, not PATHS.
MFDBAC1 is shared PATHS/COMMS chrome, not a BUILD-grid background.

**Implementation consequence:** common `waypoints_t` lives in `mobj_t`,
advanced by `P_TickWaypoints` in the ordinary thinker. DC's private fields and
AI route loop were removed; DC mission patrols explicitly retain loop mode.
Common HUD editing/save/load lives in `hud/hu_bar.c`; DR owns native drawing.
TC_PATH snapshots selected stable IDs and all cells in one delayed command;
protocol 3 encodes the complete route and checks spans, shape and bounds.
An eight-point capacity is retained from DC as an explicit **engine adaptation**,
not native DR evidence. Thirty saved routes are local to the current HUD;
editable names and persistence, exact disabled/selected text palettes and
world overlays remain unported.

Support commands now use the existing shared target eligibility rule, allowing
Medics/Mechanics to receive explicit friendly orders. Invalid targets preserve
prior orders. Investigation logging confirmed old input/network fixtures had
allegiance PLAYER on both their attacker and purported enemy (allied=1); the
fixtures now define an actual enemy. No friendly-fire workaround was added.
Temporary diagnostic logging was removed after confirming the cause.

Reproduce the evidence and focused checks:

```sh
r2 -q -e scr.color=0 -e bin.cache=true -c 'af @ 0x467d60' -c 'pdf @ 0x467d60' -c q data/REIGN/dkreign.exe
r2 -q -e scr.color=0 -e bin.cache=true -c 'af @ 0x475c10' -c 'pdf @ 0x475c10' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'px 6 @ 0x5beab0' -c q data/REIGN/dkreign.exe
r2 -q -e bin.cache=true -c 'af @ 0x48f990' -c 'pdf @ 0x48f990' -c q data/REIGN/dkreign.exe
rg -n 'waypoint|patrol|save|path' data/REIGN/dark/local/HELP.TXT
make build/bin/tests/dark-reign/test_mission_hud build/bin/tests/dark-reign/test_waypoints
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_mission_hud
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_waypoints
```

`test_mission_hud` writes `/private/tmp/open-rts-paths-hud.bmp`; the isolated
native HUD image was inspected. Shared tests verify all traversal modes,
stop/move cancellation, ownership, stable-ID snapshots, invalid-route rejection,
route checksums and independent saved/draft storage. Network regression peers
submit real two-point backtrack routes in direct and hosted lossy sessions.

A final flying-route regression exposed stale movement IDs: after an arrival,
clearing the ground flow field left the ID that `P_HasMoveOrder` uses for air
movement. Logging showed the flying actor still at (5.5,5.5) while a three-leg
one-pass route had already ended. Route advancement now clears that ID before
issuing the next leg. The shared test crosses a fully blocked terrain row with
MF_FLY and reaches the final cell in all four game builds; temporary logging
was removed. This correction concerns engine route execution, not a newly
recovered retail timing or offset.

## One screen palette (2026-10-01)

The indexed framebuffer shows one 256-colour palette per frame, so Dark Reign
sprites can no longer be drawn through a different palette from the terrain.

**Asset fact (measured, not disassembled):** `BARREN.PAL`, `SNOW.PAL` and
`JUNGLE.PAL` share entries 1–159 except the six entries 16–21; their values
reach 63, i.e. 6-bit VGA. Every 8-bit interface sheet in
`graphics/INTFACE/IGI/*.BMP` carries exactly those entries scaled by four in
its own palette (159 of 159 match `SNOW.PAL`; 153 match the other two, the
difference being 16–21) and uses no index above 159. Entries 160–254 differ
per tileset. Scenery sprites (`aoroc*`, `aotre*`, `aoclf*`, …) are mostly
indices 160–254; unit and building sprites use only 1–159.

**Consequence:** the retail screen is the tileset's PAL, and sprite, scenery
and interface pixels are indices into it. `games/dark-reign/w_spr.c` now
derives the sprite palette from the map's terrain palette (`dark_sprite_palette`)
instead of loading `BARREN.PAL` at six times its stored value. Two visible
changes follow from that, and both are corrections of the old truecolour path
rather than new decisions about the look:

- Scenery on SNOW and JUNGLE maps takes that tileset's colours. It was drawn
  with BARREN's 160–254 entries before, which is why snow rocks looked pink.
- Units and buildings use the standard range at four times the stored value,
  like the interface art, instead of six. They are darker than before.

The terrain multiplier for entries 160–254 (six on BARREN and JUNGLE, four on
SNOW) is unchanged and is still an engine choice, not a recovered value. The
purple-to-orange team band swap (32–39 from 48–55) is unchanged.

## Native shell (2026-10-02)

Sources: `reverse/dr-hud/dkreign.c` (decompile), an `objdump -d` of
`data/REIGN/dkreign.exe` for register arguments and jump tables the decompile
drops, the decoded `shell/SHELL.RLI`/`SHELL.RLD`, `shell/SHELLCFG.H` and
`local/MLSTRING.CFG`. **[V]** verified in code or data, **[I]** inferred.

### Archive and images [V]
- `0x57e0f0` maps `shell\shell.rli` and, with the last letter changed to `d`,
  `shell.rld`. RLI: `"ILR."`, u32 file size (unused), u32 count, then 32-byte
  entries: name[12], tag `"TLF."`, u32 0, RLD offset, packed size, unpacked
  size (unused by the loader).
- `0x57e430` is LZSS **without a ring buffer**: one flag byte per eight items,
  least significant bit first; 1 is a literal; 0 is two bytes `b0 b1` giving a
  back-reference into the output of distance `4096 - (b0 | (b1 & 15) << 8)` and
  length `(b1 >> 4) + 3`. Overlapping copies repeat bytes. It stops when the
  packed input ends. All 57 entries unpack to exactly their stored size and no
  match reaches before the output start.
- `0x57e580` walks `{u32 tag, u32 size including header}` chunks after the
  8-byte `"TLF."` header; tags are byte-reversed constants. Every entry holds
  `"3BGR"` (256 8-bit RGB triples) and `"LXIP"` (u32 0, u16 width, u16 height,
  indexed rows). All share one palette except the unreferenced `p_quit`.
- Fonts (`0x57ad10`) are one strip: the first pixel's colour separates the
  glyphs of codes 0..255 along row 0 from x 1; a glyph's width is its advance.
  The font14 family's space really is 14 pixels wide. `FONT_n_NAME` in
  SHELLCFG.H names the slots: 10/11/12 outer-shell button text normal, hover,
  pressed; 13 outer titles; 2/3/4 inner-shell buttons; 5 inner titles; 6 red
  info text; 8 tables and lists.

### Screens [V]
- State machine `0x579de0`: 3 main, 4 quit, 0xa single player, 0xb
  multiplayer, 0xc instant action, 0xe credits, 0xf load, 0x10 custom, 0x13
  campaign mission map, 0x14 campaign options, 0x1a briefing; a screen returns
  the next state, 9 is "back". Escape returns to the main menu.
- An outer-shell button (`0x570620`) is a 160x30 BTTN with a click-through TEXT
  child centred both ways in fonts 10/11/12; it fires on release over the
  button. There are no button sounds. Titles are TEXT centred on x 320.
  Custom and Load use `BTN_DEFAULT_WIDTH`.
- Inner-shell image buttons (`0x570680`) draw their RLD picture only while
  hovered or pressed, at their own position; the background shows the normal
  state.
- Briefing (`0x5776e0`): Freedom Guard LAUNCH (250,405,160,60) with `bf_lnch`
  at (250,410), BACK (15,405,135,60), text box (25,20,275,340); Imperium
  LAUNCH (145,348,105,75) with `bi_lnch` at (138,350), BACK (10,400,80,65),
  text box (350,150,255,295). The text is section `\1` of the mission `.BRF`.
- Options (`0x574f90`) is the campaign options screen (save list, Load/Save/
  Delete, Quit to Main Menu, Quit to Win95, back arrow), reached from the
  mission map's sidebar. The HUD's own in-game menu was not traced.

### Multiplayer toolkit [V]
- `0x511cd0` builds every multiplayer panel up front with nine PCX fonts
  (F16BLUE, F14BLUE/O/G, F12GOLD, F12TEAM, F12BLUEN/O/G; a marker row above
  each strip) and MULTMENU bitmaps. Captions come from MLSTRING.CFG by widget
  name (`<name>StaticTitle`, `<name>ButtonTitle`); the exe text is a fallback.
- Instant action sets `0x6ccac8` and opens the game-setup ("Chat", `0x516710`)
  panel directly over `MM_IA.BMP`; multiplayer uses `MM_SETU.BMP`. Player rows
  are at y `68 + 13*row`: type 19/181, side 219/90, team 309/60, handicap
  369/81. Bottom bars are 110x32 `BUTTON.BMP` buttons (normal, hover, pressed
  110 pixels apart) at x 27/140/393/506, y 414; LAUNCH (`LAUNCH.BMP`) at
  (266,410). Popups centre on (320,220).
- Connection menu `MM_MAIN.BMP`: player name, then INTERNET, LAN [IPX],
  MODEM, SERIAL and MANUAL IP buttons, 188x30 at x 226, y 134..342 in steps
  of 52, text-only (F14BLUE/O/G).

### Port behaviour (not native fidelity)
- The construction kit, intro movie (the shipped SMKs are empty here and
  there is no Smacker decoder), saves, the mission-map nodes, archive, story
  and debriefing screens are not reproduced; their buttons are disabled or
  absent.
- In a level, Escape and the HUD MENU button open the shell's options screen;
  Quit to Main Menu releases the level through `menuleave`.
- Dropdowns are shown closed and step through their items on click (right
  click steps back). Togran, handicaps, fog style, placement, colours and the
  give/view options are shown and kept but do not change the game yet.
- INTERNET, MODEM and SERIAL are disabled. LAN uses the engine's UDP session
  and lobby: the host leaves rows Available for LAN players and its first
  LAUNCH opens the lobby, fixing the map, seats and options. Joiners enter the
  same setup panel, change only their own row's side and team
  (`I_SetNetChoice`), and LAUNCH toggles their ready check, shown with the
  row's `LIGHTS.BMP` ready light (ChatRowLaunch). The host's LAUNCH is its own
  ready check; the game starts once every seat has joined and every player is
  ready (`I_LaunchNetGame`). The native start trigger was not traced. The
  Messages box and ChatMessageEntry carry the lobby chat as
  `<name>: <text>`. Humans take players 0..n-1, as `D_PlayerIsHuman`
  requires. Manual IP joins the typed address.
- The game setup reaches the loader through `DR_RequestSkirmish`: Available
  and Closed teams start empty, a chosen side swaps the authored units
  (construction crew, infantry, transporters, medium tank), team members ally,
  and the credits field replaces each team's `SetCredit`.

## Placement foundations (2026-10-08)

No executable was examined for this addition. **Confirmed native data:**
`deftxt/OVLEFF.TXT` DefineOvlEffect records name each RSPR canvas, width, height
and row-major effect pairs. Effect 3 blocks movement; effect 2 is a passable
bay; -1 leaves the cell untouched. Existing `w_map.c::resolve_effects` and
`apply_effects` establish these meanings. BUILD.TXT SetBuildingImages supplies
the associated terrain/body/top-layer composite. Its native object position
is the canvas's top-left (the existing composite ground_point is zero).
For example `nfhqt1l0.spr` is 5×6 with two empty upper rows; a sprite's pixel
size is not a substitute for its foundation mask. The matching actor catalog
now records these native dimensions/masks and corner anchoring.

**Requested engine behavior:** HUD building buttons select a world location
before payment. The shared preview draws the native composite and tints required
cells; both blocking cells and passable bays must be free of another building
or ground unit for placement. Empty cells do not constrain placement. Pending
construction reserves its site, completion revalidates it, and AI automatic
placement uses the same predicate. Original bridge/water special placement,
retail construction timing are not newly inferred. Native Dark Reign path/docking behavior is left intact; the
new generic footprint mask is a placement rule, not a wholesale replacement
of Dark Reign's map effects.

The old actor-only `_d`, `_imp` and `_t` building sprite aliases did not name
retail images. Confirmed `BUILD.TXT` SetBuildingImages records for decoys,
Imperium bridges and Togran bridges use the same un-suffixed bodies as their
counterparts (for example `fh1_decoy`, `SmallHorizontalBridge`, and
`TogranSmallHorizontalBridge1`). The generated state table already had these
native filenames. The actor table now uses those same names and masks so
on-demand preview loading succeeds without inventing an alias resolver.

`env SDL_VIDEODRIVER=dummy build/bin/tests/dark-reign/test_building_placement`
checks placement/payment/completion; `test_mission_hud` verifies the native
HUD now remains in location selection before charging for an Imperium HQ.
