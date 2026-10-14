# Z data formats

Z (The Bitmap Brothers, 1996) under `data/Z`. Everything below was read from
the retail files and checked against `ZED.EXE` (a DOS/4GW LE executable).
Image addresses assume the LE objects at their preferred bases (code at
0x10000, data at 0xe0000) with fixups applied. Many functions mark
themselves with an inline name string (`pushad; jmp short over "name\0";
mov [0x67f87], name; popad`), which makes them easy to find.

## PAC archives (`WARDATA.PAC`, `DOOR.PAC`, `HEADFX.PAC`, `SHEADFX.PAC`)

The directory ends the file: `count` entries of 24 bytes (name[16], u32
offset, u32 unpacked length), then u32 `count` and `"JMP2"`.

Members are LZSS (`Z_Unpack`): a flag byte, read from bit 7 down; a set bit
is a literal byte, a clear bit a little-endian u16 whose top 12 bits are a
position in a 4096-byte ring and low 4 bits the length less two. The ring
starts zeroed and is written from position 1.

`WARDATA.PAC` holds per-world `X.PAL` (256 6-bit VGA triples) and `X.BLK`
(512 tiles of 16x16, row-major), where X is DESERT, ARCTIC, CITY, VOLCAN or
JUNGLE, plus the LBM screens and the `X.LBM`/`X2.LBM` tile sources.

## LEVELS.DAT

240-byte entries, copied by 0x4f740: name at 0, then 13-byte file names at
+0x14 map, +0x21 robots, +0x2e CPU player, +0x3b/+0x48 wall presets, +0x61
objects, +0x6f multiplayer, +0x84 buildings, +0x91 bridges. +0x55 holds up
to twelve starting robot groups (0xff ends), +0x6e the world (0 desert,
1 arctic, 2 city, 3 volcano, 4 jungle).

## LEVELnn.MAP (version byte 0xf7, 56433 bytes)

Read at 0x73fe6:

| Offset | Size | Contents |
|---|---|---|
| 0 | 1 | Version 0xf7 |
| 0x0001 | 0x1a00 | Old zone block (superseded by the last block) |
| 0x1a01 | 0xac8 | Twenty 138-byte destructible cliff blocks |
| 0x24c9 | 0x2a8 | Twenty 34-byte editor tile brushes |
| 0x2771 | 0x20 | Tileset LBM names; u16 width at +0x1c, height at +0x1e |
| 0x2791 | 0x4000 | Tile plane, 128x128 bytes |
| 0x6791 | 0x4000 | Attribute plane, 128x128 bytes |
| 0xa791 | 0x1e0 | Skipped |
| 0xa971 | 0x3300 | 96 path nodes of 136 bytes; +0x28 is a next-hop table |

Cell (x, y) is at `y * 128 + x`. Its BLK tile is the tile byte plus 256 when
attribute bit 7 is set. The low seven attribute bits are the terrain id
(0..66). Robots' passability is the neutral row of the table at 0x148f00
(0x116c00): nonzero blocks. Each team has its own row, filled at run time.

Destructible cliffs are not in the tile plane: 0x452f2 builds them when a
level starts. A cliff block is u16 x, y (pixels), the height in cells at +4,
a run-time pointer at +6 and a 32x32 cell bitmask at +0xa (most significant
bit first). For each set cell 0x45352 picks a top tile from the per-world
95-word table at 0x143710 (edge tiles by the sides with no cliff next to
them, inside tiles by a random variant from 0x70d37), hangs a face of
`height` rows below it (tile + 20 each, the last + 40, terrain 0x12) and
shades the column to its right (0x454a7, 0x455ba). Cells another block
covers are left alone (0x45db1). `games/z/w_cliff.c` follows this; it seeds
the generator with the network game's 0x3039, as single player keeps
whatever the front end left.

Forts are not in BUILDnn.DAT. 0x4ec1c scans the tile plane for tile 0xee of
the second bank; the first hit is the fort drawn with its gate, and the
search for the second resumes 0x680 cells later. 0x126d9 draws a fort from
six SPRITES.RSC pieces (base from the per-world table at 0x117de2: 809,
2127, 2304, 2800, 2613): the body (base, base+1) at y+16, a 16-pixel strip
(base+2, base+3) at y and the front (base+4, base+5) at y+112, the right
pieces 120 pixels in. The second fort uses pieces +4 for strip and front.

## CPUPLRnn.DAT

64 territories of 30 bytes: u16 x1, y1, x2, y2 in cells (x2, y2 exclusive),
the team at +8 (0..3, 4 neutral), the flag cell at +0x12. 0x9dc69 gives a
building the team of the territory containing its top-left pixel.

## BUILDnn.DAT

35 records of 80 bytes, the run-time building structures: u16 x, y (top
left, pixels), type at +4 (0xff none), hit points at +0x1a. Types index the
74-byte table at 0xe6120 (name, footprint in cells at +0x30): 0 repair
facility, 1 tank factory, 2 fort, 3 robot hospital, 4 robot factory, 5 and
6 undefined, 7 ICBM launcher, 8 radar station. Their sprites come from
per-world tables at 0x1426d0 (repair), 0x1426e4 (robot), 0x1426f8 (tank)
and 0x14270c (radar), each five (intact, damaged) pairs.

## OBJECTnn.DAT

150 records of 10 bytes: u16 x, y (top left, pixels), the object type, then
five bytes not yet decoded. Types index the 25-byte table at 0xe5974: u16
sprite, name[15], flags at +0x12, width and height at +0x14, u16 destroyed
sprite at +0x16. `games/z/objects.inc` carries it.

## SPRITES.RSC

u16 count (7207), `count` u32 offsets, a byte per sprite (not yet decoded)
and a 768-byte palette. A sprite is its height, its width over eight, a row
table padded so the header is a multiple of four bytes, then the pixels as
four VGA planes: plane p holds every pixel with `x & 3 == p`, row by row.
Index 0 is transparent. Sprites use the world palette; team colour is the
run 0x10..0x1f, which 0x5fd39 points at 0x20 (blue), 0x30 (green), 0x40
(yellow) or 0x50 (nobody's).
