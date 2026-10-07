# Warcraft II gathering and HUD evidence (2026-10-04)

## Vehicle movement frames (2026-10-07)

The MAINDAT and WAR2.EXE fingerprints recorded below still apply. This
investigation decoded native GRPs and compared the pinned Wargus and Warcraft
2000 sources; WAR2.EXE was not disassembled or run.

**Confirmed reference behavior:** Wargus `scripts/human/anim.lua` and
`scripts/orc/anim.lua` select these `Move` pictures. Divide Wargus frame
operands by five to obtain our logical frame (five facings per row):

| Units | Wargus Move operands | Logical movement frames |
|---|---|---|
| Ballista, catapult | 0, 5 | 0, 1 |
| Both tankers, transports, destroyers, battleship, juggernaught | 0 | 0 |
| Gnomish submarine, giant turtle | 0 | 0 |
| Flying machine (`animations-balloon`) | 5, 0 | 0, 1, repeating |
| Zeppelin, Eye of Kilrogg (`animations-eye-of-vision`) | 0 | 0 |

Siege movement uses the same poses in both EnhancedEffects branches; the
enhanced branch adds a turn wait. Surface ships use operands 5 and 10 for
**death**, while submarines/turtles use those operands for **attack**.
Ballista/catapult attack uses operands 10 and 15. None belongs in movement.

**Confirmed defect:** `w2_limit_walk` assumed frame 1 followed by
`min(phases - 1, 4)` pictures was a walk cycle. Temporary diagnostics on the
native ALAMO asset load printed `ballista: phases=4 frame=1 count=3`.
Thus a rolling siege unit showed rows 1, 2, 3 (including firing art) and a
three-row ship showed rows 1, 2 (sinking/attack art) instead of row 0.
Before loading art, every mobile type incorrectly had four frames starting
at 1. **Disproven:** GRP row count determines movement sequence semantics.
Native examples are MAINDAT entries 49/50 (ballista/catapult), 59 (human
tanker), and 41 (battleship). The five-facing decode itself was unchanged.

**Implementation:** `w2_build_states` authors the vehicle movement ranges
above both at initialization and after GRP loading. Single-pose vehicles
retain a looping movement state with `A_Chase`, separate from standing.
Native frame counts bound ranges but no longer define vehicle animation.
Infantry retains its existing rows 1–4. The flying machine uses a cyclic
0/1 sequence; Wargus starts at 1 and has uneven waits within its move script.

**Warcraft 2000 comparison:** `Nation.cpp::OneObject::LoadAnimation` selects
`MoreAnimation` by `WhatFor` and `Kind`; land movement in `Nation.cpp` and
water movement in `Water.cpp` call `LoadAnimation(1, AnmGoKind, 0)`.
`LoadCurAnm` then selects one of five directions and mirrors the other
three. This confirms purpose-specific movement selection in that reference,
but its different asset format does not establish Warcraft II frame numbers.

**Remaining limits:** exact retail movement timing and speed conversion are
unknown. This correction preserves the existing four-tic presentation cadence
and movement speed; it does not reproduce Wargus's move/wait script timing,
enhanced turning pauses, or bobbing. The existing combat builder also groups
three-row surface ships with submarines and four-row scouts with siege;
those attack/death classifications remain a separate known limitation.

**Reproduce:** `env SDL_VIDEODRIVER=dummy make test-warcraft-2` includes
`test_movement`: 17 types, three complete state cycles and return to stand,
before and after native GRP loading, plus eight-direction availability for
every selected movement row. The new test failed on the old frame selection.
Verification: `make`, the movement regression, headless `--check`, and an
ALAMO world screenshot passed. Of 24 Warcraft II test binaries, 22 passed;
`test_menu` (`find(S(38, 5))`) and `test_net_menu` (English LAN labels)
failed identically when relinked with the original pre-fix state builder.

## Button decorations and footprint selection correction (2026-10-04)

The MAINDAT and WAR2.EXE fingerprints below still apply. WAR2.EXE was not
disassembled in this correction. REZDAT.WAR SHA-256:
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`.
The three supplied 640×480 JPEGs are recorded in `REFERENCES.md`; their
distribution/version is unknown and JPEG compression prevents exact palette
identification from their pixels.

**Confirmed native records:** MAINDAT GFUs 354/355 contain four full
176×176 frames at origin (0,0). Frame 0 starts at decoded entry offset 38
and is plain stone. Frames 1/2 alias offset 31014 and carry the surrounding
info-panel rim. Frame 3 at offset 61990 also contains the progress-bar
surround. The previous first-frame-only plate decoder discarded these
authored decorations. Decode all frames with the existing GFU decoder;
choose 0 for empty/group selection, 1 for single selection, and 3 for
training. The separate max-box composition is unnecessary for these full
native frames and has been removed.

REZDAT human/orc GFUs 0/1 contain the blue/red 128×20 thin button in frames
4/5 (normal/pressed), at decoded offsets 6342/8902. Palette 14 supplies
their colors. The supplied screenshots place the Menu control at (24,2);
the MAINDAT 293/294 stone backing is a separate 176×24 image. Previously
we drew only that backing and the text. Draw the REZDAT button on it, with
the existing indexed palette remapping and shared menu state dispatch.

**Confirmed screenshot geometry:** command icons occupy 46×38 rectangles
at the existing (9/65/121,340/387/434) positions. A bright one-pixel rim
lies one pixel outside the icon: the first command's upper-left rim is
(8,339). The surrounding dark edge adds another pixel, yielding a 50×42
decoration extent at (7,338). Group/single portraits and the training
icon use the same surround. The screenshots show a green active-command
rim. open-rts draws these rims in shared `hud/m_menu.c` from table fields,
including focus, press, and a command waiting for a world target. It uses
native-palette black/white/green; exact pre-compression RGB values and
retail hover/press dispatch remain unknown. All icon pixels remain native
GRP pixels, and native Menu pressed artwork is used directly.

**Disproven sprite placement:** the former GRP pivot `(width/2,height)`
shifted each sprite above the simulation footprint by half its native
canvas height. A 72×72 worker's selection then appeared below its body.
Stratagus `src/unit/unittype.cpp::DrawUnitType` centers its sprite canvas
on the occupied tiles: subtract `(sprite_size - tile_size)/2` from the
tile origin. With our object already stored at the footprint center,
the equivalent pivot is `(width/2,height/2)`. This is a reference-derived
placement correction, checked against the supplied centered building
selection; it is not a WAR2.EXE instruction trace. Native canvas and
frame pixel data, bearings, and team remaps are unchanged. Carrier GRPs
use the same loader and pivot rule.

**Explicit user presentation contract:** draw selection before all world
sprites, using `mobjinfo[type].w2.footprint * app.cell`, centered on the
object's planar position. Do not use the GRP canvas, opaque bounds, or
the reference `BoxSize`. Thus a Ballista still selects 1×1 tiles despite
its 63×63 reference box; ships select 2×2, barracks 3×3, and halls 4×4.
This overrides any reference selection-size differences. The shared
renderer runs the existing overlay callback family in a ground pass before
world sprites; Warcraft II supplies the native footprint rule there.
Sprite rectangles and floating health bars are suppressed for this game,
matching the supplied HUD-only health presentation. Hidden/invisible
objects do not draw marks. The global thinker/object ownership is unchanged;
DOOM `r_things.c` was consulted for its sorted sprite drawing separation.

Reproduction (headless):

```sh
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_hud /private/tmp/w2-human.bmp /private/tmp/w2-orc.bmp
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_selection
```

HUD tests cover both races, native menu states, all four info frames,
single/group portraits, and training decorations. The selection regression
independently checks 1×1/2×2/3×3/4×4 extents, camera/cell scaling, hidden
objects, and sprite occlusion through both world draw entry points. The
PUD test checks the native Footman's 72×72 canvas pivot at (36,36).

This investigation uses the retail DOS data in `data/WAR2`. It does **not**
claim an instruction trace of WAR2.EXE or a complete pixel comparison against
a running retail game. The executable is DOS4GW/LE; no r2 installation or
retail runtime was available during this work. Function addresses, movement
conversion, exact gathering delays and the original minimap renderer remain
unknown. See `REFERENCES.md` for source URLs and pinned reference versions.

## Inputs and fingerprints

- `DATA/MAINDAT.WAR`: 10,403,193 bytes, SHA-256
  `791bae4480d564f017122a82c9481dabd952424151f2b5d20245793e654ad3bb`.
- `WAR2.EXE`: 878,119 bytes, SHA-256
  `a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`.
- `reference/wargus`, commit
  `cde1a0718a0058cc651ecd56ff8149fc39f624e9`: format and behavior reference,
  not copied source. `reference/DOOM/p_tick.c` supplies the shared thinker
  ownership and deferred-removal model already used by Dark Colony.
- `reference/warcraft2000`, commit
  `4d12ad3e62ba03c59b2dbec2a989f58d744018ee`: gathering and interface
  comparison only. Its formats, resource multipliers and HUD are not DOS
  Warcraft II rules.

## Resource records and worker behavior

**Confirmed native records.** PUD `UNIT` records are eight bytes: x/y at
0/2, type/player at 4/5, data at 6. Gold mine type 92 becomes actor 93.
`ALAMO.PUD` has 11 mines and 2,787 forest cells (`SQM & 0x0080`). The
loader now creates a level-owned deposit for each. Mine reserves use
`UNIT.data * 2500`, independently checked against `wargus/pud.cpp`.
`test_pud` matches every mine back to its PUD coordinate and reserve.
The format interpretation is supported by Wargus, not a WAR2.EXE trace.

**Confirmed published behavior.** Blizzard's Peasant/Peon guide specifies
51 chops per tree, with progress belonging to each worker. The first worker
to finish takes the wood; a competing worker loses its progress and starts
another tree. The guide's worker stats are HP 30, damage 1–5, armor 0, sight
4, speed 10 and range 1. The general resource guide specifies 100 gold per
trip and increased yield after hall upgrades. These are authored C values;
neither unit stats nor timing are loaded from Lua or retail balance tables.

**Reference-derived implementation.** One tree contributes 100 wood.
Workers finish an axe state chain 51 times, carry one load, enter the nearest
reachable owned hall or (wood only) lumber mill, then resume gathering.
Reserves decrease at pickup; the player's stock increases at delivery.
Gold and wood cargo remain typed when retargeting. Manual Return Goods,
Stop, Move, routes and context clicks use the shared tic-command pipeline.
No owner-15 mine is treated as an enemy attack target when the selected
worker can harvest it. Invalid moves preserve the current gathering state.

Mine/depot entry waits are 150 state tics, from Wargus worker `units.lua`.
The axe chain uses logical frames `{5,6,7,8,9,5,5}` and tics
`{3,3,3,5,3,7,1}`, from its worker animations. **Unknown:** these delays
have not been traced in the DOS executable; our engine runs at its shared
tic rate. Wargus animation numbers alone do not prove retail elapsed time.

Wargus `ImproveProduction` describes player income improvements: gold
10/20 from Keep/Stronghold or Castle/Fortress, wood 25 from a lumber mill,
oil 25 from a refinery. We use the greatest living owned improvement,
never sum duplicate buildings. It applies when unloading at any eligible
depot and disappears when the improving building is removed. **Inferred:**
bonus credit and rounding are derived from the reference model; the exact
retail pickup/depletion accounting is untraced. Oil extraction is not
implemented by this change. Missing or removed depots preserve cargo and
workers retry when an owned reachable depot becomes available.

**Confirmed GRP metadata / corrected hypothesis.** Entries 124/122 are
human gold/wood carriers; 125/123 are orc gold/wood carriers. All four
native entries decode to **65 records / 13 temporal frames**, with five
authored facings mirrored into eight directions. They are not 25-record
strips: wartool.h's trailing `25` describes its repair-frame combination,
not the native GRP header count. We retain every record and use the first
five temporal frames for stationary/carried walking poses. `test_pud`
checks all four counts and direction definitions.

Wargus tileset tables designate megatile 126 as `removed-tree` in summer,
winter, wasteland and swamp. Our tileset lookup adds one internal slot
pointing to that native megatile; felling a tree clears its forest collision
and uses the stump. **Unknown:** retail neighboring forest-edge updates
are not reproduced. Exhausted mines release their footprint and use normal
deferred mobj removal; native collapse/rubble animation is unimplemented.

## Warcraft 2000 comparison

**Confirmed reference behavior, not retail Warcraft II evidence.**
`Nature.cpp:203` (`TakeResource`) depletes cells and rebuilds neighboring
surfaces; `TakeResFromCell` at 229 visits a 2×2 group of native cells.
`FindNearestBase` at 446 filters by country and accepted resource and uses
Manhattan distance. `TakeResLink` at 492–611 switches between gathering and
return phases, uses work animation 2 and the resource's portion capacity,
retries a missing/dead base, and credits
`RAmount * RDS.Multi + RESADD[owner][rtype]` plus taxation before resetting
cargo. This supports typed loads, owned depots and recoverable delivery,
but its distances, capacities and numeric rules were not adopted.

**Rejected reference shortcuts.** `OneObject::TakeResource` at 466 changes
the resource type even with an existing load; its rejection is commented
out. We preserve the old load's type on retargeting instead. `Build.cpp:42`
withdraws resources before checking building placement; a failed placement
can therefore lose resources. Our training commands check both gold and
wood before either is withdrawn. Construction still needs the separate
tic-command correction listed below.

`Interface.cpp:1525` (`GSSetup800`) uses a 160-pixel column and 48×40 icons,
with the map starting at (160,0). `Nature.cpp:1017` (`ShowRMap`) displays
three text resource rows. These are disproven candidates for the native
DOS Warcraft II HUD, whose assets use 176 pixels and 46×38 icons.
`mapa.cpp:827` (`CreateMiniMap`) samples native 32×32 terrain at byte 33
(pixel 1,1), reducing 2×2 cells; its marker at 924 uses yellow brackets
and health. Only the native-pixel sampling idea applies here; our sampling
and green owned markers use the Wargus/Blizzard references below.

## Native HUD loading and layout

**Confirmed native assets.** MAINDAT entries 293/294 (menu), 295/296
(minimap frame), 297/298 (command panel), 287/288 (resource strip), 291/292
(status strip), 289/290 (right filler), 354/355 (info GFU) provide the human
and orc chrome. Existing 640×480 geometry uses a 176-pixel left column,
16-pixel top/bottom strips and a 16-pixel right filler. Viewport:
`{176,16,448,448}`. GFU 187 supplies native 14×14 resource icons; command
and portrait icons come from native GRP 356/357/358. No generated or PNG
art is bundled. The HUD test compares more than 10,000 untouched panel
pixels per race and every nontransparent gold/wood/oil icon pixel against
the decoded native entries. This proves asset use, not full retail layout.

FONT 282 is the game font; 283 is the smaller HP font. A native FONT pixel
stores an ink category `control & 7`; transparency comes from skipped
pixels, not ink category zero. We store categories as indices 1–8 so the
engine's transparent index zero remains separate. Native glyph x/y
bearings are preserved; advance is `xoff + width`. For `M`, width 10 and
xoff 1 give advance 11. **Disproven:** dropping the x bearing and treating
category zero as transparent loses native spacing and dark/shadow ink.

**Reference-derived color/layout.** Wargus `scripts/fonts.lua` maps white
ink categories to `{239,246,246,246,104,239,239,239}`, yellow to
`{246,200,199,197,192,239,104,239}` in the native palette. Game counters
and stats use white, Menu uses yellow. The old synthetic grayscale/gold
tint was not native. These mappings are tested against the retail palette;
the executable's ink dispatch is untraced.

`scripts/human/ui_pandora.lua` and `scripts/ui.lua` supply reference
coordinates for the resource icons (x 176/251/326), portrait (9,169),
centered name (114,171), 50×7 HP bar (8,211), small HP text (35,221),
three command columns (9/65/121) and rows (340/387/434), multiple selected
portraits, training icon (110,241) and progress bar (12,313,152,14).
Larger windows anchor/stretch the existing chrome via the shared menu
table, including a tested width anchor in `hud/m_menu.c`.
Stratagus `src/ui/contenttype.cpp:60` resolves `~|` as a prefix-width
anchor: the text starts at `Pos.x - font.Width(prefix)`. Stat labels end
at x 100 before their colon, production at 85, and `Level ` at 154.
**Disproven:** placing every stat label at x 60 loses that alignment.

Armor and minimum/maximum damage text now comes from the C unit catalog
rather than displaying armor zero and minimum damage one for every unit.
Blizzard's published stats override mismatches in the reference, notably
Footman 2–9/armor 2, Catapult/Ballista 25–80/armor 0, Destroyer 2–35,
Battleship 50–130 and Submarine 10–50. Remaining catalog values and hero
labels are reference-derived, not an executable table extraction.

**Reference-derived minimap.** Stratagus `src/map/minimap.cpp`,
`GetTileGraphicPixel`, samples native terrain at x `7 + phase * 8`,
y `6 + phase * 8`, with scale precision 100. The HUD borrows the driver's
tileset and applies these samples rather than selecting artificial colors
for terrain categories. Unexplored cells are black. `DrawUnitOn` supplies
native footprint scaling, a one-pixel inset, green owned markers and bright
player colors; mines are yellow. Tests compare terrain samples and every
pixel of a single worker footprint independently of the world rendering.
Blizzard's DOS manual also explicitly identifies owned minimap units and
buildings as green. It lists gold, lumber, oil and food in the resource
bar; the previous synthetic score zero was removed. Wargus `widgets.lua`
uses 46×38 command icons with a black border and the same artwork through
hover/click. The supplied retail screenshots now supersede that reference
for icon rims; see the correction below.

**Still unverified / incomplete 1:1 claim.** The original executable's
minimap sampling and viewport marker remain untraced. Food alignment,
hover/pressed command treatment, exact stat text pixels, health color
thresholds and world selection/cursor overlays have not been compared
pixel-for-pixel against this DOS retail build. Wargus layouts can contain
enhancements. The current HUD uses native artwork and reference placement,
but a complete 1:1 retail match must not be claimed from these tests.

## Code review against Dark Colony

| Finding | Result |
| --- | --- |
| Neutral mines had no runtime resource reserve; forests were only collision | Added level-owned deposits, ordinary worker thinker state chains, depletion and delivery tests. |
| Right-clicking a mine dispatched Attack because its owner is 15 | Context picking now recognizes accessible resource sources; native HUD test exercises an actual right mouse event and tic command. |
| Lumber was spent in a HUD callback, while gold was spent through production | Both costs are checked and withdrawn when the training tic command executes; insufficient gold or wood changes neither stock. |
| Warcraft hid its state/type tables behind static game-prefixed storage | Uses `states`, `mobjinfo`, `sprnames` and shared stable mobj ownership, as Dark Colony/Doom do. Carrier definitions are built from native GRPs by the loader. |
| Empty `VER`/`ERA` sections could read payload byte zero without a span | Both now require nonempty payloads. |
| Building placement still directly spawns/spends from the HUD | **Remaining P1:** move construction into deterministic tic commands before Warcraft multiplayer is supported. Dark Colony queues simulation work separately from HUD input. |
| Combat flags/actions and campaign AI remain absent | Fighters carry `MF_ATTACK` and attack/death state rows built from the retail GRP phase layout with the Wargus waits; damage is Stratagus' `CalculateDamageStats` roll with research applied; range is Warcraft's tile distance to the target footprint (shared `mobjtype_t.footprint`); destroyed buildings free their cells. Research (sword/axe, arrow/throwing axe, shield lines) and hall upgrades (keep/castle, stronghold/fortress) run through the shared production queue with the pinned Wargus costs, times and dependencies. `test_combat`, `test_research`, the shared retaliation suite and HUD button checks cover it. **Remaining P1:** no skirmish AI features; AI shared suites stay excluded. |
| Patrol, stand ground, repair, save/load/options are visible placeholders | **Remaining P2:** no retail behavior is claimed; Dark Colony has real native menu actions and mission state. |
| Exact retail delays, minimap and several HUD states lack original-runtime evidence | **Remaining P2:** native asset tests and a Wargus layout are insufficient to certify complete 1:1 fidelity. |

## Reproduction

```sh
make
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy make test-dark-colony test-layout test-network
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_hud \
    /private/tmp/w2-human-hud.bmp /private/tmp/w2-orc-hud.bmp
```

Harvest regressions cover both races, partial/depleted gold, 51-chop wood,
stumps, repeat gathering, cargo retargeting, orders, invalid moves, missing
or destroyed depots, tree competition, multiple miners, enclosed deposits,
player-wide bonuses and atomic
training costs. The Warcraft target also runs shared facing, speed,
image, menu-item, navigation, retaliation, video and waypoint tests. Tests
that require Warcraft skirmish AI or the engine fallback menu are excluded
explicitly.
LAN tests require permission to bind local sockets in restricted runners.

## Complete base unit catalog audit (2026-10-04)

The WAR2.EXE and MAINDAT fingerprints at the start of this report still
apply. This audit read reference definitions, not executable instructions;
WAR2.EXE behavior remains untraced. Wargus is pinned to
`cde1a0718a0058cc651ecd56ff8149fc39f624e9`; Warcraft 2000 to
`4d12ad3e62ba03c59b2dbec2a989f58d744018ee`.

**Confirmed in reference:** Wargus `pud.cpp::UnitScriptNames` enumerates
105 PUD slots, 100 defined and five empty (native 34, 36, 37, 48, 54).
Every defined entry resolves in `scripts/human/units.lua`,
`scripts/orc/units.lua` or `scripts/units.lua`. This covers both races'
regular and upgraded units, all named heroes, summons, naval and flying
units, buildings, tower variants, neutral resources, special structures,
start markers and walls. Extra Wargus corpse/dead-vision/super-unit types
are not PUD slots and were not added to the native roster.

`info.h` now names every slot. `info.c::mobjinfo[]` is the sole authored
balance catalog, replacing the separate small `w_units.c` table and its
partially populated runtime copy. Numeric and capability fields include:

- HP, armor, raw speed, basic/piercing and displayed damage range;
  sight, attack minimum/maximum and person/computer reaction ranges.
- Footprint/box dimensions; gold, lumber, oil and time costs; repair HP,
  cost, range and automatic range; food supply/demand; mana maximum,
  initial value and regeneration; points, priority, AI annoyance and level.
- Decay, transport capacity, movement domain, targets, storage and resource
  provision, income improvements and each resource's capacity/step/waits
  and gathering flags.
- Organic/undead/hero/volatile, cloak/detection, indestructible, coward,
  ground/side attacks, selection/fog, shore construction, builder-outside,
  elevated, attack, neutral, teleporter and harvestable flags; projectile
  and spell identifiers. The native GRP lookup remains with the definition.

HP and maximum damage remain in Doom's `spawnhealth` and `damage`; they
are not duplicated in `w2_stats_t`. Existing ActorType projections and
state/rotation views are derived from this catalog. Zero-HP resource
markers preserve authored zero although the current engine projects HP 1.
The engine's speed divisor and sight clamp remain presentation choices.
The HUD reads base levels, food and prices from the table; training reads
`Costs.time` as seconds rather than deriving a duration from gold cost.
Gathering reads capacity and entry waits; depot choice reads storage masks;
income reads living owned structures' catalog improvements.

**Corrections and explicit differences:**

- Wargus gives Ballista armor 5. Blizzard's Catapult/Ballista guide gives
  armor 0; the authored table preserves 0. Published damage minima already
  in the catalog remain the HUD values; they are not guessed from a random
  damage formula or claimed to have been traced in the executable.
- `scripts/spells.lua::DefineVariables` defaults Level to 1 and Mana to
  `{Max=255, Value=84, Increase=1}`. Paladin, Ranger and Berserker set Level
  2. Ogre Mage uses a variable table with `Value=2`, rather than a scalar;
  treating that table as a number would incorrectly import 0. All are now
  parsed and authored correctly.
- Blizzard's Mage and Death Knight guides give initial mana 85, maximum
  255. Enabled mana uses 255/85/1 rather than Wargus's 84; applying the
  initial-value override to other enabled casters is an inference from
  the shared variable. Spell identifiers are metadata, not spell execution.
- Wargus Critter has BasicDamage 80, Demand 1, a critter-explosion missile,
  random movement and click-to-explode behavior. The existing engine choice
  keeps damage/demand 0 and leaves that special behavior unimplemented.
  The reference projectile and targeting metadata are preserved.
- **Disproven assumption:** a land-looking Daemon sprite does not establish
  movement domain. Wargus explicitly specifies `Type="fly"`, `AirUnit=true`;
  the old unsupported land classification is replaced with that reference
  rule. Retail dispatch remains unknown.
- Walls are buildings even though this engine skips wall object spawning.
  Numeric `Indestructible=1` is equivalent to `true` in the reference:
  oil patches, circle of power and both start markers now retain that flag.
  Domain is separate from `SeaUnit`: naval resource structures can have
  `Type="naval"` without being ship units.
- Deathwing has complete reference stats (HP 800) but its native GRP lookup
  is still unknown, `{0,0,0,0}`. No Dragon graphic is silently substituted.

**Warcraft 2000 comparison:** `MapDiscr.h::GeneralObject` groups capability
flags, `cost`, `delay`, `capMagic`, `ResourceID[]` and `ResAmount[]`;
`Visuals` adds life, shield, damage and productivity. This supports storing
related rules together, but Warcraft 2000's numerical rules and object
storage are not Warcraft II data and were not imported. Doom remains the
object/state ownership reference.

**Verification:** the C `test_catalog` independently parses the pinned PUD
mapping and balanced unit tables, then compares every defined type's base
numeric fields, dimensions, resources, capabilities, projectile and spell
list, with the explicit overrides above. No reference is loaded by the
game. Without the ignored reference checkout only that comparison skips;
105-slot coverage, five reserved slots, all 96 object spawns, persistent
authored stats across initialization, hero/mana/transport checks and
stat-backed production still run. Reproduce with:

```sh
env SDL_VIDEODRIVER=dummy build/bin/tests/warcraft-2/test_catalog
env SDL_VIDEODRIVER=dummy make test-warcraft-2
```

The reference audit covers base definitions, not upgrade effects or a
completed simulation of combat, spellcasting, repair, transport or oil.
Original attack/recovery, mana timing and training clock fidelity remain
unknown until retail runtime or executable evidence establishes them.

**Audit outcome:** `make all`, all eleven headless Warcraft tests and the
Warcraft game `--check` passed. The independent reference audit checked
all 100 defined types; the roster test spawned all 96 object definitions.
ALAMO still loads 64 units and 2,798 resource deposits. Native human/orc
HUD captures and pixel regressions passed after routing stats to this table.

**Verification outcome (2026-10-04):** `make all`, all ten Warcraft tests,
the complete Dark Colony suite, native SPR/FIN layout and network suite
passed. All five game binaries passed `--check` with the dummy SDL driver.
The final lossy/reordered network run completed 550 tics with 296 packets
dropped, 145 duplicated and 130 reordered. Human/orc HUD captures were
rendered and inspected at 640×480. These outcomes verify this engine's
implementation; they do not remove the original-runtime unknowns above.

## Native sound playback (2026-10-07)

**Evidence and scope.** WAR2.EXE SHA-256 remains
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`;
this task did not disassemble or execute it. The reference pins in
`REFERENCES.md` apply. These are confirmed archive facts and reference
assignments, not a claim of verified retail sound dispatch.

**Confirmed native data.** `DATA/SFXDAT.SUD` is 6,809,845 bytes, SHA-256
`05645c6efb4f38acbff955b20bfd06a6da42942434f37f771f7c792f1e62fb95`.
Its header is WAR magic 0x19 at offset 0, 293 entries at offset 4 and
archive type 5000 at offset 6; its first record starts at 0x49c.
The existing WAR extractor handles these records unchanged once type 5000
is accepted. All 184 referenced SFX records decode as RIFF WAVs, as does
MAINDAT entry 432 (the 1,524-byte UI click). The sample bytes are read from
the native archive, decoded in memory, and freed after conversion to the
shared mixer's mono signed-16 representation. There are no extracted
runtime files or reference-checkout dependencies.

**Disproven initial assumption.** SNDDAT.WAR is not the gameplay sound
bank. Wargus `wartool.h` places gameplay effects/voices in SFXDAT.SUD,
UI click/highclick/statsthump at MAINDAT entries 432/435/436, and campaign
speech in SNDDAT.WAR. `wartool.cpp::ConvertWav` preserves the extracted
WAV bytes; no additional codec or guessed raw-PCM header is required.

**Confirmed reference assignments.** Wargus `wartool.h`,
`scripts/sound.lua`, and `scripts/{human,orc}/units.lua` establish the
entry identities, group membership and type/event assignments authored in
`games/warcraft-2/sounds.c`. Examples (zero-based native entry indices):

- Human selection: 5,7,9,11,13,15; orc selection: 6,8,10,12,14,16.
  Human acknowledgements: 32,34,36,38; orc: 33,35,37,39.
- Peasant selection: 271–274, acknowledgements: 275–278, ready: 263.
  Peon selection/acknowledgements share the grunt voice, but ready is 115.
- Swords: 60–62; bow throw: 66; axe throw: 77; peasant attack: 81;
  lightning: 111; touch of darkness: 112; catapult/ballista: 55.
- Building destruction: 52–54; chopping: 56–59; ship sinking: 51.
  Dragon/Deathwing deaths use explosion 31, not orc infantry death 50.
  Eye of Kilrogg has selection click but no assigned acknowledgement/death.
- Buildings retain their distinct selection samples (e.g. farm 74,
  pig farm 75, blacksmith 69). Worker construction completion uses human
  peasant 42 or orc 41; research completion uses player-side 40 or 41.
- The base bank ends at 292; it lacks expansion hero voices. The authored
  hero assignments use Wargus's explicit non-expansion mappings, including
  Teron selection sharing basic orc voices and acknowledgement using the
  death knight. No guessed sound aliases are used.
- `scripts/{human,orc}/anim.lua` puts tree-chopping on source frame 40,
  which is logical frame 8 after the five-direction GRP conversion. The
  fourth state of the existing seven-state work cycle now invokes
  `A_W2_Chop`; attack audio uses the existing attack action event.

**Reference comparison / implementation policy.** Warcraft 2000
`GameSound.cpp::LoadSounds` and `PlayEffect` group WAV alternatives and
keep playback state separate from game simulation. `AddWarEffect` and
`AddWorkEffect` check visibility and pan relative to the viewport. Its
DirectSound buffers, fog thresholds, pan/attenuation constants and RNG
are not Warcraft II rules and were not imported. Dark Colony's shared
`sound/s_sound.c` and `sound/i_sound.c` provide sample ownership, channel
handles, stereo positioning, volume, ownership-filtered barks, fog checks
for enemy world sounds, origin unlinking and level/shutdown cleanup.
Original Doom `s_sound.c` was consulted for the sound/channel ownership
boundary. No second mixer, simulation storage or sound RNG was added.

Native samples live only in the engine sfx table. Initialization failures
free partially loaded samples before closing the audio device, so a retry
starts with a clean table. Training, construction and research completion
use the shared single-voice bark channel; level start stops old playback.

**Unknown / deliberately not inferred.** Retail annoyance thresholds,
under-attack warning cooldowns, distance attenuation, per-sample limits,
hero expansion dispatch, and tower attack assignments remain unknown.
The existing engine group randomization/no-repeat and voice interruption
are engine policy, not proven Warcraft II behavior. Warcraft retains shared
stereo panning with no new distance curve. Wargus itself marks ship attack
assignments uncertain; its fireball-throw assignment is used as reference
behavior, not retail proof. Music, campaign narration, annoyance escalation,
alerts, and sounds for unimplemented spells/transport are not implemented.

**Reproduction.** Build and run `build/bin/tests/warcraft-2/test_sounds`
with SDL video/audio dummy drivers. It checks 185 loaded WAVs and 35 groups,
plays/stops every loaded sample, checks voice/weapon/building assignments,
volume, fog, ownership, unchanged combat RNG, level cleanup, malformed WAV
rejection, partial-load failure/retry, shutdown/reinitialization and nosound.
Temporary `OPEN_RTS_DEBUG_W2_SOUND` logging recorded archive name, entry,
extracted size, RIFF signature and decode result; it was removed after
verification. The native menu screenshot also renders correctly.

**Verification outcome.** `make -j4 all` passed without compiler warnings;
all 22 `test-warcraft-2` executables passed (the network menu tests require
local UDP socket permission). Warcraft II and Dark Colony dummy-audio sound
tests passed, as did both games' headless `--check`. The Warcraft native
menu BMP was rendered and visually inspected. Playback validation used the
SDL dummy audio driver; this does not constitute a listening comparison
against the original game.

### Engine audio ownership clarification (2026-10-07)

The playback system belongs to the engine's `sound/` directory. Dark Colony
is a client of it, as is Warcraft II; there is no cross-game implementation
dependency. Warcraft's native loader now passes WAV bytes to `S_LoadSound`
instead of calling `I_LoadSampleMemory` and assigning a mixer sample pointer.
The engine validates the sfx slot and owns decoding, caching and cleanup.
This follows the `S_`/`I_` split in the local Doom reference. The sound
behavior and native assignments above are unchanged.

Generic channel, bark, visibility, attenuation, invalid-WAV and lifecycle
tests now live in `tests/shared/test_sound.c`, using a synthetic WAV with
no retail assets. They run for all five game builds through Makefile source
discovery. Native Dark Colony tests retain SOUND2/SLIST/GAMESTAT/AMB evidence;
Warcraft tests retain archive playback, event mappings and partial-bank
failure recovery. Run `build/bin/tests/<game>/test_sound` with dummy SDL
video/audio drivers for the shared ownership checks.

**Ownership refactor verification:** all five game binaries build, the
shared synthetic-WAV suite passes under every game, all 23 Warcraft II and
68 Dark Colony regression executables pass, and both games' headless smoke
checks pass. No game source calls the I_ sound/sample API.
## Scenario picker controls and screenshot comparison (2026-10-07)

**Sources.** The user supplied `/Users/igor/Desktop/Screenshot 2026-10-07 at
12.00.30.jpg`, 1290x968, SHA-256
`1a2bd92f8884822c589b62995f55527b68d1b13bcd1e4dfeb4967e3784a0a7fb`.
Its logo identifies Battle.net Edition; the installed localized DOS data is a
different edition. WAR2.EXE retains fingerprint
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`;
it was not disassembled or run for this change. REZDAT.WAR SHA-256 is
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`;
STRDAT.WAR is `5ba75d38613852be7137c5ec4035578977eb75ce5adc37d219875ba308ce2c26`.
MAINDAT's fingerprint and the pinned Wargus provenance remain as recorded
below and in REFERENCES.md.

**Confirmed native records.** REZDAT's header at offsets 4/6 reports 91
entries, type 3000. Native GFUs 0/1 contain 50 widgets in this distribution.
Orc frame 46 is a 300x18 red list/pulldown bar; 45 is its disabled artwork.
Frames 29/30 are normal/pressed up arrows, 32/33 down arrows (19x20),
28/31 their disabled states, 40 the 17x17 knob, and 41/42 the disabled/normal
19x124 vertical track. No folder-up frames 50–52 exist in this GFU, although
the pinned Wargus catalog describes them for other distributions. REZDAT 12
is the 352x352 orc scenario panel. MAINDAT FONT 281 is 17x17, with M advance
14; FONT 282 is 14x14, with M advance 11. The front end previously used 282
as its large font. Gameplay HUD fonts are separate and unchanged.

**Confirmed scenario data.** MAINDAT entries 220–247 are 28 valid PUDs,
matching the pinned `wartool.h` skirmish range. Entry 220's DESC is
"Gold separates east from west", matching the map description behind the
reference dialog. Header DIM/OWNR supply dimensions and playing-slot count
(owners 4/5); extracted archive scenarios load through the ordinary PUD
path. The eight loose scenarios remain available under Custom scenario.

**Screenshot-derived layout, not executable-derived behavior.** Normalizing
the supplied image to 640x480 gives the centered 352-pixel panel at roughly
(144,50). The picker now uses that placement, type/size/directory selectors,
six 18-pixel list rows at (166,186), native arrows and knob, dimensions/player
count, Cancel on the left and OK on the right. Setup remains visible behind
the modal panel and cannot receive clicks or hotkeys. Directory names carry
a slash because no verified folder icon was found in these DOS widgets.
Directories are listed even under a size filter, so filtering cannot trap
navigation. Built-in maps and custom directories are distinct functional
sources; empty directories disable OK. These are implemented engine behavior,
not a claim about the retail executable's dispatch or sorting.

The shared engine now owns dropdown popup drawing, hit testing, pending
selection, keyboard/wheel input, Escape/outside-click cancellation, native
list-row pictures, authored disabled states, fixed-size scrollbar knobs,
Home/End/Page navigation, and double-click list activation. A dropdown draws
over the complete screen, clips its label before the arrow, and consumes
outside clicks. Menu transitions discard popup/pressed input. This keeps
games as tables and action routines, consistent with the existing Doom menu
lifecycle; `reference/DOOM/m_menu.c` was consulted for selection, Enter,
Escape, and menu transitions, not Warcraft widget layout.

**Disproven assumptions.** Native chrome alone did not make the old generic
list faithful: its brown selection fill and bare rectangular scrollbar
replaced artwork already present in REZDAT. Using the game font as the menu
font also reduced text size. The localized multiplayer test failure seen
during the initial run was caused by denied UDP socket creation in the
sandbox, not a missing STRDAT label; it passes with local sockets allowed.

**Unknown / incomplete fidelity.** No Battle.net MPQ or executable exists
in the inspected data tree. No verified native scene-layout record was
identified or imported. This change loads native graphics/fonts and PUDs at
runtime but authors the control layout in C. REZDAT records beyond the
identified graphics were not comprehensively classified. English Battle.net
logo, localization, folder artwork, cursor placement, exact RGB/geometry,
race/opponent setup controls, and retail dropdown/scroll dispatch still need
the corresponding edition's files and further verification. The parent
setup is the current engine setup, not a pixel-exact Battle.net scene. Do not
replace these unknowns with screenshot-generated assets or tuned constants.

**Verification.** `make` builds all five binaries without new warnings.
All 24 `test-warcraft-2` executables pass with SDL's dummy driver and local
UDP permitted. Shared menu tests also pass for the other four games.
`test_menu` compares 4,320 unlettered list pixels against decoded GFU 46,
including the selected row, and checks native font dimensions, 128x128
filtering, empty DATA-directory browsing, all 28 built-in maps, archive-map
selection, and normal launch/campaign behavior. Shared tests cover popup
overlap, cancel-without-commit, outside-click consumption, disabled input/art,
upward opening, and keyboard/double-click list activation. The picker also
renders and accepts popup input at 1280x960 (UI scale two); multiplayer still
lists only the original eight loose PUDs, independent of picker filters.
Headless check and
gameplay BMP generation pass. Picker, open size popup, built-in list and
empty-folder BMPs were rendered and visually inspected. These checks certify
native asset rendering and engine controls, not whole-screen equivalence to
the supplied JPEG.

Reproduce:

```sh
make
env SDL_VIDEODRIVER=dummy make test-warcraft-2
env SDL_VIDEODRIVER=dummy W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu
env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
```

## Single-player campaign entry correction (2026-10-07)

**Source and correction.** The user's second screenshot,
`Screenshot 2026-10-07 at 13.10.15.jpg`, SHA-256
`cab6de19f435ec47d0e906f0ffc25bbf6cefb31ebb32df457ac1d644e76bbb6f`,
shows our Orc campaign list, not a retail reference. Its native panel and
font did not establish native behavior: `open_pick(PICK_ORC)` authored a
14-mission browser with a generic brown highlight and rectangular scrollbar.
The previous scenario-picker change did not correct that screen. The
comment claiming other front-end screens followed Wargus was particularly
misleading for retail fidelity. Wargus's pinned `RunCampaignSubmenu` is its
own campaign/unlock selector and cannot establish the retail entry flow.

**Confirmed published behavior.** Blizzard's Battle.net Edition manual,
printed page 6 (PDF page index 5, URL recorded in REFERENCES.md), specifies
Single Player Game → New Campaign → race selection → assignment briefing →
first mission. It does not direct the player to a campaign mission browser.
This establishes the target flow for the Battle.net Edition reference;
exact DOS/Battle.net screen differences and button positions remain unknown.

**Confirmed native text/data.** The same WAR2.EXE/STRDAT/REZDAT/MAINDAT
fingerprints recorded above apply. Temporary `OPEN_RTS_DEBUG_W2_SINGLE`
logging printed decoded dialog labels. STRDAT entry 6 string 0 is the
internal name `NewCampaign`; strings 1/2/3 are the localized Orc Campaign,
Human Campaign and Previous Menu labels. STRDAT 27/3 is the load-screen
label; 27/1 is only its Load action, which the previous single-player screen
incorrectly reused. STRDAT 9/4 supplies the localized custom-scenario label.
No visible New Campaign label was identified in these dialog records.
The engine uses the manual's English label as an explicit fallback and
retains native localized labels for the other controls. It does not display
the internal `NewCampaign` identifier as a caption or invent a translation.

Static investigation also found STRDAT entry 1 to be an 8,828-byte general
string table with native u16 count 428, including unit names and orders.
The current loader's 96-string limit rejects it; temporarily raising that
limit decoded it but did not reveal the missing New Campaign caption.
That temporary change and all diagnostic logging were removed. Supporting
that general table is separate work; its exclusion is not evidence of a
missing dialog scene. STRDAT entries 64/65 contain the two first-mission
briefing texts, and the pinned wartool catalog corroborates alternating
human/orc briefing records. The existing 160-byte `w2_text_t` would truncate
long briefings. No briefing layout or narration dispatch was verified.

**Runtime investigation boundary.** Before the user instructed us not to
run retail, DOSBox was launched against a disposable copy of the installed
DOS data. Only the animated Blizzard introduction was observed; no retail
menu was reached or captured. Both retail emulator processes were confirmed
absent after the user's instruction. No further retail execution, CD-key
entry, or licensing workaround was performed. Introduction observations
supply no evidence about single-player layout. Subsequent verification runs
only open-rts and its own headless tests.

**Implementation.** Single Player now has New Campaign, Load Game, Custom
Scenario and Previous Menu. New Campaign opens only the three native race
choice/back buttons. Selecting either race extracts its first campaign PUD
through the existing loader and records campaign level 1. The campaign
browser, pick-mode enum and campaign-specific scenario-list branches were
removed. The true scenario picker retains its native dropdown/list controls.
Menus use the existing engine-owned Doom-style menu transitions and escape
callbacks; no per-game responder or drawer was introduced.

**Unknown / incomplete fidelity.** These screens reuse the existing native
REZDAT title and full-button column geometry; their final placement/order,
English fallback localization and complete Battle.net artwork were not
verified against retail pixels. Native scene records are still not imported.
Race selection currently starts mission one directly: the manual's briefing
and campaign chapter screens/narration remain unimplemented. This is a
correction of the known wrong browser and entry structure, not certification
of full retail equivalence. Do not replace the missing briefing with another
invented screen or present the open-rts screenshot as retail proof.

**Verification.** `make` completes without new warnings; all 24 Warcraft II
regression executables pass with `SDL_VIDEODRIVER=dummy` and local UDP
permitted. The focused menu regression asserts four single-player controls,
three race-selection buttons, no list/scrollbar on either screen, both back
paths, mission-one extraction for both races with native 32x32 dimensions
and player side, and subsequent human mission-two progression. Existing
scenario controls, save/load, results and shared widget tests still pass.
Open-rts headless `--check` passes. The two corrected front-end screens were
rendered as BMPs and inspected; this verifies engine output, not pixel
identity to retail. Reproduce with `make`, `env SDL_VIDEODRIVER=dummy make
test-warcraft-2` (allow local UDP), and `env SDL_VIDEODRIVER=dummy
W2_MENU_SHOTS=/private/tmp build/bin/tests/warcraft-2/test_menu`.
