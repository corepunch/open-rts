# Reverse Engineering References

Keep these links handy when touching loaders, tile animation, map objects, or
plugin-specific behavior.

## Dark Colony

- DarkColony.pl downloads:
  https://www.darkcolony.pl/downloads.php?cat_id=2
  - Community downloads and historical Dark Colony material. Check here when
    looking for tools, map/editor notes, or the Polish community project files.
  - `DCjxspr_v002` names the third/fourth `.SPR` descriptor words `disX` and
    `disY`; treat them as per-frame placement displacement, not unused padding.

- endotermic/Dark-Colony:
  https://github.com/endotermic/Dark-Colony
  - Open-source Dark Colony reference lead. Use it when validating `.MAP`,
    `.BTS`, `.SPR`, object placement, palette cycling, and faction/unit logic.
  - Contains original Classic/Council Wars game-data snapshots and a Ghidra
    workspace. `REAP.SPR` and `REAP.FIN` match our local data byte-for-byte.

- cookgreen/OpenDC:
  https://github.com/cookgreen/OpenDC
  - OpenRA Dark Colony mod/remake. Best current source-code reference for a
    Dark Colony `.SPR` loader, though it does not parse `.FIN` animations.
  - `OpenRA.Mods.DarkColony/SpriteLoaders/SPRLoader.cs` reads each frame
    descriptor as `width`, `height`, `offsetX`, `offsetY`; stores offsets as
    per-frame `ISpriteFrame.Offset`; and decodes compressed frames with the same
    high-bit skip / low-7-bit run-count RLE shape used here.
  - The repository bundles original `.SPR`, `.BTS`, maps, sounds, and music
    under `mods/dc/bits/original`, but not `ANIMATE/*.FIN` or `ANIM.DAT`.
    Its `REAP.SPR` is byte-identical to our local `REAP.SPR`.
  - `mods/dc/sequences/units.yaml` is not a completed Dark Colony unit
    animation mapping. It only has small placeholder-style entries such as
    `AIRD` and `ALBU1..ALBU15`; no `REAP`, `BARR`, `GRAY`, or `TROOPER`
    sequences were found there.

- OpenRA forum thread for OpenDC:
  https://forum.openra.net/viewtopic.php?t=21223
  - Project discussion by DoDoCat/cookgreen. Useful context: early posts state
    the project had mostly imported resources, and a commenter mentioned having
    a working C# `BTS`/`SPR`/`MAP` loader. No public `.FIN` animation parser was
    found from this trail.

Local game-data files that have already been useful:

- `data/DCOLONY/GAMESTAT/GAMESTAT.TXT` for unit IDs, names, health, and weapon
  IDs.
- `data/DCOLONY/GAMESTAT/WEAPSTAT.TXT` for weapon range, damage, and rate of
  fire. Trooper/Grey weapon rows use range `4`, damage `100`, and rate `15`.
- `data/DCOLONY/GAMESTAT/DEPEND.TXT` for unit/building dependency names.
- `data/DCOLONY/SCENARIO/*.MAP`, `*.SCN`, and `*.BTS` for maps, starting
  objects, tilesets, and water palette bands.
- Dark Colony `.SCN` object rows shaped `x y 40 rate amount` are Petra-7 vents,
  not ordinary starting units. The original mission text explicitly calls them
  Petra-7 vents and training orders say to move/deploy the Exploiter or Brozaar
  over **active** vents to extract P-7. The fourth field is the vent extraction
  rate; `0` means dormant/inactive. The fifth field is remaining P-7 amount.
- Dark Colony `.TRO` scripts control vent eruptions. Commands such as
  `newrate 25 38 29` and `newrate2 81 77 15` set the extraction rate at an
  existing vent coordinate; `setmoney x y amount` sets the remaining P-7. This
  matches training text like "Second Vent Has Become Active" and "Watch For
  Erupting Vents."
- Dark Colony `.MAP` files have a flags plane after the terrain tile planes.
  For ground units, flag bit `9` (`0x0200`) marks impassable terrain. `.PTH`
  files contain path/family data and are not the terrain passability mask.
- `data/DCOLONY/SPRITES/*.SPR` for unit sprites.
- `data/DCOLONY/ANIMATE/*.FIN` for sprite animation labels and frame ranges.
- `data/DCOLONY/ANIM.DAT` — newline-delimited index of all `.fin` filenames
  (lowercase) used by the original engine at startup; useful for bulk-loading.

### Dark Colony SPR Binary Format

```
offset  size  field
0x00    u16LE flags          bit 7 = per-frame RLE compression
0x02    u16LE frame_count
0x04    4     padding / unknown
0x08    768   palette        256 × RGB (no alpha; index 0 = transparent)
0x308   frame_count×8  frame descriptors:
              u16LE width, u16LE height, u16LE dis_x, u16LE dis_y
after descriptors: pixel data
  if compressed: per-frame [u32LE chunk_size][RLE data]
    RLE: signed byte cmd; cmd<0 → skip (-cmd) pixels; cmd≥0 → copy (cmd+1) pixels
  if uncompressed: raw indexed pixels, row-major, width×height per frame
```

### Dark Colony FIN Binary Format

`.FIN` files map human-readable animation label names to ranges of FIN draw
commands. Those commands then point at raw frames inside one of the companion
`.SPR` files. `ANIM.DAT` lists all `.fin` stems; the matching unit `.SPR` is in
`data/DCOLONY/SPRITES/`.

```
offset  size  field
0x00    u16LE magic          always 0x001d (29); treat as version/format ID
0x02    u16LE aux_count?     not the command count; exact meaning still unknown
0x04    u16LE valid_count    number of named (non-NONAME) labels
0x06    u16LE ref_count      number of companion sprite references
0x08    ref_count×8  sprite_refs   null-padded 8-byte ASCII names of companion
                                   SPR stems loaded alongside this animation
0x08+ref_count×8  valid_count×20  named label table:
              bytes 0..15  name    null-padded ASCII label (e.g. "GRAYMOVE0")
              bytes 16..17 start   u16LE first command index (inclusive)
              bytes 18..19 end     u16LE last command index (inclusive)
after the named labels: NONAME/padding/auxiliary data, then a 22-byte command
table that runs to EOF:
              bytes 0..7   sprite  null-padded lower-case companion SPR stem
              bytes 8..9   frame   s16LE raw frame inside that sprite
              bytes 10..13 x/y     s16LE draw offsets
              bytes 14..15 flags?  usually 0 in current samples
              bytes 16..17 tics?   usually 16 in current samples
              bytes 18..19 layer   1 for main unit body, 3/5 for effects/overlays
              bytes 20..21 misc?   unknown
```

Important: label `start..end` values are **not raw `.SPR` frame ids**. For
example, `GRAYFIREA0` starts at command index `78`, and command `78` points to
raw `GRAY.SPR` frame `80`. Likewise `GRAYMOVE2 0x47..0x4d` is seven command
records, not raw frames `71..77`; its body commands point at raw frames
`23,31,39,47,55,63,71`.

### FIN Label Naming Convention

Labels follow the pattern `<STEM><ACTION><DIRECTION>` where:

- `STEM` matches the FIN/SPR filename stem (e.g. `GRAY`, `TRSC`, `TROOPER1`).
- `ACTION` is one of: `STAND`, `SHUF` (idle shuffle), `MOVE`, `ANT` (anticipation
  pose), `FIREA`, `FIREB` (two fire phases), `HITB` (hit reaction), `MOVEC`
  (alternate move), `DIEA`, `DIEB`, `DIEC` (death strips per quadrant), `BLOOD*`
  (blood effects), `FUNK` (misc), etc.
- Dark Colony state-facing codes are sprite slots `0..7`:
  `0=down`, `1=down-right`, `2=right`, `3=up-right`,
  `4=up`, `5=up-left`, `6=left`, `7=down-left`.
- The legacy sequence path still uses the older engine compass codes
  `{0, 2, 4, 6, 8, 10, 12, 14}` and is kept for Dark Reign compatibility.

The direction code set and ordering **differ by sprite**:

| Sprite       | Generated frame-slot direction order        | Frame layout |
|--------------|---------------------------------------------|--------------|
| `GRAY.SPR`   | `down,down-right,right,up-right,up,up-left,left,down-left` | frame-major |
| `TRSC.SPR`   | `down,down-right,right,up-right,up,up-left,left,down-left` | frame-major |
| `TROOPER1.SPR` | `down,down-right,right,up-right,up,up-left,left,down-left` | frame-major |

**Frame-major layout**: all directions for one animation phase are contiguous,
then the next animation phase follows. This matches Doom's mental model:
`TROO A1..A8`, then `TROO B1..B8`. For an 8-direction action:

```
raw_frame = action_base + frame_loop_index * 8 + direction_index
```

Example for `TRSCMOVE`:

```
TRSC_RUN1 → frames 16..23  (animation phase 1, all directions)
TRSC_RUN2 → frames 24..31  (animation phase 2, all directions)
TRSC_RUN3 → frames 32..39
...
TRSC_RUN8 → frames 72..79
```

To build generated Doom-style `states[]` for Dark Colony:

```c
// One state per animation phase; the state's frame set contains rotations.
int direction_codes[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
int trsc_run1_frames[8] = { 16, 17, 18, 19, 20, 21, 22, 23 };
int trsc_run2_frames[8] = { 24, 25, 26, 27, 28, 29, 30, 31 };
```

Attack strips are frame-major too, but their FIN labels include overlay commands
for `BLAZ`, `GLIT`, and related sprites. Generated body states must dereference
FIN commands and use only layer-1 commands for the unit sprite before applying
the Doom-style `base + phase * 8 + direction` formula. `GRAYFIREA0` therefore
uses body base frame `80`, even though the FIN label range starts at command
index `78`.

Muzzle flashes are generated from the same command table. For the Doom state
that calls `A_DC_MuzzleFlash`, the generator searches the FIN fire labels for
the matching layer-1 body frame, then chooses the nearest following layer-3
`BLAZ` command. The generated muzzle flash entries are ordinary `states[]`
rows: `BLAZ.SPR` frame `0`, one screen-space offset per Dark Colony direction,
and render flags for a bright additive yellow pass. `mobjinfo[].muzzleflash`
points at the unit's muzzle flash state. `GLIT` and layer-5 unit-sprite commands
remain separate overlays for later weapon polish, not the primary muzzle flash.

### Historical Rotation Bug

The old sequence path treated Dark Colony unit frames as:

```
raw_frame = frame_loop_index + direction_index * 8
```

That makes a moving unit rotate around its axis instead of walking. The generated
Dark Colony `info.c` must use the frame-major formula instead.

### Bulk-Loading Plan (FIN-driven, no hardcoded indices)

1. Read `data/DCOLONY/ANIM.DAT` to enumerate all `.fin` stems.
2. For each stem, parse the corresponding `.FIN` label table (first `valid_count`
   entries) and the trailing 22-byte command table.
3. Group labels by `ACTION` substring.  For `MOVE`, `FIREA`, `FIREB`, etc.,
   dereference label command ranges to raw SPR frames, keeping layer-1 commands
   for the unit body and preserving overlays for later effect work.
4. For single-frame actions (`STAND`, `HITB`, `SHUF`), length=1, stride=1.
5. For directional death strips (`DIEA`, `DIEB`, `DIEC`), note they only cover 4
   of the 8 directions in `GRAY`/`TRSC`; map remaining directions to nearest.
6. The companion sprite refs in the FIN header (`GRAY`, `SMSP`, `BLOO`, etc.)
   list effect/overlay sprites that the original engine loaded in parallel;
   they are not needed for the unit's main `SpriteSheet`.

## Dark Reign

- OpenDR:
  https://github.com/drogoganor/OpenDR
  - Primary Dark Reign reference for map import, `.TIL` frame layout, generated
    transition masks, resource handling, and OpenRA-style plugin structure.

- drExplorer:
  https://github.com/btigi/drExplorer
  - Useful reference for Dark Reign FTG archives.

Local game-data files that have already been useful:

- `data/REIGN/dark/deftxt/*.TXT` for unit, building, overlay, and animation
  definitions.
- `data/REIGN/dark/scenario/**/*.MAP` and `*.SCN` for terrain and placed
  objects.
- `data/REIGN/dark/graphics/**/*.TIL`, `*.PAL`, and `SPRITES.FTG` for terrain,
  palettes, and sprites.
- OpenDR sequence YAML and `DrSprLoader.cs` show the OpenRA-style unit
  animation model: each sequence has a `Start`, `Facings`, `Length`, and
  `Tick`; rendering chooses a facing frame offset from the unit direction and
  then advances within that sequence for walking/firing.
