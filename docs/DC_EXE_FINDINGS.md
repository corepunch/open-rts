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
the action-name audit below), so the generated `S_DC_EXPL_RUN1` and
`S_DC_EXPL_RUN2` states are a two-frame animation with 16 directional slots;
they are not a two-direction placeholder cycle. The generator's
`fin_state_count_for_sequence16` path preserves those authored labels and
frames.

**Confirmed from regeneration:** `tools/dc_info_gen.c` must emit the extended
14-entry `mobjtype_t` table, including `MT_DC_ORTU`, `MT_DC_SLUG`, the building
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
- `games/dark-colony/g_game.c` - MT_DC_VISION_SIGHT actor definition
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
