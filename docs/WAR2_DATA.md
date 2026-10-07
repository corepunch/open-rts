# Warcraft II data: PUD, MAINDAT, and the first playable slice

This records what `games/warcraft-2` loads from retail `data/WAR2`, which
references were used, and which choices are engine presentation rather than
retail behavior. Nothing here was traced in `WAR2.EXE`. Wargus (GPL-2) and
war2tools (MIT) were read as format/behavior references; their source is not copied.
Warcraft 2000 was consulted and not adopted.
See [gathering, HUD evidence and the Dark Colony review](WAR2_EXE_FINDINGS.md)
for the 2026-10-04 implementation and its remaining fidelity gaps.

## Retail files

| Property | Value |
| --- | --- |
| `data/WAR2/DATA/MAINDAT.WAR` | 10,403,193 bytes, 22 May 1997, SHA-256 `791bae4480d564f017122a82c9481dabd952424151f2b5d20245793e654ad3bb` |
| `data/WAR2/WAR2.EXE` | 878,119 bytes, 22 May 1997, SHA-256 `a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671` |
| Loose maps | `ALAMO`, `CHANNEL`, `DEATH`, `DRAGON`, `ICEBRDGE`, `ISLANDS`, `LAND_SEA`, `MUTTON` |

`WAR2.EXE` is present and was not disassembled for this slice.

## Confirmed

**Archive.** `MAINDAT.WAR` begins with `u32` magic `0x19`, `u16` entry count
438, `u16` type 1000, then `u32` offsets. Each entry is `u32` length with the
flags in the top byte. Flag 0 is raw. Flag `0x20` is LZ77: a control byte,
eight operations, low bit first; a set bit stores a literal into the output
and a 4096-byte ring; a clear bit is a `u16` whose low 12 bits are the ring
index and whose high nibble plus 3 is the run length. Output stops at the
declared length, including mid-match. This archive has no entry 438 or above.

**Palette.** Entries 2 (forest), 18 (winter), and 10 (wasteland) are 768
bytes. Each channel is a 6-bit value stored expanded with `<< 2` (0..252).
Index 0 is the GRP transparent index. The tileset palette keeps index 0
opaque; sprite palettes clear it.

**Tiles.** A tileset is the tuple palette, mega, mini, map:

| Era | Palette | Mega | Mini | Map |
| --- | --- | --- | --- | --- |
| Forest | 2 | 3 | 4 | 5 |
| Winter | 18 | 19 | 20 | 21 |
| Wasteland | 10 | 11 | 12 | 13 |
| Swamp | 10 | 11 | 12 | 13 |

Swamp reuses wasteland because this `MAINDAT` has no entries 438–441. The map
table is `0x9E` groups of 16 `u16` megatile indices (`42` bytes each). Lookup
slot `(group << 4) | sub` selects that megatile when the index is in range.
Megas 0–15 are raw 32×32 copies of the minitile blob and are the shroud
masks, identical in forest, winter and wasteland. Index 0 is the hole.
Index 239 is black (`0,0,0` in every era palette) and forms the stipple:
tile 10 is the top-left corner, 11 top-right, 12 bottom-left, 13
bottom-right, 2 the top edge, 8 the bottom, 4 the left and 6 the right.
Tile 0 is entirely index 0. The corner bits that select them are
top-right=1, top-left=2, bottom-right=4, bottom-left=8, the same reduction
Stratagus uses for `TiledFogTable`. Explored ground keeps that black
stipple on even `x + y` instead of blending the terrain darker. Later megas are 16
`u16` minitile refs: pixel offset `(ref & 0xFFFC) * 16`, bit 1 flips X, bit 0
flips Y. An `ALAMO` cell in MTXM `0x50..0x5f` does not match a cell in
`0x70..0x7f`. The screenshot shows distinct summer grass, pines, rock, and dirt.

Harvested forests rebuild neighboring tree edges from the native mixed
groups at `0x700..0x7df`. Runtime slots `0x9e0..0x9e3` expose the removed,
top, middle and bottom single-tree megatiles (126, 121, 122, 123). Unsupported
fragments lose their lumber and blocking too. See “Forest borders after
harvesting” in [the findings](WAR2_EXE_FINDINGS.md) for the reference-derived
corner rules and regression commands.

**PUD.** Sections are `char tag[4]; uint32 length`. `TYPE` is `WAR2 MAP`.
Unknown sections are skipped. All eight loose maps are `VER` `0x11` (17).
The loader also accepts `0x13` and rejects any other version when `VER` is
present. Dimensions are 1..256 and `MTXM` must cover `width * height` cells.

| Map | Era | Size | Units | Human slot (`OWNR` 5) |
| --- | --- | --- | --- | --- |
| ALAMO | 0 forest | 96×96 | 71 | 1 |
| CHANNEL | 2 wasteland | 96×96 | 19 | 5 |
| DEATH | 2 wasteland | 64×64 | 199 | 0 |
| DRAGON | 2 wasteland | 128×128 | 62 | 5 |
| ICEBRDGE | 1 winter | 128×128 | 125 | 6 |
| ISLANDS | 1 winter | 96×96 | 23 | 0 |
| LAND_SEA | 1 winter | 96×96 | 59 | 0 |
| MUTTON | 0 forest | 32×32 | 600 | 0 (slots 0–3 are also human) |

`ALAMO` owners are `[3,5,4,4,4,4,4,4, 3,3,3,3,3,3,3,2]`. No loose map uses
era 3 or `MTXM` at or above `0x9E0`.

**Passability.** `SQM` bit `0x0080` is forest (terrain 2), else bit `0x0040`
is water (terrain 1), else land (terrain 0). `ALAMO` is land 6429, water 0,
forest 2787. `CHANNEL` is land 3968, water 3609. Forests are not
`cell_solid`, so air can cross them and `tile_flags` stays null, which means
forests do not block sight. Wall MTXM values set `cell_solid`: orc
`(v & 0xFFF0) == 0x00A0`, `(v & 0xFFF0) == 0x00C0`, or `(v & 0xFF00) == 0x0900`;
human `(v & 0x00F0) == 0x0090`, `(v & 0xFFF0) == 0x00B0`, or
`(v & 0xFF00) == 0x0800`. Water and forest also set `blocked[]`. Building
footprints set both. Felling a tree clears its forest collision and selects
native removed-tree megatile 126; neighboring forest edges are not recomputed.

**Units.** A `UNIT` record is `u16 x, y; u8 type, player; u16 data`. The
spawned actor id is `type + 1` because `P_InitMobj` rejects type 0. Starts
(types 94 and 95) and walls (103 and 104) are not spawned. `ALAMO` therefore
spawns 64 of 71 records. Placement is the footprint center. Structures mark
that footprint solid. Gold-mine reserves use `data * 2500`, checked against
the PUD records and Wargus's PUD reader. Oil patches retain their PUD data;
oil extraction is not implemented.

`ALAMO` resources for the first two slots: gold 3000/3000, lumber 1000/1400,
oil 1000/2000. Single player shows the `OWNR` 5 slot. On `ALAMO` that is
slot 1: gold 3000, lumber 1400, oil 2000. Food sums living owned units'
catalog demand and supply: a farm or pig farm adds 4, a hall adds 1.
The unsupported score counter was removed.

**Base unit catalog (reference-confirmed).** `info.h` names all 105 native
PUD slots; `info.c` owns the 100 defined types as authored `mobjinfo[]`
literals. Slots 34, 36, 37, 48 and 54 are reserved. Starts and walls retain
definitions but are skipped when spawning map things, leaving 96 object
types. The former `w_units.c` balance table was removed. HP and maximum
damage use Doom's `spawnhealth` and `damage`; `w2_stats_t` groups the
Warcraft-specific values: armor, basic/piercing/minimum damage, raw speed,
attack/reaction/sight ranges, dimensions, costs/time, repair, food, mana,
points/priority/annoyance, level, decay, transport capacity, movement domain,
target/storage/resource-provider masks, income, gathering, capabilities,
projectiles and spells. Native GRP numbers remain in this same definition.

Values follow the pinned Wargus unit definitions with the documented
Blizzard overrides. No Lua is loaded at runtime. Actor projections, HUD,
production, depot selection, gathering capacity/waits and income read this
table. Training time uses reference `Costs.time` in seconds, replacing the
previous gold-dependent duration. The engine's current speed/sight/zero-HP
projections do not alter the authored values. This imports base definitions,
not a complete simulation of combat, spells, repair, transport or oil.
See the full catalog audit in [the findings](WAR2_EXE_FINDINGS.md).

**GRP.** Header is `u16` count, max width, max height, then per frame at
`6 + i * 8`: `u8` x offset, y offset, width, height, and `u32` offset. The
high bit of the offset clears and the width grows by 256. Row zero is
`blob + offset`. Each row is a `u16` offset from that row table. RLE: control
`& 0x80` skips `n = control & 0x7F` pixels; `& 0x40` repeats the next byte
`n = control & 0x3F` times; otherwise it copies `n` bytes. `n == 0` fails the
sprite. Forest footman is entry 45: 60 frames, 72×72, eight rotations.

A mobile GRP whose frame count is a positive multiple of 5 is directional.
Five authored facings fill eight slots, counter-clockwise from north:
N, NW (flip), W (flip), SW (flip), S, SE, E, NE. West-side slots set
`RTS_FRAME_FLIP_X`. Other GRPs are one frame and one rotation. Buildings use
frame 0. The ground point is the center of the native max box, so the
canvas is centered on the tile footprint. The former bottom-center pivot
was incorrect; see the HUD/selection correction in `WAR2_EXE_FINDINGS.md`.

**Team color.** GRP pixels use the red ramp at palette indices 208, 209, 210,
211, brightest first. Their RGB is `(164,0,0)`, `(124,0,0)`, `(92,4,0)`,
`(68,4,0)` in forest, winter, and wasteland. Players 0–6 occupy
`208 + player * 4` for four shades. Yellow is indices 12–15 in all three
palettes. Indices 236–238 are a brown ramp and 239 is black, so yellow is not
the next slot after white. The remap is by index. Winter blue at 212 is
`(0,60,192)`, while forest blue at 212 is `(12,72,204)`; searching for one
RGB table misses shades. Wargus `DefinePlayerColorIndex(208, 4)` plus its
yellow triple in `scripts/stratagus.lua` describe the same split. The forest
screenshot of the human start (slot 1) shows a blue town-hall roof and blue
unit trim.

**Movement classes.** Land is class 1 and crosses terrain 0. Sea is class 2
and crosses terrain 1. Air is class 3 and crosses 0, 1, and 2. `cell_solid`
blocks every class, including air. Structures, including oil platforms, are
class 0 and use `blocked[]`.

**Who you play.** `G_DoLoadLevel` sets `consoleplayer` to the PUD view player
(the first `OWNR` 5 slot) when this is not a net game. Single-player
`D_CheckNetGame` keeps that slot. Replacing it with the lobby seat (always 0
in single player) left the window on an empty player: ALAMO then showed
3000/1000/1000 and 0/0 food, the human start stayed explored, and none of
its units were drawn. In a net game the lobby seats are 0..numplayers−1, which
are not the map's person slots. The load permutes each person slot onto a
seat, turns any person slot past that into a computer, and swaps paired unit
types when that seat's lobby race differs from the slot's side. Allegiance,
sight, and selection use that slot.
Critters are not selectable. With none of your units selected, one click can
inspect any other unit, including a gold mine. A drag still selects only
your units.

**Archive type.** `REZDAT.WAR` is the same record layout as `MAINDAT.WAR`
with `u16` type 3000 and 91 entries. The opener accepts type 1000 and type
3000.

**IMG.** `u16` width, `u16` height, then raw pixels. Accept
`size >= 4 + width * height`.

**GFU.** `u16` count, max width, max height, then 8-byte frames: `u8` x
offset, y offset, width, height, `u32` offset. The high bit of the offset
clears and the width grows by 256. Pixels are raw `width * height` at
`blob + offset`. A bad frame is skipped. Widget buttons store the tight
image and do not use the GFU offset as a displacement.

**FONT.** Entry 282. Skip `"FONT "` (5 bytes). Then count, max width, max
height. `count - 32` is the glyph count (239 becomes 207, ASCII 32 onward).
Each glyph is a `u32` offset from the start of the blob. The glyph is `u8`
width, height, x offset, y offset, then RLE. A control byte's high 5 bits
(`ctrl >> 3`) skip, wrapping at the glyph width; the low 3 bits are one
pixel's ink category 0..7; skipped pixels are transparent. Native ink
categories are retained as engine indices 1..8, separately from transparent
index zero. White/yellow color ramps follow the reference's native palette
indices. The cell is `xoff + width` by max height; ink uses both bearings.
Entry 283 supplies the smaller HP font. A space with offset 0 is an empty
cell about half the max width wide. Earlier grayscale/no-x-bearing decoding
was incorrect; `M` advances 11 pixels, not 10.

**In-game panel, 640×480, human entry then orc entry.** The column is on the
left. The map viewport is `{176, 16, 448, 448}`.

| Piece | Entries | Size |
| --- | --- | --- |
| Menu button | 293 / 294 | 176×24 |
| Minimap frame | 295 / 296 | 176×136 |
| Button panel | 297 / 298 | 176×144 |
| Resource bar | 287 / 288 | 448×16 |
| Status bar | 291 / 292 | 448×16 |
| Right filler | 289 / 290 | 16×480 |
| Info plate | 354 / 355, GFU frames 0–3 | 176×176 |

All four info frames already fill the native box. Frame 0 is plain stone;
frames 1 and 2 share the bordered panel; frame 3 adds a progress-bar frame.
The HUD uses 0 for no/group selection, 1 for a single selection, and 3
while training. Index 0 stays opaque on chrome. Icons are GRP entry 356 (forest,
palette 2), 357 (winter, palette 18), or 358 (wasteland and swamp, palette
10): 186 frames, max 46×38. Era 1 uses palette 18, era 2 or 3 uses palette
10, otherwise palette 2. Command icons above 185 are not in this archive.

**Menus.** REZDAT entry 14 is the widget palette. Entries 0 and 1 are the
human and orc widget GFUs: 50 frames, max 300×164. Thin buttons are frames
4 and 5 (128×20) for the HUD Menu button at (24,2). The HUD loads these
from REZDAT with palette 14 and remaps their colors into the world palette.
The game-menu small buttons are frames 10 and 11 (106×28). Large buttons
are frames 16 and 17 (224×28). Panel images
are entries 3 and 4, 256×288. The title backdrop is entry 13, 640×480. The
title puts Start and Quit on the left. In a level, F10 or Escape opens the
game menu on panel 1 at x 0, y 96, with the buttons from Wargus
`scripts/menus/game.lua` measured against that panel. The world palette stays
in level; REZDAT pixels are matched into it.

## Inferred

Hero and upgrade GRP numbers that share a line unit's forest entry (paladin
with knight, ranger with archer, and the rest of the catalog in `info.c`)
follow the war2tools sprite table. They were not each extracted and compared.
Submarine entries `{43,0,182,526}` and `{44,0,183,527}` use the same table;
a later-era 0 falls back to the forest entry.

Daemon now follows Wargus's explicit `Type = "fly"` and `AirUnit = true`.
The earlier land classification had no evidence; it is superseded. This
reference-derived classification enables `MF_FLY`; retail dispatch remains
untraced.

Critter `BasicDamage` 80 in that lua file is not used. Critters render and
move, are not selectable, and store damage 0.

## Disproven

war2tools `libwar2/sprites.c` lists one 8-bit RGB ramp per player, darkest
first. Its blue `(0,4,76)`, `(0,20,108)`, `(0,36,148)`, `(0,60,192)` matches
the winter palette, not forest. Using it as a universal exact-RGB search
recolored only the darkest forest shade, so the human base stayed red.
Violet and orange in that table also miss the forest slots. The index remap
above replaces it.

Warcraft 2000 (`reference/warcraft2000`, commit `4d12ad3`) does not read PUD
or `MAINDAT.WAR`. `Build.cpp` places buildings through `tmap`, `OILMAP`, and
`LLock`. Its map format is not used.

Hard-coded summer.lua tile ranges are not the passability system. `SQM` bits
are. `MTXM` selects the graphic.

Mega index 0 is a real tile (the fog tile), not a missing graphic.

## Presentation

These are engine choices, not traced `WAR2.EXE` behavior.

- Construction follows Wargus `constructions.lua` and the Stratagus build
  order: the worker walks to a free cell beside the footprint, pays the
  `mobjinfo[]` price on arrival, and steps inside (hidden, unselectable,
  not a target). The site shows MAINDAT entry 252 (land construction site,
  64x64) frame 0 to 25%, frame 1 to 50%, then the type's own second GRP
  frame. Hit points rise from one to full over `costs.time` seconds, with
  the retail `% Complete` panel and a Cancel button that refunds the whole
  price and lets the builder out. The cursor cell is the footprint's
  top-left. Footprints need plain land with nothing standing on them; shore
  buildings need water beside them; halls keep three clear cells from a
  gold mine (Wargus `BuildingRules` distance). Walls show their icon and are
  not built. Oil platforms, hall upgrades and map furniture are not placed.
- A destroyed structure leaves the era's destroyed-site sheet (MAINDAT 121
  forest, 163 winter, 191 wasteland; 189/190/188 for one-cell footprints;
  frames 2 and 3 over water) for two frames of 200 tics, as Wargus
  `animations-destroyed-place`, while the ground clears at once.
- The computer player is the shared engine AI with a Warcraft interface:
  computer and player PUD slots other than the console player's, a goal
  ladder after Wargus `ai/land_attack.lua` (workers, farms, barracks,
  soldiers, mill, smithy, first research, tower, keep, stables, knights),
  sites found by spiral search around the hall with a free ring of cells,
  and workers sent gold-first with every third on lumber to the deposit
  nearest their depot. Workers never join defense rallies or attack waves.
- Patrol, stand ground, and repair set a status line and do not issue an
  order. Save, load, and options are not stored.
- The advanced build page is shown only when the player owns a lumber mill
  (PUD 76 or 77) or a keep, stronghold, castle, or fortress (PUD 88–91).
- Training spends gold and lumber atomically when its tic command executes.
  Building placement and cancellation are tic commands too (`TC_CONSTRUCT`),
  so they replay in lock step. Costs follow the authored `mobjinfo[]` catalog.
- Sight uses the Stratagus simple-radial circle: a cell at `(dx, dy)` is
  seen when `dx² + dy² < (radius + 1)²` (`ProceedSimpleRadial` in
  `reference/stratagus/src/map/fov.cpp`). The Dark Colony ray table stops
  at `radius²` and keeps only `(0, ±radius)` and `(±radius, 0)` on the
  axes. Each of those cells has every shroud corner hidden, so
  `TiledFogTable[15]` selects megatile 0, the empty mask, and the cell
  stays fully lit: a square in the black or in the explored stipple, and
  a one-cell cap on the circle. Forests still do not block sight.
- Harvest uses ordinary mobj thinkers, native axe/carrier graphics, finite
  deposits, owned reachable depots, repeated trips and a Return Goods command.
  Lumber progress is private to each worker and finishes on chop 51, as
  documented by Blizzard. Delay values are reference-derived, not EXE-traced.

## Unknown

- How `WAR2.EXE` converts a unit's speed byte into movement per tick. The
  engine divides the Wargus speed by 8 (`W2_SPEED_DIVISOR`), so a footman
  (speed 10) walks at 1.25 cells per second. Walk states last 4 tics
  (`W2_WALK_TICS`). Both are presentation choices.
- Deathwing (archive name fire-breeze, PUD type 35) has GRP entries `{0,0,0,0}`. No graphic is
  borrowed.
- Whether retail draws a damage frame. Frame 0 is the intact picture and
  frame 1 the half-built one (Wargus `main` frame 1 at 50%); every
  buildable structure's GRP holds exactly those two.
- Retail attack recovery and sight blocking. This slice sets no `MF_ATTACK`.
  Sight of 0 becomes 1, then sight is clamped to 10, so the oil patch and
  circle of power are visible. On `ALAMO` the camera starts on the human.
- Retail oil extraction and exact bonus/depletion accounting.
- Exact minimap rendering, command visual states and complete HUD pixel fidelity.
- Retail patrol, stand ground, repair, and wall placement. Retail
  construction timing is taken from the Wargus `time` cost in seconds; the
  builder's entry and exit positions are engine choices.
- Whether expansion swamp entries 438–441 differ from wasteland. This file
  does not contain them.

## Checks

`make test-warcraft-2` runs as part of `make test`; `data/WAR2` is tracked
like the other games' data.

```
env SDL_VIDEODRIVER=dummy make test-warcraft-2
# alamo land=6429 water=0 forest=2787
# alamo spawned=64 thinkers=64
# alamo sprites ok=1 cache=19
# channel land=3968 water=3609 era=wasteland

env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --check
# Includes 2,787 ALAMO trees; the PUD regression spawns and checks 11 mines.

env SDL_VIDEODRIVER=dummy build/bin/warcraft-2 --screenshot /private/tmp/open-rts-warcraft-2.bmp
```

`make test-warcraft-2` also checks the human slot after single-player net
setup, that the town hall is still visible to that slot, and the UI entry
sizes above (title 640×480, panels 256×288, icons 186).
`test_catalog` checks all 105 slots, spawns all 96 object definitions and
independently compares all 100 defined types to the pinned Wargus checkout
when `reference/wargus` is present. Only that optional comparison is skipped
without the ignored reference tree; roster checks still run.
The default binary loads `data/WAR2/ALAMO.PUD`.
