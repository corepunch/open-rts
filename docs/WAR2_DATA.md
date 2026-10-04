# Warcraft II data: PUD, MAINDAT, and the first playable slice

This records what `games/warcraft2` loads from retail `data/WAR2`, which
references were used, and which choices are engine presentation rather than
retail behavior. Nothing here was traced in `WAR2.EXE`. Wargus (GPL-2) and
war2tools (MIT) were read for layout only; their source is not copied.
Warcraft 2000 was consulted and not adopted.

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
Megas 0–15 are raw 32×32 copies of the minitile blob. Later megas are 16
`u16` minitile refs: pixel offset `(ref & 0xFFFC) * 16`, bit 1 flips X, bit 0
flips Y. An `ALAMO` cell in MTXM `0x50..0x5f` does not match a cell in
`0x70..0x7f`. The screenshot shows distinct summer grass, pines, rock, and dirt.

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
footprints set both.

**Units.** A `UNIT` record is `u16 x, y; u8 type, player; u16 data`. The
spawned actor id is `type + 1` because `P_InitMobj` rejects type 0. Starts
(types 94 and 95) and walls (103 and 104) are not spawned. `ALAMO` therefore
spawns 64 of 71 records. Placement is the footprint center. Structures mark
that footprint solid. `data * 2500` on gold mines and oil patches is kept on
the PUD record and not applied.

`ALAMO` resources for the first two slots: gold 3000/3000, lumber 1000/1400,
oil 1000/2000. Single player shows the `OWNR` 5 slot. On `ALAMO` that is
slot 1: gold 3000, lumber 1400, oil 2000. Food is living mobile units over
supply: a farm or pig farm adds 4, a hall adds 1. Score stays 0.

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
frame 0. The ground point is the bottom center of the max box.

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
(the first `OWNR` 5 slot) when this is not a net game. Allegiance, sight, and
selection then use that slot. Critters are not selectable. With none of your
units selected, one click can inspect any other unit, including a gold mine.
A drag still selects only your units.

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
pixel's index 0..7. Index 0 is transparent. Indices 1..7 are a gray ramp
`80 + i * 25`. The cell is `width` by max height with the ink at `y = yoff`.
The x offset is not applied. A space with offset 0 is an empty cell about
half the max width wide.

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
| Info plate | 354 / 355, GFU frame 0 | 176×176 |

The info plate is composited into the max box. Untouched pixels stay index 0,
and that index stays opaque on chrome. Icons are GRP entry 356 (forest,
palette 2), 357 (winter, palette 18), or 358 (wasteland and swamp, palette
10): 186 frames, max 46×38. Era 1 uses palette 18, era 2 or 3 uses palette
10, otherwise palette 2. Command icons above 185 are not in this archive.

**Menus.** REZDAT entry 14 is the widget palette. Entries 0 and 1 are the
human and orc widget GFUs: 50 frames, max 300×164. Small buttons are frames
10 and 11 (106×28). Large buttons are frames 16 and 17 (224×28). Panel images
are entries 3 and 4, 256×288. The title backdrop is entry 13, 640×480. The
title puts Start and Quit on the left. In a level, F10 or Escape opens the
game menu on panel 1 at x 0, y 96, with the buttons from Wargus
`scripts/menus/game.lua` measured against that panel. The world palette stays
in level; REZDAT pixels are matched into it.

## Inferred

Hero and upgrade GRP numbers that share a line unit's forest entry (paladin
with knight, ranger with archer, and the rest of the catalog in `w_units.c`)
follow the war2tools sprite table. They were not each extracted and compared.
Submarine entries `{43,0,182,526}` and `{44,0,183,527}` use the same table;
a later-era 0 falls back to the forest entry.

Daemon is catalogued as land. Wargus `units.lua` sets `Type = "fly"` for it.
The retail selection path is unknown, so the catalog does not give it
`MF_FLY`.

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

- A placed building appears at once and marks its footprint. Walls show
  their icon and are not spawned.
- Patrol, stand ground, and repair set a status line and do not issue an
  order. Save, load, and options are not stored.
- The advanced build page is shown only when the player owns a lumber mill
  (PUD 76 or 77) or a keep, stronghold, castle, or fortress (PUD 88–91).
- Training spends gold through the engine queue and lumber through the
  button. Building costs are the Wargus `units.lua` numbers. Score stays 0.

## Unknown

- How `WAR2.EXE` converts a unit's speed byte into movement per tick. The
  engine divides the Wargus speed by 8 (`W2_SPEED_DIVISOR`), so a footman
  (speed 10) walks at 1.25 cells per second. Walk states last 4 tics
  (`W2_WALK_TICS`). Both are presentation choices.
- Fire-breeze (PUD type 35) has GRP entries `{0,0,0,0}`. No graphic is
  borrowed.
- Which GRP frame is construction versus damage. Frame 0 is the intact
  picture used for the standing state.
- Retail attack recovery and sight blocking. This slice sets no `MF_ATTACK`.
  Sight of 0 becomes 1, then sight is clamped to 10, so the oil patch and
  circle of power are visible. On `ALAMO` the camera starts on the human.
- Gold-mine and oil-patch amounts.
- Retail construction, patrol, stand ground, repair, and wall placement.
- Whether expansion swamp entries 438–441 differ from wasteland. This file
  does not contain them.

## Checks

`make test-warcraft2` is not part of `make test`, because CI has no
`data/WAR2`.

```
make test-warcraft2
# alamo land=6429 water=0 forest=2787
# alamo spawned=64 thinkers=64
# alamo sprites ok=1 cache=15
# channel land=3968 water=3609 era=wasteland

env SDL_VIDEODRIVER=dummy build/bin/warcraft2 --check
# Smoke check OK: 372 terrain tiles, 60 unit frames from footman, 0 resource vents.

env SDL_VIDEODRIVER=dummy build/bin/warcraft2 --screenshot /private/tmp/open-rts-warcraft2.bmp
```

`make test-warcraft2` also checks the human slot, the town hall's selectable
flag, and the UI entry sizes above (title 640×480, panels 256×288, icons 186).
The default binary loads `data/WAR2/ALAMO.PUD`.
