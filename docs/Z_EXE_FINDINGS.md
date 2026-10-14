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
| 0x2aade | `draw_flags`: sprite 480 + [0x103d07] over shadow 484 + [0x103d07] at the flag cell, team colour by remap; [0x103d07] steps every second tick (0x19e81); sorted 32 px lower |
| 0x79ddc | Terrain 0x23: a robot on the flag cell, or a vehicle (class 1) with the flag in its 2x2 cells (0x79936), takes the territory unless its lockout is above 0x1d3; buildings follow through their territory pointer |
| 0x62e3c | The robot that took a flag turns south and cheers (action 0x21, sprites 1959..1962, seven times) |
| 0x61022 | Robots walk to the empty vehicle or gun they are sent to; 0x7af62 deletes the object within 20 px and turns the robot's record into the vehicle (type kept at +0x8a) |
| 0x61821 | A vehicle whose driver dies with hit points left goes back to an empty object; a destroyed one leaves its wreck |
| 0x79b25 | Terrain 0x24: a Grunt, Pyro or Laser (classes 0, 0xf, 0x10) within 16 px of a crate, whose squad holds no grenades, takes 10 (type 9) or 20 (type 10) for the squad, the remainder to the first member (0x79bea), at most 127 |
| 0x75939 | A robot with grenades throws at vehicles and guns (Lasers fire their laser), action 0x31 (0x6360e: 2522 + direction * 4 + frame); attacking a building needs grenades (0x753bb) |
| 0x636a0 | The grenade leaves on the throw's last frame; 0x5a205 launches it, drawn as 1336..1339 (0x59538); it bursts through 0x8476c with strength 0x32 for a Grunt |

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
  1464), fire truck and truck drawing, the robots' other actions, and the
  meaning of most unit and ROBOTS.DAT fields.
- Per-team terrain rows and the other unit classes' tables.
- The byte per sprite after the SPRITES.RSC offsets, and each sprite's row
  bytes.
- `BRIDGEnn.DAT`, the computer player settings in CPUPLRnn.DAT, `MULT.DAT`,
  the five unknown bytes of each object record.
- `DOOR.PAC`, `HEADFX.PAC` and `SHEADFX.PAC` members.
- Robot hospital and ICBM launcher sprites (no retail level uses them).
- Audio sample rates, the HUD and the front end.
