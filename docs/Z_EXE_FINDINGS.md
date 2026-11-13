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
| 0x79b25 | Terrain 0x24: a Grunt, Sniper, Psycho, Pyro or Laser (runtime classes 0, 0xf, 0x10) within 16 px of a crate, whose squad holds no grenades, takes 10 (type 9) or 20 (type 10) for the squad, the remainder to the first member (0x79bea), at most 127 |
| 0x75939 | A robot with grenades throws at vehicles and guns (Lasers and Tough use their own weapons), action 0x31 (0x6360e: 2522 + direction * 4 + frame); class-0 and Laser building attacks use grenades (0x753bb), while Tough uses its launcher (0x752bc) |
| 0x636a0 | The grenade leaves on the throw's last frame; 0x5a205 launches it, drawn as 1336..1339 (0x59538); it bursts through 0x8476c with strength 0x32 for a Grunt |

**Flag sampling correction:** `0x796cb..0x796f1` chooses the map cell at
native unit origin +(8,8). For runtime class 1, `0x79936..0x79964` also
checks the cells to its right, below and diagonally below-right. The engine
previously started that 2x2 window at the loaded sprite centre. Composed
vehicles use a 16x16 spawn extent, but Fire Truck uses its 32x32 native
sprite, so its centre is eight pixels farther right and down than the native
sample. `on_flag` now derives the sample from the native origin for all
mobile units. `test_capture` checks the exclusive 7-pixel and inclusive
8-pixel boundary for a Grunt, Jeep and Fire Truck. Inspect with `r2 -q -e
bin.cache=true -c 'pd 12 @ 0x796cb' -c 'pd 25 @ 0x79936' -c q
data/Z/ZED.EXE`.

**Stationary guns can capture:** the common active-unit pass
`0x6040c..0x60412` reaches `0x60568` and calls the terrain dispatcher unless
unit +0x67 bit 0x40 is set. `0x7983f` applies the four-cell extension only
to class 1; terrain 0x23 maps through `0x11580d` to table entry 6 at
`0x14ad30`, the flag handler `0x79ddc`. That handler has no mobile-only
gate, and its helper `0x79f5f` explicitly recognizes a class-2 taker. The
prior engine mobile-only gate therefore incorrectly excluded all manned
guns. All four gun types now capture from their single native sampled cell,
not the vehicle's 2x2 probe; neutral unoccupied guns remain ineligible.
`test_capture` checks each gun at both sides of the sample boundary and
verifies the neutral case. Ordinary gun placement restricts flag terrain, so
this principally matters when a later terrain or flag rebuild places 0x23
under an existing gun. The engine still approximates native +0x67 bit 0x40
with its existing live/team/traits eligibility. Reproduce with `r2 -q -e
bin.cache=true -c 'pd 16 @ 0x6040c' -c 'pd 18 @ 0x60552' -c 'pd 16 @
0x79833' -c 'px 1 @ 0x11580d' -c 'pxw 4 @ 0x14ad30' -c 'pd 38 @ 0x79f3f'
-c q data/Z/ZED.EXE`.

**Flag terrain and territory dispatch, confirmed and implemented:** `0x2a89f` writes
terrain 0x23 at each active flag location, preserving bit 7, and is called
again during capture at `0x79e7b`. A subsequent map effect can overwrite
that terrain until a later flag rebuild or restoration. In addition,
`0x79df1..0x79dfe` resolves the territory from the capturing unit's native
origin, not from the specific neighboring 0x23 cell. Open-rts now stamps
flags before loading OBJECT effects, dispatches from sampled terrain, selects
the origin territory and restamps active flag markers after capture. A crate
on a flag saves 0x23 and temporarily replaces it with 0x24; picking it up
restores 0x23, while capturing elsewhere can first overwrite its marker again.
The territory owns the current team and capture hold even if it has no flag;
its team, hold and mutable options are saved and hashed. `0x2a950` creates
a flag and clears the low option bits for a fallen fort territory; the engine
now creates an ordinary flag mobj in that case. The existing 18-tic hold is
still a 30 Hz approximation to the native 70 Hz counter, and terrain rebuilt
over a cliff remains subject to the existing cliff reconstruction limit.
`test_capture` covers an adjacent-territory vehicle,
a flagless captured territory, overlapping crate restoration, another capture
restamping over a crate, and save/load; `test_fort` checks flag creation.
Inspect with `r2 -q -e bin.cache=true -c 'pd 40 @ 0x2a89f' -c 'pd 45 @
0x79ddc' -c 'pd 55 @ 0x2a950' -c q data/Z/ZED.EXE`.

**Multiplayer flag availability:** `0x2a8c7..0x2a8ef` skips a territory's
flag when option bits 0 or 1 are set; it also skips bit 2 in matches with
more than two players, and bit 3 in matches with more than three. The word
at `0x103d7f` supplies the participant count; `0x49e3e` stores it and enables
that many player slots. Open-rts now applies the same gate when creating
flags, rebuilding their terrain and checking flag destinations for detours.
It retains the configured match count in saved and hashed level state, so a
reload or later player departure does not silently change the marker set.
`test_capture` checks the actual 2/3/4-player initialization, startup
objects and terrain, rebuild after a capture, and save/load for all four
option cases. Inspect with `r2 -q -e bin.cache=true -c 'pd 36 @ 0x2a8bc'
-c 'pd 16 @ 0x49e3e' -c q data/Z/ZED.EXE`.

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

**Network fort falls:** `0x11ea5` allocates a per-fall record and
`0x11dfa` advances up to four independently. On its first update, the
multiplayer branch at `0x1284b` calls `0x19d9a`, which marks the defeated
team inactive, decrements the active-player word at `0x11d436` once, and
calls `0x60fe6` for every active unit of that team. A fall started with
more than two active players locates the fort territory with `0x9dc69`
and `0x2a950` plants a flag owned by the entering team. `0x1241f` rejects
new falls once fewer than two remain; `0x11dfa` ends the game when a queued
fall finishes and fewer than two remain. Open-rts queues concurrent
fort-owned fall timers, transfers the territory for a capture with more
than two standing forts, eliminates each defeated team on the following
fall update, and delays its local outcome until a fall finishes. It uses
the existing saved fort state and assigns a draw when simultaneous falls
leave no standing fort; native zero-survivor presentation is not established.
Counting forts assumes one fort per active team and does not model a team
without a fort. The original four-record capacity, ordering of simultaneous
captures and the exact flag allocation failure path are not reproduced.
`test_fort` covers cross-captures, parallel falls, territory transfer,
continuation with two survivors, local victory/defeat and save/load.
Inspect with `r2 -q -e bin.cache=true -c 'pd 55 @ 0x11dfa' -c 'pd 40 @
0x11ea5' -c 'pd 40 @ 0x19d9a' -c 'pd 15 @ 0x1241f' -c 'pd 35 @
0x12024' -c 'pd 45 @ 0x2a950' -c q data/Z/ZED.EXE`.
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
| 0x58745 | Vehicle contact with opposing robots: one with +0x3a above 10 loses 10 and is knocked down (0x58910), the rest are killed (0x60fe6, then 0x61560 and 0x777d9 take it off the map and out of its group). Previously described as a blast; corrected by the action-9/10 dispatch trace below. |
| 0x58910 | Knockdown: action 0x17 (sprite 315) facing directions 6, 7, 0 and 1, else 0x2e (316); direction and frame reset |
| 0x61f43 | Actions 0x17 and 0x2e hold while a class-1 vehicle in the list at 0x18b81c is within 0x18 px, then become 0x18 (319..329) or 0x2f (365..375); the previous identification as an explosion list was wrong (see vehicle contact trace) |
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
finished guns with these same checks; its later native-priority correction
is described below. No branch depends on the local console player,
preserving lock-step behavior.

**CPU placement priority, implemented with engine validation:** `0x430b2` calls
`0x9df4f` for the single-player CPU. When the territory record +0x18 bit 0
is set, that routine first probes six fort mounts using the level-indexed
six-byte sequence at `0x11d1a3`, the permutation at `0x11d299`, the mount
offsets at `0x145088`, and `0x4e8be`. This order is a separate table from
the player starting-gun table at `0x11d0ad`. Otherwise, or if all six
mounts fail, `0x9e1ab` scans the territory's min-inclusive/max-exclusive
rectangle in row-major order for the first usable terrain marker, checking
classes 0x2d, 0x35, 0x36 and 0x37 in that order. A successful search
clears that marker to its high bit immediately, even when the caller later
continues to another marker class. After each success `0x9e048..0x9e0d9`
uses `(0x70d37() & 127) < [0x208120]` to stop; otherwise the last successful
candidate wins. `0x982c8..0x982ff` sets that threshold to the CPU's
effective intelligence, including its separately documented boost. No
successful candidate makes the routine return carry instead of falling back
to arbitrary ground. `0x9e13c..0x9e14a` subtracts (12,12) from the chosen
point, then another (4,4) for weapon classes other than 2. The marker scan
validates the marker point through `0x4e6b7`, which has a separate fort
snapping path, before the final spawn conversion.

The engine now uses the level's CPU mount order, then searches and consumes
the four classes in that sequence using its saved and hashed AI random
stream. It validates the marker point with its existing `Z_GunPosition`,
then applies the native sub-cell origin to the spawned ground gun. The
random stream is separate from retail's global stream. The engine uses an
owned fort object in the producer territory and its existing placement and
collision checks; their exact native equivalents and marker points that snap
to a fort are not fully reproduced. It also rejects a ground gun whose
post-conversion sprite footprint leaves the map or overlaps another selectable
object; native behavior for the shifted footprint has not been established.
Its product handoff timing is also an
approximation. `test_production` covers level-1 fort order, missing markers,
all four marker classes, early stopping, Light Gun and turning-gun origins,
and a save/load before placement. Inspect with `r2 -q -e bin.cache=true
-c 'pd 110 @ 0x9df4f' -c 'pd 36 @ 0x9e1ab' -c 'pd 30 @ 0x9e0f7' -c
'pd 25 @ 0x982c8' -c 'px 36 @ 0x11d1a3' -c q data/Z/ZED.EXE`.

**Explicit engine policy:** production holds one completed gun at the
head of the shared queue until placement. Retail `0x4317f` instead shifts
four built-list bytes at building +75..78, and retail AI `0x9df4f` has
additional placement priorities and random choices. At the time of this
investigation the engine did not reproduce that four-gun storage or those
AI priorities; storage is now implemented as described below. Mobile-unit
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

Engine limits, explicitly **not claims of native equivalence**: some direct
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
requires grenades for Grunt, Sniper, Psycho, Pyro and Laser (the native APC cargo-class-3
exception is now supported as described below). This replaces the former 8-cell fort
penalty and 2-cell proximity claim heuristic; neither was a retail rule.

**Restricted-territory commander correction:** the idle attack pool at
`0x9a165..0x9a1bb` rejects vehicles when territory +0x18 bit 0x20 is set.
For bit 0x40 it rejects Jeeps and rejects APCs unless their +0x8b is class 3.
APC setup `0x638ac..0x638af` copies the boarding robot's class +0x79 into
that byte; class 3 is Tough. For classes 0, 0x0f and 0x10, the same attack
pool calls `0x7a776`, which totals grenade bytes of occupied squad slots,
and rejects a zero total. The general dispatch guard `0x9956a`, used by
`0x9d42c` and frontier selection, applies the grenade test but does not
apply the attack pool's APC/Jeep exclusion. The engine now follows these
distinct checks, using its saved driver type and live same-team squad
members. **Correction:** an earlier conclusion treated weapon types 17 and
18 as runtime classes and incorrectly excluded Snipers and Psychos from this
gate. The constructor branches `0x50e1a..0x50e2e` assign both runtime class
0, so both require a squad grenade just like a Grunt. `test_commander` runs actual territory decisions for an
empty squad, a grenade held by a follower, an enemy with the same squad
number, Tough- and Grunt-driven APCs, Sniper and Psycho, and the separate
vehicle prohibition. It also checks initial general dispatch with a Jeep,
APC, Grunt, Sniper and Psycho, including both empty and armed specialists.
When `0x9956a` rejects a mobile unit, `0x9d42c` branches to `0x9d4c2`:
it queues a command at that unit's current native origin instead of leaving
its previous order active. Class-2 guns return before this branch. The
engine queues a stop at its corresponding current position; the commander
test checks both the queued point and cancellation of a pre-existing move.
Reproduce with `r2 -q -e bin.cache=true -c 'pd 45 @ 0x9a165'
-c 'pd 32 @ 0x9956a' -c 'pd 18 @ 0x9d4c2' -c 'pd 20 @ 0x7a776' -c 'pd 8 @ 0x638ac'
-c q data/Z/ZED.EXE`.

The same distinction applies to grenade pickup: `0x79b9e..0x79bae` accepts
runtime class 0, 0x0f or 0x10. Sniper and Psycho therefore can order, collect
and throw grenades; Tough is class 3 and cannot collect them. The pinned Zod
`zrobot.h` and `rsniper.cpp`/`rpsycho.cpp` also allow their grenade pickup
and throwing, whereas `rtough.h` disables it. Open-rts now uses the corrected
class mapping for crate commands, automatic pickup and both commander gates.
`test_capture` exercises each specialist's actual pickup and vehicle throw,
and confirms Tough cannot collect; `test_commander` covers both specialist
types with and without grenades. A Tough can still receive grenades when a
mixed squad shares a crate, but `0x7594f..0x75953` selects its launcher
action before the vehicle-target grenade check; the test verifies a shell
and unchanged ammunition in that case. Its separate building-order path is
unchanged. Inspect `r2 -q -e bin.cache=true -c
'pd 16 @ 0x50e1a' -c 'pd 30 @ 0x79b8b' -c 'pd 24 @ 0x9a19e' -c
'pd 24 @ 0x9957c' -c q data/Z/ZED.EXE`.

**Tough structure attacks:** `0x75297..0x752bc` dispatches a coordinate
attack by runtime class: class 0 and Laser reach the grenade path at
`0x753bb`, whereas class 3 (Tough) proceeds to launcher action 0x13 at
`0x752c3` without checking its grenade count. Action table entry 0x13 at
`0x149084` points to `0x60c2a`, which calls the Tough launcher path
`0x60c9d`. The former engine gate incorrectly required grenades for every
robot to attack a building, bridge or cliff, and could send a Tough holding
a shared grenade through the grenade action. Tough now uses its shell for
those targets regardless of grenade count. `test_capture` runs a launcher
shot into an enemy factory with no grenades and checks the case where one
is held; `test_cliffs` orders and flies a Tough shell into a cliff, then
restores a save containing that cliff. Reproduce with `r2 -q -e bin.cache=true
-c 'pd 28 @ 0x75297' -c 'pxw 4 @ 0x149084' -c 'pd 38 @ 0x60c2a'
-c 'pd 35 @ 0x60c9d' -c q data/Z/ZED.EXE`.

The nearby Pyro branch is distinct: `0x752ff..0x7537c` tests the target's
terrain through `0x193f0`. With no squad grenades, that routine indexes
`0x117214`, whose entries for terrain 0..66 contain no value 2; the branch
therefore selects flame action 0x41 rather than the grenade path. Ground fire
and its building damage are still incomplete in the engine, so this finding
does not by itself justify treating its existing full-damage instantaneous
hit as equivalent building damage. Inspect with `r2 -q -e bin.cache=true -c
'pd 50 @ 0x752ff' -c 'pd 30 @ 0x193f0' -c 'px 67 @ 0x117214'
-c q data/Z/ZED.EXE`.

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
branches were subsequently implemented as described below; retail random
placement remains unported.

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
- `0x99f46` calls `0x99f95` after assigning an attack. When the target
  has territory +0x18 bit 1 (the human fort), `0x9d61e` enumerates adjacent
  territories in record order. `0x99fc5..0x9a013` scans their active task
  records for both +0x10 bits 3 and 4, skips tasks already in state 3 with
  this destination, and sets every other qualifying task to state 3 with
  target and destination set to the fort before calling `0x9d42c`. The
  engine follows this pass after at least one initial order succeeds, using
  its existing path, destination-reservation and delayed-order validation;
  unlike native it leaves a task unchanged when that validation fails.
  This follow-up uses the ordered edge-equality branches of `0x9d641`:
  nested or partially overlapping rectangles without a matching edge are
  excluded, while touching edges and corners qualify. The same native
  function is called for frontier selection at `0x98dc6`, staged-attack
  handling at `0x99733`/`0x99780`, and defence at `0x9d166`; these engine
  paths now share the corrected predicate. `test_frontier_overlap` checks
  a normal AI pass with an enemy territory nested inside a friendly one.
  `test_fort_followup` exercises nearby occupied, remote, nonmobile and
  already assigned tasks and starts an attack through the normal heavy AI
  pass. Inspect with `r2 -q -e bin.cache=true -e bin.relocs.apply=true
  -c 'pd 70 @ 0x99f46' -c 'pd 65 @ 0x9d61e' -c q data/Z/ZED.EXE` and
  `SDL_VIDEODRIVER=dummy build/bin/tests/z/test_commander`.
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

## Commander service and defensive responses (2026-11-11)

Static disassembly of the DOS LE image with SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
Radare2 was used with LE fixups applied. The heavy dispatcher at `0x9b1e4`
orders these passes after the offensive pass and before the final defence
pass. `0x4772d` routes ordinary CPU commands through the delayed order
queue (`0x98098`). The port uses that queue for service dispatch and return.

### Cranes, buildings and bridges

- `0x3e892` lists dead buildings in CPU territory. `0x9c58d` gives usable
  cranes (`0x9c880`, runtime type `0x24`) the nearest unassigned building
  by squared distance to `origin + footprint_cells * 8 - (8,8)`. It
  reserves the building at `0x9c948` and puts the crane on task 9. The
  ordered point is the signed offset at building type `+0x34/+0x35`.
  `BUILDnn.DAT +0x1a` holds maximum HP and `+0x1c` holds current HP
  (`0x3e838`, `0x422a7`). The port retains a destroyed building in its
  native wreck state, then restores it and clears its old production
  queue once work completes. `0x24770` accumulates the crane's work at
  building `+0x39` until it reaches maximum HP.
- `0x299b5` clears the dead-bridge list. `0x29a3c..0x29a65` appends a
  collapsed bridge in CPU territory only when record `+0x0f` bit 0 is
  set. `0x9c68b` visits these after the buildings. Its ranking point is
  the bridge midpoint plus `(8,-8)` for horizontal bridges or `(0,-8)`
  for vertical ones. An assigned bridge can pass to a crane at equal or
  shorter squared distance. `0x9c9ae` installs the assignment and checks
  four points for terrain `0x2b`, in the order `(0,0)`, `(16,0)`, `(0,16)`,
  `(16,16)`. The port uses a reachable end of the span when that point
  cannot be routed to on the engine's cell grid.
- `0x24675` uses the high nibble of vehicle body `+6`, copied from weapon
  record `+0x20` at `0x50c1d`. The Crane's byte is 120, so the work rate
  is seven HP per 70 Hz frame. A collapsed bridge starts at signed HP -1.
  `0x24893`/`0x24946` restore the deck when work finishes. `0x9c4f2` sends
  idle cranes and Fire Trucks outside the CPU fort territory to fort
  origin plus `(80,200)` on task 8. `0x9bc08` uses the same return order
  when a crane's work is complete or its assignment is invalidated.
- `0x13855` calls `0x29768` during startup. It copies BRIDGE record
  `+0x0a` (max HP) to `+8` (live HP), so LEVEL14 slot 0 starts at 600
  despite the file's 300 in `+8`. Zero at `+0x0a` initializes a collapsed
  bridge with a 600 HP maximum. `0x2a096` marks both inner lanes `0x2f`
  with `0x32` on the ends. `0x2a16d` restores those lanes to `0x17` with
  `0x07` on the ends and stamps both outer edges `0x2b`. Both preserve
  the tile-bank bit. `0x29a65` draws one broken-deck sprite per interior
  segment using the per-world pair copied to `0x117dd8` by `0x182d3`.

The engine uses persistent mobjs for bridges and buildings, stable IDs for
orders and ordinary sprite mobjs for bridge debris. The bridge receives
explicit terrain attack orders through the existing engine combat path. Its
HP, debris and terrain survive save/load. The follow-up below traces the
arm, crane egress, bridge effects and automatic rebuild.

### Vehicle repair and order validation

`0x9c39b` selects usable idle reserve vehicles whose HP is strictly below
`floor(max_HP/2)`. `0x9c42c` scans the 35 building slots for live CPU repair
facilities. `0x9c335` ranks them by squared distance to the footprint
ranking point and rejects distances of 256 pixels or more. `0x9c485`
orders the vehicle toward the type's `(31,28)` entry and writes task 11.
`0x79cf9` tests the repair facility and `0x79d08` admits one vehicle at a
time. The engine plans to a reachable cell outside the footprint and holds
the vehicle there while it is in the bay.

`0x7a065..0x7a091` calls `0x41ae1` with `max(1, missing_HP / 20)`.
The initial stock is `floor((1 + max(1, missing_HP / 20)) * 880 / speed)`.
`0x3fd5c` uses the ordinary production accelerator. Every 17 facility
ticks, `0x3fe09` computes `remaining_seconds = stock / rate / 17`; for a
nonzero result the vehicle gains `1 + missing_HP / remaining_seconds`.
The bay releases the unit at full HP when stock runs out. A change of
ownership releases it at current HP (`0x3fdaa`, `0x3fe67`). The port uses
the same 17 Hz time conversion as existing Z production.

### Other dispatcher passes

- `0x9be16` picks the first owned fort or territory with importance >32.
  With enemy strength present it directs reserve units at an enemy
  there. `0x5969a` records a building's attacker in territory `+0x15`.
  `0x9bf90` finds the first owned territory with an active enemy record
  and consumes the alert when a local unit is ordered to engage.
  The port records the actual damage source and uses normal attack targets
  for the resulting pursuit.
- `0x9cc83` scans 20 unit slots per heavy pass. Flag `+0x6f & 0x10` is
  set on CPU manufacture by `0x5103e`. Eligible reserves outside CPU land
  return to the nearest owned territory, unless a live lockout and
  `cpu_strength - human_strength - 16 * weight * importance >= 0`
  justify holding. The engine scans live unit IDs with a saved cursor.
- `0x9bba5` checks mobile groups' target territories through `0x9ce49`.
  A target outside CPU land without a capture lockout sends a group with
  a home back there. The port checks the territory of the group's active
  enemy target or movement goal. Native path-order and targeting sentinels are
  represented by the engine's ordinary movement and attack orders.

Recheck the native disassembly and the focused regression cases with:

```sh
r2 -q -e bin.cache=true -e bin.relocs.apply=true \
  -c 'pD 0x7c0 @ 0x9c272' -c 'pD 0x1e5 @ 0x9bc08' \
  -c 'pd 35 @ 0x7a03f' -c 'pd 13 @ 0x41ae1' -c q data/Z/ZED.EXE
xxd -g 1 -l 26 data/Z/LEVEL14/BRIDGE14.DAT
make build/bin/tests/z/test_commander
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_commander
```

## Zod reference: factory rally routes (2026-11-12)

**Confirmed remake behavior, not a retail executable finding.** Zod revision
`5ccb43ab76ee08c9f624906fb465dc3ba76a7a35`, `zserver_events.cpp` rally
messages and `zserver.cpp` manufactured-unit setup supply factory waypoint
lists. A selected open-rts producer now accepts a map-click rally point or
the shared waypoint route; Stop clears it. New mobile units inherit the
route. Existing units keep their previous orders. The shared produced-unit
hook runs after exit spacing, so the exit order cannot erase the rally.
`test_production` verifies ownership, invalid paths, replacement, travel and
clearing; StarCraft's gameplay test verifies its existing rally destination
survives the same hook ordering. Rally markers and target-object rally orders
are not implemented by this change.

## Zod reference: APC crew and ejection (2026-11-12)

**Zod cannon re-entry policy:** pinned Zod `zserver_events.cpp` sets
`just_left_cannon` on every robot released from a cannon; its idle and
`CheckOpportunisticGrab` searches in `zobject.cpp` exclude cannons while
that flag is set. An explicit enter waypoint clears it, and
`CloneMinionWayPoints` propagates the leader's value to followers. This is
fork behavior for the already-adopted voluntary ejection command, not a
confirmed retail rule. Open-rts now saves and hashes the per-robot flag,
retains it across ordinary movement, and clears it for the squad when a
board order begins, including a queued board when it reaches the front.
The internal AI board path shares that clearing rule. Its detour continues
to use native candidate ranking before this eligibility check, rather than
Zod's separate candidate-list ranking; idle auto-entry is not introduced.
APC ejection does not set the flag. Z save layout 21 records it.
`test_detour` exercises movement suppression, save/load and consistency,
direct and queued board orders, resumption of automatic entry, and squad
isolation; `test_capture` checks the APC exception.

**Voluntary ejection and empty-hull damage:** pinned Zod `exit_vehicle_event`
clears the drivers and leaves the same hull at its position with neutral
ownership; its `ProcessMissileDamage` also considers neutral objects. The
engine now registers a voluntarily abandoned APC or gun as an empty object
with the native effect-health units used by `0x61bb9..0x61bd5` (body HP
divided by 25, at least one), so its shell-impact pass can damage it and a
subsequent boarding releases its slot. The voluntary command is Zod policy,
not a traced retail conversion: it keeps the existing hull position and HP
until the next impact. It uses the first free represented native effect slot;
if all 200 are occupied, the ejected hull remains as an unslotted object and
is considered after slotted effects, preserving Zod's damageable hull rather
than claiming native allocation parity. Unrepresented native effects also
make the represented slot order approximate. `test_combat` covers all five
ejectable types, free and full slots, shell damage without a duplicate unit
hit, save/load and reboarding.

**Ordered boarding reach correction:** `0x7afc9..0x7aff4` squares the
differences between the empty effect's origin (+0/+2) and robot's origin
(+0x2c/+0x2e), and allows boarding at a sum at most 400. Explicit boarding
orders now use the same native-origin test as opportunistic detours; the
former centre-to-centre test could board too early from one side and too
late from the other. `test_capture` exercises an explicit order through
`Z_Ticker` at origin offsets +21 and -20 pixels. Reproduce with `r2 -q -e
bin.cache=true -c 'pd 35 @ 0x7afb1' -c q data/Z/ZED.EXE` and
`SDL_VIDEODRIVER=dummy build/bin/tests/z/test_capture`.

**Confirmed in the Zod remake, not yet confirmed in ZED.EXE.** At pinned
revision `5ccb43ab76ee08c9f624906fb465dc3ba76a7a35`,
`src/zserver.cpp:RobotEnterObject` gives an entered object the robot's driver
type and health. For an APC it also absorbs each member of the leader's minion
list, with no distance check. `src/vapc.cpp:ResetDamageInfo` takes the driver's
weapon and multiplies its damage by the crew count. `src/zserver_events.cpp`
`eject_vehicle_event` restores the drivers' individual health and neutralizes
the vehicle; `src/zcannon.cpp` and `src/vapc.cpp` permit ejection, but
`src/zserver.cpp:MakeAllFortTurretsUnEjectable` excludes fort turrets.

The engine represents Zod minions by its existing live same-team squad and
stores the members' type, health, robot record and grenade count while aboard.
Squad IDs are eight-bit and can wrap, so they are an approximation of Zod's
explicit minion membership, not permanent unique group identities.
The U key and sidebar EJECT button submit `TC_UNLOAD`; ejection restores those
members and leaves an empty APC or gun. Starting APCs and guns without a stored
crew use a Grunt driver, as in Zod's cannon setup. APC bullets and Tough shells
now resolve separately per stored crew member. APC projectile effects and exact retail
cargo/driver mutation remain **unknown or unported**; separately traced
driver-dependent reload and range are described below.
The save layout is versioned for the added crew state. Reproduce with:

```sh
make build/bin/tests/z/test_capture build/bin/tests/z/test_hud
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_capture
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_hud
```

**Native APC reload (ZED.EXE SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`):**
`0x6375a` saves the boarding robot's fire-rate byte +0x75, and APC setup
`0x638ac..0x638c3` restores it, using 80 only when that byte is zero. The APC
action dispatch entry 0x38 points to `0x1e9dc`; `0x1ea4a..0x1ea59` loads
`100 - rate` plus half that value, with a signed overflow fallback. For the
shipped robot rates this is 30 native frames for Grunt, Tough, Sniper and
Laser, and 3 for Psycho and Pyro. `G_WeaponCooldown` now uses the authored
driver stats and that formula, including when the body has been damaged.
`test_combat` covers all six driver types through actual attack dispatch,
boarding, save/load, and real ticker intervals. In its stationary fixture
the engine fires at 18-tic intervals with a Grunt and 6 with a Psycho;
the shared attack animation and 30 Hz quantization affect those intervals,
so the 70 Hz native reload alone does not prove full firing-timing parity.
This does not establish the native APC's projectile or damage dispatch.
Inspect with `r2 -q -e bin.cache=true -c 'pd 12 @ 0x6375a' -c 'pd 22 @
0x638ac' -c 'pd 30 @ 0x1ea0d' -c q data/Z/ZED.EXE`.

**Native APC range:** `0x50df7..0x50e04` stores the robot weapon's range
in +0x71 and doubles it into the unit's +4 range. On APC conversion,
`0x63900..0x63917` copies that saved robot +0x71 through body +0x27 to
body +5 and the APC's +0x71, then doubles it into +4. The engine now uses
the authored C range of the current driver: 120 pixels for Grunt, Tough,
Psycho or Pyro, 144 for Sniper and 136 for Laser; none of the six shipped
robot types has a shorter range than the APC's 120-pixel baseline.
`test_combat` checks each exact boundary, live target selection, boarding
and save/load. Its general centre-distance comparison remains an
engine approximation; this does not prove native target acquisition parity.
Inspect with `r2 -q -e bin.cache=true -c 'pd 20 @ 0x50df1' -c 'pd 18 @
0x638fc' -c q data/Z/ZED.EXE`.

**APC projectile dispatch, partial trace:** `0x1ea5c..0x1eb02` iterates
group member slots and caps a salvo at 12 port attempts. The branch at
`0x1f037..0x1f0d1` dispatches the stored driver class: Tough (3) calls shell
creation `0x59ed1`, Pyro (15) uses flame creation `0x5a099`, and other
classes including Laser (16) use bullet creation `0x5af81`. The scanner loop
at `0x1eacd..0x1eaed` retries a port while CH is unchanged; in its normal
mode a successful shot increments CH and moves on to the next group member.
Target choice among four scanned targets and skipped or blocked ports are
not yet implemented.

The separate coordinate-target path `0x1eb04..0x1ec12`, called at
`0x58090` and `0x58cc7`, sets mode +0x12dd2b and loops once per group
member at `0x1eb86..0x1ebfd`. For each Tough it dispatches a separate shell
through `0x1ed92` and `0x1eeb4`; unlike the scanner loop, this path has no
12-port-attempt cap. It adds independent random offsets -8..7 pixels to
the target coordinates at `0x1ebc1..0x1ebdd`. Open-rts applies those
pixel bounds to the existing target selection anchor, using its deterministic
Z combat coin keyed by each projectile ID; that is not the native random
stream and the engine's anchor is an approximation of the native coordinates.
Its reload at `0x1ec07`
also differs from the scanner reload at `0x1ea4a`. Open-rts now spawns a
separate Tough shell with individual damage per stored crew member, matching
the coordinate-target path's multiplicity. It applies that rule to all APC
attacks with a common target: native mode selection, port acquisition, native
random sequencing and the alternate reload remain unmodeled.
The single-target implementation therefore does not establish complete APC
combat parity. Bullet-equipped APCs also apply the driver's single-shot damage
and vehicle armor separately for each crew member, rather than multiplying
damage before one armor and driver-hit roll. Each roll uses the existing
deterministic Z coin with a per-shot salt, not the retail random stream;
the engine applies all shots to its single current target and stops if that
target is removed or changes team. For a Pyro driver, the coordinate-target
loop `0x1eb94..0x1ebb3` increments each member's byte +0xb0 modulo 16 for
every driver class and dispatches Pyro flame only when the resulting phase
is below 4. The scanner loop
`0x1eab2..0x1eacb` instead uses a threshold of 10. Open-rts applies the
coordinate-target gate to its single-target attacks, retaining each member's
phase through boarding, ejection and save/load. The engine still approximates
flame as an immediate hit: native flame projectile `0x5a099` and its ground
fire are unported, and native mode selection remains unknown. The robot
record is initially zeroed at `0x5109c..0x510a3`. Correction: constructor
`0x5117d..0x51185` then assigns byte +0xb0 to `0x70d37() & 15`, so zero
is not its final initialization. Open-rts initializes each newly placed or
manufactured unit's phase using its saved and hashed combat RNG, including
each produced squad member; crew transfers retain the saved member phase.
The combat RNG is a separate deterministic stream rather than retail's
global stream, and direct test-created mobjs do not pass through the normal
Z unit creation paths. Other possible uses of +0xb0 remain untraced.
`test_production` checks startup and produced squad phases and save/load;
`test_combat` covers per-crew advancement. Reproduce with `r2 -q -e
bin.cache=true -c 'pd 90 @ 0x5108c' -c q data/Z/ZED.EXE`.

**Flame impact detail:** `0x5a099` creates the projectile with +0x2a set
to 1. Impact `0x593b5..0x593c6` checks that byte and calls `0x595e6`,
then `0x59678`; it bypasses the ordinary `0x8476c` blast call at
`0x593d3`. In `0x595e6`, the loop counter starts at one and is saved while
the random effect ID is formed: `(rand & 7)`, with zero changed to one,
plus 70. Thus it attempts one effect of ID 71..77, not 70..77 effects.
The subsequent effect and damage lifecycles remain to be traced before
replacing the current instantaneous flame approximation. Inspect with
`r2 -q -e bin.cache=true -c 'pd 35 @ 0x5a099' -c 'pd 45 @ 0x593aa'
-c 'pd 45 @ 0x595e6' -c q data/Z/ZED.EXE`.

**Flame effect lifecycle, partial:** `0x5969f` allocates a 40-byte effect
record via `0x4a5d7` (200 slots), places its coordinates, selects the
25-byte descriptor at `0xe5974 + 25 * effect ID`, stores terrain-derived
DL in effect +0x22 and copies projectile +0x1e into effect +0x26. The
descriptors 71..77 name the flame art. On alternating global steps,
`0x4a677..0x4a6fc` dispatches those seven sprite IDs to `0x4aa53`.
That routine advances a four- or six-frame cycle depending on sprite ID;
at each wrap, it calls `0x4ab23` and then `0x868ef`. The latter invokes
terrain damage and selects units in a 24-by-24 coordinate box, filtering
the stored team. Unless effect +0x22 is 127, each wrap subtracts 8 and
removes the effect when the signed result is negative. `0x4ab23` can alter
damage and lifetime behavior according to terrain/object checks. The
damage byte comes from global `0x144d9b`, set to 20 at `0x472b0` and also
written from a data byte at `0x50dc9`. Ground fire is a recurring area effect,
not a single immediate hit. Reproduce with `r2 -q -e bin.cache=true -c
'pd 45 @ 0x5969f' -c 'pd 45 @ 0x4a677' -c 'pd 75 @ 0x4aa53' -c 'pd 45
@ 0x868ef' -c q data/Z/ZED.EXE`.

**Flame initialization and terrain lookup:** spawn dispatch
`0x50cbc..0x50ccd` selects `0x50dc6` for class `0x0f` (Pyro), which copies
its weapon record +0x20 into both global `0x144d9b` and the unit's +0x63.
Startup `0x472b0` supplies 20 before such a spawn; the global may therefore
change when a Pyro is created, rather than being stored per flame.
`0x595e6` looks up the 16-pixel map cell with `0x5fcdd`, reads its terrain
byte at cell storage +0x4000 masked by 0x7f, and indexes table `0x1157a7`
for the effect's initial +0x22. For example table entries 0, 8, 16, 40,
and 51 are 6, 50, 12, 100, and 127 respectively; these are terrain indices,
not a random lifetime. The special +0x22 value 127 avoids lifetime
decrement at the normal damage wrap. In `0x4ab23`, if the coordinate checks
at `0x281da`/`0x29fd8` find an active building or an eligible bridge, damage
is scaled by global `0x11d4cd` as `max(1, damage * factor / 100)`.
`0x281da` scans 35 BUILD records with nonzero health and inclusive descriptor
footprints; flag `0x14aa50` includes building type 2. If no building covers
the point, `0x29fd8` scans 12 bridge rectangles, skipping records whose
flags +0x0f contain bit 2. Startup at `0x4734c` writes factor 24 (the
executable's static byte is 50); all runtime writes remain unknown. Correction:
although `0x4aafb` contains a decrement-by-one path based on a flag set by
these checks, `0x4aaf9` unconditionally jumps past it to the subtract-eight
path. No incoming branch to `0x4aafb` was found in the linear disassembly,
so the one-step path is not claimed reachable. Inspect with `r2 -q -e bin.cache=true -c 'pd 30 @ 0x50cbc' -c
'pd 15 @ 0x50dc6' -c 'pd 12 @ 0x5fcdd' -c 'px 64 @ 0x1157a7' -c
'pd 45 @ 0x4ab23' -c q data/Z/ZED.EXE`.

**Flame sprites and area geometry:** descriptors 71..77 at `0xe6063`
select base sprite IDs 5836, 557, 469, 5840, 5844, 5848 and 5854,
respectively. `0x4aa74..0x4aaa5` wraps IDs 557, 5848 and 5854 after six
advances; the other four wrap after four. Frame begins at zero. The effect
pass is gated by global `0x10e91b` at `0x4a6c2`, but also by display-list
limit checks at `0x4a695..0x4a6bd`; these calls alone do not prove a fixed
offscreen simulation period. The unit candidate rectangle starts at the
effect coordinates and ends at +24,+24, with both edges inclusive.
`0x64364..0x643bd` tests each unit's top-left +8,+8, or +16,+16 for class 1,
against those edges. Separately `0x88a7f` scans 100 other object records
within absolute coordinate differences <=24 on each axis; it is not the
unit rectangle. Effect allocation, display-list coupling, map-object damage,
and flame projectile flight and target scatter remain unimplemented.
Inspect with `r2 -q -e bin.cache=true -c 'px 175 @ 0xe6063' -c 'pd 65 @
0x4a677' -c 'pd 30 @ 0x4aa74' -c 'pd 35 @ 0x868ef' -c 'pd 40 @ 0x64364'
-c 'pd 45 @ 0x88a7f' -c q data/Z/ZED.EXE`.

**Flame flight and impact, partial:** `0x5a099` uses the common projectile
initializer `0x5a39e`, which allocates one of 50 44-byte records through
`0x592a8`, retains launch and aim coordinates, owner/team and a Bresenham-like
major-axis movement state. `0x592df` iterates these records and `0x592ff`
dispatches movement through the direction table at `0x148bb0`. At arrival
`0x593aa` subtracts (12,24), then flame creator `0x595e6` adds (9,21):
the effect origin is impact minus (3,3) pixels. Although `0x59637..0x59655`
calls the random generator twice and shifts the results, subsequent `mov
ax,0` instructions discard both values, so this routine adds no random
offset to the effect origin. Earlier aim selection/scatter and projectile
speed still need tracing. The separate `0x88a7f` callback scans 100 64-byte
map-object records, excludes unused and several flagged/state cases, and
for eligible objects within ±24 pixels sets state fields and calls
`0x8790a`; that routine dispatches visual/audio and object-specific work.
The semantics of these object fields and resulting gameplay changes remain
partly unknown. Further trace: `0x88a7f..0x88b12` invalidates each selected
record's x word before calling `0x8790a`. That function starts the animation
whose embedded debug name is `burst_an` (`0x6c292..0x6c2b8`), and its
subsequent `an_shrap` callback at `0x6c2ee..0x6c32c` calls `0x86963`
with strength 10. The latter calls `0x8476c`, invokes another `0x88a7f`
pass, and selects nearby units for damage through `0x64309` and `0x86ca4`.
This is a potential secondary and chained gameplay effect, not merely a
visual callback. The record-type mapping, animation callback timing, and
complete damage flags and ordering remain unresolved; do not apply a
generic immediate second blast to ordinary map objects. Reproduce with
`r2 -q -e bin.cache=true -c 'pd 55 @ 0x88a7f' -c 'pd 50 @ 0x8790a'
-c 'pd 45 @ 0x6c292' -c 'pd 80 @ 0x86963' -c q data/Z/ZED.EXE`.
`0x6d093` additionally suppresses that animation when global `0x10f33a`
is nonzero and returns if its animation allocator fails; successful setup
installs `0x6c2ee` as its later callback and a 400-unit field at +0x12.
Consequently the secondary blast is conditional on successful animation
allocation, and that field alone does not establish its delay. The scanned
64-byte records are constructed at `0x875c7`; their +0x20/+0x21 and +0x3d
come from a 16-byte type descriptor at `0x14c309`. Their semantic types
are not yet identified.
The Pyro firing action calls `0x60ed5`, which supplies its +0x76
weapon speed and derives a facing-dependent launch position; object targets
pass through `0x75cc1`, while the alternate coordinate branch at `0x60f54`
computes an accuracy-dependent spread via `0x64250`. Flame creation forces
projectile +0x17 to 16 while retaining the original speed in +0x16; each
`0x59c2b` update adds 16 to +0x17, and movement `0x5a4c8` uses the smaller
of +0x17 shifted right four and +0x16. Thus a speed-4 Pyro flame accelerates
through major-axis steps of 1, 2, 3, then 4 pixels, limited by remaining
distance. This is separate from the existing constant-speed shell model.
`0x59c71..0x59ca0` also stops flight when the terrain-height table exceeds
the projectile's recorded launch threshold. Reproduce with `r2 -q -e bin.cache=true -c 'pd 55 @ 0x5a39e' -c
'pd 30 @ 0x592a8' -c 'pd 55 @ 0x593aa' -c 'pd 45 @ 0x595e6' -c 'pd 55
@ 0x88a7f' -c 'pd 45 @ 0x8790a' -c 'pd 75 @ 0x60ed5' -c 'pd 40 @
0x5a4c8' -c 'pd 45 @ 0x59c2b' -c q data/Z/ZED.EXE`.

**Flame in-flight unit response:** while still traveling, the flame branch
`0x595d5` invokes `0x59894` after the terrain check. It looks up candidates
around the stored aim, excludes same-team units, class 2 and several state
flags/actions, requires each candidate's +8,+8 position to be within 32
pixels of the aim on both axes, and compares squared distance from the
projectile's current position with a threshold derived from unit byte +2.
For a passing candidate it derives a destination using two random -8..7
values, sets unit +0x62 bit 0x40, calls `0x7ba32` (sets speed modifier +0x89
to 2), and `0x7ba9a` (starts movement toward that destination where permitted).
This is a flame-specific movement reaction and does not establish that the
previously traced generic missile reaction action 0x2b displaces units.
One test in this routine reads `[ebp+0x67]` while EBP refers to the 44-byte
projectile record; its intended meaning and effects of that out-of-record
read need runtime verification before reproducing the radius formula. Given
the 44-byte stride, for projectile slots 0..47 that address is byte +0x0f
of the slot two positions later; for slots 48 and 49 it is beyond the
50-record array. It is not the candidate unit's +0x67. Another apparent
register anomaly at `0x599f0` adds the second random value to AX while the
following sign test and y-coordinate arithmetic use BX. A literal port
would preserve those reads and register effects; treating them as intended
unit flags or symmetric scatter would be an unsupported correction.
Inspect with `r2 -q -e bin.cache=true -c 'pd 12 @ 0x595d5' -c 'pd 105 @
0x59894' -c 'pd 45 @ 0x7ba9a' -c q data/Z/ZED.EXE`.

**Further mode investigation:** the wrapper at `0x1e9dc` calls the ordinary
vehicle pass `0x57dd6`, then calls the scanner at `0x1ea0d`; that scanner
returns without a salvo when all four scanned target bytes are `0xff`
(`0x1ea17..0x1ea44`). The coordinate path at `0x58090` instead receives
unit words +0x50/+0x52 after temporarily copying them to the body coordinates;
the call at `0x58cc7` receives coordinates returned by `0x764e8` after its
carry check. These call sites establish distinct acquisition paths, but do
not establish a reliable mapping to open-rts automatic versus ordered attacks.
More precisely, the `0x58cc7` caller is reached from `0x58b03`: it passes
body words +0x0e/+0x10 into `0x764e8`, rotates body facing +0x15 towards the
result, and only enters dispatch `0x58bc5` once aligned and unit +0x78 is
zero. After successful APC dispatch, `0x58bab..0x58bb0` reloads from the
unit's +0x75, overwriting the body-based reload in `0x1ec07`. This differs
from the scanner's 1.5-times unit reload and from the independent coordinate
call at `0x58090`; selection between these paths remains unproven.
The path through `0x58b03` is also reached when unit +0x67 bit 0x80 is set
(`0x58040..0x58044`). `0x771f9..0x7723b` encodes an object reference in
body +0x0e/+0x10 using sentinel `0xef1f` and the target's unit-record offset,
then sets unit +0x62 bit 8 and +0x67 bit 0x80. This establishes an
object-target route into that firing path, but does not establish whether
the target arose from a player order, automatic response, or both. The
scanner wrapper executes the vehicle pass before checking its own cooldown,
so a coordinate shot in that pass can suppress the scanner in the same pass.
Do not infer such a mapping solely from their names. Inspect with `r2 -q -e
bin.cache=true -c 'pd 20 @ 0x1e9dc' -c 'pd 25 @ 0x1ea0d' -c 'pd 30 @
0x58040' -c 'pd 80 @ 0x58b03' -c 'pd 28 @ 0x58c80' -c 'pd 28 @
0x771f9' -c q data/Z/ZED.EXE`.
`test_combat` checks three Grunt bullets against infantry, three Sniper
bullets against tank armor, individual driver/body hit outcomes and the
three-Pyro firing phases, independent crew phases, and save/load. `test_capture`
checks phase preservation through boarding and ejection. `test_combat`
also checks scatter bounds and variation, identical
aims and consistency hash after restoring the same state, independent shell
impacts, save/load and a full 21-member salvo. Inspect with `r2 -q -e bin.cache=true
-c 'pd 90 @ 0x1ea5c' -c 'pd 65 @ 0x1eb64' -c 'pd 65 @ 0x1effd' -c q
data/Z/ZED.EXE`.

## Task 7, automatic bridge reconstruction and crane poses (2026-11-11)

The DOS LE image is `data/Z/ZED.EXE`, SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`.
Addresses below are relocated virtual addresses. The code object starts at
VA `0x10000`, file offset `0x62a00`, with size `0xb0254`.

### Task 7

- **Confirmed.** The table at `0x18d388` has 240 records of 22 bytes.
  `0x9c125` resolves the CPU lead through the parallel 72-byte records at
  `0x1943f8`. `0x9bc34` admits task 7 to the service validation pass.
  Its target is the lead unit's `+0x28` pointer, with `0x2084` skipped.
  `0x9bc89` checks the building's territory owner, and `0x9bdb9`
  keeps the assignment while signed building HP at `+0x1c` is strictly
  between zero and maximum HP at `+0x1a`. Otherwise `0x9bcc9` queues
  the CPU fort approach at `(+80,+200)` and `0x9bd14` writes task 8.
  Task 7 bypasses task 9's reservation cleanup at `0x9bc9e`.
  `0x991d7` also recognizes task 7 and allows it into the idle cleanup
  even without the ordinary mobile-task flag.
- **Assignment unknown.** Auditing table references, immediate and
  register stores to the task byte, and wider stores covering that byte
  identified direct assignment values `-1,0,1,2,3,4,5,6,8,9,10,11`.
  The indexed access at `0x9de1a` tests `+0x10`, and the register
  store at `0x9c9e5` writes a crane index into a **bridge** record.
  `0x98798` is a wider store in the territory table. `0x9a996`
  explicitly assigns **6**. Native save/load writes/reads the entire
  `0x14a0`-byte task table at `0x1c378`/`0x1cf06`. The ten installed
  `SAVE?.Z` files are 43-byte empty slots and contain no task snapshot.
- **Disproven lead.** `0x431a8` initializes the damaged-building list
  at `0x1fa9e4`. `0x3e8af` appends living buildings below maximum HP
  through cursor `0x1426c8`. The references traced for these addresses
  only initialize or append. The dead-building list at `0x1fa958` does
  have a reader, `0x9c58d`, which assigns task 9. The Fire Truck's
  action at `0x24c14` does not assign the commander task. The port
  handles task 7 with an existing unit target and leaves its dispatch
  unresolved.

#### Indirect writes and retail runtime follow-up

**Confirmed statically.** The LE fixup audit found 61 targets in
`[0x18d388,0x18e828)`. Sixty target the table base. The one interior
reference targets `0x18d398`, the indexed flag test at `0x9de1a`.
`0x9c0fc`, `0x9cc29` and `0x9cd1c` derive rows with a stride of
22. Their accesses use the active or flags bytes. The pointer calculation
at `0x7789a` is discarded by `pop eax`. `0x4f248` initializes row
offset 0 and flag offset `+0x10`. `0x9d9eb` writes `0xff` to the task
byte of each valid CPU lead before applying any opening order. The save
writer at `0x1c378` and loader at `0x1cf06` are the confirmed bulk
copies covering `+0x11`. A save could restore a previously stored 7.
These paths establish no gameplay source for that value.

**Retail runtime.** The fingerprint above ran unmodified in DOSBox-X
2026.10.01, build `f1c4414`, on a disposable copy of the game data.
`0x10227` checks for the native `/CD` argument. The emulator used a
Sound Blaster Pro 2 at IRQ 5/DMA 1 to match `DIG.INI`. Breakpoints in
New Game selected LEVEL20 through the normal level loader. In this run,
CS was `0x160` and DS was `0x168`, both with base zero. Code loaded
at static VA plus `0x1af000`. The task table relocated to `0x31d388`,
buildings to `0x31ad2c`, units to `0x301aa8`, and the damaged list
to `0x38a9e4`. Data objects have different relocation deltas. Confirm
addresses from the loaded operands on a new launch.

- **Confirmed.** Setting `BUILD20.DAT` record 6 to 750/1500 HP on disk
  still produced a full-health CPU radar at startup. `0x3ec33` copies
  max HP to current during building initialization. Setting current HP
  to 750 after that pass put the radar pointer `0x31af0c` in the
  damaged-building list. With CPUPLR group 15 at slot 0, 64 consecutive
  hits at the heavy-pass call `0x9b4a4` sampled all 240 rows. All 64
  samples showed the radar on the list at 750/1500 HP. The 14 active
  rows yielded 896 observations of tasks 0, 1, 3 and `0xff`. No
  active row held 7 at that checkpoint.
- **Disproven Fire Truck fixture.** `ROBOTS.DAT` group 15 resolves to
  Fire Truck weapon 14, category `0x0d`. Replacing group 16 with 15
  at `CPUPLR20.DAT +0x792` spawned infantry at `(1568,384)` in
  unit slot 16, type `0x04`, category `0x0d`, CPU squad 120.
  `0x50fd0` initializes the infantry. Its conversion switch at
  `0x50cbc..0x50d1b` has a Crane case (`0x0c`) and no Fire Truck
  case (`0x0d`). A second trial placed an empty Fire Truck at
  `(1568,400)` using unused `OBJECT20.DAT` record 14. The ten
  fixture bytes appeared in the loaded object data, and the
  instantiated world table contained no object at those coordinates.
  `0x1843b..0x1843e` explicitly skips placed object type 37
  (`0x25`), the Fire Truck, before the object initializer at
  `0x4a184`.
- **Stock Crane control.** With the original LEVEL20 files restored,
  unit 16 was an occupied CPU Crane (type `0x24`, squad 120).
  The radar stayed on the damaged list at 750/1500 HP for 32
  consecutive samples at `0x9b4a4`. The Crane's row stayed
  `0xff` throughout. The 14 active rows yielded 448 observations
  of tasks 0, 1, 3 and `0xff`, with no 7 at this checkpoint.
  Setting the radar's HP to zero put its pointer on the dead
  list at `0x38a958`. A memory breakpoint on the Crane's task
  byte caught `ff -> 09` at linear EIP `0x24b9a8`, immediately
  after the store at `0x9c9a4`. The debugger changed HP and the
  retail dispatcher assigned task 9.
- **Debugger positive control.** At the validator call `0x9b645`,
  the debugger seeded task 7 into row 120 and set the lead's
  `+0x28` target to the radar. `0x9bdb9` ran with EBP on the
  row and EAX on the lead. It retained 7 at 750/1500 HP. With
  HP restored to 1500 and the call replayed, a linear memory
  breakpoint on the task byte recorded `07 -> 08` at the
  instruction immediately after the native store at `0x9bd14`.
  The debugger supplied the 7.

**Inferred.** The damaged list, Fire Truck handlers and task 7 validator
are consistent with a service path whose producer is absent or disabled
in this build. The two adjacent return instructions at `0x9c58b` and
`0x9c58c` provide no evidence of a producer by themselves. Breakpoint
samples cover the tested campaign state at a specific point in the heavy
pass. A gameplay trigger for task 7 remains unverified.

Reproduce the relocation audit and relevant disassembly with:

```sh
rabin2 -R data/Z/ZED.EXE | perl -ne '
  if (/(0x[0-9a-f]+)\s*$/i) {
    $va = hex($1);
    $refs{$va}++ if $va >= 0x18d388 && $va < 0x18e828;
  }
  END { printf "%x %u\n", $_, $refs{$_} for sort {$a <=> $b} keys %refs }
'
r2 -q -e bin.cache=true -e bin.relocs.apply=true \
  -c 'pd 20 @ 0x18418' -c 'pd 26 @ 0x3ec07' \
  -c 'pd 25 @ 0x50cb3' -c 'pd 12 @ 0x9bdb9' -c q data/Z/ZED.EXE
```

For the fixtures, use a copied game directory. Set
`CPUPLR20.DAT +0x792` to `0f` and `BUILD20.DAT +0x1fc` to
`ee 02`. In the placed-object trial, set `OBJECT20.DAT +0x8c`
to `20 06 90 01 25 0e ff ff ff ff`. Mount the copy as the DOS
drive and run `ZED.EXE /CD`. In the DOSBox-X debugger with the
relocation layout above:

1. Break at `160:201062` and use `SMV 2D61D9 02` for New Game.
   Break at `160:205739` and use `SMV 293D63 14` for LEVEL20.
   At the first gameplay AI update (`160:24A1E4`), use
   `SMV 31AF28 EE 02` to damage the radar.
2. Break at `160:24A4A4` for each passive sample. Use
   `MEMDUMPBIN 168:31D388 14A0 TASKS.BIN` and
   `MEMDUMPBIN 168:38A9E4 8 LIST.BIN`, then continue. In the
   task dump the value and active bytes are at `22 * row + 0x11`
   and `22 * row + 0x15`. Check the placed-object trial with
   `MEMDUMPBIN 168:2753F0 A OBJECT.BIN` and
   `MEMDUMPBIN 168:3504D0 1F40 WORLD.BIN`.
   For the stock Crane trial, restore the original files and
   repeat with runtime HP 750. At the final sample, set
   `SMV 31AF28 00 00` and arm `BPLM 31DDE9`. On the first
   `00 -> FF` initialization stop, resume to catch `FF -> 09`.
3. For the positive control, break at `160:24A645` and use
   `SMV 302650 0C 0AF 31 00` and `SMV 31DDE9 07`. Prefix
   `AF` with zero so the debugger treats it as a number. Check
   the retained task at `160:24A64A`. Set `SMV 31AF28 DC 05`,
   clear the execution breakpoints and arm `BPLM 31DDE9`. Use
   `SR EIP 24A645` and `RUN`. The newly armed watchpoint may
   report `00 -> 07` from its initial comparison. Resume to
   observe the native `07 -> 08` at linear EIP `0x24ad18`.

#### DOS demo and other retail editions

The five Renegade language archives and the DOS Games Archive package all
contain the same `ZED.EXE`. The Virgin archive contains a second build.
Both are 32-bit x86 DOS/4GW LE executables. Their first code object starts
at VA `0x10000`, file offset `0x58000`, with size `0xa1a04`.
The embedded assembly stamps agree with the ZIP entry timestamps.

| Demo | Version | Timestamp | Bytes | Executable SHA-256 |
| --- | --- | --- | ---: | --- |
| Renegade, five languages | `0.013071HS` | 1996-08-13 13:39 | 2,190,905 | `03af8dae03838e4037e6bd19b7ac4b9979cffb25fe6d43adc3442a9b5f40b720` |
| Virgin | `0.013062HS` | 1996-08-12 19:51 | 2,190,937 | `96990c3f583c62a253119943c10701b72406e56158e7973e9b97d3b154c7c0b9` |

- **Confirmed.** The two demos have identical bytes in the commander
  interval `[0x8bae5,0x90d75)` after masking the same 877 four-byte LE
  fixup locations. The task table bases are `0x17accc` (Renegade)
  and `0x17acec` (Virgin), with 240 rows of 22 bytes. Both have 56
  fixups to the table base and one to `+0x10`, the flag test at
  `0x90b6a`. The two references outside the commander interval
  initialize row offsets 0 and `+0x10` (`0x4a58b`) or discard the
  computed pointer (`0x6d6ba`). The installed retail build has four
  additional save/load references to the task table.
- **Confirmed.** Both demos read task 7 in the idle cleanup at
  `0x8bf27` and the service validator at `0x8e984`. The validator
  tests signed current and maximum building HP at `0x8eb09` and
  changes a finished or invalid order to 8 at `0x8ea64`. The native
  return-home pass recognizes occupied Fire Truck type `0x26` at
  `0x8f280` and assigns **8** at `0x8f288`. The neighboring
  `0x8f2db` and `0x8f2dc` return immediately. No LE fixup or
  relative near call or jump to either stub was found in the
  demos or at their retail counterparts. The dead-building
  dispatcher starts at `0x8f2dd` and the Crane assignment writes
  **9** at `0x8f6f4`. No task 7 store was identified.
- **Disproven lead.** The damaged-building pointer list
  (`0x1e49fc` in Renegade, `0x1e4a1c` in Virgin) has one fixup
  into its 35-entry span, at its initializer `0x3f116`. The three
  fixups to its cursor (`0x131a4c` or `0x131a6c`) initialize it
  at `0x3f121` or append through it at `0x3bebc` and `0x3bec5`.
  No consumer was identified.
- **Confirmed.** Both demo placement loops skip Fire Truck object
  type `0x25` at `0x1881f`. The opening-squad conversion
  `0x4c00a..0x4c069` includes Crane category `0x0c` and omits
  Fire Truck category `0x0d`. Both `ROBOTS.DAT` versions retain
  group 15 with robot 15, weapon 14, and category `0x0d` at weapon
  offset `+1`. The bundled `LEVEL06` placement and CPU files are
  byte-identical across the six publisher archives. None of the 86
  active object records has type `0x25` or Crane type `0x24`.
  No active CPU starting group is group 15. The 36 loose retail
  `OBJECTnn.DAT` files also contain no active type `0x25` and
  the corresponding `CPUPLRnn.DAT` files contain no active group 15.
  The demo compare against `0x25` at `0x9d41c` belongs to a
  format-string parser. Its branch calls the field parser at
  `0x9d60d`, which reads decimal widths and `*`.
- **Confirmed.** Fire Truck group 15 is at zero-based index 17 in
  the retail manual production cycle at `0x12ecb9`. The forward
  chooser at `0x2515e..0x25175` wraps before index 17 when the mode
  byte `0x10e91e` is zero and before index 16 when it is nonzero.
  The reverse chooser at `0x251c5..0x251db` wraps to index 16 or 15,
  respectively. Neither mode's ordinary product cycle reaches Fire
  Truck. The same forward bounds appear in the demo at `0x2265e`.
- **Retail copy and scope.** The `ZED.EXE` in the RetroGames.cz
  DOS FAT16 image matches the installed retail fingerprint
  `3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`
  byte for byte. The executable's embedded version is `0.011862MM`.
  Regional DOS v1.00 pressings and the 1998 native
  Windows `Z and Z Expansion Kit` are cataloged. No independently
  verified executable from those pressings was available for this
  comparison. The indexed Windows rip advertises a fan patch.
  The Bitmap Brothers' 1996 single-player guide omits Fire Truck
  from its vehicle roster on page 4 and its multiplayer guide does
  not mention it. Both guides predate Z95.
- **Retail save.** The same image contains a populated 576,590-byte
  `SAVE0.Z`, SHA-256
  `3d1769461c185d0b2c7981007e70b778f29f78f9ce7df049629b0066b6facecc`.
  Its eight-byte tag at file offset `0x2b` is `0.011862`, the
  version prefix written by `0x1bb7f`. The 58 write lengths from
  `0x1bb1c` through `0x1c367` total `0x3ce8a` bytes, locating
  the task table copied by `0x1c389` and restored by `0x1cf17`.
  The save-time pointer fixup at `0x1d895` changes row dwords at
  `+0`, `+4` and `+0x0c`. Task and active bytes retain their
  in-memory offsets. Only rows 120–123 are active. Their task bytes
  are `00 00 ff ff`, and none of the 240 rows contains task 7.
  This is one saved state from the matching DOS build.

**Unknown.** These static comparisons establish no gameplay assignment
of task 7 and no build with an enabled Fire Truck. The existing validator
port has no verified trigger to add.

Reproduce with the `ZED.EXE` extracted from each publisher ZIP. For
`base`, use `0x17accc` for Renegade or `0x17acec` for Virgin.

```sh
unzip -p "$zip" ZED.EXE > "$exe"
shasum -a 256 "$exe"
rabin2 -R "$exe" | perl -ane '
  BEGIN { $base = hex shift @ARGV }
  next unless $F[-1] =~ /^0x/;
  $va = hex $F[-1];
  print if $va >= $base && $va < $base + 0x14a0;
' "$base"
r2 -q -e bin.cache=true -e bin.relocs.apply=true \
  -c 'pd 18 @ 0x18801' -c 'pd 26 @ 0x4c001' \
  -c 'pd 20 @ 0x8e958' -c 'pd 13 @ 0x8eb09' \
  -c 'pd 17 @ 0x8f262' -c 'pd 8 @ 0x8f2da' \
  -c 'pd 12 @ 0x3f111' -c 'pd 8 @ 0x3bebc' -c q "$exe"
```

With `SAVE0.Z` extracted from the FAT16 partition at LBA 63 in
`z.img`, reproduce the saved task check with:

```sh
perl -e '
  local $/;
  $save = <>;
  for $row (0 .. 239) {
    ($task, $active) = unpack("x17Cx3C", substr($save, 0x3ce8a + 22 * $row, 22));
    printf "%d %02x\n", $row, $task if $active || $task == 7;
  }
' SAVE0.Z
```

#### Z95 Replay Expansion Kit (2026-11-12)

The Replay edition archived by Abandonware France contains a
387,500,032-byte `ZAndZExpansionKit.ISO` with volume label `Z95VOLUME`
and a volume creation stamp of 1998-03-02 18:51:12 UTC. Its SHA-256 is
`9fc5aaa9b7f3c3bb787ebf4b679137abe162aa9b0dd65ea3254793c1e2d93634`.
The analysis used `Z/WINZ.EXE` directly from the disc image. No installer
or fan patch was run. The disc image was not independently matched to
a physical pressing.

`WINZ.EXE` is 2,016,256 bytes with SHA-256
`feab885dd04f5ee76a53d89e14f73847049e7bfa335cbdcb4e9ba26c233bf22e`.
Its PE32 header has timestamp `0x34fae468` (1998-03-02 16:55:04 UTC),
Microsoft linker version 5.10, a preferred image base of `0x400000`,
and no base relocation directory. The Rich header includes
`Cvtomf510` and the image imports DirectDraw, DirectInput and
DirectSound. The commander code retains the DOS register and table
conventions, with extra DS prefixes on EBP accesses. The `.text`
section starts at VA `0x401000`, file offset `0x400`, and has
`0x9e400` bytes on disk. Addresses here use the preferred image base.

- **Confirmed task 7 consumer.** The heavy dispatcher calls the
  validator at `0x477b31 -> 0x47810c`. It iterates 240 task rows of
  22 bytes at `0x524b80` alongside 72-byte lead records at
  `0x52bbf0`. Task is row `+0x11` and active is `+0x15`.
  The idle cleanup at `0x45478c` and validator at `0x478139`,
  `0x4781a5` and `0x47822b` compare the task against 7.
  The validator resolves the lead's target at unit `+0x28`.
  After the ownership check, `0x4782c4` retains the order
  only if signed current building HP `+0x1c` is positive and below
  maximum `+0x1a`. The return path queues the fort approach
  `(+80,+200)` and stores **8** at `0x47821c`.
- **Assignment unverified.** Scanning every little-endian 32-bit
  window in `.text` found 60 operands equal to the task table base.
  The only genuine interior references are the active and flags
  tests at `0x47a485` and `0x47a48e`. Two other interior
  matches are bytes from relative calls at `0x42b35a` and
  `0x493f79`. Row-derived stores assign `ff,00,01,02,03,04,05,06,08,09,0a,0b`.
  The opening pass at `0x47a04e` initializes the task to `ff`.
  The apparent register writer at `0x478f78` stores a Crane index
  in a **bridge** record. Save and load at `0x46f1b6` and
  `0x46fd65` copy all `0x14a0` bytes, so the loader can restore a 7
  from an existing save. No gameplay writer of 7 was identified.
- **Confirmed damaged-building and service paths.** The damaged
  list at `0x5a4820` is initialized at `0x446663`. Its cursor at
  `0x5a3ed8` has only the initializer and the append accesses
  at `0x44314f` and `0x443158`. The dead-building list at
  `0x5a4794` has a dispatcher at `0x478b03` that reaches the
  task **9** store at `0x478f33`. The two preceding `ret`
  instructions at `0x478b01` and `0x478b02` have no identified
  caller. The idle return pass recognizes runtime Fire Truck type
  `0x26` at `0x478aa5` and assigns **8** at `0x478aad`.
  The Fire Truck action at `0x48aa13` still exists.
- **Fire Truck availability.** The 25-byte object-name table places
  `Fire Truck` at VA `0x4b873b`, index 37. The 150-record
  map-object loop at `0x42cb62` explicitly skips type `0x25` at
  `0x42cb80`. The opening-group conversion at
  `0x44a2a5..0x44a304` handles Crane category `0x0c` and
  omits Fire Truck category `0x0d`. The manual product table
  at `0x5b3899` places Fire Truck group 15 at index 17.
  The forward and reverse bounds at `0x44ce0d` and `0x44ce7f`
  cap the cycle at index 16 or 15, depending on mode and level.
  The initial CPU production chooser at `0x47a9b2` also excludes
  category `0x0d` from each family list at `0x5d3988..0x5d39a5`.
- **Shipped maps.** All 92 loose `OBJECTnn.DAT` files retain
  150 ten-byte records. None places type `0x25` or wreck type
  `0x26`, and none of the 1,153 boardable object records uses
  weapon byte 14. `ROBOTS.EXP` (SHA-256
  `d1a2bc5f88f1fb66be9e47b79cd39dafb8c471bf8650d2e2bb3f72268c49c168`)
  maps group 15 at `0x4b1e` through robot 15 to weapon 14 at
  `0x583c`, category `0x0d` at weapon `+1`. No active entry
  in the 92 `CPUPLRnn.DAT` starting-group lists references it.
  The embedded object-name list in `ZEDITOR.EXE` includes Crane and
  omits Fire Truck. Dynamic editor options were not traced.

The Replay comparison was static. It establishes no shipped Fire Truck
map or gameplay assignment of task 7. The existing port retains the
verified validator without a new dispatch trigger.

The executable can be extracted from the verified archive by streaming
its ISO member. The on-disc executable starts at ISO sector 183814.

```sh
bsdtar -xOf jeu-01043-z-pcwin.7z ZAndZExpansionKit.ISO 2>/dev/null |
  dd iflag=fullblock bs=2048 skip=183814 count=985 |
  head -c 2016256 > WINZ.EXE
shasum -a 256 WINZ.EXE
objdump -d --x86-asm-syntax=intel WINZ.EXE |
  rg '0x524b(80|90|95)|0x5a(3ed8|4820)|0x11\], 0x7\b'
r2 -q -c 'pd 17 @ 0x42cb62' -c 'pd 26 @ 0x44a29a' \
  -c 'pd 22 @ 0x47810c' -c 'pd 13 @ 0x4782c4' \
  -c 'pd 14 @ 0x44665e' -c 'pd 16 @ 0x443126' \
  -c 'pd 18 @ 0x44ce0d' -c 'px 30 @ 0x5d3988' -c q WINZ.EXE
```

### Bridge timer and segment effects

- **Confirmed.** `0x29803` initializes bridge runtime stock at
  `record+0x140` to `BRIDGEnn.DAT[+0x12] * 17 * 100`, or `-1` for
  `0xff`. `0x1440d` calls `0x299b5` in the gameplay update. Its
  timer branch requires a collapsed bridge (`+0x0f & 2`), no crane
  reservation (`& 4` clear) and the automatic rebuild flag (`& 0x10`).
  `0x29a2f` subtracts 100 per 70 Hz frame and starts rebuilding only
  after stock goes negative. The displayed time at `0x298d2` uses 17
  decrements per displayed count. LEVEL31 slot 0 starts collapsed
  (`+0x0a = 0`) with flag `0x10` and input 58, for 987 updates
  before the deck opens. LEVEL32 has a distinct input of 105.
- **Confirmed.** `0x29bed` opens the deck with `0x2a16d` and queues
  one effect per interior segment. `0x4f57a` loads the four
  `VSHWING?.DAT` and four `HSHWING?.DAT` scripts from `WARDATA.PAC`.
  The chosen variant advances modulo four with each segment. The delays
  at `0x12dd9c` and `0x12dda4` are respectively `{0,4,4,10}`
  (horizontal) and `{0,4,4,6}` (vertical). The script header is 12
  bytes with layer count at `+0`, the pivot at `+6/+8` and the
  initial countdown at `+0xb`. `0x3e005` draws frames from
  countdown minus one down through 1. Each 10-byte layer starts with
  `u16 x, y, sprite`, with an all-zero triple skipped. Sprite IDs 0–16
  add the world base from `0x1430b0` via `0x45ae5`. `0x3e2ff`
  restores HP and resets the timer on the last segment's completion.
- The port keeps the remaining ticks and the segment count on the bridge
  and plays the scripts on ordinary effect mobjs. A queued or active
  crane pauses the timer. The segment variant's first choice uses a
  deterministic local seed. The original effect-pool limit, script
  drawing flags and sound cues are outside the port.

### Crane animations

- **Confirmed.** `0x797ce` starts arm deployment at pose 0 on terrain
  `0x32` (bridge) or `0x3a` (ruin), retaining the anchor in
  `unit+0x8e/+0x90`. `0x79771` starts the hook at 1 on `0x2f`
  (bridge) or `0x38` (ruin). `0x24a2c` reads twelve 8-byte records
  per layer (`s16 x, y; u16 sprite, terminator`). The layer pointer list
  for buildings starts at `0x12ddcc`. `0x12dde8` points to four
  bridge lists in south, west, north, east order, indexed by the
  approaching hull direction divided by two. A pointer of `-1`
  ends a layer list. In the final pose, the last layer's terminator is
  `0xffff`. The frame records occupy `0x12de68..0x12e9a7`.
- **Confirmed.** `0x2454f` advances deployment every native frame.
  `0x2452c` reverses it on every other frame and `0x24ad0`
  swaps sprites 5930 and 5932 in reverse. `0x24aa8` applies the
  two flicker nibbles to 5922 and 5924. `0x245d4` turns the arm
  opposite the hull. `0x24615` rocks the hook through `1..7..2`
  while `0x24675` adds seven work HP per native frame. `0x7ea2b`
  queues hull 5794 + direction × 3 + track frame, arm 5818 + arm
  direction, and hook 5826 + hook frame. The arm offsets are at
  `0x14b454`, with `(+6,-3)` applied. The extra hook offsets are
  at **`0x14b464`**, a valid adjacent table. Rear hull directions
  3–5 queue the hook before the hull, using the unadjusted arm offset.
- **Confirmed egress.** When work stock reaches maximum, `0x2469d`
  sends a crane south to bottom + 16, west to left − 48, north to top
  − 48, or east to right + 48. `0x24770` sends it south of a building.
  Retail starts that movement during retraction and finishes restoration
  once it leaves the work area (`0x245df..0x24807`). The port plays
  deployment, work and reverse poses at the engine's reachable service
  point and issues the egress after retraction. Building sparks and
  crane sound cues remain unported. The work sheet and the ordinary
  vehicle layers both apply the rear hook occlusion, including in the
  factory preview.

The open-rts Z save signature changes with the added states. Saves created
with these states retain the in-progress timer, segment effects and crane
phase. This is static executable tracing with engine regression checks.

```sh
r2 -q -e scr.color=0 -e asm.lines=false -e asm.bytes=false \
  -e bin.cache=true -e bin.relocs.apply=true \
  -c 'pD 0x5d00 @ 0x9821d' -c 'pd 30 @ 0x70251' \
  -c 'pd 36 @ 0x299b5' -c 'pxh 0x10 @ 0x12dd9c' \
  -c 'pxw 0x9c @ 0x12ddcc' -c 'pxh 0xb40 @ 0x12de68' \
  -c 'pd 70 @ 0x244cb' -c 'px 0x20 @ 0x14b454' -c q data/Z/ZED.EXE
xxd -g 1 -l 26 data/Z/LEVEL31/BRIDGE31.DAT
strings data/Z/WARDATA.PAC | rg '[HV]SHWING[0-3]'
make build/bin/tests/z/test_commander build/bin/tests/z/test_units
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_commander
env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_units
```

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

The service and defensive response passes listed here were still missing
on 2026-11-06. See [Commander service and defensive responses](#commander-service-and-defensive-responses-2026-11-11).

### Destroyed-building automatic restoration

**Confirmed**, DOS `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
the 80-byte BUILD record's u16 +0x49 is a restoration setting, not the
Zod 600–660-second constant. `0x3e251..0x3e280` and `0x3ec90..0x3ecc1`
initialize runtime dword +0x45 to -1 for 0xffff, otherwise setting * 17 *
100. The per-building update `0x3e566..0x3e697` requires current HP
+0x1c == 0 and an enabled timer. It subtracts 100 each step, increased by
up to 75 for a team's held-territory percentage above 12 or 25 depending on
global 0x10e91e (`0x680b9`, `0x45e80`);
on negative stock it calls `0x3ece7` and reloads the timer. That call
queues a restoration effect if an effect slot is available. The effect
pool has 64 slots (`0x3dfb5`, `0x24cc0`); exhaustion skips the effect even
though the timer is reloaded. The runner `0x3dfb5` decrements slot byte
+0x0c, and when its pre-decrement value is 2 (`0x3e005..0x3e00b`,
`0x3e225`) calls `0x24ba3`, where `0x24bbb..0x24bbf` copies max
HP to current HP and `0x24bf1` clears the stored gun slots. Active building
records in LEVEL01, LEVEL07 and LEVEL14 use 0xffff; LEVEL31 and LEVEL32
include 117 and 176. The separate live auto-mend paths at `0x3e7aa` and
`0x3e832` are skipped for zero-HP buildings. Global owner selector 0x125e58
is assigned later in this pass at `0x3e6d2`, so the previous selector is
used. The effect completion path can also write that selector at `0x3e2e0`,
and another conditional path writes it at `0x3e919`. The main loop calls
the building pass at `0x1452f`, before the effect runner at `0x156d8`, so
an effect completion can change the selector inherited by the next pass;
the conditional path's mode and full ordering remain unknown. The descriptor frame count is produced dynamically during
destruction: `0x3ee87` calls `0x6be75`, `0x6d89f..0x6d8a5` counts recorded
particle frames, and `0x6df39..0x6df44` writes the count to descriptor
bytes +0x0a/+0x0b without comparison, so the last completed recorded
particle wins. Boundary exit is a distinct path: `0x6e0e6` marks the
particle slot free directly, bypassing the descriptor write at `0x6df39`.
The upper bounds come from `(map_dimension / 2) * 32` at
`0x13621..0x13635`; lower bounds are -32 at `0x6dcdd`/`0x6dd30`.
The
`burst8` initializer `0x6be9b` calls `0x6b8e6`: a random word supplies
independent horizontal velocities in -16..15, a second supplies initial
vertical velocity -4..-7, and a third supplies rotation. `0x6d817..0x6d8b6`
records on alternating particle updates. Lifetime starts at 400 at
`0x6beab` and decrements before processing at `0x6d43b`; the decay counters
start at 4, so `0x6ddd1..0x6deaa` reduces horizontal speed toward zero and
increments vertical velocity every five particle updates. Ground contact
calls `killpix`. `0x6d3b2` sets projection inputs (1, 471, -256) and angle
0x352 before the particle pass. `0x6e25f..0x6e3ba` applies signed 16-bit
inputs, shifted matrix products and perspective division; its zero-depth
fallback of 1 is explicit at `0x6e382..0x6e3a3`. `0x70c0d..0x70c3b` loads
and byte-swaps `SINCOS.DAT` from WARDATA.PAC, shifting signed words right
by five; bytes at offsets 0x352 and 0x452 produce coefficients -898 and
493 for this projection. Installed `data/Z/WARDATA.PAC` SHA-256:
`3a17f168f1b7231be3bac5d324afb1311451aeb4da224b998754851aa035e035`.
Multiple buildings of the same kind reuse
the same descriptor (`kill_vrf` at `0x3ef32`, `kill_tmf` at `0x3f158`,
`kill_rmf` at `0x3f448`). The descriptor's layer counts are not durations. At
`0x3ed0f` EDI points to that descriptor, so its +0x14 write does not clear
the effect slot's +0x14 delay. The bridge paths share allocator `0x24cc0`
and explicitly set that slot field at `0x24911`, `0x249c0`, `0x29ca1`,
and `0x29d4f`; slot completion `0x3e3d7` clears its descriptor pointer
only. Thus reused slot state and allocator ordering are relevant. The engine
loads the BUILD timer and records the four building kinds' burst particles
as ordinary invisible thinkers, using the native 70 Hz gravity, alternating
sample count, fixed projection and SINCOS.DAT coefficients. Crane and timer
both start reconstruction effects; completion restores HP, clears gun stock
and production, and reloads the timer. Bridge and building effects share a
64-effect limit. The per-kind descriptor count, previous owner, timer and
particle state are saved and hashed. The engine seeds a dedicated native-RNG
stream with 0x3039, so its draws need not match retail's global stream; its
effect count excludes unimplemented effect kinds and does not reproduce stale
slot delays. The conditional selector write at `0x3e919` is unimplemented.
It tests global byte `0x1eb263` at `0x3e8e2`; the same byte is tested by
building paths `0x3fcb0`, `0x41026`, `0x41534` and `0x426e5`, where a nonzero value
skips normal production or display work. Direct absolute references found
in the executable are reads; its initialization and meaning remain
unknown, so this does not establish when to reset the selector. Inspect with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 40 @ 0x3e8c0'
-c 'pd 20 @ 0x4100d' -c 'pd 20 @ 0x426e0' -c '/x 63b21e00' -c q data/Z/ZED.EXE`.
The debris pictures themselves are not drawn. Reproduce via
`r2 -q -e bin.cache=true -c 'pd 100 @ 0x3e551' -c 'pd 35 @ 0x3e678'
-c 'pd 30 @ 0x3ec90' -c q data/Z/ZED.EXE` and inspect each 80-byte
`BUILDnn.DAT` record at +0x49. Run
`SDL_VIDEODRIVER=dummy build/bin/tests/z/test_production` for timer boundaries,
slot contention, descriptor overwrite, projection bounds and saved state;
`test_commander` exercises the crane path.

### Moving-unit object search (2026-11-12)

**Confirmed** in `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
action-table entry 2 at `0x149040` points to `0x7a9e4`, the movement handler
that advances unit position at `0x7aa61..0x7aaa2`. On action 2, with unit
byte +0x5b zero, its `0x7ab0c..0x7ab36` branch searches for an object via
`0x78fed` on nonzero `0x10e91b`, checks `2*DL <= unit[+4]`, calls the
`0x78bfb` guard and dispatches the selected record to `0x61022`. That global
is a parity byte: initialized to zero at `0x4f0f0`, complemented each step
at `0x19e72`; it is not a user preference or 0.5-second timer. The +4 field
is twice the weapon range in eight-pixel units (see unit-stat findings below).

The search uses direction-indexed region and sector tables (`0x78fff`,
`0x79174`, `0x791cd`), the object's +0x13 team visibility mask, signed
coordinate deltas shifted by three, and SQROOT.BIN to rank candidates;
`0x6e491` checks the terrain line between the unit and object. Selection
precedes the type/eligibility dispatch in `0x61022`; its sprite 0x1e0 branch
is the invisible flag marker (`0x61293`), and other accepted records enter
the separate interaction branches. Eligible interactions reserve unit/object
flags, set the interaction target and temporarily change movement speed
modifier +0x89 to 3 (`0x61259..0x612c6`). The marker branch invokes
`0x7ba9a` without replacing the existing waypoint-chain pointer at unit +0x14;
on capture, `0x79e91` starts the robot's cheer and `0x79e96..0x79ec7`
clears the marker reservation and interaction target, then clears the route
only when unit +0x5c is not 0xff. Independent tracing identifies 0xff as
squad leader, 1..20 as follower slot and 0 as ungrouped. Cheer action 0x21
ends through `0x62adb` and `0x79049`; the latter resumes an existing path
only under the signed +0x5a condition. A leader's `0x7d053` copies group
command +0x3a through `0x772bf` into +0x5a. The exact sign lifecycle and
continuation for every other object type remain **unknown**.

This confirms an opportunistic movement-time object search, but does not
establish equivalence to Zod `CheckOpportunisticGrab`: that fork scans every
0.5 seconds, combines separate flag, vehicle and grenade lists and applies
its own eligibility rules. The native direction cones, object reservation,
line-of-sight gates and continuation must be preserved before implementing a
retail detour. Reproduce with `r2 -q -e bin.cache=true -c 'pxw 12 @
0x149038' -c 'pd 45 @ 0x7aaf2' -c 'pd 55 @ 0x791cd' -c 'pd 75 @ 0x61022'
-c 'pd 25 @ 0x61293' -c 'pd 15 @ 0x19e4b' -c q data/Z/ZED.EXE`.

Additional **confirmed** selection details: `0x2a9fb..0x2aa3e` allocates an
invisible object type 28 at the flag's native cell origin. `0x2aa44..0x2aa75`
sets its +0x13 team mask to the complement of `1 << territory owner`, so
the owner's own search excludes its marker. This is distinct from the drawn
flag; the current visible `MT_FLAG` alone is not that native object record.
The direction predicates at `0x79275..0x7931f` select north/east/south/west
45-degree cones and the intervening diagonal quadrants, with signed boundary
tests. `0x791cd..0x79272` performs the visibility and cone tests first, ranks
signed pixel differences shifted by three using SQROOT.BIN, and accepts an
equal rank later in the sector list. It tests line of sight before replacing
the candidate. `0x6e491` rejects a target on nonzero terrain height from the
table at `0x11714b`; `0x6e5b7..0x6e61f` steps a 3-D line and rejects
any intervening cell whose terrain height exceeds the line height. The source
height lookup uses the unit's native terrain cell; `0x6e928` obtains the line
start from native position +8, adding another 8 for unit class 1. This is not
the blocked-cell test used by pathfinding. `0x7ba32` sets the temporary speed
modifier to 2 and each successful interaction increments it to 3. The
vehicle/crate path `0x61337` through `0x612da..0x61336` prepends a route node, unlike the direct
flag correction. On release, `0x7b1c7` clears both reservation bits and sets
object +0x1f to 85; `0x4a677` decrements this timer to zero, and the flag
branch refuses a nonzero timer. This is separate from territory lockout.
Reproduce with `r2 -q -e bin.cache=true -c 'pd 45 @ 0x2a9fb'
-c 'pd 95 @ 0x791cd' -c 'pd 100 @ 0x6e491' -c 'pd 40 @ 0x6131a'
-c 'pd 5 @ 0x7ba32' -c 'pd 6 @ 0x7b1c7' -c 'pd 8 @ 0x4a677'
-c q data/Z/ZED.EXE`.

`0x79174` concatenates the object IDs in nine direction-dependent sectors,
not global object order. The signed word offsets are eight rows of nine at
`0x1047ca`; the sector number is `(x >> 7) + 16 * (y >> 7)`, and each
sector has a 32-byte list and count at `0x1c4b34` / `0x1c6b34`. Native object
allocation `0x4a5d7` reuses the first free one of 200 records. `0x4abc4`
rebuilds the sector lists by scanning those records in ascending native slot
order, appending no more than 32 objects per sector. At `0x4a354`,
ordinary object types start with visibility mask 1, types 9 and 10 use 0xf,
and types 11, 12 and 17 use zero; special vehicle classes have their own
initialization. Therefore iterating every world mobj as visible would not
preserve the native candidate set or equal-distance ordering. In particular,
the engine currently uses `SIGHT_OPEN`, which is not by itself evidence that
the original object's separate search mask may be discarded. Reproduce with
`r2 -q -e bin.cache=true -c 'pd 40 @ 0x79174' -c 'px 144 @ 0x1047ca'
-c 'pd 85 @ 0x4a354' -c 'pd 15 @ 0x4a5d7' -c 'pd 30 @ 0x4abc4'
-c q data/Z/ZED.EXE`.
**Unified represented slot order:** during the traced level setup,
`0x13562` creates flag markers through `0x2a89f`/`0x2a9fb`, and
`0x13835` subsequently calls the OBJECT-record pass at `0x18418`.
That pass checks 150 ten-byte records, skips x = 0xffff and type 0x25,
and calls `0x4a184`, which uses the same first-free allocator `0x4a5d7`
as flag markers and later empty vehicles. Holes in OBJECT files therefore
do not reserve effect slots. Open-rts now stores one represented effect
slot on each thinker for both detour ordering and shell selection; loaded
flag markers and all represented OBJECT records allocate in this order,
and removal or boarding releases their slot. It no longer uses OBJECT
record index as the shell-selection slot or a second independent detour
slot. Z save layout 20 records this field once. Native transient effects
that the engine does not represent can still occupy slots and alter retail
ordering; an unslotted represented object, including a Zod-ejected hull,
can acquire a released represented slot on a later detour scan.
`test_detour` covers flags, sparse records,
the type-0x25 skip and slot reuse; combat tests cover shell ordering,
pool exhaustion and save/load. Inspect `r2 -q -e bin.cache=true -c
'pd 18 @ 0x1354c' -c 'pd 12 @ 0x13821' -c 'pd 25 @ 0x18418'
-c 'pd 25 @ 0x4a184' -c 'pd 15 @ 0x4a5d7' -c q data/Z/ZED.EXE`.
`0x603d8` rebuilds those lists before the unit pass at `0x6040c`. The
leader-only guard at `0x78bfb` invokes `0x64473`, which for group command
+0x3a equal to 1 resolves an object at the group's +0x2c/+0x2e destination,
or a flag there via `0x2aa76`; its result and unit +0x99 control whether the
selected object may divert the leader. This is a separate gate after candidate
selection, not merely a group-membership check.
For the flag lookup, `0x2aa8b..0x2aa94` constructs a rectangle extending
eight pixels from the group destination, and `0x2aaa3..0x2aaaf` tests the
stored flag origin + (4,8). The shared point-in-rectangle function `0x28369`
rejects only values below the low edge or above the high edge, so both edges
are inclusive. For candidate ranks, `0x7923b..0x79241` branches when the
squared index is at least 1024 to `0x7926e`, which sets the rank to 0x30 (48).
Reproduce with `r2 -q -e bin.cache=true -c 'pd 30 @ 0x28369' -c 'pd 45 @
0x2aa76' -c 'pd 30 @ 0x7921b' -c q data/Z/ZED.EXE`.
For continuation, `0x7d053..0x7d09d` copies the group's positive command and
destination into the leader's +0x5a/+0x50..+0x54; `0x7c780` and `0x7c949`
negate +0x5a after particular command dispatches. On reaching a movement
destination with no interaction in progress, `0x7ae93..0x7aea8` negates
the special value -5 back to 5 only when no path node remains and +0x31 bit
0x20 is clear. Other negative commands enter the branches at `0x790a2` after
the cheer. `0x7c027..0x7c037` dispatches the magnitude of +0x5a through the
table at `0x14b088`; entry 1, `0x7c560`, places the stored destination in a
path node before negating the command. `0x7bd1f` subtracts (8,8) from a node's
stored centre to produce native top-left coordinates; `0x7bde1` consumes the
node only at that exact top-left. `0x7cdaf` treats magnitude 1 as complete
when native position +8,+8 equals the unit destination +0x50/+0x52; the leader
clears group command +0x3a when this point also equals group destination
+0x2c/+0x2e. These observations do not prove a complete mapping for every
command or failed path.

**Confirmed and implemented:** crate collection at `0x79b25..0x79b9e` scans
objects and tests `abs(unit.x-object.x) < 16` and `abs(unit.y-object.y) < 16`
for eligible robot classes, using native sprite origins rather than sprite
centres. Thus an offset of (15,15) is accepted and either axis at 16 is not.
`games/z/p_capture.c` uses the native robot and crate origins and this strict
square; the focused `env SDL_VIDEODRIVER=dummy build/bin/tests/z/test_capture`
covers all four exclusive boundaries, the diagonal corner and existing squad
sharing. The current engine still invokes collection from its per-tic robot
pass; this does not establish exact native terrain-dispatch timing.

**Terrain-dispatch correction, confirmed and implemented:** The
strict square above is the test *inside* the pickup handler, not by itself a
sufficient condition for a pickup. `0x796cb..0x796f1` samples the terrain
cell containing the robot's native origin plus (8,8). At `0x7984a` terrain
0x24 maps through `0x1157ea` to 0x6b; `0x79924..0x7992a` dispatches table
entry 7 at `0x14ad34` to pickup handler `0x79b25`. Thus the earlier statement
that (15,15) is accepted describes only the handler's geometric check: if
that robot's sampled cell does not contain terrain 0x24, this path does not
run. Conversely, a nearby 0x24 cell belonging to a different effect may
dispatch the handler, which then scans all crate slots.

Map effect creation `0x4a1f1..0x4a276` saves the previous terrain byte in
effect +0x14 and writes 0x24, retaining bit 7, for descriptors with neither
flag 4 nor low two flag bits >= 2. This includes the two crate descriptors
and other map objects. Flag 4 instead writes 0x2e, at y+16 when flag 2 is
also set; effect types 0x47..0x4d skip the final terrain write. On pickup,
`0x79bbe..0x79bd5` restores the selected crate's saved byte at its origin
before freeing its effect slot. Overlapping effects therefore depend on
creation and removal order; simply treating every living crate as an
independent 0x24 overlay would not reproduce this behavior. Inspect with
`r2 -q -e bin.cache=true -c 'pd 12 @ 0x796cb' -c 'pd 13 @ 0x7983f' -c
'pd 6 @ 0x79924' -c 'px 1 @ 0x11580e' -c 'pxw 4 @ 0x14ad34' -c 'pd 55 @
0x4a1f1' -c 'pd 12 @ 0x79bbe' -c q data/Z/ZED.EXE`.

Open-rts stamps represented map objects in native record order, saves and
hashes effect +0x14, gates pickup on the sampled tile and restores the saved
byte on pickup. Crate destruction still follows its separately traced 0x16
terrain change. `test_capture` checks a neighboring unmarked tile, overlapping
crate order and saved bytes across reload, restoration across reload, and
non-crate 0x24/0x2e markers. Objects which the existing loader cannot
represent or cannot load a sprite for do not stamp terrain. Its cliff rebuild
reconstructs terrain from the saved ground plane and may overwrite object
marks on cliff cells; the exact interaction of subsequent native cliff
rebuilding and effect removal remains unknown. The once-per-tic handler
dispatch also remains an approximation of native update timing.

**Vehicle effect scan is not established as active:** `0x57d6a..0x57dd5`
contains a 200-slot scan that would remove the first nearby bit-0x80 effect
and restore its saved terrain byte. However, the vehicle update at `0x57de2`
calls `0x57d69`, which is an immediate RET, not `0x57d6a`. The broad linear
disassembly has no direct call or jump to `0x57d6a`; indirect entry remains
unverified. Do not infer that vehicles destroy nearby crates from the scan
alone. Reproduce with `r2 -q -e bin.cache=true -c 'pd 55 @ 0x57d69' -c
'pd 10 @ 0x57dd6' -c q data/Z/ZED.EXE` and search direct references in
`reverse/z-ui/service-code.txt`.

**Confirmed slot-order correction:** `0x79bea..0x79c38` divides the crate
total by group +0x12, then traverses the 21 member slots beginning at +0x13.
The remainder is applied to slot zero and cleared before the next slot. For
a valid group slot zero is its leader, including a promoted leader. The
prior open-rts code instead gave the remainder to the first matching thinker,
which can be a follower after promotion or a different allocation order.
`test_capture` creates followers before their leader and checks an exact
8/6/6 split for 20 grenades. Inspect with `r2 -q -e bin.cache=true -c
'pd 30 @ 0x79bea' -c q data/Z/ZED.EXE`.

**Overlapping crate order:** `0x79b53..0x79bdc` walks effect slots from
zero upward and consumes the first eligible crate within the strict pickup
square. Open-rts now selects the lowest represented slot, with unslotted
engine fixtures following slotted objects, rather than relying on thinker
creation order. `test_capture` creates overlapping 20- and 10-grenade crates
in the opposite order to their slots, saves and reloads, and verifies that
the lower-slot 10-grenade crate is collected. Inspect with `r2 -q -e
bin.cache=true -c 'pd 65 @ 0x79b53' -c q data/Z/ZED.EXE`.

**Correction, native non-crate interaction (confirmed instructions; runtime
address unknown):** The strict-square description above holds until the scan
reaches a nearby active effect with +0xd bit 0x80 that is not a grenade box.
At `0x79b8b` the handler overwrites EDI with that effect's descriptor pointer;
`0x79b9c` and `0x79bdd..0x79be1` continue scanning without restoring it, so
the following signed distance comparisons at `0x79b75` and `0x79b86` use the
pointer's low word as DI. Default effect initialization sets bit 0x80 at
`0x4a41a`, while the empty-gun/vehicle branches and object types 0x0b, 0x0c
and 0x11 do not retain it. Terrain 0x24 still has to invoke the handler first.
The descriptor table is shown at image address `0xe5974` with 25-byte entries,
but its references are relocated: the actual low word at runtime has not
been established. Consequently no fixed enlarged radius can be justified from
the static image address. Open-rts continues to apply the 16-pixel check to
represented crates, and therefore does not reproduce this native register
reuse. Reproduce with `r2 -q -e bin.cache=true -c 'pd 65 @ 0x79b25' -c
'pd 80 @ 0x4a354' -c q data/Z/ZED.EXE`.

**Implemented with stated engine scope:** `games/z/p_detour.c` applies the
native sector ordering, initial object visibility masks, direction predicates,
SQROOT ranking, terrain line and flag-destination guard to ordinary ground
`TC_ORDER` movement. The chosen object is dispatched after ranking; eligible
flag markers, grenade crates and empty vehicles reserve their target. A flag
diversion preserves the leader's destination through the capture cheer;
followers and ungrouped robots clear that native path. Crates resume the
interrupted movement; boarding ends it, matching `0x7b257..0x7b264`, which
clears group command +0x3a, unit command +0x5a and the path. New commands,
ownership changes, death and removed or already-captured targets release
reservations. The stable IDs, slot, interrupted destination and interaction
state are saved and included in consistency checks.

The restriction to ordinary orders is **Zod policy**, not a proven mapping of
native action 2: Zod `CheckOpportunisticGrab` excludes `FORCE_MOVE_WP`, so
engine `TC_MOVE` remains forced movement. The engine samples at its 30 Hz
tick; retail checks on alternate 70 Hz frames. Its open-sight policy and
initial object masks do not implement the full native changing-visibility
lifecycle, and the native object allocation/sector view is reconstructed from
the loaded thinker-owned objects. The one-shot formation rejoin after a
follower's flag cheer is Zod-derived engine behavior, not a proven direct
native flag-capture branch. The focused `env SDL_VIDEODRIVER=dummy
build/bin/tests/z/test_detour` exercises the three object classes, forced
movement, range and destination boundaries, direction and height, selection
order/ties, save/restore and queued order continuation, ownership changes and
the different ungrouped flag outcome.

### Native squad leadership

**Confirmed**, DOS `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
`0x77fd6..0x78079` attaches a unit to a 72-byte group at
`0x1943f8 + 0x48 * unit[+0x5d]`. Group +0x12 counts members; +0x13
stores the leader unit index and +0x14 onward holds follower indices.
The first member receives unit +0x5c = 0xff; later members receive their
slot number 1..20. Zero means no group. The removal path
`0x77821..0x77870` clears the departing member's slot and decrements the
count. When the removed unit was leader and members remain, `0x77b97`
scans the follower slots in ascending order, promotes the first non-0xff
entry to group +0x13, sets its unit +0x5c to 0xff and transfers the former
leader's path pointer +0x14 and unit bytes +0x5a/+0x5f and words
+0x50/+0x52/+0x54. `0x77c61` also updates references to the old leader.
This establishes native first-survivor promotion, but not the semantics of
every transferred order field or the complete movement formation policy.
When a group becomes empty, `0x77880..0x77888` writes 200 to its byte at
`0x11d567 + group index`. `0x775f5..0x77619` decrements each nonzero byte
once per pass. The first allocation scan at `0x77d25` prefers empty group
records with a zero byte; its fallback at `0x77d49` accepts any empty
group, so this is a delayed-reuse preference rather than a capacity limit.
`0x775e4` clears all 240 cooldown bytes. In the production spawn loop,
failure from `0x50fc9` or `0x50c5c` returns carry immediately at `0x506f0`
or `0x5078c`; no rollback of earlier members appears on those exits.
Zod's leader-held grenade transfer remains distinct from the native
member distribution traced at `0x79bea`. Reproduce with
`r2 -q -e bin.cache=true -c 'pd 45 @ 0x77fd6' -c 'pd 50 @ 0x77821'
-c 'pd 50 @ 0x77b97' -c q data/Z/ZED.EXE`.

**Additional confirmed field uses:** when +0x50 equals `0xd80`,
`0x756b7..0x756d5` compares +0x52/+0x54 to the unit's +0x2c/+0x2e
position as coordinates. The different `0x7bf4e` path instead tests
+0x54 for 2 and passes +0x50/+0x52 as coordinates. These words are
state-dependent; they cannot all be given one universal coordinate meaning.
Reproduce with `r2 -q -e bin.cache=true -c 'pd 50 @ 0x756a1' -c
'pd 25 @ 0x7bf4e' -c q data/Z/ZED.EXE`.

The current implementation stores a group ID and native-style slot on each
member, promotes the lowest live slot on death, boarding or team departure,
and replans the transferred movement goal from the new leader's position.
It replaces the survivor's old movement, boarding and service work and
transfers the engine route and attack target. This translates the observed
promotion into existing engine orders; exact retail state-field mapping,
boarding continuation and follower formation movement remain **unknown**.
`SDL_VIDEODRIVER=dummy build/bin/tests/z/test_squad` exercises selection,
slot promotion, route transfer, stale work cleanup, lethal damage, ownership
change, crew slot normalization, a save/load checksum round trip and travel
after load; it does not establish exact retail formation behavior.
When the new leader cannot replan, shared navigation discards the old follower
path and the old leader's path is not copied. No retry timing is inferred;
the blocked-promotion case is covered by the same test.

**Follower formation confirmed, dispatch incomplete:** `0x506c2` copies byte
+1 of the ROBOTS.DAT 82-byte group record into group +8 and +0x3c.
Startup `0x77f62..0x77fd5` converts five 15-by-15 grids beginning at
`0xe63d4` (225 bytes each) into five 20-pair tables at `0x1582a0`, using
signed grid positions −7..7 shifted left by the five values at `0x14aa75`
(all 4 in this executable). Missing numbered cells leave zero entries.
`0x78e21` uses positive unit +0x5c minus one as a table index, group +8
plus one as the pointer-table index at `0xe63bc`, rotates its signed pair
using native trig scaled by 1024 (`0x642ec`), and adds the leader's
+0x2c/+0x2e position. It refuses ordinary following without group +9 bit 0.
The angle is quantized to eight directions by `0x61eed`; the native
SINCOS.DAT (sine, cosine) coefficients after byte swap and signed >>5 are
`(0,1023), (724,724), (1023,0), (724,-725), (0,-1024),
(-725,-725), (-1024,0), (-725,724)`. `Z_FormationOffset` stores these and
the five derived grids as C literals; `test_squad` compares all eight
rotations with the decoded asset. Each product is shifted separately before
addition or subtraction, including the negative rounding: `x' = (x*cos >>
10) + (y*sin >> 10)` and `y' = (y*cos >> 10) - (x*sin >> 10)`. This is
the register order at `0x78eef..0x78f2b`; it corrects a first investigation
that had interchanged sine and cosine.
`0x78d7d` calls it and uses a 32-pixel squared-distance boundary at
`0x78df3`; `0x7aefb` temporarily sets the group bit to calculate a rejoin
target. `0x7815e` temporarily sets the bit while refreshing each follower,
called when group +0x3f counts down to zero at `0x60517`. Leader movement
sets the group bit at `0x7bb24` and `0x7aabe` when group +0x3f is zero;
the leader's `0x7ae44..0x7ae6a` path clears it. The complete unit-state
gates, other enable/disable transitions, and individual-order interaction
remain **unknown**. Do not substitute Zod's replan cadence or
force a follower away from a player order based on this partial trace.
The current implementation dispatches a grouped player order once to the
leader, rotates native slot offsets using the leader's current path leg,
and replans an idle follower when its slot is at least 32 pixels away while
the leader has an active movement order. Active diversions are not
interrupted. Following a flag diversion, the explicitly adopted Zod rejoin
behavior requests one formation plan even if the leader has stopped; group
Stop or a new group order cancels it. Native `0x7bd39` sets +0x62 bit 0x80
for an alternative path and `0x7aee5..0x7af0a` consumes it to refresh the
formation once with group +9 bit 0 temporarily set. Whether the native flag
capture path always establishes that bit is **unknown**; its use here to
connect flag diversion to stopped-leader rejoin is an engine adaptation.
The engine's path planner and moving-leader gate are not a complete port of
native +9/+0x3f state transitions. Both player and AI group orders use one
leader route. `test_squad` compares the grids against file offset `0x11bdd4`
in ZED.EXE and tests turns, route deduplication, diversion, stopped-leader
rejoin, cancellation and save/load; `test_commander` covers AI following.
The player-order path `0x47aec..0x47b52` writes the selected unit's +0x5a
and, whenever its +0x5c is nonzero, also writes the command and operands
to group +0x3a/+0x2c/+0x2e/+0x30; `0x7d053` copies those group fields to
the leader. Group rotation `0x61eed` transforms the eight-way result of
`0x5a648` into 0..1023 in steps of 128, clearing each member's +0x31 bit
0x80 when the direction changes. This confirms group propagation on that
path, not every possible individual-order exception.
Inspect with `r2 -q -e bin.cache=true -c 'px 1125 @ 0xe63d4' -c
'pd 45 @ 0x77f62' -c 'pd 90 @ 0x78e21' -c 'pd 60 @ 0x78d7d' -c
'pd 35 @ 0x7aec2' -c q data/Z/ZED.EXE`.

### Movement modifier investigation

**Confirmed instruction behavior**, `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
`0x79882` computes the terrain and body-speed terms separately, adds 3
when unit +0x5c is nonzero and the record at global 0x103f84 has a
nonzero byte +0x3d, then adds unit byte +0x89 and clamps the result to
0..12. The same routine sometimes substitutes terrain term 10 based on
unit and group flags. `0x7ba2a`, `0x7ba32` and `0x7ba3a` set +0x89 to 4,
2, or conditionally 0; `0x6126b..0x61270` and `0x612bb..0x612c0` set it
to 2 and increment it while scheduling an object or flag interaction.
`0x7c5a8..0x7c5ff` has additional assignments 12 and 2 based on unit
state. Reproduce with `r2 -q -e bin.cache=true -c 'pd 65 @ 0x79882' -c
'pd 20 @ 0x7ba2a' -c 'pd 45 @ 0x7c58c' -c q data/Z/ZED.EXE`.

`0x796cc..0x796f1` samples the native terrain byte at unit top-left +8,+8,
masks off bit 7 and indexes the signed table at `0x1157ea`. Fixed entries
are +20 for terrain 7, 17, 23; -20 for 8, 22, 39, 59; -10 for 9; -30
for 42; +90 for 31; other ordinary entries are zero. Values 100..112
dispatch interactions at `0x14ad18` and return a zero terrain term. The
two signed divisions at `0x798c3..0x798cf` occur separately before addition,
so e.g. body speed 39 and terrain -20 give level 1. `Z_MobjTicker` now
applies these independent terrain terms and clamps through `Z_LevelSpeed`;
vehicle body degradation uses its mutable speed byte. Other conditional
movement terms described here remain unmodeled. `test_terrain_speed` checks
positive/negative terrain, high-bit masking, clamp, interaction terrain,
vehicle damage, and return to plain ground. The shared movement engine uses
average speeds, not the executable's whole-pixel-per-frame pattern.

**Confirmed countdown correction:** the record at `0x103f84` is the active
72-byte squad record. Its +0x3d byte is a countdown, not a rechargeable
stamina value. Player command dispatch sets unit +0x98 bit 0x20 at
`0x47af6`. The leader's route setup (`0x7c6ba..0x7c6e1`) tests that bit
and excludes unit +0x6f bit 0x10, then sets squad +0x3d to 120, or 30 when
squad +0x10 is zero, and clears the command bit. The group pass
`0x780b3..0x78108`, called before the unit pass at `0x603f1`, decrements
+0x3d with saturation at zero; it also clears +0x3d when the group's
+0x10 countdown is zero or expires. Target-response paths `0x759b3`,
`0x759d6` and `0x75cb9` load +0x10 with 75. Nonzero +0x3d both adds the
three movement levels above and suppresses the walking unit's enemy scan
at `0x7ab3d..0x7ab41`. This is coupled order/target state, so merely giving
every move order a 30- or 120-frame speed boost would be incorrect.
Reproduce with `r2 -q -e bin.cache=true -e bin.relocs.apply=true
-c 'pd 50 @ 0x780b3' -c 'pd 20 @ 0x7c6ba' -c 'pd 20 @ 0x47aec'
-c 'pd 35 @ 0x759a7' -c q data/Z/ZED.EXE`.

**Unknown:** the precise semantics and all entry conditions for the other
modifiers and the complete squad target-state lifecycle. They do not establish
Zod's 1.8 speed multiplier or its stamina recharge and exhaustion policy;
those remain fork behavior, not confirmed native mechanics. The earlier
unqualified base-speed formula below omitted these conditional modifiers.

**Confirmed projectile reaction dispatch:** the per-unit update at
`0x60579..0x6058d` dispatches action through dword table `0x149038`;
entry `0x2b` points to `0x62cec`. With global `0x10e91b` clear, this
handler calls `0x61fac`, advances frame byte +0x32 toward a value derived
from facing, decrements +0x7d, then calls `0x79049`, which returns the
eligible robot to idle action 1. `0x61fac` checks group flags and nearby
target eligibility, not position or damage. `0x79049` may also re-evaluate
an existing order when unit +0x5a is negative. The handler itself writes
no position or waypoint. The proximity caller admits only actions 0 and 1,
so an attack already in progress is not interrupted. No explicit action
0x2b damage-immunity comparison was found. This supports an animation/state
reaction to nearby projectiles, not Zod's random displacement waypoint;
possible indirect effects outside this handler have not been ruled out.
Reproduce the table and handler with
`r2 -q -e bin.cache=true -c 'pxw 4 @ 0x1490e4' -c 'pd 18 @ 0x62cec'
-c 'pd 24 @ 0x79049' -c q data/Z/ZED.EXE`.

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
  Specifically, action `0x76098` fires at frame 2 when unit +0x78 is zero;
  after the animation cycle `0x760d5..0x760f0` increments unit +0x9c modulo
  four and, on zero, loads the frame timer with eight times `(100 - +0x75)`,
  saturated at 127. The shipped Pyro's fire-rate byte gives 16. The frame-2
  shot normally repeats after four native frames; on the fourth cycle the
  signed timer must decrement past zero, giving a 21-frame shot interval.
  A grenade uses a different action: `0x63628..0x6362f` loads twice
  `(100 - +0x75)` into +0x78, four steps for a Pyro, and does not increment
  the flame cycle counter. Dispatch table `0x149038` entry 0x41 points to
  `0x76098`; its drawing record at `0x14b210` specifies base sprite 6022
  with three pictures per facing. The shot displays frame 2, and the cycle
  resets to frame 0 before its recovery wait. Dedicated Pyro shot/recovery states now advance
  the existing saved and hashed shot counter at recovery entry; the shared
  robot grenade action preserves it and sets twice the firing delay, four
  native steps for Pyro and 40 for Grunt or Laser. `test_combat` exercises
  each through normal targeting and throw animation. The Pyro flame action
  resolves the engine's current valid target before choosing whether to
  throw, so a retargeted grenade does not advance its flame cycle. This is
  an engine integration rule, not a recovered retail retargeting sequence. With the
  engine's 30 Hz tic quantization, a stationary test observes firing gaps
  of 2, 2, 2 and 9 tics. The separately traced 70 Hz animation does not map
  exactly to 30 Hz, and projectile and flame area remain unported. The prior
  shared-reload attempt was discarded: its generic attack animation produced
  shots every 12 engine tics regardless of the intended reload. Inspect with
  `r2 -q -e bin.cache=true -c 'px 4 @ 0x14913c' -c 'px 4 @ 0x14b210' -c 'pd 65 @ 0x76098' -c 'pd 20 @ 0x6360e'
  -c q data/Z/ZED.EXE`; run `SDL_VIDEODRIVER=dummy build/bin/tests/z/test_combat`.
- Base speed level = terrain term / 10 + unit speed / 10, with additional
  modifiers before clamping to 0..12 (`0x79882`; see the movement modifier
  investigation below; terrain bytes at `0x1157ea`, not decoded per terrain). Each
  level walks a repeating pixels-a-step pattern (`0x7bf10`, table
  `0x117e5c`) averaging 1/5, 1/4, 1/3, 2/5, 1/2, 2/3, 4/5, 1, 4/3, 3/2, 2,
  7/3, 5/2.
- Bullets (`0x5af81`) lose **5** (`0x103f49`) against anything that is not
  a robot class, leaving 0 or 1 at random when nothing is left
  (`0x5ae44`). A bullet that hits a gun or a Jeep rolls
  `rand & 0x7f` against the shooter's accuracy (halved against guns); on
  success the driver takes the shot and an emptied vehicle stands for
  anyone to take (`0x5ad72..0x5ae0d`, `0x860fd`). Correction: this includes
  all four guns, not just the Light Gun. Setup `0x63afb..0x63b2f` assigns
  runtime class +0x79 = 2 to Light Gun, Gatling, Howitzer and Missile Gun;
  `0x5ad74` accepts class 2 and sets the accuracy shift to one. The native
  subtract at `0x86d39` and signed return to `0x5ae0d` empty the gun when
its driver's health becomes negative; a result of exactly zero remains
occupied until a further driver hit. `test_combat` checks that boundary on
a Jeep as well as the subsequent neutralization. Open-rts uses this gun set and
  half-accuracy rule; its deterministic hit coin and impact timing remain
  approximations. `test_combat` checks driver hits and neutralization for
  all four guns and the ordinary tank exclusion. Reproduce with `r2 -q -e
  bin.cache=true -c 'pd 45 @ 0x63aa6' -c 'pd 55 @ 0x5ad72' -c 'pd 20 @
  0x86d1f' -c q data/Z/ZED.EXE`. Type 0x1b and bodies with
  +0x19 = 3 are also eligible; neither is identified.

  **Disproven armor hypothesis:** the bullet test at `0x5ae2c..0x5ae42`
  compares the runtime class byte +0x79, not the weapon type byte. Although
  Sniper and Psycho use weapon types 0x11 and 0x12, their constructor branches
  `0x50e25..0x50e2e` and `0x50e1a..0x50e23` explicitly write runtime class
  zero. Both therefore bypass the five-point reduction alongside Grunt,
  Tough, Pyro and Laser. The existing `Z_IsRobot` exclusion is consistent
  with those branches; changing Sniper/Psycho to armored targets would be
  incorrect. Reproduce with `r2 -q -e bin.cache=true -c 'pd 16 @ 0x5ae29'
  -c 'pd 10 @ 0x50e1a' -c q data/Z/ZED.EXE`.

  **Additional instruction-level finding:** vehicle setup at
  `0x637e7`, `0x63838`, `0x63889`, `0x638f4` and `0x6399b` initializes
  body +0x19 to -1. `0x58454..0x5847b` starts an ascending sequence when
  +0x19 is below 3; `0x584e9..0x58531` advances or reverses it using
  body +0x1a/+0x1b, and `0x58531..0x58580` invokes a secondary firing
  path when +0x19 reaches 3. `0x5adb0..0x5adbb` tests that same body byte
  for driver-hit eligibility. This links the driver vulnerability to an
  animated secondary-weapon state. The call at `0x58214` passes the acquired
  target in ESI and the attacking vehicle in EBP to `0x583b2`: target types
  Jeep 0x22, Gatling 0x16, and class 0 run an accuracy-dependent pair of
  `0x640ff` tests under a +0x97 countdown, target classes 1 and 2 otherwise
  request closure, and other classes request opening. `0x58454` starts
  opening with a five-frame timer; the animated update and secondary-fire
  path run from `0x57dec` every vehicle pass. Exact player/AI triggers,
  probabilities and per-type behavior remain **unknown**. Mobile missile type 0x18 has an
  additional facing and half-accuracy check at `0x5add9..0x5adfb` if it
  reaches that eligibility branch; its ordinary setup is `0x63955`.
  Open-rts does not yet model that secondary state, so do not grant all
  vehicles permanent driver vulnerability. Reproduce with `r2 -q -e
  bin.cache=true -c 'pd 45 @ 0x6392b' -c 'pd 90 @ 0x58454' -c 'pd 55 @
  0x5ad72' -c q data/Z/ZED.EXE`.
- Shells, rockets and grenades (`0x59ed1`/`0x5a205`/`0x5a39e`) use the
  explosion path (`0x85ce4..0x85e9f`); class-0 robots throw strength 50
  (`0x103f47`). Correction: its selection point is not the unit top-left;
  see the projectile impact analysis below.
- A hit taking a body to or below 3/4, 1/2 or 1/4 of its hit points scales its
  mutable speed and fire-rate bytes by 90%, 85% or 80% (`0x86d55`). A moving vehicle
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

Engine boundaries, **not** native equivalence: shells apply their existing engine damage over the candidate box below;
vehicle crush, the tanks' apparent
secondary gun (weapon +0x25..+0x27: 4 damage, rate 80; inferred only),
terrain speed terms and the Pyro flame (`0x5a099`) remain incomplete. The
Fire Truck path is now implemented in the bounded form below.

**Bullet travel trace:** `0x5af81` rejects equal endpoints
and zero speed before allocating; `0x5b021` chooses the first free one of 200
28-byte records. `0x5b055..0x5b1a5` stores the source and target, owner/team,
damage, range and speed, and initializes a signed Bresenham error and major/
minor distance. Robot weapon +0x23 supplies unit +0x76 at `0x50e0d`, which
the normal robot firing call passes as speed; this is not the cooldown byte.
The main loop's `0x15387` calls the bullet pass at `0x5a8e6`, which visits
active slots in order. Its step functions at `0x5b1a6..0x5b2e1` advance at
most the speed byte in major-axis pixels. `0x5a90d..0x5aab6` checks the
resulting point against units, range and terrain, rather than checking every
intermediate pixel. A completed initial segment makes two more collision
checks: at its endpoint and after an extra step with remaining distance
increased by 16; those checks' return flags are not used to skip the second
check. `0x5acca..0x5ad64` scans nearby spatial bins, skips same-team units,
and uses inclusive native-origin rectangles of ±8 pixels for nonvehicles
and ±16 for class-1 vehicles after subtracting (8,8) from the bullet point.
`0x64138..0x64198` builds the unit bins before the unit pass, visiting native
unit slots 0..239 in ascending order, excluding units whose group byte is 0xff
or whose group +9 has bit 0x20, and accepting at most 32 indices per bin.
`0x7a7c4` derives
the byte bin index as `(origin.x >> 7) + 16 * (origin.y >> 7)`; the bullet
search starts at `((bullet.x-8) >> 7) + 16 * ((bullet.y-8) >> 7)` and adds
the signed table at `0x1047b8`: {0, -1, 1, -17, -16, -15, 15, 16, 17}.
The index is checked against 0..255 after addition, rather than checking
each neighbor's x coordinate independently. Before the normal range and unit
tests, `0x5aa49` also calls `0x88b13` on the old bullet position minus (1,1).
That routine can remove the first qualifying record in the separate
100-entry, 64-byte map-object array at `0x191ba8` within an inclusive ±16
square. This shares the `0x8790a` burst-animation path with the separately
traced flame scan above; its precise record types and complete lifecycle
remain unknown. All six shipped ROBOTS.DAT weapon records for Grunt, Sniper,
Psycho, Gatling, Jeep and Laser have byte +0x23 equal to 15. The scan order,
terrain rejection and endpoint extension matter when another
unit intercepts the shot or a target moves; a delayed hit on the original
target would not reproduce them. The game loop is capped at one step per
70 Hz PIT tick (above), but the timing of a newly spawned bullet within the
engine's 30 Hz thinker pass still needs verification. Spawn muzzle and aim
for every bullet weapon, representation of native reusable unit-slot ordering,
and all collision cases remain to be resolved before replacing the existing
instantaneous paths for the other weapons. Inspect with
`r2 -q -e bin.cache=true -c 'pd 45 @ 0x5af81' -c 'pd 110 @ 0x5b055' -c
'pd 120 @ 0x5a8e6' -c 'pd 85 @ 0x5acca' -c 'pd 45 @ 0x64138' -c
'pd 40 @ 0x88b13' -c 'pxh 18 @ 0x1047b8' -c q data/Z/ZED.EXE`. For the
speed bytes, run `for n in 2 4 7 11 13 18; do od -An -tu1 -j
$((0x55f0+n*42+35)) -N 1 data/Z/ROBOTS.DAT; done`.

**Implemented subset:** Grunt, Sniper, Psycho and Laser fire against units launch an ordinary
`MT_BULLET` thinker. It snapshots the aim, uses the 15-pixel
Bresenham step, the shooter's 120/144/120/136-pixel range and launch-height comparison, tests represented
enemy units using the nine native bin offsets and 8/16-pixel rectangles, and
performs the two endpoint checks. It shares the saved and hashed trajectory
fields already used by water projectiles. Terrain, a moving target, another
enemy, or removal of the shooter can therefore affect a shot after launch;
`test_combat` covers real attack dispatch, interception, terrain, source
removal, moving targets and mid-flight save/load. The bullet uses thinker order
as an approximation of reusable native unit slots and omits the native group
bit and 100-record map-object interactions. It applies the existing
deterministic combat RNG to one launch-time tracked-aim sample, rather than
the native cached aim and its additional 0x773bd scatter; impact after the
source has been removed uses the recorded damage with a simplified armor
fallback. Laser drawing, exact
unit-slot and cross-projectile update order remain unimplemented.
Sniper and Psycho use the same `0x77364..0x773cd` firing path as Grunt;
Laser's `0x77542..0x77577` also adds (2,2), scatters and calls `0x5af81`,
with additional visual flags. The engine draws the normal tracer for the
first three; Laser's native drawing is not implemented. `test_combat`
checks each specialist's muzzle, speed, damage and range, real thinker flight,
and save/load before impact. Inspect with `r2 -q -e bin.cache=true -c
'pd 45 @ 0x77364' -c 'pd 45 @ 0x77512' -c q data/Z/ZED.EXE`.

Gatling also fires an ordinary bullet: `0x783d7..0x783e6` selects type 0x16
and `0x784b5..0x784f3` calls `0x5af81` using the facing-dependent pivot
selected by `0x64227`. The engine uses that eight-entry pivot and the
existing fixed-gun tracked-aim scatter, 15-pixel speed, 120-pixel range and
6 damage. `test_combat` checks all eight muzzle origins, normal attack
dispatch, saved flight and a target hit by both native endpoint checks.
The same slot, terrain and RNG approximations
described above remain. Inspect with `r2 -q -e bin.cache=true -c 'pd 105 @
0x783d7' -c 'pd 14 @ 0x64227' -c q data/Z/ZED.EXE`.

Jeep type 0x22 follows `0x58bc5..0x58be4` to `0x58d20`. Its muzzle at
`0x59116..0x5915e` adds the signed byte pair indexed by hull facing at
`0x14b484` and the signed word pair indexed by weapon facing at `0x148b60`
to the vehicle origin. The firing path scatters the aim at `0x58d87`, calls
`0x5af81` at `0x58dea`, and increments the resulting bullet's range byte at
`0x58e00`; its 120-pixel body range therefore becomes 128 for flight. The
engine implements those offsets, 15-pixel speed and 4 damage using the same
ordinary bullet thinker. `test_combat` verifies all 64 hull/weapon facing
combinations, normal attack dispatch, saved flight, and impact after source
removal. Its moving-target prediction, input-mode aim, native RNG,
native slot order and chain effects remain approximate
or unimplemented as described above. Inspect with `r2 -q -e bin.cache=true
-c 'pd 45 @ 0x58bc5' -c 'pd 85 @ 0x58d20' -c 'pd 30 @ 0x59116' -c
'px 16 @ 0x14b484' -c 'pxh 32 @ 0x148b60' -c q data/Z/ZED.EXE`.

Reproduce: `make build/bin/tests/z/test_units build/bin/tests/z/test_commander
build/bin/tests/z/test_ai`, then run each with `SDL_VIDEODRIVER=dummy`
(`AI_TRACE=1` on `test_ai` prints the computer's course each minute).

### Shell blast selection (2026-11-12)

**Confirmed** in `data/Z/ZED.EXE`, SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
`0x592ff` reads the projectile's current x/y, and ordinary impact at
`0x593aa` subtracts `(12,24)` before `0x8476c`; `0x84c4e` copies those
coordinates into the explosion record. `0x85ce4..0x85cfe` selects bounds
record x−6 through x+38 and y+10 through y+42. `0x64384..0x643bd`
compares inclusive bounds with unit x/y plus 8, or plus 16 for class-1
vehicles. Thus the bounds relative to the projectile impact are x−18..+26,
y−14..+18. `0x85d33..0x85d43` filters active units and the shooter's team;
`0x85d7a..0x85e9f` calls `0x86ca4` to damage the selected bodies. The
separate robot-knockdown routine `0x58745` described above is not this call
site; its relationship to other effects and complete combat remains unported.

**Implementation boundary:** shells now travel to a snapshot of the target's
selection anchor; native scatter remains incomplete.
The delayed third-frame unit pass is approximated as described below.
`Z_BlastAnchor` recovers the spawn
origin from its sprite size, then applies the native selection offset;
composed vehicles are initialized with a 16×16 spawn extent. The shell types
are the missile-bearing units in pinned Zod `src/zsettings.cpp`; their exact
retail class dispatch is not fully traced. The engine applies existing direct
weapon damage once to each enemy unit in the box. At impact, building damage
uses a separate native point lookup: `0x593aa` passes impact minus (12,24)
to `0x8476c`, which calls `0x3edc2`; with `0x14c05f` clear, that adds
(16,16) and calls `0x281da`. The ordinary shell path does not set this
flag; the flame-area routine sets it temporarily at `0x86904` and clears
it at `0x86919`. This port assumes it is clear for ordinary shell impacts.
The latter selects the first live BUILD record whose descriptor footprint
contains impact plus (4,-8), with inclusive edges, and `0x3ee3f` subtracts
the damage without a team test. Open-rts selects the lowest native building
index in that footprint and permits friendly building damage. Forts remain
excluded by their native separate lifecycle. `test_combat` checks a friendly
non-target factory, both inclusive edges and an outside point. Terrain and
other object damage and retail knockdown remain incomplete. Grenades use the
same rectangular explosion path, as confirmed by `0x5a280..0x5a288` clearing
projectile flags then setting bit 4, and `0x59363..0x593d0` taking the
ordinary non-water branch to `0x8476c`. This supersedes the earlier authored
3×3 weighted blast. Open-rts uses
the actual landing position for the explosion. Z save layout 15 distinguished
the new in-progress blast lifecycle. `test_combat` exercises real flight,
pending-impact save/load, the inclusive rectangle, ally exclusion and a
single delayed hit. Inspect with `r2 -q -e bin.cache=true -c 'pd 30 @
0x5a26d' -c 'pd 70 @ 0x59363' -c q data/Z/ZED.EXE`.
Zod instead applies radial distance falloff in
`src/zserver.cpp:ProcessMissileDamage`; that is not the retail rectangle.

**Grenade flight trace, partial:** `0x636cc..0x636f8` adds the signed
facing-indexed dword pair at `0x149174` to the robot's +0x2c/+0x2e before
calling `0x5a205`; it derives ESI from robot byte +0 divided by eight, with
zero replaced by one. The call's earlier branches select target coordinates,
including +8,+8 at `0x63689..0x63694`, or an accuracy-dependent adjustment
through `0x64250` at `0x63699..0x636c7`. That helper skips scatter when
the SQROOT.BIN distance from `0x60be8` is at most 6; otherwise it derives a
radius from a `0x70d37` random value divided by `65535 / max(1, DI)`,
draws a second random value masked with 0x3fe for SINCOS.DAT lookup, and
adds signed products shifted by 10 to the target coordinates. The caller's DI
is `(128 - adjusted unit accuracy) / 2`; exact RNG and aim branch parity are
not established in the engine. For a tracked unit, `0x75cc1..0x75d3a`
reads the target unit's top-left into CX/DX, adds eight to CX when target
class +0x79 is 2, applies `0x64250`, and writes the resulting point into
attacker +0x42/+0x44 with +0x46 = -1. The grenade action adds (8,8) to
that point, so its pre-scatter destination is target top-left +(16,8) for
class-2 guns and +(8,8) otherwise. If +0x46 names an indexed target with
high byte 0x55, the throw first refreshes those coordinates through the
same helper; other nonnegative high bytes reject the throw. The direct
coordinate branch instead uses +0x52/+0x54 when the low word of +0x50 is
0x0d80. `0x7ce72..0x7ce86` and `0x7d089..0x7d09d` copy command-record
words +0x2c/+0x2e/+0x30 into unit +0x50/+0x52/+0x54, and `0x7ce98..0x7cebe`
distinguishes the sentinel to resolve the latter as map coordinates. The
exact command producers and complete automatic targeting sequence remain
unresolved. Open-rts uses these unit aim points, the eight signed muzzle
pairs, the native tracked-unit distance gate and scatter formula, and the
SINCOS.DAT words preprocessed as at `0x70c24..0x70c3b`. It draws from a
saved/hashed combat seed using the native `0x70d37` generator. This stream is
separate from retail's global RNG and is sampled at throw time; retail caches
tracked aim and refreshes it on its own schedule, and its +0x93 flag doubles
the tracked scatter radius at `0x75d10..0x75d19`, which is not yet modeled.
`0x7dc90..0x7dceb` updates +0x93 one unit toward the terrain-dependent value
at `0x11742c`, sampled at unit origin +(5,11), unless global 0x12ec7e is
nonzero. Nonzero values are used to offset and clip the unit drawing at
`0x7ddbf..0x7de06`; at values at least 16 `0x7e0f8` sometimes creates an
effect. This routine is reached from `0x7d2e3` in the unit drawing path;
`0x2666f` also calls that drawing routine for the hardware preview but sets
0x12ec7e to 1 around the call (`0x26668..0x26674`), so preview does not
mutate depth. For terrain IDs 0..66, table `0x11742c` contains nonzero
targets only at IDs 9 → 7, 38 → 4, 39 → 11 and 42 → 16. Therefore the
timing, simulation/render coupling and any other consumers need tracing
before a lock-step depth ticker or a direct terrain test can replace it.
The engine always applies the 25% accuracy bonus; retail skips it when unit
+0x67 bit 0x80 is set (`0x75cf4..0x75d04`), a flag not yet modeled.
`0x77237..0x7723b` sets this bit with unit +0x62 bit 3 during an enemy-unit
engagement path; `0x77031..0x7707f` clears both after a failed range/target
check. This ties the flag to a tracked engagement, but the full entry and
exit lifecycle has not been mapped to engine orders, so treating every
grenade attack as engaged would be an unverified substitution.
Direct-coordinate mode invokes separate scatter at `0x636c7` with radius
`(128 - adjusted accuracy) / 2` and no final (8,8) offset. Open-rts applies
that formula to non-unit targets using their existing target position as a
stand-in for native command coordinates; it does not retain the original
clicked point. Mapping non-unit targets to direct mode and unit targets to
tracked mode is an inference: retail dispatches on the +0x50 sentinel,
whose command producers are not yet fully traced. `test_combat` checks all eight launch facings, both tank and
gun aim anchors and the wider direct-coordinate scatter. `0x5a205` initializes the common
Bresenham record via `0x5a39e`, then `0x5a2b1..0x5a314` derives an arc-table
index from squared coordinate differences shifted by seven, capped at 1023,
and the runtime lookup at `0x104028`; its five table pointers at `0x148b9c`
are 0x148bc0, 0x148bc2, 0x148bc6, 0x148bcc and 0x148bd4. The first byte
of the chosen table divides the major-axis distance and the rest supply arc
samples. Correction: `0x104028` is the allocated address of WARDATA.PAC's
SQROOT.BIN; `0x6fb6c` assigns it and `0x70bf5..0x70c06` loads that asset.
The level already loads the same native table, so its contents are available.
The ESI value from the robot's +0 byte survives the call into `0x5a39e` and
is popped into EDX at `0x5a3d5`; DL is stored as both projectile speed bytes
at `0x5a3fc..0x5a403`. Thus grenade speed is max(1, robot byte +0 / 8).
`0x510b7..0x510fd` resolves the manufactured member's 44-byte robot record
and copies record +0x1c (strength) to unit +0 and +1; unit allocation at
`0x7a66b..0x7a670` initially sets +0 to 40 before record setup. The shipped
first robot records for Grunt, Tough, Sniper, Psycho, Pyro and Laser have
strength 25, 100, 40, 30, 50 and 50, giving grenade speeds of 3, 12, 5, 3,
6 and 6 pixels per native update. Open-rts authors those speed values and runs
native updates within its 30 Hz simulation. A scan of the 48 group records and their 21 member slots finds only
robot identities 2, 3, 4, 6, 17 and 18 for those weapon types in the shipped roster;
custom robot identities and runtime strength changes remain unmodeled. Recheck
the shipped group records with `od -An -v -tu1 data/Z/ROBOTS.DAT | awk
'{for(i=1;i<=NF;i++) a[n++]=$i} END {for(g=0;g<48;g++) for(j=0;j<21;j++)
{p=18000+g*82+28+2*j; r=a[p]+256*a[p+1]; if(r<400 &&
(a[r*44+34]==2 || a[r*44+34]==3 || a[r*44+34]==4 || a[r*44+34]==7 ||
a[r*44+34]==17 || a[r*44+34]==18))
c[a[r*44+34] ":" a[r*44+28] ":" r]++} for(k in c) print k,c[k]}'`.
`0x594a8..0x59518`
advances that sample based on traveled distance,
adds launch-terrain height +1, and can terminate flight when a terrain height
exceeds the resulting threshold. The sample pointer advances only once per
native update when the signed segment counter is negative, resetting that
counter to its initial quotient; `0x5a4c8` subtracts actual major-axis pixels
from it. The five tables contain respectively {16}, {16,12,16},
{16,12,8,12,16}, {16,12,8,4,8,12,16}, and {16,12,8,4,0,4,8,12,16};
their leading bytes are the sample counts. Before the terrain test,
`0x59440..0x59484` also terminates the projectile once squared distance
from its stored launch point reaches the square of projectile +0x29 times
eight; `0x5a419` initializes +0x29 from robot +0x71, which `0x50df7..0x50dff`
initializes from weapon record u16 +2 shifted right three. The existing
authored attack range represents this same value. `0x5a4c8` steps the major axis by projectile
speed. Open-rts now uses an ordinary grenade mobj with native 70 Hz integer
major-axis stepping, the five sampled arcs, range and terrain checks in this
order; its saved/hashed flight state requires Z save layout 16, and the
combat RNG added in layout 17 is also saved/hashed. Target refresh,
direct-coordinate selection and strength mutation remain approximate, as do launch-update
phase and other projectile/unit interactions. Grenade sprite phase is per
projectile; native `0x59554..0x59559` instead uses global animation counter
`0x103d07`. `test_combat` checks all six speeds, actual flight, sampled arc,
range and terrain boundaries, saved flight and invalid saved arc. Inspect with `r2 -q -e bin.cache=true -c 'pd 55
@ 0x63650' -c 'pd 55 @ 0x5a2b1' -c 'pd 45 @ 0x594a8' -c 'pd 30 @ 0x5a4c8'
-c 'pd 15 @ 0x6fb62' -c 'pd 10 @ 0x70bf5' -c 'pd 35 @ 0x5108c'
-c 'pd 15 @ 0x7a660' -c 'pd 60 @ 0x64250' -c 'px 32 @ 0x148b9c' -c q data/Z/ZED.EXE`.

**Impact effect-object path, partial:** `0x8476c` also calls `0x8523f` at
the same impact-minus-(12,24) coordinates. Unless global +0x14c05a blocks
it, `0x85257..0x852a9` probes four points in order: offsets (4,16),
(20,16), (4,32), (20,32) from that argument, or impact-relative
(-8,-8), (8,-8), (-8,8), (8,8). Each uses `0x28646`/`0x2853d` to find
the first matching record in the 200-slot 40-byte effect table, subject to
effect +0x0d flags and descriptor-dependent inclusive bounds. At the first
match `0x85347` subtracts damage from effect byte +0x0f and branches on
zero/sign into destruction handling. `0x4a3d2..0x4a414` routes native object
IDs 8, 24, 26, 29, 31, 33, 35 and 37 (empty vehicles in `objects.inc`) to
`0x4a4cb`, which sets effect +0x0d bits 0x30 and copies word `0x103f2d`
into byte +0x0f; its static executable value is 8. IDs 22 and 23 (empty
Gatling and Howitzer) instead use `0x4a472`, copying word `0x103f21` (static
value 5). `0x2853d` accepts their bit 0x10/0x40 for impact probes. Other
creation paths can initialize +0x0f from weapon data (`0x4a28a`) or the
default 50 (`0x4a41a`); this prevents treating all matched effects alike.
On destruction, `0x853ee` branches on descriptor flags; an eligible flag-4
object modifies the terrain byte at `0x85431..0x85447`, while other selected
descriptor IDs enter the four-cell handler at `0x8551a`; it finally removes
the effect at `0x85558`. The runtime writers of the global values, ownership
and exact empty-to-occupied health transfer, and the complete destruction
consequences remain unverified. In particular, the engine represents empty
vehicles and guns as ordinary neutral unit mobjs rather than this effect
record, so the field cannot be directly substituted for body HP. This is not evidence
that all scenery mobjs can receive ordinary unit damage. `0x8476c` also calls
the separate `0x84f9d` path and a tile-based lookup at `0x27e0f`. Reproduce
with `r2 -q -e bin.cache=true -c 'pd 35 @ 0x84822' -c 'pd 55 @ 0x8523f'
-c 'pd 50 @ 0x2853d' -c q data/Z/ZED.EXE`.

**Empty-object transfer and grenade-box destruction:** when a robot enters
an empty object, `0x63a1d..0x63a9d` allocates its body record and sets
current body HP +8 to `25 * effect[+0x0f]`, and maximum body HP +0x0a to
`25 * effect[+0x27]`. The `0x4a184` creation path calls `0x4a28a`, which
copies weapon +5 to both effect +0x0f and +0x27; damage to the empty object
therefore survives boarding in 25-HP increments while maximum uses the
original value. The separate `0x4a30e`/`0x4a4cb` default-object path copies
global `0x103f2d` to +0x0f but no +0x27 initialization was observed there;
the interaction with prior slot contents remains unknown. Likewise, the
globals' runtime writers were not found in the linear disassembly, which
does not exclude indirect writes. On effect destruction, `0x8563a` checks
base sprite 350/351 (the two grenade boxes), writes terrain value 0x16 at
their location and calls `0x8476c` with damage 127 under flags that suppress
recursive effect damage. This proves a box-explosion path, but its damage
allocation, tile effects, and interaction with pickup/removal still need
an end-to-end trace before representing it as an ordinary missile blast.
Inspect with `r2 -q -e bin.cache=true -c 'pd 50 @ 0x63a1d' -c 'pd 30 @
0x4a184' -c 'pd 35 @ 0x4a28a' -c 'pd 38 @ 0x4a4cb' -c 'pd 48 @ 0x8563a'
-c q data/Z/ZED.EXE`.

**Driver-loss conversion:** when a bullet kills an exposed driver,
`0x5ae13..0x5ae1f` sets `0x148be0` while calling `0x860fd`. The latter
reaches the unit-removal path `0x61560`, whose `0x617f1..0x6182b` branch
allows a surviving body to become an empty object. At `0x61bb9..0x61be3`
the new effect gets `floor(max body HP / 25)` at +0x27 and
`max(1, floor(current body HP / 25))` at +0x0f. Boarding converts those
bytes back to body units through `0x63a85`; a remainder below 25 is lost,
and a surviving body below 25 becomes 25. Open-rts applies that quantization
on fatal driver loss while retaining the same neutral mobj; its separate
Zod-derived voluntary ejection path is not claimed to be this retail path.
`test_combat` checks 499/500 becomes 475/500. Inspect with `r2 -q -e
bin.cache=true -c 'pd 24 @ 0x5adfb' -c 'pd 32 @ 0x617ed' -c 'pd 55 @
0x61b4e' -c 'pd 45 @ 0x63a1d' -c q data/Z/ZED.EXE`.

**Grenade-box blast geometry:** `0x8567e..0x85699` calls `0x8476c` at
box origin minus (16,16). Its building lookup consequently tests the box
origin (assuming `0x14c05f` clear). `0x84c58..0x84c65` sees the box's
`0x14c05b` flag and stores team and source as `0xff`; the later frame-3
unit pass at `0x85ce4` therefore filters no team and tests inclusive unit
anchors in box-origin-relative bounds x -22..+22, y -6..+26. The immediate
`0x8523f` effect lookup is disabled by `0x14c05a`, so this nested call does
not itself trigger another effect object; the delayed unit pass is separate.
Both flags are cleared after the nested call. Its explosion record is
allocated by the common path, so allocation exhaustion and exact pass timing
also matter. The impact probes' eligible effect instances and initial health
still need mapping to each map-loaded grenade box before implementing the
trigger. Inspect with `r2 -q -e bin.cache=true -c 'pd 48 @ 0x8563a' -c
'pd 24 @ 0x84c38' -c 'pd 48 @ 0x85ce4' -c 'pd 25 @ 0x8523f' -c q
data/Z/ZED.EXE`.

**Map-loaded box trigger:** the OBJECT loader `0x18427..0x18443` passes
the ten-byte map record's type +4 and weapon index +5 into `0x4a184`.
That calls `0x4a28a`: index `0xff` initializes effect +0x0f to zero;
otherwise it copies the selected 42-byte ROBOTS.DAT weapon record +5.
Grenade box descriptors 9/10 have flag 0x31, and `0x4a1e4..0x4a1ed` sets
effect +0x0d bit 0x40 for descriptor bit 0x10, making them eligible for the
ordinary `0x2853d` shell probe. For example `OBJECT01.DAT` has both `0xff`
and 13 in box record +5; shipped weapon 13 has +5 = 2. At `0x85347` a
positive shell hit therefore destroys a zero-health box immediately, while
a 2-health box survives one point and explodes at zero or below. Boxes are
removed by the outer effect destruction after the nested blast is scheduled;
the separate collection path does not invoke this destruction handler.
Open-rts retains the per-crate effect health and OBJECT slot, using authored
C values for weapon +5. A shell probes crates in native point order, applies
damage to the first matching crate, and on destruction creates an ordinary
saved mobj for the delayed teamless unit blast. It applies the nested building
hit and saves the terrain change at the box origin; pickup removes a box
without exploding it. The engine approximates frame-3 delay with three 30 Hz
tics, and does not reproduce native explosion-pool exhaustion. Map-loaded
unoccupied vehicles and guns also participate in that lookup in OBJECT order,
using the native 24-pixel inclusive extent for descriptor flag 0x32, raw
effect health and 25:1 conversion to body health on boarding. Destroying one
removes its empty mobj without triggering the box explosion. `test_combat`
checks an empty Jeep and an unboardable Truck preempting a box, damage and boarding, save/load,
destruction and a following shot reaching the box. Native object-destruction
visual effects, complete native effect-slot occupancy, and dynamic eligible
effects remain unmodeled, so a
crate can still be hit when retail would select another effect. Shell interception, remaining terrain
effects and complete native ordering are likewise unmodeled. `test_combat`
checks damage, non-chaining, delayed all-team damage and save/load; `test_capture`
checks pickup without an explosion.

**Map-loaded wreck preemption:** descriptor flag 0x10 causes the loader
`0x4a1e4..0x4a1ed` to set runtime effect +0x0d bit 0x40, so `0x2853d`
selects the five map-object wreck types 59 and 63..66 in addition to crates
and empty vehicles. Their descriptor low bits yield an inclusive 24-pixel
square, and the loader's weapon-index byte supplies effect health through
`0x4a28a` even for these ordinary object types. If the selected wreck has
descriptor flag 4, `0x853ee..0x85447` replaces the terrain attribute at its
origin, adding 16 to y when flag 2 is set, with `(old & 0x80) | 0x16`;
otherwise these five wrecks remove without that terrain write. The compare
with 7 follows masking to bit 0x80, so it does not suppress this write.
Open-rts now includes represented eligible wrecks in the same first-slot
probe, initializes their saved health from the authored weapon values,
removes them on a fatal hit, and saves/rebuilds the terrain attribute. Native
debris animation remains unported. `test_detour` checks map-loaded weapon
and 0xff health; `test_combat` covers all five wrecks, a noneligible earlier
object, 24/25-pixel bounds, preemption, subsequent crate hit and save/load
of health and terrain. Reproduce with `r2 -q -e bin.cache=true -c
'pd 42 @ 0x4a1df' -c 'pd 35 @ 0x4a28a' -c 'pd 48 @ 0x2853d'
-c 'pd 52 @ 0x853ee' -c q data/Z/ZED.EXE`.

**Driver-loss allocation constraint:** `0x61b4e` calls `0x6140f` before
transferring the body health into a new empty effect. Except for effect IDs
5 and 0x15..0x17, `0x6140f` first calls `0x6147d`: it rounds the position
using `(coordinate + 7) & 0xfff0`, then checks four terrain bytes at
offsets 0, 1, 0x80 and 0x81 through the table at `0x11ceae`. Any nonzero
table entry rejects placement. `0x4a5d7` then scans 200 40-byte effect
records in order for a negative first word, failing if none is free. Both
failures reach `0x61c7f`, which frees the original unit record. Thus giving
a driverless unit an arbitrary free slot among only the effects currently
modeled would miss terrain rejection and competition with other effects.
`0x5fcdd` maps the rounded position to the 128-wide native attribute plane;
after `0x6147d` adds 0x4000 this is the terrain plane represented by
`zmap_t.terrain`. For the shipped terrain IDs 0..66, the table permits only
0, 7, 8, 10, 22, 23 and 59. The four gun IDs skip this terrain check;
the exposed-driver Jeep does not. The Jeep unit type 0x22 selects signed
offset (0,-7) at `0x14b01c`; the subsequent subtraction of four from x
places the object at unit origin +(-4,-7). `0x6147d` rounds both coordinates
to `(coordinate + 7) & 0xfff0` and returns those rounded coordinates to
`0x61440..0x61443`, which stores the empty effect there. Open-rts converts
its unit center back to native origin and applies this placement and terrain
test on Jeep driver loss. On gun driver loss open-rts uses the first
free represented effect slot, transfers quantized health and changes to the
empty sprite, allowing shell damage and subsequent boarding; if all represented
slots are occupied it removes the gun. Unrepresented native effects can occupy
slots, so allocation and collision ordering are approximations. The engine
converts the unit center to its native origin before applying the offsets at
`0x14af94` and the common x−4 at `0x617ed`. The resulting gun effect offsets
are Light Gun (−4,0), Missile (0,0), Gatling (−4,−1), and Howitzer (−1,−6).
Unlike the Jeep, these gun IDs bypass `0x6147d` and retain unrounded origins.
`test_combat` checks all four origins, save/load, a shell hit,
boarding, represented pool exhaustion, and Jeep placement on permitted and
blocked terrain. Reproduce the table with `r2 -q -e bin.cache=true -c
'pxj 67 @ 0x11ceae' -c q data/Z/ZED.EXE`; reproduce the control flow with
`r2 -q -e bin.cache=true -c 'pd 30 @ 0x6140f' -c 'pd 35 @ 0x6147d' -c
'pd 15 @ 0x4a5d7' -c 'pd 10 @ 0x61c7f' -c 'px 96 @ 0x14afa8' -c q data/Z/ZED.EXE`.
Inspect with `r2 -q -e bin.cache=true -c 'pd 25 @ 0x18418' -c 'pd 35 @
0x4a184' -c 'pd 35 @ 0x4a28a' -c 'pd 15 @ 0x85338' -c q data/Z/ZED.EXE`,
and `xxd -g1 -c10 data/Z/LEVEL01/OBJECT01.DAT`.

Reproduce the trace with `r2 -q -e bin.cache=true -c 'pd 72 @ 0x592ff' -c
'pd 20 @ 0x84c38' -c 'pd 25 @ 0x85ce4' -c 'pd 30 @ 0x64384' -c q
data/Z/ZED.EXE`, and run `make build/bin/tests/z/test_combat
build/bin/tests/z/test_cliffs build/bin/tests/z/test_commander` followed by
each binary with `SDL_VIDEODRIVER=dummy`.

### Shell flight (2026-11-12)

**Fire Truck water:** unit type 0x26 dispatches `0x59ffb` at `0x58c60`.
Its ROBOTS.DAT weapon 14 has damage 20 and shot-speed byte 8; the created
projectile sets +0x21 to 0x42 and +0x17 to 16. `0x59498` selects
`0x59b63`, whose +0x17 increment and common `0x5a4c8` step produce
successive major-axis speeds 1 through 8 pixels per 70 Hz update; its
drawn pictures are SPRITES.RSC 1622..1625. The unit-target branch
`0x76849`/`0x76aa3` aims at target unit top-left +(12,8). At arrival,
`0x59363..0x59385` halves projectile damage and calls `0x8476c` with the
impact coordinates unchanged. Thus the common explosion's unit bounds are
x−6..+38, y+10..+42 relative to water impact, and its building point is
impact +(16,16), assuming `0x14c05f` clear. This differs from the shell
branch's (−12,−24) adjustment. In particular, these bounds do not by
themselves guarantee that the chosen target anchor is hit.

Open-rts launches an ordinary saved water mobj through normal attacks. Its
state action runs the native integer major-axis movement and acceleration
in 70 Hz substeps, cycles the four projectile pictures, checks the native
terrain-height table against the launch tile's height plus one, and stops
at the first higher in-flight sample. The arrival check precedes that sample
as in `0x59349`; equal height passes. The water effect uses the shared shell
explosion handling with the corresponding offset. Its launch point now uses
the native Fire Truck hull and nozzle offsets: `0x590a1..0x590bc` adds the
signed byte pair at `0x14b474` indexed by unit facing to unit top-left;
`0x59197..0x591a7` adds the signed word pair at `0x148966` indexed by body
+0x15. `0x58b24..0x58b92` computes the nozzle's desired bearing from unit
top-left +(14,12), rotates body +0x15 one direction on alternate updates,
and fires only after it reaches that bearing. `0x5a648` uses integer half-axis
tests with asymmetric equality. Open-rts uses the native offset tables and
bearing at shot time; it does not separately animate or time the nozzle turn.
The common explosion applies object and building damage immediately at
`0x8476c`; `0x85b2f` advances its animation on alternating native updates and
calls the unit-damage pass `0x85ce4` at frame 3. The explosion loop at
`0x14ec2` precedes projectile updates at `0x154a0`, so a new explosion first
advances on a subsequent update. Open-rts applies the unit pass three 30 Hz
tics after impact, an approximation of the native five or six 70 Hz updates
depending on phase. The saved water mobj carries this pending action; Z save
layout 13 distinguishes the changed impact lifecycle. Non-unit aim, complete
terrain/effect interactions, native effect-pool exhaustion, and exact update
phase remain approximations. Shells use the same immediate object/building
and delayed unit passes; Z save layout 14 distinguishes their changed impact
lifecycle. When a shell destroys a grenade box, the engine's thinker order
may differ from native explosion-slot allocation order, affecting attribution
when both blasts kill the same unit. The existing direct non-unit target
fallback (such as bridges) remains immediate; its native terrain path has
not been traced. `test_combat` covers normal attack dispatch, real accelerating substeps,
sprite/height boundaries, interception, terminal-tile order, save/load,
all eight hull/nozzle headings, bearing ties, immediate building damage,
delayed unit damage across save/load and an inclusive damage boundary with
ally exclusion.
Inspect with `r2 -q -e bin.cache=true -c 'pd 40 @ 0x58c3a' -c 'pd 55 @
0x59ffb' -c 'pd 30 @ 0x59b87' -c 'pd 35 @ 0x5934f' -c 'pd 20 @
0x76849' -c 'pd 10 @ 0x76aa3' -c 'pd 40 @ 0x58b24' -c 'pd 20 @
0x5909b' -c 'pd 20 @ 0x5915f' -c 'pd 75 @ 0x5a648' -c 'px 16 @
0x14b474' -c 'pxw 8 @ 0x148966' -c q data/Z/ZED.EXE`, and inspect weapon 14
with `dd if=data/Z/ROBOTS.DAT bs=1 skip=22588 count=42 2>/dev/null | od -An -tu1`.

**Confirmed** on the same executable: the shot-speed byte from weapon
record +0x23 is copied into the projectile at `0x5a3fc..0x5a403`.
`0x5a4c8..0x5a523` and `0x5a528..0x5a57f` step a Bresenham major axis
by up to projectile +0x16 pixels per native update; for the shipped shell
weapons that byte is 4, 5, 8, 10 or 12. Correction: ordinary shell creation
`0x59fa2..0x59faa` sets flags 3 and accumulator +0x17 to 16. `0x5948a`
therefore selects `0x59a35`, which advances the accumulator by 16 after
each step, giving major-axis speeds 1, 2, 3 and so on up to +0x16. This
ordinary branch draws SPRITES.RSC 1622..1625 at `0x59b04`, not 1634: the
earlier 1634 conclusion described the different arc branch at `0x59538`.
It checks terrain height at `0x59a87..0x59ac3` and the shared range gate
at `0x59440..0x59484`; destination arrival precedes both. Open-rts now
uses an ordinary saved mobj and its state action for 70 Hz integer stepping,
acceleration, range and terrain stops; Z save layout 18 stores this flight
state. The explosion animation remains the earlier stand-in.
**Dispatch correction:** the preceding conclusion applies to the
`0x59ed1` initializer, not every shell-bearing unit. Vehicle firing dispatch
`0x58bcc..0x58c71` sends native type 0x18 (Mobile Missile) to the special
multi-shot `0x58e2f`/`0x59ed1` path, while ordinary tank types 0x06, 0x19
and 0x1e use `0x5a205` at `0x58c71`; the direct firing path `0x580da..0x580ee`
does the same. `0x5a205` stores the weapon speed in +0x16 and +0x17 through
`0x5a39e`, leaves flags 4 at `0x5a280..0x5a288`, and uses the same sampled
arc/terrain path as grenades. Since +0x17 is less than 16 for the shipped
tank speeds, `0x5a4d8..0x5a4e5` selects +0x16 directly on every update:
those tank shells move at constant native speed. With flag 8 clear they draw
SPRITES.RSC 1634 at `0x59538`, not the 1622..1625 sequence used by the
accelerating `0x59ed1` branch. Open-rts now selects the saved arc state for
Light, Medium and Heavy Tank shells; Tough/APC shells retain the confirmed
`0x59ed1` path. Fixed-gun dispatch `0x783d7..0x78539` sends type 0x05
(Light Gun) and 0x17 (Howitzer) to `0x5a205` as well; type 0x16 (Gatling)
uses bullets, while type 0x15 (Missile Gun) takes two `0x59ed1` calls at
`0x7843d` and `0x78499`. Those two missiles use facing-indexed origin tables
`0x14ac10`/`0x14ac50` and target-offset tables `0x14ac90`/`0x14acb0`.
Their selector 0x2a produces flags 0x15 at `0x59f54` and fixed RSC sprite
5345 at `0x59af0`. Mobile Missile type 0x18 takes three `0x59ed1` calls at
`0x58ebb`, `0x58f7b` and `0x5903b`: `0x59053` forms the first origin from
body-facing `0x1489c8`, turret-facing `0x148a48` and y−8; the next two add
the turret-facing offsets at `0x148b08`/`0x148b28` to both the first origin
and target. Selector 0x29 produces flags 0x25 and fixed RSC sprite 5425 at
`0x59afd`. Open-rts now emits the two/three independently damaging saved
mobjs with those tables, using the engine's current turret facing and target
anchor; target acquisition, smoke and exact retail turret timing remain
approximations. Z save layout 19 stores their sprite selection. `test_combat`
checks all tank and fixed-gun arc speeds, salvo counts, all eight fixed-gun
and 64 body/turret direction combinations, flight save/load and independent
impacts. The former statement
that all shells accelerate was an overgeneralization and is superseded here.
The range gate compares 32-bit squared displacement from launch against
`(+0x29 * 8)^2` by high word then low word, and detonates on equality or
greater. `0x5a419` copies +0x29 from unit +0x71; `0x50df7..0x50dff` derives
that byte from weapon u16 range shifted right three. `test_combat` exercises
both an exact range boundary and a distant target beyond it.

Missiles retain their firing team and snapshotted damage if the shooter is
removed. The existing thinker and save lifecycle preserve their originator,
target and aim point; removing either mobj clears its borrowed pointer.
Unit movement can evade a projectile because its impact point does not
follow the target. Tank targeting `0x76849` reads the target unit origin and
`0x76aa3` adds (12,8); `0x58c13..0x58c40` then calls `0x64250` with the
actual muzzle and radius `(128 - adjusted accuracy) / 4` (Fire Truck skips
this call). Open-rts now applies that target anchor and native distance,
SINCOS and RNG math for tank attacks on units, using the saved combat RNG.
The separate retail global RNG stream, +0x67 accuracy flag and cached target
lifecycle remain approximations; structure targeting is unchanged.
`test_combat` checks the no-scatter distance-six boundary, native trig result,
two RNG draws and save/load reproduction. Some other weapon scatter and muzzle positions
remain approximations. Tank muzzle placement is now traced:
`0x590c9..0x59111` temporarily puts body +0x15 (turret facing) in unit
+0x39, calls `0x64227` to add the signed word pair at `0x1492c8` to unit
origin, then adds the signed byte pair at `0x14b283` and (6,-9).
`0x5915f..0x591c9` adds the turret-facing word pair from `0x148946` for
Medium Tank type 0x06, `0x1489a6` for Heavy Tank type 0x19, or `0x148986`
for Light Tank type 0x1e. Open-rts uses these native tables and its current
absolute turret facing; retail turret acquisition and timing remain separate
approximations. `test_combat` checks all eight turret facings for all three
tanks, including a hull direction different from turret direction. Reproduce
with `r2 -q -e bin.cache=true -c 'pd 50 @ 0x590c9' -c 'pd 35 @ 0x5915f'
-c 'pd 12 @ 0x64227' -c 'px 32 @ 0x1492c8' -c 'px 16 @ 0x14b283'
-c 'px 32 @ 0x148946' -c 'px 32 @ 0x1489a6' -c 'px 32 @ 0x148986'
-c q data/Z/ZED.EXE`.
The fixed-gun firing paths call `0x64227` at `0x7822d` and `0x78302`
before dispatching Light Gun and Howitzer through `0x783d7`, so those two
use the same `0x1492c8` pivot indexed by unit facing. The Tough action
`0x60c9d..0x60cc6` instead adds its facing-indexed signed word pair from
`0x14aa54` directly to robot origin before launching via `0x59ed1`. Open-rts
now uses both offsets; `test_combat` checks all eight directions for Light
Gun, Howitzer and Tough. Reproduce with `r2 -q -e bin.cache=true -c 'pd 45
@ 0x7820b' -c 'pd 45 @ 0x782f2' -c 'pd 25 @ 0x60c9d' -c 'px 32 @
0x1492c8' -c 'px 32 @ 0x14aa54' -c q data/Z/ZED.EXE`.
For automatically targeted units, `0x78302..0x78351` uses that fixed-gun
pivot as the source, the target origin +(0,12) as the initial aim, calls
`0x7628f` for a possible moving-target lead, then calls `0x64250` with
radius `(128 - adjusted accuracy) / 4`. `0x7837f` dispatches the resulting
single aim to `0x783d7`, which applies its two port offsets if the weapon is
a Missile Gun. Open-rts now applies the anchor and native scatter once per
unit-target salvo using its saved combat RNG; both rockets share the result.
It does not yet model the moving-target lead, flag-dependent accuracy,
exact retail random stream or the explicit-coordinate branch at `0x78201`,
which takes its target from the gun body and does not run this scatter call.
`test_combat` verifies shared two-rocket scatter, native trig and save/load
reproduction. Reproduce with `r2 -q -e bin.cache=true -c 'pd 55 @ 0x782f2'
-c 'pd 40 @ 0x7628f' -c 'pd 45 @ 0x78201' -c q data/Z/ZED.EXE`.

**Further explicit-aim trace:** the command dispatch at `0x7c027..0x7c037`
indexes the table at `0x14b088` by the absolute value of unit +0x5a; entry 2
points to `0x7cc9d`. Player dispatch `0x47aec..0x47af3` writes that byte
from AL. At `0x7480f..0x74844`, the class-2 branch sets command 2 and
copies AX/BX into its first two argument words before calling the common
dispatcher; `0x74862..0x74896` similarly issues command 2 after its target
check. The surrounding UI selection and all coordinate/target validity
gates are not yet mapped to the engine's input actions. This path is reached
from `0x7471b` after `0x7a7ff` fails to resolve a unit under the pointer. For
class 2, the `0x74752..0x747af` branch first applies `0x591ca`, then checks
`0x193f0` and `0x4a03b`; a different outcome selects command 4 instead of
the command-2 route. A resolved unit takes the separate `0x74980` branch,
which calls `0x74a3a` to encode group or unit sentinels before sending 2.
These tests prevent treating every empty map click as a valid attack order.
`0x7cc9d..0x7cd5b` copies unit words +0x50/+0x52 into the gun body at
+0x0e/+0x10 before entering action 4.
Action 4's `0x78540..0x786de` separately checks those coordinates with
`0x764e8`, rotates the gun and, after further line-of-sight and reload
checks, calls `0x64250` for accuracy scatter at `0x786b4` before the same
shell dispatch. Thus the no-scatter call at `0x78201` does not prove that
every explicit-coordinate attack omits scatter. `0x764e8` also recognizes
the sentinel words 0xfb2e and 0xef1f as special group and unit references;
their handlers can replace the coordinates. Open-rts attack commands require
a target mobj (`game/g_cmd.c`), and a target's category alone does not
establish which native action or command supplied its aim. Do not equate all
non-unit targets with this action-4 coordinate path without tracing command
producers and target resolution. Inspect with `r2 -q -e bin.cache=true -c
'pd 70 @ 0x747e1' -c 'pd 50 @ 0x7cc9d' -c 'pd 100 @ 0x78540' -c 'pd 35 @ 0x764e8' -c
'pxw 12 @ 0x14b088' -e bin.relocs.apply=true -c q
data/Z/ZED.EXE`.

**Moving-target lead refinement:** for the fixed-gun caller above, the
ordinary branch of `0x7628f..0x763ec` admits target actions 2 and 7,
action 8 outside vehicle class 1, or a class-1 vehicle whose body +0x17
bit 1 is set. The call to `0x640ff` gates extrapolation using a random
accuracy test; its threshold conditionally includes a 25% bonus when the
shooter +0x67 bit 0x80 is clear. It quantizes the source/target delta by
arithmetic shift 4, indexes the square-root table with the clamped sum of
squares, multiplies the table byte by 16, divides by shooter byte +0x76
(default 8 if zero), and masks the quotient with 0x3f. Starting from the
target's movement-pattern pointer +0x20, it consumes that many pattern
entries and adds their word values along the target-facing directions in
the eight-entry jump table at `0x14a788`. The loop executes at least one
pattern entry even when the masked count is zero. Target action, movement
pattern position, and the shooter flag are not presently equivalent to the
engine's velocity and route state; applying a simple velocity-times-distance
lead would not reproduce this branch. Reproduce with `r2 -q -e
bin.cache=true -c 'pd 90 @ 0x7628f' -c 'pd 30 @ 0x640ff' -c 'pxw 32 @
0x14a788' -c q data/Z/ZED.EXE`.
A zero period for timed missiles
now means one simulation step per tic, including flights longer than 99
tics; other missile periods keep their existing millisecond clock.
A failed shell allocation consumes the attack
instead of silently falling back to an immediate hitscan shot.
`tests/z/test_combat.c` checks cardinal and diagonal acceleration, native
sprite, terrain and range boundaries, moving and removed targets, shooter
removal and in-flight save/load; `test_cliffs` and `test_commander` cover
impact on the two terrain targets. Reproduce with `r2 -q -e bin.cache=true
-c 'pd 35 @ 0x59fa2' -c 'pd 60 @ 0x59440' -c 'pd 65 @ 0x59a35'
-c 'pd 30 @ 0x5a4c8' -c q data/Z/ZED.EXE`.

The native projectile's nearby-robot pass at `0x59707` is separate from
the remake's `DodgeMissile` waypoint: `0x59736..0x59740` accepts only
actions 0 or 1 and `0x59766` checks squared distance at most 64 before
calling `0x63465`. The intervening `0x5976d` comparison against action
`0x2b` cannot succeed after that action filter; the apparent `+0x7d = 2`
branch is therefore **disproven as reachable through this path**. The
called action has now been traced as described above; it has not been ported.
`0x63465` first calls `0x62db2`, which checks terrain at unit top-left
`(+5,+11)` and rejects terrain 0x09, 0x26, 0x27 and 0x2a. It then calls
`0x61fac`, which can reject based on group state and a `0x78f61` search,
before setting action 0x2b. These gates do not establish equivalence to
Zod's idle missile-dodge waypoint.

Further trace of this reaction in `ZED.EXE` (SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`):
the projectile update calls the proximity pass at `0x59318` before the
projectile-type movement dispatch at `0x59342`. On entry, `0x63473..0x63491`
sets action 0x2b, frame `(facing & 0x0e) * 3 / 2`, and timer 4. The action
table at `0x14b1b8` gives sprite base 2075; four facing groups each have
three consecutive frames. On updates where `0x10e91b` is zero,
`0x62cec..0x62d24` rechecks eligibility, advances twice, holds the last
frame for four updates, and exits on the next update. No position write
occurs in that action.

The eligibility gate is materially more than a nearby-distance test:
`0x61fac..0x62023` rejects absent or invalid squad identifiers, group
flag +9 bit 1, nonzero group +0x3d, conditional group +0x3a, and a
combination of group +0x10 and +0x12. Otherwise it calls `0x78f61`;
if that returns a candidate, twice its returned distance must exceed the
unit's +4 byte for the reaction to proceed. That search has its own unit
and group flag filters and selects directions through `0x104558` and
`0x104578`; it can also mutate the searcher's +0x6f and +0x74. The engine
does not yet represent this complete eligibility/search lifecycle, so a
simple all-idle-robots radius scan would change behavior and is not
implemented. Reproduce with `r2 -q -e bin.cache=true -c 'pd 95 @ 0x61fac'
-c 'pd 65 @ 0x78f61' -c 'pd 45 @ 0x62cec' -c 'pd 45 @ 0x63465'
-c 'px 16 @ 0x14b1b8' -c q data/Z/ZED.EXE`.

### Vehicle body degradation (2026-11-12)

**Confirmed** for `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
`0x86d55` compares health before and after a hit against `(max/2)/2`,
`max/2`, and `max/2 + (max/2)/2`. On a downward crossing it multiplies
the current body +7 speed and +0x20 fire-rate bytes by 80, 85 or 90,
then integer-divides by 100. One hit crossing multiple levels applies
only the lowest reached scale; separate crossings compound. `0x86ded`
retains the fire byte for unit types `0x22` and `0x16`, assigned to the
Jeep at `0x6378f..0x639a3` and Gatling at `0x50b8c..0x50b91`.
The vehicle firing path `0x58bb8` uses body +0x20, while paths including
`0x60c4c` use unit +0x75. The existing C unit table provides plain-ground
speed levels and reload durations; the shipped vehicle/gun speed bytes are
exactly ten times those levels, so the port compiles the initial bytes from
those literals and quantizes scaled speed through the existing native level
table. Unit HP before the hit is supplied by the common damage lifecycle.

The Z save-layout version is 3 and includes mutable speed, fire and accuracy
bytes and their
initialization flag in the lockstep checksum. A repaired body's retained
bytes and subsequent crossing compound as these instructions specify;
whether the original repair facility separately restores body bytes remains
**unknown**. On the direct completion path, `0x3fe59..0x3fe6d` clears
unit +0x67 bit 0x40 and copies body max HP +0x0a to HP +8 unless ownership
changed. The subsequent visible call `0x57ce9` writes the unit and group
orders/coordinates, rather than restoring body speed or fire bytes. This
narrows the search but does not exclude other repair callbacks. Inspect with
`r2 -q -e bin.cache=true -c 'pd 45 @ 0x3fe40' -c 'pd 15 @ 0x57ce9' -c q
data/Z/ZED.EXE`. The currently selected body-based firing path covers Light,
Medium, Heavy and Mobile Missile vehicles; other paths retain their existing
cooldown pending further tracing. `0x86df0..0x86e01` also scales unit +2
(accuracy) and unit +6. Accuracy now affects the driver-hit roll; unit +6's
meaning remains **unknown**, and the earlier interpretation of it as weapon
damage was incorrect. Focused
threshold, multi-threshold, Jeep, healing and save/load checks run with
`make build/bin/tests/z/test_combat` and `SDL_VIDEODRIVER=dummy
build/bin/tests/z/test_combat`.

**Accuracy configuration.** The 32 ROBOTS.DAT group records at `0x4650`
contain up to 21 little-endian robot indices at +0x1c; each 44-byte robot
record names its weapon at +0x22 and accuracy at +0x1d. All references in
the shipped active groups agree on one accuracy for each weapon, so the C
table in `g_game.c` preserves the previous record lookup for those groups.
`test_units` compares every referenced record with the C literals, without
using game data as runtime stat authority. On boarding, the new driver's
configured accuracy replaces that of an empty vehicle; its subsequent body
damage scales the mutable value. Dynamic experience or changes to robot
records beyond the shipped groups are not represented by the C configuration.

### Stored guns and territory capacity (2026-11-12)

**Confirmed** on `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
`0x4317f..0x4318f` shifts the building's four built-list bytes at
`+0x4b..+0x4e` and inserts the newly manufactured group at the front.
`0x26aad..0x26ad6` temporarily excludes the queried building's current
product, calls `0x26b82`, and rejects a count of four or more. That count
includes active same-team class-2 guns in the territory, another building's
gun in production, every building's four nonempty built-list slots, and
pending placement records. `0x9de4a` also uses the territory count while
choosing the CPU's production. This corrects the earlier notes that treated
the four-slot list and its territory limit as unimplemented.

The engine stores up to four product types on each producer, newest first;
the shared production record continues to own the active product. It
enforces the territory limit for player and CPU selection, preserves stock
through capture and destruction, and can place any stored type via the
production window. Placing does not free a territory slot because the gun
then counts as deployed. A queued gun which finishes after another producer
reserved the last slot waits until a slot becomes free; pending placement
records do not exist because placement commands here are atomic. The Zod
`ZBuilding::AddBuildingQueue`/`ResetProduction` behavior supports mixed
upcoming unit products; ordinary Start with no stored gun and a running
product still closes the window without enqueueing a duplicate. The retail
ordering of an arbitrary mixed queue and exact destroyed-factory stock
lifecycle remain **unknown**. Zod `ProcessKillObject` leaves the cannon
list in place; the implementation follows it for factory destruction. Zod
limits its upcoming queue to five (`zbuilding.h:8`); the engine retains its
existing Z queue capacity of nine total slots as a documented policy difference.

Zod `BuildingCreateUnit`/`CannonsInZone` also impose a four-cannon zone cap,
counting stored and placed cannons. Retail's count is not identical: its
deployed-unit pass filters by team and it includes in-progress production
and pending placements. The tests follow the retail selection rule.
`tests/z/test_production.c` exercises mixed stock, cross-producer reservation,
retry after capacity is freed, the cap with deployed guns, capture and
destruction/rebuild, invalid serialized counts, save/load and checksum;
`tests/z/test_hud.c` covers product selection while stock exists and placing
stock when no products are offered. Run both with `SDL_VIDEODRIVER=dummy`.

**Later CPU product selection:** `0x410bd`/`0x42775` choose the first
robot/tank product, while `0x41336`/`0x42a2c` choose again after production.
The corresponding selectors `0x9de4a`, `0x9de9b` and `0x9def5` use gun
class list `0x152ee2` after their first selection when the reserve-adjusted
territory value is at least 500, deployed guns number below two (four for a
fort), and the combined gun capacity `0x26ab4` is below four. Robot
factories and forts also require rating strictly greater than 1000; tank
factories have no such extra test. `0x42044`/`0x42e91` set the temporary
flag at `0x20a448` on the gun placement path, forcing the following
selection back to the normal unit list. Open-rts selects the CPU's next
product in its existing production callbacks and uses its existing
reserve and territory counts; its immediate stored-gun handoff approximates
retail placement timing, and generic producer-availability checks still
apply. Player factories retain their existing repeat behavior.
`test_production` covers the reserve threshold, rating boundary and a
fourth gun followed by mobile production. Inspect with `r2 -q -e
bin.cache=true -c 'pd 90 @ 0x9de4a' -c 'pd 35 @ 0x9def5' -c 'pd 30 @
0x26ab4' -c 'pd 40 @ 0x42044' -c 'pd 30 @ 0x42b64' -c q data/Z/ZED.EXE`.

### Typed player routes and factory rallies

**Reference-derived, not a claim of retail parity.** Pinned Zod
`5ccb43ab76ee08c9f624906fb465dc3ba76a7a35` defines waypoint action,
position and target reference in `zobject.h:82..86,162..170` and processes
move, attack, enter and crane/unit repair at `zobject.cpp:2853`, `3140`,
`3286`, `3577` and `3756`. `zserver.cpp:1889..1892` copies every factory
rally waypoint to a newly produced unit. The engine keeps one Z route per
unit or producer, storing stable target IDs, and dispatches actions through
its ordinary move, attack, boarding and service code. Shift appends actions;
STOP, death and ownership changes clear the relevant route. Attack target
death or ownership change ends that attack step. A crane ground click on a
wreck resolves its ID using the building or bridge footprint, since wrecks
are not selectable in the shared picker. Enemy-fort attack orders use the
previously traced native entrance tiles and the existing fort-fall path.

`MAXWAYPOINTS` is an engine limit of eight pending actions. A queued
`TC_PATH` with `WP_ONCE` appends all its points atomically; if they do not
fit, or the requested queued mode is loop/backtrack, the existing route is
retained. An unqueued `TC_PATH` keeps its existing engine mode. Factory
rallies are one-shot templates. Zod's internal dodge and aggression policies
are not inferred from these player commands. Route state participates in
save/load and consistency hashing. Headless `tests/z/test_production`,
`test_capture`, `test_commander` and `test_fort` cover mixed routes, Shift,
target loss, bounded paths, boarding, wreck service and fort entry.

### Manufactured robot groups

**Confirmed**, DOS `ZED.EXE` SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`:
fort and factory completion call `0x5060c` at `0x42131` and `0x42f8f`.
With manufacture flag `0x10f37b` set, `0x50a83` selects the production's
82-byte ROBOTS.DAT group and `0x50665..0x50683` walks up to 21 member words
at group +0x1c. `0x508cc` gives every manufactured member the same factory
origin; the initial-map four-pixel stagger at `0x50757..0x50770` is skipped.
`0x51044..0x51071` makes a leader with `0x77cc6` and attaches later
members through `0x77fd6`; `0x506c2` copies group +1 as formation selector.
Failures return carry at `0x506f0`/`0x5078c` without observed rollback of
already allocated members. Zod `zserver.cpp:1124..1157,2212..2251` also
creates a group at a common origin and gives its leader the factory route.

The port spawns each authored robot member as an ordinary mobj, retains its
ROBOTS.DAT record and leader/follower identity, and applies the factory route
once to the leader. Non-robot production and the production queue remain
unchanged. The shared group-ID allocator scans 240 live slots deterministically
and gives single-player CPU groups the upper half first, following
`0x77cc6..0x77d6d`. Native also prefers slots whose 200-pass reuse cooldown
has expired (`0x77880`, `0x775f5`), then falls back to any empty slot; that
preference is modeled as described below. If the engine cannot allocate a follower, already
spawned members remain and the engine's completed production is not retried;
native carry propagation beyond the caller remains unverified. Inspect with
`r2 -q -e bin.cache=true -c 'pd 70 @ 0x50665' -c 'pd 45 @ 0x77cc6' -c q data/Z/ZED.EXE`;
run `SDL_VIDEODRIVER=dummy build/bin/tests/z/test_production` for authored
members, group identity and save/load allocation checks.

**Confirmed reuse preference detail:** `0x77888` writes 200 to the indexed
byte in the table at `0x11d567`; `0x77608..0x77617` decrements its 240
nonzero entries. The allocator's first scan at `0x77d25` requires both an
empty 72-byte group record and a zero table byte. If that scan fails,
`0x77d4a..0x77d69` scans all 240 records for an empty one without checking
the table. This confirms a preference, not a prohibition on reuse. Group
removal at `0x777d9` clears the departing member's slot and decrements the
count at group +0x12; it loads the cooldown when the count reaches zero, or
when a departing leader has no occupied follower slot to promote at
`0x77b97`. Calls also occur during group reassignment (`0x77671`, `0x77710`),
so this is not limited to death. The update at `0x14b8d` runs before the
unit pass `0x14be5`, while level initialization `0x4f041` clears the table.
Open-rts stores the 240 timers in level data, saves and hashes them, loads
200 when its last live squad member leaves, and advances them by native-frame
steps. This approximates the native group record and reassignment paths: the
Z ticker runs after thinkers in coarser tics, so a departure during a tic can
be decremented at that tic's end instead of on the next native pass. The
engine does not represent the native exceptional failed-promotion case
independently of its live member list. `test_production` checks release,
preference, fallback, expiry, save/load and consistency. Reproduce with `r2 -q -e bin.cache=true -c
'pd 90 @ 0x777d9' -c 'pd 65 @ 0x77b97' -c 'pd 20 @ 0x775e4' -c
'pd 45 @ 0x77d25' -c 'pd 35 @ 0x14b75' -c q
data/Z/ZED.EXE`.

**Boarding distinction:** `0x7b225..0x7b232` removes a boarding non-APC
unit from its former group and allocates a new group for the converted unit;
the APC branch skips those calls. Open-rts instead transfers the existing
squad ID to its separate vehicle mobj, so that mobj reserves the ID until
it is destroyed or transfers its crew. `test_capture` checks boarding,
continued reservation beyond 200 native frames, and release on destruction.
This reservation preserves current engine identity and is not claimed to
reproduce the native non-APC allocation sequence. Inspect with `r2 -q -e
bin.cache=true -c 'pd 35 @ 0x7b1e9' -c q data/Z/ZED.EXE`.

**Countdown boundaries and command ownership:** for ZED.EXE SHA-256
`3635cb6960637eecd2298a84e8d6bdc4fe571ce88c716d3235151b7964184676`,
`0x603f1` runs the group pass before iterating the 240 unit slots. In
`0x780b3`, the +0x3d decrement saturates at zero for the reachable 0..120
range; its signed-byte `jns` sequence is not general unsigned saturation for
128..255. When group +0x10 is zero or decrements to zero, the same pass
clears both +0x3d and +0x40. If group +9 bit 0x40 is set at expiration,
`0x7810e` compacts member slots and the pass clears that bit. A 30 loaded by
route setup while +0x10 is zero is therefore cleared on the next group pass,
not sustained for 30 frames. It may affect work during the current unit pass;
the exact intervening calls remain **unknown**. For example, `(countdown,
target timer) = (120, 1)` becomes `(0, 0)` on the next group pass, while
`(120, 75)` becomes `(119, 74)`; target events can refresh the latter timer.

The player dispatcher sets +0x98 bit 0x20 on the selected unit. It skips the
group command copy for command codes 8, 9, 11 and 12; for other commands it
copies the group fields when +0x5c is nonzero. Leader dispatch `0x7d053`
copies the command and operands under its own gates but does not copy +0x98
bit 0x20. Whether another path sets that bit on the leader after a follower
selection remains **unknown**. Route setup clears the bit even when unit
+0x6f bit 0x10 prevents loading +0x3d.

Additional command lifecycle check: `0x7d053` copies squad command and
operands only to a unit with leader slot `0xff`, and `0x772bf` invoked by
both the direct dispatcher and this copy does not propagate the command bit
for ordinary robot classes. Separately `0x7d0a2` clears unit +0x98 bit
0x20 before clearing the squad command byte +0x3a and unit order +0x5a.
These observations do not rule out another writer setting the leader bit;
they do show that copying an order to the leader is not itself sufficient
to establish the 120-frame countdown. A faithful implementation still needs
the selected-unit iteration, all bit writers, and target-timer lifecycle
mapped to the engine's normalized squad commands. The command-replay entry
`0x478f6..0x47937` now supplies a further distinction: nonnegative command
codes index the 72-byte group using the event byte at +2, reject an empty
group or leader slot `0xff`, and resolve group +0x13 to the leader's unit
record before setting `0x104048`. Thus that event path sets +0x98 bit 0x20
on the resolved leader at `0x47af6`, regardless of which squad member led
to the group event. Negative command codes bypass this group resolution and
have separate handling. This establishes the replay/event resolution, not
yet every local UI selection and event-generation path. A search of the
linear service disassembly finds only the direct +0x98 bit-0x20 OR at
`0x47af6`; indirect or bulk writes are not excluded. Inspect with `r2 -q -e
bin.cache=true -c 'pd 35 @ 0x47aec' -c 'pd 8 @ 0x772bf' -c 'pd 38 @
0x7d053' -c 'pd 18 @ 0x7d0a2' -c 'pd 28 @ 0x478f6' -c q data/Z/ZED.EXE`.

The preceding statement that nonzero +0x3d suppresses the walking unit's
enemy scan is too broad: `0x7ab38..0x7ab5d` checks it only in the branch
where global `0x10e91b` is zero. The other alternating branch calls `0x78fed`
without that check. Do not suppress both scans based on this countdown.
Reproduce with `r2 -q -e bin.cache=true -e bin.relocs.apply=true -c
'pd 35 @ 0x603cc' -c 'pd 50 @ 0x780b3' -c 'pd 25 @ 0x7c6ba' -c
'pd 45 @ 0x47aec' -c 'pd 35 @ 0x7d053' -c 'pd 35 @ 0x7ab0c' -c q
 data/Z/ZED.EXE`.

**Additional target-timer writes:** `0x58219..0x58237` and
`0x5859f..0x585bf` call `0x75ca3`, which reloads the other unit's squad
+0x10 to 75 whenever that unit has a nonzero +0x5c. If the other group's
+0x3b is `0xff`, those callers also populate +0x3b/+0x44 and +0x3e and
reload the timer. Separately, `0x58745..0x587d0` walks unit indices,
excludes same-team units and several unit types/actions, tests position
against a rectangle, and reloads +0x10 to 75 for each qualifying grouped
unit. This is an additional event source, so modeling timer refresh only on
individual target acquisition would be incomplete. The event that invokes
that rectangle traversal and its complete gates remain **unknown**.

At the robot attack transition `0x759a7..0x759da`, the attacker's current
group always receives the selected target in both +0x3b and +0x44 and a
75-frame +0x10 timer. The call to `0x75ca3` independently refreshes the
target's group when its unit has a nonzero group slot; only if that group's
+0x3b is `0xff` does this caller also install the attacker as its target.
Thus refresh is symmetric across two groups, but target replacement is not.
The route path `0x7c6c6` clears the current group's +0x3b whenever unit
+0x6f bit 0x10 is clear, before checking the player's +0x98 bit or deciding
whether to load +0x3d. This target clear must not be modeled as conditional
on a countdown being loaded. Inspect `r2 -q -e bin.cache=true -c 'pd 38 @
0x759a7' -c 'pd 16 @ 0x75ca3' -c 'pd 18 @ 0x7c6ba' -c q data/Z/ZED.EXE`.

Damage supplies another refresh source: both `0x86ca4` (ordinary unit/body
damage) and `0x86d1f` (driver damage) converge on `0x75ca3` with ESI set to
the damaged unit at `0x86d0a` and `0x86d3f`. The common refresh follows the
body-survival and protected-flag branches as well as unit-health subtraction;
do not limit it to lethal hits or infer that every refresh implies a new
target. The helper only updates grouped units (+0x5c nonzero). This is
separate from the earlier acquisition and vehicle-contact refreshes. Inspect
with `r2 -q -e bin.cache=true -c 'pd 45 @ 0x86ca4' -c 'pd 25 @ 0x86d1f'
-c 'pd 16 @ 0x75ca3' -c q data/Z/ZED.EXE`.

**Engine integration boundary:** `Z_RouteTick` runs both player route steps
and producer-inherited rally templates through the same internal `issue()`
and `G_RunTiccmd` path, while `Z_SquadOrder` also serves internal commands.
Consequently neither call by itself proves native player command bit +0x98
was set. Similarly the engine's current attack target pointer can be assigned
before native attack-transition timing and does not encode vehicle-contact
refreshes. Loading +0x3d on every route execution or refreshing +0x10 on
every nonnull pointer would therefore add unsupported speed boosts. A port
needs explicit player-command provenance and group target/response state,
including save/load and squad reuse, together with tests of player, AI,
factory rally, acquisition and damage paths. These are unresolved here.

A broad linear disassembly search found `0x47af6` as the only direct
`or byte [ebp + 0x98], 0x20` write; `0x7c059..0x7c0a9` iterates squad
members but only clears specific +0x5a orders and does not set that bit.
This search does not exclude indirect or whole-byte writes, so it is not
proof that player selection never sets a leader's bit through another path.
Reproduce with `r2 -q -e bin.cache=true -e bin.relocs.apply=true -c
'pd 35 @ 0x58214' -c 'pd 30 @ 0x58592' -c 'pd 65 @ 0x58745' -c
'pd 35 @ 0x7c059' -c q data/Z/ZED.EXE` and search the broad disassembly
for writes to `+ 0x98`.

**Rectangle event and command dispatch refinement:** action table `0x149038`
entries 9 and 10 both point to `0x57dd6`, which calls `0x58679` at
`0x57df1`. That routine returns if global `0x10e91b` is nonzero or source
unit +0x6f bit 2 is set; otherwise `0x7911a` collects up to 288 indices
from nine spatial bins, and the inner rectangle tested at `0x58783` is
inclusive source x−8..x+16, y−8..y+16. Qualifying opposing units outside
classes +0x79 = 1 or 2 and actions +0x35 = 0x12, 0x17 or 0x2e refresh
their own group's +0x10 before `0x60fe6` or `0x58910`. The latter changes
the unit to action 0x17 or 0x2e; the wider rectangle can issue an avoidance
order. This is the vehicle contact/knockdown path, not an ordinary enemy
acquisition timer. Its full interaction behavior remains unported; do not
invent proximity timer refresh on all units.

**Vehicle avoidance rectangle:** `0x586ac..0x5872b` expands the inner
rectangle x−8..x+16, y−8..y+16 by 10 on the left, right and top and 6 on
the bottom. If the source's sampled terrain is neither 7 nor 0x33 it expands
all four edges another 5, yielding outer bounds x−23..x+31, y−23..y+27;
on terrain 7 or 0x33 the bounds are x−18..x+26, y−18..y+22. An eligible
unit outside the inner rectangle but inside the outer one receives destination
`2 * unit_origin - outer_rectangle_centre` at `0x588aa..0x588eb`, then
`0x7ba2a` and `0x7ba9a`. The preceding same-team exclusion at
`0x5877a..0x58781` means the same-team-only path in `0x5897c..0x589c8`
is unreachable from this scan: `0x104154` retains the source unit, and
`0x58948` returns carry clear for the opposing team. This supersedes any
inference that its terrain-7 group-speed gate protects these opponents.
Complete porting remains blocked on the `0x7ba9a` movement/order gates and
the inner knockdown/get-up action lifecycle, which must interrupt and resume
existing orders correctly. In particular `0x7ba9a` returns carry if the
destination equals the current unit origin, clears +0x6f bit 2 otherwise,
rejects classes 2, 4, 5 and 6 unless action 2 is already active, and starts
action 2 for other classes. A knocked-down robot waits in action 0x17/0x2e
while a vehicle record is within strictly 24 pixels on each axis; absent
one, `0x61f43` changes it to get-up action 0x18/0x2f, and `0x61f94` advances
11 frames before calling `0x79049`. **Correction:** an earlier trace called
this an explosion list. `0x575a3` rebuilds the list at `0x18b81c` from
active unit records with class +0x79 equal to 1 and +0x6f bit 2 clear,
recording their unit pointers and top-left positions. `0x575fa` refreshes
those positions; the hold test does not compare team. `0x79049` sets robot
action 1 and conditionally re-evaluates negative command +0x5a, so it does
not simply restore the old action byte saved at +0x72. A byte-pattern search
for direct EBP-relative byte reads of +0x72 (`8a4572`, `807d72`,
`0fb64572`) found none; this does not rule out indirect or wider reads.
The exact lifecycle
of unit +0x6f bit 2 and mapping of pending native commands into engine route
state remain necessary for a faithful port. The bit is not simply a
stationary-vehicle flag: `0x57b1c..0x57b7f` sets it for class-1 vehicles
while saving a 2×2 terrain patch into body +0x1c/+0x1e and stamping terrain
0x13 there. `0x57b94` conditionally clears both low bits of +0x6f and
restores that patch when body +0x17 bit 2 is set; `0x7baaf` also clears bit
2 when starting movement. This temporary path/occupancy state is not
currently represented by a matching vehicle flag in open-rts. Calls at
`0x577de` and `0x57a47` follow collision-resolution paths that record
another unit in +0x6e and set +0x6f bit 1; `0x7b33c` invokes the same
occupancy placement on path completion if that bit is still set. While
waiting, `0x57a61` clears the relationship when the other vehicle becomes
inactive, moves more than 48 pixels away on either axis, or the +0x70
counter expires. These are connected-unit and occupancy lifecycles, not a
single motion-state predicate. Inspect with `r2 -q -e bin.cache=true -c
'pd 60 @ 0x58679' -c 'pd 75 @ 0x5883f' -c 'pd 50 @ 0x58948'
-c 'pd 35 @ 0x575a3' -c 'pd 20 @ 0x575fa' -c 'pd 45 @ 0x61f43'
-c 'pd 55 @ 0x57b1c' -c 'pd 35 @ 0x57b94'
-c 'pd 40 @ 0x57a61' -c 'pd 20 @ 0x7b31f'
-c 'pd 55 @ 0x79049' -c q data/Z/ZED.EXE`.

For a queued ordinary command, `0x4767f..0x47692` serializes the unit's
+0x5d group ID. On receipt `0x4790d..0x47937` validates group +0x12 and
resolves group +0x13 into a unit pointer, then `0x47988` calls the dispatcher
which sets that unit's +0x98 bit 0x20. Thus this path selects the current
leader by group, even if a different member supplied the queued command.
The immediate path `0x476ed..0x47720` uses the existing pointer at
0x104048 instead. In one selection path `0x69cbf` obtains the unit from
one-based selection index `0x103ce1` through `0x68498`, then puts that
pointer in 0x104048; unlike the queued path this call does not itself
normalize it to group +0x13. Selection normalization and the full set of
command paths remain **unknown**, so the preceding direct-write search
alone cannot determine every local command's recipient. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 60 @ 0x58679'
-c 'pd 45 @ 0x58745' -c 'pd 35 @ 0x7911a' -c 'px 48 @ 0x149038'
-c 'pd 40 @ 0x47674' -c 'pd 40 @ 0x478f3' -c 'pd 25 @ 0x69cbf'
-c 'pd 12 @ 0x68498' -c q data/Z/ZED.EXE`.

**Later CPU gun selection boundary:** `0x9de4a`, `0x9de9b` and `0x9def5`
choose the regular family when global `0x20a448` is nonzero or building
+6 bit 2 is initially clear; they set that bit on the regular path. Otherwise
they call `0x999e5` and require its result >=500, require fewer than two
existing territory guns for a factory or four for a fort (`0x26ae5`), and
require `0x26ab4` to accept the four-slot territory capacity before choosing
the gun family through `0x9e235`. Robot factories and forts additionally
require building +0xc >1000; tank factories do not. `0x20a448` is cleared
at `0x42051`/`0x42e9e`, set after successful `0x430b2` at
`0x420ca`/`0x42f17`, and reset after selection at `0x414e8`/`0x42b98`.
Thus it is not safe to replace the initial selector with an unconditional
periodic gun choice. The precise building update lifecycle, initial/reset
state of +6 bit 2 on ownership changes, and mapping of the placement result
to `0x20a448` remain **unknown**. The current AI calls the initial selector
when no product is active and retains a product through repeats, so a
faithful port also needs those selection timing transitions. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 90 @ 0x9de4a'
-c 'pd 35 @ 0x999e5' -c 'pd 25 @ 0x26ab4' -c 'pd 45 @ 0x420a2'
-c 'pd 35 @ 0x42eef' -c 'pd 15 @ 0x414d8' -c q data/Z/ZED.EXE`.

**APC bullet salvo:** In the coordinate-target loop `0x1eb04..0x1ebee`,
each crew member dispatches through `0x1ec13` with port index zero.
For bullet drivers `0x1ed92` reads the first facing-indexed word pair from
`0x12db1e` and subtracts (6,6); Laser additionally subtracts (7,7) at
`0x1f05d`. `0x1effd` calls tracked-target aim `0x75cc1`, and `0x1f086`
increments the spawned bullet's range byte by one (eight pixels). The
bullet record stores the firing damage on creation. The engine now launches
one ordinary saved bullet per crew member for Grunt, Sniper, Psycho and
Laser against unit targets, with that port and the driver's damage and
range. A later driver change therefore does not alter a bullet in flight.
`tests/z/test_combat.c` checks all eight facings and four drivers, actual
flight, interception, and save/load before changing driver. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 100 @ 0x1eb04'
-c 'pd 110 @ 0x1ed92' -c 'pd 75 @ 0x1effd' -c 'px 192 @ 0x12db1e'
-c q data/Z/ZED.EXE` and `make build/bin/tests/z/test_combat`.

The engine maps its current unit target to the native tracked-target input
and samples its deterministic scatter independently at launch. Native
`0x1ee09` reads the global tracked slot even in the coordinate path; its
lifecycle and cached-aim refresh are not represented. Scanner mode's
separate 12-port-attempt selection, non-unit target dispatch, native random
stream and exact unit-slot update order remain approximations or unknown.

**Bullet terrain gate:** Before checking unit bins, `0x5ac67` calls
`0x5abad` with the bullet's current pixel position. That function indexes
the 128-byte terrain table at `0x11662f` using the tile attribute masked
with `0x7f`; `0xff` marks the stopping case. For y bit 3 clear it tests
the current tile. For y bit 3 set it tests the tile below first and stops
only if both tiles are marked `0xff`. It returns carry and invokes the
`0x6b641` effect on a stop; `0x5ac67` then returns without scanning units.
The engine implements the confirmed gate for represented map tiles before
bullet-unit collision; the effect itself and native behavior beyond the map
edge remain unimplemented. `test_bullet_terrain_gate` covers both halves,
both combinations of adjacent tile attributes, the high attribute bit and
collision ordering. Reproduce with `r2 -q -e bin.cache=true -e
bin.relocs.apply=true -c 'pd 45 @ 0x5abad' -c 'pd 30 @ 0x5ac67'
-c 'px 128 @ 0x11662f' -c q data/Z/ZED.EXE` and
`make build/bin/tests/z/test_combat`.

The omitted terrain-hit effect is not an immediate damage operation:
`0x6b641` dispatches a `burst3` particle constructor via `0x6d093`.
At `0x6b667..0x6b6c7` it samples a four-way variant, randomized signed
horizontal velocities, and a vertical velocity from -4 through -11; it
sets a 200-step field and selects a callback, with a special callback when
the global sprite word is 0x76f. The constructor iterates the caller's
count, obtaining the first free 44-byte particle through `0x6e0f1`; the
pool is initialized with 2000 records at `0x6e10f`, while the active scan
count is the runtime value at `0x1d4458`. The ordinary callback `0x6d23e`
frees the record. The special callback `0x6b759` can invoke explosion
`0x8476c` for terrain types 9, 0x26, 0x27, 0x28 and 0x2a, and otherwise
tries to create a separate effect record. The circumstances that supply
sprite 0x76f elsewhere, particle motion and update timing still need tracing before
an ordinary mobj can reproduce the effect. Reproduce with `r2 -q -e bin.cache=true -e
bin.relocs.apply=true -c 'pd 55 @ 0x6b641' -c 'pd 50 @ 0x6d093'
-c 'pd 65 @ 0x6b7c8' -c 'pd 30 @ 0x6e0f1'
-c q data/Z/ZED.EXE`.
The bullet terrain-hit caller `0x5ac2e..0x5ac3c` supplies sprite 0xfa and
count 1, so it selects the ordinary freeing callback, not the special 0x76f
explosion path. The particle update at `0x6d43b` decrements its +0x12 word
and enters the termination branch only when the result becomes negative;
the relation of this update pass to simulation tics is still unconfirmed.
On a surviving pass `0x6dd79` adds signed byte +0x0c to vertical word
+0x04; crossing above zero clamps it and normally terminates, while +0x18
bit 0 selects a bounce. The bullet caller sets +0x18 to 4, so its particle
takes the termination branch on that crossing. Bounds can also end the
particle before its timer expires. Projection and velocity decay still
require tracing.

**Ordinary bullet tracer:** `0x5ab34..0x5ab3d` constructs palette index
`0x17 + 16 * (bullet +4 high nibble)` and calls the function pointer at
`0x104378`, whose initialized target is `0x81aba`. That VGA routine draws
four pixels at the bullet position as `(index+3, index; index, index-3)`.
The engine builds this native 2x2 indexed picture directly, using its
existing team palette translations, and draws it with the bullet mobj.
`test_units` checks the indices and palette mappings; `test_combat` checks
ordinary bullets are renderable across save/load. The separate Laser path
at `0x5ab44..0x5abac` uses sprites 1364 and 1365 with a continuous angle;
it remains invisible until that drawing path is reproduced. Simulation and
render frame timing still differ from the 70 Hz native pass. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 35 @ 0x5ab04'
-c 'pxw 4 @ 0x104378' -c 'pd 28 @ 0x81aba' -c q data/Z/ZED.EXE` and
`make build/bin/tests/z/test_units build/bin/tests/z/test_combat`.

**Laser tracer rotation details:** The Laser firing path sets `0x148be1`
before `0x5af81`; the bullet constructor `0x5b0b1..0x5b0f5` computes an
angle from launch and aim through `0x11ad5`, scales its integer degrees by
256/360 and stores the low byte at bullet +0x18. `0x11ad5` returns north 0,
east 90, south 180 and west 270. After passing screen clipping, the display
path `0x5ab44..0x5abac` flips bullet +0x19 on each visible draw, chooses
`1364 + phase`, and passes `255 - angle`
to `0x47208` with scale word `0xe052c = 1`; `0x47232` adds 16 to that
scale. Both native pictures are 16x16 in `SPRITES.RSC`. The caller subtracts
(8,8) from the bullet position; `0x47251` then adds half the source width
to both coordinates before the rotation routine. `0x3d814` uses a 256-step
trigonometric table at `0x1422c4` for the transformation; `0x3d1b4` is an
alternate drawing-mode branch. Phase advances on draw, not on simulation
update. The engine currently selects preauthored directional frames and has
no equivalent per-sprite arbitrary-angle rasterizer, so neither substituting
eight directions nor advancing the phase on the thinker establishes native
visual parity. Laser tracers remain invisible pending a verified rendering
implementation. Reproduce with `r2 -q -e bin.cache=true -e
bin.relocs.apply=true -c 'pd 35 @ 0x5ab44' -c 'pd 45 @ 0x5b0b1'
-c 'pd 45 @ 0x11ad5' -c 'pd 30 @ 0x47208' -c 'pd 100 @ 0x3d814'
-c q data/Z/ZED.EXE`; `Z_SpriteSize` confirms both source dimensions.

**Bullet launch height:** `0x5afab` saves the firing unit's +0x10 terrain
pointer before bullet allocation; `0x5afc5..0x5afd7` reads its masked
terrain byte through height table `0x11714b` and stores height plus one in
the projectile. Unit position update `0x796c7..0x796ec` computes that
pointer from the native unit origin plus (8,8), not from its muzzle.
The engine uses that shooter-relative cell for the saved bullet height.
`test_bullet_launch_height` puts the muzzle and shooter in different tiles
with both height arrangements, and exercises real bullet travel across
equal-height terrain. The native pointer's update scheduling may differ
from recomputing the cell at firing time. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 32 @ 0x5af81'
-c 'pd 16 @ 0x796c7' -c q data/Z/ZED.EXE` and
`make build/bin/tests/z/test_combat`.

**Bullet range ordering:** On an unfinished step `0x5aa57..0x5aa9b`
compares squared displacement from the stored launch point against the
square of the range byte times eight; equality or greater exits through
`0x5a986` before `0x5ac67` scans terrain and units. An initial segment
that completes instead takes `0x5a941..0x5a985`, performing its two collision
passes without that range check. The engine now preserves this ordering;
`test_bullet_range_order` checks a target one pixel inside the range,
exactly at it, at a completed segment's endpoint, and only at the extended
second point beyond range. The separate native
pre-range map-object scan at `0x5aa49` is still unimplemented. Reproduce
with `r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 100 @ 0x5a90d'
-c 'pd 35 @ 0x5aa57' -c q data/Z/ZED.EXE` and
`make build/bin/tests/z/test_combat`.

**Possible ambient house inhabitants (inferred):** `0x87410` scans the 200 effect records;
`0x87591` compares each live record's first sprite against 16 entries at
`0x14c205`. The five populated entries are 2937, 2938, 2939, 2935 and
2936, the Desert, Jungle, Volcano, Arctic and City house pictures in
`objects.inc`. For a match `0x874c5..0x87585` clears the effect record's
flag 0x80, creates a linked record through `0x889e4`, then makes up to six
calls to `0x875c7`; each call selects one of four types from `0x14c1d5`.
Correction: the static bytes at `0x14c1d5` are overwritten by world setup
`0x45a62..0x45a77` from `0x14c1dd`, so the earlier interpretation of
2, 3, 5 and 11 as the fixed runtime choices was incorrect. In world order
Desert, Arctic, City, Volcanic and Jungle the rows are respectively
`{2,3,11,2}`, `{15,16,42,15}`, `{32,32,33,41}`, `{8,9,10,37}` and
`{18,19,20,52}`. Zod calls comparable hut occupants animals and has
world-dependent species in `reference/zod/src/ahutanimal.cpp` and
`ohut.cpp`; its independent implementation is not proof of native identity.
Before each allocation `0x87578` calls `0x873f0`, which writes terrain
`0x11` at the house's pixel origin and preserves attribute bit 7; loop
counts 6 through 3 always execute this path. The normal startup call is
`0x13846`, after the placed-object pass. Open-rts reproduces this terrain
stamp after all placed objects, including crates, have been loaded; it
stores the changed ground attribute for consistency and saves. The shipped
level-one houses and save/rebuild behavior are checked in
`test_terrain_speed`. `test_capture` exercises all five types, attribute
bit 7, a flag and crate sharing a house cell, suppression of crate pickup
under the later house mark, and restoration of the crate's saved flag mark.
The native effect record's separate flag 0x80 clear at `0x874cd` is not
modeled; it is distinct from the preserved terrain attribute bit.
Desert types 2, 3 and 11 have descriptors at `0x14c309` with nonzero high byte at offset 3,
which the constructor stores in the spawned 64-byte record +0x20 at
`0x876a0..0x876ac`. `0x8752e..0x8753b` sets a random 32..42 delay,
stored at record +0x27; `0x88c5c..0x88c68` decrements that word before
processing, while the bullet filter `0x88b26` rejects a nonzero low byte.
That distinction also matters for the later 160..287 delay: at exactly 256
the bullet test sees a zero low byte even though the full delay is nonzero.
Once the delay clears, these records pass both the +0x27 and +0x20 parts
of the bullet's `0x88b13` eligibility gate. This establishes one source of relevant native
dynamic effects, not their complete movement, lifetime, position update,
or gameplay semantics. Open-rts does not yet create these inhabitants, so
it cannot faithfully reproduce bullet preemption against them. Reproduce with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 110 @ 0x87475'
-c 'pd 25 @ 0x87591' -c 'pxh 64 @ 0x14c205' -c 'pxh 8 @ 0x14c1d5'
-c 'pd 16 @ 0x873f0' -c 'pd 55 @ 0x875c7' -c 'pd 20 @ 0x88c43' -c 'pd 45 @ 0x88b13'
-c q data/Z/ZED.EXE`.

**House-animal lifecycle trace:** `0x889fc` allocates the first free of
100 records; `0x88c43` decrements a nonzero +0x27 delay and skips the rest of
that update even when the decrement reaches zero. Processing resumes on the
next pass. `0x8924b` gates animation
and movement on the byte timer +0x2f; `0x89219` selects a direction's frame
run from the descriptor, and `0x88f1f` advances position using signed pairs
at `0x14c1a0`. At `0x88d72` the routine selects a turn and probes terrain
through table `0x1172dd`; it removes the record after ten failed attempts
(`0x88dae..0x88dbc`). A return-home path (`0x88fed..0x8908f`) uses the
saved creation coordinates and chooses the eight-way bearing; on its
arrival `0x88d1d..0x88d40` sets a 160..287 native-step delay. `0x8953e`
also searches for nearby units and affects its state. The exact fear and
avoidance thresholds, successful movement state transitions, recreation
policy after a bullet or flame burst, and representation of the shared
100-slot priority are not yet fully mapped. Zod's `OHut::Process` maintains
and periodically changes a preferred population, and `AHutAnimal::Process`
has walking, looking and return-home states, but those rules differ in shape
and timing from this native trace. Do not infer a native implementation from
the Zod timers. Reproduce with `r2 -q -e bin.cache=true -e
bin.relocs.apply=true -c 'pd 160 @ 0x88c43' -c 'pd 60 @ 0x88f1f'
-c 'pd 75 @ 0x88fed' -c 'pd 80 @ 0x8953e' -c 'px 32 @ 0x14c1a0'
-c q data/Z/ZED.EXE`.

The nearby-unit branch `0x8953e..0x89653` is more specific than generic
fear: for effect class +0x3d equal to 0, 1 or 2, it scans the unit list
returned by `0x7911a`. A class-1 vehicle or unit in action 0x12 whose
origin lies inclusively within `[effect.x-8,effect.x+16]` and
`[effect.y-8,effect.y+16]` sets effect +0x36 bit 0x20, resets +0x2e,
and sets +0x2f to 127. On subsequent passes `0x89280` chooses descriptor
sprite +0x18 and `0x88c76` skips the remaining effect update. The full
duration and cleanup of that state are still unknown. Inspect with
`r2 -q -e bin.cache=true -e bin.relocs.apply=true -c 'pd 100 @ 0x8953e'
-c 'pd 24 @ 0x88c43' -c 'pd 24 @ 0x89280' -c q data/Z/ZED.EXE`.
