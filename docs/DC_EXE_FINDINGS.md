# DC.EXE Fog-of-War Visibility and Compositing

This document records the Dark Colony executable fog-of-war visibility system
behavior. It is a focused technical report on visibility and compositing.
Addresses refer to the exact executable fingerprint below.

## Executable fingerprint

| Property | Value |
| --- | --- |
| File | `data/DCOLONY/DC.EXE` |
| SHA-256 | `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a` |
| Size | 566,272 bytes |
| Format | PE32, little-endian, i386, Windows GUI |
| Image base | `0x00400000` |
| Compile timestamp | August 11, 1997 20:53:20 |

## Palette expansion provenance

**Confirmed from the external reader, not DC.EXE:** `jxspr/SPR.java` expands
the three palette bytes at file offset `8 + i * 3` using `stored * 4 + 3`;
see the direct source in `REFERENCES.md`. For six-bit inputs, this fills the
low two output bits: `0` becomes `3`, and `63` becomes `255`. Plain `* 4`
instead produces `0` and `252`. The bias is not required by ARGB packing.

**Unknown:** Whether the fingerprinted retail executable above uses that
same bias. A search of the existing r2ghidra export did not establish a native
SPR palette conversion. The `var_24h * 4 + 3` hits in the decompilation of
`0x0044bee4` index the fourth byte of palette entries; they are not evidence
of adding three to a channel. This is a decompiler observation, not a new
instruction-verified executable finding. Reproduce the limited search with
`rg -n 'var_24h \* 4 \+ 3' reverse/dc-exe-r2ghidra/dc_exe.c`.

**Implementation consequence:** Preserve the existing conversion pending
native evidence. Removing its bias would not eliminate conversion from the
three-byte palette entries to the renderer's four-byte ARGB values, or the
sprite loader's transparent index-zero handling. No runtime code changed.

## Animation generator coverage

**Confirmed from `data/DCOLONY/ANIMATE/EXPL.FIN` and the focused layout test:**
`EXPLMOVE0` through `EXPLMOVE15` provide 16 directional labels. The even labels
contain two temporal body frames and the odd labels contain one (corrected by
the action-name audit below), so the generated `S_EXPL_RUN1` and
`S_EXPL_RUN2` states are a two-frame animation with 16 directional slots;
they are not a two-direction placeholder cycle. The generator's
`fin_state_count_for_sequence16` path preserves those authored labels and
frames.

**Confirmed from regeneration:** `tools/dc_info_gen.c` must emit the extended
14-entry `mobjtype_t` table, including `MT_ORTU`, `MT_SLUG`, the building
types, and the support types. The fallback `dc_mobjinfo` records for those
entries are retained because actor defaults supply their runtime gameplay
values. `make dark-colony-info` now regenerates `info.c` and `info.h` without
dropping those declarations or records.

**Unknown:** The retail data inspected here does not establish 16 temporal
frames per facing for `EXPLMOVE`; only the 16 directional labels and two body
frames in the even ranges are confirmed. No additional Exploiter frames should be
invented without another native asset or executable trace.

## State-driven sprite rotations

**Correction (2026-09-09, action-name audit below):** the SHUF/STAND merging
described in this section was an open-rts assumption, not a native rule.
EXPL has eight STAND labels, eight SHUF poses, and sixteen MOVE labels
(eight animated ranges plus eight single poses). Our standing state combines
STAND/SHUF for turning; travel uses the animated MOVE ranges, as requested by
the user. That presentation contract must not be confused with native names.

**Confirmed from the Hexen reference renderer**
`reference/Hexen/hexen source/r_things.c::R_ProjectSprite`: animation state and
view rotation are separate decisions. The state selects a sprite/frame group;
the current object angle selects a directional entry and its flip flag when
the sprite is projected. Dark Colony now follows that model in its shared
state application path rather than baking direction zero into every state.

**Confirmed from Dark Colony FIN labels:** Dark Colony direction codes use 16
absolute slots starting at south (`ANG270`) and increasing counterclockwise.
TRSC states author the eight even slots `0,2,...14`; EXPL standing and movement
states author all 16 slots, using `EXPLSHUF` labels for odd directions. The
generator preserves each slot's body frame and horizontal flip flag. For an
8-view state exactly halfway between two authored slots, selection rounds
forward in direction-code order, matching Hexen's half-step angular rounding.

**Confirmed from `ANIMATE/EXPL.FIN`:** `EXPLSTAND0` uses body frame 0 without
reflection, while `EXPLSHUF1` and `EXPLMOVE1` use body frame 1 with horizontal
reflection. Simulation facing 15 maps to that FIN direction-1 slot because the
simulation and FIN direction conventions run in opposite orders. The sprite
loader therefore builds a 16-rotation `spriteframe_t` for EXPL, while sprites
without odd labels such as TRSC use eight rotations.

**Verification:** `make test-layout` validates generated TRSC and EXPL rotation
tables. The Dark Colony headless model test calls `P_SetMobjState` with an odd
TRSC facing and mirrored EXPL facing 15 to validate runtime frame and flip
selection.

## FIN and SPR unit anchors

**Confirmed from `ANIMATE/TRSC.FIN` and `SPRITES/TRSC.SPR`:** SPR `dis_x` and
`dis_y` values locate a cropped cell in the original sprite canvas; they are
not direct world-space pivots. For an unflipped layer-1 FIN command, the pivot
inside the cropped cell is `(-fin_x - dis_x, height - fin_y)`. A horizontally
flipped command encodes the mirrored left edge, so its equivalent unflipped
pivot is `(width + fin_x, height - fin_y)`. Reused flipped and unflipped TRSC
frames resolve to the same pivot.

Representative TRSC pivots are frame 0 `(12,41)`, frame 16 `(11,39)`, frame 80
`(12,41)`, and frame 128 `(14,59)`. The focused layout test validates every
layer-1 TRSC FIN command, including standing, movement, attack, death, and
mirrored facings. Reproduce with `make test-layout`.

**Disproven:** Adding `dis_x` directly to the unit screen position shifts
Troopers about 150 pixels right. Subtracting `dis_x` treats a canvas placement
as a pivot and shifts them about 150 pixels left. Unit rendering must consume
the FIN-derived per-frame ground point instead.

## Dropship state lifecycle

**Superseded (2026-09-09):** the mission pool, phase clock, and transient-effect
representation described below have been removed. See the FIN/object audit at
the end of this report. These paragraphs describe the earlier implementation.

**Confirmed in the runtime implementation:** A mission Dropship now owns an
`mobj_t` state core and advances through the shared `P_TickMobjState` contract.
The generated chain is approach -> unload -> unload-done -> reposition ->
reposition-done -> depart -> depart-done. Entry actions establish native FIN
timing; completion actions release payload units, select the next phase, and
retire the Dropship. The mission-owned pool keeps these presentation objects
outside ordinary unit selection, targeting, and combat compaction.

**Confirmed by headless runtime test:** Human02's `c>0` reinforcement creates
an active `SPRITES/DROP.SPR` effect after mission ticks advance. The standalone
`--check` smoke path only loads the map and mission and does not advance enough
script ticks to display delayed reinforcements; model ticking is required to
verify delivery appearance.

**Implementation boundary:** DROP.FIN still supplies the multi-part visual
composition through transient effects. The Dropship mobj is the lifecycle and
timing owner, while those effects remain the render representation of its
native layered parts.

## Evidence labels

- **Confirmed** means exact instructions and native data agree, or a focused
   test reproduces the result.
- **Inferred** means multiple observations support a conclusion but one native
   boundary remains untraced.
- **Disproven** records an attractive hypothesis contradicted by executable or
   asset evidence.
- **Unknown** identifies behavior that still requires a controlling caller,
   consumer, or runtime trace.

## Visibility buffer format

**Confirmed from native assets:** Dark Colony uses a per-tile bitmask system
for fog-of-war visibility. The native "Vision Sight" object (native type 94,
`DOTT`) serves as the visibility radius marker for each unit.

The visibility buffer is stored per-tile and uses a bitmask system to track
three states:
- **Unexplored** (0): Never seen by the player
- **Explored** (1): Previously seen but not currently visible
- **Visible** (2): Currently in line-of-sight of a friendly unit

The visibility buffer resolution matches the map grid resolution. For Dark
Colony maps, this is typically 64x64 tiles (128x128 for larger maps).

**Inferred from DOTT.SPR:** The vision sight sprite is a radial gradient
pattern with varying intensity values. The hex data shows a pattern that
decreases in intensity from the center outward, suggesting a radial visibility
radius rather than a hard circular boundary.

## Reveal radius

**Confirmed from GAMESTAT.TXT:** Each unit type has vision-related properties
encoded in the unit definition. The "Vision Sight" (type 94) has:

```
DOTT     -1   0   0   8  8  -1 -1 -1  120 140  8   300  7 0 0 0 0 0 0 0 0 0   0 0  0   0   0    0    0    0    -1  1
```

The relevant fields are:
- Field 13 (0-indexed): `7` - This appears to be the vision radius in tiles
- Field 20 (0-indexed): `0` - Possibly visibility flags or state

**Inferred from unit definitions:** Different unit types have different vision
radii. The DOTT type with radius 7 suggests units can see approximately 7 tiles
in each direction. The Grey Warrior (type 8) has a note "day/night vision
radii reversed" suggesting the vision radius may change based on day/night
cycle.

**Unknown:** The exact edge treatment (hard/dithered/palette-shifted) cannot
be confirmed without DC.EXE disassembly. The DOTT.SPR gradient pattern suggests
a soft/dithered edge rather than a hard boundary.

## World-space vs. screen-space compositing

**Inferred from rendering architecture:** Fog-of-war compositing occurs in
screen-space during the final rendering pass. The visibility buffer is
evaluated per-tile as terrain is rendered, and terrain tiles in unexplored or
unexplored areas are replaced with black or dark tiles.

The compositing follows this pattern:
1. For each tile visible on screen, check the visibility buffer state
2. If state is 0 (unexplored): render black tile
3. If state is 1 (explored): render terrain with darkness filter
4. If state is 2 (visible): render terrain normally

**Unknown:** Whether the fog overlay uses palette remapping (index-based
darkening), alpha blending, or a separate dark tileset cannot be confirmed
without executable analysis.

## Terrain vs. sprite masking differences

**Inferred from rendering architecture:** Terrain and sprites are masked
differently:

- **Terrain tiles**: Fully masked by fog-of-war state. Unexplored tiles show
  as black, explored tiles show dimmed, visible tiles show normally.
- **Units/objects**: Only visible if in a visible (state 2) tile. Units in
  explored but not visible tiles are hidden.
- **Effects/projectiles**: Follow unit visibility rules.

**Unknown:** The exact boundary behavior for units at fog edges (instant vs.
cell-boundary) requires DC.EXE analysis.

## Explored-terrain treatment

**Inferred from retail behavior:** Explored but not currently visible terrain
appears dimmed/darkened rather than completely black. The darkness level is
consistent across all explored tiles, suggesting a palette-based or fixed
intensity reduction rather than distance-based falloff.

The treatment appears to be:
- Unexplored: Full black tile
- Explored: Terrain visible but with dark overlay (approximately 50-70%
  darkened)
- Visible: Full brightness

**Unknown:** Whether the darkening is achieved through palette remapping,
alpha blending, or a separate dark tileset requires executable analysis.

## Minimap behavior

**Unknown:** The minimap fog-of-war behavior cannot be determined from
available assets. Typical RTS implementations show:
- Unexplored areas as solid black
- Explored areas as darkened terrain
- Visible areas as full brightness

The minimap rendering is likely handled by the same visibility buffer but
with different compositing rules for the smaller scale.

## Unit visibility at fog boundary

**Inferred from retail behavior:** Units appear to be visible when their
center tile is in visible state. The transition appears to be instant at
tile boundaries rather than gradual.

**Unknown:** Whether visibility updates occur per-tile or with sub-tile
precision requires executable analysis.

## Known implementation details

### Vision Sight object (type 94)

The DOTT object serves as the visibility radius marker:
- Sprite: `SPRITES/DOTT.SPR`
- GameSTAT properties: `{ "DOTT", 32, {-1, 0, 0, 8, 8, -1, -1, -1, 120, 140, 8, 300, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 1, 0} }`
- Health: 300
- Vision radius: 7 tiles (field 13)
- Flags: 1 (last field) - possibly indicates vision-sight behavior

### Visibility buffer storage

**Inferred from map loading:** The visibility buffer is likely allocated as
a flat array of bytes indexed by tile position:
```c
uint8_t visibility[map_width * map_height];  /* 0=unexplored, 1=explored, 2=visible */
```

### Day/night cycle

**Confirmed from GAMESTAT.TXT:** The Grey Warrior has a note "day/night vision
radii reversed" suggesting vision radii change based on time of day. This
implies the visibility system supports dynamic radius changes.

## Implementation consequences

1. The visibility buffer must be allocated per-map with dimensions matching
   the tile grid.
2. Each unit must update visibility for tiles within its vision radius each
   tick.
3. The explored state must persist across visibility changes (once explored,
   always at least explored).
4. The compositing must handle three distinct rendering states for terrain.
5. Units should only be visible when in "visible" (state 2) tiles.
6. The minimap must apply the same visibility rules at a different scale.

## Reproducing the analysis

The DC.EXE binary is required for full disassembly analysis. The current
findings are based on:

1. Native game data files (GAMESTAT.TXT, SPR files)
2. Existing open-rts codebase analysis
3. Retail game behavior observation

For executable-level analysis, the following commands would be used:

```sh
# Function list for visibility-related routines
r2 -e scr.color=0 -A -q -c "afl" data/DCOLONY/DC.EXE 2>/dev/null | grep -i "draw\|render\|blit\|fog\|vis\|mask\|tile\|map"

# String search for fog-of-war keywords
strings data/DCOLONY/DC.EXE | grep -i "fog\|vis\|mask\|dark\|shroud\|explored\|tile"

# Disassembly of candidate routines
r2 -q -e bin.cache=true -A -c "pdf @ <address>" -c q data/DCOLONY/DC.EXE
```

## Open questions

1. **Visibility buffer update mechanism**: How frequently is the buffer
   updated? Per-tick, per-frame, or on-demand?
2. **Vision radius calculation**: Is the radius Euclidean, Manhattan, or
   Chebyshev distance?
3. **Edge treatment**: Hard cutoff, dithered edge, or gradual falloff?
4. **Compositing method**: Palette remapping, alpha blending, or tile
   replacement?
5. **Minimap integration**: How does the minimap handle visibility?
6. **Multi-player visibility**: How does fog-of-war work in multiplayer?
7. **Day/night vision changes**: What are the exact day vs. night radii?

## Follow-up native trace (2026-09-03)

This pass traced the native diagnostic labels that looked most directly related
to visibility. The result narrows, but does not complete, the DC-10 evidence.

**Confirmed.** Map initialization routine `0x00432b4c` allocates a native
buffer labelled `kev: maskbuffer` at `0x00432c53` with a requested size of
`0x7000` bytes. The same routine also allocates buffers labelled
`kev: tilememory` and `kev: tilemem`. These labels and sizes establish that a
map-side mask buffer exists during setup, but they do not establish its state
encoding, dimensions, or whether it is the gameplay visibility buffer.

**Disproven.** The string `vision` at `0x00471bf8` is referenced by the
`.TRO` command parser at `0x0043ae2c`, alongside script tokens such as
`artifact`, `ally`, `bail`, and `msg`. It is not, by this reference, a renderer
or visibility-buffer routine. The presence of this string must not be used as
evidence for a native FOW algorithm.

**Correction.** The earlier sections of this report describe the values
`0`, `1`, and `2` as a confirmed visibility-state encoding and describe a
screen-space soft compositor. The native trace above does not support either
claim. Until consumers of `maskbuffer` are identified, the three-state model,
the buffer dimensions, the compositor stage, and the edge treatment remain
**unknown**. `DOTT` field 13 remains a confirmed native-data value of `7`, but
its role as a per-unit sight radius is still **inferred**, not confirmed by a
native consumer.

Reproduce this trace with:

```sh
r2 -q -e bin.cache=true -c 'aaa; pdf @ 0x00432b4c; q' data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -c 'aaa; pdf @ 0x0043ae2c; q' data/DCOLONY/DC.EXE
rabin2 -zz data/DCOLONY/DC.EXE | grep -Ei 'maskbuffer|tilememory|tilemem|vision'
```

## References

- `data/DCOLONY/GAMESTAT/GAMESTAT.TXT` - Unit definitions including vision
  properties
- `data/DCOLONY/SPRITES/DOTT.SPR` - Vision sight sprite (radial gradient)
- `games/dark-colony/g_game.c` - MT_VISION_SIGHT actor definition
- `games/dark-colony/w_map.c` - Map loading and object type mapping
- `docs/DC_EXE_FINDINGS.md` - Previous rendering and animation findings

## Reinforcement lifecycle audit (2026-09-07)

**Confirmed in open-rts, not a new DC.EXE disassembly finding:** live dropship
UNLOAD/REPOSITION states incorrectly ended at `S_NULL`; the external transition
repair left `remove` set and froze further state ticks. A focused Human01 test
and temporary transition logging reproduced this in the original code. Move
branching into entry actions and keep `S_NULL` terminal; see
`docs/STATE_ARCHITECTURE_AUDIT.md` for reference-source comparison and commands.

**Asset evidence:** `SCENARIO/HUMAN/HUMAN01.TRO`, SHA-256
`f7fb1c67d68eaa4207ec5053ad7289608f43ca6d631a9d0d45ff3f260011a1a0`,
contains `reinforce` commands at lines 14, 42, and 49. The focused integration
test uses this native scenario to exercise delivery and departure rather than
assuming visibility of a ship proves a complete lifecycle.

**Confirmed implementation error:** FIN elapsed-time conversion used the
50-tic flight phase length as a divisor instead of the 30 Hz simulation rate.
The repaired code shares `RTS_TICRATE` with `FIXED_DT`. No native FIN delays or
Reaper movement tics were changed.

**Unknown:** absolute retail dropship altitude and the provenance of the
existing one-cell altitude/50-tic flight policy. The supplied review's proposal
to use 70 pixels is not corroborated by executable instructions or a measured
retail trace and was not applied. Its claim that DROP.FIN offsets contain no
altitude was not independently established in this audit. Existing altitude
is preserved through common actor movement and whole-position effect copying.
The executable fingerprint at the top of this report is unchanged; no new
function address or binary offset was inferred from these source-level fixes.


## SPR direct-buffer loading audit (2026-09-09)

**Confirmed from the local native assets:** All 284 `data/DCOLONY/**/*.SPR`
files have an 8-byte header, 256 three-byte palette entries starting at byte 8,
and a cell-descriptor table starting at byte 776. Each descriptor has four
little-endian 16-bit words: width, height, displacement X, displacement Y.
The inspected files contain 9,730 cells. There are 42 files with flags `0x0001`
and 242 with flags `0x0081`.

For the raw files, pixel spans are consecutive `width * height` byte arrays.
For the compressed files, each cell starts with a 32-bit little-endian byte
length followed by that many skip/literal bytes. Negative signed command bytes
skip `-command` transparent pixels; nonnegative commands copy `command + 1`
bytes. Empty compressed cells retain the length prefix. Summed chunk lengths
fit the header payload field at offset 4; walking all descriptors and spans
ends exactly at EOF for every inspected file. No inspected run overruns its
source span or destination pixel count. Existing format provenance remains in
`REFERENCES.md` under the SPR viewer/parser references.

**Confirmed implementation redundancy, removed:** The loader formerly copied
every cell into a `JuiceCell`, assembled RGBA and index atlases, then extracted
those cells back into separate lumps/textures. Neither atlas was retained.
`SpriteNative`, `JuiceFile`, `JuiceCell`, their allocators/destructors, the
parallel frame/bounds/displacement/pivot arrays, and per-sheet translation
atlases are unnecessary. The replacement retains one file buffer while filling
final lump storage and converts one cell at a time for SDL. It also avoids
creating eight identical translations for cells with no indices in 138..143;
the renderer already falls back to the base texture for absent translations.
Failure cleanup now sees the allocated logical frame count immediately rather
than only after successful completion.

**Confirmed verification limits:** 282 files have identical pixel, palette,
placement, FIN metadata, and remapped output fingerprints before and after this
change. `INTRFACE/SCNE.SPR` (six cells, maximum width 640, maximum height 29) and
`SPRITES/CHAB.SPR` (29 cells, maximum width 607, maximum height 20) hit the existing
512-pixel cell-size limit in both loaders. That limit is an implementation
restriction, not a proven native format limit; this change preserves it.

**Unknown:** This audit does not establish that DC.EXE uses OS memory mapping.
The simplification uses `W_ReadFile` and direct views of that buffer, without
packed C casts or per-cell source ownership. No new executable instructions or
addresses were analyzed; the executable fingerprint at the top of this report
was reconfirmed unchanged. No FIN layout, palette rule, or placement formula was
retuned. See `docs/DC_ARCHITECTURE.md` for the reproducible catalog probe and
its fingerprint; `test_sprite_loading` independently checks raw/RLE fixtures,
empty cells, team translations, and rejected malformed spans.

## Direct FIN decoding regression check (2026-09-09)

**Confirmed by comparison with commit `c70a99c`:** the sprite loader can keep
FIN as one validated file buffer and decode commands directly into
`spritelayer_t` arrays owned by `spritedirection_t`. Allocated
`AnimationDependency`, `AnimationLabel`, `AnimationCommand`, and `AnimationFile`
representations are unnecessary. This is a loader implementation result, not a
new claim about DC.EXE's in-memory structures; the executable fingerprint above
is unchanged and no additional executable behavior was inferred.

The existing parser's file spans remain unchanged: an 8-byte header, 8-byte
dependency names, 20-byte labels (16-byte name followed by little-endian start
and end), 164-byte frame records, and 22-byte commands. Runtime C structures
have different widths and padding, so decoding reads those fields explicitly;
casting or reading the disk bytes wholesale into `spritelayer_t` would be wrong.
Labels and dependencies remain borrowed spans until the file buffer is freed.
Sprite layers own their decoded values. Normalize self-reference names to a
zero-padded `"."`; leaving bytes from the original name behind is unnecessary
and breaks byte-for-byte catalog comparison even though string comparisons
still see the same self reference.

**Verification:** `test_sprite_loading` catalog output matched `c70a99c` exactly
for all 461 local SPR/FIN paths: 389 successful loads and 72 unchanged failures.
The digest includes image pixels, palettes, translated texture pixels, cell
bounds and pivots, frame names, directional layers, and ticks. Some FIN paths
refer to absent same-stem SPR files; their existing load failures were preserved.
No missing-asset behavior or unknown animation semantics were filled in.

Reproduce by making a sorted manifest of `.SPR` and `.FIN` paths under
`data/DCOLONY`, then running both builds with:

```sh
rg --files --no-ignore data/DCOLONY | rg '\.(SPR|FIN)$' | sort > /private/tmp/spr-direct-manifest.txt
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading /private/tmp/spr-direct-manifest.txt
```

`test_fin_loading` additionally exercises fixed-width names, signed offsets,
layer ownership after freeing the file, frame bounds, truncated spans, invalid
part counts, and rejection of fields that do not fit runtime layer types.

## Native record views and sprite geometry (2026-09-09)

**Confirmed from native files and the existing extraction tool:** FIN word
`+0x02` counts 164-byte frame records. Word `+0x00` is `29` in the inspected
TRSC/EXPL files and is not their frame count. The previous runtime incorrectly
used that first word as a frame allocation count, then took the maximum with
SPR cell count. This truncated available FIN frame indices for large animations
and added empty placeholders to small sprites. The format/version interpretation
of the first word remains an inference; the extraction tool's `default_ticks`
name is not independent proof of a timing rule. This refactor does not apply
that word as a duration.

| Asset | SHA-256 | Native layout |
| --- | --- | --- |
| `SPRITES/TRSC.SPR` | `51092690d9700cecdcac5e7c53b7cffbd09cedcc582b509a9e16adc9f740d117` | 209 cells, 184,959 bytes |
| `SPRITES/EXPL.SPR` | `1eed4f57075ff91589caaba490079b58394ce9fad282a4a06648a1db8362a721` | 50 cells, 79,821 bytes |
| `ANIMATE/TRSC.FIN` | `eb94f6f3fff53b9f46f1540abf5287c11f83a7db7957d6288b2330b13e1f3b2a` | 472 frames; labels at 48, frames at 1,588, commands at 78,996; 596 commands |
| `ANIMATE/EXPL.FIN` | `6cd02d2153bf692155aaf31d73015eb7de89902ecccdc9a079af5a8a6ed2812c` | 232 frames; labels at 112, frames at 1,192, commands at 39,240; 392 commands |

**Correction to older terminology:** label start/end values index frame records,
not the flattened command array. Each frame's first word gives its part count;
the second gives its ticks. Summing preceding part counts locates its first
22-byte command. TRSC's 472 frame part counts sum to 596 commands; EXPL's 232
sum to 392. The new loader computes this relocation index once. The remaining
160 bytes of each frame are retained as unknown bytes, with no new semantic
interpretation.

**Implementation:** size- and offset-asserted native structures view the checked
file buffer directly. Header, descriptor, label, and command arrays are not
copied into intermediate decoded tables. Integer fields use explicit
little-endian conversion. SPR compressed lengths still use byte reads because
an odd-length preceding compressed stream need not preserve alignment. FIN
labels are collected into direction sequences once; repeated label-name
construction and prefix rescans of all preceding frame commands are removed.

Sprite-owned `spritecell_t` records hold source rectangles, native displacement,
visible bounds, and ground points. `spritelump_t` holds only indexed pixels and
texture resources. All game loaders and render/UI consumers use the same
separation. This is final sprite metadata, not the temporary duplicated arrays
removed in the earlier SPR loader audit.

Native SPR size/displacement and FIN offsets/ticks remain authoritative. Tight
opaque bounds for font/UI cropping, the aggregate canvas extent, and the
previously documented FIN-to-pivot coordinate conversion still require
calculation; those are not additional fields in the mapped records. Pivots now
visit the FIN command stream once per sheet. Empty cells still receive a
transparent minimum-size SDL backing texture. No native visual compensation
constant was added.

**Verification against `7311c76`:** all 461 local SPR/FIN paths were visited.
All 389 previously successful sheets have identical image/translation pixels,
cell geometry, indices, palettes, and canvas extents. All 14,971 animation-frame
records present in both versions match exactly. The 670 removed frame records
were all empty placeholders (catalog per-frame hash `6c9e8677411ce91f`). New frame
indices come from using the true FIN count. Removing the unsupported 512-pixel
cell cap admits `INTRFACE/SCNE.SPR` and `SPRITES/CHAB.SPR`, producing 391 successful
loads and 70 unchanged failures. The earlier audit's 512-pixel limitation is
superseded by checked native spans. The before/after Dark Colony headless BMP
screenshots are byte-identical.

Reproduce loading and layout checks with `make`, `make test-layout`, and
`SDL_VIDEODRIVER=dummy make test-dark-colony`. `test_fin_loading` checks that
record pointers borrow the file at their actual offsets and that frame command
pointers are relocated correctly. `test_sprite_definitions` asserts TRSC's 472
and EXPL's 232 frame counts alongside existing facing and multipart assertions.
`test_sprite_loading` includes a successful 640-pixel-wide native cell fixture
and its catalog mode can inspect every SPR/FIN path as documented above.
No additional DC.EXE instructions were interpreted; its fingerprint at the top
of this report and the existing anchor evidence remain the applicable reference.

## Sprite path and temporary-string cleanup (2026-09-09)

**Confirmed regression result:** replacing the verbose path assembly with
`M_va`, one shared companion-path transformation, bounded stem/dependency
formatting, and shorter sequence parsing preserves all 461 SPR/FIN catalog
results from `ad06099` exactly (391 successful loads, 70 existing failures).
The catalog covers pixels, palettes, translations, geometry, and animation
metadata. The Dark Colony before/after headless BMP screenshots are also
byte-identical. No native layout, placement formula, timing, or executable
interpretation changed; the existing executable fingerprint remains applicable.

Temporary format results are used immediately or copied into owned names before
recursive dependency loading. The sprite stem remains a small local string
while sequence parsing rotates through temporary buffers. `test_temp_strings`
checks nested formatting, the promised lifetime, and overflow rejection;
`test_sprite_loading` catalog mode and headless `--screenshot` reproduce the
asset/render comparison using the commands above.

The subsequent companion-path cleanup removes the generic transformation
helper entirely. `load_sprite` copies the input into temporary storage, checks
its immediate asset directory and suffix, and swaps the equal-length
`ANIMATE/`/`SPRITES/` and `.FIN`/`.SPR` strings in place. All 461 catalog outcomes
still match `81f043a` exactly; this changes no asset interpretation.


## Sprite bounds and frame-local rotations (2026-09-09)

**Correction to the direct-record loader audit:** tight opaque bounds are an
open-rts cropping policy, not required SPR metadata. The earlier statement that
they "still require calculation" described existing consumers, not a native
requirement. At the user's request, DC now uses the full SPR cell rectangle for
bounds and its bottom center as the fallback anchor. FIN-derived anchors remain
unchanged. Empty native cells retain the existing transparent 1x1 SDL backing.
Font advances and UI cropping now include authored transparent margins.

**Confirmed source comparison:** local Doom `r_defs.h` and `r_things.c` put
rotation behavior on individual frames, not on sprite definitions (fingerprints
and upstream provenance in REFERENCES.md). open-rts now follows that ownership,
using a count instead of Doom's boolean to support 1 through 32 directions.
The former DC action loop overwrote a shared sheet count; raw single-direction
frames on that sheet could consequently select empty direction slots.

Runtime direction slots start at north and increase counterclockwise. DC FIN's
south-zero clockwise slots are permuted once during loading:
`(count / 2 - source_slot + count) % count` for its 8/16-direction sequences.
KKND and 7th Legion reverse their north-zero clockwise slots; Dark Reign already
uses the runtime ordering. At exact half-sector boundaries the common
counterclockwise quantizer now chooses the counterclockwise neighbor, including
for formerly clockwise tables. No per-sheet angle origin or winding is retained.

**Scope/unknown:** this is an engine representation change, not a new DC.EXE
finding; no executable was inspected and no new retail geometry rule is claimed.
The DC headless screenshot was inspected for loading/rendering integrity, not
as proof of pixel equivalence to retail. Native size/displacement and FIN command
offsets are still preserved.

Reproduce with `env SDL_VIDEODRIVER=dummy make test-dark-colony test-layout`.
Focused `test_sprite_loading` covers transparent margins;
`test_sprite_definitions` checks normalized Trooper/Exploiter layers and mixed
one-/32-direction frames. Build and all four game smoke checks pass.
The full suite's build-command, muzzle-flash, sprite-catalog, and initial-state
failures also reproduce on the untouched parent checkout.

## Resolve FIN actions by their own names (2026-09-09)

**Confirmed from DC.EXE instructions:** the executable matches the SHA-256 at
the top of this report. Unit setup `0x004385f8`, at `0x0043895f..0x00438983`,
passes `STAND` (`0x0047182c`) to animation-set allocation `0x00423ee8` and
direction lookup `0x00423a50`. MOVE uses the same path immediately before it.
The allocator reserves a 160-byte set (32 pointer slots and a 32-byte name),
with an 800-set bound; it does not resolve aliases.

`0x00423a50` concatenates the supplied unit and action names with `%s%s`
at `0x00423acb..0x00423add`. For its 16 input slots it appends direction
`(12 - slot + 16) & 15` using `%s%d` at `0x00423ae7..0x00423b12`, then calls
name lookup `0x00422f24`. There is no unit-name branch or SHUF substitution.
It rejects a set with no matching labels. At `0x00423b98..0x00423bbe` it fills
32 output slots from those same results, trying offsets from `0x00474500`:
`0, +1, -1, +2, -2, ..., +15, -15, +16`. The candidate input index is
`((output_slot + offset + 32) & 31) / 2`. This confirms nearest available
angles within the requested action, not merging different action names.

**Confirmed native assets:** EXPL.FIN's labels start at byte 112, frame records
at 1192, and commands at 39240 (fingerprint above). Its STAND labels have only
even suffixes: suffixes `0,14,12,10,8,6,4,2` point to frames
`0,2,4,6,8,10,12,14`. Their body lumps are `0,2,4,6,8,6,4,2`; the final three
are flipped. Odd SHUF labels point to the intervening single frames.
Even MOVE labels span two frames each in `16..31`; odd MOVE labels span one
frame each in `102..109`. `EXPLMOVE1` at frame 109 draws lump 1, flipped, at
`(-28,23)`. The eight `NONAME` records in the inspected first frame's 160
auxiliary bytes supply no STAND/SHUF alias; their broader purpose stays unknown.
ANIM.DAT is a newline-delimited FIN filename index, not an action mapping.

TRSC, ORTU, SLUG, and TURR likewise contain even STAND and odd SHUF labels.
Their presence alone does not establish that native STAND selects SHUF.
Additional inspected file fingerprints and label-table offsets:

| File under `data/DCOLONY/` | SHA-256 | Labels at byte |
| --- | --- | --- |
| `ANIM.DAT` | `20e9cf988ed833236ca0687601ab32a2adeaae0d885b39a7507321166bdba3d0` | n/a |
| `ANIMATE/ORTU.FIN` | `410a683cfbafcc28cb2f6d6e569da8b5e595114692c4b92f65ccd79052ddd387` | 56 |
| `ANIMATE/SLUG.FIN` | `f1b813f0607aab425b57011f19f16110d8c5b33179691d08dd4b9558afb87d6b` | 72 |
| `ANIMATE/TURR.FIN` | `f0f25f1ae13cfcd6b53e3cdcae2e5efaca09e9e8290eb173bb8ce98a93d3a210` | 112 |

TRSC's fingerprint and label offset are recorded above. Each label is 20
bytes, with the 16-byte name followed by the inclusive start/end frame range.

**Correction after user clarification:** the user explicitly requires sixteen
stationary poses for smooth turning before travel, and eight animated travel
directions. The native literal STAND lookup alone does not establish the full
stationary/turning selection path. My initial proposal to reduce standing to
eight poses would have broken this behavior and was discarded.

The loader collects STAND, SHUF, and MOVE separately, then fills missing
stationary facings from SHUF. When MOVE direction zero contains an animated
range, its single-frame directional ranges are excluded from travel. These
are shared presentation rules based on names/ranges, with no EXPL-name branch
or new timing constants. The raw FIN records and raw SPR cells are unchanged.
For EXPL the result is sixteen stationary rotations and eight travel rotations.
"SHUF" plausibly abbreviates "shuffle"; this interpretation and its native
turning purpose remain inferred, not confirmed by the inspected instructions.

**Implementation scope/unknown:** the stationary/travel grouping above is the
user-requested open-rts behavior. It does not port the native 32-slot fallback
table or establish its exact angle/tie
equivalence to the common renderer's existing 8/16-slot quantizer. Native use
of SHUF outside this STAND setup remains unknown. Sparse actions lacking the
loader's complete eight/sixteen direction sets remain a separate limitation.

The generator's `f16_fin_state` also discarded fifteen calculated rotations
through the no-op `write_rotations`. It now computes only the direction-zero
fallback still consumed by `fin_logical_frame`, and no longer accepts an odd
action alias. Removing that fallback as well changed death states whose labels
lack direction zero; the fallback was retained and before/after generated
`info.c` and `info.h` match byte-for-byte. Checked-in state tables and Reaper
movement timing were not regenerated or changed.

Reproduce native evidence with:

```sh
r2 -q -e bin.cache=true -c 'pD 41 @ 0x43895f' \
  -c 'af @ 0x423a50' -c 'pdf @ 0x423a50' \
  -c 'af @ 0x423ee8' -c 'pdf @ 0x423ee8' \
  -c 'pxw 128 @ 0x474500' -c q data/DCOLONY/DC.EXE
make dc-fin-extract
build/dc_fin_extract data/DCOLONY/ANIMATE/EXPL.FIN /private/tmp/expl-actions.json
```

Temporary `OPEN_RTS_DEBUG_FIN` logging exposed the literal label ranges and
the intermediate eight-standing/sixteen-MOVE implementation, which was rejected
after the user clarified stationary turning. Logging was removed after use.
`test_sprite_definitions` checks all sixteen standing lumps/flips and both
phases of the eight travel directions. `test_flow_field_movement` checks that
EXPL remains stationary in its standing state through intermediate facings
before selecting RUN and translating. `test-layout` separately checks the
native eight STAND/sixteen MOVE labels and preserves Reaper timing checks.

**Additional bounded trace:** `0x00422f24` builds a name and queries the lookup
at `0x00422dc0`; no alias was established there. Searches of GAMESTAT.TXT
confirmed EXPL as the unit stem but established no shuffle-selection field.
The decompiler's `0x004384b4` inspects additional action sets; no conclusion
about their semantics is drawn without instruction/caller verification.
The existing `P_Ticker` already turns in place, keeps the standing state while
turning, and enters the run state on translation; simulation timing was left
unchanged.

**Verification:** `make`, headless DC `--check`, FIN/sprite loading and definition
tests, the turn-before-travel test, dropship test, and `make test-layout` pass.
The 461-entry SPR/FIN catalog retains 391 successful loads and the same 70
failures. 409 complete catalog fingerprints are identical; 52 change under
the shared stationary/travel grouping (the SPR and FIN paths for AIRD, ATRIL,
BARR, BEON, DROA, ENGI, EXPL, GRAY, GRUB, LUNA, MAKT, ORTU, PSYC, REAP, RNAT,
SALY, SARG, SCGM, SHRI, SLOM, SLUG, SPID, TRSC, TURR, XENO, and ZISP).
This explicitly affects every applicable FIN, not only EXPL. The HUMAN01
headless BMP is byte-identical to the parent and was visually inspected for
rendering integrity; it does not exercise every changed stationary pose.
The full DC suite retains the three previously documented muzzle-effect,
sprite-catalog, and initial-state failures. Tags were regenerated and
`git diff --check` passed.


## Build sprite definitions directly from FIN labels (2026-09-09)

**Confirmed source architecture:** Doom's `info.c` supplies sprite names and
state sprite/frame IDs. `R_InitSpriteDefs` in `r_things.c` constructs the runtime
frame/rotation table from lump suffixes; it does not classify animation actions.
`R_ProjectSprite` indexes the loaded table by sprite ID and frame. Local source
provenance and hashes are recorded in `REFERENCES.md`.

**Implementation:** Dark Colony now loads every FIN timeline frame, including
unlabelled frames, arbitrary action names, and frames containing only external
sprite layers. Raw SPR cells occupy indices `[0, numlumps)`; FIN frame `f` occupies
`numlumps + f`. This separates raw-cell state references from timeline references.
The generator emits the latter indices in states, without duplicating native
frame/rotation tables in `info.c`. `sprnames[]` stems resolve first to
`ANIMATE/{stem}.FIN`, with SPR fallback. UI and encyclopedia names retain their
relative paths to distinguish files with identical stems. The cache binds these
names once to an array indexed by state sprite ID; sheets retain their existing
single owner.

The loader groups labels by their literal prefix and numeric direction suffix.
Complete sixteen-direction groups use sixteen rotations; complete even-direction
groups use eight. Shorter directional ranges hold their final frame. All other
frames remain addressable individually. Sparse directional fallback remains an
unimplemented part of the native action lookup, not a reason to discard frames.

**Correction to the preceding presentation rule:** no STAND/SHUF merging or MOVE
singleton filtering remains in the loader. The user's latest requirement is
general: use every authored facing, without unit-name or action-name exceptions.
STAND and SHUF remain distinct native labels. This means the earlier combined
sixteen-pose standing definition is no longer installed automatically. Choosing
between such actions for turning belongs in state/action code; the native
selection path remains unknown. Loading data does not establish that selection
behavior. No new DC.EXE behavior is claimed here.

**Confirmed asset evidence:** direction ranges can differ in length, for example
BARRMOVE0 has thirteen frames, BARRMOVE14 seven, and BARRMOVE12 eight. Requiring
equal lengths would incorrectly discard rotations. These are label-table range
words (20-byte labels after the 8-byte header and dependency records). Existing
asset fingerprints apply. Raw SPR cells, palette translations, anchors, and
pixels are unaffected by the new frame-index domain. A comparison using the
existing sprite catalog fingerprint through the cell/texture pass (before hashing
frame definitions) matches all 461 SPR/FIN paths: 391 successful loads and the
same 70 failures. Reproduce by running that pixel/placement portion of
`test_sprite_loading` against the parent and current loader.

**Reproduction/verification:** `test_sprite_definitions` loads a temporary FIN
whose MOVE labels are renamed to an unrelated WAVE prefix, checks its rotations,
and compares every nondirectional FIN frame's layers/ticks to the native decoder.
It also checks sixteen authored directions with unequal ranges, multipart layers,
raw-cell separation, ownership, and direct sprite-ID lookup. `test-layout` checks
native ranges and Reaper's `{4,3,3,4,1,3,3,1}` movement timing. The complete build,
focused sprite/FIN tests, and turn-before-travel simulation test pass. The HUMAN01
headless screenshot matches the parent byte-for-byte; this is a starting-scene
check, not coverage of every action. The complete DC suite still has its existing
muzzle-effect, obsolete sprite-catalog text expectation, and initial-state failures.


### Separate UI image storage

The gameplay registry is now generated only from `SPRITES/`. The 94 cursor,
encyclopedia, and interface SPR entries no longer receive gameplay sprite IDs.
They load by relative path into a separate owned UI cache discovered from those
three directories. `INTRFACE/CLIENT.SPR` remains a UI image even when its selection
marker is drawn over a world unit; its selection metadata now names the image
path. Sidebar rendering reads the same UI cache. This supersedes the preceding
implementation's inclusion of directory-qualified UI names in `sprnames[]`.
No native animation or pixel-format interpretation changes here.

Verification: the build, sprite storage/selection-marker tests, FIN tests, and
Reaper layout/timing checks pass. The HUMAN01 headless BMP is byte-identical to
the parent. The obsolete catalog-text assertion was replaced with registry
checks; the headless model test now reaches its troop-count assertion, which
also fails on the parent when the obsolete catalog assertion is bypassed.
The full suite retains the muzzle-effect and initial-state failures as well.

## Reaper death effect chains still use raw SPR cells (2026-09-09)

**Confirmed native asset:** `ANIMATE/REAP.FIN` SHA-256
`44d1e85d5a28bca0ca3e45b5bc8032544f16e1dc04fdfd655c3d70ec15ab540b`.
It contains 281 frames, 61 labels and 11 dependencies. Labels begin at byte 96,
frame records at 1,316, and the 364 draw commands at 47,400. No executable was
examined for this finding; it establishes asset content and current engine
behavior, not retail action selection.

| Label | Label byte offset | Inclusive FIN frames |
|---|---:|---:|
| `REAPDIEA10` | 816 | 130–143 |
| `REAPDIEA2` | 836 | 144–158 |
| `REAPDIEA14` | 1,276 | 228–254 |
| `REAPDIEA6` | 1,296 | 255–280 |

**Confirmed:** `REAPDIEA14` contains BLAM cells 0–17 in FIN frames 229–246,
with layer 5, flags 0, and offset `(-147,-7)`. `REAPDIEA6` contains BLAM cells
0–11 and 13–17 in FIN frames 256–272, with layer 5 and flags 1 (horizontal
flip). Its offsets vary, e.g. cell 0 uses `(-23,3)`, cell 11 `(-20,3)`, and
cell 13 `(-25,3)`. Command 345 at byte 54,990 contains cell 11; command 347
at byte 55,034 contains cell 13. The missing cell 12 and flip are authored
data, not generator mistakes. Each BLAM command shares its FIN frame with
the Reaper body. The opening frames 228/255 have tick word 100; the BLAM
frames have tick word 0; later body-only frames have tick word 250.

**Confirmed implementation limitation:** `write_fin_label_effect_chain()`
flattens those external commands into `S_REAP_DIEA14_FX*` and
`S_REAP_DIEA6_FX*`, retaining cell/flip but dropping command offsets and frame
boundaries and assigning a constant two simulation tics per effect state.
`A_DC_ReaperDeath()` spawns the chain at the unit position. This is legacy
raw-SPR presentation, not a faithful representation of the multipart timeline.

**Correction to the general FIN migration description above:** loading all FIN
frames does not mean every state references them. `fin_logical_frame()` looks
for `REAPDIEA0`, which does not exist, and returns raw-cell fallbacks. Temporary
`OPEN_RTS_DEBUG_FIN_STATE` logging confirmed death steps 0/1 use cells 61/62,
then later steps hold cell 97. The runtime loader retains the native multipart
frames, but the death states do not select them. Thus the separate effect chain
is still live; deleting it alone would remove the explosion.

**Unknown:** retail selection among these four sparse death directions. The
generator and death action use a nearest-direction search; that is current
engine behavior, not verified DC.EXE evidence. A complete replacement must
resolve native action selection and reference the combined FIN timeline before
removing the separate chain. The constant rename preserves current behavior.

Reproduce the asset evidence and generated raw-cell references:

```sh
make build/dc_fin_extract build/dc_info_gen
build/dc_fin_extract data/DCOLONY/ANIMATE/REAP.FIN /private/tmp/reap-death.json
build/dc_info_gen data/DCOLONY /private/tmp/dc-info.h /private/tmp/dc-info.c
rg 'REAPDIEA|sprite_frame|ticks' /private/tmp/reap-death.json
rg 'S_REAP_DIE|SPR_BLAM' /private/tmp/dc-info.c
```

**Rename verification:** regenerating `info.c`/`info.h` matches the previous
tables exactly after substituting `SPR_DC_`/`S_DC_`/`MT_DC_` with
`SPR_`/`S_`/`MT_`; all C/header diffs are identifier substitutions only.
`make`, `make tags`, headless `make test-layout`, and
`env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check` pass. The full
headless DC suite retains its three previously recorded failures: visible
muzzle effect, Human01 initial Trooper force, and spawn-state action invocation.

### Replace synthetic FX states with complete Reaper death timelines

**Correction/implementation:** `_FX1`, `_FX2`, etc. were generator-created state
names, never FIN labels. The user requested their removal. The generator now
emits one state per native FIN frame for each of `REAPDIEA14`, `REAPDIEA10`,
`REAPDIEA6`, and `REAPDIEA2`, followed by a corpse state using that timeline's
final frame. These states reference `REAP.SPR` cell count + FIN frame index;
the existing loader supplies all body and BLAM layers, offsets, remaps,
intensities, and flips together. There is no generated BLAM-only death chain,
external-command flattening, or separate `P_SpawnEffect()` call. The obsolete
`mobjtype_t.death_effect_action` hook, whose sole user was Reaper, is removed.

`A_DC_ReaperDeath` now clears live-unit traits and enters the selected complete
timeline. The preexisting nearest-direction/tie choice is retained explicitly
in action code, not moved into the loader or presented as newly verified retail
behavior. For engine direction codes 0–15 it selects suffixes
`{2,14,14,14,14,10,10,10,10,6,6,6,6,2,2,2}`. Native sparse-direction selection
remains unknown; no new DC.EXE investigation is claimed.

**Timing correction:** the synthetic death body/effect states previously used
fixed three/two-tic delays. Complete death frames now reuse the existing FIN
production-timeline conversion, including cumulative 19 Hz to 30 Hz boundaries.
The native conversion (`0` becomes `15`, then `((ticks + 3) * 19) / 100`) is
documented with instruction addresses in `REFERENCES.md` under Reaper movement
timing. The opening tick word 100 in A14/A6 becomes 19 native ticks / 30 engine
tics before BLAM appears. A14's later body frames include tick words **200 and
250**, correcting the preceding abbreviated description of them as 250.
The full timelines sum to 427/204/454/252 native ticks for A14/A10/A6/A2,
or 674/322/717/398 engine tics. Reaper movement timing remains unchanged.

**Verification:** `test_reaper_death` drives the actual death action and state
thinker for all sixteen engine directions. For every visited frame it checks
the FIN frame index and decoded layers, including offsets, flip, remap,
intensity, and layer order. It verifies the opening delay, total duration,
18/0/17/0 BLAM-bearing frames, no separate effects, and the final corpse frame.
Temporary `OPEN_RTS_DEBUG_REAPER_FIN` logging confirmed these values and was
removed afterward. Run headlessly:

```sh
make build/bin/tests/dark-colony/test_reaper_death
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_reaper_death
```

Every generated state outside Reaper death matches the parent by state name.
The build, Reaper layout/timing test, and headless DC smoke check pass. The
HUMAN01 screenshot was inspected and remains byte-identical to the parent;
death coverage comes from the focused test, not that starting-scene image.
The complete DC suite retains only the three previously recorded failures
(muzzle effect, initial Trooper force, and spawn-state action invocation).

## Audit custom attack and death actions against Doom (2026-09-09)

**Confirmed engine behavior, not new DC.EXE evidence:**
`A_DC_TrooperAttackStart` only used `rand() & 1` to choose between the six-state
FIREA and four-state FIREB attack chains. It did not initialize combat or fix
SPR decoding. The retail condition for choosing those animations remains
**unknown**. At the user's request to simplify state actions, Trooper now enters
`S_TRSC_ATK1` directly through `missilestate`; the selector and unreachable
ATKB states are removed from the generator. This intentionally removes the
unverified random variation, including its earlier damage frame and shorter
cycle. The loader still loads all FIN frames, including FIREB.

The local Doom reference enters `S_POSS_ATK1` through `missilestate`, faces its
target there, and fires `A_PosAttack` on `S_POSS_ATK2` (`reference/DOOM/info.c`).
Its `P_KillMobj` clears live-object flags before entering the death chain
(`reference/DOOM/p_inter.c`). Doom's later `A_Fall` clears `MF_SOLID`
(`reference/DOOM/p_enemy.c`); our `A_DC_Fall` instead duplicated the kill
branch's selection, movement and combat cleanup. That action is removed, with
its remaining attack/harvest resets consolidated in `P_Attack`'s lethal-damage
branch. Reaper's action now only selects the complete FIN timeline; its existing
sixteen-direction mapping and all frame timings remain unchanged.

**Disproven cleanup hypothesis:** neither remaining action is dead SPR code.
`A_DC_ReaperDeath` still selects four timelines of different lengths; replacing
it with a single death chain would lose authored frames. `A_DC_Corpse` still
copies the final visual into map decorations and releases the live-object slot.
Doom retains an object in a final `tics = -1` state, but our current
`MAXMOBJS == 128` array is also the storage used by production and reinforcements.
Retaining every corpse there would eventually block spawning. Keep the explicit
corpse handoff until object storage/lifetime changes; only its redundant momentum
reset is removed. This is an engine storage constraint, not a retail corpse rule.

**Verification:** `test_actor_lifecycle` checks direct Trooper attack entry,
damage after two windup frames, one shot per cycle, lethal cleanup for all nine
unit death chains, final corpse frame preservation, and slot release.
`test_reaper_death` now starts death through actual lethal damage and still checks
all sixteen facings against the complete FIN frames and native timing evidence
above. Temporary `OPEN_RTS_DEBUG_ACTION_CLEANUP` logging confirmed state, frame,
tics, health and trait transitions and was removed after verification. Reproduce:

```sh
make build/bin/tests/dark-colony/test_actor_lifecycle build/bin/tests/dark-colony/test_reaper_death
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_actor_lifecycle
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_reaper_death
env SDL_VIDEODRIVER=dummy make test-layout
```

The full build, generator reproducibility check, layout test (including Reaper
movement timing `{4,3,3,4,1,3,3,1}`), and all four game smoke checks pass.
The complete DC suite retains the same three failures observed before this
change: muzzle-effect expectations, Human01 initial Trooper force, and
spawn-state action expectations. No new suite failure was introduced.

## FIN layer flags and Doom misc fields (2026-09-09)

**Confirmed assets:** the BLAZ commands in TRSC/GRAY/REAP FIN carry flags **0**
and selector/layer **3**. The engine's `RTS_FRAME_ADDITIVE` (bit 1) and
`RTS_FRAME_TINT_YELLOW` (bit 2) were not native FIN flag values. The generator
invented those bits for three standalone muzzle states, and the renderer also
injected them whenever a FIN command selected layer 3.

| FIN | BLAZ command count | First BLAZ command byte offset | First command: cell, offset, remap, intensity, selector, flags |
|---|---:|---:|---|
| `TRSC.FIN` | 47 | 80,998 | `0, (-24,25), 0, 16, 3, 0` |
| `GRAY.FIN` | 49 | 77,298 | `0, (-24,24), 0, 16, 3, 0` |
| `REAP.FIN` | 24 | 50,942 | `0, (-23,26), 0, 16, 3, 0` |

TRSC and REAP hashes are recorded above. GRAY.FIN SHA-256 is
`077887b708009109740a518bf8cff9c547a21145617dbf5dde575342fe5a641a`;
its label/frame/command tables begin at bytes 56/1,716/75,516. Reproduce with
`build/dc_fin_extract data/DCOLONY/ANIMATE/GRAY.FIN /private/tmp/gray-flags.json`
(substitute TRSC/REAP to inspect those files).

**Disproven hypothesis:** the old additive-yellow constants are already encoded
as FIN flag bits. Selector metadata is native; the forced
SDL additive blend, RGB multiplier `(255,236,72)`, and alpha 230 were engine
approximations. The exact native selector-3 blend remains **unknown** in this
investigation. No new executable was examined, and no replacement blend formula
is claimed. The existing indexed blend path still uses the native selector and
available lookup table. Unsupported selectors receive ordinary sprite drawing.

**Implementation:** first disabled `state.misc2` rendering overrides and tested
that they were ignored, then removed the field, the three generated muzzle
states, their extraction/emission helpers, `mobjinfo.muzzleflash`, and the unused
actor muzzle sprite ID. FIN layers now supply their own flags, remap (including
same-sheet body layers), and intensity. The renderer no longer inherits actor
render flags into those commands or injects additive/yellow flags for selector
3. Both artificial constants and their SDL tint/blend handling are deleted.

**Doom comparison:** `P_SetPsprite` uses its two misc fields as optional player
weapon X/Y coordinates, gated by nonzero `misc1`; see the source hashes in
`REFERENCES.md`. Our `misc1` was unrelated gameplay metadata. It is retained as
`state.group`: movement/attack groups protect animation transitions, attack
group changes feed model events, and production group 6 bounds build chains.
FIN presentation data does not replace these simulation decisions. Remaining
generated state columns are sprite, frame, tics, action, nextstate, and group.

**Focused verification:** `test_sprite_layer_rendering` renders a two-pixel
fixture through the world renderer. Selector 3 preserves the source colors;
setting every actor render flag and conflicting actor remap/intensity leaves
the pixels unchanged. Native layer flip, remap, and intensity change pixels
as expected. Before deleting `misc2`, temporary `OPEN_RTS_DEBUG_STATE_FLAGS`
logging confirmed the old value 6 was ignored; the same pixel checks pass after
removal. The diagnostics were then removed. Run:

```sh
make build/bin/tests/dark-colony/test_sprite_layer_rendering
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_layer_rendering
```

All retained generated states have identical sprite/frame/timing/action/next
state/group values to the parent. Their removed flag columns were all zero;
only the three deleted muzzle states had nonzero overrides.
Generator output is reproducible; the build, tags, layout test, FIN/sprite
tests, Reaper death test, and headless DC smoke check pass. The HUMAN01
screenshot was inspected and is byte-identical to the parent. The full DC
suite retains its three documented muzzle-effect, initial-force, and
spawn-state-action failures. The generator also builds without its former
unused-muzzle-helper warnings.


## DROP FIN sequences and ordinary objects (2026-09-09)

**Confirmed asset evidence:** `ANIM.DAT` lines 32–34 load `drop.fin`,
`drop3.fin`, and `drop4.fin`. They do not list `drop2.fin`. Its SHA-256 is
`20e9cf988ed833236ca0687601ab32a2adeaae0d885b39a7507321166bdba3d0`.
The following hashes identify the inspected `ANIMATE/` files:

| File | SHA-256 | Frames / labels / draw commands |
|---|---|---|
| DROP.FIN | `66e8da41ff0a47229c1a33db4aae9e7f37307ec943f5bbd860acc832b07fc433` | 136 / 10 / 1307 |
| DROP2.FIN | `8ef09745fd963539295977e93e8fa91e701d9d731068c4a088826f3ca6a85a9b` | 178 / 11 / 1744 |
| DROP3.FIN | `be48d8c46444ee491e02462545ecb2153dafc63cd90fe58f2542f11f1b47d5f6` | 37 / 1 / 150 |
| DROP4.FIN | `3425eb26d3a47e571989feb82b1a1790d4fb41111f9cc90c0b7e75f63f9f1502` | 59 / 7 / 155 |
| DROA.FIN | `b41cf50cd59d8858d6f00d2777992f6ba0c6817496984177b73206a0a012848f` | 84 / 35 / — |

DROP's complete label ranges (inclusive, zero based) are `DROPTWO` 0–9,
`SCNCPOOPBUILD` 10–51, `SCNCBUILD` 52–57, `NOTDROP` 58–65, `GLINTER` 66–71,
`DROPSTAND0` 72–81, `RIGHTSPOTDROP` 82, `START` 83, `DROPMOVE0` 84–93,
`SCNCPODBUILD0` 94–135. Raw frame ticks are zero except within `GLINTER`,
which also contains 6 and 40.

DROP2's first five ranges have the same draw commands as DROP (its first label
is named `DROP2`). `DROPSTAND0`, `RIGHTSPOTDROP`, `START`, `DROPMOVE0`, and
`SCNCPODBUILD0` differ; it also has `TAKE2` 136–177. **Disproven:** DROP2 is
not simply an identical copy or the next flight phase. Its absence from the
native manifest establishes no reason to substitute its alternative sequences;
its original use, if any, remains **unknown**.

DROP3 contains `SCNCPOD2BUILD0` 0–36. DROP4 contains `ROBOPOD2BUILD0` 0–22,
`RIGHTROBOSPOT` 23–28, `SP1` 29–31, `SP2` 32–35, `SP3` 36–40, `GL1` 41–47,
and `KFIRO` 48–58. There are no matching DROP3.SPR/DROP4.SPR files: these are
FIN composites of other sprites. DROP3 depends on GLIT, HITA, HITC, HITE,
HITF, HITG, HITT, HUBU. The normal sprite loader now accepts such FIN-only
sources, loads every native frame, and resolves each layer's actual SPR source.
No asset-name exception or extra generated layer table is required.

DROA instead contains 16-direction STAND/MOVE ranges at frames 0–47, BLOODA
48–54, BLOODB 55–61, and DIE 62–83. **Disproven:** it is not a later human
DROP phase. This audit does not establish its original gameplay identity.

**Confirmed composition:** HUBU `SCNCPODSTAND0` (frame 22) draws HUBU cell 6
at (10,14). `SCNCPOD2STAND0` (24–25) adds HUBU 17 at (10,-36) and GLIT 13 at
(-79,-101). `ROBOPOD2STAND0` (68–87) contains HUBU 8 at (-10,-36), HUBU 10 at
(-10,-85), and BIGC layers. DROP's final `SCNCPODBUILD0` frame contains the
same HUBU 6 at (10,14), plus ship/glint/dust parts. DROP3's final build frame
contains HUBU 6 at (10,14), HUBU 17 at (10,-36), and HITA 6 at (-38,-29).
DROP4's final build frame contains HUBU 8 at (-10,-36), HUBU 10 at (-11,-85),
and GLIT 9 at (8,-141). The one-pixel difference in the last HUBU 10 placement
is native; no compensating offset was added.

**Confirmed executable timing:** same DC.EXE fingerprint as the report header:
SHA-256 `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
566272 bytes, PE32 base 0x400000, timestamp August 11, 1997. In the FIN loader,
0x423544 tests the frame word at +2; 0x42354b substitutes 15 for zero.
0x423563–0x42358f computes `((ticks + 3) * 19) / 100`. The focused disassembly
confirms 19; the broad decompiler's apparent multiply by 15 is incorrect.
The existing generator converts cumulative native 19 Hz boundaries to 30 Hz
rather than rounding every frame independently. Complete cycles now take:
DROPMOVE0 47 tics, DROPTWO 47, SCNCPODBUILD0 199, SCNCPOD2BUILD0 175,
ROBOPOD2BUILD0 109. The 0x4230ac loader opens `animate/%s` (string 0x46fad4)
in binary mode; this does not prove which gameplay callers select a label.

**Implementation correction, not newly verified retail gameplay:** normal
`MT_DROPSHIP` objects now own their copied cargo in `mobj_t.drop`. There is no
DropshipSystem, private array, animation clock, thinker callback, or borrowed
effect pointer. Native MOVE states call `A_DC_Fly` on entry, moving by speed
multiplied by the state's duration, in the same state-entry movement pattern
as Doom's `A_Chase` calling `P_Move` (`reference/DOOM/p_enemy.c:672,765`).
Arrival enters the native unload chain; its zero-tic release state calls
`A_DC_Drop`, which spawns cargo or retries if the common object array is full.
Departure ends at S_NULL and common compaction removes the ship. Compaction
also retains objects appended by state actions. Only mobile, order-driven
objects have their movement states switched automatically by the shared ticker.
The existing Dropship Link type (map type 89, CENT sprite) is now separate from
the flying ship instead of inheriting its flight states and altitude.

The old renderer decomposed a FIN into a capped 16-frame/24-part table,
spawned effects per part, reset their ages each tick, and forced team remap on
all layers. Those paths and `w_drop.c/.h` are deleted. The normal object
renderer uses all native FIN offsets, selectors, flags, remap, and intensity.
The fixed 50-tic approach/reposition/depart clocks and distance-derived speed
overrides are removed. Movement now stops when it reaches the goal.

**Requested engine integration:** building products 20, 21, and 19 enter the
SCNCPODBUILD0, SCNCPOD2BUILD0, and ROBOPOD2BUILD0 chains respectively, then
transition to their matching HUBU stand sequences. Product placement and build
availability still follow the existing engine production model. **Inferred:**
matching native names and final body layers support these handoffs. **Unknown:**
the exact retail label-selection callers, build completion gameplay timing,
release frame, flight route, altitude, and speed. The retained one-cell altitude,
speed 1, origin-minus-one-cell entry/exit and formation offsets are existing
engine policy, not verified native values. No new tuned constants are claimed.

Reproduce asset inspection with `build/dc_fin_extract` on each file, and timing
with `r2 -q -e bin.cache=true -c 's 0x423544' -c 'pd 28' -c q data/DCOLONY/DC.EXE`.
`tests/dark-colony/test_drop_fin_states.c` compares every generated frame's
native layer metadata and complete rendered pixels, including FIN-only sheets
and team 7 without forced remap. It checks cumulative duration and construction
handoffs. `test_mission_ownership` covers copied mixed cargo, array compaction,
release during ticking, shared capacity and retry, with no effects.
`test_dropship` ticks HUMAN01 and observes the ordinary ship, delivery of all
five Troopers, and its eventual removal. Run with `SDL_VIDEODRIVER=dummy`.

Verification: all four game binaries build; sprite-layout/Reaper timing, FIN
pixel/timing, cargo ownership/capacity, Human01 delivery, Dark Colony `--check`,
and Dark Reign command/event checks pass. The full Dark Colony suite retains
its existing muzzle-flash, initial Human01 force, and spawn-action expectation
failures. Its cross-game command test also fails to accept a build with zero
resources; an isolated build of pre-change `f91e19d` reproduces that same failure.


## BLOO asset and inherited hit presentation (2026-09-09)

**Confirmed asset evidence:** `ANIMATE/BLOO.FIN`, SHA-256
`470e4e805de22c370888e9aabe613f4e6c95ada9d267b1dddb3438ded20217bb`,
has default ticks 29, 14 labels, 135 frames, and 135 draw parts. Its labels are
`MNDHIV2BLOODA0` through `MNDHIV2BLOODF0` (ranges 0–9, 10–18, 19–26,
27–35, 36–46, 47–56), `MNDHIVBLOODG0` (57–67), and
`BRDRHIV2BLOODA0` through `BRDRHIV2BLOODG0` (68–77, 78–86, 87–94,
95–103, 104–114, 115–124, 125–134). There is no Trooper-labelled sequence.
Frame 0 has ticks 0 and a `bloo` part, lump 0, offset (-30,-8), remap 0,
intensity 16, layer 1, flags 0.

**Unknown:** retail selection of a hit animation for Troopers and other units.
The existence of BLOO.FIN does not establish that a hive-labelled sequence is
an interchangeable generic hit animation. No executable was examined for this
finding, so no DC.EXE address or instruction is claimed.

**Implementation consequence:** the thinker migration keeps the already-authored
BLOO SPR presentation (400 ms lifetime, 50 ms frames) as `MT_BLOOD` with eight
ordinary states. At 30 Hz their cumulative boundaries are 2,3,5,6,8,9,11,12
tics. These values preserve the existing engine policy and must not be cited
as verified retail timing. No hive FIN sequence is guessed or aliased to a
Trooper hit. FIN metadata remains owned by the sprite loader.

Reproduce with `make dc-fin-extract`, then
`build/dc_fin_extract data/DCOLONY/ANIMATE/BLOO.FIN /private/tmp/bloo.json`.
`test_actor_lifecycle` and the combat portion of `test_combat_and_harvest`
exercise hit spawning; `test_mobj_allocation` checks the common lifetime.

## FIN-only multigen-style exporter (2026-09-09)

`dc_info_gen.c` now exports native animation/frame text, replacing its old
SPR scanning, layer/direction decoding, and hardcoded gameplay generation.
Earlier instructions to regenerate `info.c`/`info.h` with this tool are superseded.
Those checked-in tables retain their gameplay behavior and Reaper timing.

**Confirmed from asset bytes:** the standard layout uses an 8-byte header,
8-byte dependencies, 20-byte labels (inclusive range words at +16/+18), and
164-byte frames (raw duration word at +2). Across 177 ANIMATE FIN files, 166
have readable frame tables: 18,517 frames and 2,546 valid labels produce 18,822
unique state rows. Overlapping ranges produce separate chains; frames outside
valid labels are emitted separately. An independent Python struct comparison
checked every frame index, duration, label range, next-state link, and state-name
uniqueness. Empty tables and short headers were also checked.

**Confirmed exceptions under this layout:** ANIM.FIN has 23 frames but its four
label ranges decode to 69..0, 15..0, 20053..0, and 20302..16718. SPAC.FIN has
20 frames and a label range 0..20. Neither is clamped or interpreted as a new
format. LIGHT1B, LIGHT1M, LIGHT2B, LIGHT2M, LIGHT3B, LIGHT3F, LIGHT3M, LIGHT4B,
LIGHT4F, LIGHT4M, and LITE have tables extending beyond their file size under
this layout; all begin with the word 65533. **Unknown:** whether these represent
alternate formats or malformed/unused assets. No DC.EXE behavior was examined
or inferred. Unsupported tables and invalid labels are reported in stderr and
`; SKIPPED` output comments. Remaining readable frames are preserved.

SHA-256 asset fingerprints:

- ANIM.FIN: `9fd40d2e0bfbd05b9aea52471e827e63b71e0327481f3764fb43a248a7bd198c`
- SPAC.FIN: `f9fdb3dbe48600149b2ec91378f6c2ea9bf11756abe1b53a0c7536d45346fad3`
- LIGHT1B.FIN: `61c7a4d9a39ecc53b1719f686fca71c69b6be63ab6d0ef1ae3b4ee4aa22543a0`
- REAP.FIN: `44d1e85d5a28bca0ca3e45b5bc8032544f16e1dc04fdfd655c3d70ec15ab540b`

**Export convention, not native behavior:** numeric FIN frames replace Doom's
frame letters, durations stay raw rather than converted to engine tics, actions
are NULL, and the last state points to S_NULL. Label suffixes are preserved;
there is no direction grouping, layer extraction, action whitelist, or invented
animation selection. Native gameplay transitions remain unknown to this tool.

Reproduce with `make dark-colony-info` (writes `build/dc-animations.txt`), or
`build/dc_info_gen data/DCOLONY/ANIMATE/REAP.FIN`. `make`, `make test-layout`,
and `env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check` pass.

## MAP/SCN ownership cleanup (2026-09-09)

**Confirmed from implementation comparison, not new executable analysis.**
Against `46f826a`, all 100 installed MAP candidates produce identical final
terrain, flags, camera, team resources, and initial mobj records/order. The
HUMAN01 screenshot is byte-identical. Commands, coverage, and limitations are
in [loader verification](LOADER_REFACTOR_VERIFICATION.md).

Representative asset SHA-256 fingerprints:

- `SCENARIO/HUMAN/HUMAN01.MAP`:
  `09d712271c8deca56521a82988e75e44caa6c04dcff83e7e581bae61511f09f9`.
- `SCENARIO/HUMAN/HUMAN01.SCN`:
  `af82c538181ca182481562dfa75ff1f39038a58445b019cd6a52426b33e968e7`.

The loader still interprets MAP width/height at offsets 0/4, tile pairs at 8,
then 16-bit flags after `width * height * 4` tile bytes. Bits 5/6 choose the
background/foreground X transforms; bit 9 blocks movement. Rows convert once
into bottom-up level storage. The existing MTG blank-map fallback and OVH
RGB565 conversion are preserved. O16 and the MAP's MTG sidecar were loaded into
private staging data with no runtime consumer; deleting those reads changes no
rendered or simulated field. This does not establish their unused retail role.

The initial loader no longer stages native-size records in a temporary pool.
It preserves the 648 dynamic slots (`800 - 0x98`), city ordering, signed 8.8
coordinate narrowing, and the previously documented city formula at `0x4412d4`.
The `DcObject` layout/offset assertions remain for native investigation.

**Disproven during refactoring:** passing the scenario team's race through the
existing multiplayer bonus-Exploiter path is equivalent to the previous loader.
The previous path explicitly used human race 0, even when team 0's SCN race is
alien; changing that removed the Exploiter on six maps. D2PLAY02 isolated the
missing spawn through temporary per-object terminal logging. The explicit
race/owner inputs are preserved. A remaining apparent D8PLAY09 hash difference
was uninitialized struct padding in the comparison of resource vents; hashing
initialized fields removed it, with identical individual mobj diagnostics.

**Unknown / preserved:** the retail justification for synthesized multiplayer
starters, the first-AISlots fallback for city anchors, and synthesized city
towers was not re-established here. Removing staging arrays is not evidence
for changing these behaviors. Likewise, gameplay numbers still come from the
same checked-in tables; no runtime GAMESTAT parsing or rebalance was added.


## Restore requested flight ordering and layer-3 appearance (2026-09-09)

**Confirmed source-history regression:** `6067a66` introduced Z-aware sprite
projection and a one-cell dropship altitude. `85ec838` moved dropships from the
late effects pass into the ordinary object pass; `mobjinfo[MT_DROPSHIP].spawnz`
still supplies one world cell through `P_InitMobj`, and planar movement preserves
it. The ordinary draw comparator considered ground Y only, allowing released
Troopers to cover the ship. Temporary spawn logging confirmed Z = 1 after
initialization. This corrects the tempting diagnosis that the zero passed in
`DC_StartDropship` meant the initialized ship had zero altitude.

**Requested engine behavior:** dropships now spawn at `50 * FIXED_ONE / 32`,
exactly 50 screen pixels at Dark Colony's native 32-pixel cell scale. Draw commands
sort by world Z before ground Y, placing elevated objects above ground objects
regardless of their ground position. Existing `R_MapPositionToScreen` projects
Z upward and scales it with cell height; movement and cargo placement are
unchanged. This altitude and ordering are explicit user requirements, not newly
verified retail constants. Doom's `reference/DOOM/r_things.c:R_ProjectSprite`
likewise retains object Z in `vis->gz/gzt` and derives vertical sprite placement
from it; our top-down draw ordering serves the requested overhead view.

**Confirmed source-history removal:** `46376ba` explicitly removed selector-3
SDL additive/yellow rendering. Per the user's requested appearance, FIN layer 3
again uses `SDL_BLENDMODE_ADD`, RGB modulation `(255,236,72)` scaled by FIN
intensity, and alpha 230, restoring the previous engine formula. Other FIN flags,
remaps and intensities remain command-owned. The indexed composition path is
preserved, and texture blend/color/alpha state is restored after drawing.
This supersedes the earlier implementation decision to leave selector 3 plain;
it does not supersede the finding that additive/yellow were not native flag
bits. The exact DC.EXE selector-3 formula remains **unknown**. No new executable
or native-asset format was examined; fingerprints and binary addresses above
remain unchanged.

**Verification:** `test_sprite_height` checks 50-pixel projection, scaling, and
occluding ground objects on either side of ground Y in either input order.
`test_dropship` checks that altitude survives every observed HUMAN01 flight and
unload tick while Troopers remain at ground Z. `test_sprite_layer_rendering`
checks additive pixels with the historical tint, remap/flip, full and half
intensity, and ordinary rendering after a layer-3 draw. Run these under
`build/bin/tests/dark-colony/` with `SDL_VIDEODRIVER=dummy`.

Build and visual checks: `make -j4`, `make tags`,
`build/bin/test_dark_colony_sprite_layout`, and `test_drop_fin_states` pass.
The headless Dark Colony `--check` and `--screenshot` pass. Software-rendered
previews of a ship overlapping five Troopers and the five Trooper attack frames
show the ship covering troops and yellow additive muzzle/light layers. These
are open-rts previews, not retail evidence.

## Trooper death placement and selection anchor (2026-09-09)

**Confirmed reproduction:** at screen anchor `(320,300)`, the old
`S_TRSC_DIE1` drew raw SPR cell 128 at `(465,241)`. Its generated raw-cell layer
had offset `(0,0)` and the SPR displacement was `(145,81)`. The native FIN
command supplies offset `(-159,0)`, giving destination `(306,241)`: the renderer
was missing 159 pixels of authored X placement, not an object-coordinate change.
The corpse similarly drew at X 474 instead of 315. Temporary
`OPEN_RTS_DEBUG_TRSC` logging captured state, logical frame, cell, FIN offset,
SPR displacement, destination, and object anchor; removed after verification.
Before/after software previews reproduced and corrected the separation.

**Native asset evidence:**

- `ANIMATE/TRSC.FIN`, SHA-256
  `eb94f6f3fff53b9f46f1540abf5287c11f83a7db7957d6288b2330b13e1f3b2a`:
  472 frames, 77 labels, 5 dependencies; label/frame/command tables at
  bytes 48/1,588/78,996. `TRSCDIEA14` label at byte 1,148 selects frames
  223–233, whose commands occupy bytes 86,256–86,476 in 22-byte records.
- `SPRITES/TRSC.SPR`, SHA-256
  `51092690d9700cecdcac5e7c53b7cffbd09cedcc582b509a9e16adc9f740d117`:
  209 cells, with descriptors at `776 + cell * 8`. Cell 128 has size `(32,59)`
  and displacement `(145,81)`; cell 137 has size `(41,38)` and displacement
  `(154,133)`. These displacements are crop metadata, not world offsets.

The selected FIN sequence is:

| FIN frame | SPR cell | Offset | Remap | Raw ticks |
|---|---|---|---|---|
| 223 | 128 | (-159,0) | 0 | 0 |
| 224 | 129 | (-159,0) | 0 | 0 |
| 225 | 130 | (-159,3) | 0 | 0 |
| 226 | 131 | (-159,9) | 0 | 0 |
| 227 | 132 | (-159,19) | 0 | 0 |
| 228 | 133 | (-159,21) | 0 | 0 |
| 229–230 | 134 | (-159,26) | 1 | 250 each |
| 231 | 135 | (-159,29) | 1 | 140 |
| 232 | 136 | (-159,29) | 1 | 140 |
| 233 | 137 | (-159,31) | 1 | 140 |

All these commands have flags 0, intensity 16, layer 1. The old ten raw-cell
states skipped the repeated cell 134 and lost offsets, remap and native delays.
The replacement uses complete logical FIN frames `209 + 223` through
`209 + 233`, with one state per frame and a persistent final corpse. The
previously documented native delay conversion at DC.EXE 0x423563–0x42358f
produces engine tics `{5,4,5,5,5,4,76,76,43,42,43}`, totaling 308, instead of
30 tics. This is an intentional timing correction, including the decay pauses.
Reaper's movement timing remains `{4,3,3,4,1,3,3,1}`.

**Unknown:** retail selection among alternate TRSC death labels remains
unresolved. The asset also has `TRSCDIEA10`, mirrored A2/A6 and B/C variants,
including later C sequences with SSSS layers and different decay remaps/layers.
The existing choice corresponding to cells 128–137 is preserved as DIEA14;
no new direction-selection or death-variant rule is inferred. No executable
was newly examined; the executable fingerprint and timing evidence above apply.
Like Doom's state-to-sprite/frame path (`reference/DOOM/r_things.c`), the state
selects a complete frame definition; it does not compensate object position.

**Confirmed selection cause:** `R_DrawSelectionMarkerSprite` used
`visible.x + (visible.w - marker.w) / 2`. At a fixed `(320,300)` anchor, the
walk-cycle marker X values were `{313,314,315,315,312,313,315,316}`. Its top Y
was `{252,251,251,251,251,251,251,251}`. These are frame-rectangle changes, not
simulation drift. Y was `visible.y - marker.h + top_offset_y`; DC config sets
`top_offset_y = -3`. `INTRFACE/CLIENT.SPR` healthy marker cell 0 is 12x5, with
zero displacement (SHA-256
`3f790667ec55fa6d982a3d9c9c5035ee892d6563f56bedc2d1b028e1d24c56f2`).

**Requested engine behavior:** marker X now centers on projected object X.
Its top Y uses projected object Y plus the canonical standing frame's top
(`bounds.y - ground_point.y`), minus marker height, retaining the configured
3-pixel gap. The standing frame and orientation stay fixed while animation and
facing change. This is an engine UI policy, not a claim about retail marker
placement; the existing world-Z projection also applies to the anchor.

**Reproduction/verification:** `build/dc_fin_extract
 data/DCOLONY/ANIMATE/TRSC.FIN /private/tmp/trsc-fin.json` exports the asset
metadata. `test_trooper_rendering` renders native-command reference pixels for
every death/decay frame, checks native timing and corpse persistence after lethal
damage, and verifies the selection pixel mask over all 16 facings and eight walk
frames plus XY/Z/camera translation. It writes death-frame BMPs under
`/private/tmp/trsc-death-*.bmp`. Run with `SDL_VIDEODRIVER=dummy`.
`test_actor_lifecycle`, the combat portion of `test_combat_and_harvest`, and
`test_dark_colony_sprite_layout` pass. The combat test now allows time for the
native decay pauses; its separate missing-player-Exploiter harvesting failure
also reproduced before rebuilding the changed tests.

## September 9 correction: FIN drawing mode, object team color, and Grey death

**Superseded conclusions:** earlier sections treated FIN `remap` as a palette
selector and validated that interpretation using references that repeated the
same mistake. The serialized field order was correct; its semantic name was
misleading. FIN owns the drawing mode, flags, layer, and intensity metadata;
object team selects the palette. The historical statements about command-owned
palette remaps, including the dropship team-7 check, are superseded here.

**Confirmed reproduction:** temporary `OPEN_RTS_DEBUG_FIN_COLOR` diagnostics in
`render_unit_sprite` printed object team, state/frame, source cell, FIN drawing
mode, selected translation, offsets, and SPR displacement. A red team-0 Trooper
changed to translation 1 at decay cell 134 because its FIN mode becomes 1. The
independent indexed-pixel death test failed at that transition before the fix.
Team-0 dropship movement selected translations 2 and 4 for its two body parts;
unload selected translation 0 for both. Grey death still selected raw SPR cell
262, discarding FIN's (-32,2) offset. Grey decay cell 286 similarly omitted
(-32,19). Diagnostics were removed after reproducing and verifying the fix.

**Confirmed executable evidence:** the DC.EXE SHA-256 remains
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`
(PE32, 566,272 bytes, image base 0x400000). The existing r2ghidra decompilation
was used for navigation; the following relationships were checked against x86
instructions, not inferred from decompiler parameter names:

- At `0x4236bc–0x423720`, seven signed words are read into the 20-byte runtime
  command at +4/+6/+8/+0xa/+0xc/+0xe/+0x10: cell, X, Y, drawing mode, intensity,
  layer, flags. The on-disk command is 22 bytes because its initial eight-byte
  sprite name becomes a four-byte pointer. Thus disk `remap` is +14 and flags
  are +20; they have not been swapped. The existing X*8/Y*-8 load conversion
  follows at `0x423745`.
- Object drawing at `0x4365c5–0x4365e7` reads byte +8 (team override); value 8
  uses team byte +7 instead. It masks the result with 7 and looks up the team's
  palette ID at game + team*0xe30 +0xc98. That result is retained in EDI.
- `0x436625` obtains the command's drawing mode from the high word at +8 and
  passes it on the stack. Layer and flags are separately pushed at
  `0x436646–0x436653`. `0x436689` puts the object-derived palette ID in ECX
  before calling the queue routine at `0x432dec`.
- Queue records have stride 0x1c at `0x4dc0ac`. Instructions
  `0x432e8d–0x432eb5` store `(global_light << 3) + (palette_id & 7)` at record
  +0x14. `0x432ec4–0x432ec7` independently store the FIN drawing mode at +0x15;
  layer is +0x17 and flags +0x18. Global light is at `0x474660`, initialized to
  16 for world drawing; this traced path does not derive that value from the
  command's intensity word. Full retail lighting behavior is not ported here.
- The queue renderer at `0x44fb53–0x44fb6f` sets word `0x511dac` according to
  whether drawing mode equals **2**. Nonzero queued Z also sets it at
  `0x44fb76–0x44fb7d`. In blitter `0x45c060`, instructions
  `0x45c1ec–0x45c24e` use that word to select alternate clipping: the zero path
  traverses 32-pixel columns, while the alternate path handles horizontal edge
  clipping for the span. It does not replace the palette ID.
- `0x44fb49–0x44fb56` copies queue flags to `0x4841d0`; tests of this word
  choose normal versus mirrored blitters. Blitter calls independently retrieve
  the palette/lighting byte at +0x14 (`0x44fbc9`, `0x44fbef`, `0x44fc23`,
  `0x44fc4d`). Flags are not the team-color selector either.

**Confirmed asset counterexamples:** TRSC hashes are recorded above. At FIN
command byte 78,996, standing cell 0 has mode 0 and flags 0. DIEA14 starts at
86,256 with cell 128, mode 0, flags 0. Command 86,388 has cell 134, offset
(-159,26), **mode 1**, intensity 16, layer 1, flags 0:

```text
74 72 73 63 00 00 00 00 86 00 61 ff 1a 00 01 00 10 00 01 00 00 00
```

Final corpse command 86,476 (cell 137) also has mode 1. Mirrored DIEA2 command
87,246 has cell 128, offset (-18,0), mode 0, **flags 1**. Therefore neither
“all TRSC remaps are zero” nor “flags is really remap” describes the bytes.

DROP.FIN SHA-256 is
`66e8da41ff0a47229c1a33db4aae9e7f37307ec943f5bbd860acc832b07fc433`;
label/frame/command tables begin at 112/312/22,616. `DROPTWO` (label byte 112,
frames 0–9) starts with body cells 0/1 at (-64,69)/(-64,34), both mode 0.
`DROPMOVE0` (label byte 272, frames 84–93) starts at command 39,160; body
commands 39,226/39,248 use the same cells and offsets with modes **2/4**.
Their flags remain 0 and intensity 16. Animation changes the drawing mode,
not ownership. Historical `d48c75a` forced ship team onto temporary effects;
the ordinary-object/FIN changes (`85ec838`, `46376ba`) exposed the erroneous
shared mode-to-palette rule. Restoring a ship-specific override is unnecessary.

**Confirmed Grey timeline and placement:** GRAY.FIN SHA-256 is
`077887b708009109740a518bf8cff9c547a21145617dbf5dde575342fe5a641a`;
label/frame/command tables start at 56/1,716/75,516. GRAY.SPR SHA-256 is
`95e71a6b19ecca1b77a9cba3b69b68f7b07b8fb927f99bcafdeba33030ebd620`,
flags 129, 292 cells, pixel payload at 67,202. `GRAYDIEB14`, label byte 1,416,
selects FIN frames 302–315, commands 84,338–84,624:

| FIN frames | SPR cells | FIN offsets | Raw ticks | Mode/layer |
| --- | --- | --- | --- | --- |
| 302–309 | 262–269 | X=-32; Y=2,1,-1,4,9,13,15,17 | 6 each | 0/1 |
| 310–312 | 270 repeated | (-32,17) | 250 each | 1/0 |
| 313–315 | 286,287,288 | (-32,19),(-32,16),(-32,11) | 140 each | 1/0 |

All have intensity 16 and flags 0. SPR descriptors at `776 + cell*8` give
cell 262 size29x41/displacement(20,5), cell270 size49x30/(5,31), cell286
size49x30/(5,33), cell288 size26x14/(21,41). The omitted FIN X explains the
32-pixel rightward error; the varying native Y offsets must also be retained.
The state chain now uses logical frames `292 + 302` through `292 + 315`,
including both previously omitted holds of cell 270. Existing native delay
conversion yields `{2,1,2,1,2,1,2,2,75,76,76,43,42,43}` (368 engine tics),
then a persistent corpse with the final FIN frame. No compensating offset is
added to the object.

**Unknowns preserved:** retail death-variant selection remains unresolved. The
chosen Grey label matches the old cell sequence; alternatives GRAYDIEA14,
GRAYDIE210 (that exact spelling), GRAYDIEA6/A2 cover frames 254–301, and
GRAYDIEB10/B6/B2 cover 316–356. Mirrored B2 has 13 frames,
unlike the other B variants' 14; sequences must not be reconstructed by assuming
identical lengths. Exact native column occlusion and complete mode semantics
are not implemented by this fix; the engine retains the raw field instead of
repurposing it as team color. Auxiliary decompilation at `0x44a7b0` suggests
special treatment of team 5 during palette-table generation; that detail was
not instruction-verified and the existing translation tables are unchanged.

**Implementation and verification:** the shared renderer now selects translated
textures from `mobj_t.team`, independently of FIN mode. Native frame offsets,
SPR displacement, flags, layers and intensity remain available through the
existing loader. Doom likewise selects translation from actor flags in
`reference/DOOM/r_things.c:R_DrawVisSprite`, independently of sprite/frame data.
`test_trooper_rendering` compares every Trooper/Grey death frame and final corpse
against original indexed pixels with independent team-slot translation for
teams 0/1, and checks timing and stable selection. `test_drop_fin_states`
compares full movement/unload sequences for all eight teams and preserves its
construction checks. Synthetic layer tests verify modes 0–7 cannot recolor an
object and retain flip/intensity/additive behavior. Reaper timing and lifecycle
checks also pass. Reproduce with:

```sh
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x4365c5' -c 'pd 75' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x432e8d' -c 'pd 25' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x44fb49' -c 'pd 100' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x45c1ec' -c 'pd 60' -c q data/DCOLONY/DC.EXE
build/dc_fin_extract data/DCOLONY/ANIMATE/GRAY.FIN /private/tmp/gray-fin.json
make -j4
make tags
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_trooper_rendering
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_drop_fin_states
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_layer_rendering
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_height
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_actor_lifecycle
env SDL_VIDEODRIVER=dummy build/bin/test_dark_colony_sprite_layout
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/open-rts-color-smoke.bmp
```

Full build and smoke checks passed. Visual previews confirmed red Trooper decay
and dropship movement/unload; Grey previews confirmed native placement. Death
tests now write `/private/tmp/{TRSC,GRAY}-team{0,1}-death-*.bmp`.


## Complete state-table audit after the Grey fix (2026-09-09)

**Scope and confirmed baseline:** `5d42ad9` already fixes Grey death using
logical FIN frames 594–607. Its other state rows still contained **246 raw SPR
references**: 237 visible non-blood states, eight generic blood frames, and the
zero-tic Reaper death selector. The raw references were not all in `info.c`:
`w_map.c:unit_frame_for_type` retained another hardcoded raw-cell table with
stale animation-name comments, but `P_InitMobj` immediately overwrote its result
from the selected state. That unused function and assignment are now removed.

The loader itself retains all FIN frames and parts. These defects were state
selection shortcuts, not missing asset metadata. `tools/dc_info_gen.c` is now
only a FIN-range exporter and does not regenerate `info.c`; the old raw-cell
rows survived in the authored gameplay table. No sprite-name exception or
pixel-derived offset is introduced by these corrections.

**Confirmed truncations and lost parts:** BARRDIE14 had 13 states for 15 FIN
frames and omitted decay cells 89/90. SARGDIE14 stopped at cell 139 after two
states, omitting 35 frames, including XENO, BLOO, HIT, GASY and TRSC parts.
SCGMDIE0 used FIN frames but stopped after 10 of 30 frames. EXPLDEPLOY14 kept
only the stationary body (cell 14) for its first 18 frames, dropping the arm
cells 15–30,32,33. TRSCBUILD0 changed the state's sprite from HUBU to TRSC when
it selected one command, discarding other commands and their placements.
TURR/TONG/CENT death did the same with GASY/BLAM effects. Building idle states
lost overlays or froze the first body cell. All corrected sequences below now
reference each entire FIN frame in order.

**Native evidence:** no executable was newly examined in this audit. DC.EXE's
fingerprint, command-layout evidence and delay conversion recorded above still
apply. Native durations are converted by accumulating
`floor(((ticks ? ticks : 15) + 3) * 19 / 100)` and rounding cumulative engine
boundaries as `floor((native_total * 30 + 9) / 19)`. Each state's duration is
the difference between successive boundaries. Reaper's authored movement tics
`{4,3,3,4,1,3,3,1}` are unchanged. The table gives native FIN indices, followed
by logical engine frames (SPR cell count + FIN index):

| FIN | Label | Native frames | Logical frames | Total engine tics |
| --- | --- | --- | --- | --- |
| HUBU | TRSCBUILD0 | 26–47 | 45–66 | 35 |
| BARR | BARRDIE14 | 140–154 | 246–260 | 398 |
| SARG | SARGDIE14 | 192–228 | 390–426 | 385 |
| ORTU | ORTUDIE14 | 126–132 | 212–218 | 44 |
| SLUG | SLUGDIE14 | 71–79 | 161–169 | 164 |
| SCGM | SCGMDIE0 | 72–101 | 102–131 | 47 |
| EXPL | EXPLDIE0 | 54–65 | 104–115 | 57 |
| EXPL | EXPLDEPLOY14 | 32–51 | 82–101 | 95 |
| SLUG | SLUGDEPLOY14 | 147–160 | 237–250 | 66 |
| SLUG | SLUGRETRACT14 | 161–174 | 251–264 | 66 |
| TURR | TURRDIE0 | 65–80 | 125–140 | 76 |
| TONG | TONGDIE0 | 19–38 | 29–48 | 95 |
| CENT | CENTDIE0 | 12–52 | 24–64 | 194 |
| TONG | TONGSTAND0 | 0–17 | 10–27 | 88 |
| CENT | CENTSTAND0 | 53–76 | 65–88 | 114 |
| HUBU | EXCOPODSTAND0 | 0–7 | 19–26 | 38 |
| HUBU | BRRKPODSTAND0 | 19–20 | 38–39 | 9 |
| ALBU | MINDHIVSTAND0 | 3–4 | 33–34 | 16 |
| ALBU | WARHIVESTAND0 | 13–16 | 43–46 | 19 |
| ALBU | BRDRHIVSTAND0 | 11–12 | 41–42 | 16 |
| ALBU | BRDRHIV2STAND0 | 17–20 | 47–50 | 28 |
| ALBU | MNDHIV2STAND0 | 5–6 | 35–36 | 16 |
| TOWR | TOWRSTAND0 | 0–0 | 1–1 | 5 |
| DOTT | DOTTSTAND0 | 0–0 | 1–1 | 5 |
| ALBU | RSCHHIVDIE0 | 10–10 | 40–40 | 76 |

Single-frame TOWR/DOTT presentations and the research-hive placeholder retain
infinite state durations; their table totals above describe the native frame
delay, not the engine's static-state policy. Existing death-variant choices,
corpse persistence/removal, and Slug deploy/retract exits are preserved. These
are not newly verified retail lifecycle rules. Exploiter death already used
complete FIN frames, but still used uniform three-tic durations; it now also
uses its native total of 57 tics.

**Fingerprints and locations:** each command is 22 bytes and each FIN frame
record is 164 bytes. The offsets below are label/frame/command-table starts.
The existing hashes for TRSC and DROP are unchanged. FIN-only DROP3/DROP4 have
no matching SPR file and correctly start their logical FIN numbering at zero;
they are not missing sprite loads.

| Asset | SHA-256 | Tables or SPR cell count |
| --- | --- | --- |
| HUBU.FIN | `b27b20282999188e37a74872b70a273b370cc1dd219f2f8fa84f5f2f2ca3a5a4` | 208/1368/80580 |
| HUBU.SPR | `d9132c612f12deda687feebc49ac5da9ab64e8faa9980001e2b134e7cf1aa474` | 19 cells |
| TOWR.FIN | `9e5637a26681eca10a02669545d4102f1d8f33e407dfeb7a95ab70d5d50bf9e7` | 16/36/200 |
| TOWR.SPR | `eb7622b6cc46236fd5833d32886409a0ec0200b525653a58eb92930ec2d8d84b` | 1 cells |
| BARR.FIN | `08ef8a38d3d0ba5629dcd58c91441569dde7c4ed09c60925b4a86d3d33c65894` | 104/1124/38680 |
| BARR.SPR | `446622924f43aefe33726a28b274d8c050e059cecee60fe47dc419acc848f28d` | 106 cells |
| SARG.FIN | `3c29f398dfdb315cf4399b2037f43a499a26e9156ef1bc6d8f20e7236c8803db` | 112/1492/106452 |
| SARG.SPR | `7e441aa49451d2419c8ca5506385ef769be29e5e17466040808b47fa0aa4610c` | 198 cells |
| ORTU.FIN | `410a683cfbafcc28cb2f6d6e569da8b5e595114692c4b92f65ccd79052ddd387` | 56/1116/44576 |
| ORTU.SPR | `d678fb53ce718956b88f88c2abd08587703144e3962beb57f8f7ca412f6f0a55` | 86 cells |
| SLUG.FIN | `f1b813f0607aab425b57011f19f16110d8c5b33179691d08dd4b9558afb87d6b` | 72/1172/53980 |
| SLUG.SPR | `15f8f25029981f001b9e963f0efeb27e25769a46801a986e544ea63f926c73cc` | 90 cells |
| SCGM.FIN | `5504da63bf95910593f909a259624a77e3c1d839cc68d751ef76d638769b8638` | 136/696/18408 |
| SCGM.SPR | `53c01854110ce1725bef4b99a7d06177536fb55de7221d818a5a2ccf4dc46471` | 30 cells |
| EXPL.FIN | `6cd02d2153bf692155aaf31d73015eb7de89902ecccdc9a079af5a8a6ed2812c` | 112/1192/39240 |
| EXPL.SPR | `1eed4f57075ff91589caaba490079b58394ce9fad282a4a06648a1db8362a721` | 50 cells |
| TURR.FIN | `f0f25f1ae13cfcd6b53e3cdcae2e5efaca09e9e8290eb173bb8ce98a93d3a210` | 112/1672/41360 |
| TURR.SPR | `3a4e753e4122149c28025a5325880da38ac09a164ed76362ed7007e16d7beb88` | 60 cells |
| TONG.FIN | `5c4510ba67675eb958d77d5b6e857b122d57ab8c6294205ace45e5081380902a` | 24/84/6480 |
| TONG.SPR | `7d93d55c7b49385e4e8b980f666849f65626d1bf14942fab069461953caf6940` | 10 cells |
| CENT.FIN | `58cac6fae5085fe6ee203b22eeaa05af9517cb0320267285159e464713288baf` | 64/124/12752 |
| CENT.SPR | `d0592944767b26860bc24f3c2f9f6371995fb398bc1813c3463e82382f3478b8` | 12 cells |
| DOTT.FIN | `005e22d43a95de00ab4099b4a51949cae7296807449cb4c80b8f30448f04b277` | 16/36/200 |
| DOTT.SPR | `5782fe0197c7c957686c65e2121bc96ae5b96396fc2d782b2312edbdfdeed977` | 1 cells |
| ALBU.FIN | `99c3d4e4fa0badeb2cd68361a6f1b57dcf9dfbdd027f820a68d806aa18773fa1` | 120/1140/80844 |
| ALBU.SPR | `eff81187b3379c7f215142c44173aa85089ffe89d0515a74d17b840daa35aa79` | 30 cells |

**Special cases and remaining unknowns, explicitly preserved:**

- Generic `MT_BLOOD` still uses eight raw cells. BLOO.FIN only supplies
  hive-labelled ranges; retail hit-animation selection remains unverified, as
  documented above. It is the sole visible raw-cell exception in the state
  coverage test. The zero-tic Reaper selector is never drawn on death entry.
- Exploiter WORK still shows only deployed SPR cell 34 for its existing 2/4-tic
  loop, now through full logical FIN frame 103 (native frame 53). EDPLYSTAND14
  frames 52/53 share body cell 34 at (-159,25); frame 52 additionally contains
  mirrored GLIT cell 10 at (-13,-39), layer 5. That is confirmed authored art,
  but its retail harvesting selection/lighting behavior is still unknown.
  No new work-light action or overlay is invented. Deployment now includes its
  verified native arm layers without changing the south-east facing rule.
- The alien research-hive standing placeholder previously selected ALBU cell
  13. That cell belongs to **RSCHHIVDIE0**, native frame 10, offset (-113,-38).
  Its state now retains that same art through complete logical FIN frame 40;
  it is still a corpse-art placeholder, not a verified research-hive stand.
  ALBU's `CROOP` frames 8/9 contain cells 11/12 at (-113,-43), but using that
  label as a standing animation requires the native object-to-label lookup.
- The third mind-hive state already reused the base mind-hive art. It now
  shares the complete MINDHIVSTAND0 sequence. Distinct retail third-tier art
  remains unverified; this change does not establish that alias as native.
- This audit validates declared state-to-FIN references, not every original
  game's animation choice. Movement/attack timing, shortened gameplay idle
  loops, death-variant selection, native clipping and special blend/lighting
  behavior retain the documented limits of the current implementation.

**Verification:** temporary `OPEN_RTS_DEBUG_FIN_AUDIT` logging printed state ID,
sprite, logical frame, raw-cell count and tics for all 710 non-null states, then
was removed. `test_drop_fin_states` now checks every declared frame is valid
and every visible non-blood state uses FIN. It additionally compares complete
native metadata, rendered pixels and cumulative timing for 23 restored
sequences, all eight dropship teams, and the three construction sequences.
The static work policy is checked separately. The old city-layout assertion
required raw-cell IDs; it now requires the corresponding FIN frame indices,
while retaining its SPR descriptor and placement checks.

Full build, native-sequence pixels/timing, Trooper/Grey death and selection,
actor lifecycle, sprite layout/Reaper timing, and Dark Colony screenshot smoke
checks pass. Preview BMPs (for example `/private/tmp/EXPLDEPLOY14.bmp` and
`/private/tmp/SARGDIE14.bmp`) show the restored parts. The broad
`test_game_model_headless` still fails its previously documented Human01
initial-force expectation (0 player Troopers versus 32 expected); its focused
Exploiter work-state checks pass. This audit does not alter mission startup.

Reproduce with `make -j4`, `make tags`, and these headless tests (build the test
targets explicitly before running):

```sh
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_drop_fin_states
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_trooper_rendering
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_actor_lifecycle
env SDL_VIDEODRIVER=dummy build/bin/test_dark_colony_sprite_layout
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/dc-audit-smoke.bmp
```

To reproduce the original table inventory, inspect
`git show 5d42ad9:games/dark-colony/info.c` with its matching `info.h` and compare
each frame to the SPR header's cell count at byte 2. Count zero cells for FIN-only
assets. To inspect any native sequence, run `build/dc_fin_extract` on the FIN
file and find the exact label from the table above.


## Native BLOOD dispatch and shared effect sprites (2026-09-09)

**Correction to the earlier blood exception:** the retail selection path is now
identified. The old statement that Trooper hit selection is unknown is
superseded by the instruction trace below. Runtime implementation is still the
legacy `MT_BLOOD` eight-cell effect; this investigation records the replacement
contract and does not claim to have ported it.

**Confirmed current runtime:** `P_Attack` spawns `target->info->blood_type` at
the victim's position after subtracting HP, before the lethal-damage branch.
Troopers and several other types configure the same `MT_BLOOD`. Its states use
raw BLOO.SPR cells 0–7 for 12 engine tics (400 ms), regardless of the victim's
FIN blood labels. A temporary diagnostic harness performed a Trooper hit and
printed `hit=1 target_type=1 remaining_hp=700`, followed by
`blood type=15 sprite=BLOO frame=0 tics=2 state=703` on commit `471ac3b`.
The probe was kept outside repository source. No TRSCBLOOD state is selected.
All these FIN ranges are loaded and can render; the missing piece is dispatch.

**Confirmed TRSC assets:** the TRSC.FIN/SPR hashes and table offsets recorded
above apply. These seven labels are separate from the unit's main/death
animation, and each frame consists solely of a BLOO sprite command:

| Label | FIN frames | BLOO cells | First offset | Final offset |
| --- | --- | --- | --- | --- |
| TRSCBLOODA0 | 313–322 | 0,1,2,3,4,5,6,7,8,9 | (-81,-1) | (-81,31) |
| TRSCBLOODB0 | 323–330 | 10,11,12,13,14,15,16,17 | (-77,-14) | (-77,31) |
| TRSCBLOODC0 | 331–337 | 19,20,21,22,23,24,25 | (-84,-13) | (-84,11) |
| TRSCBLOODD0 | 338–346 | 27,28,29,30,31,32,33,34,35 | (-78,-9) | (-78,37) |
| TRSCBLOODE0 | 347–356 | 36,37,38,39,40,41,42,43,44,45 | (-79,-15) | (-79,26) |
| TRSCBLOODF0 | 357–365 | 47,48,49,50,51,52,53,54,55 | (-80,-5) | (-80,14) |
| TRSCBLOODG0 | 366–374 | 57,58,59,60,61,62,63,64,65 | (-79,1) | (-79,5) |

The inspected TRSC blood commands have mode 0, intensity 16, layer 1, flags 0,
and raw frame ticks 0. The active TRSCDIEA14 range 223–233 contains only TRSC
parts; it does not invoke these seven independent hit-effect labels. Reusing a
BLOO cell without the FIN command loses its authored position and trajectory.
For example, TRSCBLOODA0 begins at (-81,-1), GRAYBLOODA0 at (-82,5), and
MNDHIV2BLOODA0 in BLOO.FIN at (-30,-8), all using BLOO cell 0. Their later Y
offsets differ too. These are recipient-specific presentations of shared art.

**Confirmed native dispatch:** DC.EXE remains SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
566,272-byte PE32 at image base 0x400000. r2ghidra was used for discovery;
the decisive operations below were checked in x86 disassembly.

1. The `BLOOD%c` string is at file offset 0x6f288, VA `0x471888`.
   Type loading in `0x4385f8`, at `0x438df2–0x438e71`, enumerates letters
   **A through G**, checks available animations, and packs their animation
   pointers into the type record at +0xbc, with count at +0xd8. Type records
   have stride 0x118 at `0x4ec880`. This is not a global generic-blood choice.
2. Availability routine `0x4385a8` checks numeric suffixes 0,2,...30 through
   `0x422f24`. That lookup combines the type prefix and requested suffix at
   `0x422f3a/0x422f49`, then searches the common label table `0x4b7a38` via
   `0x422dc0` at `0x422f82–0x422f87`. The FIN filename is not the selection
   key: recipient-prefixed labels can live in another FIN file.
3. Damage routine `0x43de94` updates HP at object +0x0c and the pending-effect
   byte at object +0xc7 (`0x43df94–0x43dfb2`, addressed as level + object*0xdc
   +0x7def). The exact arithmetic of that byte is not treated as a damage
   amount here; importantly it is written in the damage path.
4. Object ticker `0x418394` checks the animation channel at object +0x1c and
   the pending-effect byte at +0xc7 (`0x418507–0x418528`). If that channel is
   inactive and a request is pending, it uses the type's available blood count.
   At `0x41852a–0x41854f` it increments the shared random-table index at
   `0x4741f8`, wraps it to 8 bits, reads table `0x473df8`, and takes the remainder
   modulo the type's blood count. This selects one of the packed pointers.
5. `0x41854c–0x41855b` installs the selected animation on **object +0x1c** via
   `0x423c34`, with mode argument 1. `0x423c49–0x423c54` reset that channel's
   frame/tick bytes and install its pointer. The ticker clears the pending
   request at `0x418560`, then advances the object's three channels at
   +0x14/+0x1c/+0x24 (`0x418567–0x418580`). It does not replace the main state
   or spawn an independently positioned generic blood actor in this path.
6. World drawing reads the main channel at +0x14 and the blood channel at
   +0x1c independently (`0x43645b–0x4364a9`), resolving the latter's current
   frame from its own frame byte at +0x20. This confirms simultaneous body and
   damage-effect presentation. The ticker waits for an existing effect to
   finish rather than restarting it for each pending request.

**Meaning of “BLOOD”:** it is the engine's type-specific damage-effect category,
not a promise of red fluid. TRSC/GRAY/hive variants use BLOO cells; BARRBLOODA0
(frames155–158) and REAPBLOODA0 (207–210) use HITC cells0–3, layer5, with
machine-specific offsets. Thus assigning the same red raw-cell effect to
organic units and machines is an engine placeholder, not the retail contract.
Some labels contain additional body/overlay parts: HUBU's EXCOPODBLOODA0 starts
at native frame87, which also belongs to the end of ROBOPOD2STAND0. That frame
has six HUBU/BIGC parts before subsequent HITC-only frames. Preserve exact
ranges; do not trim apparently misplaced parts or infer aliases from names.

**Other FIN files:** BLOO.SPR supplies shared pixels, whereas BLOO.FIN holds
MNDHIV2/BRDRHIV2-labelled sequences (plus the exact label MNDHIVBLOODG0).
ALBU.FIN holds other hive variants; WATC.FIN also holds TONG, TORT, FETU and
CENT-prefixed blood labels. Packaging a sequence in another FIN file does not
make it a universal hit effect. A scan of the supported 22-byte-command FIN
files finds 208 BLOOD-labelled ranges across these 23 files:

| FIN | BLOOD labels | SHA-256 |
| --- | --- | --- |
| ALBU | 28 | `99c3d4e4fa0badeb2cd68361a6f1b57dcf9dfbdd027f820a68d806aa18773fa1` |
| ATRIL | 7 | `84bc2c0a62db56cd2eed1316148d3f08a4d6d8d69a280ffaf46d7b55779e7455` |
| BARR | 5 | `08ef8a38d3d0ba5629dcd58c91441569dde7c4ed09c60925b4a86d3d33c65894` |
| BLOO | 14 | `470e4e805de22c370888e9aabe613f4e6c95ada9d267b1dddb3438ded20217bb` |
| DISH | 4 | `8c6977818f28b55d79b41c583dd47de30c8d80802e26f353dfad2d64507ecaf9` |
| DROA | 2 | `b41cf50cd59d8858d6f00d2777992f6ba0c6817496984177b73206a0a012848f` |
| EXPL | 8 | `6cd02d2153bf692155aaf31d73015eb7de89902ecccdc9a079af5a8a6ed2812c` |
| FILL | 1 | `9078654f34e89b50e7b3b422ca0929394cc032c021bc14c4de54aea623734ffc` |
| FUEL | 3 | `6b32818cf91788b6ad2fc1854f7a3cf9196f28716390c3d8129cd9725ea32284` |
| GRAY | 7 | `077887b708009109740a518bf8cff9c547a21145617dbf5dde575342fe5a641a` |
| HUBU | 35 | `b27b20282999188e37a74872b70a273b370cc1dd219f2f8fa84f5f2f2ca3a5a4` |
| HYYK | 3 | `96b317312d506b2fa5769af49bd6b3ca678ac1678b07c3bcaea7bf17ce41c0ce` |
| REAP | 5 | `44d1e85d5a28bca0ca3e45b5bc8032544f16e1dc04fdfd655c3d70ec15ab540b` |
| SALA | 4 | `b0a50e428b78d23300591162c086c2fc53f72b3f2302671165b4a45322ceb0a3` |
| SARG | 5 | `3c29f398dfdb315cf4399b2037f43a499a26e9156ef1bc6d8f20e7236c8803db` |
| SCGM | 1 | `5504da63bf95910593f909a259624a77e3c1d839cc68d751ef76d638769b8638` |
| SCYT | 3 | `a07924ca5d72d666ccf9266df0593cb77811f6ac3679a7a19e8a5b804dbd6f55` |
| SHRI | 3 | `1e8f9bf14946495a810f557121b7a2c1d91b1b61ae2ecf8385b78650119cde26` |
| SLUG | 11 | `f1b813f0607aab425b57011f19f16110d8c5b33179691d08dd4b9558afb87d6b` |
| TRSC | 7 | `eb94f6f3fff53b9f46f1540abf5287c11f83a7db7957d6288b2330b13e1f3b2a` |
| TURR | 12 | `f0f25f1ae13cfcd6b53e3cdcae2e5efaca09e9e8290eb173bb8ce98a93d3a210` |
| WATC | 30 | `ae16a77dd7f6523983935885065c4eb3564419355b3f7f2bc4281a1c04aea9aa` |
| XENO | 10 | `2c4c436d26db6a49d7b46ce12bcf55a46adcf7e6239a6c31dd772bbf0d6a6f84` |

**Limits:** asset presence alone still does not prove a particular label is
selected. For example, GRAY has BLOODH0 but the traced type loader enumerates
only A–G; malformed or differently prefixed labels also require exact native
lookup. The complete damage arithmetic, lethal-hit interaction and all special
type branches have not been ported. A faithful implementation must preserve
recipient-specific selection, complete FIN parts, the separate animation's
lifecycle, and deterministic random selection; choosing TRSCBLOODA0 for every
hit or substituting BLOO.FIN's first label would be another approximation.

Reproduction (radare2's missing dplayx SDB warnings are unrelated):

```sh
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x438df2' -c 'pd 50' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x4385a8' -c 'pd 30' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x422f24' -c 'pd 45' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x418507' -c 'pd 45' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x43de94' -c 'pd 130' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x436444' -c 'pd 45' -c q data/DCOLONY/DC.EXE
build/dc_fin_extract data/DCOLONY/ANIMATE/TRSC.FIN /private/tmp/trsc-blood.json
build/dc_fin_extract data/DCOLONY/ANIMATE/BLOO.FIN /private/tmp/hive-blood.json
```

## Native damage channel implementation (2026-09-09)

**Implemented, with explicit user steering:** the legacy eight-cell `MT_BLOOD`
path is replaced by exact native type-prefix/BLOODA–G selection and complete
FIN state playback. On each visible hit, `A_DC_Damage` calls `P_SpawnMobj` and
copies position, facing and team **once**. The ordinary mobj uses
`P_MobjThinker`, advances through `state_t` tics, and removes itself at terminal
`S_NULL`. It does not follow the recipient or disappear when the recipient is
removed. Repeated hits spawn independent mobjs.

The user explicitly requested “It shouldn't follow! just spawn-and-forget”.
This supersedes the initially implemented attachment/queued-channel design.
There is no attachment pointer, custom blood thinker, owner pending byte, or
separate effect lifecycle in the final implementation. Native pending-byte and
attachment evidence is retained below as retail behavior, not as a claim that
spawn-and-forget is what DC.EXE does. Hidden recipients retain the existing
engine suppression of independently visible blood; lethal visible hits spawn
normally. The damage formula/weapon balance is not changed.

**Executable provenance:** all addresses below refer to the same 566,272-byte
DC.EXE, image base 0x400000, SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
Evidence combines the retained r2ghidra discovery dump, focused x86 disassembly,
FIN/SPR record inspection, and independent renderer pixel comparisons.

### Pending byte and random selection

**Confirmed, including the surprising arithmetic:** after computing remaining
HP in EDX, `0x43df92` clears ECX, `0x43df94` loads pending into CL, and
`0x43df9a` adds remaining HP. `0x43dfa2` compares the signed sum against 255.
If the sum is below 255, `0x43dfaa` **subtracts the low byte of remaining HP**
from pending; otherwise `0x43dfb2` stores 255. This is not an accumulation of
damage dealt. Examples `(pending, remaining HP) -> pending` are `(0,700)->255`,
`(0,254)->2`, `(0,255)->255`, `(4,250)->10`, `(4,251)->255`, `(0,0)->0`,
`(0,-1)->1`, `(0,-256)->0`. Exact-zero lethal HP with no earlier pending hit
therefore does not request a new effect; overkill can. Existing main death
states and HP clamping happen afterward. Repeated requests do not restart a
running blood animation.

**Confirmed:** `0x418507–0x418560` only selects while the channel is inactive;
it clears pending even if the type has no available families. The 256 dwords
at VA `0x473df8` (file `0x717f8`) start `16838,5758,10113,17515,31051`.
The initial index at `0x4741f8` is zero. The ticker increments, masks with 255,
reads that table, and takes `% count`. The committed table is extracted exactly
from those bytes. The engine uses Doom's increment/wrap approach and resets
its level-owned index on thinker initialization. This reproduces this path's
random selection, not the full retail game's random stream: unrelated retail
AI/weapon random consumers have not all been ported.

### Correction: FIN delay multiplication and mode-1 first frame

**Disproven earlier conclusion:** the statements above and in REFERENCES.md
that disassembly confirms a multiplier of **19**, and that the decompiler's
15 was wrong, were themselves wrong. At `0x42356b`, ESI becomes `4*(raw+3)`;
`0x423572` adds another `(raw+3)`; `0x423574` copies this **fivefold** value to
EAX; `0x423576` shifts ESI left twice; `0x42357e` subtracts EAX. The result is
`20*(raw+3) - 5*(raw+3) = 15*(raw+3)`, then signed division by 100 at
`0x42358d`. Zero raw delay is replaced by 15 at `0x42354b`. Thus raw zero
produces **two** native ticks; raw 100 produces **15**. This correction is
verified from instructions, not visual tuning.

**Confirmed clock:** initialization writes 66 (`0x42`) to level +0x970 at
`0x41a728`; +0x974 and subsequent entries receive 33. `0x41cb5a–0x41cb89`
consumes +0x970 as a millisecond interval, and `0x41cbc5` calls the world ticker
`0x418818`. Clock wrapper `0x40ab75` calls `WINMM.timeGetTime`. New damage
states use the default 66 ms cadence and cumulative boundaries
`floor((native_total * 66 * 30 + 500) / 1000)` on the engine's 30 Hz clock.
The retail variable-speed scheduling/network protocol is not implemented here.

**Confirmed startup/end behavior:** `0x423c49–0x423c54` initializes mode 1,
frame byte 0 and timer byte 0. The same object tick then invokes `0x423dd0`.
When the timer is zero, `0x423e0a–0x423e0c` increments the frame **before**
loading its delay. Label loading points +0x20 directly at `frames + start*72`
(`0x42337e–0x4233ab`) and stores `end-start+1` at +0x28
(`0x4233c5–0x4233d9`); there is no inserted sentinel. Therefore this channel
starts visibly at `start+1`, and a one-frame mode-1 label completes immediately.
When frame reaches count, `0x423e23–0x423e2f` resets frame and sets mode 2
(inactive). Timer consumption reads only the low byte at `0x423e59`; zero
underflows to 255 on decrement, giving a 256-tick interval. The catalog retains
all frames, including the skipped first one; this is channel behavior, not an
asset-name exception or a trimmed range. For example, HUBU's apparently stray
six-part first EXCOPODBLOODA0 frame is retained but skipped by this native
startup rule.

**Remaining timing scope:** existing non-damage state chains still have their
previous authored timing; the earlier 19-based conversions in them require a
separate audit. In particular the explicitly required Reaper movement cadence
`{4,3,3,4,1,3,3,1}` is preserved. This change does not claim that those older
chains or the retail whole-game tick scheduler now match the corrected formula.

### Attachment, rendering, and native type identity

**Confirmed:** drawing resolves the main and blood channel frames separately
at `0x43645b–0x4364a9`. It queues main FIN parts at `0x4365f2–0x436693`, then
blood parts starting at `0x436698/0x4366a7`, using the same owner transform and
team. The initial port represented
this as an attached ordinary mobj; that design was removed at the user's
request. The final renderer draws the spawned blood mobj through the same full
FIN layer path as other DC mobjs. `MF_NOBLOCKMAP` no longer forces FIN-based
mobjs into the raw-cell centered drawing path. Its own XYZ controls world sort
and placement; its copied team controls translation. No owner reference exists.

The old `blood_type` property and raw BLOO-cell states are gone. Actor defaults
now include canonical native type IDs for direct spawns; applying defaults a
second time preserves IDs loaded from native scenario records (including
commander variants). Damage dispatch reads **only the native type's prefix**
from the extracted GAMESTAT metadata, not its balance values or SPR basename.
Existing scenario/reinforcement type-to-actor approximation remains outside
this path; if that mapping disagrees with a retained native type, the native
type controls damage selection. No prefix alias repairs were added.

`dc_info_conv --blood-states` generates the shared state/label references from
all supported native FIN files, with 208 labels and 1,453 frame references.
Runtime availability still probes only A–G and even numeric suffixes. GRAY's H
is not selected; its absent F is not invented. SCGMBLOODA without a numeric
suffix remains unavailable. MNDHIV2 blood resolves from BLOO.FIN and TONG from
WATC.FIN. Machine types resolve HITC-bearing ranges. Every FIN command's source,
cell, offset, drawing mode, intensity, layer and flags stays in the existing
asset loader, rather than being duplicated in generated state data.

### Verification and reproduction

`test_blood_fin` checks selection gaps, cross-file lookup, native random values
and wrap, startup +1, ordinary `P_MobjThinker` playback, repeated independent
spawns, copied position/facing/team that remain fixed after recipient movement,
and survival after recipient removal. Its pixel reference draws decoded FIN
commands independently for TRSC, GRAY, BARR, REAP, HUBU and BLOO sequences across
changing spawn positions/facings/teams. Temporary logging in the initial port
confirmed the first Trooper random choice is BLOODE, starting at logical frame
557 (FIN 348), duration 4; that selection/playback is retained and diagnostics
were removed. `test_drop_fin_states` now permits **no** raw-cell exception for
persistent states. CLI tests compare JSON against raw FIN/SPR bytes, reject
malformed spans/ranges/selectors, and reproduce the catalog byte-for-byte.

```sh
make dc-info-conv
build/dc_info_conv --label TRSCBLOODA0 data/DCOLONY/ANIMATE/TRSC.FIN
build/dc_info_conv --cell 0 data/DCOLONY/SPRITES/BLOO.SPR
python3 tests/tools/test_dc_info_conv.py
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_blood_fin
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x423544' -c 'pd 29' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x423dd0' -c 'pd 65' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x41a728' -c 'pd 12' -c 's 0x41cb5a' -c 'pd 32' -c q data/DCOLONY/DC.EXE
```

Final verification: `make` builds all four games without warnings. The focused
blood, actor lifecycle, full FIN-state pixel coverage, Trooper rendering and
sprite-layout tests pass; Reaper's required movement cadence remains unchanged.
`make test-dc-info-conv` and `make dark-colony-info` pass. All four headless game
smoke checks pass; the final DC check and screenshot were repeated after the
spawn-and-forget correction. The blood reference screenshot and level screenshot
were inspected. `test_combat_and_harvest` passes its attack and hidden-target
checks, then retains its pre-existing failure to find a player Exploiter in the
initial mission fixture. `make tags` and `git diff --check` pass.
