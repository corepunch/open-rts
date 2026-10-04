# Warcraft II gathering and HUD evidence (2026-10-04)

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
hover/click; no separately drawn state artwork is introduced here.

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
| Combat flags/actions and campaign AI remain absent | **Remaining P1:** displayed Attack is not a playable combat implementation. Warcraft has no skirmish AI features; combat/AI shared suites are not enabled for this game. |
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
image, menu-item, navigation, video and waypoint tests. Tests that require
Warcraft combat/AI or the engine fallback menu are excluded explicitly.
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
