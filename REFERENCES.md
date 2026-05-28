# Reverse Engineering References

Keep these links handy when touching loaders, tile animation, map objects, or
plugin-specific behavior.

## Dark Colony

- DarkColony.pl downloads:
  https://www.darkcolony.pl/downloads.php?cat_id=2
  - Community downloads and historical Dark Colony material. Check here when
    looking for tools, map/editor notes, or the Polish community project files.

- endotermic/Dark-Colony:
  https://github.com/endotermic/Dark-Colony
  - Open-source Dark Colony reference lead. Use it when validating `.MAP`,
    `.BTS`, `.SPR`, object placement, palette cycling, and faction/unit logic.

Local game-data files that have already been useful:

- `data/DCOLONY/GAMESTAT/GAMESTAT.TXT` for unit IDs, names, health, and weapon
  IDs.
- `data/DCOLONY/GAMESTAT/WEAPSTAT.TXT` for weapon range, damage, and rate of
  fire. Trooper/Grey weapon rows use range `4`, damage `100`, and rate `15`.
- `data/DCOLONY/GAMESTAT/DEPEND.TXT` for unit/building dependency names.
- `data/DCOLONY/SCENARIO/*.MAP`, `*.SCN`, and `*.BTS` for maps, starting
  objects, tilesets, and water palette bands.
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
              u16LE width, u16LE height, u16LE unk, u16LE unk
after descriptors: pixel data
  if compressed: per-frame [u32LE chunk_size][RLE data]
    RLE: signed byte cmd; cmd<0 → skip (-cmd) pixels; cmd≥0 → copy (cmd+1) pixels
  if uncompressed: raw indexed pixels, row-major, width×height per frame
```

### Dark Colony FIN Binary Format

`.FIN` files map human-readable animation label names to frame ranges within the
corresponding `.SPR`.  `ANIM.DAT` lists all `.fin` stems; the matching `.SPR` is
in `data/DCOLONY/SPRITES/`.

```
offset  size  field
0x00    u16LE magic          always 0x001d (29); treat as version/format ID
0x02    u16LE total_records  size of the label table (including NONAME padding)
0x04    u16LE valid_count    number of named (non-NONAME) labels
0x06    u16LE ref_count      number of companion sprite references
0x08    ref_count×8  sprite_refs   null-padded 8-byte ASCII names of companion
                                   SPR stems loaded alongside this animation
0x08+ref_count×8  total_records×20  label table:
              bytes 0..15  name    null-padded ASCII label (e.g. "GRAYMOVE0")
              bytes 16..17 start   u16LE first frame index (inclusive)
              bytes 18..19 end     u16LE last frame index (inclusive)
  Entries beyond valid_count are NONAME padding; ignore them.
after label table: auxiliary data (hitbox / attachment-point records per frame)
  Not needed for sprite loading; structure is not fully reversed.
```

### FIN Label Naming Convention

Labels follow the pattern `<STEM><ACTION><DIRECTION>` where:

- `STEM` matches the FIN/SPR filename stem (e.g. `GRAY`, `TRSC`, `TROOPER1`).
- `ACTION` is one of: `STAND`, `SHUF` (idle shuffle), `MOVE`, `ANT` (anticipation
  pose), `FIREA`, `FIREB` (two fire phases), `HITB` (hit reaction), `MOVEC`
  (alternate move), `DIEA`, `DIEB`, `DIEC` (death strips per quadrant), `BLOOD*`
  (blood effects), `FUNK` (misc), etc.
- `DIRECTION` is a clock-face code in `{0, 2, 4, 6, 8, 10, 12, 14}` mapping to
  compass octants.  The engine's `direction_code_from_vector()` produces these
  same values: `East=0, SE=2, S=4, SW=6, W=8, NW=10, N=12, NE=14`.

The direction code set and ordering **differ by sprite**:

| Sprite       | Direction order in FIN label table          | Frame layout |
|--------------|---------------------------------------------|--------------|
| `GRAY.SPR`   | `0,14,12,10,8,6,4,2` (East-first, CCW)      | direction-major |
| `TRSC.SPR`   | `0,14,12,10,8,6,4,2` (East-first, CCW)      | direction-major |
| `TROOPER1.SPR` | `6,4,2,0,14,12,10,8` (SW-first, CCW)       | direction-major |

**Direction-major layout**: all frames for one direction are contiguous, then the
next direction follows.  Within a direction the frames advance as a normal
animation strip (stride = 1).  Example for `GRAYMOVE`:

```
GRAYMOVE0  → frames 16..22  (7 frames, direction East=0)
GRAYMOVE14 → frames 23..30  (8 frames, direction NE=14)
GRAYMOVE12 → frames 31..38  (direction N=12)
...
GRAYMOVE2  → frames 71..77  (direction SE=2)
```

To build a `SpriteSequence` from FIN labels for a given action:

```c
// Read one direction label at a time; use its start frame and direction code.
// stride = 1 (frames advance within a direction block, not across directions).
// length = min(end - start + 1) across all direction labels for that action,
//          or use the per-direction length if they are uniform.
//
// Example for GRAYMOVE (8 directions):
int frame_starts[8] = { 16, 23, 31, 39, 47, 55, 63, 71 };
int direction_codes[8] = {  0, 14, 12, 10,  8,  6,  4,  2 };
// sprite_sheet_add_sequence(out, "run", 8, 7, 120, frame_starts, direction_codes);
// No extra stride call needed (default stride=1 is correct).
```

### Known Bugs in Current Hardcoded Sequences

The hardcoded arrays in `load_dark_colony_sprite()` (src/main.c ~line 1821) use
`stride=8` with consecutive starting frame numbers (e.g. `gray_run={16..23}`),
which incorrectly cross direction boundaries during animation playback.  The
correct approach reads each direction label's `start` field from the FIN and uses
`stride=1`:

| Sprite | Sequence | Current (wrong) start frames | Correct start frames (from FIN) |
|--------|----------|------------------------------|----------------------------------|
| GRAY   | run      | `{16,17,18,19,20,21,22,23}` stride=8 | `{16,23,31,39,47,55,63,71}` stride=1 |
| GRAY   | shoot    | `{80,81,82,83,84,85,86,87}` stride=8 | `{78,86,94,102,110,118,126,134}` stride=1 |
| TRSC   | run      | `{16,17,18,19,20,21,22,23}` stride=8 | `{16,24,32,40,48,56,64,72}` stride=1 |
| TRSC   | shoot    | `{80,81,82,83,84,85,86,87}` stride=8 | `{88,96,104,112,120,128,136,144}` stride=1 |
| TROOPER1 | run    | `{8,9,10,11,12,13,14,15}` stride=8 | `{8,10,12,14,16,18,20,22}` stride=1 |

`TROOPER1` also uses a different direction-code set (`dc_codes_trooper={6,8,10,12,14,0,2,4}`)
but the correct set from the FIN label order is `{6,4,2,0,14,12,10,8}`.

### Bulk-Loading Plan (FIN-driven, no hardcoded indices)

1. Read `data/DCOLONY/ANIM.DAT` to enumerate all `.fin` stems.
2. For each stem, parse the corresponding `.FIN` label table (first `valid_count`
   entries; skip `NONAME`).
3. Group labels by `ACTION` substring.  For `MOVE`, `FIREA`, `FIREB`, etc. that
   have 8 direction variants, collect the 8 `(direction_code, start, length)`
   tuples and call `sprite_sheet_add_sequence` with those arrays and `stride=1`.
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
