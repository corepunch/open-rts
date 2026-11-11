# ZED.EXE findings

Notes on reading `data/Z/ZED.EXE` and what it showed. File formats are in
`Z_FORMATS.md`; this file covers the executable itself.

## Reading the executable

`ZED.EXE` is a Watcom DOS/4GW program: an MZ stub with an LE header at file
offset 0x28b8. It has three objects:

| Object | Base | Size | Contents |
|---|---|---|---|
| 1 | 0x10000 | 0xb0254 | Code; entry point 0x90144 |
| 2 | 0xd0000 | 0x103c | Small data |
| 3 | 0xe0000 | 0x130390 | Data and tables |

Load each object at its base and apply the LE fixup records (types 7,
32-bit offset, and 8, 32-bit relative) to get the addresses used throughout
these notes. A Python loader and a Capstone disassembler were enough. They
are not kept in the repository, which keeps tools in C.

There is no symbol table, but many routines name themselves. Two inline
markers occur:

- `pushad; jmp short past "name\0"; mov dword [0x67f87], name; popad`
- `mov edx, name; jmp short past "name\0"`

0x67f87 holds the current routine's name for the game's error handler.
About 320 routines carry a marker, mostly buildings, effects and the
computer player: `do_robot_factory`, `draw_forts`, `attack_that_fort` and so
on. A linear disassembler must jump over the string instead of decoding it.

Many routines are reached through dispatch tables of pointers in object 3
rather than direct calls, so look for the routine's address as data as well.

## Level loading

| Address | What it does |
|---|---|
| 0x4f71a | Loads `levels.dat` to 0xe2394 |
| 0x4f740 | Copies the current level's file names (level number at 0x103d63) |
| 0x4f7ee | Picks `desert.blk` etc. by world (+0x6e) from 0x14510e |
| 0x4f453 | Level load sequence: CPU player, robots, map, objects, buildings, bridges |
| 0x4f40a | `CPUplr00.dat` to 0x18cadc |
| 0x56fc5 | `robots.dat` to the buffer at [0x103f94] |
| 0x73fdc | `map.map`: version-dependent block copies, then the fort scan |
| 0x4f6f9 | `objects1.dat` to 0xe5364 (1500 bytes) |
| 0x4f694 | `multiply.dat` to 0x152b10 |
| 0x4f6b2 | `building.dat` to 0x18ad2c (2800 bytes) |
| 0x4f6d8 | `bridges.dat` to 0x10f3d4 (312 bytes) |
| 0x4ec1c | Fort scan (positions at 0x103d1b, two u16 per fort) |
| 0x182d3 | Per-world fort sprites from 0x117de2 into 0x117dd2 |
| 0x45953 | Per-world building and terrain tables (world in 0x1178bc and 0x142f09) |
| 0x458c2 | Per-world cliff tiles to 0xe797d |
| 0x136aa | Level start: calls 0x452f2 to build the cliffs |

### Where the retail commander gets its data (2026-11-11)

The local DOS build runs the commander as native x86 code in `ZED.EXE`
(SHA-256 `3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`).
At `0x9b1e4` the dispatcher calls intelligence adjustment (`0x982c8`).
Its branches call scoring (`0x987a4`), allocation (`0x9b679`), offensive
orders (`0x98d58`), building and bridge responses (`0x9c58d` and `0x9c68b`),
recapture (`0x9c174`), repair orders (`0x9bc08`) and defence (`0x9d115`).
These are direct calls inside the executable. The installed loose-file
inventory and all 175 member names in `WARDATA.PAC`, `DOOR.PAC`,
`HEADFX.PAC` and `SHEADFX.PAC` yielded no separate AI script or module.
This check covers the local DOS installation.

Retail files supply inputs the port already reads in `games/z/w_lvl.c`:

- `LEVELS.DAT` selects the CPU-player, roster and multiplier files at
  descriptor offsets `0x2e`, `0x21` and `0x6f` (`0x4f740`). All 36 installed
  `CPUPLRnn.DAT` files are 2217 bytes. `0x4f40a` loads the selected file to
  `0x18cadc`. It supplies territory geometry, flags and importance, starting
  squads and their initial destinations, intelligence, force allocation and
  production settings. In LEVEL01, intelligence is 55, allocation is 40,
  and the enabled squads' destination bytes are `1,3,0,0`. At `0x9da1a`
  the code converts a nonzero destination byte to a territory index and
  `0x9da41` calls the native order routine. These records encode opening
  assignments. The full meaning of `CPUPLR` byte `0x8a3` remains unknown.
- `ROBOTS.DAT` supplies squad, robot and weapon records. `MULT.DAT` supplies
  the 628-byte valuation and production weight block loaded by `0x4f694`.
  `WARDATA.PAC` contains the 2048-byte `SQROOT.BIN` loaded at `0x70bf5`.
  Territory distance ranking uses its first 1024 divisors (`0x9a29c`).
- `LEVELnn.MAP`, `BUILDnn.DAT`, `OBJECTnn.DAT` and `BRIDGEnn.DAT` supply
  the world the commander evaluates. The two `preset*.wal` names in
  `LEVELS.DAT` are absent from the local install and its archives. The
  calls passing those names at `0x56d74`, `0x56d8c` and `0x56db9` reach
  `0x56dd7` (which copies 12 starting group bytes from descriptor `+0x55`)
  or `0x56dfc` (an immediate return).

Recheck the executable and archive directories without executing retail code:

```sh
shasum -a 256 data/Z/ZED.EXE
r2 -q -e bin.cache=true -e bin.relocs.apply=true \
  -c 'pd 24 @ 0x4f740' -c 'pD 0x495 @ 0x9b1e4' \
  -c 'pd 22 @ 0x56dd7' -c q data/Z/ZED.EXE
find data/Z -iname 'cpuplr*.dat' -exec wc -c {} +
for pac in data/Z/*.PAC; do
  count=$(tail -c 8 "$pac" | od -An -N4 -tu4)
  printf '%s (%d members)\n' "$pac" "$count"
  tail -c "$((count * 24 + 8))" "$pac" | strings -n 5
done
```

The existing C commander is the starting point for completing the missing
passes documented under [Commander passes and unit statistics](#commander-passes-and-unit-statistics-2026-11-06).

## Drawing

| Address | What it does |
|---|---|
| [0x1042ec] | Draws sprite `cx` at screen `ax`, `bx` (top-left) |
| 0x125be, 0x126d9 | Forts: the six pieces; 0x12742 the fort's light |
| 0x3e4db | Per-frame building pass over the 35 building records |
| 0x40e04 | `do_robot_factory`; sprite from 0x1426e4 by world |
| 0x4248d | `do_tank_factory`; 0x1426f8 |
| 0x3fa65 | `do_repair_facility`; 0x1426d0 |
| 0x40723 | `do_radar_station`; 0x14270c |
| 0x5fd39..0x5fd9d | Team colour: fill the 16-entry remap at 0xe0018 with 0x10, 0x20, 0x30, 0x40 or 0x50 onwards (0x5fd27: 0x50) |
| 0x10e950 | Team to colour index, set at run time |

## Buildings

The building type table at 0xe6120 has 74-byte entries: name (46 bytes with
padding), footprint at +0x30, then handler and draw pointers. 0x3ded8 turns
type 2 (fort) records into type 0xff when it sets up a level; forts live
in the map. 0x3ed17 links a building to its territory through 0x9dc69.

## Units

- Unit records are 0xb8 bytes, 240 of them at 0x171aa8, 60 per side.
  Known fields: +0x2c, +0x2e position (top-left pixel), +0x30 unit type,
  +0x32 animation frame, +0x35 action, +0x39 direction (0 north, clockwise
  through 7), +0x5d group, +0x79 unit class.
- A unit's terrain table pointer is at +0x18, set by 0x64bf6 from its type
  (+0x30) and team; see the terrain tables in `Z_FORMATS.md`.
- 0x603cc (`do_robots`) runs each unit's action through the table at
  0x149038; 0x7d2c4 adds it to the sprite list (see SPRITES.RSC in
  `Z_FORMATS.md`).
- Level start (0x56d2e): 0x56dd7 copies LEVELS.DAT's twelve groups to
  0x10e98b, 0x503dc mirrors them for each side, and 0x505ec spawns them.
  The player's groups stand at fort-relative offsets from a per-level table
  (pointers at 0x145e60, 0x145f2c for the second side; ported as
  `games/z/starts.inc`); a group's later members stand four pixels apart
  in rows of four. Guns (classes 2, 4, 5, 6) skip that table and take the
  fort emplacements at 0x145088 in the order the level's six letters at
  0x11d0ad give, remapped through 0x11d299 (0x50988); classes 4..6 sit 2
  pixels left and 8 up of the slot. Player units start facing north
  (0x51019).

## Flags, vehicles and grenades

Robots react to the terrain under their centre ((x + 8) / 16, (y + 8) / 16,
0x796cd) through the handler table at 0x1157ea; level setup marks flag
cells with terrain 0x23 and grenade crates with 0x24.

| Address | What it does |
|---|---|
| 0x2a89f | Plants a flag per territory (table at 0x18e828) and an Invisible flag object (type 28) on it, visible only to the other teams (+0x13, 0x2aa44), which is how robots find flags to walk to (0x61293) |
| 0x2aade | `draw_flags`: sprite 480 + [0x103d07] over shadow 484 + [0x103d07] with their top-left on the flag cell, team colour by remap; [0x103d07] steps every second tick (0x19e81); sorted 32 px lower (see the sprite list below) |
| 0x79ddc | Terrain 0x23: a robot on the flag cell, or a vehicle (class 1) with the flag in its 2x2 cells (0x79936), takes the territory unless its lockout is above 0x1d3; buildings follow through their territory pointer |
| 0x62e3c | The robot that took a flag turns south and cheers (action 0x21, sprites 1959..1962, seven times) |
| 0x61022 | Robots walk to the empty vehicle or gun they are sent to; 0x7af62 deletes the object within 20 px and turns the robot's record into the vehicle (type kept at +0x8a) |
| 0x61821 | A vehicle whose driver dies with hit points left goes back to an empty object; a destroyed one leaves its wreck |
| 0x79b25 | Terrain 0x24: a Grunt, Pyro or Laser (classes 0, 0xf, 0x10) within 16 px of a crate, whose squad holds no grenades, takes 10 (type 9) or 20 (type 10) for the squad, the remainder to the first member (0x79bea), at most 127 |
| 0x75939 | A robot with grenades throws at vehicles and guns (Lasers fire their laser), action 0x31 (0x6360e: 2522 + direction * 4 + frame); attacking a building needs grenades (0x753bb) |
| 0x636a0 | The grenade leaves on the throw's last frame; 0x5a205 launches it, drawn as 1336..1339 (0x59538); it bursts through 0x8476c with strength 0x32 for a Grunt |

In level 1 each fort's flag cell is its entrance: the computer's fort has
its gate cells (terrain 0x15, 0x9e, 0x3f) on its south side and the flag on
the cell below them, while the player's fort has them (0x1e, 0x3e) on its
north side under the flag. The second fort is drawn without a gate, so its
flag stands on its north wall.

## Forts

A fort has no hit points: 0x3ded8 drops the fort records from the
building list, and nothing else counts damage to one. A fort falls to the
first unit of another team inside it. Its state byte at 0x11d43d + team is
0xff while it stands (0x139ea), 2 while it falls and 0 once it is gone.

| Address | What it does |
|---|---|
| 0x7985d | `check_win`: terrain under a unit with a handler index of 0x64 or more (0x1157ea) goes through the table at 0x14ad18 |
| 0x79c41 | Fort entrances: terrain 0x14, 0x15, 0x3c or 0x3d marks the 4x4 cells in the middle of the fort of team 0, 1, 2 or 3 (the computer's forts have two more cells below); a unit of another team on one, while that fort stands, calls the fort's routine with its own team |
| 0x11fb1, 0x120b4, 0x121b9, 0x122b7 | The fall of the fort of team 0..3: state 2, the falling fort + 1 at 0x10e933, the fall's step count at 0x103f68 set to 1, a record queued at 0x11d4d4; with more than two players its territory goes to the taker (0x9dc69, 0x2a950) |
| 0x1241f | In a single player game no fort falls while another's fall runs (0x103f68 nonzero); in a network game, none with fewer than two players left |
| 0x11dfa | `do_fort_sequence` (0x15634), in the per-step routine list, steps each queued fall through 0x127a6; when one ends a single player game, the level ends (0x1e4b45, then 0x198fe) |
| 0x127a6 | The fall, by step: on step 2, if the fort was the local player's, 0x4611b, else 0x46089; steps 15 and 16 run 0x129c6 and 0x12ab6 (effects, not traced); step 17 the fort is gone (state 0, 0x10e932 = fort + 1); step 170 the end |
| 0x4613e | After 0x4611b: one of the local player's units dies (0x60fe6) each step, until step 169 |
| 0x460ac | After 0x46089: 0x470f5 runs for the winner's units one at a time (a sample, not traced) |
| 0x123e0 | The fort's wall terrain 0x10 becomes 0x13 |
| 0x12606 | A gone fort draws from the world's second set of pieces (0x117de2 entries at +10: 1675, 2148, 2431, 2845, 2725) in the standing fort's layout |

The game steps at most 70 times a second, so a fall takes about 2.4
seconds. `games/z/p_fort.c` ports the single player rule.

## The computer player

Historical discovery notes; the marker/implementation distinction,
vehicle search and per-level destinations are corrected and traced in
[the 2026-10-18 findings](#cliffs-vehicle-objectives-and-per-level-computer-orders-2026-10-18).

These routines name themselves but are not traced: `attack_that_fort`
(0x9b26f), `send_robots_to_flags` (0x9b295), `recapture_lost_territories`
(0x9b513) and `check_defend_territories` (0x9b64c). The forts also build
robots (`ffacchoose_fort_unit_to_build`, 0x41627).

## Robots knocked down and killed

| Address | What it does |
|---|---|
| 0x58745 | A blast's effect on the robots in its box: one with +0x3a above 10 loses 10 and is knocked down (0x58910), the rest are killed (0x60fe6, then 0x61560 and 0x777d9 take it off the map and out of its group) |
| 0x58910 | Knockdown: action 0x17 (sprite 315) facing directions 6, 7, 0 and 1, else 0x2e (316); direction and frame reset |
| 0x61f43 | Actions 0x17 and 0x2e hold while an explosion in the list at 0xab81c is within 0x18 px, then become 0x18 (319..329) or 0x2f (365..375) |
| 0x61f94 | Actions 0x18 and 0x2f step a frame every second update; at frame 11 0x79049 stands the robot up in action 1 |

The action table's 0x1a (304..314) is the robot sitting idle; 0x1b..0x1f
(2445, 2457, 2496, 2462, 2500) and 0x23..0x29 look like other idle antics.
Which routine draws a robot shot dead is not traced; `games/z` uses 290..295,
a robot bursting into bits and sparks, the only death-like run beside the
burning 167..183.

## The sprite list

Things to draw are 8-byte entries (u16 x, u16 y, u16 sprite, u8 palette and
draw routine, s8 y offset) at 0x24978, sorted by y (0x7d97e). The draw loop
(0x7da5d) adds the signed byte at +7 to y before drawing, so an entry sorts
by its y and draws at y plus that offset: `draw_flags` gives the flag
y + 32 and -32, and the shadow y - 16 and +16, which draws both on the flag
cell.

The game steps at most once per tick of a 70 Hz PIT (divisor 0x4295 at
0x702f5; main loop 0x49322), and many animations every second step.

## Random numbers

0x70d37 steps a 16-bit shift register at 0x103cdf with `rcr`/`rcl` (ported
as `next_random` in `games/z/w_cliff.c`). The game also records each call
for its network desync log. Network games reset the seed to 0x3039 at
0x49c77; single player does not reset it.

## Strings worth knowing

| Address | Contents |
|---|---|
| 0xe5976 | Object type names ("Land Mine", "Jeep", "Cactus (Des)"...) |
| 0xe6120 | Building type names |
| 0x117b7e | `sprites.rsc` plus `fsprites`, `gsprites`, `ssprites`, `isprites` (probably the French, German, Spanish and Italian sets) |
| 0x117c14 | Level file name templates (`map.map`, `robots.dat`, `CPUplr00.dat`...) |
| 0x1263ca | Unit names in several languages |

## Not decoded yet

- How the fort releases the starting robots (`do_fort_sequence`), and how
  the computer's CPUPLRnn.DAT groups are chosen and owned.
- The grenade burst's sprites, radius and damage per unit; the throw's
  range limit; driver ejection (0x7d0a2, 0x648e2).
- The crane's hook (0x7eaad reads its second offset through a pointer
  scaled as an index), the tank drivers' heads (0x7eccf, sprites from
  1464), fire truck and truck drawing, the robots' other actions (the shot
  death among them) and how robots notice enemies. Most unit and
  ROBOTS.DAT combat fields are now decoded ("Unit statistics" below);
  robot +0x24, weapon +0x21/+0x24 and body +0x18/+0x19 remain unknown.
- Per-team terrain rows and the other unit classes' tables.
- The byte per sprite after the SPRITES.RSC offsets, and each sprite's row
  bytes.
- `BRIDGEnn.DAT`, the computer player settings in CPUPLRnn.DAT, `MULT.DAT`,
  the five unknown bytes of each object record.
- `DOOR.PAC` members and the `BOTTOM` members of `HEADFX.PAC`.
- Robot hospital and ICBM launcher sprites (no retail level uses them).
- What each fort and factory manufactures and how long it takes
  (`do_robot_factory`, `do_tank_factory` and the fort's sequence);
  `games/z/products.inc` is authored, as is the speed-up from held
  territories.
- Audio sample rates and the front end. The T, D and Z buttons' handlers,
  the face animation sequences and the native production catalog/timing
  remain unported. The window drawing and panel coordinates were traced
  below; the earlier layout-by-eye is superseded.

## Building windows and sidebar correction (2026-10-15)

Static analysis only; the retail executable was not run. Fingerprints (SHA-256):

```
ZED.EXE      3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676
SPRITES.RSC  56b79cfaf489bdbcd9fd658c583d01ed31dd4e7b8575042b4fd8c216a3f5f6e8
SHEADFX.PAC  b923bc9460b1b60a138b83e810332b63c6c05ad893300d0302661e321ee91710
```

The Watcom/LE fingerprint and mapping above still apply. `r2` was unavailable
on this host. A scratch C loader applied the LE type 7/8 relocations and used
Apple's `libLTO` disassembler (`lto_initialize_disassembler`, `LLVMCreateDisasm`
with `i386-unknown-unknown`). The analysis output stays under ignored
`reverse/z-ui/`. No Python tooling was introduced. Addresses below are relocated
image addresses, not file offsets. With an LE-aware disassembler, inspect
`0x24ce0`, `0x25d89`, `0x25edd`, `0x25fbf`, `0x2638c`, `0x2cc7f`, `0x2c880`,
`0x32713`, `0x345b0` and their callees to reproduce the instruction evidence.

### Confirmed: the production window

The named per-frame `draw_building_box` marker at `0x161e6` calls `0x24ce0`.
The routine requires the active building pointer at `0x12e9a8`, checks that
the linked territory's team is the local player, and draws production controls
for building types 1 (robot factory), 2 (fort) and 4 (vehicle factory).
This corroborates showing the window only for the player's live producers.

`0x25fbf` draws **SPRITES.RSC 4694**, a 112x80 frame; this is independent of
the HEADFX/SHEADFX sidebar picture lists. All offsets here are relative to
that frame's top-left:

| Routine | Native artwork | Position / behavior |
|---|---|---|
| `0x25edd` | 4695 robot / 4696 fort / 4697 vehicle factory title | (8,6) |
| `0x25ffd` | 4699 select / 4700 building / 4701 placing status backgrounds; 4712..4714 their lettered versions | (64,19); the original alternates versions using its tick bit |
| `0x25e3c` | 4702/4703 Cancel, 4704/4705 OK; 4718/4719 Place when placing | (67,47), (67,62) |
| `0x25e8e` | 4706/4707 up, 4708/4709 down | (50,21), (50,64); only while no product is running |
| `0x25d89` | 4710 green meter, 4711 yellow overdraw | (53,21), clipped from its top by elapsed production's 37-pixel travel |
| `0x25fbf` | 1889 preview background | (6,22), 48x37 |
| `0x2572b` | Product name from the word table at `0x12eccd` | (3,62), 48x8 |
| `0x26129`, `0x2638c` | RSC small font 4722 onwards | Time at (92,35), `m:ss`, saturating at 9:59 |
| `0x25f1a` | Same small font | Building health at (89,6) when full; (91,6) when damaged, plus native damage shading |

The health field uses palette index 121 to shade its top `10 - health/10`
rows over a 19-pixel width, exactly as `0x25f1a`. The small-font renderer
at `0x5c684` subtracts 32 from the requested font base 4722 and adds
the character code; English uppercase/numeric advances are bytes at `0x148c46`.
This is a separate font from CHARS.BIN. Its advances keep the time inside the
small space following the frame's authored “Time” label.

`0x12ecb9` lists production group IDs; parallel u16 entries at `0x12eccd`
give the name sprites. Resolve groups through ROBOTS.DAT's 82-byte records
and their first robot's weapon type, rather than treating group IDs as unit
types. In weapon-type order 0..18 the verified labels are
`803,808,792,791,796,800,802,793,805,787,807,801,799,798,806,804,unknown,795,789`.
The unlisted Truck is not a product in our catalog.

`0x25977` tests Cancel at (67,47), OK at (67,62), arrows at (53,21)/(53,65),
and the preview box at (6,22). Cancel's `0x25c5f` sends a stop command when
production is running; otherwise it closes. OK's `0x25b3d` starts the chosen
product or closes an already-running window. Arrow routines `0x25ceb` and
`0x25d3a` refuse changes while running. Our widget hitboxes use the complete
authored button extents, and closing clears the building selection so a
second world click can reopen it.

`0x24ffc` anchors the window at the building's native (x,y) minus (96,48).
`0x24e72` recomputes its screen location as the camera moves. Retail can
leave the window partially outside the viewport; our resizable-window policy
clamps it to keep controls reachable. This clamp is engine behavior, not a
retail coordinate correction. The original supports dragging the frame
(`0x25a74`); that interaction remains unported.

### Confirmed: sidebar coordinates and portrait composition

The high-resolution branch at `0x2cc7f` initializes byte offsets into the
96-wide sidebar and 544-wide bottom bar. Dividing by the native row stride
gives these positions:

- Clock: one hours digit (26,4), minutes (50,4), seconds (74,4).
  `0x2f490` draws the hours remainder only; `0x2f45a` draws two digits with
  ten-pixel advance, even though each source cell is twelve pixels wide.
- Weapon / vehicle hardware panel: (0,144); grenade (26,186), grenade count
  (56,188); name (0,122), weapon label (0,228).
- T/D/Z buttons: (6,262), (36,262), (66,262), from `0x32713` onwards.
  `0x32513` draws directly into the panel; extra black slot pictures should
  not be placed behind these authored button images.
- Bottom percentages: (130,10)/(278,10) for two sides;
  (130,10)/(232,10)/(334,10) for three; and
  (130,10)/(204,10)/(278,10)/(352,10) for four.
  Bars start 22 pixels after each number. `0x31fc2` saturates at 99;
  wrapping 100 to 00 is wrong. Sources 67..69 are 80 pixels wide, 70..73
  are 54, 74..78 are 40 (team colors in order, then the empty bar).
  Do not crop an 80-wide team's
  artwork to stand in for a different-sized authored bar.
- Radar: `0x345b0` uses 86x104, at (6,288), and shrinks/centers the map
  preserving aspect ratio. The old (4,296), 92-square viewport overwrote
  the frame and distorted the map. The native output does not establish
  our current radar terrain sampling, tint or fog rules; those remain
  reimplementation behavior.

`0x2c880` decodes the packed portrait pose. `0x2c30c` gives the neutral pose
as **0x000f**: head 1, mouth 4, eyes 20, shoulders 0, no hands. The x/y
tables start at `0x1311fa`; each coordinate doubles with SHEADFX. In face
order Grunt, Tough, Pyro, Sniper, Psycho:

| Piece | Half-resolution coordinates |
|---|---|
| Head 1 | (4,3) for all |
| Shoulders 0 | (2,26), (2,21), (2,23), (2,21), (2,23) |
| Mouth 4 | (15,16), (15,16), (15,15), (15,16), (15,17) |
| Eyes 20 | (11,8), (11,8), (11,7), (11,8), (11,9) |

`0x2c131` copies the composed buffer from source row 4 to panel (2,42).
Do not center head cell 2 alone: it is a complete alternate face but omits
the shoulders. **Disproven during this investigation:** cells 31/32 are
helmet tops; the assets show hands, and pose bits 2..3 >= 2 suppress them.
Swapping the mouth/eye tables also creates an obvious hole and misaligned
eyes. Static neutral portraits are now used; native speech/blink/reaction
pose sequences remain unported. Friendly vehicles without an explicit
recorded driver use a Grunt portrait (an engine fallback), while known
drivers retain their own face type.

`0x2dbc6` and the high-resolution table at `0x13eb0c` place vehicle art
98..109 in the **lower hardware panel**, over background 113, with the
driver's portrait above. In picture order the top margins are
`8,8,10,4,10,2,16,6,2,10,8,2`. The previous implementation put these
96-wide hardware pictures into the portrait window. Blank/non-unit
selection no longer draws invented ownership or building-name labels.

### Verification and remaining differences

```
make
env SDL_VIDEODRIVER=dummy make test-z
env SDL_VIDEODRIVER=dummy build/bin/z --check
env SDL_VIDEODRIVER=dummy build/bin/z data/Z LEVEL01/LEVEL01.MAP --screenshot /private/tmp/z-hud.bmp
env SDL_VIDEODRIVER=dummy OPEN_RTS_Z_UI_SHOTS=/private/tmp build/bin/tests/z/test_hud
```

`test_panel` checks native artwork dimensions. `test_hud` checks both 1x
and 2x layouts, pressed artwork, aspect ratio, production/cancellation,
same-building reopening, enemy selection, all five portrait sets and the
vehicle panel. Its optional BMPs show idle/running windows and sidebars.
The final full build passed, all 23 Z tests passed, and the headless smoke
check loaded LEVEL01's 512 terrain tiles and 99 units. The network-menu
test required permission to bind a temporary localhost port; it passed
outside the restricted network sandbox.
Temporary diagnostic logging printed producer ID, chosen product, queue
state, popup bounds and radar bounds; it was removed after verification.

This ports the retail window artwork/layout and its basic control flow,
not the complete factory simulation. Product availability, manufacturing
times, territory acceleration and the preview's animation/orientation
still follow the existing engine implementation. Repair/radar structures
do not acquire invented production controls. Gun placement, star ratings,
frame dragging, UI status flashing and speech animation remain unported.

## Production preview portraits and complete vehicles (2026-10-15)

Static analysis used the same ZED.EXE and SPRITES.RSC fingerprints and
Watcom/LE mapping recorded above; retail execution was not needed. The
additional native portrait archive is:

```
HEADFX.PAC  ae4c3eb0c2bc5999b19fab62c207e1b5b5b8e5a6fb7384810dc87edff5bc9a27
```

**Confirmed:** `0x257b7` resolves the chosen production group through the
82-byte group, 44-byte robot and 42-byte weapon records. The table at
`0x12e9cc` selects the hardware preview path (`0x26452`) when nonzero;
otherwise `0x258d5` takes the first robot's face byte at robot offset 35
and renders a portrait. Robot previews are not standing world sprites.
`0x258d5` temporarily selects the low-resolution head set through
`0x2bf7c`, calls `0x60307`, then draws its generated sprite at popup
**(5,21)**. `0x60307` reads the 48-wide composed HEADFX buffer starting
at **row 1** and packs **48x38** pixels into the native planar format.
The frame strip **SPRITES.RSC 4717** follows at **(50,20)**. HEADFX.PAC's
HEADBIT0..4.DAT supply the five face sets and HEADBACK.DAT the six
48x40 backgrounds. The neutral pose uses the same head, mouth, eyes and
shoulder coordinates as the sidebar, without SHEADFX's doubling.

**Confirmed:** hardware preview routine `0x26452` prepares a temporary
unit, applies coordinate pairs from `0x12ea2c`, and dispatches through
`0x7e2d7`/`0x7da4f`. Its vehicle path thus uses the native multipart
unit rendering, including turrets. It does not draw a hull image alone.
The earlier popup implementation's use of only the spawn-state sprite
was incomplete. Its centering also used the shared all-directions,
animation-and-wreck canvas rather than the current pose's native parts.

**Implementation consequence:** the HUD now owns both native portrait
resolutions and shares neutral-pose composition. Vehicle loaders retain
each composed pose's rectangle union from native source dimensions and
part offsets; pixels, ground points and turret mounts are unchanged.
The popup combines hull and turret using that mount, then centers the
union in its 48x37 viewport. This centering is the requested engine
behavior, not a claim to reproduce the retail offset table. Native
preview rotation, animation and speech remain unported; the current
engine keeps its static south-facing vehicle and neutral face policy.
Laser retains the existing Grunt-face fallback; no separate Laser face
set was discovered.

Verification: `test_hud` checks reviewed LEVEL01 indexed-pixel fixtures
for all 13 products at 1x and 2x, including all six robot products and
all three tanks. `test_panel` checks both archives' dimensions. Optional
`OPEN_RTS_Z_UI_SHOTS=/private/tmp` writes each product popup for review.
Temporary diagnostics confirmed the old hull canvas/ground/mount values
(Light 36x32 / (19,14) / (5,0); Medium 34x32 / (16,15) / (2,7);
Heavy 34x34 / (16,16) / (2,2)); they were removed after verification.
Reproduce the instruction evidence with an LE-aware disassembler at
`0x257b7`, `0x258d5`, `0x26452`, `0x60307` and `0x2c880`.
The full `make` passed without warnings, all 23 Z tests passed (the
localhost network-menu check needed an unrestricted retry), and the
headless check/screenshot loaded LEVEL01's 512 tiles and 99 units.
All 13 captured product popups and the full-level screenshot were
visually inspected.

## Production availability thresholds (2026-10-15)

This investigation answers why "Virgin Soldiers" offers only Grunt,
Gattling (the native spelling) and Jeep. Static disassembly used the same
Watcom/LE mapping, ZED.EXE fingerprint and C/libLTO fallback described
above. No original executable was run. Additional SHA-256 fingerprints:

```
ROBOTS.DAT           7a9e251c02178c270a5723614e9432506d359a0ca7cd85bb04240005c42026be
LEVELS.DAT           7a934e1dd7872ad0ac86d29551e6fbff133bf0a71c3d5a86f7803afeeecd00e7
LEVEL01/BUILD01.DAT  aa786c00888ec671d3c4fe75d9b5df27a97288c05cf6cd0d4dbd2d8c1038688b
```

### Confirmed: product requirements and a producer's rating

There is no bitmask in the controlling single-player selection path.
Availability is determined from each producer's live u32 rating at building
record **+0x0c**, compared with the chosen ROBOTS.DAT group's minimum.
The popup's forward/backward selectors are:

| Producer native type | Forward / backward routine | Group byte read |
|---|---|---|
| 1, tank factory | 0x25231 / 0x252ee | +73 (0x49) |
| 4, robot factory | 0x253af / 0x25464 | +74 (0x4a) |
| 2, fort | 0x25622 / 0x25519 | +75 (0x4b) |

The choice of routine is visible at `0x25d09` and `0x25d58`. A group
record starts at `ROBOTS.DAT + 18000 + group * 82`. The selectors:

1. Reject a zero minimum byte.
2. Multiply that minimum by **1000** and reject it when greater than the
   producer's u32 at +12.
3. Require the group's byte 0 to be nonzero and its first u16 member at
   +28 to differ from 0xffff.
4. Resolve that member through the 44-byte robot record, weapon index
   at robot +34, then the 42-byte weapon record at file offset 22000.
   Weapon +1 is the class used to restrict the producer family.

Tank factories accept weapon classes `{2,1,8,7,4,9,5,6,11,10,12,13}`;
robot factories accept `{0,17,18,3,15,16,2,4,5,6}`. Forts accept their
union. In particular, a robot factory can produce guns: it is not a
robots-only producer. The common chooser at `0x2512c` / `0x25194` walks
the group list at **0x12ecb9**; in single player the valid indices are
0..16, giving these group IDs in order:

```
10, 0, 14, 8, 2, 6, 3, 4, 17, 18, 11, 13, 7, 1, 5, 9, 16
```

Fire Truck follows at index 17 and is outside that single-player cycle;
Truck is absent and has all three requirement bytes zero. Neither should
be added to the single-player options merely because its group is active.
The chooser also has a separate multiplayer bound; multiplayer policy was
not fully traced in this investigation.

Verified retail requirements, in threshold units (multiply by 1000;
**0 means unavailable**, not free access):

| Group | Product | Weapon index | Tank factory | Robot factory | Fort |
|---|---|---|---|---|---|
| 10 | Gattling | 11 | 1 | 1 | 1 |
| 0 | Gun | 0 | 2 | 3 | 2 |
| 14 | Howitzer | 12 | 3 | 5 | 4 |
| 8 | Missile | 9 | 5 | 0 | 5 |
| 2 | Grunt | 2 | 0 | 1 | 1 |
| 6 | Psychos | 7 | 0 | 2 | 2 |
| 3 | Tough | 3 | 0 | 3 | 2 |
| 4 | Sniper | 4 | 0 | 3 | 3 |
| 17 | Flamer / Pyros | 17 | 0 | 4 | 4 |
| 18 | Laser | 18 | 0 | 5 | 5 |
| 11 | Jeep | 13 | 1 | 0 | 1 |
| 13 | APC | 10 | 3 | 0 | 3 |
| 7 | Light | 8 | 1 | 0 | 2 |
| 1 | Medium | 1 | 2 | 0 | 3 |
| 5 | Heavy | 5 | 4 | 0 | 5 |
| 9 | M. Missile | 6 | 5 | 0 | 5 |
| 16 | Crane | 15 | 5 | 0 | 5 |

### Confirmed: ordinary factories versus forts

The ordinary factory's rating comes directly from its 80-byte BUILDnn.DAT
record at **+12**. Every active robot factory in BUILD01.DAT (record
indices 0, 2, 5 and 6) contains **1000**. BUILD02.DAT already demonstrates
variation within a level: robot factory record 4 contains **2000**, while
its other robot factories and tank factories contain 1000. Thus a single
level-wide allowed-product mask would lose real differences between
capturable factories.

Forts are different. `0x136c1` / `0x136d3` call `0x4ee67` to construct
fort building records from the map markers. At `0x4ee94`, that routine
reads the low byte of the u16 at **0x12d789 + level_number * 2**,
multiplies it by 1000 and writes building +12. The startup code at
**0x4735a..0x4751b** initializes this table. Reading only its on-disk
image is misleading: entries 1..20 initially contain zero and are
assigned by startup instructions. Their zero image contents do not mean
those forts lack production.

The initialized table, indices 0..50, is:

```
0,
1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5,
5,4,5,4,5,3,4,4,5,5,5,5,3,3,4,5,5,4,5,3,2,3,4,5,5,5,5,2,2,3
```

For the regular first twenty campaign missions this means fort ratings
1000 in missions 1..4, 2000 in 5..8, 3000 in 9..12, 4000 in 13..16,
and 5000 in 17..20. This is an executable-owned campaign table;
LEVELS.DAT's twelve starting groups are an army roster, not the build
catalog. The 10000 values at LEVELS.DAT +0x7c/+0x80 instead feed
`0x4ec4a..0x4ecc2` and fort hit-point globals; they are not technology
ratings (the arithmetic rounds their low-word values to multiples of 9).

### Verification, implementation consequence and remaining work

A focused scratch C check read the original ROBOTS.DAT and BUILD01.DAT,
walked the 17 verified production groups, and asserted:

- Fort rating 1000: exactly groups **10, 2, 11**, weapon indices
  **11, 2, 13** — Gattling, Grunt, Jeep.
- All four level-1 robot factories have rating 1000 and accept exactly
  groups **10, 2** — Gattling, Grunt.

The check passed. Raw values can be reproduced without the scratch tool:

```sh
# First factory's u32 rating (1000); repeat at 80*record + 12.
od -An -t u4 -j 12 -N 4 data/Z/LEVEL01/BUILD01.DAT
# Grunt group: bytes +73..75 are tank=0, robot=1, fort=1.
od -An -t u1 -j 18237 -N 3 data/Z/ROBOTS.DAT
# Gattling: tank=1, robot=1, fort=1.
od -An -t u1 -j 18893 -N 3 data/Z/ROBOTS.DAT
# Jeep: tank=1, robot=0, fort=1.
od -An -t u1 -j 18975 -N 3 data/Z/ROBOTS.DAT
```

Reproduce the control flow using an LE-aware disassembler at `0x2512c`,
`0x25231`, `0x253af`, `0x25622`, `0x4735a`, `0x4ee67` and the data
at `0x12ecb9`. Discovery outputs remain under ignored `reverse/z-ui/`;
the useful results and rejected hypotheses are preserved here.

**Engine mismatch at the time of investigation (corrected below):** `Z_ProductsFor` used a hardcoded family mask
without the native thresholds, and `G_ModelProducerHasTech` always returns
true. `products.inc` omitted all four guns and incorrectly excluded them
from robot factories. Correct implementation must carry each producer's
rating, read the appropriate group requirement, and enforce the same check
in UI, direct production orders and computer-player decisions. It must
also expose the four gun products. This investigation changes documentation
only; runtime availability is not corrected yet.

Gun manufacturing must retain its separate placement behavior: `0x42044`
checks the weapon class through `0x142de6` and branches to the
`bnrplace_made_gun` path at `0x4209f`, calling `0x430b2`. Treating a
finished gun as an ordinary vehicle leaving a factory door would be a
separate, unverified behavior change. That placement path remains unported.

Timing was examined only far enough to reject another tempting shortcut:
`0x98acc` computes a group's stock value from its members and weapon
properties, using weight tables selected through `0x152a78` and
`0x152ac4`. The pointed-to weights are zero in the static image and
require runtime initialization; static zero weights do not establish
instant production. `0x41a69` / `0x41afc` scale the stock by 221 and the
speed setting at `0x103d81`; `0x422a7` incorporates health and territory
rate. `0x42405` computes that territory rate as
`floor(floor(100 * held / total) / 4) + 1`, which differs from the engine's
current linear double-speed-at-all-territories rule. These timing formulas
and weight initialization need a dedicated port; no new delay constant or
runtime balance change was introduced here. Live rating upgrades, gun
placement details and multiplayer availability remain unverified.

### Implementation: native availability and the missing gun products

The runtime now carries building +12 in `mobj_t.z.rating`, includes it in
the game-field consistency hash/save record, and preserves the loaded
ROBOTS.DAT under the active level's native-data owner. Starting armies
and production borrow the same checked records. There is no second
requirements table or second roster read. BUILDnn.DAT supplies ordinary
factory ratings; the verified campaign table supplies fort ratings.

The catalog follows the 17 native single-player group IDs in order.
`G_ModelProducerHasTech` enforces group enablement, checked first-member
resolution, native weapon classes, a nonzero requirement and the exact
`minimum * 1000 <= rating` boundary. Popup enumeration, model availability,
direct build commands and the existing AI all reach this one check.
Fire Truck and Truck remain outside the single-player catalog. Capturing
an ordinary factory preserves that particular factory's rating.

The four missing gun products are selectable and manufacturable. A
completed gun waits in the producer's existing production record using
the shared release lifecycle. The authored Place button (4718/4719)
and placing status (4714) become visible, and the shared menu target
mechanism requests a world location. Escape cancels targeting while
retaining the gun; Cancel discards the producer's production. A validated
TC_CONSTRUCT command consumes the stored gun, allocates an ordinary mobj
and restarts the producer. Other players, incorrect product IDs, occupied
mounts and foreign-territory targets cannot bypass the checks.

**Confirmed additional placement evidence:** `0x4e6b7` aligns the target
to a 16-pixel cell and compares its territory with the selected producer
territory. Its fort area is 144x144; `0x4e676` chooses a mount with the
x=64 split and y<31 / y<=80 / y>80 bands. Left top/middle/bottom use slots
3/5/0; right top/middle/bottom use 1/4/2. The coordinates are the existing
six native offsets at 0x145088 (already used for starting guns). Turning
guns subtract (2,8), whereas the Light Gun does not. `0x4e8be` rejects
overlap with existing guns; `0x4e99c` checks building rectangles. The
normal-ground branch also calls terrain/occupancy checks before returning
an acceptable location. The terrain table at 0x11582d allows IDs
`0,8,10,22,30,31,45,53,54,55,59,62,63,64,65,66`; other entries are nonzero.

The implementation uses those fort sectors/mounts, the terrain table's
allowed IDs over a 2x2 placement and checked overlap with live engine
footprints/native unit rectangles. Roof positions use the same raised
representation as starting guns. A robot factory can place on a friendly
fort in the same territory. The existing computer-player scheduler places
finished guns with these same checks, preferring fort mounts before a
deterministic first-valid-cell ground search. No branch depends on the
local console player, preserving lock-step behavior.

**Explicit engine policy:** production holds one completed gun at the
head of the shared queue until placement. Retail `0x4317f` instead shifts
four built-list bytes at building +75..78, and retail AI `0x9df4f` has
additional placement priorities and random choices. The engine does not
yet reproduce that four-gun storage or those AI priorities. Mobile-unit
manufacturing times and their existing linear territory acceleration are
preserved; only the newly introduced gun timers use the native stock/rate
calculation described next. Live technology upgrades and multiplayer
catalog bounds remain outside this availability change.

#### Corrected: zero static weights are loaded from MULT.DAT

The previous unknown about zero stock weights is resolved. `0x4f694`
loads the file selected by LEVELS.DAT +0x6f (MULT.DAT in the retail levels)
into **0x152b10..0x152d83**, exactly **628 bytes**. The pointer tables at
0x152a78 (robot weights) and 0x152ac4 (weapon weights) point inside this
loaded block. The zero executable image is not the active weight data.
Additional SHA-256 fingerprints:

```
MULT.DAT              b71e60b739b512697c9eb95f91f23443724945bfacbe865a4a916a7729b1d396
LEVEL01/CPUPLR01.DAT  f745ecbe54147c82fc1d6edd2251113d395949e00161f4197d0f5debf92f8a3b
```

A scratch C evaluator applied those pointers after copying MULT.DAT to
0x152b10 and followed the unsigned integer arithmetic of `0x98acc`:
for every member of a production group, average the applicable weighted
robot fields (+28,+29,+30,+31,+32,+33,+36); average applicable weighted
weapon fields (u16 +2, byte +4, +5*25, +32,+34,+35,+36*8,+37,+38,+39),
skipping any weight 0xff; multiply the weapon average by weight +10,
then add the robot average. Sum those member values for the group.
The resulting new gun stock literals are:

| Native group | Product | Stock |
|---|---|---|
| 10 | Gattling | 1468 |
| 0 | Gun | 1746 |
| 14 | Howitzer | 2610 |
| 8 | Missile | 3451 |

These are authored C configuration literals, consistent with the project's
rule against loading native unit balance tables at runtime. Inspect
0x98acc..0x98d55 and the two pointer tables against the original ROBOTS.DAT
and MULT.DAT to reproduce them. Scratch evaluation of the remaining groups
also produced 1062 for Grunt, 1826 for Tough, 2244 for Sniper, 1482 for
Psychos, 2436 for Flamer and 2716 for Laser; those values do not replace
the existing engine's mobile-unit timers in this change.

**Confirmed speed correction:** global 0x103d81 initially contains 8,
but `0x9d81b` replaces it from CPUPLR offset **0x8a2** (2210). LEVEL01's
value is **10**. Using the static initial 8 would therefore give the wrong
mission timing. The new gun timer's base is
`floor(stock * 221 / level_speed)` native ticks, converted to milliseconds
at **17 Hz**. `0x41a69` / `0x41afc` establish the stock scaling and
`0x26129` the 17-tick time conversion. Territory rate is
`floor(floor(100 * held / total) / 4) + 1`; `0x422a7` multiplies that by
`floor(100 * hp / max_hp)` and divides by 100. The timer and popup time
estimate apply the same rate. A near-destroyed building whose rounded
rate is zero stalls rather than continuing at full speed.

Reproduce the raw speed value with:

```sh
od -An -t u1 -j 2210 -N 1 data/Z/LEVEL01/CPUPLR01.DAT
wc -c data/Z/MULT.DAT
```

Focused verification now covers exact level-1 fort/factory choices;
level-2 factories with different ratings; tier boundaries at 1999/2000;
zero minimums and inactive groups; fort ratings and unlock counts in
missions 1,5,9,13,17,22,26,33; blocked model/direct build commands;
Gatling's stock/speed timer; stored-gun cancellation, owner/product/territory
validation, occupied mounts, native roof placement and AI placement.
`test_hud` checks all 17 preview pixel fixtures at 1x/2x and the Place,
Escape and world-target flow. Temporary diagnostics printed producer ID,
rating, group, resolved type, minimum and permission result, and were
removed after confirming the exact first-level restrictions.

Final verification: full `make` completed without warnings; all 23 Z tests
passed, with the network-menu test retried outside the network sandbox to
bind its temporary localhost port. A final focused production rerun also
verified that a zero rounded health rate stalls the new gun timer. Headless
`--check` and `--screenshot` loaded LEVEL01's 512 tiles and 99 units.
All 17 preview captures, the completed-gun Place window and the full-level
screenshot were visually inspected.

## Native production timing for all units (2026-10-15)

This follow-up supersedes the earlier implementation decision to preserve
mobile-unit timers and linear territory acceleration. Robots, vehicles and
guns now use the same verified native stock, level-speed and health/territory
calculation. The earlier gun-only timing implementation also missed flagless
territories; that denominator is corrected for every product.

**Provenance:** the same local ZED.EXE, ROBOTS.DAT, MULT.DAT and
LEVEL01/CPUPLR01.DAT fingerprints recorded above. ZED.EXE SHA-256 remains
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
No original executable was run. The documented scratch C LE relocation loader
and Apple libLTO instruction decoder were used because r2 is unavailable;
C evaluation of ROBOTS.DAT with the loaded MULT.DAT weights reproduced the
following stock values. The native algorithm, weight tables, field offsets
and loaded weight-block span are documented in the preceding section.

| Group | Engine product | Native stock |
|---|---|---|
| 10 | Gatling | 1468 |
| 0 | Light Gun | 1746 |
| 14 | Howitzer | 2610 |
| 8 | Missile Gun | 3451 |
| 2 | Grunt | 1062 |
| 6 | Psycho | 1482 |
| 3 | Tough | 1826 |
| 4 | Sniper | 2244 |
| 17 | Pyro | 2436 |
| 18 | Laser | 2716 |
| 11 | Jeep | 1250 |
| 13 | APC | 1820 |
| 7 | Light Tank | 2152 |
| 1 | Medium Tank | 3380 |
| 5 | Heavy Tank | 4124 |
| 9 | Mobile Missile | 4780 |
| 16 | Crane | 2011 |

**Confirmed common timer:** `0x41a69` and `0x41afc` call `0x98acc`, multiply
its stock by 221 and divide by global `0x103d81`. The latter is installed
from CPUPLR +0x8a2, so LEVEL01 uses 10 rather than the executable's initial
8. The resulting timer units use the display's divisor of 17 (`0x26164` to
`0x2616c`). In the shared engine, the base duration is therefore
`floor(floor(stock * 221 / speed) * 1000 / 17)` milliseconds. Preserve that
order: at speed 20 Jeep is 812470 ms, rather than half its speed-10 value
of 1625000 ms. Balance values are authored C literals in `products.inc`;
there is no runtime extraction of unit statistics or a second seconds table.

**Confirmed territory denominator:** `0x42405..0x42437` walks all 64
30-byte records at `0x18cadc`. Each record whose signed first word is
nonnegative contributes to the denominator, regardless of options +0x18
or whether a flag was planted. Matching team byte +8 contributes to the
numerator. It calculates `floor(100 * held / total)`, shifts right two,
then adds one. LEVEL01 has **nine** active records but **eight** flags:
record zero has team 4 and options 0x83 (no flag). The initial teams have
one territory each, with seven neutral. The healthy player rate is thus
**3**, not the **4** produced by counting only flags. Capturable territories
use their flag's current owner in the engine; flagless ones use the native
record's owner. Counts are a temporary borrowed view, with no new persistent
ownership or timer state.

**Confirmed health rounding and common use:** `0x422a7..0x422ce` and
`0x422cf..0x422f6` first calculate `floor(100 * hp / max_hp)` using building
+28/+26, select the territory's team's rate through building +30 and team
byte +8, then calculate `floor(health_percent * territory_rate / 100)`.
Robot factory `0x41206`, fort `0x417d0` and tank factory `0x428fa` all call
`0x422cf`, explicitly skipping work when it returns zero. With one of nine
territories, full health gives rate 3; 999/1000 health gives 2; 670/1000 gives
2; 669/1000 gives 1; 340/1000 gives 1; 339/1000 gives 0. These two integer
rounding steps must not be replaced by a single fractional multiplier.
Eight of nine territories gives rate 23 at full health and 11 at half;
all nine gives 26. Neither a capture nor a health change resets work already
done. A zero rounded rate stalls; regaining health or territory resumes it.

**Confirmed popup behavior:** `0x26129` divides remaining timer units by
rate and then 17, truncating seconds. Busy timers no longer round up a
partial second. A zero rate branches to `0x2619a`, drawing the bytes
`$:$$` from `0x12ecaf` with font 4722 at window (88,35), rather than a
9:59 saturation. Numeric time uses (92,35) and the existing native 9:59
clamp. A completed gun follows `0x26240`, with zero remaining time even
when the building's health would stall further work. The native glyphs
and their placement were inspected in both normal and stalled HUD captures.

**Implementation boundary:** the engine advances the converted timer in
its shared deterministic millisecond ticker, with the same effective integer
rate used by the popup estimate. Completion is serviced on the next engine
tick; no separate retail clock or game-specific queue is introduced. Gun
storage/placement remains the explicit serial engine policy documented above;
classification now uses the actor's mobile trait rather than nonzero stock,
since every product has stock. All thirteen mobile products still leave the
producer, vehicles retain their driver and repeat production still queues the
same product. The excluded Truck and Fire Truck remain outside the native
single-player catalog; scratch stock evaluation returned 1257 and 1511
respectively, with no runtime consequence.

Reproduce the asset inputs and focused behavioral checks:

```sh
shasum -a 256 data/Z/ZED.EXE data/Z/ROBOTS.DAT data/Z/MULT.DAT data/Z/LEVEL01/CPUPLR01.DAT
od -An -t u1 -j 2210 -N 1 data/Z/LEVEL01/CPUPLR01.DAT
od -An -t u1 -N 30 data/Z/LEVEL01/CPUPLR01.DAT
make -j4 build/bin/tests/z/test_production build/bin/tests/z/test_capture build/bin/tests/z/test_hud
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_production
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_capture
env SDL_VIDEODRIVER=dummy OPEN_RTS_Z_UI_SHOTS=/private/tmp build/bin/tests/z/test_hud
```

`test_production` checks all 17 base durations at speeds 10 and 20, the
millisecond before and at each completion deadline, and 15 rate/health
boundary cases on forts, robot factories and tank factories. It verifies
idle/active estimates, capture changes without resetting progress, stall
and recovery, rate 26 with a flagless owned territory, completed-gun timing,
and correct mobile release versus gun storage. `test_capture` verifies the
production share changes on actual capture and recapture. `test_hud` checks
native truncation across the one-second boundary at both UI scales and
captures the busy/stalled windows. Temporary diagnostics printed producer
ID, product type, base/remaining work, territory counts, health, rate and
elapsed time; they were removed after matching these boundaries.

Final verification: `make -j4` completed without warnings; all **23 Z tests
passed** in the complete headless suite (localhost access enabled for its
network-menu test). Headless `--check` and explicit LEVEL01 `--screenshot`
passed with 512 terrain tiles and 99 units. The full level plus normal and
stalled production popups were visually inspected. No diagnostic logging
remains in the committed implementation.

## Cliffs, vehicle objectives and per-level computer orders (2026-10-18)

Static inspection of the retail files; the executable was not run. ZED.EXE
SHA-256 remains
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
LEVEL01/CPUPLR01.DAT SHA-256 is
`f745ecbe54147c82fc1d6edd2251113d395949e00161f4197d0f5debf92f8a3b`.
The Watcom DOS/4GW LE object mapping above still applies. `r2` remains
unavailable on this host; the same scratch C LE-fixup loader and Apple's
libLTO i386 disassembler were used. Ignored discovery output is under
`reverse/z-ui/` (`ai-dis.txt`, `board-ai.txt`, `cliffs-dis.txt`,
`cliff-impact.txt`, `cpu-refs.txt`, `level-fields.txt`). No Python tools or
new external sources were used.

### Confirmed: damageable cliff cells

- Startup `0x4541e` gives each set mask bit an eight-byte runtime record:
  tile-plane address at +0, cliff block index at +4, **durability copied
  from block byte +5** at runtime +5, and an inside-tile random variant
  at +6. `0x45321` counts the set cells to size this storage.
- `0x8476c` is an explosion/impact handler, not a movement routine. Its
  terrain checks at `0x8498f` and `0x84a26` classify terrain **1..6** and
  **0x12** as cliff. At `0x84b4d` it calls `0x44e28` with the impact
  strength in DL. It checks the impact cell and nearby cells; the
  `0x14c05d` flag can suppress cliff damage. That flag's full weapon
  dispatch has not been decoded.
- `0x44e28` resolves a face cell upward to its top, finds that top's
  runtime record among at most 5000 records, and subtracts DL from
  the signed durability byte at `0x44e93`. **Zero survives; negative
  destroys** (`jns` at `0x44e96`). It removes the runtime entry and clears
  the corresponding native mask bit through `0x4589e`.
- `0x4529a` replaces the top plus its height of face rows with rubble:
  terrain **0x16**, keeping the bank bit. It chooses one of eight native
  rubble entries with RNG & 7. `0x4592b` constructs these entries from
  byte offsets at `0x1faa74`: 178,180,182,184,186,188,188,188, i.e.
  cliff-table indices **89,90,91,92,93,94,94,94**. Assuming eight distinct
  consecutive cliff-table entries would overrun the 95-word table.
- The destruction path calls `0x44f11` to protect overlapping cliffs and
  `0x45352` to redraw surviving edges and faces. It also spawns debris
  through `0x450a5`. Native debris sprites and full overlap/shadow repair
  are not yet ported.

LEVEL01 has 153 set cliff cells: blocks 1 and 2 contain 77 and 76, each
height 2 and durability 5. The stamped plane has 153 top cells and 120
visible face cells. Across campaign levels 1..20, all occupied cliff
blocks have height 2; durability is 5 in level 1 and 0 in levels 2..20.
The static neutral robot row at `0x116c00` has **zero** for heights 1..6
and face 0x12. It therefore cannot by itself describe ground movement
through the stamped cliff volume. The earlier assumption that this one
row was sufficient is **disproven**. This does not prove that native
robots can never occupy an elevated surface: the complete native
movement/height transition and path-node rules remain **unknown**.

Implementation: the engine's planar blocked grid now excludes the native
cliff terrain range as a whole for ground units, so path planning and
collision agree. Each destructible mask bit is an ordinary stable mobj;
its HP is durability + 1 to express the native zero-survives boundary.
The native tile planes own its picture. Contextual orders on the top or
face target that cell; robots require grenades, and shell/missile guns
can attack it. Ordinary gunfire cannot target it. Grenade blast damage
also reaches nearby cliff mobjs. Destruction edits the native mask and
rubble ground, then rebuilds visible tiles, collision and nav inputs.
Destroyed masks and rubble are saved and included in consistency checks.
Z's loader now retains its map path, required by the shared save writer.

Engine limits, explicitly **not claims of native equivalence**: direct
weapons still use the existing engine instant-hit path; exact native
projectile/cliff interception, damage strength selection and per-weapon
suppression are unported. Rebuild uses the existing deterministic network
seed 0x3039 and rerolls equivalent interior tiles; rubble selection uses
that same fixed seed locally. It does not retain the native per-cell
random variant, and rebuilt shadows follow the current stamp routine.
The engine's existing weapon damage and grenade blast weights remain the
authoritative port balance until native dispatch is traced completely.

### Confirmed: the computer does more than take flags

The strings previously listed as routine addresses are **inline debug
markers**, not the strategic implementations. The dispatcher around
`0x9b1e3` calls:

| Marker | Implementation |
|---|---|
| attack_that_fort (`0x9b26f`) | `0x99086` |
| send_robots_to_flags (`0x9b295`) | `0x9b816` |
| aggressive_opponent (`0x9b3fc`) | `0x98d58` |
| check_CPU_player_units (`0x9b56e`) | `0x9cc83` |
| check_units_homes (`0x9b59a`) | `0x9cb5e` |
| check_for_repairable_vehicles (`0x9b5c1`) | `0x9c272` |

`0x98d58` dispatches twelve further strategy passes, including `0x98ee1`.
That pass considers robot classes 0,3,15,16 and calls `0x98f32` to find
empty vehicles/guns. It enumerates 200 forty-byte world object records
at `0x1c04d0`, rejects invalid/ineligible/reserved objects, recognizes
native object types 5,8,21,22,23,24,25,29,31,33,35,37, checks the
containing territory, and selects the nearest by squared distance.
`0x99a69` checks existing computer task **5** for the same territory;
`0x99074` records the territory and `0x99077` sets task 5 before sending
an ordinary order through `0x475d5`. Territory byte +29 bit 0 bypasses
a random gate; otherwise `0x98fe3` uses RNG & 1 to sometimes skip the
candidate. These territorial eligibility/probability rules have not been
ported as a complete strategic system.

The computer keeps 240 task records of 22 bytes at `0x18d388`. Their
+17 task and +21 active byte are separate from the actual unit record.
The corrected conclusion is that flags are one objective family among
vehicle acquisition, fort assault, territorial defense/recapture,
repairs and building/bridge responses. The old nearest-flag commander
was an authored approximation, not behavior recovered from ZED.EXE.

### Confirmed: CPUPLR contains per-level starting destinations

`0x9d985` reads exactly **26** ten-byte records from CPUPLR +0x792,
using **bit 0** of record byte +1 to enable a starting group. The previous
27-record limit and any-nonzero test were incorrect; file length must not
be used to include trailing settings as placements.

`0x9d9c7` associates enabled records with computer tasks. A nonzero
record **byte +7** selects territory **byte - 1**, stored as both the
initial and destination territory; it sets task 0 and calls `0x9d42c`.
That order routine derives its destination from territory flag coordinates,
with special fort-territory destinations. LEVEL01's four enabled groups
are 2,2,2,10 and their +7 values are **1,3,0,0**. LEVEL02 uses 2,4,0,0,0;
LEVEL03 uses 0,0,3,4,5,1. Thus the level really does direct particular
starting groups. Record bytes +6,+8,+9 remain **unknown**.

CPUPLR +**0x89f** is also a strategic weight: `0x9882f`, `0x9cd3d` and
`0x9d40d` multiply it by territory byte +9 and 16 for value/balance
calculations. LEVEL01's tail +0x89f..0x8a3 is 10,2,11,10,10; level 2
has 10,2,11,8,10. `0x9d7de` installs +0x8a0,+0x8a1,+0x8a3,+0x8a2
in globals 0x103d79,0x103d7b,0x103d7d,0x103d81 respectively. Only
+0x8a2's production-speed meaning was previously fully decoded. The
other settings' complete semantics and the intelligence-delay control
are still **unknown**; no difficulty scaling was guessed from them.

Implementation: starting groups retain +7 in mobj state and get their
initial territory order. Computer robots choose reachable empty
vehicles/guns and reserve them with their ordinary boarding orders;
currently these reservations are per object, rather than native task
records per territory. Idle remaining squad members can still take flags.
Flags, forts and vehicles must be in the unit's reachable nav component.
Computer marches/boarding can yield to nearby hostile units, and squads
join a member's fight; human explicit travel/boarding is preserved.
The existing authored notice radius and nearest-flag/fort scoring remain;
this change does not claim a complete port of native strategic AI.

### Verification and reproducibility

Inspect relocated instructions at `0x4541e`, `0x44e28`, `0x4529a`,
`0x4592b`, `0x8476c`, `0x98ee1`, `0x98f32`, `0x99a69`, `0x9d985`,
`0x9d9c7`, `0x9d42c`, `0x9882f` and `0x9d7de`; compare the MAP mask
blocks and CPUPLR placement/tail fields above. No retail execution is
needed. Focused engine reproductions:

```
make build/bin/tests/z/test_cliffs build/bin/tests/z/test_commander
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_cliffs
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_commander
```

These cover the 153 tops/120 faces, blocked collision, the exact
zero-survives boundary, grenade spending/blast, tank damage, walkable
rubble, intact/destroyed save restoration, initial territory destinations,
exclusive vehicle assignments and boarding, hostile vehicle interception,
flag/fort acquisition and squad retaliation. Capture/fort unit fixtures
now isolate those rules from commander decisions and incidental flag
cheering on a cliff detour. Full `make`, all 24 Z suites (network menu
requires a local socket), smoke loads in all five worlds, and a native
level-1 screenshot were checked. Temporary diagnostic logs printed cliff
counts/blocked cells, placement destinations, boarding IDs and fort-entry
route positions, and were removed after diagnosis.

## Native computer scoring and campaign settings (2026-10-18)

This continues the cliff/commander investigation above. It supersedes that
section's **authored nearest-flag/fort commander and per-object vehicle
reservations**, and resolves the intelligence/allocation fields previously
left unknown. It is a port of the scoring and initial offensive-allocation
paths, not a claim that every branch of the retail commander is implemented.

The executable is the same local DOS/4GW LE image, SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
Addresses below are relocated image addresses. Radare2 remains unavailable
in this environment; the existing scratch C LE relocation loader and Apple's
libLTO i386 disassembler were used, checking instructions and relocated
pointers rather than assigning meaning from strings alone. No retail code
was executed and no external implementation was substituted. MULT.DAT's
fingerprint is recorded above. The existing native PAC decoder reads
WARDATA.PAC's **SQROOT.BIN**; entries 0,4,1023 are **1,2,32**. Its first
1024 bytes are nonzero divisors, rather than a floating-point square-root
calculation reconstructed by the engine.

### Confirmed settings and clock

`0x9d7de` calls the intelligence adjustment and initializes phase to 1;
`0x9b1e4` recalculates intelligence before every commander dispatch.
CPUPLR fields and their runtime destinations are:

| File offset | Runtime | Meaning established by consumers |
|---|---|---|
| 0x780 | 0x18d25c | Base intelligence, 0..127 |
| 0x781 | 0x18d25d | Reserve/allocation percentage in `0x9b679` |
| 0x89f | 0x18d37b | Multiplier for territory importance byte +9 |
| 0x8a0 | 0x103d79 | Default robot/fort manufacturing group (loaded by `0x410cd`, `0x41662`, `0x26e44`) |
| 0x8a1 | 0x103d7b | Default tank manufacturing group (`0x42785`, `0x26e5b`) |
| 0x8a2 | 0x103d81 | Previously verified production speed |
| 0x8a3 | 0x103d7d | Loaded setting; complete consumers/meaning still unknown |

Calling +0x781 "aggression" from the file alone was a disproven hypothesis:
**lower values make more of the army available for offensive tasks**.
The campaign pairs (intelligence, allocation), read from all 20 files, are:

```
01 55,40   02 50,50   03 60,40   04 60,50   05 70,50
06 70,50   07 75,40   08 70,40   09 75,40   10 80,40
11 90,40   12 80,50   13 95,50   14 85,50   15 90,40
16 90,40   17 110,30  18 127,40  19 127,40  20 127,50
```

All twenty have zero at 0x782..0x791. Territory weight is 10 except
LEVEL14 and LEVEL16, which use **0**. LEVEL01's CPU fort territory has
importance **40**; its other territories have zero importance.

`0x98280` computes `floor(100 * cpu_owned / (cpu_owned + human_owned))`;
neutral territories do **not** enter that denominator. At a share <=40,
`0x982c8` uses `min(127, base + floor(base/2))`, otherwise base.
`0x9d700` sets the heavy-pass countdown to `2*(127-I)`. Dispatch decrements
it and runs the heavy passes when it becomes negative, so their cadence is
**2*(127-I)+1 native frames**, not a guessed think interval. The existing
70 Hz PIT/main-loop evidence applies. Between heavy passes the native
commander still updates territory membership, local flag/fort orders,
observed force and some scores.

`0x980e2..0x98136` queues group orders with delay `2*(127-I)+tail`, then
increases tail by `127-I`. `0x9821d` replaces an existing queued order for
the same group; `0x9b335` resets tail at each heavy batch. The engine keeps
one pending order in each task and a native-frame clock, with an integer
70/RTS_TICRATE accumulator. The AI scheduler's new optional interval is 1
for Z and retains the existing default for other games. No millisecond
rounding or extra authored reaction delay is introduced.

### Confirmed estimates and strength values

`0x9830b` is a **pure estimate of a value**, not a new draw from the global
random stream. It saves the global 16-bit seed, temporarily replaces it
with the input's low word, draws twice through `0x70d37`, and restores the
seed. Intelligence 127 returns the input unchanged. Otherwise:

```
seed = low16(value)
sign = next(seed) & 0x100
roll = next(seed) / (floor(65535/(128-I)) + 1)
delta = floor(uint64(roll) * uint32(value) / 128)
estimate = value + (sign ? -delta : delta)
```

The inclusive random-range helper is `0x70e0e`. Known vectors are
`estimate(1000,55)=969`, `(1000,110)=993`, `(32768,55)=47360`,
`(65535,0)=45568`. `0x9836a` applies a second estimate when a territory's
+29 bit 0 is clear; radar-known territories skip this second distortion.

`0x983e0` differs from manufacturing-stock evaluator `0x98acc`.
The relocated robot and weapon weight-pointer tables at
**0x152a78/0x152ac4** select rows inside the file loaded to **0x152b10**.
Robot-row byte offsets are:

```
7,14,21,7,21,21,21,14,14,14,14,14,14,14,14,7,7,7,7
```

Weapon-row offsets are:

```
1c,3b,5a,79,c2,e1,100,11f,13e,15d,a3,17c,19b,1ba,1d9,1f8,217,236,255
```

The class index comes from runtime unit +157. Weight 255 omits a field
**and its denominator**; weight zero still counts in the denominator.
The seven human fields are runtime offsets **0,2,4,7,58,62,119**.
Spawn `0x5108c` maps ROBOTS record +28,+29,+30,+31,+32,+36 to these,
but **+29 supplies both runtime +2 and +62**. Weapon initialization replaces
runtime +4 with twice the byte-sized `(u16 weapon[2] >> 3)`.
Copying production's +33 into combat's +62 would be wrong.

For robot bodies (runtime categories 0,3,15,16), ten weapon fields are
`{8*range_byte,0,0,damage99,field117,field118,0,0,0,0}`. The six absent
properties still count when their weights are not 255. For vehicles/guns
these fields read the separate 44-byte driven-body record:

| Index | Body offset | Initial weapon source |
|---|---|---|
| 0 | +5, times 8 | low8(u16 +2 >>3), times 8 |
| 1 | +7 | +4 |
| 2 | u16 +8 | 25 * +5 |
| 3 | +6 | +32 |
| 4 | +32 | +34 |
| 5 | +33 | +35 |
| 6 | +38, times 8 | +36, times 8 |
| 7 | +35 | +37 |
| 8 | +36 | +38 |
| 9 | +37 | +39 |

**Disproven during this trace:** an incomplete read suggested body +35
retained the allocator's 255 fill. `0x50c23..0x50c26` explicitly copies
weapon +37 there. Boarding also copies object +27 to body +35 at
`0x63a73..0x63a76`. Neither path justifies an artificial 255 property.
`0x63a1d` copies the other empty-object properties into a driven body;
`0x6375a` dispatches vehicle initialization, rather than initializing a
world object. The earlier `0x5969f` lead is an object/effect allocation path,
not the weapon-property copier; that copier is **0x4a28a**.

The human component is the integer average. The weapon component
**multiplies by weapon weight +10 before dividing by the field count**
(`0x985fe..0x98613`), then adds **25 per runtime +149 grenade**.
A full-health level-1 Grunt is `floor(1045/6)+floor(270*2/3)=354`;
Gatling is **1469**, distinct from manufacturing stock **1468**.
Borrowing the stock literals for combat would change the algorithm.
The engine retains the native robot-record identity through spawning,
manufacturing and boarding. AI reads this metadata; gameplay HP, damage,
speed and cooldown remain the existing C configuration. Current body HP
is mapped proportionally to native initial health for scoring. Dynamic
APC driver-weapon swaps, native experience/stat changes and every combat
mutation of the raw retail fields are not yet represented by engine mobjs.

Empty objects use `0x9890b`, not a fake driver. Native object -> weight class:
`5->2,8->1,35->12,26->8,29->9,31->10,21->4,22->5,23->6,33->11,24->7`.
Its ten fields read object offsets
`{8*14,16,25*15,17,24,25,8*30,27,28,29}`; `0x4a28a` populates these from
weapon +2,+4,+5,+32,+34,+35,+36,+37,+38,+39. Unrecognized object types
return their type from the early exit (including Fire Truck 37), rather
than going through weights. A level-1 crewless Jeep has value **1138**.

`0x986dc` clears per-territory four-team u16 strength, visits every live
unit except Fire Truck/native type 38 and Crane/type 36, sums its entire
squad (`0x98376`), estimates it, locates that member's territory, applies
the radar-dependent second estimate, caps at 65535 and adds as a **word**.
The surprising result is confirmed by the loop: **the whole squad's value
is added once per member**, not once per squad. Nine full-health human
Grunts in three squads contribute **9558**, not 3186; the initial Gatling
adds 1469, giving 11027 at perfect intelligence. Task strength is instead
an estimate of the squad sum once (`0x9b69c`).

### Confirmed territory scoring and allocation

The 30-byte territory's runtime fields include strength words at +10..16,
flag coordinates +18/+19, scratch group index +20, alert +21,
lockout word +22, flags +24, signed value dword +25, radar bits +29.
`0x9d8a7` retains flags &0xe0, then sets bit 1 for the **first fort**,
bit 2 for the **second fort**, and other fort bits for multiplayer.
`0x4ec1c..0x4ed7c` establishes the mapping: first fort is the upper CPU
fort (blue entrance 0x15), second is the lower human fort (0x14).
An initial interpretation of bit 1 as the human fort was disproven by
this scan. `0x9da79` supplies building-related flag 0x10.

`0x987a4` refreshes a territory's value when phase bit 0 is set or radar
knows it. The heavy dispatcher toggles phase after its first observation
pass and can observe again. Value is:

```
sign_extend16(-human_strength)
+ 16 * CPUPLR[0x89f] * territory.importance
+ sum(estimate(empty_object_value))
+ sum(estimate(building_rating_at_+12))
```

Building and object membership use native top-left coordinates via
`0x9dc69`, with exclusive rectangle maxima. Buildings are included
regardless of side. Ratings are not inferred from sprite size or maker type.

`0x9a1dd` builds non-CPU, unreserved candidates (`0x9a33c`), adjusts distance,
stably sorts descending (`0x9a263`), then clears low six score bits
(`0x9a245`). Distance (`0x9a29c`) is from the **CPU fort territory's flag**,
not the acting unit or the player's fort. It subtracts flag coordinates
as signed bytes, arithmetic-shifts by 2, sums their squares, clamps the
index to 1023, and divides **only the score's low word** by SQROOT.BIN[index].
The high word survives unchanged. Emergency global 0x209d9e suppresses
that adjustment; emergency retargeting is not ported here.

The initial attack pass `0x99e3f` visits the whole stable ranking; it does
not call the tie selector for its first target. `0x9a126` gathers usable,
offensive, idle tasks, excluding tasks 4/5. `0x9a080` stably orders squads
by squared top-left distance to that territory. Required force (`0x9a054`)
is **1 when human strength is zero**, otherwise
**2*human_strength + territory_value**. `0x9a027` requires enough combined
squad strength before ordering any of that pool. `0x99f46` writes task 0
and the target/destination territory. Existing tasks 0,2,1,3 reserve their
destinations. Territory flag 0x20 bars vehicles; 0x40 bars Jeeps/APCs and
requires grenades for ordinary robot classes (the native APC cargo-class-3
exception remains unsupported). This replaces the former 8-cell fort
penalty and 2-cell proximity claim heuristic; neither was a retail rule.

The mobile budget (`0x97f50`, `0x97fad`, `0x9b679`) uses these integer rules:

```
if cpu_total >= human_total and cpu_total != 0:
    imbalance = (cpu_total-human_total)/2
# Otherwise the native absolute imbalance global is retained.
if home_human >= home_cpu:
    diff = home_human-home_cpu
    credit = diff + trunc((allocation-50)*diff/100)
    deficit = 0
else:
    credit = 0
    deficit = home_human + trunc((allocation-50)*home_human/100)
mobile = clamp(total*(100-allocation)/100 + imbalance-credit, 0,total)
mobile = max(0, mobile-max(0, deficit-(total-mobile)))
```

Home strengths sum territories with **asset flag 0x80**, not just a fort
(`0x98060`). Guns contribute to the candidate total only in first-fort
territory and cannot receive the usable/mobile flag. Native group order
is retained; a group gets flag 16 only if its strength fits the remaining
budget and flag 8 says usable. A group that does not fit is skipped, without
stopping examination of later groups. `0x9b9a1` returns former offensive
tasks 0..3 to CPU-fort staging when they become reserves; task 1 already
in the player's fort territory is kept offensive. That state and the
retained absolute imbalance are saved rather than recomputed from a
nearest-target order after loading.

### Confirmed observations and local objectives

`0x9b9fd` clears known bits and checks CPU-owned radar stations. Radius is
`min(25, building_effective_rate)*80` pixels; effective rate uses the
previously verified health/territory production formula. Flag **origins**
are compared by squared distance and fort territories are excluded.
There is a native stride peculiarity: `0x9ba72` advances **30 bytes**, while
`0x9ba0c..0x9ba1e` only accepts alignment to an **80-byte** building.
Within 35 iterations, only original building slots **0,3,6,9,12** are
examined as actual buildings. The runtime preserves original BUILD slot
identity and this checked behavior, instead of silently scanning all radars.

`0x98ee1/0x98f32` sends usable, offensive, idle robots to the nearest
eligible empty object. Discovery outside radar coverage requires a set
random low bit. A **task 5 reserves the entire territory** (`0x99a69`),
not each object independently. Native eligibility compares object IDs
5,8,21..25,29,31,33,35,37 and checks object flags; its list notably names
25 rather than 26. The engine applies that ID list together with its shared
Z_Enterable admission rule, which rejects wrecks; exact dead-object/property
flag handling remains a fidelity boundary.
The squad receives the normal boarding order and the new driven unit
retains the robot's identity for valuation.

`0x9b816` retries a local flag order when its signed-byte counter underflows,
then resets it to **100 native frames** after an order. It excludes forts,
CPU-held territory, Sniper/native type 4, and gun category 2. Its distance
test is **<16384 square pixels** from unit top-left to flag origin.
`0x9a49a` sends offensive mobile units in the player's fort territory to
fort origin +(80,72). Normal territory orders `0x9d42c` stage outside the
CPU fort at +(80,220) or the player fort at +(80,-80). `0x9d4f4` tries eight
**48-pixel** offsets clockwise from a random start, rejecting blocked or
already targeted cells. These authored native offsets replace no guessed
visual compensation. The engine uses its existing nav reachability and
formation helpers to execute orders; its movement, tactical notice radius
and combat animation behavior are not claimed to be instruction-level ports.

### Confirmed initial manufacturing choice

`0x9e33a` enumerates enabled groups by ascending requirement*1000, then
ascending group ID. After each append, `0x9e3e4` exchanges the last pair
if the new group's stock is lower; this is **not a full sort**. Fort choice
then fully sorts by stock (`0x9e435`). Initial class lists at
0x152ec9/0x152ed0/0x152ed6 are robot {0,3,15,16,17,18}, vehicle
{1,7,8,9,11}, and their union. The gun-selector list at 0x152ee2 is
{2,4,5,6}. `0x9e235/275/2b5/2f5` select:

```
index = floor(cpu_total * available_count / (cpu_total+human_total))
index += floor(index/4)
index = min(index, available_count-1)
```

The engine uses the native initial-family selector when an idle producer
needs a product, replacing fewest-owned-unit choice. It guards the empty
list/zero-strength denominator, which the native code assumes valid.
Later retail gun selection at `0x9de4a/9de9b/9def5` depends on the
producer's first-selection flag, reserve-adjusted territory value >=500,
existing gun count (<2 for factories, <4 for forts), and planned/stored
gun count (<4). `0x999e5` computes the reserve-adjusted territory value,
not raw army strength. `0x26ae5` counts territory guns and gun production;
`0x26b82` also counts stored guns and placement records. Those later
branches, four-slot native gun storage and retail random placement remain
unported; existing production repeat/release and gun placement are retained.

### Additional traced branches and explicit remaining scope

The following useful results are preserved for subsequent ports, rather
than being replaced with numeric approximations:

- `0x9a1f2` finds equal quantized top scores; for >1 it calls the inclusive
  random helper with **[1,tie_count]**, then uses that result as a zero-based
  index. This appears to skip the first tied entry and can select beyond
  the candidate count. It is **not** a normal [0,count-1] tie roll. Safe
  behavior for the native out-of-range case and the later retargeting
  passes remains unimplemented; the initial ranked attack pass needs no roll.
- `0x9a9f9` chooses an adjacent non-CPU territory by raw value (later index
  wins ties), gives the player's fort candidate score -10000, and falls
  back through `0x9aaf1` to nearest non-CPU flag if no adjacent candidate
  exists. Adjacency `0x9d61e` uses touching rectangle edges with inclusive
  overlap, including corners. A successful march installs lockout **1020**
  at `0x9aae7`. This native *local fallback* is distinct from the removed
  global nearest-flag commander and is not substituted for native scoring.
  **Correction (2026-11-06):** its only callers, `0x9a3c3` and `0x9a433`,
  are never called or referenced as data. The fallback and its 1020
  lockout are dead code in the retail executable and stay unported.
- `0x9c174` recaptures a non-CPU territory with nonzero lockout, positive
  CPU strength and zero human strength, selecting a usable local reserve
  not marked with task flags 2/4. `0x9cb5e` returns reserves to assigned
  home territories. Both are ported as of 2026-11-06 (below).
- `0x9d390` values a potential donor squad, caps it at 65535, and requires
  the donor territory's remaining own strength to be at least
  `16*weight*importance` with native word/sign checks. `0x9d330` chooses
  the largest eligible donor; `0x9d115` compares defending strength and
  adjacent donors. Defence and reinforcement are ported as of 2026-11-06
  (below); priority territories, alerts for attacked buildings, repairs,
  cranes, bridges and APC cargo remain unported.
- Native strategy is disabled by multiplayer dispatcher gates at
  `0x9b21b..0x9b23d`; the engine now schedules this single-player commander
  for team 1 only. It does not create new computer teams in multiplayer.
- The single-player front end leaves an incidental RNG seed. Engine AI
  deliberately starts from the retail network seed **0x3039** (previously
  established at `0x49c77`) for repeatability; there is no attempt to guess
  the front end's random history or share the cliff-decoration RNG stream.

Task records persist only stable unit IDs, squad IDs and territory indices,
not retail addresses or duplicate simulation objects. Save extra data and
consistency hashes include native clocks, phase, estimates, allocation,
RNG seed, task targets and pending orders. The expanded Z extra record
rejects older incompatible Z saves by its existing size check.

Reproduce the settings and numeric vectors with:

```
make build/bin/tests/z/test_commander
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_commander
```

The test also checks native radar slot behavior, the disadvantaged intelligence
boost, delayed-command save/replay, farther valuable territory preference,
force gating, initial manufacturing selection and boarding with one reservation
per territory. The previous test requiring the authored AI to take every flag
within 90 seconds was removed: its deadline and all-out allocation were not
native evidence. Full-suite/build verification is recorded with the commit.

Final verification: full `make -j4` succeeds without compiler warnings;
`env SDL_VIDEODRIVER=dummy make -j4 test-z` passes all **24 suites**.
Headless `--check` succeeds on LEVEL01/05/09/13/17, covering desert,
volcano, arctic, jungle and city. A LEVEL01 BMP was rendered and visually
checked after conversion to a temporary PNG; no converted runtime asset
or new image dependency was introduced.

## Retail main menu and multiplayer setup (2026-10-18)

### Provenance and method

The installed executable is still the Watcom DOS/4GW LE image described at
this document's start. SHA-256 fingerprints for this investigation:

- `ZED.EXE`: `3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`
- `WARDATA.PAC`: `3a17f168f1b7231be3bac5d324afb1311451aeb4da224b998754851aa035e035`
- `SPRITES.RSC`: `56b79cfaf489bdbcd9fd658c583d01ed31dd4e7b8575042b4fd8c216a3f5f6e8`

No retail execution or web reference was used. The previous temporary radare2
installation recorded in REFERENCES.md was absent, and no `r2` was available
on PATH. The existing ignored C LE/fixup reader `reverse/z-ui/examine.c` and
Apple LLVM's disassembler were the local fallback. Both code addresses and
fixed-up data pointers below use the relocated image addresses. Focused dumps
are `frontend.txt`, `main-draw.txt`, `multi-render.txt`, `multi-maps.txt` and
`net-render.txt` under ignored `reverse/z-ui/`; the durable conclusions follow.

### Confirmed image layout (and correction)

`WARDATA.PAC/MANE.LBM`, `MULTI.LBM`, `NET.LBM`, `HSCORE.LBM`, `DIRECT.LBM`
and `MODEM.LBM` all decode to 320x200 pictures with their own 256-entry CMAP.
These six members are **uncompressed eight-plane FORM ILBM**, 64850 bytes
apiece, not the chunky compressed PBM previously claimed in Z_FORMATS.md.
Their BMHD has width 320, height 200, planes 8, masking 0 and compression 0;
the BODY stores eight 40-byte planes for each scanline. This disproves the
old blanket PBM/ByteRun1 description; it must not be used to decode this UI.

The complete backgrounds already contain labels, frame furniture and dim
buttons. RSC overlays contain substantial surrounding background, rather than
just glyphs or button faces. Draw them at their native origins and restrict
input with independent hitboxes. Sampling artwork to guess a button's bounds,
recoloring states or centering overlays by their image dimensions is wrong.
Native font sprites must use the active screen's CMAP, as must the overlays.
The main-menu CMAP applied to multiplayer artwork produces visible noise.

### Confirmed main-menu controls

Entry `0x51d1e` sets the front-end clip to 320x200. `0x56896` loads
`MANE.LBM` through the string at `0x1487c9`; `0x565e0` is the shared screen
loader. `0x549c4` draws the base menu sprite 1129 at (16,24), and the two
Quit pieces 1134/1135 at (24,128)/(144,128).

The English control table begins at `0x146232`: each record contains four
u16 bounds (inclusive), then hover, left-click and secondary function pointers.
The labels are New Game, Load Game, Replay Level, Multi-Player and High Score.

| Control | Inclusive native bounds | Highlight image and origin |
|---|---|---|
| New Game | (35,58)..(100,69) | 1118 at (16,24) |
| Load Game | (35,72)..(98,83) | 1119 at (16,32) |
| Replay Level | (34,86)..(105,99) | 1120 at (16,48) |
| Multi-Player | (34,99)..(108,113) | 1121 at (16,64) |
| High Score | (34,113)..(91,127) | 1133 at (16,80) |
| Quit | (56,154)..(121,168) | hover: 2869/2870 at (24,136)/(144,136); pressed: 1136/1137 at (24,128)/(144,128) |

The one-pixel shared edges in the original table are retained through table
order. `0x54b58` onwards sets hover flags; `0x54c3c` onwards commits clicks.
`0x52062` dispatches New Game; `0x520a3` dispatches High Score;
`0x520b0` Load Game; `0x520bd` Replay Level. New Game reaches `0x566b1`,
then the level-start path `0x569c6`. The main menu is not a fabricated
"Single Player / Multiplayer" splash: New Game is its single-player entry.
`0x521be` loads `HSCORE.LBM`, with the dismissible OK control in the artwork.

`0x57002` draws the proportional front-end font: ASCII 33..90 starts at RSC
830; lower-case a..z starts at 888. `0x571c2` selects this bright set, and
`0x571d1` selects the corresponding set 84 images later. The 91 advance
bytes for ASCII 32..122 live at `0x11cc84`; space is explicitly handled as
an eight-pixel advance. They are committed as C literals in the menu loader.
This is different from the HUD's CHARS.BIN font.

Space needs a valid blank font cell as well as its advance. Leaving its
glyph index at -1 made the shared text renderer substitute `?` (confirmed by
temporary logging: index -1, advance 8, 84 cells). The menu font now has an
empty 85th cell for space, reproducing the native advance-without-drawing
branch; the regression test checks that it is valid and has no pixels.

### Confirmed multiplayer layout and map lists

`0x52411` loads `MULTI.LBM` via the pointer at `0x11cd66`; `0x52abf`
draws it. The English multiplayer controls start at `0x146310`. The four
rows are Link Type, Level Name, Number of Players and Player Name.
The displayed values start at (200,63), (200,79), (200,97), (200,112).
The map arrow hitboxes are (281,79)..(288,90) and (291,79)..(298,90);
player-count arrows are the same x ranges at y=95..106. The count wraps
2..4 in `0x55663`/`0x556f6`. Cancel, Configure, and Connect have bounds
(18,168)..(60,182), (114,168)..(167,182), (223,168)..(274,182).
Disconnect uses (205,144)..(274,159).

Base overlays: 3591/3592 at (24,24)/(144,24); Cancel 3597 at (0,152);
Configure 3768 at (80,136); connection-status pieces 6647/6648 at
(168,120)/(288,120); Connect 3762/3763 at (168,144)/(288,144); arrow-bank
3598 at (264,48); value-field 6653 at (197,59). The instruction at
`0x52b18` places 3769 at (80,256), outside the 200-line clip. It is **not**
evidence to move a right-hand cap to a guessed onscreen position. The
background already supplies the normal right-hand art.

Map and player hover overlays are 3040/3041 at (24,32)/(144,32), and
3042/3043 at (24,48)/(144,48). Their arrows have their own authored images
3602..3607. Cancel hover/pressed are 3595/3596. Connect hover is
3764/3765 at (184,144)/(304,144), pressed is 3766/3767 at those origins.
After connecting, `0x52f67` draws disabled Start 6912/6913 at
(176,144)/(296,144); enabled hover is 6640/6641, pressed is 6642/6643.

`0x55134` indexes three pointer-selected eight-byte map lists at `0x11cd35`.
The lists themselves start at `0x11cd41`, `0x11cd49`, `0x11cd51`:

- Two players: levels **26, 27, 28, 29, 31, 32**.
- Three players: levels **40, 38, 22, 37, 39**.
- Four players: levels **33, 35, 24, 23, 21**.

0xff terminates each list. `0x53102` uses the same selection and indexes
LEVELS.DAT with a 240-byte stride to display the level name. These are the
16 maps exposed by the implementation, in native order. Showing every map
folder or silently using LEVEL01 for multiplayer would be incorrect.
`0x53247` opens `NET.LBM`, whose native options are Battle Group and Network
Type. Native serial/modem/IPX/NetBIOS transports are separate paths, not the
engine's UDP LAN protocol.

### Implementation boundary and remaining unknowns

The game's menu is an ordinary `menuitem_t` table driven by the engine,
following Dark Colony's ownership and lifecycle. Native RSC pictures are
loaded into menu-owned sheets, outside gameplay sprite IDs. IFF decoding and
integer scaling/centering live in the engine; no PNG asset, generated icon,
Python tool, native-format renderer callback or extra input loop was added.
The fit/letterbox policy is an explicit engine presentation choice, not a
claim about DOS pixel aspect correction.

New Game starts LEVEL01. Replay exposes installed campaign levels 1..20;
retail progress-based unlocking remains **unknown** and is not claimed.
Load Game reads verified open-rts `.sav` metadata in the user's Z directory
and schedules the driver's existing load operation; retail SAVE*.Z conversion
is not implemented. The High Scores background can be opened and dismissed;
retail HISC.DAT score rows remain undecoded and are not synthesized.
In-level Escape retains the existing engine Resume/Quit panel; native pause
and option screens are outside this front-end change.

The Multiplayer screen retains the retail fields, art and map/count lists.
Connect hosts the selected map and becomes Start when the roster fills;
Disconnect and Escape cancel the session. **Engine adaptation:** Configure
opens LAN discovery in the same native shell, with the level field offering
discovered host names and Connect joining the chosen game. Configure refreshes
discovery there. The transport, setup publication, peer count, launch and
error handling use `hud/m_net.c`; no retail modem, IPX or NetBIOS protocol is
emulated. Tests verify menu-driven host launches at all three player counts;
this is not evidence that Z's multiplayer simulation already matches retail.

### Reproduction

```sh
make
make build/bin/tests/z/test_menu build/bin/tests/z/test_menu_items build/bin/tests/z/test_images
env SDL_VIDEODRIVER=dummy OPEN_RTS_Z_UI_SHOTS=/private/tmp build/bin/tests/z/test_menu
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_menu_items
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_images
env SDL_VIDEODRIVER=dummy build/bin/z --check
env SDL_VIDEODRIVER=dummy build/bin/z --screenshot /private/tmp/z-main-menu.bmp
# Explicit map bypasses the front end:
env SDL_VIDEODRIVER=dummy build/bin/z data/Z LEVEL01/LEVEL01.MAP --screenshot /private/tmp/z-level01.bmp
```

The menu test checks original background/palette pixels at 2x, 4x rendering,
retail mouse hitboxes, keyboard navigation, replay/load selection, pause
resumption, LAN browsing, cancellation and multi-process host launch. The
shared image test covers uncompressed/ByteRun1 planar and chunky rows,
padding, palettes, truncated files, oversized chunks and runs crossing rows.

Final verification: full `make -j8` completed without warnings; all **24 Z
suites passed**. Native menu tests exercised save selection, LAN discovery
and joining, and two-, three- and four-player host launch. Dark Colony's
`test_menu --quick` and shared menu-item suite passed. Z `--check` loaded
252 objects and 512 terrain tiles. The game-generated main-menu screenshot
and the menu-test multiplayer, Start, join-browser and 4x views were inspected,
including a final rerun after correcting the blank space glyph.

## Commander passes and unit statistics (2026-11-06)

Static reading of the same ZED.EXE (SHA-256 above) with a scratch Python
LE loader (fixups) and Capstone; nothing retail was executed. This adds the
heavy-pass branches the previous section left unported and replaces the
authored unit statistics.

### The heavy pass, in order

`0x9b1e3` runs every native frame: lockouts count down (`0x9cb40`), then
`set_unit_territories`, `attack_that_fort` (`0x99086`) and
`send_robots_to_flags`; everything after `reset_intel_delay` only runs when
the intelligence countdown (`0x152f24`) goes negative. That heavy part is,
in order: observations, `monitor_home_territories`/`adjust_for_unit_balance`
/`split_units` (ported before), `aggressive_opponent` (`0x98d58`), then
dead buildings and bridges, bored cranes, priority territories, buildings
under attack, `recapture_lost_territories`, `clear_recapture_flags`,
`check_CPU_player_units`, `check_units_homes`, repairable vehicles, unit
targets, repair orders and `check_defend_territories`.

Task record fields (22 bytes at `0x18d388`): +0 current territory, +4 target
territory, +8 strength, +0xc destination, +0x10 flags (1 cleared each
frame by `0x9dc00`, 2 recapturing, 4, 8 usable, 16 mobile), +0x11 task,
+0x12 frontier counter, +0x14 idle counter, +0x15 active. Task *i* goes
with group *i* (72 bytes at `0x1943f8`; +0xc is the group's home
territory, -1 until a defence donation or a stray-unit reroute sets it).
Task values established by their writers:

| Task | Meaning | Writer |
|---|---|---|
| -1 | idle | many |
| 0 | marching on a destination | `0x99f46`, `0x9959d` |
| 1 | at the destination, taking its flag (or entering the human fort) | `0x9989a`, `0x99d42` |
| 2 | waiting in a territory beside the destination | `0x997cb` |
| 3 | assaulting the destination | `0x99751`, `0x996f0`, `0x99813` |
| 4 | holding (garrisoning) a territory, target in +4 | `0x999c2` |
| 5 | going for an empty vehicle or gun | `0x99077` (ported before) |
| 10 | moving up to the frontier | `0x98e33` |

`aggressive_opponent` calls, in order: `0x9a1dd`+`0x99e3f` (ranking and the
first attack, ported before), then:

- `0x993ff`: for each marching task, if the strength bound for its
  destination (`0x995de`: usable+mobile tasks not idle/4/5) is below the
  required force (`0x9a054`), all of them become task 0 and `0x99e60` looks
  for more: the idle pool (`0x9a126`/`0x9a080`) must reach the *whole*
  requirement alone, or with the one garrison whose territory has the
  lowest value (`0x9937f`, last of equals) if that value is no more than
  the target's. Failing that, every task bound there falls back
  (`0x994d3` → `0x99502`: re-stage in its current territory, or stop where
  it is if barred there, and clear). An attack that has enough lets go of
  the tasks beyond the requirement, in table order (`0x9942a`), except
  against the human fort.
- `0x998c5`: a garrison leaves when its territory is lost or its
  reserve-adjusted value (`0x999e5`, below) drops under **500**.
- `0x9962c`, per offensive task: `0x99818` notices a captured destination
  (task -1, then `0x99910` garrisons it with idle forces standing there when
  its value is >= 500 and nobody holds it, assigning until the
  reserve-adjusted value is used up) or an arrival (task 1 and an order onto
  the flag at flag*16+4, one task per territory). `0x99abd` is the fort
  entry ported before. `0x99708`: a marching task waits next to the
  destination (task 2, staged in its own territory) while another task is
  still marching on or taking it; once the waiting strength reaches the
  requirement, everyone marching or waiting assaults together (`0x996c1`);
  a lone task beside the destination assaults at once; waiting tasks go in
  as they are once nobody is still marching (`0x997d5`). Adjacency is
  `0x9d61e`. `0x9a534` and `0x9a801` (buildings in the territory) are not
  ported.
- `0x993c7` drops a vehicle task whose territory has nothing left to board;
  `0x991c4` retires tasks idle for three heavy passes — tasks 2 (waiting)
  are exempt, which the port previously got wrong.
- `0x99149`: the first CPU-owned non-fort territory whose reserve-adjusted
  value is >= 500 and above its guards (`0x99976`) gets enough idle offensive
  force to make up the difference, as task 4. Only that territory, each pass.
  `0x99976` counts task-4 guards of the territory **and every gun anywhere**
  (the class-2 test skips the territory check); the port keeps that.
- `0x98ee1` vehicles (ported before), then `0x98d95`: an idle offensive task
  whose territory borders only CPU territories, with its group still and no
  queued order, after two such passes marches to the flag (+8) of the first
  CPU non-fort territory that borders someone else's (task 10).

`0x999e5`: `value * (100 - 100 * mobile / (mobile + reserve)) / 100`,
with the mobile budget at `0x152f3c` and the rest at `0x152f34`
(`0x9b775..0x9b79a`). A negative value is treated as zero in the port.

Lockout: a taken flag sets territory +0x16 to **510** (`0x79e69`); `0x9cb40`
counts it down once a frame and `0x79e04` refuses a capture while it is above
467. `0x9c174` recaptures while it is nonzero (above). The port counts it
from observed ownership changes; the existing 18-tic flag hold already
covers the capture refusal.

`0x9cb5e`: a reserve with a home, outside it, whose territory holds no other
team's strength, goes home through `0x9d42c`. Cranes (36), Fire Trucks (38)
and units in some actions are skipped; guns go through `0x651e2` (not
ported, they cannot move).

`0x9d115`: `0x9dc25`, called when a robot acquires a target in reach
(`0x581e1`, `0x782d4`, `0x758a9`...), lists the target if the robot is a
computer unit whose task is not mobile; `0x9dc00` clears the list each frame
before the robots update. For each listed enemy in a territory not its
team's where human strength >= CPU strength: the adjacent territory with
the most CPU surplus (CPU - human, signed 16) that can spare a squad
(`0x9d330`: the most valuable squad, by unestimated `0x983e0`, capped 65535,
whose loss leaves at least `16 * weight * importance` there) sends it
(`0x9d23c`, recording the donor's home); failing that any reserve in a
territory of no greater importance and enough surplus; failing that the
reserves in the threatened territory fall back to their home, or to the
nearest other own territory (`0x9cf33`). The port rebuilds the list from
computer reserves' current targets on the heavy pass.

Not ported from this dispatcher: buildings and bridges, cranes, priority
territories (`0x9be16`), buildings under attack (`0x9bf90`), stray units
(`0x9cc83`), repairs and unit-target checks.

### Unit statistics

Spawn `0x5108c` copies a robot record's +0x1c strength (melee damage
>> 3, `0x613a4`), +0x1d accuracy (aim scatter, driver kills), +0x1f speed
factor and **+0x20 hit points** (runtime +0x3a/+0x3b; damage `0x86d04`).
Weapon setup `0x50df1` gives robots damage (+0x20), range (u16 +2 >> 3, in
8-pixel units), fire rate (+0x22) and shot speed (+0x23). Vehicles and guns
get a 44-byte body (`0x50bfb`): hit points **25 * weapon +5** (u16 +8),
speed weapon +4 (+7), damage (+6), fire rate (+0x20), shot speed (+0x21).

- Range in pixels is 8 * (u16 weapon +2 >> 3); `0x75a0a` compares squared
  pixel distance, acquisition uses SQROOT.BIN.
- Unit +0x78 counts down once a step (`0x6056d`); every fire routine fires
  at 0 and reloads **100 - fire rate** steps (`0x60c4a`, `0x75ec2`,
  `0x58bb8`, `0x26ea6`). The fire animation can make the real interval
  longer: Grunt and Sniper about 4 steps at least, rate >= 92 (Psycho)
  uses action 0x39 (2 steps). The Pyro pauses 16 steps after 4 bursts.
- Speed level = terrain term / 10 + unit speed / 10, clamped 0..12
  (`0x79882`; terrain bytes at `0x1157ea`, not decoded per terrain). Each
  level walks a repeating pixels-a-step pattern (`0x7bf10`, table
  `0x117e5c`) averaging 1/5, 1/4, 1/3, 2/5, 1/2, 2/3, 4/5, 1, 4/3, 3/2, 2,
  7/3, 5/2.
- Bullets (`0x5af81`) lose **5** (`0x103f49`) against anything that is not
  a robot class, leaving 0 or 1 at random when nothing is left
  (`0x5ae44`). A bullet that hits a Light Gun or a Jeep rolls
  `rand & 0x7f` against the shooter's accuracy (halved against guns); on
  success the driver takes the shot and an emptied vehicle stands for
  anyone to take (`0x5ad72..0x5ae0d`, `0x860fd`). Type 0x1b and bodies with
  +0x19 = 3 are also eligible; neither is identified.
- Shells, rockets and grenades (`0x59ed1`/`0x5a205`/`0x5a39e`) burst at full
  strength, without armour, on every enemy whose top-left is in a 44x32
  box (`0x85ce4..0x85e9f`); class-0 robots throw strength 50 (`0x103f47`).
- A hit taking a body under 3/4, 1/2 or 1/4 of its hit points scales its
  speed and fire rate by 90%, 85% or 80% (`0x86d55`). A moving vehicle
  knocks down robots in a [-8, +16] pixel box (`0x586a0`).

Decoded values (plain ground; cooldown in 70 Hz steps):

| T | Type | HP | Dmg | Range px | Cooldown | Speed level | Accuracy |
|---|---|---|---|---|---|---|---|
| 0 | Light Gun | 100 | 75 | 120 | 40 | - | 60 |
| 1 | Medium Tank | 200 | 80 | 120 | 30 | 4 | 80 |
| 2 | Grunt | 30 | 5 | 120 | 20 | 4 | 60 |
| 3 | Tough | 100 | 40 | 120 | 20 | 3 | 40 |
| 4 | Sniper | 50 | 10 | 144 | 20 | 5 | 120 |
| 5 | Heavy Tank | 300 | 120 | 144 | 40 | 3 | 80 |
| 6 | Mobile Missile | 300 | 120 | 160 | 80 | 2 | 80 |
| 7 | Psycho | 50 | 2 | 120 | 2 | 4 | 50 |
| 8 | Light Tank | 100 | 50 | 120 | 20 | 5 | 80 |
| 9 | Missile Launcher | 100 | 100 | 160 | 70 | - | 60 |
| 10 | APC | 200 | 5 | 120 | 20 | 5 | 60 |
| 11 | Gatling | 50 | 6 | 120 | 3 | - | 50 |
| 12 | Howitzer | 75 | 100 | 200 | 90 | - | 60 |
| 13 | Jeep | 50 | 4 | 120 | 2 | 6 | 40 |
| 14 | Fire Truck | 200 | 20 | 120 | 2 | 5 | 60 |
| 15 | Crane | 300 | (120, never fired, `0x58bea`) | 120 | - | 5 | 60 |
| 16 | Truck | 75 | 0 | 0 | - | 6 | 60 |
| 17 | Pyro | 80 | 20 | 120 | 2 (+16 every 4) | 4 | 80 |
| 18 | Laser | 60 | 15 | 136 | 20 | 5 | 40 |

Accuracy is the first robot's (drivers' for vehicles). `games/z/units.inc`
now holds these values in native units and `g_game.c` converts them; the
bullet armour and the Jeep/Light Gun driver kill are in `Z_HitDamage`
(`p_capture.c`). The random coin is a deterministic hash of the tic and the
two units, not the native stream.

Engine boundaries, **not** native equivalence: direct weapons still hit
instantly and never miss (native bullets travel and scatter by accuracy),
so fast weapons (Psycho, Gatling, Jeep) are more reliable than retail;
shells do not splash; damage slow-down, vehicle crush, the tanks' apparent
secondary gun (weapon +0x25..+0x27: 4 damage, rate 80; inferred only),
terrain speed terms, the Pyro flame (`0x5a099`) and the Fire Truck
(`0x59ffb`) are not ported.

Reproduce: `make build/bin/tests/z/test_units build/bin/tests/z/test_commander
build/bin/tests/z/test_ai`, then run each with `SDL_VIDEODRIVER=dummy`
(`AI_TRACE=1` on `test_ai` prints the computer's course each minute).
