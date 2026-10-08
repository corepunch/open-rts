# StarCraft classic format reference

This document records the native formats examined while adding the StarCraft
catalog, checked against the local retail files and pinned reference sources
on 2026-10-08. It is an implementation reference, not a complete StarCraft
specification. Implementation descriptions (including “current” below) refer
to the initial catalog baseline, commit `a23c42d`; concurrent mission-loader
work is outside this audit and should add its own verified findings. All offsets below are decimal unless prefixed `0x`.
Multibyte integers are little-endian; `u8/u16/u32` and `s8` denote unsigned
and signed fixed-width values. File offsets are absolute unless stated otherwise.

Read alongside [investigation findings](SC_EXE_FINDINGS.md), which contain
input fingerprints, extraction results, disproven assumptions, and fidelity
limits, and [source provenance](../REFERENCES.md#starcraft--stargus-2026-10-08).

Evidence labels used here:

- **Confirmed locally:** inspected bytes and/or exercised by our C decoder/tests.
- **Reference-defined:** described by the pinned Stargus/PyMS sources; not every
  field has been independently verified in the retail executable.
- **Implementation:** current plugin behavior, which may be a deliberate subset.
- **Unknown:** not established; no executable address or behavior is invented.

## 1. Containers, names, and inventory

The local input chain is:

```text
StarCraft.iso
  -> disc/INSTALL.EXE                     installer containing an MPQ
     -> install/files/stardat.mpq         game MPQ
        -> native/arr, rez, unit, ...     unchanged member bytes
     -> install/files/font/*.fnt          fonts outside the game MPQ
     -> install/files/starcraft.exe       original executable, retained
```

ISO extraction uses `bsdtar`. MPQ extraction uses StormLib's C API through
`tools/sc_import/main.c`; the runtime does not parse MPQ archives. MPQ hash
entries do not provide a complete directory of original filenames on their
own: Stargus's `mpqlist.txt` supplies known names. Installer lookup additionally
tries the `files\` prefix. Therefore “all enumerated entries extracted” is
not a claim that every unknown name was recovered. StormLib's synthetic names
are retained when used. Paths are normalized to lowercase `/`; data bytes
are not rewritten. No original EXE is executed during extraction.

Confirmed local counts: 782 installer output files and 2,897 game output files.
Use `rg --files --hidden --no-ignore` when recounting: ordinary `rg --files`
hides eleven installer files under the current ignore rules. The game output
includes 782 GRPs, 477 SMKs, 368 PCXs, 966 WAVs, 79 BINs, 20 TBLs and 11 DATs.
These are archive inventory counts; the renderer currently uses 148 unique
unit GRPs, not every graphic in the archive.

| Family/path | Role | Initial implementation |
| --- | --- | --- |
| `arr/*.dat` | Parallel unit/image/portrait/rule columns | Selected image and portrait chains; offline unit balance import |
| `arr/*.tbl`, `rez/stat_txt.tbl` | String and filename tables | Image/portrait filenames and unit names |
| `unit/**/*.grp` | Unit images, effects, command icons, wireframes | All 228 unit slots; command and wireframe subsets |
| `unit/**/*.lo?` | Per-frame overlay locations | Special-overlay LOL positions for turrets |
| `scripts/iscript.bin` | Image animation bytecode | Bounded visual-path compiler |
| `rez/*.bin` | Dialog/control layout resources | Main menu, pause menu and catalog HUD records |
| `glue/**/*.pcx`, `game/*.pcx`, `dlgs/*.grp` | UI art and palette ramps | Original menu and Terran console |
| `portrait/**/*.smk`, `glue/mainmenu/*.smk` | Portrait and menu animation | Video and per-frame palettes |
| `tileset/*.cv5/vx4/vr4/wpe` | Terrain composition and palette | Badlands |
| `tileset/*.vf4`, `tileset/*/dddata.bin` | Additional terrain metadata | Retained, not decoded by this plugin |
| Other BINs, TRGs, GOTs, maps and WAVs | AI/map/rule/sound resources | Retained; not evidence of implemented gameplay |

The extension `.bin` does not identify a single format. Never run a dialog
parser on IScript, AI bytecode, or terrain `dddata.bin` just because the suffix
matches. SCM/SCX/CHK mission parsing was not investigated in this task; the
catalog is constructed in C rather than loaded from a retail mission.

## 2. TBL string tables

Confirmed lookup used by `tbl_string` in `games/starcraft/w_assets.c`:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u16 | Number of string entries, N |
| 2 | N × u16 | Offsets of NUL-terminated strings |
| Offset from table | bytes | String data; not assumed to be UTF-8 |

Native filename references are **one-based**: reference `k` reads the u16
at file offset `2*k`; zero means no entry. Thus reference 1 reads the first
offset at byte 2. Validate both the offset span and a terminating NUL.
Multiple references can share strings; do not require distinct offsets.

For unit slot `i`, the importer reads `stat_txt.tbl` entry `i+1`. It keeps the
first NUL-terminated name, removes bytes below 32, escapes quotes/backslashes,
and writes non-ASCII bytes as C octal escapes. This is sufficient for the
local data, not a complete localized text/markup implementation.

## 3. DAT: column arrays, not interleaved records

DAT columns are stored back-to-back. A per-entry size such as 15 for flingy
or 38 for images is the sum of field widths, **not** a disk record stride.
For a column of width W beginning at B, entry i is at `B + W*i`.

### units.dat

The importer accepts classic 19,192-byte and expanded 19,876-byte layouts.
The classic file was verified locally. The expanded layout is described by
Stargus and supported by the importer, but no Brood War asset set was tested.
Both have 228 slots. Width-zero columns below are absent from classic data
and have width one in the expanded layout. Their classic offsets mark the
insertion boundary, not a readable field. Short columns have their own index
ranges: infestation/addon positions cover native IDs 106..201; ready/annoyed/
acknowledgement sounds cover IDs 0..105. Do not index those arrays by an
unadjusted arbitrary unit ID.

Field names come from Stargus's `src/kaitai/units_dat.ksy`. Offsets are computed
from the widths/counts used by our C importer and checked to end at the two
accepted file lengths. Names for unused fields are reference-defined.

| Column | Field | Classic width | Entries | Classic start | Expanded start |
| --- | --- | --- | --- | --- | --- |
| 0 | `flingy` | 1 | 228 | 0 | 0 |
| 1 | `subunit1` | 2 | 228 | 228 | 228 |
| 2 | `subunit2` | 2 | 228 | 684 | 684 |
| 3 | `infestation` | 2 | 96 | 1140 | 1140 |
| 4 | `construction_animation` | 4 | 228 | 1332 | 1332 |
| 5 | `unit_direction` | 1 | 228 | 2244 | 2244 |
| 6 | `shield_enable` | 1 | 228 | 2472 | 2472 |
| 7 | `shield_amount` | 2 | 228 | 2700 | 2700 |
| 8 | `hit_points` | 4 | 228 | 3156 | 3156 |
| 9 | `elevation_level` | 1 | 228 | 4068 | 4068 |
| 10 | `unknown` | 1 | 228 | 4296 | 4296 |
| 11 | `rank` | 1 | 228 | 4524 | 4524 |
| 12 | `ai_computer_idle` | 1 | 228 | 4752 | 4752 |
| 13 | `ai_human_idle` | 1 | 228 | 4980 | 4980 |
| 14 | `ai_return_to_idle` | 1 | 228 | 5208 | 5208 |
| 15 | `ai_attack_unit` | 1 | 228 | 5436 | 5436 |
| 16 | `ai_attack_move` | 1 | 228 | 5664 | 5664 |
| 17 | `ground_weapon` | 1 | 228 | 5892 | 5892 |
| 18 | `max_ground_hits` | 0 | 228 | 6120 | 6120 |
| 19 | `air_weapon` | 1 | 228 | 6120 | 6348 |
| 20 | `max_air_hits` | 0 | 228 | 6348 | 6576 |
| 21 | `ai_internal` | 1 | 228 | 6348 | 6804 |
| 22 | `special_ability_flags` | 4 | 228 | 6576 | 7032 |
| 23 | `target_acquisition_range` | 1 | 228 | 7488 | 7944 |
| 24 | `sight_range` | 1 | 228 | 7716 | 8172 |
| 25 | `armor_upgrade` | 1 | 228 | 7944 | 8400 |
| 26 | `unit_size` | 1 | 228 | 8172 | 8628 |
| 27 | `armor` | 1 | 228 | 8400 | 8856 |
| 28 | `right_click_action` | 1 | 228 | 8628 | 9084 |
| 29 | `ready_sound` | 2 | 106 | 8856 | 9312 |
| 30 | `what_sound_start` | 2 | 228 | 9068 | 9524 |
| 31 | `what_sound_end` | 2 | 228 | 9524 | 9980 |
| 32 | `piss_sound_start` | 2 | 106 | 9980 | 10436 |
| 33 | `piss_sound_end` | 2 | 106 | 10192 | 10648 |
| 34 | `yes_sound_start` | 2 | 106 | 10404 | 10860 |
| 35 | `yes_sound_end` | 2 | 106 | 10616 | 11072 |
| 36 | `staredit_placement_box` | 4 | 228 | 10828 | 11284 |
| 37 | `addon_position` | 4 | 96 | 11740 | 12196 |
| 38 | `unit_dimension` | 8 | 228 | 12124 | 12580 |
| 39 | `portrait` | 2 | 228 | 13948 | 14404 |
| 40 | `mineral_cost` | 2 | 228 | 14404 | 14860 |
| 41 | `vespene_cost` | 2 | 228 | 14860 | 15316 |
| 42 | `build_time` | 2 | 228 | 15316 | 15772 |
| 43 | `requirements` | 2 | 228 | 15772 | 16228 |
| 44 | `staredit_group_flags` | 1 | 228 | 16228 | 16684 |
| 45 | `supply_provided` | 1 | 228 | 16456 | 16912 |
| 46 | `supply_required` | 1 | 228 | 16684 | 17140 |
| 47 | `space_required` | 1 | 228 | 16912 | 17368 |
| 48 | `space_provided` | 1 | 228 | 17140 | 17596 |
| 49 | `build_score` | 2 | 228 | 17368 | 17824 |
| 50 | `destroy_score` | 2 | 228 | 17824 | 18280 |
| 51 | `unit_map_string` | 2 | 228 | 18280 | 18736 |
| 52 | `broodwar_flag` | 0 | 228 | 18736 | 19192 |
| 53 | `staredit_availability_flags` | 2 | 228 | 18736 | 19420 |

The committed `units.inc` contains name, integer HP, special flags, placement
width/height, sight, right-click action, editor group bits, mineral/gas costs,
and portrait ID. HP is stored in DAT as a u32 with eight fractional bits;
our importer shifts it right by eight. Placement is two u16 pixel dimensions;
unit dimensions are a separate eight-byte field and are not substituted for
placement dimensions. The current field named `race` stores editor group bits,
not a normalized race enum; `orders` stores the default right-click action,
not the complete orders.dat state machine. These naming differences matter
when extending the plugin.

Balance remains committed C literals under this repo's architecture. Runtime
DAT reads resolve artwork; editing retail HP bytes alone will not change the
compiled catalog's balance. Re-run `sc_catalog` and review the literal diff.

### flingy.dat

Confirmed size 2,760 bytes, N=184. Reference-defined columns:

| Column | Start | Width | Usage |
| --- | --- | --- | --- |
| sprite | 0 | 2 | Runtime native sprite ID |
| speed | 2N | 4 | Not used for catalog movement |
| acceleration | 6N | 2 | Not simulated |
| halt_distance | 8N | 4 | Not simulated |
| turn_radius | 12N | 1 | Not simulated |
| unused | 13N | 1 | Unknown purpose |
| movement_control | 14N | 1 | Not simulated |

### sprites.dat

Confirmed classic size: 2,081 bytes. The image column contains 386 u16 indices
at offsets 0..771. The remaining columns were not needed or decoded. The
expanded-table formula `130 + (size-520)/7` gives 353 for this file and fails
for door references near the end of the classic image array. Keep the verified
classic case separate; do not silently apply the expanded schema to it.

### images.dat

Confirmed size 28,690 bytes, N=755. Fields and column starts:

| Field | Start | Width | Current use |
| --- | --- | --- | --- |
| grp | 0 | 4 | One-based images.tbl filename |
| gfx_turns | 4N | 1 | Directional grouping |
| clickable | 5N | 1 | Not interpreted |
| use_full_iscript | 6N | 1 | Not interpreted |
| draw_if_cloaked | 7N | 1 | Not interpreted |
| draw_function | 8N | 1 | Not implemented |
| remapping | 9N | 1 | Not implemented generally |
| iscript | 10N | 4 | Animation script ID |
| shield_overlay | 14N | 4 | Not implemented |
| attack_overlay | 18N | 4 | Not implemented |
| damage_overlay | 22N | 4 | Not implemented |
| special_overlay | 26N | 4 | One-based images.tbl LOL path |
| landing_dust_overlay | 30N | 4 | Not implemented |
| lift_off_dust_overlay | 34N | 4 | Not implemented |

The complete runtime image chain for native unit slot i is:

```text
units.dat[i]                                  -> flingy ID f
u16(flingy.dat + 2*f)                          -> sprite ID s
u16(sprites.dat + 2*s)                         -> image ID m
u32(images.dat + 4*m)                         -> TBL reference k
images.tbl[k]                                 -> relative filename
"unit/" + filename                            -> GRP
images.dat[4*N + m]                            -> turns flag
u32(images.dat + 10*N + 4*m)                  -> IScript ID
```

A spot-check during discovery found Marine 0 -> flingy 78 -> sprite 235 ->
image 239, and Goliath 3 -> 75 -> 232 -> 234. These are diagnostic examples,
not special cases in the loader. Different units sharing an image ID alias one
owned sprite sheet; each keeps its own type/state references. Native unit i
maps to engine type `i+1` because engine type/state zero is reserved.

### portdata.dat and portraits

Confirmed size 1,080 bytes, N=90. Reference-defined parallel arrays are idle
video references (u32 at 0), talking references (u32 at 4N), idle variation
(u8 at 8N), talking variation (u8 at 9N), and two unknown byte arrays at 10N
and 11N. Idle references index portdata.tbl one-based.

The current path is `portrait/<TBL string>0.smk`. For example the string
`tmarine\TMaFid0` becomes `portrait/tmarine/tmafid00.smk`. The extra zero
selects the first numbered variant; do not remove the string's existing zero.
Out-of-range portrait IDs are rejected. Only the selected unit's idle movie
is resident; it is freed when the portrait ID changes. Talking/variant
selection and the reference-defined variation probabilities are not simulated.

Other DAT files are retained but were not interpreted: mapdata (128 bytes),
orders (3,872), sfxdata (8,712), techdata (432), upgrades (920), weapons (4,200).
These sizes are local observations, not accepted-format specifications.

## 4. GRP indexed images

Confirmed structure:

| Header offset | Type | Meaning |
| --- | --- | --- |
| 0 | u16 | Physical frame count |
| 2 | u16 | Nominal canvas width |
| 4 | u16 | Nominal canvas height |
| 6 | count × 8 bytes | Frame descriptors |

Each descriptor is `(x_offset:u8, y_offset:u8, width:u8, height:u8,
image_offset:u32)`. Offsets place the cropped rectangle in the nominal
canvas. A crop's engine ground point is
`(canvas_width/2 - x_offset, canvas_height/2 - y_offset)`.
Do not center the cropped pixels independently or add compensating offsets.

For compressed frames, `image_offset` begins a `height × u16` row-offset
array. Each row offset is **relative to image_offset**. Row packets are:

| Control byte | Operation |
| --- | --- |
| bit 7 set | Skip `control & 0x7f` transparent pixels |
| bit 7 clear, bit 6 set | Repeat following palette index `control & 0x3f` times |
| both high bits clear | Copy `control` literal palette indices |

Rows stop after the declared crop width. The implementation rejects zero
lengths, overshoots, truncated literals/repeats, invalid crop extents and
out-of-file references. Empty frames retain a logical cell with zero visible
bounds. The engine represents transparency with index zero; native skip runs
carry the transparency information. This is not proof that palette index zero
is universally transparent in every StarCraft format/rendering mode.

Some GRPs are raw indexed rectangles with no row-offset/RLE layer. Following
Stargus, the loader compares the first frame-data offset plus the sum of
`width*height` for unique data offsets against total file length. Equality
selects raw decoding. This is a layout discriminator, not a format tag;
retain bounds checks and do not assume it identifies arbitrary malformed data.

If images.dat enables turns, consecutive groups of 17 physical frames cover
north through south clockwise. For shared renderer direction d=0..31:

```text
native_direction = (32-d) % 32
slot = native_direction <= 16 ? native_direction : 32-native_direction
physical_frame = logical_frame*17 + slot
mirror_x = native_direction > 16
```

North uses slot 0, south 16, east/west share slot 8 with one mirrored. A partial
last set retains only authored frames; missing directions are not fabricated.
Nondirectional GRPs use one physical frame per logical frame. Keeping all
physical frames is independent of how many are used by the current animation
compiler.

## 5. LO overlay-location files

The parsed LOL structure has u32 frame count at 0, u32 locations-per-frame at
4, and a u32 absolute data offset per frame from byte 8. Each frame's data
contains that many `(x:s8, y:s8)` pairs. Signed interpretation is essential.
Other `.lo?` files are retained; their semantic roles were not all investigated.

Turret composition follows units.subunit1, verifies the subunit flag `0x10`,
and resolves the body's images.special_overlay through images.tbl. It reads
the first location for each physical body frame. The child layer offset is
`body_pivot - turret_pivot + authored_location`; mirrored pivots are adjusted
by their crop width and a mirrored body's location X is negated. No world Y
flip is involved. The current turret uses its idle frame and the body's facing;
independent turret aiming and generic IScript overlays are not implemented.

## 6. IScript animation BIN

Reference-defined header selection, also used by the implementation: if the
u32 at byte 2 is zero, byte 0's u16 points to the entry table; otherwise the
entry table starts at zero. Entries are `(iscript_id:u16, offset:u16)` until
`FFFF 0000`. A script header starts with `SCPE`, one type byte, three padding
bytes, and an array of u16 absolute animation entry offsets. Zero means no
entry. Reference-defined type/entry counts:

| Header types | Animation entries |
| --- | --- |
| 0, 1 | 2 |
| 2 | 4 |
| 12, 13 | 14 |
| 14, 15 | 15 |
| 20, 21 | 21 |
| 23 | 23 |
| 24 | 25 |
| 26, 27, 28, 29 | 27 |

Do not extrapolate meanings for unlisted types. Our compact reader currently
uses broader numeric ranges than the reference's explicit list; malformed or
unfamiliar types need a stricter audit before broader compatibility is claimed.

The catalog tries animation 23 (StarEditInit), falls back to 0 (Init), and
separately compiles animation 11 (Walking). StarEditInit was historically
called InitTurret in some references; it is not a command to create turrets.

| Opcode | Reference meaning | Current compiler behavior |
| --- | --- | --- |
| 0x00 | playfram, u16 frame | Sets frame; divides by 17 for directional sheets |
| 0x01 | playframtile, u16 frame | Treated like playfram; tileset-specific semantics not modeled |
| 0x05 | wait, u8 ticks | Emits a shared timed state |
| 0x06 | waitrand, two u8 bounds | Uses the first bound, not a random value |
| 0x07 | goto, u16 offset | Follows the jump |
| 0x16 | end | Stops this visual path |
| 0x30 | ignorerest | Stops this visual path |
| 0x35 | call, u16 offset | Pushes return address and follows target |
| 0x36 | return | Pops return address |
| Other opcodes | Sound, movement, effects, conditions, etc. | Skipped using operand-length table; not simulated |

Conditional branches are not evaluated. Revisited wait instructions close the
compiled loop. Bounds are 2,048 instructions per path, 128 recorded waits,
16 call frames and 8,192 shared state slots. A missing path keeps its default
first-frame state. Tick conversion currently assumes native 24 Hz and computes
`(max(wait,1)*30 + 12)/24`; that conversion is an implementation timing model,
not an independently measured retail timing trace. The log's “1,322 visual
states” is the next-free state index, including reserved slot zero in its span.

**Known audit finding:** the current operand-length table is not a complete
validated VM description. Comparison with both references exposes differences,
including 0x3f (`liftoffcondjmp`: u16 target, current length zero), 0x41
(`orderdone`: u8, current zero), and 0x42 (`grdsprol`: u16 plus two bytes,
current zero). Encountering these can misalign subsequent decoding. Existing
catalog tests do not prove every instruction path is correct. This documentation
records the issue; it does not change runtime code or claim a full VM fix.

## 7. Terrain composition

Confirmed Badlands inputs: CV5 86,580 bytes (1,665 groups), VX4 155,008 bytes
(4,844 megatiles), VR4 1,679,808 bytes (26,247 minitiles), VF4 155,008 bytes,
and WPE 1,024 bytes. VF4 is retained but not read by the catalog loader.

- WPE: 256 entries of red, green, blue, unused byte. The fourth byte is not
  interpreted as alpha.
- CV5: 52-byte group records. Sixteen u16 megatile IDs begin at byte 20.
  The first 20 metadata bytes are not interpreted by the current loader.
- VX4: each megatile is 16 u16 references, arranged 4×4. `reference >> 1`
  is a VR4 minitile ID; bit zero reflects that minitile horizontally.
- VR4: each minitile is 64 palette indices, 8×8 row-major pixels.

For minitile slot m, local pixel (x,y) writes to megatile pixel
`((m%4)*8+x, (m/4)*8+y)`; the source x is `7-x` when reflected. Resulting
megatiles are 32×32. The shared lookup index is `group*16+variant`, not a
VX4 ID directly. The catalog uses lookup indices 32..47 (group 2).

Walkability, elevation, doodad metadata, creep, map decoding and other tilesets
have not been connected to this implementation. Never infer retail pathing
from the sandbox's mostly empty blocking grid.

## 8. PCX, font, and color formats

### PCX

The shared C reader handles the encountered 128-byte PCX header, 8-bit single
plane, RLE encoding. Width/height are inclusive max-minus-min plus one.
Header byte 65 must specify one plane; u16 at 66 is padded bytes per scanline.
Packets with high bits `11` repeat their following byte `control & 0x3f`
times; other bytes are literal pixels. Consume row padding but do not draw it.
The last 769 bytes are marker `0x0c` and 256 RGB triples. Unsupported encodings
are rejected; this is not a general-purpose PCX implementation.

A PCX can be a picture or a palette lookup image. For lookup images, use the
pixel indices to select colors from the embedded palette; copying the palette
alone does not apply the lookup.

### FONT/FNT

Header: `FONT` at bytes 0..3, low/high inclusive character codes at 4/5,
maximum glyph width/height at 6/7; `(high-low+1)` u32 glyph offsets follow.
Offset zero is an empty glyph. Glyph data begins width, height, x offset,
y offset (one byte each), followed by encoded ink positions.

For each byte: advance by `byte >> 3`, then write `byte & 7` at the current
position and advance one. Interpret this position in the cropped glyph's
row-major rectangle and add its x/y origin. Ink zero is transparent. The
engine reserves index zero and stores nonzero inks as `ink+1`, then builds
palette maps for the authored ramps. The current advance width is
`x_offset + width + 1`; this is the adapter's spacing rule, not a traced EXE
kerning routine. `font10`, `font14`, `font16` and `font16x` are used for UI.

### Palette roles

| Asset | Confirmed local dimensions | Mapping |
| --- | --- | --- |
| `game/tunit.pcx` | 128×1 | Eight-color player ramps; plugin uses first eight players |
| `game/twire.pcx` | 24×1 | Wireframe health/shield color lookup |
| `unit/cmdbtns/ticon.pcx` | 96×1 | Six 16-color command-icon ramps; initial UI uses first |
| `glue/palmm/tfont.pcx` | Loaded at runtime | Main-menu text ramps |
| `game/tfontgam.pcx` | Loaded at runtime | In-game text ramps |

Player p remaps GRP indices `8+k` to `tunit[p*8+k]`, k=0..7. The remapped
indices are rendered using the world palette. The UI uses tunit's embedded
palette for console/widget art, ticon's first 16 **pixel-selected colors** for
command icons, and twire's full-health mappings for wireframes. See the
findings journal for the exact wireframe index table and its primary source.
Damaged-section colors, generic images.dat remapping, translucency and
terrain-dependent effect lookup tables are not implemented.

## 9. Dialog BIN and SMK descriptors

Classic control records are 86 bytes, rooted at file offset zero. This table
accounts for the full record; fields marked unknown retain PyMS's uncertainty.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u32 | Next sibling file offset |
| 4, 6 | u16, u16 | x1, y1 |
| 8, 10 | u16, u16 | x2, y2; adapter uses width/height instead |
| 12, 14 | u16, u16 | Width, height |
| 16 | u32 | Unknown1 |
| 20 | u32 | String/path file offset, zero for none |
| 24 | u32 | Flags |
| 28 | u32 | Unknown2 |
| 32 | u16 | Control identifier |
| 34 | u32 | Type |
| 38, 42, 46, 50 | four u32 | Unknown3..6 |
| 54, 56, 58, 60 | four u16 | Responsive x1,y1,x2,y2 |
| 62 | u32 | Unknown7 |
| 66 | u32 | Root child pointer or control SMK descriptor pointer |
| 70, 72 | u16, u16 | Text x/y offsets |
| 74, 76 | u16, u16 | Responsive width/height |
| 78, 82 | u32, u32 | Unknown8/9 |

Child coordinates are relative to the root dialog origin. Do not add the
root origin to a file pointer. Sibling offset zero terminates the list.
The adapter bounds traversal to 64 controls and four SMK descriptors per
control, rejects invalid spans/types/zero extents, and limits copied strings
to 127 bytes. These are engine limits, not proven retail maxima. Remastered's
88-byte records are reference-defined but unsupported here.

Reference-defined types: 0 dialog; 1 default button; 2 button; 3 option button;
4 checkbox; 5 image; 6 slider; 7 unknown; 8 textbox; 9/10/11 left/center/right
labels; 12 listbox; 13 combobox; 14 highlighted/animated button. The initial
adapter implements buttons/images/labels for the loaded screens, not every
control behavior implied by this list.

Relevant reference-defined flags:

| Mask | Meaning |
| --- | --- |
| 0x2, 0x8, 0x10 | Disabled, visible, responsive |
| 0x40 | Cancel button |
| 0x100, 0x200 | Virtual hotkey / character hotkey prefix |
| 0x400, 0x800, 0x4000, 0x10000 | Fonts 10, 16, 16x, 14 |
| 0x2000, 0x40000 | Transparency / translucency |
| 0x80000, 0x100000 | Default button / on top |
| 0x200000, 0x400000, 0x800000 | Center / right / second center alignment flag |
| 0x1000000, 0x2000000, 0x4000000 | Top / middle / bottom alignment |
| 0x80, 0x40000000 | Suppress hover / click sound |

Not all flags are acted upon. In particular exact responsive bounds,
translucency, virtual-key translation and retail text-color behavior remain
incomplete. The loader records byte-4 to byte-1 highlight spans; the adapter
retains metadata but does not implement the full original color interpreter.

A linked SMK descriptor occupies 30 bytes: next descriptor u32 at 0, flags
u16 at 4, unknown u32 at 6, filename offset u32 at 10, unknown u32 at 14,
x/y u16 at 18/20, and unknown u32 values at 22/26. Known flags include fade-in
1, dark 2, loop 4 and hover-only 8. Only loop/hover behavior is applied.
Descriptors are movie overlays at offsets within the control. Normal overlays
must remain visible underneath hover overlays.

### Native screens actually mapped

| File | Native root/role | Adapter action |
| --- | --- | --- |
| `glumain.bin` | 640×480, ten child controls | Original menu; Single Player opens catalog |
| `gamemenu.bin` | (184,32), 264×288 | Pause menu with original button geometry |
| `minimap.bin` | (0,315), 138×165 | Minimap at (6,348), 128×128 |
| `statdata.bin` | (138,388), 270×92 | Selected name, HP and wireframe |
| `statbtnt.bin` | (496,354), 144×126 | Nine native slots; Move 228 and Stop 229 icons |
| `statport.bin` | (408,408), 88×72 | Portrait at (413,410), 60×56 |
| `stat_f10.bin` | (408,388), 88×20 | Menu button at (416,388), 64×20 |
| `statres.bin`, `statlb.bin` | Resource/leaderboard layouts | Inspected/retained; no full resource UI |

Runtime content such as selection labels is absent/hidden in many records
until executable logic fills it. Do not conclude that a hidden label is unused.
The menu button's native virtual-key prefix is explicitly overridden to SDL F10.

Native menu art includes `glue/palmm/backgnd.pcx`, `glue/mainmenu/etail.pcx`,
and the single/multi/editor/exit SMK pairs. Panel frames use `dlgs/tile.grp`
indices 0..8 in 3×3 order. `dlgs/terran.grp` has authored button pieces:
normal center-style left/middle/right 115..117, pressed 118..120, disabled
112..114. References that deduplicate GRP frames can report different indices;
our raw native frame numbering is preserved.

## 10. SMK decoding and playback scope

`games/starcraft/w_smk.c` delegates the compressed bitstream to the pinned
libsmacker C library. It obtains dimensions, frame count and microseconds per
frame, enables video, and copies both indexed pixels and a 256-color palette
for each frame. This is not an independent reverse engineering of Smacker's
compression or audio formats.

Limits: at most 1,024 frames and 640×480 pixels per frame; decoded frames are
resident in memory. Frame milliseconds are `max(1, floor(microseconds/1000))`.
Playback uses SDL ticks; hover does not restart a movie clock. Menu index zero
is transparent in the adapter; portraits are drawn opaque. Audio is not
enabled. Fade/darken behavior and retail startup/hover timing are unverified.

## 11. Coordinates, verification, and next investigations

All native world/UI coordinates stay top-left, X right, Y down. The plugin
builds with `RTS_WORLD_Y_UP=0`, matching Warcraft II. Native pixels convert
to the shared 32-pixel grid at the engine boundary; no asset, world, mouse or
pathfinding Y coordinate is independently flipped. Renderer-facing direction
lookup and horizontal GRP mirroring are separate from world coordinates.
No framebuffer-flip change was needed.

The catalog's 128×128 map, regular spacing, fully revealed terrain, shared
movement speed, common Terran console and restricted command panel are authored
sandbox choices. They must not be cited as retail mechanics. In particular
the rectangular viewport currently reserves 128/480 of the window height;
this is not a recovered retail occlusion mask for the console's irregular edge.

Reproduction and regression commands, from the repository root:

```sh
make build/sc_catalog
build/sc_catalog data/STARCRAFT/native > /tmp/sc-units.inc
cmp /tmp/sc-units.inc games/starcraft/units.inc
make test-starcraft
env SDL_VIDEODRIVER=dummy build/bin/starcraft --check
env SDL_VIDEODRIVER=dummy build/bin/starcraft --screenshot /tmp/sc-menu.bmp
env SDL_VIDEODRIVER=dummy build/bin/starcraft --map catalog --screenshot /tmp/sc-map.bmp
rg --files --hidden --no-ignore data/STARCRAFT/install | wc -l
rg --files --hidden --no-ignore data/STARCRAFT/native | wc -l
```

`--check` alone reports the fallback sheet (zero frames) and does not establish
complete sprite-cache coverage. The focused native test explicitly checks all
228 cache entries, 148 unique GRPs, 7,667 physical frames, native dialog
geometry, representative facing/mirroring, selected portrait and actual
movement. Passing it does not establish correctness of every animation path,
all race HUDs, or every original game rule.

Further evidence needed: complete IScript operand/branch audit, original
movement/timing, images.dat drawing/remapping modes, shadows and overlay
lifecycle, every UI state/control callback, full text/hotkey handling, health
wireframe randomization, retail map/pathing semantics, sound playback, and
Brood War/Remastered compatibility. Follow [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md)
when executable investigation begins; no StarCraft function addresses were
established in this work.
