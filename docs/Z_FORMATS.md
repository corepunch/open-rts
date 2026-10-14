# Z data formats

Z (The Bitmap Brothers, 1996) as installed under `data/Z`. Everything here
was read from the retail files and, where noted, checked against `ZED.EXE`.
Addresses such as 0x4f740 are `ZED.EXE` image addresses; see
`Z_EXE_FINDINGS.md` for how to read them. "Not decoded" means exactly that:
the bytes are there, their meaning is not yet known.

All numbers are little-endian. A cell is 16x16 pixels. Maps are at most
128x128 cells and every per-cell plane uses a row stride of 128.

## File inventory

| Path | What it is | Status |
|---|---|---|
| `ZED.EXE` | The game: DOS/4GW LE executable, 2.3 MB | Main reference |
| `LEVELS.DAT` | 51 level descriptors of 240 bytes | Decoded (below) |
| `LEVELnn/LEVELnn.MAP` | Map: cliffs, tile and attribute planes, path nodes | Decoded |
| `LEVELnn/CPUPLRnn.DAT` | Territories and computer player settings | Territories decoded |
| `LEVELnn/BUILDnn.DAT` | Building records | Position, type, hit points |
| `LEVELnn/OBJECTnn.DAT` | Placed objects | Position and type |
| `LEVELnn/BRIDGEnn.DAT` | Bridges, 312 bytes, loaded to 0x10f3d4 | Not decoded |
| `SPRITES.RSC` | 7207 sprites and a palette | Decoded |
| `WARDATA.PAC` | 147 members: tilesets, palettes, screens | Decoded |
| `DOOR.PAC` | Fort door animations, `xDOOR.DAT` + `xDBACK.LBM` for five worlds | Archive decoded, members not |
| `HEADFX.PAC`, `SHEADFX.PAC` | Panel and talking-head graphics (`HEADBITn.DAT`, `NEWPANEL.DAT`; the `S` set is about four times larger, purpose not confirmed) | Archive decoded, members not |
| `ROBOTS.DAT` | 25000 bytes; robot group roster with names (`Grunt`, `Tough`, `Sniper`, `Psychos`...) | Not decoded |
| `MULT.DAT` | 628 bytes, multiplayer settings, loaded to 0x152b10 | Not decoded |
| `PHRASES.BIN` | 35328 bytes, also a `WARDATA.PAC` member | Not decoded |
| `LOADER.LBM` | Loading screen, IFF PBM | Standard format |
| `AUDIO/*.RAW` | 210 sound effects, headerless unsigned 8-bit mono PCM | Sample rate not confirmed |
| `AUDIO/*.XMI` | 70 music tracks, Miles XMIDI | Standard format |
| `AUDIO/xFX.LST` | Sound tables per world (A, C, D, J, V) and shared (S): text, `num name priority [stereo loop freq volume]` | Readable text |
| `AUDIO/1024.GUS` | Gravis Ultrasound patch bank | Standard format |
| `SAVEn.Z` | Save slots, 43 bytes when empty (`0xff`, "Empty Slot") | Not decoded |
| `STATS/STATnn.TXT` | Plain-text level statistics the game writes | Text |
| `HISCORE/HISC.DAT` | High scores | Not decoded |
| `OPTIONS.CFG` | Options and modem strings (`ATH`, `ATE0`) | Not decoded |
| `*.MDI`, `*.DIG`, `*.INI`, `SETSOUND.EXE`, `UVCONFIG.EXE` | Miles sound drivers and setup | Not game data |

## PAC archives

`WARDATA.PAC`, `DOOR.PAC`, `HEADFX.PAC` and `SHEADFX.PAC` share one layout.
The directory ends the file: `count` entries of 24 bytes (char name[16],
u32 offset, u32 unpacked length), then u32 `count` and the magic `"JMP2"`.
Member data starts at its offset; the first four bytes of the file are not
part of any member.

Every member is LZSS compressed (`Z_Unpack` in `games/z/w_pac.c`):

- A flag byte governs the next eight items, from bit 7 down.
- A set bit is one literal byte.
- A clear bit is a u16 pair: the top 12 bits are a position in a 4096-byte
  ring, the low 4 bits are the length minus two (2..17 bytes).
- The ring starts zeroed and is written from position 1, one byte per
  output byte.

Decoding stops at the unpacked length from the directory; a member can end
with a few spare bytes.

### WARDATA.PAC members

| Members | Contents |
|---|---|
| `DESERT`, `ARCTIC`, `CITY`, `VOLCAN`, `JUNGLE` `.PAL` | 256 VGA colours, 6-bit RGB triples |
| same names `.BLK` | 512 tiles of 16x16 pixels, 256 bytes each, row-major (131072 bytes) |
| `ARTIC`, `CITY`, `DESERT`, `VOLCAN`, `JUNGLE` `.LBM` and `...2.LBM` | 320x200 tile source sheets (the BLK tiles drawn out, 20 tiles a row) |
| `MANE`, `MULTI`, `HSCORE`, `NET`, `MODEM`, `SMODEM`, `DIRECT` `.LBM` | Front-end screens |
| `[DACVJ]M*STAT*`, `*LOSE*`, `*LOAD*`, `[DACVJ]STAT1`, `[DACVJ]LOSE[1-4]` `.LBM` | Per-world briefing, loading, win and lose screens; B, G, Y variants by team colour |
| `HSHWINGn.DAT`, `VSHWINGn.DAT` | 1280 bytes each, not decoded |
| `CHARS.BIN`, `SQROOT.BIN`, `SINCOS.DAT`, `PHRASES.BIN` | Font and lookup tables, not decoded |

The LBM files are IFF `FORM PBM ` chunky bitmaps with ByteRun1 compression
(`BMHD` compression 1) and their own `CMAP`.

## LEVELS.DAT

51 entries of 240 bytes; ZED.EXE loads it to 0xe2394 and copies the names
of the current entry with 0x4f740. Entry 0 is a test level.

| Offset | Contents |
|---|---|
| 0x00 | Level name, e.g. "Virgin Soldiers" |
| 0x14 | Map file (`level01.map`) |
| 0x21 | Robot roster (`robots.dat`) |
| 0x2e | Territory / computer player file (`cpuplr01.dat`) |
| 0x3b, 0x48 | Wall preset names (`preset2.wal`, `preset1.wal`); no such files ship |
| 0x55 | Up to twelve starting robot group indices, 0xff ends (copied by 0x56dd7) |
| 0x61 | Objects file (`object01.dat`) |
| 0x6e | World: 0 desert, 1 arctic, 2 city, 3 volcano, 4 jungle |
| 0x6f | Multiplayer file (`mult.dat`) |
| 0x7c, 0x80 | Two u32 values, 10000 in the retail levels; not decoded |
| 0x84 | Buildings file (`build01.dat`) |
| 0x91 | Bridges file (`bridge01.dat`) |

Names are lower case; the files on disk are upper case.

## LEVELnn.MAP

Every retail map is version 0xf7 and 56433 bytes. ZED.EXE reads it at
0x73fe6, and other versions skip or resize some blocks:

| Offset | Size | Contents |
|---|---|---|
| 0x0000 | 1 | Version 0xf7 |
| 0x0001 | 0x1a00 | Old path node block, replaced by the one at 0xa971 |
| 0x1a01 | 0xac8 | Twenty 138-byte destructible cliff blocks (0x1896ac) |
| 0x24c9 | 0x2a8 | Twenty 34-byte editor tile brushes (0xe09cc) |
| 0x2771 | 0x20 | Tileset LBM names; u16 width at +0x1c, u16 height at +0x1e |
| 0x2791 | 0x4000 | Tile plane, 128x128 bytes (0x106910) |
| 0x6791 | 0x4000 | Attribute plane, 128x128 bytes (0x10a910) |
| 0xa791 | 0x1e0 | Skipped by the loader |
| 0xa971 | 0x3300 | 96 path nodes of 136 bytes |

### Tile and attribute planes

Cell (x, y) is byte `y * 128 + x` of each plane; cells past the map's width
and height are filler (tile 0xef, attribute 0x80).

- The cell's BLK tile is the tile byte, plus 256 when attribute bit 7 is set.
- Attribute bits 0..6 are the terrain id, 0..66.
- Fort footprints are marked with tile 0xee of the second bank, and the fort's
  interior terrain carries its team: 0x14 + team.

### Terrain tables

Terrain ids index several 67-byte tables in ZED.EXE:

| Address | Use |
|---|---|
| 0x148f00 (five pointers) | Robot passability by team; entry 4 (0x116c00) is the static, neutral row. Nonzero blocks. |
| 0x148f14, 0x148f28, 0x148f3c, 0x148f50 | The same for other unit classes (vehicles and others, picked by unit type at 0x64bf6) |
| 0x11582d | Building placement: nonzero means nothing may be built (roads, cliffs, buildings) |
| 0x115764 | Ground a cliff's shadow may be drawn over |
| 0x1157a7, 0x11714b | Height classes compared when a unit climbs (0x594e0) |
| 0x115e32 (five pointers) | Per-world colours of debris and particles by terrain, four bytes each |

`games/z` uses the neutral robot row for `level.blocked`.

### Path nodes

Each 136-byte node has a rectangle in pixels (u16 x1, y1, x2, y2) and a
list of linked nodes ended by 0xff. The 96 bytes at +0x28 are a next-hop
table that 0x95e24 fills at run time (node to node routing).

### Destructible cliffs

The tile plane does not contain the destructible cliffs. ZED.EXE builds
them into both planes when a level starts (0x452f2, called from 0x136aa).
A cliff block is:

| Offset | Contents |
|---|---|
| 0x00 | u16 x, pixels |
| 0x02 | u16 y, pixels |
| 0x04 | Height in cells |
| 0x05 | Not decoded (copied into each cell's run-time record) |
| 0x06 | Run-time pointer |
| 0x0a | 128-byte bitmask: 32x32 cells from (x, y), row-major, most significant bit first |

For each set cell, in row order across the twenty blocks, 0x45352:

1. Draws a cliff top (0x456f4). An inside cell takes one of 16 inside tiles,
   chosen by the game's random number generator (0x70d37). A cell on an edge
   takes the tile for the sides that have no cliff next to them. Its terrain
   is the bank bit plus the height.
2. Shades the column right of the cliff face (0x454a7, 0x455ba), unless
   another block covers that cell (0x45db1) or the ground there cannot take
   a shadow.
3. Hangs a face of `height` rows below the top: tile + 20 for each row and
   tile + 40 for the last (20 tiles is one row of the LBM sheet), terrain
   0x12.

The tiles come from a per-world table of 95 words at 0x143738 (pointers at
0x143710), copied to 0xe797d by 0x458c2: tile in the low byte, bank bit 7 in
the high byte. `games/z/cliffs.inc` holds it and `games/z/w_cliff.c` ports
the routine. Single player starts the random generator from whatever state
the front end left; the port uses the network game's fixed seed 0x3039
(0x49c77). The seed only selects among equivalent inside tiles.

### Forts

Forts are not in BUILDnn.DAT. After loading the map, 0x4ec1c scans the tile
plane for tile 0xee with attribute bit 7 set. The first hit is the fort
drawn with its gate; the scan for the second resumes 0x680 cells later.
The fort's position is that cell's top-left pixel.

0x126d9 draws a fort from six SPRITES.RSC pieces. The base comes from the
per-world table at 0x117de2: 809, 2127, 2304, 2800, 2613.

| Pieces | Position | Contents |
|---|---|---|
| base, base + 1 | (0, 16), (120, 16) | Body, 96 rows |
| base + 2, base + 3 | (0, 0), (120, 0) | Top strip, 16 rows |
| base + 4, base + 5 | (0, 112), (120, 112) | Front with the gate, 80 rows |

The second fort uses base + 6 and base + 7 for the strip and base + 8 and
base + 9 (64 rows, no gate) for the front.

## CPUPLRnn.DAT

2217 bytes, loaded to 0x18cadc. The first part is 64 territories of 30
bytes; unused ones start with 0xffff.

| Offset | Contents |
|---|---|
| 0x00 | u16 x1, y1, x2, y2 in cells; x2 and y2 exclusive |
| 0x08 | Team: 0 red, 1 blue, 2 green, 3 yellow, 4 or more nobody |
| 0x12 | Flag cell x, y |
| 0x18 | Flags, not decoded |

0x9dc69 gives a building the team of the territory that contains its
top-left pixel. The rest of the file (computer player settings) is not
decoded.

## BUILDnn.DAT

35 records of 80 bytes, loaded to 0x18ad2c. They are the game's run-time
building structures, so several fields hold stale pointers from the editor.

| Offset | Contents |
|---|---|
| 0x00 | u16 x, y: top-left pixel |
| 0x04 | Type, 0xff for none |
| 0x1a, 0x1c | u16 hit points and maximum |
| 0x1e | Run-time pointer to the owning territory (set by 0x3ed17) |

Types index the 74-byte table at 0xe6120: name, then the footprint in cells
at +0x30, then handler pointers.

| Type | Name | Footprint | Sprites by world (desert, arctic, city, volcano, jungle) |
|---|---|---|---|
| 0 | Vehicle Repair Facility | 5x4 | 2125, 2962, 3095, 4024, 2823 |
| 1 | Tank Manufacturing Facility | 4x5 | 2089, 2960, 3093, 4022, 2821 |
| 2 | Psuedo Fort (sic) | 10x10 | Forts come from the map instead |
| 3 | Robot Hospital | 4x5 | Not found |
| 4 | Robot Manufacturing Facility | 4x5 | 2090, 2961, 3094, 4023, 2822 |
| 5, 6 | Undefined | | |
| 7 | ICBM Launcher | | Not found |
| 8 | Radar Station | 4x3 | 2126, 2963, 3096, 4025, 2824 |

The sprite tables are at 0x1426d0 (repair), 0x1426e4 (robot), 0x1426f8
(tank) and 0x14270c (radar): five (intact, damaged) u16 pairs each. A
building is drawn with its sprite's top-left at its position. No retail
level uses types 3 or 7.

## OBJECTnn.DAT

150 records of 10 bytes, loaded to 0xe5364; unused records start with 0xffff.

| Offset | Contents |
|---|---|
| 0x00 | u16 x, y: top-left pixel |
| 0x04 | Object type |
| 0x05 | Five bytes, not decoded (0x0d or 0xff in the first byte) |

Types index the 25-byte table at 0xe5974 (78 types, `games/z/objects.inc`):

| Offset | Contents |
|---|---|
| 0x00 | u16 sprite |
| 0x02 | Name, 15 characters |
| 0x12 | Flags (0x32 vehicles and guns, 0x0e scenery, 0x02 wrecks) |
| 0x14, 0x15 | Width and height, pixels |
| 0x16 | u16 destroyed sprite, 0xffff for none |

Types cover mines and crates (0..10), houses and volcanoes (11..20), guns
and vehicles with their wrecks (21..38, 58, 59, 63..66), per-world rocks,
cacti, trees and snow (39..57, 60..62), doors (67..70) and flames (71..77).

## SPRITES.RSC

| Offset | Contents |
|---|---|
| 0 | u16 count, 7207 |
| 2 | `count` u32 offsets to sprites |
| 2 + 4 * count | One byte per sprite, not decoded (values 0..43) |
| then | 768-byte palette, 6-bit RGB; it matches the desert palette in all but about 25 entries |

A sprite is:

| Offset | Contents |
|---|---|
| 0 | Height in rows |
| 1 | Width divided by eight |
| 2 | Per-row bytes (0, 2, 4 or 6; not decoded), padded so the header is a multiple of four bytes |
| header | Width x height pixels as four VGA planes |

Plane p holds every pixel whose `x & 3 == p`, row by row, and the planes
follow each other. Pixel value 0 is transparent. No sprite exceeds 120x127
pixels; larger pictures, such as forts, are drawn from several pieces.

Sprites are drawn with the current world palette, not the one in the file.
Team colour is the run 0x10..0x1f (red). 0x5fd39 and its neighbours point
that run at 0x20 (blue), 0x30 (green), 0x40 (yellow) or 0x50 (nobody's).
