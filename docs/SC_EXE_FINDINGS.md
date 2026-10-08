# StarCraft native data and UI findings

Investigated 2026-10-08. This is the original StarCraft disc, not a Brood War
installation. No executable disassembly was used for the initial catalog.
Do not infer original executable behavior from the sandbox's engine behavior.

For the consolidated byte-level specification, lookup formulas, all 54 unit
columns, adapter limits and unresolved audit findings, read
[StarCraft classic format reference](SC_FORMATS.md). This file remains the
investigation journal and input fingerprint record. The initial implementation
sections describe commit `a23c42d`; later work should append dated findings
rather than silently treating these limits as permanent.

## Reproducible inputs

SHA-256 fingerprints:

| Local input | SHA-256 |
| --- | --- |
| `data/STARCRAFT/StarCraft.iso` | `4f7455c8ea55ca3b7d49209bc25cf9edb62f40a7e06015701159ae47cbd0033b` |
| `install/files/stardat.mpq` | `4a503190f79289d827bb7ea99f06b8b9f18a120e360f8442e10933939eafb525` |
| `install/files/starcraft.exe` | `16cd8f0d1098f8bcd0625d5e13964c2facf2df4874636acb01c2e0bfb062b2f5` |

`bsdtar` extracts the ISO unchanged to `disc/`. Its `INSTALL.EXE` contains an
MPQ. The C `sc_import` tool uses StormLib with Stargus's listfile, extended
with the installer `files\` prefix. Enumeration extracts 782 installer
members, then 2,897 members from `files/stardat.mpq`, with zero extraction
failures. Unknown member names are retained as supplied by StormLib.
Output paths are lowercase and separator-normalized, with traversal rejected.
Fonts are in `install/files/font/`; most runtime assets are in `native/`.
The extraction retains all entries, including sounds, maps, movies, EXEs,
and files the initial implementation does not yet consume.

```
make starcraft-unpack
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > /tmp/sc-units.inc
cmp /tmp/sc-units.inc games/starcraft/units.inc
make test-starcraft
```

## Confirmed: UI assets and layout are external

The MPQ contains `rez/glumain.bin`, `rez/gamemenu.bin`, `rez/statdata.bin`,
`rez/statbtnt.bin`, `rez/statport.bin`, `rez/minimap.bin`, `rez/stat_f10.bin`,
and many more dialog resources. They are not IScript bytecode despite sharing
`.bin`. The original classic dialog record is 86 bytes. PyMS's independent
format definition and the retail bytes agree:

| Byte offset | Value |
| --- | --- |
| 0 | next control, u32 file offset |
| 4, 6 | x, y, u16 relative to dialog origin |
| 12, 14 | width, height, u16 |
| 20 | NUL-terminated text/path, u32 file offset |
| 24 | flags, u32 |
| 32 | identifier, u16 |
| 34 | control type, u32 |
| 54..60 | responsive rectangle, four u16 values |
| 66 | first child for root; SMK descriptor for a control |
| 70, 72 | text origin offset, u16 |

Flags include visible 8, disabled 2, hotkey prefixes 0x100/0x200, font
selection and text alignment. Text uses embedded color controls. The loader
retains hotkeys and highlighted spans and strips nonprinting color bytes.
The initial shared-menu adapter uses the visual bounding rectangle for input;
precise responsive subrectangles are not yet applied.

A SMK descriptor is 30 bytes: next overlay u32 at 0, flags u16 at 4, path
u32 at 10, and x/y u16 at 18/20. Flag 4 repeats and flag 8 restricts an
overlay to hover. Normal and hover files are layered; replacing the whole
normal movie with its smaller hover overlay was an incorrect early approach.
The movies retain each frame's palette and native dimensions/timing.

`glumain.bin` has ten controls. Single Player is id 3 at (0,0), 320x190,
with text offset (150,111); its hover overlay is at (46,66). Campaign Editor
is id 5 at (375,104). Main menu background is `glue/palmm/backgnd.pcx`.
No invented logo or catalog buttons are inserted into this screen. Single
Player deliberately launches the catalog; unsupported actions explain the
sandbox limit through the engine message dialog.

Native HUD geometry, after adding parent offsets:

- minimap (6,348), 128x128;
- nine command slots, first (505,358), 36x34;
- portrait (413,410), 60x56;
- Menu button (416,388), 64x20;
- selection dialog origin (138,388), 270x92.

`game/tconsole.pcx` supplies the full console. `dlgs/terran.grp` contains
widget artwork, including authored pressed states; `dlgs/tile.grp` supplies
nine dialog frame tiles. Compressed and raw GRPs both occur. Duplicate raw
frame offsets contribute once to the payload size, matching Stargus's
raw-layout discriminator. Button three-part frame indices 115..117 and
118..120 are documented by PyMS. The game uses these through shared menu
items and draw callbacks, not a separate menu responder or application loop.

Stargus's `scripts/guichan.lua` manually places animated buttons at different
coordinates (for example Single Player at 100,50). `UIConsole.cpp` slices
PCX graphics; it is not a native BIN layout decoder. The initial assumption
that Stargus already reproduced all original BIN layouts was disproven.
The executable still supplies control actions and gameplay-dependent state;
external layout is not a complete UI implementation. Warcraft II in this repo
also reads external dialog records from REZDAT.WAR, with some strings/resources
in its EXE; the distinction is not simply “StarCraft files versus Warcraft EXE.”

## Confirmed: fonts and palette indirection

`FONT` header bytes 4..7 give low/high glyph, maximum width/height, followed
by u32 glyph offsets. Each glyph begins width,height,x,y bytes. Payload byte
high five bits skip pixels, low three select ink; ink zero is transparent.
Treating ink zero as opaque produced magenta fringes and was corrected.
`glue/palmm/tfont.pcx` and `game/tfontgam.pcx` supply the actual font ramps.

Player colors remap indices 8..15 through eight-pixel rows of `game/tunit.pcx`.
Command images use the first 16 pixels of `unit/cmdbtns/ticon.pcx` as their
palette, not the console's embedded palette. Full-health wireframes remap
192/193 from twire pixels 3/4, 208..211 from pixel 1, and 216..219 from pixels
0/1/18/10. These are the Stargus author's published mappings, verified against
our extracted ramp pixels. Damaged-section randomization is not implemented.

## Confirmed: unit image chain and terrain

Classic table sizes in this disc are units 19,192, flingy 2,760, sprites 2,081,
and images 28,690 bytes. All 228 units follow units.graphics -> flingy.sprite
-> sprites.image -> images.grp -> images.tbl path, prefixed with `unit/`.
Shared images alias one engine-owned sheet; all 228 unit entries are retained,
including heroes, subunits, unused slots, resources, doors and map objects.
The result has 148 unique GRPs and 7,667 original physical frames.

The first 772 bytes of classic sprites.dat are **386** u16 image indices.
Stargus's expanded-table formula yields 353 for this classic file and wrongly
rejects door sprite IDs 381..385. The verified classic size has an explicit
386-entry image span. No interpretation of the remaining classic sprite
columns is claimed by this loader.

GRPs have six-byte headers and eight-byte cropped-frame descriptors. Crops,
pivots, RLE skip/repeat/literal runs, and every physical frame are retained.
Directional sets store 17 facings north through south clockwise. Shared
renderer slots run counterclockwise, with the other half mirrored. This is
sprite lookup, not a world-coordinate flip. Native unit subunit1 references
and images.dat special-overlay LOL offsets compose tank/Goliath turrets.

IScript Init/StarEditInit and Walking visual paths compile to shared states:
frame changes, waits, unconditional jumps, calls/returns. Conditional branches
are skipped, not evaluated. Only the bounded visual path is supported, not
the complete original VM or spawned effects. The adapter assumes
24 Hz for wait conversion to the engine's 30 Hz; this was not established by
a retail timing trace. The current import logs next-free state index 1,322
(including the reserved zero slot in that span). Missing visual paths retain
the first frame.
Shadows, construction/damage overlays, some special image draw modes, combat,
and exact original randomized animation behavior remain unimplemented.

Badlands WPE supplies RGBx colors. CV5 groups have 52-byte records and their
16 megatile references begin at byte 20. VX4 contains sixteen u16 minitile
references per 32x32 tile, with bit zero indicating horizontal reflection;
VR4 stores 8x8 indexed pixels. The extracted tileset decodes 4,844 megatiles.

## Explicit sandbox behavior and verification

The authored 128x128 catalog places every unit slot at native Y-down pixel
coordinates `(192 + column*240, 192 + row*240)`, 16 columns. Dirt is CV5 group
2. All cells are explored and visible. This layout and the common mobile
speed of three grid cells/sec are sandbox choices, not reverse-engineered
retail behavior. HP, costs, dimensions, flags and portrait IDs are generated
once into committed C literals; runtime balance does not depend on DAT tables.

Build uses `RTS_WORLD_Y_UP=0`, exactly as Warcraft II. No shared coordinate or
framebuffer transformation was changed. Navigation, thinker movement,
selection and menu input all use shared engine facilities. Original native
portraits follow units.portrait -> portdata.dat idle -> portdata.tbl, with the
first authored numbered `.smk` variant. Random talking/idle variation is not
implemented. The HUD currently uses the Terran console for the catalog.

`make test-starcraft` verifies 228 actors, all image references, the classic
sprite chain, decoded frame counts, malformed GRP/BIN rejection, representative
north/east/south/west facing/mirror relationships, movement toward increasing
native X/Y, full catalog visibility, native menu/HUD rectangles and selected
portrait availability. It saves main-menu, Marine, building and pause BMPs
under `/private/tmp/`. The visual review also checks palette remapping.

This is a basic catalog sandbox. Economy, combat, missions, saves, multiplayer,
intro playback, full retail resource/selection states, and race switching are
not implemented. Imported files remain available for that subsequent work.

## Documentation audit (2026-10-08)

The detailed [format reference](SC_FORMATS.md) was checked against the pinned
schemas, current C readers and local data. Additional findings:

- **Confirmed locally:** unfiltered file counts remain 782 installer and 2,897
  native members. Ordinary `rg --files` reports only 771 installer files due
  to ignore rules; use `--hidden --no-ignore` for inventory counts.
- **Confirmed locally:** portdata.dat is 1,080 bytes, giving 90 classic portrait
  entries. Badlands has 1,665 CV5 groups, 4,844 VX4 megatiles and 26,247 VR4
  minitiles. The full 54-column units layout sums to 19,192 classic bytes;
  adding the three expanded byte columns gives the reference's 19,876 bytes.
- **Implementation limitation:** `race` in the imported unit struct is the
  original editor group bitfield and `orders` is the right-click action byte.
  These are not a race enum and complete order program respectively.
- **Disproven overstatement:** “branches supported” is too broad for IScript.
  Only unconditional jumps and calls/returns are followed; conditional jumps
  are skipped, and waitrand deterministically uses its first bound. The earlier
  summary is corrected above.
- **Known discrepancy, not repaired by this documentation change:** the current
  IScript operand-length table differs from both Stargus and PyMS for several
  instructions, including 0x3f, 0x41 and 0x42. Those instructions require 2, 1,
  and 4 operand bytes; the current table gives zero. Paths encountering them
  can decode subsequent bytes incorrectly. A full opcode audit and focused
  fixtures are needed before claiming general animation correctness.
- **Unknown retail behavior:** exact timing, callback semantics, virtual-key
  mapping, damaged wireframe randomization and hover movie restart behavior
  have not been traced in STARCRAFT.EXE. Existing data and visual tests cannot
  substitute for that evidence.

No runtime code was changed by this documentation audit.

## Native CHK terrain and placements (2026-10-08)

**Confirmed cause of the repeated terrain screenshot:** the initial default
`catalog` never read a map. It allocated 128x128 cells and assigned
`32 + (linear_index % 16)` everywhere. Temporary loader diagnostics reproduced
IDs 32, 33, 34 at the beginning of every row. CV5/VX4/VR4 pixel composition was
already correct; changing its stride, palette, or flip direction would have
been a compensating error. The catalog remains an explicit inspection mode.
Single Player and the default check now load Terran 01's extracted CHK.

No executable was disassembled for this change; executable/MPQ fingerprints
are above. Additional SHA-256 inputs:

| Input under `data/STARCRAFT/` | SHA-256 |
| --- | --- |
| `install/campaign/terran/terran01/staredit/scenario.chk` | `4b0e5009c4ae80cfb4cb13ede30842db0aa05bb3be2eedad695fb093fa5786f6` |
| `native/tileset/badlands.cv5` | `b417ddd4523461159fce5357462bb2c9fe5c6f4536b10e0ddd1bdc84c56ae3ee` |
| `native/tileset/badlands.vx4` | `270663f0c17589b61193bdd0fb8a3e9bdac0931f9e92ee3e76373ed3423bd9ec` |
| `native/tileset/badlands.vr4` | `e3e96586ad1dca3678f0d0a0132e7c454b5c701e3dfd2299c93fcc45f036aaa7` |
| `native/tileset/badlands.vf4` | `7bfa246f2bc9e8d9cc1323c968fb0a74c9ab7c7abe41daf110ad82fe05090d89` |
| `native/tileset/badlands.wpe` | `37736f2f738384e0668d7ff8bd658e540d02138e00f660d59b75fb5c5cb5a3ea` |

**Confirmed format chain:** Stargus `src/Chk.cpp` reads `DIM ` as two u16s,
`ERA ` as the tileset selector, and `MTXM` as width*height row-major u16 CV5
indices. Its `TilesetHub.cpp`, `MegaTile.cpp` and Kaitai terrain schemas agree
with the local data. For MTXM value t, read CV5 at
`(t >> 4)*52 + 20 + (t & 15)*2` to obtain the VX4/VF4 megatile index. Both
VX4 and VF4 have 32-byte records; VF4 bit 0 marks each 8x8 minitile walkable.
Do not substitute the editor's `ISOM` or `TILE` chunk for runtime `MTXM`.

PyMS `CHKSectionERA.py` corroborates the low-three-bit tileset selector and
uses native basename `ice` for ERA 6; Stargus's CHK converter calls that era
`arctic`. This disc only supplies eras 0..4, so expansion terrain remains
unverified. The loader selects all terrain resources and the WPE palette from
ERA rather than hardcoding Badlands.

`UNIT` records are 36 bytes: pixel x/y at +4/+6, unit ID +8, valid property
bits +14, owner +16, HP percentage +17. IDs remain native and convert to
engine type ID +1 only when spawning. ID 214 is a start location, not an
actor. OWNR value 6 chooses the local human player. `THG2` records are ten
bytes: ID +0, pixel x/y +2/+4, owner +6, flags +8. PyMS's `CHKSectionTHG2.py`
distinguishes sprite records (flag 0x1000, ID indexes sprites.dat) from unit
records (ID indexes units.dat). Non-sprite THG2 records therefore spawn
ordinary actors; sprite records use the existing level decoration renderer.
GRP crop pivots minus pixel remainders preserve placement within a 32px cell.
Native records are kept in one level-owned blob, released by P_FreeLevel.

Terran 01: 64x64 Badlands, MTXM payload at CHK offset 1158, 8,192 bytes,
1,134 distinct tile references, 49 UNIT records including three starts,
46 actors, and 20 sprite doodads. The human is owner 1, starting at pixel
(128,176), grid (4,5.5). All coordinates stay top-left/Y-down.

Focused native-data verification:

| Map | Dimensions | Tileset | Distinct MTXM IDs | Actors including THG2 | Sprite doodads |
| --- | --- | --- | --- | --- | --- |
| Terran 01 | 64x64 | badlands | 1134 | 46 | 20 |
| Terran 04 | 128x128 | install | 1040 | 120 | 10 |
| Terran 05 | 96x96 | badlands | 1498 | 168 | 14 |
| Zerg 01 | 64x64 | jungle | 810 | 99 | 47 |
| Terran tutorial | 64x64 | platform | 856 | 44 | 16 |
| Zerg 03 | 64x96 | ashworld | 1034 | 129 | 18 |

`test_map` compares every MTXM value, each referenced tile's pixels (including
both flip branches), WPE colors, wholly unwalkable VF4 cells, unit types,
owners, exact fixed-point positions and camera coordinates. It rejects bad
chunk spans, missing MTXM, invalid dimensions and out-of-range tile IDs, and
accepts reordered/unknown chunks. The existing catalog test still checks all
228 unit slots. Reproduce with:

```sh
make
make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
env SDL_VIDEODRIVER=dummy build/bin/starcraft \
  --map install/campaign/terran/terran01/staredit/scenario.chk \
  --screenshot /private/tmp/starcraft-terran01.bmp
```

Verification completed: `make` and `make test-starcraft` pass, and all 35
extracted campaign CHKs pass `--check` without sprite-load warnings. The map
test saves `/private/tmp/starcraft-terran01-terrain.bmp`, a 2048x2048 terrain
and sprite-doodad overview without fog or actors. Visual inspection confirmed
continuous native road/cliff/river boundaries and authored bridge tiles;
the ordinary gameplay screenshot also preserves the native player start.

**Explicit engine limits, not claimed retail fidelity:** pathing remains at
32px resolution, with a cell blocked only when all sixteen VF4 minitiles are
unwalkable; mixed cliff/ramp cells need finer pathing. Sprite doodads currently
show their first GRP frame, without IScript animation or enabled/disabled
state evaluation. Creep, doodad special effects, mission triggers, rescue
rules/alliances, resource amounts, shields/energy and full campaign startup
are not implemented. Native UNIT placements are the initial map records;
triggers can change the retail opening state. Fog uses the existing engine
sight rules. SCM/SCX archives still require the existing import tool to extract
`staredit/scenario.chk`; no runtime MPQ dependency was introduced. These
limitations supersede the earlier catalog-only status without asserting that
loading a campaign map makes its mission playable.
