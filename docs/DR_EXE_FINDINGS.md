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

**Implementation consequence.** Resource bases carry a resource acceptance
mask. Freighters select the nearest base accepting the mine's resource, and
return when full; an unrelated HQ is not a fallback. Removed the prior
three-cell distance exclusion: the executable evidence describes typed source
and destination matching, not a distance rule. `impmn` now yields resource 1
while `impww` yields resource 0.

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
