# DC.EXE Fog-of-War Visibility and Compositing

The opening investigation contains superseded hypotheses. See “Confirmed fog,
map flags and day/night clock (2026-09-10)” below for the instruction-verified
implementation and its remaining fidelity limits.

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

## Sprite texture memory regression (2026-09-10)

**Confirmed engine allocation bug, not a retail behavior finding:** on HUMAN01,
temporary `OPEN_RTS_DEBUG_MEMORY` logging in `I_CreateTexture` counted more than
73,000 startup textures and 1,879,333,416 bytes of uploaded RGBA pixels by the
73,000th texture. `create_cell_textures` created a base texture for every loaded
SPR cell and eight more for every cell containing indices 138..143, including
animations, teams, and encyclopedia images never drawn. Renderer storage adds
to the pixel payload. This explains multi-gigabyte use without a per-tic leak.
The diagnostic logging was removed after verification.

`R_GetSpriteTexture` now creates and retains only requested cell/translation
textures. The loader owns decoded indexed cells, the SPR source palette and
ordinary index translation maps; world RMP palette data remains separate.
Frames, facings, FIN layers, anchors, and palette rules are unchanged. HUD,
fonts, shadows and world rendering all use the same cache, and `R_FreeSprite`
releases both source data and every created texture. This follows GZDoom's
`FHardwareTextureContainer` (pinned source linked in `REFERENCES.md`). The earlier
cell-at-a-time conversion described above is now deferred until drawing.

**Measured on macOS with SDL's dummy/software renderer:** the original
`4c61fe8` build's HUMAN01 screenshot run peaked at 2,696,141,888 bytes of memory
footprint; the fixed run peaked at 382,585,472 bytes. A sustained fixed run used
196,336 KiB RSS at 29 seconds and 204,128 KiB at 76 seconds. These are headless
measurements; they do not establish the exact Metal footprint or a universal
memory ceiling as additional animations and teams become visible.

**Confirmed preservation:** the 461-entry SPR/FIN catalog was byte-identical:
447 loadable entries matched the original build's pixel/translation, palette,
geometry and animation fingerprints. Both builds rejected the same 14 FIN
entries: ANIM, BUILDING, LIGHT1B, LIGHT1M, LIGHT2B, LIGHT2M, LIGHT3B, LIGHT3F,
LIGHT3M, LIGHT4B, LIGHT4F, LIGHT4M, LITE and TOP. Their rejection was not
investigated in this memory fix. The
HUMAN01 screenshots were byte-identical, SHA-1
`824e714d6402b2ea74e78502246716c056d93c0c`. This audit adds no new DC.EXE address,
native layout or timing claim; the executable fingerprint above is the existing
provenance, and no executable was reinterpreted for this fix.

Reproduce from the repository root (compare outputs against `4c61fe8`):

```sh
make -j8 all build/bin/tests/dark-colony/test_sprite_loading
rg --files data/DCOLONY | rg '\.(SPR|FIN)$' | sort > /private/tmp/dc-sprites.txt
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading /private/tmp/dc-sprites.txt
/usr/bin/time -l env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/dc-memory.bmp
```

The sprite fixtures also check decoding without a renderer, allocation only on
first draw, cache reuse, all eight team colors, untranslatable-cell sharing,
invalid requests, and independence of the source and world palettes. Dark
Colony rendering/loader tests and all four game smoke checks pass. The broader
suite still has two failures reproduced on unmodified `4c61fe8` with the same
data: the Exploiter deployed-body assertion in `test_game_model_headless` and
the available-build-command assertion in `test_model_commands_dark-colony`.

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

**Historical correction, superseded by “Stationary shuffle versus animated
travel” below:** no STAND/SHUF merging or MOVE
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

**2026-09-09 engine movement correction (user-requested, not retail evidence):**
The state-entry movement described above is superseded. Diagnostic logging
confirmed that `A_DC_Fly` applied 4–5 tics of displacement every 133–167 ms,
producing visibly stepped flight. Dropships now have `MF_MOBILE | MF_FLY` and
receive ordinary `P_MoveUnitTo` orders for approach, repositioning after each
release, and departure. The shared `P_MobjThinker` movement advances each tic,
independently of FIN frame timing, as Doom's thinker separates momentum movement
from state timing (`reference/DOOM/p_mobj.c`, `P_MobjThinker`). Flying orders go
directly to their goal and bypass ground pathing, crowd arrival, and ground
separation. Arrival enters the unload spawnstate; `A_DC_Arrive` removes an empty
ship through `S_NULL` and deferred thinker removal. `A_DC_Fly` and its otherwise
unused `P_MoveMobjToward` helper are deleted. Native animation timing is unchanged.
Reproduce with `env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_dropship`:
it checks per-tic displacement, movement within a single animation frame, delivery,
departure, and the requested 50 px altitude.

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


## September 9: projected sprite shadows

**Confirmed executable identity:** `data/DCOLONY/DC.EXE`, 566,272 bytes,
SHA-256 `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
Analysis used the existing broad `dc_exe.c` export followed by focused r2
`pdf`/`pd` and cross-references. PE linker version is 2.18; no Rich signature
occurs before the PE headers. Imports include DirectDraw, DirectSound,
DirectInput and Win32 APIs. **Inferred:** the EAX/EDX/EBX/ECX register argument
order, EBP established after saved registers, and `ret 8` for two additional
stack arguments in `0x45bf80` are consistent with Watcom register calling
conventions. Compiler version remains **unknown**; r2's generic `cdecl` label
and r2ghidra's argument names are not authoritative.

### FIN dispatch and ground anchoring

**Confirmed:** `0x44f95c` dispatches queue byte +0x17 through the jump table at
`0x44f944`. This is the FIN layer field, independent of the drawing mode:

| Layer | Native operation |
| --- | --- |
| 0 | Body only: `0x44fc62`, normal `0x45c060` or mirrored `0x45c41c` |
| 1 | Shadow immediately followed by body: `0x44fcd6`, shadow calls at `0x44fd13` / `0x44fd7b` |
| 2 | Shadow only: `0x44fea3`, calls `0x45cc04` / `0x45c7b0` |
| 3 | Separate prepass (`0x44f95c`); no projected shadow |
| 4, 5 | Other compositors; no projected shadow |

Shadow calls use the command's ground Y, while the body call adds the queued
Z word. The layer-2 subtraction reads queue word +0x10, explicitly cleared
at `0x44fb41`; this does not subtract the object's Z word at +0x12.
Layer 1 temporarily clears the alternate-clipping flag for its shadow.
Flags choose the mirrored routine; team color and intensity do not choose the
shadow colormap. `0x432ac0` enables shadows at `0x4e186d` only when its graphics
setting argument is 2. Both shadow entry points test that byte. open-rts uses
the enabled-shadow behavior; no new graphics setting was added.

**Disproven:** shadows are neither separate SPR assets nor black sprites with
an arbitrary alpha, ellipse, or hand-tuned positional offset. Layer 2 must not
also draw a colored body. Not every FIN layer casts a shadow: DROP's initial
movement body commands use layer 0. The initial HUMAN01/HUMAN02 screenshots
therefore show the arriving body without an invented dropship shadow.

### Integer projection and silhouette

**Confirmed:** normal shadow setup is `0x45c7b0`; mirrored setup is `0x45cc04`.
`0x45c7f0–0x45c84d` sets horizontal carry increment **128** and vertical repeat
increment **40**. Let `h`/`w` be the native SPR cell size, `B` the screen-space
ground bottom of its FIN command, and `X` the ordinary body left edge including
SPR displacement for the unmirrored command. For an unclipped cell:

- `H = h + floor(h * 40 / 256)` output rows;
- shadow top is `B - H`;
- first-row left is `X - floor(H / 2)`;
- each output row advances right by `floor(row / 2)`;
- mirrored drawing starts at `FIN_X + w - floor(H / 2)` and writes backwards.
  Thus its left edge is one pixel to the right of the unmirrored formula.
  This follows the endpoint stores at `0x460e62–0x460e76`, not a visual adjustment.

The span routines `0x45ba5a` / `0x463bc0` initialize byte accumulators to zero.
After consuming a source row, add 40 to the vertical accumulator. On carry,
reuse that row once; the reused row does not increment the accumulator. After
every output row, add 128 to the horizontal accumulator; carry advances the
destination by +1 in **both** reflected and unreflected paths. A 16-row source
produces source row sequence:

```
0 1 2 3 4 5 6 6 7 8 9 10 11 12 12 13 14 15
```

This is not identical to replacing the operation with floating-point scaling.
The control flow is at `0x45bb51–0x45bb9c` and `0x463cb7–0x463d02`.
For top clipping (`B < H`), setup uses `floor(B/2)` for the initial shear,
starts at screen row zero, skips `floor((H-B)*256/296)` source rows, and restarts
the byte accumulators. `0x45c8bb–0x45c8e2` verifies that divisor 296 is 256+40.

Negative RLE runs skip destination pixels. Literal runs ignore source colors
and process the covered destination pixels. The unrolled routine selected by
`0x457fa8` ends at `0x45681a`: load destination into AL, load `[EAX]`, store AL
back to the destination. Mirrored equivalents are selected by `0x462876`.
An audit of all 190 `SPRITES/*.SPR` files found **zero literal-zero bytes** in
compressed runs, so existing decoded nonzero indices preserve retail shadow
coverage without another mask allocation. Thirteen older raw files were
excluded from that compressed-run assertion; no new raw-format claim is made.

### Native palette lookup

**Confirmed:** `0x44a680` allocates and aligns the palette tables to 64 KiB;
`0x44a7c8–0x44a810` installs the base at `0x4841f0`, opens the terrain `.RMP`,
and reads **0x30000** bytes directly into it. `0x45bb00–0x45bb0b` loads this
base and forces AH to **0x48**, replacing the preceding palette/lighting byte.
The clipped and mirrored paths do the same at `0x45b460`, `0x4635cc`, and
`0x463c71`. For each covered pixel:

```
destination_index = RMP[0x4800 + destination_index]
```

Overlapping shadows apply this mapping again. The first bank's row 0x48 is
lighting level 9, team 0 in the native `(light*8 + team)*256` layout. This is
not a universal RGB multiplier: use the shipped terrain table.

| Asset | SHA-256 |
| --- | --- |
| DESERT.RMP | `450b62c07f54925f17b5e69bb26308f97d3237875b865e130451516e45478216` |
| JUNGLE.RMP | `386a427f1141f198f0d03abc9dae0fd76ae790cbaa478601002fe8069a4a1a56` |
| ATLANTIS.RMP | `5d7f64c5a62f1d9b171504993bffa9cf3300cb6208603c2cd1c2caf9c0225ce0` |
| HTRAIN.RMP | `f0bd17a7db3bf917154023b015f62e28c83ab9ff6a5dc284eadc8873e89629df` |
| PALETTE.RMP | `7b0fa7f515db2d5de1f13738d4d314047a66d56d9af86b9bd68d5075d35ca3b4` |
| FUEL.FIN | `6b32818cf91788b6ad2fc1854f7a3cf9196f28716390c3d8129cd9725ea32284` |
| DROP.FIN | `66e8da41ff0a47229c1a33db4aae9e7f37307ec943f5bbd860acc832b07fc433` |

Every inspected RMP is 196,608 bytes. Example DESERT row-0x48 mappings:
`0→0`, `1→23`, `32→54`, `64→180`, `96→98`, `128→130`, `138→98`,
`143→253`, `192→137`, `255→23`. FUEL.FIN byte 57,716 is a shadow-only
command: cell 0, offset (-43,22), mode 0, intensity 16, layer 2, flags 0.
The existing TRSC fingerprints apply; the native standing-frame render test
selects `TRSCSTAND0` through the loader's rotation definition (cell 2 in the
north-facing test view), with dimensions 21×41. Diagnostic output confirmed
47 shadow rows at ground bottom 56, top 9 and initial left 8.

A disk-command audit of FIN files with the current complete 22-byte command
layout counted layers 0/1/2/3/4/5 as 8858/10503/501/2591/2098/32058.
It excluded legacy/incomplete-layout files LIGHT2B, LIGHT3F, LIGHT1M, LIGHT3B,
LIGHT4B, LITE, LIGHT4F, LIGHT4M, ANIM, LIGHT3M, BUILDING, LIGHT2M and LIGHT1B.
These counts describe disk commands, not the number of visible runtime parts.

### Implementation, limits and reproduction

`w_spr.c` loads the 256-byte shadowmap with the existing terrain render tables.
`R_RenderSpriteShadow` uses the engine-owned indexed image, destination
colormap and native integer projection. FIN dispatch draws it immediately
before layer-1 bodies, draws only it for layer 2, and restores ground Z for
both. Other games have no shadowmap and retain their existing behavior.
The shared exact-RGB cache avoids repeated palette searches for both indexed
blending and shadows. Doom's `reference/DOOM/r_draw.c::R_DrawFuzzColumn` also
remaps destination indices through a colormap; DC's projection and source
coverage are concrete native requirements beyond that reference.

**Known renderer limits:** our framebuffer is RGBA. Destination colors are
converted to the nearest terrain palette entry before applying the exact RMP
row. Duplicate palette RGB values cannot recover the original index, and
colors produced by additive rendering/fog can be off-palette. Full byte-for-byte
retail framebuffer identity is therefore not claimed. The native shadow paths
also use 32-pixel column clipping from `0x45bf80`, deriving thresholds from
map-word bits 22–25, plus a per-pixel mask pointer at `0x4841e8`. The complete
producer/meaning of that mask remains **unknown** here and is not emulated by
this change. Shadows use our existing object order and SDL viewport clipping;
full native terrain occlusion and retail viewport edge conventions remain
separate renderer work. No retail runtime screenshot comparison was performed.

Reproduce instruction checks with:

```sh
r2 -q -e scr.color=0 -e bin.cache=true -A \
  -c 'pdf @ 0x44f95c' -c 'pdf @ 0x45c7b0' -c 'pdf @ 0x45cc04' \
  -c 'pdf @ 0x45ba5a' -c 'pdf @ 0x463bc0' -c 'pdf @ 0x44a7b0' \
  -c 'pd 12 @ 0x45680a' -c q data/DCOLONY/DC.EXE
build/dc_info_conv --label TRSCSTAND0 data/DCOLONY/ANIMATE/TRSC.FIN
make build/bin/tests/dark-colony/test_sprite_shadows
env SDL_VIDEODRIVER=dummy OPEN_RTS_SHADOW_SCREENSHOT=/private/tmp/dc-shadow-trooper.bmp \
  build/bin/tests/dark-colony/test_sprite_shadows
```

Temporary `OPEN_RTS_DEBUG_SHADOW` diagnostics printed FIN layer/cell/table/Z,
ground coordinates, projection size, reflection and clipped source row during
verification; removed before commit. Focused tests cover projection, exact
row carries, reflection, transparency, overlap, top clipping, layer dispatch,
Z, all four terrain tables and an actual TRSC FIN standing-frame image.

Verification completed with `make` (all four binaries), `make tags`, all four
headless `--check` commands, Reaper/layout checks, the shadow test and the
existing sprite-layer, height, loading, Trooper and blood rendering tests.
Inspected HUMAN01/HUMAN02 screenshots and the native Trooper fixture BMP.
The full Dark Colony suite is not green: `test_combat_and_harvest` cannot find
its initial player Exploiter, and `test_game_model_headless` expects Human01's
initial Troopers immediately. Those mission-startup failures do not enter the
shadow renderer. A separate stale `test_thinker_level` reference to removed
`S_BLOOD1` was updated to explicitly enter the current ten-frame, four-tic
`S_TRSCBLOODA0_313` chain; that lifecycle test now passes.


## Grey blood uses body-only FIN layers

**Confirmed asset behavior (September 9, 2026):** every command in Grey's
BLOODA/B/C/D/E/G/H labels has layer **0**. Every command in Trooper's
BLOODA/B/C/D/E/F/G labels has layer **1**. The distinction is authored in FIN,
not selected by the BLOO source image, team, blood mobj type or renderer name
exceptions. The reason for the different authored values remains **unknown**.

The existing DC.EXE fingerprint applies:
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
The previously verified queue dispatch at `0x44fc62` draws layer 0's body
only; layer 1 at `0x44fcd6` calls the shadow blitter at `0x44fd13` or
`0x44fd7b` before drawing its body. No new executable behavior is inferred
from the visual difference.

| FIN | SHA-256 |
| --- | --- |
| GRAY.FIN | `077887b708009109740a518bf8cff9c547a21145617dbf5dde575342fe5a641a` |
| TRSC.FIN | `eb94f6f3fff53b9f46f1540abf5287c11f83a7db7957d6288b2330b13e1f3b2a` |

First disk command offsets and command counts, in label order:

- GRAY A/B/C/D/E/G/H: offsets 85548/85768/85944/86120/86318/86538/86758;
  counts 10/8/8/9/10/10/9, all layer 0.
- TRSC A/B/C/D/E/F/G: offsets 88236/88456/88632/88786/88984/89204/89402;
  counts 10/8/7/9/10/9/9, all layer 1.

For example, GRAYBLOODA0 frames 357–366 and TRSCBLOODA0 frames 313–322
both use BLOO cells 0–9, mode 0, intensity 16 and flags 0. Their first
commands differ in placement (Grey -82,+5; Trooper -81,-1) and layer
(Grey 0; Trooper 1). The layer is the little-endian word at command +18.
The lack of Grey blood shadows is therefore consistent with the native
assets and verified dispatch, and no renderer override was added.

**Test correction:** `test_blood_fin` previously loaded sprites before calling
`load_render_tables`, leaving every cached sprite's shadowmap and indexed
blend table unset. Its passing pixel comparison did not exercise native
shadow composition. The fixture now loads tables first, asserts a valid
shadowmap, checks the two native blood-A layer values, and includes native
layer shadow/body dispatch in the direct-FIN reference rendering. The
separate shadow test remains responsible for projection/colormap correctness.
Temporary renderer diagnostics confirmed TRSC states 1706–1715 dispatch BLOO
cells 0–9 with layer 1/casts=1, while GRAY states 1259–1268 dispatch the same
cells with layer 0/casts=0; both use the same valid shadowmap. Diagnostics were
removed after verification.

Reproduce the asset observations and regression check:

```sh
build/dc_info_conv --label GRAYBLOODA0 data/DCOLONY/ANIMATE/GRAY.FIN
build/dc_info_conv --label TRSCBLOODA0 data/DCOLONY/ANIMATE/TRSC.FIN
make build/bin/tests/dark-colony/test_blood_fin
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_blood_fin
```

## Stationary shuffle versus animated travel (2026-09-09)

**Correction/user-required behavior:** interpreting every literal MOVE label as
a walking direction regressed the earlier turning fix. EXPL's standing state
was reduced to eight rotations, while both RUN states acquired sixteen rotations
and held the same body on every odd direction. The simulation already turns in
place in the standing state before translating in the run state. Its sprite
definitions must expose the stationary SHUF poses there. This supersedes the
no-merging/no-filtering presentation rule above; it does not undo loading every
FIN frame or separating states from renderer-owned resources.

**Confirmed asset data:** EXPL retains the fingerprint and offsets recorded
above. It has eight even STAND labels and eight odd SHUF labels (frames 0–15),
eight even MOVE ranges with two frames each (16–31), and eight odd singleton
MOVE labels (102–109). The extra MOVE labels do not supply additional two-frame
walk cycles. Sixteen stationary poses means STAND plus SHUF, not sixteen labels
literally named SHUF.

A label-table audit of retail 164-byte-frame FIN files found the following
STAND0 prefixes with SHUF labels. Ranges are inclusive; counts below come from
`end - start + 1`, not sprite-cell counts or guessed missing frames.

| Prefix | SHUF suffixes | MOVE ranges |
| --- | --- | --- |
| EXPL | All eight odd suffixes | Even: 2 frames; odd: 1 |
| TRSC | All eight odd suffixes | Eight even ranges, 8 frames each |
| TURR | All eight odd suffixes | Eight even ranges, 2 frames each |
| ENGI | All eight odd suffixes | Eight even ranges, 5 frames each |
| GRAY | All eight odd suffixes | Even 0/2: 7 frames; remaining even: 8 |
| XENO | All eight odd suffixes | Eight even ranges, 10 frames each |
| TRUK | Odd suffixes except 7 | Eight even ranges, 2 frames each |
| BARR | All eight odd suffixes | Even: 13,7,8,10,10,10,8,7 frames in suffix order; odd: 1 |
| ATRIL | All eight odd suffixes | Even: 10 frames except 12 has 9; odd 1/3/5: 1; other odd absent |
| REAP | All eight odd suffixes | Even: 8 frames; odd: 1 |
| SLOM | All eight odd suffixes | Eight even ranges, 2 frames each |
| ORTU | All eight odd suffixes | All sixteen animated: 7 frames except suffix 8 has 5 |
| SLUG | All eight odd suffixes | Even: 7 frames except 6 has 6; odd: 1 |

TRSC, EXPL, REAP, SLUG, ORTU, TURR, and GRAY fingerprints are recorded earlier.
Additional audited files under `data/DCOLONY/ANIMATE/`:

| File | SHA-256 | Label table byte offset |
| --- | --- | --- |
| BARR.FIN | `08ef8a38d3d0ba5629dcd58c91441569dde7c4ed09c60925b4a86d3d33c65894` | 104 |
| ENGI.FIN | `e2f29845b5dcc6d4063b734856eebca89fcce83682fa9ae96b3e16c0969ad375` | 40 |
| XENO.FIN | `2c4c436d26db6a49d7b46ce12bcf55a46adcf7e6239a6c31dd772bbf0d6a6f84` | 48 |
| TRUK.FIN | `6dc904c057dd02c87ce733e3a8e3b940b96b505cef6d850c0ce47ff6b65b32e2` | 24 |
| ATRIL.FIN | `84bc2c0a62db56cd2eed1316148d3f08a4d6d8d69a280ffaf46d7b55779e7455` | 32 |
| SLOM.FIN | `54b62a8a750695fcfb2076095ea7bd46c466da6b467bb2ee7fef74fcdce40984` | 32 |

**Implementation:** the shared FIN definition builder fills missing STAND
directions using that prefix's SHUF labels. If MOVE0 is animated, singleton
MOVE ranges are omitted from its walking rotations. Complete eight/sixteen
animated direction sets retain their native range lengths; shorter animations
hold their last frame. ORTU therefore keeps sixteen animated walking directions.
No unit names, new frames, timing constants, or state-table metadata are added.
All raw cells and individual FIN records, including SHUF and singleton MOVE,
remain addressable. Existing state selection chooses stationary versus travel.
The EXPL southeast deploy turn, harvesting presentation, and Reaper timings
remain unchanged.

**Unknown/limits:** this is the user's explicit open-rts presentation contract.
It does not establish DC.EXE's native SHUF dispatch, change the prior verified
literal-name lookup at `0x00423a50`, or port its 32-slot nearest-angle fallback.
No new executable trace was performed. The missing TRUK SHUF7 remains missing;
its incomplete sixteen-pose set still resolves to eight standing rotations.
Pathfinding/translation and turn timing are unchanged; the fix selects animation
facings, not a new grid movement algorithm.

**Diagnostics and verification:** temporary `OPEN_RTS_DEBUG_FIN` logging printed
prefix, range, stationary/travel classification, and installed rotations. It
confirmed EXPL STAND=16/MOVE=8, TRSC STAND=16/MOVE=8, and ORTU STAND=16/MOVE=16;
logging was removed. The definition regression compares every selected frame's
native command layers, offsets, flips, remap, intensity, and delay for TRSC,
EXPL, REAP, BARR, SLUG, and ORTU. The movement regression now loads EXPL.FIN and
checks actual SHUF bodies while stationary, then eight rotations on translation.

`make`, FIN/SPR loading, definition, movement, complete FIN-state rendering, and
sprite-layout tests pass, including Reaper timing. The DC headless check passes;
the HUMAN01 screenshot was visually inspected for rendering integrity (it is
not a turning demonstration). The before/after 461-path catalog has identical
pixels, team translations, palettes, geometry, and anchors for all 447 successful
loads, with the same 14 failures. Exactly 52 complete definition fingerprints
change, the SPR/FIN pairs of the same 26 stems listed in the earlier audit.
Tags were regenerated and `git diff --check` passed.

Reproduce the label evidence and focused regressions:

```sh
build/dc_info_conv --labels data/DCOLONY/ANIMATE/EXPL.FIN
build/dc_info_conv --labels data/DCOLONY/ANIMATE/ORTU.FIN
build/dc_info_conv --labels data/DCOLONY/ANIMATE/TRUK.FIN
make build/bin/tests/dark-colony/test_sprite_definitions \
     build/bin/tests/dark-colony/test_flow_field_movement
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_definitions
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_flow_field_movement
```

## Vent and beacon mobj states and vent origin (2026-09-09)

**Confirmed regression in open-rts:** the scenario object pass skipped types 40
and 84. Resource loading left both vent decoration indices at -1, and no code
created the referenced glow/smoke decorations. Human02 diagnostics reported
`(69,48), rate=22, amount=12000, active=1` and `(53,27), rate=15,
amount=7000, active=1`, both with `glow=-1, smoke=-1`. Only the crater already
present in the terrain remained visible. These are missing world objects, not
missing SPR pixels or a failure of TRSC/GRAY's shared FIN layer renderer.

**Confirmed native asset content:** `ANIMATE/VENT.FIN` SHA-256
`9f43206aba24f71a4a0cb7df841e09ebde2a243dc20310cd34644fed2c3ea84a`
contains 39 frames and two labels: NOTHIBAGAIN (0–18) and VENTSTAND0 (19–38).
The standing label contains PUFF, VENT2, GLIT and SMSP commands together; frame
23 has a second PUFF command. Frame 19/20 raw delays are 26, and subsequent
standing delays are zero. VENT2 cell 0 stays at (-40,12), layer 0, with intensity
15 on frame 31 and 16 elsewhere. There is no attached/exhausted FIN label, and
no VENT crater body command in this sequence. NOTHIBAGAIN has different offsets
and two simultaneous smoke streams; it is not evidence of a harvesting state.
`SPRITES/VENT.SPR` SHA-256
`dfb4454f2f19928a3c522db50a4f039c3677258df1b227ba6fc70b60a22b31c5`
has four raw cells, so VENTSTAND0 uses logical sprite frames 23–42.

**Confirmed engine origin error, corrected:** initially spawning the vent mobj
at `fvec2_cell_center(resource.cell)` put the whole animation one map row above
the crater. The existing resource record explicitly separates the SCN/script
key `cell` from its visual/harvesting `attachment = (x+0.5, y-0.5)`. The mobj now
uses that attachment as its position, just as harvesting does. No command,
SPR displacement, layer selector, sprite bounds, or render offset is modified.
The focused Human02 screenshot shows the VENT2 light inside the opening with
PUFF above it. This uses the existing engine attachment contract; the exact
retail relationship between editor stamp placement and animation origin has
not been established by this audit. Earlier REFERENCES notes applying disY or
negating FIN Y to this plume are superseded by the shared FIN placement contract.

**Requested engine behavior:** each vent is a persistent MT_VENT in thinkercap.
A_DC_Vent uses ordinary state entry actions to select the looping complete
standing FIN sequence, a hidden attached state, or a hidden exhausted/dormant
state. Hidden states retain the terrain crater and poll for renewed activity;
removal/death/departure of the harvester resumes an available vent. The attached
state suppresses the whole free-vent animation, without manufacturing a work
light or extracting a subset of FIN commands. Exact retail attachment visual
behavior remains unknown. Delays use the previously verified native conversion
and default 66 ms clock (see the damage-channel timing correction above), giving
87 engine tics for this loop. Active-state changes are checked at FIN frame
boundaries; inactive states check each tic. This is state-machine scheduling,
not a newly inferred retail transition delay.

**Confirmed beacon data:** BEAC.FIN SHA-256
`034a2fbe82bb7554b74952e735f038b77c2dfda5fc33889367236973cea24dac`
and BEAC.SPR SHA-256
`52b3185f84d793753ed5dc6fe9b33010f37e49028dd4cfe8e57ec23370d5a5e4`.
BEAC.FIN has BEACSTAND2 frames 0–1 (raw delays 6,0),
BEACSTAND14 frames 2–3, two DIE ranges, two SCRCH ranges, and BEEKSTAND2/14.
BEACSTAND2 frame 0 draws BEAC cell 0 at (-50,14), layer 1; frame 1 adds cell 1
at (-50,-42), layer 5. MT_BEACON loops the complete first standing sequence
(logical frames 2–3, 2/4 engine tics). Thus the base persists while its authored
light switches with the FIN frame; there is no decoration blink flag, separate
effect mobj, or renderer clock. Type-84 Human01/Human02 rows load through the
ordinary scenario object spawn path at cell centers. Retail choice between
the two authored facing labels was not established here; both remain loaded.

**Confirmed executable initialization:** executable SHA-256 remains
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
The existing r2ghidra dump led to spawn routine 0x419d44. Exact instructions at
0x41a048 initialize object byte +0x2c to 1. At 0x41a08c–0x41a0a1, a supplied
health <= -1 selects the type table health at `0x4ec8c4 + type*0x118`, then
stores object +0x0c. Negative SCN health is a default-health sentinel, not a
hidden-object flag. The old `object.status < 0` visibility assignment is removed
from the common scenario spawn path; this matters for beacon rows ending
`84 0 -1 0`. Reapplying an unchanged ActorType also no longer overwrites live
mobj flags, which otherwise cleared a vent's state-selected hidden flag.
Doom's P_SpawnMobj initializes flags once and leaves subsequent state actions
in control; the same lifecycle is used here.

**Further native evidence / limits:** searching the assertion string at
0x46e3ac located the mining path at 0x412c5a. Instructions 0x412bfe–0x412c11
compare remaining amount at vent +0x0c against the signed rate at +0x32;
0x412c17–0x412c24 calls 0x4154c0 on the referenced vent on exhaustion.
0x412c2e–0x412c58 checks map bit 26 at the vent's integer x/z coordinates and
0x412c9d–0x412cd2 clears it. The full removal routine and rendering implications
of this map bit remain unknown; the requested persistent exhausted mobj does
not claim to duplicate native removal. The generic FIN loader at
0x423745–0x42375f still converts X*8 and Y*-8, and draw queue code
0x436660–0x436687 adds those offsets to the object's position. No new vent-only
FIN coordinate rule was found.

Reproduce with:

```sh
build/dc_info_conv --label VENTSTAND0 data/DCOLONY/ANIMATE/VENT.FIN
build/dc_info_conv --label BEACSTAND2 data/DCOLONY/ANIMATE/BEAC.FIN
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x41a048' -c 'pd 47' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 's 0x412bfe' -c 'pd 65' -c q data/DCOLONY/DC.EXE
make build/bin/tests/dark-colony/test_vent_states
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_vent_states
```

The test checks every active vent frame's complete decoded commands and timing,
animated pixels against bare terrain, attached/exhausted pixels against the
unchanged terrain, harvester departure/removal, actual resource depletion,
persistence, native `newrate 12 11 68` reactivation, and beacon base/light frames.
It writes `/private/tmp/vent-active.bmp`, `vent-exhausted.bmp`, and `beacon-lit.bmp`.
The old script lookup incorrectly excluded dormant vents; newrate now searches
SCN identities including inactive vents and updates their active status.

**Verification:** `make`, `make tags`, the headless Human02 `--check` and
`--screenshot`, and `test_dark_colony_sprite_layout` pass. The full Dark Colony
suite passes the new vent/beacon test, complete FIN-state coverage and the
TRSC/GRAY pixel checks. Its two failures also reproduce using binaries rebuilt
from unmodified `ba5092f` against the same assets: `test_combat_and_harvest`
cannot find the player Exploiter, and `test_game_model_headless` counts zero
initial Human01 Troopers where it expects 32. These scenario-model failures
are not introduced by this change.

## Restore city FIN initialization and native slot positions (2026-09-09)

**Confirmed, executable and history:** the city geometry was not lost. The table
at VA `0x475b64` in `data/DCOLONY/DC.EXE` remains identical to
`city_slot_offset()` in `w_map.c`. Executable SHA-256:
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`
(566272 bytes). This restores findings recorded in `db463f4` (2026-08-31),
also checked against the pre-September-8 loader in `8aabd1b`.

| Slot | Native pixel offset | Human base module |
| --- | --- | --- |
| 0 | (-64, 15) | EXCOPOD |
| 1 | (0, 0) | BRRKPOD |
| 2 | (32, 64) | ROBOPOD |
| 3 | (64, 10) | SCNCPOD |
| 4 | (-32, 65) | RSCHPOD |
| 5 | (0, 32) | TOWR |
| 6–14 | (0, 0) | remaining city slots |

The constructor `0x4412d4` uses the second `%AISlots` pair, stored at team
`+0x2c/+0x30`. Instructions `0x44145a..0x4414b3` compute signed 8.8 object
coordinates as `anchor * 256 + slot_offset * 8`. Slot lookup uses two dwords
per pair (low signed words are consumed); native type lookup is at `0x475f9c`,
indexed by race, upgrade, and slot. At `0x4414f6`, slot 5 bypasses the occupancy
loop. Unlike mobiles (`0x419f1a..0x419f3e`), cities do not add `0x80` to center
their coordinates. Human02's anchor is `(56,55)`, giving EXCOPOD
`(54,55.46875)`, Barracks `(56,55)`, and TOWR `(56,56)`.

For city object indices below 120 and slots below six,
`0x43654f..0x436567` calls `0x441080` to recover `slot_offset * 8`.
Queue construction then uses:

```text
0x436662..0x436675: draw_z = object_z - slot_z * 8 + FIN.runtime_y
0x43667c..0x436687: draw_x = object_x - slot_x * 8 + FIN.runtime_x
```

Thus the scattered coordinates are intentional gameplay positions. FIN
commands use the common city origin. Ordinary depth-key setup at
`0x4365a7..0x4365c0` consumes object Z before this subtraction. One separate
branch, `0x43657b..0x4365a5`, tests team field `+0xbb8 == 1` and slot 2 and
subtracts `0x108` before sorting; its complete condition semantics remain
**unknown here**, and this correction does not claim to reproduce that branch.

**Confirmed source regression:** `23cbabb` replaced the loader's
`P_SetMobjState()` call with an authored state ID/tic assignment followed by
`P_InitMobj()`. City types are configured by `ActorType` with IDs 1000–1015,
outside `mobjinfo[]`; initialization returned before copying sprite/frame.
Temporary `OPEN_RTS_DEBUG_CITY` logging at spawn showed Human02 EXCOPOD,
Barracks and TOWR with the correct states (1, 9, 11), but all had sprite `-1`
and frame `0`, instead of `(SPR_HUBU,19)`, `(SPR_HUBU,38)`, `(SPR_TOWR,1)`.
TOWR's persistent state never transitioned to repair its visuals. Raw SPR
cell zero explained both the duplicate pod appearance and the misplaced tower.
The diagnostic was removed after confirming the corrected values.

Initialization now applies an explicitly supplied valid state even when no
`mobjinfo` defaults exist. It preserves tics and does not run actions, following
`reference/DOOM/p_mobj.c`'s `P_SpawnMobj` initialization contract. The loader
also retains native physical coordinates for sorting and cancels slot offsets
only through the object's render offset. In bottom-up projection this is
`(-slot.x, slot.y + g_cell_h)`; the existing terrain-row conversion now uses
DC's actual 32-pixel cell instead of the engine's stale 24-pixel `CELL_H`.
FIN command offsets themselves and terrain geometry are unchanged.

Human TOWR now uses slot 5, as the type/geometry table specifies, rather than
copying EXCOPOD's slot-0 physical position. The existing synthesized-tower
policy is retained for both races, but an explicitly populated tower slot no
longer creates a second human tower. The retail justification for synthesizing
a missing tower, the first-AISlots fallback, and dynamic-object tower companions
was **unknown / preserved** at this point. The September 9 phantom-city
investigation below disproves the first-AISlots fallback and identifies the
native tower-enabling branch; dynamic-object tower companions remain unknown.

**Confirmed native assets:** the existing SCNCPOD, SCNCPOD2 and ROBOPOD2 idle
states were not selected for starting objects. They are now selected alongside
new ROBOPOD and RSCHPOD idle chains from `HUBU.FIN`:

| Exact FIN label | Native frames | Logical frames (19 SPR cells) |
| --- | --- | --- |
| ROBOPODSTAND0 | 48–67 | 67–86 |
| ROBOPOD2STAND0 | 68–87 | 87–106 |
| SCNCPODSTAND0 | 22 | 41 |
| SCNCPOD2STAND0 | 24–25 | 43–44 |
| RSCHPODSTAND0 | 9–17 | 28–36 |

The two new chains retain all FIN commands. ROBOPOD has raw delay zero in every
frame; RSCHPOD has zero except raw 80 in frame 17. Native delays are 2 and 12,
respectively; durations use cumulative 66 ms native-to-30 Hz boundaries, as
already established in the native damage timing findings. HUBU/TOWR asset
fingerprints are listed in the complete FIN-state findings above. TOWR's
single command supplies offset `(-36,-9)` relative to the common origin;
EXCOPOD's initial body command is `hubu/0 (-114,12)`, and Barracks is
`hubu/4 (-36,37)`, explaining why raw-cell fallback cannot position them.

**Disproven:** missing geometry table, a city-only half-cell adjustment, or
moving the terrain to assemble the base. The older mixed Human02 anchor
`(56,53.5)` and centered `(56,55.5)` both contradict the constructor. The user
screenshot and new local Human02/Human03 renders establish the visible
regression/correction; they are not retail screenshot equivalence evidence.

Reproduce the native evidence and regression:

```sh
r2 -q -e bin.cache=true -c 'pxw 120 @ 0x475b64' \
  -c 'af @ 0x4412d4' -c 'pdf @ 0x4412d4' \
  -c 'pd 42 @ 0x43654f' -c 'pd 25 @ 0x436650' -c q data/DCOLONY/DC.EXE
build/dc_info_conv --label ROBOPODSTAND0 data/DCOLONY/ANIMATE/HUBU.FIN
build/dc_info_conv --label RSCHPODSTAND0 data/DCOLONY/ANIMATE/HUBU.FIN
make build/bin/tests/dark-colony/test_city_layout
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_city_layout
```

The city test loads Human02 and Human03, checks initial FIN visuals for human
and alien buildings, verifies the player's native slot positions and shared
projected origin, checks the two new FIN timelines, and writes
`/private/tmp/city-human02.bmp` and `/private/tmp/city-human03.bmp` for visual
inspection. `test_mobj_states` protects action-free authored-state initialization.
`make`, `make tags`, Dark Colony `--check`, and the game screenshot pass.
The full Dark Colony suite retains the two previously reproduced baseline
failures: missing Human01 initial Troopers (`test_game_model_headless`) and
missing player Exploiter (`test_combat_and_harvest`). City, sprite layout,
complete FIN states, Trooper/Grey rendering, and vent/beacon tests pass.

## Register building mobjs and restore Barracks production (2026-09-09)

**Implementation correction, superseding the compatibility part of the city
repair above:** all sixteen city/building types now live in the contiguous
`info.h` mobj enum and have designated `mobjinfo[]` entries. Their native type
numbers remain unchanged in `native_type_id`; engine building IDs 1000–1015
and the sidebar/production copies of those IDs are removed. `P_SpawnMobj`
selects their spawn states normally. The loader's separate building-state
switch and sprite reset, and the out-of-table initialization allowance in
`P_InitMobj`, are removed. Native city slot geometry remains unchanged. Human building ActorTypes also
name HUBU rather than the obsolete SHORTCIT placeholder. Starting objects retain the existing authored ActorType speed instead of overwriting it
from the GAMESTAT table; building health/flags match the authored ActorTypes.
This follows Doom's action-free spawn initialization in
`reference/DOOM/p_mobj.c`, rather than adding another city initialization path.

**Confirmed native asset addition:** `DISHSTAND0` is FIN frames 0–16. `DISH.SPR`
has three raw cells, so the complete logical frames are 3–19. All seventeen
frames have zero raw delay (two native ticks); their cumulative 66 ms timing
is 67 engine tics. The base command is `dish/0 (-19,17)`; subsequent commands
include GLAT and DISH parts. Even the distant parts in frames 13/14 remain
unaltered. This lets the communications building initialize through an ordinary
FIN state rather than a raw SPR fallback. SHA-256 fingerprints:

- `ANIMATE/DISH.FIN`: `8c6977818f28b55d79b41c583dd47de30c8d80802e26f353dfad2d64507ecaf9`.
- `SPRITES/DISH.SPR`: `6ae4521f43fa6e3b031704f847e1f7f1df53cd4cc24b3712aa47c75cbf45c80f`.

**Confirmed production regression from source/history:** `e80cf56` and
`51219fd` contain the earlier Barracks release and handoff work. The later
complete-FIN migration (`471ac3b`) made release frames logical HUBU frames
45–66, while both production callers still searched all group-6 states for
raw frame 12. This is not a valid identity lookup: unrelated DROP states also
contain frame 12. The handoff scan also still expected individual TRSC states
and had zeroed its command coordinates, so it could never recover the final
TRSC part from a complete HUBU FIN frame.

The Barracks now enters the named `S_BRRKPOD_BUILD_TRSC1` sequence. Returning to
its ordinary spawn state runs `A_DC_ProductionReady`, which signals the
production queue. Both the model and interactive-driver paths consume this
signal to spawn an ordinary `MT_TROOPER`, copy team/owner, emit model events
where applicable, and advance the queue once. The separate release millisecond
timer and raw frame/group searches are gone. If terrain blocks the native exit,
production waits and retries that exit; it does not spawn in an unrelated free
cell or replay the animation. Existing post-release unit-spacing orders remain.

**Confirmed FIN sequence and corrected timing:** `HUBU.FIN/TRSCBUILD0` has
22 inclusive frames (26–47), all raw delay 6. The multiplier-15 correction at
DC.EXE `0x42356b..0x42358d` and the default 66 ms clock at `0x41a728` apply:
one native tick per frame, 44 cumulatively rounded engine tics in total.
The previous 35-tic value inherited the disproven 19 Hz interpretation.
Executable fingerprint remains
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.

| Native frames | Authored commands |
| --- | --- |
| 26–28 | HUBU door cells 12, 13, 14 at (-36,27) |
| 29–34 | HUBU cell 15 and TRSC cells 72,16,24,32,40,48 at (-156,36..58) |
| 35–37 | HUBU cells 14,13,12 and TRSC cells 56,64,72 at (-156,57..59) |
| 38–43 | TRSC cells 16,24,32,40,48,56 at (-156,68..79) |
| 44–47 | TRSC cells 8,1,2,18 at (-155,78), (-154,80), (-153,79), (-143,79) |

**Confirmed presentation coordinates:** the ANG90 spawn facing resolves to
native `TRSCSTAND8`, whose command offset is `(-159,0)`. `TRSCSTAND0` and
`TRSCSTAND2` use `(-159,4)` and are not the standing pose selected at this
handoff. The final release part minus the selected standing command gives
`(16,79)` pixels. Include the producer's render offset before converting Y
back into bottom-up world coordinates:

```text
pixel_delta = producer.render_offset + (-143,79) - (-159,0)
spawn = producer.position + (pixel_delta.x / 32, -pixel_delta.y / 32)
```

For the Human02 Barracks this is `(56.5,51.53125)`, with pixel delta `(16,111)`.
The checked-in offsets are literal native FIN commands, not tuned constants;
`test_barracks_production` reads both FIN files independently and checks the
actual emitted mobjs against their command delta. Ignoring the producer's
32-pixel city-row render offset would create a one-cell jump at release.
Temporary production logs confirmed state 12 (`S_BRRKPOD_BUILD_TRSC1`), the
exact delta/position above, and one handoff per queued Trooper. Logs were removed.

**Confirmed user flow:** the focused test loads Human02, waits for its native
Exploiter delivery, rejects a Trooper order below 350 Petra-7, issues a harvest
command, observes mining income, buys two queued Troopers for 700, and checks
all 44 release tics per unit and exact spawn positions. It additionally clicks
the actual sidebar Trooper button and ticks the interactive production path,
including a temporarily blocked exit. Mission combat is disabled after the
Exploiter arrives to isolate production: an initial test run retained a pointer
to a Barracks that enemy attacks had destroyed while it waited for more money.
That was a test-fixture lifetime error, not an ignored sidebar click.
The earlier combat/harvest test now identifies the Exploiter by its mobj type;
its obsolete `EXPL.SPR` substring check missed the normal `EXPL` state sprite.
It now passes without bypassing the mission or granting money.

**Unknown / preserved:** the retail caller-side distinction between construction
and troop release at `0x438c95..0x438d16` / `0x441430..0x441450` remains only
partially traced, as in the earlier report. This task preserves the full
22-frame Barracks presentation and its engine training-time policy (cost*10 ms).
It does not establish native training duration, animation startup mode, or
post-exit spacing behavior. In particular, the last ten FIN frames contain only
TRSC commands; no extra persistent closed-door layer is invented for them.

Reproduce asset inspection and verification:

```sh
build/dc_info_conv --label TRSCBUILD0 data/DCOLONY/ANIMATE/HUBU.FIN
build/dc_info_conv --label TRSCSTAND8 data/DCOLONY/ANIMATE/TRSC.FIN
build/dc_info_conv --label DISHSTAND0 data/DCOLONY/ANIMATE/DISH.FIN
make build/bin/tests/dark-colony/test_barracks_production
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_barracks_production
```

The test writes `/private/tmp/barracks-{closed,open,exit,released}.bmp` for visual
inspection. Building registration/initialization is covered by `test_city_layout`;
`test_drop_fin_states` compares every Barracks FIN frame's layers and pixels,
and `test_dark_colony_sprite_layout` checks the release duration and preserved
Reaper timing. The full suite's remaining pre-existing failure is Human01's
initial-Trooper assertion in `test_game_model_headless`.


### September 9: phantom cities at AI locations

**Confirmed bug:** the extra green human base left of Human02's player city
and alien base above it were spawned by the first-AISlots fallback, introduced
in commit `3f6665c`. They belong to teams 4 and 2 respectively. An active team
and populated `%City` values do not suffice to enable a city at its AI location.
The earlier assumption that this fallback restored allied cities is **disproven**.

Primary evidence: retail `data/DCOLONY/DC.EXE`, 566272 bytes, SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`;
scenario loader `0x41a61c` and city constructor `0x4412d4`. Checked the cached
r2ghidra decompilation against fresh radare2 instruction disassembly.
Here `EDI` is the native team record, `level + 0xb98 + team * 0xe30`.

- `0x41abb7..0x41abce` reads the first `%AISlots` pair into team `+0x34/+0x38`.
  `0x41abe6..0x41abfa` reads the second pair into `+0x2c/+0x30`, the city anchor.
- `0x41ac1d..0x41ac32` copies the city pair into the first pair only when both
  first-pair components are zero. The native fallback runs in the opposite
  direction to the removed loader code; it never invents a city from an AI pair.
- After parsing a city slot, `0x41ad47..0x41ad52` tests city X at `+0x2c`.
  Zero branches to `0x41ac95`, clearing both the slot value at
  `+0x3c + slot*4` and upgrade at `+0xc4 + slot*4`. Constructor
  `0x441337..0x441341` rejects a zero slot value by clearing the object's
  active byte `+0x2c` and returning. Thus these are absent cities, not cities
  which should be spawned at world origin or shifted to the first pair.
- `0x41abff..0x41ac16` sets team bytes `+0xdb2/+0xda4` when either city
  component is zero. Their full gameplay meaning remains **unknown**.
- **Confirmed tower evidence:** `0x41acde..0x41ad25` requires both city
  components nonzero and applies mode/team conditions before writing slot 5
  value `+0x50 = 1`, upgrade `+0xd8 = 0`; otherwise both are cleared.
  This establishes native tower synthesis. The complete meaning of mode
  `level+0x14a0` and team flag `level+0x1524+team` remains **unknown**;
  this fix does not claim to port that entire policy.

Human02's active team records and diagnostic results:

| Team | First (AI) pair | Second (city) pair | City after correction |
| --- | --- | --- | --- |
| 0 | (61,53) | (56,55) | Present |
| 1 | (36,57) | (0,0) | Absent |
| 2 | (56,61) | (0,0) | Absent |
| 3 | (31,60) | (0,0) | Absent |
| 4 | (50,55) | (0,0) | Absent |

Temporary `OPEN_RTS_DEBUG_CITY_ANCHOR` logging confirmed that all four empty
city pairs selected their AI coordinates before the fix and were suppressed
after it. Logging was removed. Human03 independently preserves legitimate
cities for teams 0 `(75,6)`, 2 `(10,87)` and 7 `(14,7)`, while rejecting team 1's
empty city pair despite its AI location `(24,51)`. `test_city_layout` checks
these exact team sets, in addition to physical slot geometry and FIN origins.
Explicit scenario objects (including communications dishes) still load normally.
The corrected Human02 render was inspected: both pictured extra bases and their
towers are gone, while the player city remains assembled at its native anchor.

Reproduce:

```sh
r2 -q -e bin.cache=true -e scr.color=false -c 'af @ 0x41a61c' -c 'pdf @ 0x41a61c' -c q data/DCOLONY/DC.EXE
make build/bin/tests/dark-colony/test_city_layout
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_city_layout
```

The test writes `/private/tmp/city-human02.bmp` and `/private/tmp/city-human03.bmp`.


### September 9: resolve the stale Human01 headless assertions

**Confirmed test error, not missing startup units:** `HUMAN01.SCN` places
30 native type-8 Greys: 15 for team 2, 12 for team 3, and 3 for team 4. It
places no player Troopers. `HUMAN01.TRO` block 8, condition `(c>0)`, issues
`reinforce 0 22 2 0 4 69 1 0 0 0 0 0 0` at line 49: four type-0 infantry and
one type-69 commander. The current engine maps both native types to
`MT_TROOPER`. The assertion requiring 32 player Troopers before the first tic
was introduced by `485a81a`; that commit replaced the prior 30-Grey assertion
without supporting scenario evidence. It was wrong, not an accepted mission
startup limitation. Earlier reports of this pre-existing failure are historical;
the failure is now resolved.

Asset SHA-256 fingerprints:

| Asset under SCENARIO/HUMAN | SHA-256 |
| --- | --- |
| HUMAN01.SCN | `af82c538181ca182481562dfa75ff1f39038a58445b019cd6a52426b33e968e7` |
| HUMAN01.TRO | `f7fb1c67d68eaa4207ec5053ad7289608f43ca6d631a9d0d45ff3f260011a1a0` |
| HUMAN02.SCN | `bed27b613d20fb8b2533369d949adb4e90b96922372e7df3e7957140d44c90ab` |

Temporary `OPEN_RTS_DEBUG_HEADLESS` logging printed every Human01 initial
mobj's type, owner, sprite and hidden flag: 30 enemy `MT_GREY`/GRAY objects,
three allied city parts and the player beacon, with zero player Troopers.
After correcting that expectation, the test advanced through the real mission
script and observed the dropship and all five opening infantry objects before
selecting a Trooper and issuing its move order. No runtime spawn code or
mission timing was changed. Retail distinctions for the commander beyond the
existing type-69 mapping were not investigated here.

**Additional stale checks uncovered:** the old test returned at Human01's
first failure and never checked Human02/03. Their snapshot sprite assertions
still compared `SPRITES/DISH.SPR`, `SPRITES/HUBU.SPR`, `SPRITES/TOWR.SPR` and
`SPRITES/ALBU.SPR`, while ordinary FIN-state mobjs expose stems `DISH`, `HUBU`,
`TOWR` and `ALBU`. These checks now follow the existing sprite registry contract.
Human02 has ten native type-8, team-2 records with health -1. The old test
mistook these for hidden placeholders. As established in “Confirmed executable
initialization” above, retail `0x419d44` sets active byte +0x2c at `0x41a048`
and resolves negative health from the type table at `0x41a08c..0x41a0a1`.
The executable SHA-256 remains
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
This task reuses that previously verified disassembly, rather than claiming a
new native discovery. Human02 diagnostics confirmed ten live enemy Greys,
GRAY sprite, hidden=0, HP=800. The test now checks their exact count, owner,
visibility and positive health. Diagnostics were removed before committing.

The three mission checks now run even if an earlier mission check fails;
each reloads its own level. Removed unused test helpers and an obsolete log
that counted the same mobj list twice as separate units and effects. Verification
includes the complete Dark Colony suite and sprite-layout tests, with no
remaining failures:

```sh
make
make tags
SDL_VIDEODRIVER=dummy make test-dark-colony test-layout
SDL_VIDEODRIVER=dummy build/bin/dark-colony --check
```

## Human city damage, fire and explosions (2026-09-09)

**Confirmed executable:** 566,272-byte retail `DC.EXE`, image base 0x400000,
SHA-256 `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
Focused r2 disassembly of `0x413620`, `0x4385f8`, `0x4154c0`, `0x415618`,
`0x423c34` and `0x423dd0` establishes the following behavior.

### Health selection and persistent fire

**Confirmed:** city idle action `0x413620` reads the native type's maximum HP
at type-table +0x44 (`0x413679`), and remaining HP at object +0x0c. While main
animation mode at object +0x1a is 1, it leaves that animation running
(`0x413685–0x413688`), protecting production/release animations. Otherwise:

| Remaining HP | Type-table animation | Native label |
| --- | --- | --- |
| HP > `(maxHP * 11) >> 4` | +0x80 | STAND |
| `(maxHP * 5) >> 4` < HP <= `(maxHP * 11) >> 4` | +0x88 | BURN |
| HP <= `(maxHP * 5) >> 4` | +0x84 | SCRCH |

The apparently reversed BURN/SCRCH order is **confirmed**, not a typo:
`0x4136ad` branches on signed `HP <= high`; `0x4136d4` branches on signed
`low >= HP` to the +0x84 load at `0x4136de`. The intervening branch loads
+0x88 at `0x4136d6`. Loader `0x438d20–0x438d9e` associates SCRCH with +0x84
and BURN with +0x88. Missing SCRCH makes both entries use STAND
(`0x438dc3–0x438ddb`); missing BURN makes it use SCRCH
(`0x438da0–0x438dac`). Do not substitute HIT labels or assume names imply
increasing severity. Examples for the 2400-HP Barracks: 1651 -> STAND,
1650 -> BURN, 751 -> BURN, 750 -> SCRCH.

`0x4136e4–0x4136e6` selects mode 0 (loop), not a one-shot hit effect.
`0x423c39–0x423c47` preserves an unchanged label/mode's frame and timer.
**Consequence:** fire persists at damaged HP without another attacker or
another hit. FIN commands already combine the damaged body, fire, smoke and
other parts; no separately positioned generic flame mobj is necessary.

### Native assets and duplicate-label trap

**Confirmed:** `ANIM.DAT` lines 8 and 9 load `burn.fin` and `burn2.fin`.
`burn3.fin` is absent. Scanning every file without respecting the index finds
a tempting but wrong one-frame `BRRKPODDIE0` in BURN3. The indexed BURN2 label
is the actual 35-frame sequence. The committed exporter uses ANIM.DAT and
rejects duplicate selected labels.

| Building prefix | SCRCH FIN frames | BURN FIN frames | DIE FIN frames |
| --- | --- | --- | --- |
| EXCOPOD | BURN 170–185 | BURN 186–201 | HUBU 191–219 |
| BRRKPOD | BURN 301–320 | BURN2 57–76 | BURN2 112–146 |
| ROBOPOD | BURN 88–107 | BURN 386–405 | BURN2 147–181 |
| ROBOPOD2 | BURN2 0–19 | BURN 108–127 | BURN2 217–251 |
| SCNCPOD | HUBU 289–304 | BURN 72–87 | BURN 321–352 |
| SCNCPOD2 | HUBU 408–423 | HUBU 466–481 | BURN 353–385 |
| RSCHPOD | BURN 128–148 | BURN 149–169 | BURN2 182–216 |

SHA-256 fingerprints:

- ANIM.DAT: `20e9cf988ed833236ca0687601ab32a2adeaae0d885b39a7507321166bdba3d0`
- BURN.FIN: `26ed51a0e037f1fb3c05506d4d34c81fb80bdab34b13ae1711171b081bbafe55`
- BURN2.FIN: `b2dd6c3721245166f7b707c7b99add8d71cdbd2f944480478729dabdaa7d2f15`
- BURN3.FIN (excluded): `21c7282befda8704adb67ee6d29360a96d0760e34adbeb15e803d3651998c972`
- HUBU.FIN: `b27b20282999188e37a74872b70a273b370cc1dd219f2f8fa84f5f2f2ca3a5a4`

### Death, timing and implementation boundary

**Confirmed:** lethal damage in `0x43de94` enters `0x4154c0` and clears
occupancy through `0x431be8`. Death dispatch entry 10 at VA `0x47432c`
points to `0x415618`. It chooses one of the type's +0xac death labels using
the shared random table (`0x415671–0x4156bb`) and plays mode 1; the loader
tries DIE, then DIEA/B/C, falling back to STAND if absent
(`0x438a4f–0x438ba0`). The seven human city types above have DIE sequences
with complete explosion/fire/debris commands. There is no need to replace
the building with a guessed explosion asset. The special type +0x100 branch
instead uses FUNK/+0x9c mode 3; it is not used for these human city types.

The main channel is advanced before the state handler (`0x418567–0x418580`),
so selecting a new main animation exposes frame zero for one native tick.
At the next tick its reset zero timer increments the frame (`0x423e0c`).
Mode 0 wraps to zero; mode 1 becomes inactive at completion
(`0x423e23–0x423e2f`). The port shows this initial frame for rounded 66 ms,
then uses FIN delays and cumulative 66 ms -> 30 Hz conversion. Existing
healthy STAND timing remains authored as before. Death ends at S_NULL in the
shared Doom thinker lifecycle. Retail retains the inactive object until its
150-tick death counter expires (`0x4156d6–0x4156ee`); this port does not retain
that invisible slot or add a separate lifetime timer. Main drawing exits when
the channel is inactive (`0x436463–0x43646a`), confirming that this does not
truncate a visible retained frame. Full retail scheduling,
non-city special death modes and alien city transitions are outside this change.

### Spark alignment and verification

**Confirmed bug:** `A_DC_Damage` copied XYZ/angle/team but omitted
`core.render_offset`. Human02's Barracks has `(0,32)`, so its sparks were
32 pixels above their FIN placement; the Exco slot has `(64,47)` and loses
both components. Temporary `OPEN_RTS_DEBUG_BUILDING_DAMAGE` logging printed
owner and effect origins before and after the fix, then was removed. Native
main/blood drawing uses the same transform (`0x43645b–0x4366a7`, documented
above). Copying the complete offset once fixes this without attaching the
spawned effect or adjusting any native command offsets.

The headless Human02 regression captures the city with an actual P_Attack hit,
damaged fire continuing for 100 simulation ticks without further hits,
low-HP SCRCH, and a lethal-hit explosion. Screenshots were inspected at
640x480. `/private/tmp/building-sparks-before.png` shows the old displacement;
`building-sparks.png`, `building-burn-idle.png`, `building-scratch.png` and
`building-explosion.png` show the corrected FIN placement and state changes.
The tests cover all seven human city types, threshold boundaries, repeated-hit
phase preservation, repair selection, production handoff, copied offsets,
lethal cleanup, and FIN sequence lengths/delays.

Reproduce:

```sh
r2 -q -e bin.cache=true -c 'af @ 0x413620' -c 'pdf @ 0x413620' -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -c 'af @ 0x415618' -c 'pdf @ 0x415618' -c q data/DCOLONY/DC.EXE
make dc-info-conv
python3 tools/dc_building_states.py > /private/tmp/building_states.inc
cmp /private/tmp/building_states.inc games/dark-colony/building_states.inc
make build/bin/tests/dark-colony/test_building_damage
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_building_damage
```

Verification completed: `make`, `make tags`, the full headless
`make test-dark-colony` suite, `test_dark_colony_sprite_layout` (including
Reaper's `{4,3,3,4,1,3,3,1}` movement timing), default headless `--check`,
Human02 headless `--screenshot`, deterministic state-export comparison, and
`git diff --check` all pass. The focused screenshot test also attacks both
native Human02 city buildings and preserves their shared FIN origin through
fire and explosions.

## Human02 petrovent guards and premature attack audit (2026-09-09)

**Confirmed assets:** HUMAN02.SCN places ten type-8, team-2 Greys at
X=51..55 on rows Y=28 and Y=26, around the type-40 petrovent at (53,27)
(rate 15, amount 7000). The city anchor is (56,55). HUMAN02.TRO block 0,
`c>0`, assigns a waypoint command to each of these exact starting cells:
the Y=28 row gets two destinations, (53,25) then its starting cell; the
Y=26 row gets (53,29) then its starting cell. These are local routes around
the vent, not orders toward the city. Block 17 separately contains
`reinforce 2 47 6 8 10 10 7 0 0 0 0 0 0` and `ai 2 3`, conditioned on
`((c>880)&&(s(0,10)==0))`. Do not confuse that later reinforcement with
the ten initial guards, or infer a seconds conversion from the counter alone.

SHA-256 fingerprints:

- DC.EXE: `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`
- HUMAN02.SCN: `bed27b613d20fb8b2533369d949adb4e90b96922372e7df3e7957140d44c90ab`
- HUMAN02.TRO: `0e5a6593768b4fff717be69609d8ed5eb2aea48d1bab68c30c8d5d621d7e080d`

**Confirmed executable:** parser `0x43ae2c`, at `0x43ba8a..0x43bc17`,
recognizes `waypoint` (string `0x471b78`) as action 10. The 28-byte action
record stores source cell X/Y at +4/+5, point count at +6, and byte X/Y
pairs from +7. `0x43bb47..0x43bbab` checks 1..8 points (native assertion
string `0x471b84`). Dispatcher `0x43a144`, case 10 at
`0x43a8d4..0x43a943`, searches the 800 objects of stride 0xdc at level
+0x7d28, comparing object coordinate words +0/+4 shifted right eight
against the source cell. It applies the command to the first matching object
through `0x43a094` (call at `0x43a8da`).

`0x43a094..0x43a0ea` copies each destination into object word pairs
+0xa6/+0xa8 with stride four, converting each cell component to
`(cell << 8) + 0x80`. It sets byte +0x36=1, +0x37=9 and +0xc6=point count.
The command dispatcher `0x4114a4` consumes +0x36/+0x37 through table
`0x4742ac`; entry 9 at `0x4742d0` is `0x415260`. That function checks
the native type table +4, then starts object action 9 through `0x4112b8`
with ECX=1 and initializes its returned word to zero, or returns through
`0x4114a4` when the type-table value is zero. This establishes a real
object waypoint order, not a decorative marker or a reinforcement command.

**Confirmed current-engine defects:** `p_script.c` neither parses nor
executes `waypoint` or `ai`. `OPEN_RTS_DEBUG_SCRIPT=1` on Human02 prints
all ten waypoint commands and `ai 2 3` as unknown. `p_ai.c:DC_UpdateAI`
orders every live non-player mobile attacker toward a target on its first
500-ms think. `ai_target` scans the whole mobj list with no detection,
visibility, or maximum-distance gate; distance only changes a score.
The 5000-ms wave timer selects a preferred target, not permission to attack.
`map_has_ai(map, 1)` reduces all active native teams' AI modes to one global
boolean; the actual owner argument is otherwise unused. Target selection
also uses owner inequality rather than the shared allegiance predicate.

**Confirmed runtime diagnostic:** temporary `OPEN_RTS_DEBUG_AI_AUDIT`
logging just before `P_MoveUnitTo`, with 35 extra Human02 model ticks in
`test_game_model_headless`, printed all ten Greys still at their initial
cell centers receiving a target at (63.57,48.57), `wave=-1`, attack range
4.00. Target type 14 is `MT_DROPSHIP`: in this trace they initially chase
the player's opening dropship from more than twenty cells away. This
disproves an attack gated by the five-second wave timer and confirms that
the targeting filter also admits dropships. The headless test passed;
temporary logging and extra ticks were removed after the audit.

**User-observed retail behavior:** these Greys remain around the petrovent
and engage when approached. The authored routes support that observation.
**Unknown:** exact native target-acquisition distance/visibility rules,
waypoint combat interruption and resumption, whether the route repeats,
and the full semantics of AI modes 2 and 3. This audit does not claim to
have ported Krusty AI or to have verified our scoring weights and timers
against retail, despite the existing source comment claiming a mirrored
policy. Do not fix this by inventing a guard radius or delaying a dropship.
The implementation needs native waypoint execution and order-aware local
engagement, together with faithful per-team AI handling. Runtime behavior
is unchanged by this investigation.

Reproduce the asset/parser evidence and focused disassembly:

```sh
env SDL_VIDEODRIVER=dummy OPEN_RTS_DEBUG_SCRIPT=1 build/bin/dark-colony --check data/DCOLONY SCENARIO/HUMAN/HUMAN02.MAP
r2 -q -e bin.cache=true -e scr.color=false -c 'pd 110 @ 0x43ba8a' -c 'af @ 0x43a144' -c 'pdf @ 0x43a144' -c 'af @ 0x43a094' -c 'pdf @ 0x43a094' -c 'pxw 48 @ 0x4742ac' -c 'af @ 0x415260' -c 'pdf @ 0x415260' -c q data/DCOLONY/DC.EXE
make build/bin/tests/dark-colony/test_game_model_headless
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_game_model_headless
```

### Implement petrovent patrols and remove unconditional pursuit

**Correction to the audit's route-repetition unknown:** retail object action
table `0x474304`, entry 9 (`0x474328`), points to `0x415364`.
`0x41539d..0x4153aa` compares the current signed word index to the byte
point count at object +0xc6 and resets the index to zero at the end.
`0x4153af..0x4153d3` copies the indexed +0xa6/+0xa8 point to movement
destination +0x2e/+0x30 and increments the index. `0x4153d6..0x4153dd`
calls movement setup `0x413fc0` with EBX=1 and ECX=0. A pending command
at +0x36 instead dispatches through `0x4114a4`. **Confirmed:** these
waypoints repeat; they are not a one-time walk followed by a global attack.

**Visibility versus combat acquisition:** `0x432a30` resolves the object's
team-selected weapon and passes weapon-table dword `0x4eb214 + weapon*0x48`
to `0x4323bc` as the search bound (`0x432aa4..0x432aad`). The search walks
local cell offsets, stopping when its ring counter exceeds the passed bound;
it also checks team masks, alliances and target/type/weapon eligibility.
The native weapon records for Grey weapons 15/16/17 each have range 4.
This supports retaining local weapon-range combat, not an unlimited target
scan. The broader idle paths at `0x413e76`, `0x413e93` and `0x413ea3`
pass bounds 16, 9 and 4 under different conditions. Do not flatten these
into one universal visibility radius; their complete policy remains unported.

Separately, `0x446158` reads level +0x540 and its complement to 256;
`0x446240..0x44625e` computes
`(weight * type[0x4ec890] + (256-weight) * type[0x4ec894]) >> 8`, with
type stride 0x118, for the map-mask update. GAMESTAT's authored OBS_DAY /
OBS_NIGHT values are Grey 4/7 and Trooper 7/4. This is evidence of a
separate sight calculation, not proof that a Euclidean radius test alone
reproduces all native visibility or acquisition. Exact mask production,
occlusion, time-of-day evolution and its full relationship to every AI search
remain outside this fix. **Disproven lead:** `0x41203c` selects neighboring
movement cells through `0x411b1c`, `0x411c94`, `0x411ec4`; it is not the
general enemy acquisition routine.

**Implemented:** parse 1..8 waypoint destinations, assign the route by the
authored source cell, retain it on the stable mobj, and repeat it using the
existing movement system. Execute scripts before route updates. Combat
continues through ordinary `A_Look` / `P_MobjThinker` and pauses route
movement while a live hostile target remains in weapon range. Remove the
unverified 500-ms global attack selector, 5000-ms preferred-target timer,
threat weights, and averaged-base defense policy. Existing explicit movement
orders are no longer overwritten with globally selected attack targets.
This deliberately removes fabricated automatic offensive waves across DC
missions; native strategic attack dispatch and `ai` command semantics still
need implementation. The economy's existing vent assignment remains.
Do not describe this change as a complete Krusty AI or fog-of-war port.

The shared combat selectors now consistently exclude `MF_NOBLOCKMAP`
objects, matching the existing movement-time selector: dropships, beacons,
vents and visual effects must not attract these ordinary attacks. The Doom
reference `reference/DOOM/p_enemy.c` likewise separates `A_Look` /
`P_LookForPlayers` perception from `A_Chase`; the removed global policy
had bypassed that actor-owned acquisition boundary.

Temporary `OPEN_RTS_DEBUG_GUARDS` logging in the new headless regression
printed every guard's position, waypoint index/count, target and HP once per
second. All ten stayed near the vent for 60 simulated seconds with no target
and full HP. A group can crowd its shared waypoint destination under the
current collision/pathing implementation, so route repetition is additionally
tested with an isolated surviving guard; it visits the second point and wraps
to the first. Spawning a player Trooper beside it then causes combat damage.
Logging was removed before committing. Crowded-endpoint pathing remains an
existing limitation, not justification for adding an arrival-distance hack.

Reproduce the newly resolved native behavior and encounter regression:

```sh
r2 -q -e bin.cache=true -e scr.color=false -c 'pxw 48 @ 0x474304' -c 'af @ 0x415364' -c 'pdf @ 0x415364' -c 'af @ 0x432a30' -c 'pdf @ 0x432a30' -c 'af @ 0x446158' -c 'pdf @ 0x446158' -c q data/DCOLONY/DC.EXE
make build/bin/tests/dark-colony/test_petrovent_guards
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_petrovent_guards
```

The executable fingerprint is unchanged from the audit above. Verification
uses `make`, `make tags`, the full headless Dark Colony suite, sprite-layout
tests and the Human02 headless smoke check. An isolated checkout was used
to separate this change from concurrent sidebar/production edits.

## Global build menu and native dependency configuration (2026-09-09)

**Confirmed from retail configuration:** `GAMESTAT/DEPEND.TXT` contains the
building/unit dependency graph; executable decompilation is unnecessary for
recovering these entries. SHA-256:
`9e5e5251d5196677aa96b690413ccf15b601b5e9a00892d393fc260798efc67d`.
Rows contain ID, cost, UI ID, class, then building slot/upgrade/race or unit
type, followed by prerequisite row IDs terminated by -1.

| Row | Product | Prerequisite rows |
|---|---|---|
| 0 | Exo-Ctr | none |
| 1 | Barracks | 0 |
| 2 | Sci-Pod | 0 |
| 3 | Robo-Ftr | 2, 1 |
| 4 | Sci-Pod+ | 2 |
| 5 | Robo-Ftr+ | 3, 2 |
| 6 | Rsch-Bay | 4 |
| 7 | Exploiter | 0 |
| 9 | Trooper | 1 |
| 29 | Sentinel | 1, 2 |
| 11 | Reaper | 3, 2 |
| 10 | Osprey IV | 0, 3, 4 |
| 8 | Firestorm | 5 |
| 12 | Barrager | 5, 4 |
| 13 | S.A.R.G.E | 1, 6 |
| 83 | Medi-craft | 4, 3, 6 |

**Correction:** the hand-authored Medi-craft prerequisites previously required
upgraded Robo-Ftr instead of Sci-Pod+, Robo-Ftr and Research Bay. Osprey omitted
its explicit Exo-Ctr prerequisite. Both now match DEPEND.TXT. Upgraded buildings
share their base module slot and satisfy the lower-tier requirement in the
interactive sidebar; they replace the earlier module rather than adding a
second building at another position.

**Confirmed layout:** `INTRFACE/MAINE`, SHA-256
`f8dc545cd8d2eae674dd7b1604a1f5d3f5aaf2c14416a31a1a40ac1305beeaad`,
controls 80–94, 135 and 206 specify fixed 59×41 buttons. Left x=518 has
Exploiter/Trooper/Sentinel/Osprey/Reaper/Firestorm/Barrager at
112/153/194/235/276/317/358. Right x=577 has S.A.R.G.E and Medi-craft in its
first two rows, then Exo-Ctr/Barracks/Sci-Pod/Robo-Ftr/Research Bay at
194/235/276/317/358. Science and factory upgrades reuse their base button.
Unavailable controls leave black space; there is no compacted product grid.
The sidebar now reads those rectangles directly from MAINE.

**User-provided retail screenshot:** the image attached to this request shows
Exploiter and Trooper in the first two left slots and the Sci-Pod icon (control
81, MAINBUT frame 21) in the right slot at y=276. This identifies the previously
unnamed building. The user explicitly specifies this menu when nothing is
selected. The initial Human02 base supplies Exo-Ctr and Barracks; owned modules
are omitted, leaving exactly those three controls. This is base-dependent
availability, not an unconditional unlock on every mission.

**Implementation:** clicks locate an owned producer without requiring its
selection. Units retain their existing queues. Building clicks create ordinary
mobjs at the native city module position, reusing the slot table already traced
to DC.EXE `0x441080` / `0x4412d4` in the city initialization audit above
(executable SHA-256 `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`).
Slot pixel deltas become 16.16 positions via *8*256; FIN render offsets keep
the shared city origin. Sci-Pod enters existing `S_SCNCPOD_BUILD1` and advances
through P_MobjThinker. Construction-group states do not unlock their dependents
until finished; a producer playing a unit-release animation remains available.
Already owned modules cannot be purchased again. Costs are deducted only when
the production/build request succeeds.

**Limits/unknowns:** the complete retail building-order/delivery scheduling,
construction cancellation/refunds and queue UI remain unverified. This change
uses the existing construction state table; entries without a registered build
chain use their spawn state. Exo-Ctr foundation placement is not implemented.
Later units without actor mappings remain unsupported. The screenshot does not
prove selection behavior for every other menu/tab. No timing constant or new
delivery effect was inferred from it.

**Verification:** all 16 authored human cost/UI/prerequisite entries compared
exactly to DEPEND.TXT. Temporary `OPEN_RTS_DEBUG_PRODUCTS` logs confirmed initial
prerequisites and exposed a premature upgrade while Sci-Pod was constructing;
logging was removed after fixing it. `test_barracks_production` now clicks the
unselected Trooper and Exploiter slots, checks resource deduction and queues,
builds Sci-Pod at its native slot, checks its construction state, and prevents a
second purchase during construction. Reproduce with:

```
make build/bin/tests/dark-colony/test_barracks_production
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_barracks_production
```

The test writes `/private/tmp/dc-build-menu.bmp`; visual inspection confirmed
the three icons and empty slots match the supplied screenshot's menu layout.

## Keep the Barracks visible during Trooper release (2026-09-09)

**Confirmed correction:** production was incorrectly replacing the producer's
main FIN state with TRSCBUILD0. Its frames contain the door and Trooper only,
so this removed the building. The earlier statement that no persistent building
presentation was established is superseded; the FIN frames themselves remain
unchanged and complete.

Retail DC.EXE SHA-256:
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
At `0x4139e0` the city production handler loads the product animation from
`0x4ec918 + type*0x118`, adds **0x24** to the producer at `0x4139ef`, and
calls `0x423c34` with mode 1 at `0x4139f2`. This is distinct from the main
channel at +0x14. Drawing resolves +0x14 at `0x43645b`, then independently
resolves +0x24 at `0x4364b6–0x4364ef` and queues its commands after the main
and damage commands at `0x43678e–0x436807`. All three channels tick at
`0x418567–0x418580`. Construction startup at `0x44143a–0x44144b` is a
different caller; it does not justify replacing the producer during release.

**Implementation:** an ordinary MT_PRODUCTION_RELEASE mobj runs the existing
22-frame chain through P_MobjThinker and terminal S_NULL, following the same
ownership pattern as Doom's P_SpawnPuff (`reference/DOOM/p_mobj.c:812`). Copy
the producer's position, render offset, angle and team once. A zero-tic terminal
action resolves the producer by stable ID to mark its queue ready, avoiding a
dangling pointer if the building was removed. The building continues its own
standing/damage animation. No renderer exception or synthesized FIN layer is
needed. Native training duration and exit spacing remain **unknown**; the
existing engine timing and handoff policy are unchanged. Unlike retail attached
channels, this short-lived visual finishes independently if its producer dies.

Temporary diagnostics confirmed building state 9 and release state 12 coexist
at the same position with render offset (0,32); logs were removed after testing.
Tests assert both objects' states and renderability throughout all 44 release
tics, two queued units, the exact native handoff, blocked-exit retry, and damage
to the building while the release proceeds. Headless screenshots cover open-door
and late Trooper-only frames with the building still present.

Reproduce:

```sh
r2 -q -e bin.cache=true -c 'pd 14 @ 0x4139c6' -c 'pd 24 @ 0x4364b6' -c q data/DCOLONY/DC.EXE
make build/bin/tests/dark-colony/test_barracks_production build/bin/tests/dark-colony/test_building_damage
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_barracks_production
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_building_damage
```

Screenshots: `/private/tmp/barracks-open.bmp`, `/private/tmp/barracks-exit.bmp`.

## Production-channel audit across all city buildings (2026-09-09)

**Scope:** follow-up to the Barracks disappearance fix. Checked the retail
executable, all FIN files in ANIM.DAT, all 106 GAMESTAT unit prefixes, and the
current production implementation. Executable SHA-256 remains
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
The existing Watcom/register calling-convention evidence applies; addresses
below were checked with r2 disassembly, not just inferred decompiler arguments.

### Confirmed native channel contract

The native building owns three **eight-byte animation channels**:

| Object offset | Role | Runtime fields within each channel |
| --- | --- | --- |
| +0x14 | Main building, including standing/damage/construction | +0 pointer to direction array; +4 frame byte; +5 timer byte; +6 mode byte |
| +0x1c | Damage visual | Same layout |
| +0x24 | Produced unit's release presentation | Same layout |

`0x423c34` compares the animation pointer and mode, resetting frame and timer to
zero only when either changes (`0x423c39–0x423c54`). Mode 0 loops; mode 1 plays
once and becomes inactive mode 2 at the end; mode 3 holds the final frame
(`0x423e1e–0x423e3b`). The three channels advance independently, in the order
main, damage, release, at `0x418567–0x418580`, before the city action handler.
The timer-zero path advances the frame before loading its delay
(`0x423e03–0x423e65`). Since city release startup occurs after channel ticking,
reset frame zero is available for drawing until the next native tick; its raw
delay does not determine that initial interval. This does not establish all
caller timing policies or justify treating every BUILD sequence as 44 tics.

The city action at `0x413620` only suppresses its main standing/damage selection
when **main mode +0x1a** is 1 (`0x413685–0x413688`). Active **release mode
+0x2a** does not suppress the building's idle or HP-dependent animation.
This independently confirms the concurrent building-damage test in the fix.

Drawing requires an active main channel (`0x43645b–0x43646a`), resolves damage
at `0x436470–0x4364a9` and release at `0x4364b6–0x4364ef`, then submits their
native FIN commands using the owning object's transform. The release command
loop includes `0x43678e–0x436807`, after the main and damage loops. Retail does
**not** allocate a second gameplay object for this presentation. Our ordinary
release mobj preserves visible layering through the shared Doom thinker model,
but is not a literal port of the native three-channel storage/lifetime.

### Confirmed lookup and queue lifecycle

The type table starts at `0x4ec880`, stride `0x118`. Loader
`0x438c95–0x438d16` fills type **+0x98** by selecting:

1. `<type-prefix>BUILDSTAND<even facing>` if the family exists;
2. otherwise `<type-prefix>BUILD<even facing>`;
3. otherwise a null pointer.

Availability `0x4385a8` probes suffixes 0,2,...30. Selection is by the **produced
unit's prefix**, not its producer's sprite name, and not necessarily the file
with that prefix. This audit found no duplicate BUILD labels in the ANIM.DAT
load list. GRAY, ORTU and ZISP select BUILDSTAND families. RNAT and GRUB have
nonzero-facing BUILD labels; probing only BUILD0 would miss them.

The city queue lives in team state at `game + 0xb98 + team*0xe30`:
+0x108 holds four phase bytes, +0x10c four countdown bytes, +0x110 four queue
counts, and +0x118 four 800-byte type queues. In the phase-1 path, after checking
exit occupancy, countdown and unit capacity (`0x413752–0x413938`), a non-null
product +0x98 is started on **producer +0x24** with mode 1
(`0x4139d9–0x4139f2`), and the phase becomes zero. If +0x98 is null, the
immediate-spawn path is used (`0x413938–0x413a39`). In phase zero, the handler
waits while release mode +0x2a equals 1 (`0x413a63–0x413a6a`); after it ends,
`0x413ab7` calls the spawn routine, the queue shifts, its count decrements, and
phase returns to one (`0x413abc–0x413ae8`). The release is not a replacement
for the building and is not the queued unit already walking under simulation.

**Confirmed producer slots:** `0x419c9c` maps city slot to queue through the
15-dword table at `0x419c60`: `2,0,1,4,3,4,4,4,4,4,4,4,4,0,0`.
The handler exits when the result is 4 (`0x4136eb–0x4136f1`). Using the already
verified city type/slot table gives:

| City slot | Human / alien building family | Queue |
| --- | --- | --- |
| 0 | EXCOPOD / BIOHIV | 2 |
| 1 | BRRKPOD / WARHIVE | 0 |
| 2 | ROBOPOD, ROBOPOD2 / BRDRHIV, BRDRHIV2 | 1 |
| 3 | SCNCPOD, SCNCPOD2 / MINDHIV, MNDHIV2 | None (4) |
| 4 | RSCHPOD / RSCHIV | 3 |

In particular, do not infer a manufacturing queue from a science-building
prerequisite. The exact enqueue-side product-to-queue assignment is still
**unknown in this audit**; the current C maker lists are not native evidence.

**Confirmed exit lookup:** `0x41377f–0x4137a5` and `0x413a86–0x413ab7` use
`city_anchor + exit_table[queue][product_type.f0]`. GAMESTAT loader arguments
at `0x43873b` / `0x4387f3` map type +0xf0 to zero-based numeric column 22.
This is the `exit_variant` printed by `tools/dc_production_audit.py`.
The signed dword pairs at `0x419c00` are:

| Queue | Variant 0 | Variant 1 | Variant 2 |
| --- | --- | --- | --- |
| 0 | (0,-3) | (0,-3) | (0,-3) |
| 1 | (2,3) | (-5,-1) | (-4,-1) |
| 2 | (-4,0) | (-5,-1) | (-4,0) |
| 3 | (-4,3) | (-4,3) | (-4,3) |

These are native grid offsets used for occupancy/reservation and passed into
`0x41a478`, not guessed pixel deltas from the last FIN command. Downstream
spawn-position adjustment inside that routine was not traced here. The existing
Trooper last-frame/standing-frame delta is therefore an **engine handoff policy**,
not proof of retail's final exact position. No guessed generalized handoff has
been implemented from the new inventory.

### Confirmed unit release inventory

Inclusive native FIN ranges, before the engine's raw-SPR-cell prefix:

| Native type | Prefix | Selected label | FIN | Frames |
| --- | --- | --- | --- | --- |
| 0 | TRSC | TRSCBUILD0 | HUBU | 26–47 (22) |
| 1 | TURR | TURRBUILD0 | BURN | 285–300 (16) |
| 2 | REAP | REAPBUILD0 | HUBU | 402–407 (6) |
| 3 | BARR | BARRBUILD0 | HUBU | 382–401 (20) |
| 4 | SARG | SARGBUILD0 | BURN | 257–268 (12) |
| 5 | SCGM | SCGMBUILD0 | BURN | 202–228 (27) |
| 6 | EXPL | EXPLBUILD0 | BURN | 252–256 (5) |
| 8 | GRAY | GRAYBUILDSTAND0 | ALBU | 303–334 (32) |
| 9 | XENO | XENOBUILD0 | ALBU | 377–385 (9) |
| 10 | SCYT | SCYTBUILD0 | ALBU | 367–376 (10) |
| 11 | ATRIL | ATRILBUILD0 | ALBU | 348–366 (19) |
| 12 | PSYC | PSYCBUILD0 | PSYC | 301–320 (20) |
| 13 | ORTU | ORTUBUILDSTAND0 | ALBU | 465–485 (21) |
| 14 | SLUG | SLUGBUILD0 | ALBU | 432–445 (14) |
| 43 | ENGI | ENGIBUILD0 | BURN | 229–251 (23) |
| 44 | SLOM | SLOMBUILD0 | ALBU | 402–431 (30) |
| 49 | BEON | BEONBUILD0 | BURN | 269–284 (16) |
| 50 | ZISP | ZISPBUILDSTAND0 | ALBU | 446–464 (19) |

The extra ATRIL prefixes at native types 7,15,27,39 resolve the same family;
this does not establish that each type is trainable. Commander TRSC types
69–72 and GRAY types 73–76 also resolve their prefix's release family, without
proving a production UI entry. RNAT types 25,38 resolve RNATBUILD4 (120–126)
and RNATBUILD12 (113–119), both in RNAT.FIN; GRUB type 36 resolves GRUBBUILD6
(98–104) in GRUB.FIN. Their role is outside city troop production here.
DROP's `SCNCPOOPBUILD` and `SCNCBUILD` do not match a native type BUILD family;
they must not become guessed aliases for SCNCPOD.

**Asset consequences:** TRSC and ENGI start with the same HUBU door cell 12 at
(-36,27); GRAY and SLOM start with ALBU door cell 24 at (-65,8). Other releases
can consist solely of unit parts. SCGM, ORTU, BEON and ZISP end with multiple
commands, sometimes multiple copies of the same unit sprite, so “use the last
command's offset” is not a valid general spawn algorithm. The audit tool retains
all first/last commands and raw delays. REAP, SARG and EXPL use raw-zero delays;
TRSC/ENGI/BEON use raw 6; several others mix 0,6,13; RNAT and GRUB use 33.
Do not reuse the Trooper's frame count or delay for another product.

### Confirmed building construction inventory

Constructor `0x4413db–0x441413` initializes main to standing mode 0 and both
secondary channels to inactive mode 2. When the construction branch is taken,
`0x44143a–0x44144b` starts **the building type's +0x98 on main +0x14**, mode 1.
The pointer saved at `0x4413e4` is the main-channel address; it is not +0x24.
This intentional main replacement differs from releasing a unit. Native
construction frames include the developing building and delivery/effect parts;
adding an unconditional finished-building underlay would be incorrect.

| Native type | Building prefix / label suffix BUILD0 | FIN | Native frames |
| --- | --- | --- | --- |
| 16 | EXCOPOD | PART4 | 61–81 (21) |
| 17 | BRRKPOD | PART4 | 103–123 (21) |
| 18 | ROBOPOD | ROBO | 0–41 (42) |
| 19 | ROBOPOD2 | DROP4 | 0–22 (23) |
| 20 | SCNCPOD | DROP | 94–135 (42) |
| 21 | SCNCPOD2 | DROP3 | 0–36 (37) |
| 22 | RSCHPOD | PART2 | 61–83 (23) |
| 28 | BIOHIV | SAUC | 67–150 (84) |
| 29 | WARHIVE | SAUC | 151–206 (56) |
| 30 | BRDRHIV | SAUC2 | 78–152 (75) |
| 31 | BRDRHIV2 | SAUC2 | 207–229 (23) |
| 32 | MINDHIV | SAUC2 | 0–71 (72) |
| 33 | MNDHIV2 | SAUC2 | 167–188 (22) |
| 34 | RSCHIV | SAUC4 | 0–60 (61) |

Type 35 repeats RSCHIV. All listed construction sequences have raw-zero delays;
that is not a claim that they have zero displayed duration. First/last command
inspection confirms completed building parts within the FIN sequences, including
upgrades which retain their lower module as authored commands.

### Current implementation versus retail

**Confirmed from source:** `G_ModelStartProductionRelease` and the special exit
path are still restricted to Barracks/Trooper. The visibility fix correctly
preserves its building, but no other native release family is connected to
production. `G_ModelBuildingStateForProduct` selects native construction chains
only for types 19,20,21. Types 16,17 enter standing directly; 18,22 fall through
to their spawned standing states. Alien construction is not exposed by the
current product table. This audit does not silently turn those missing paths
into finished functionality.

Retail stores the release on the producer. Our release mobj copies the transform
once and may finish visually after producer destruction; terminal notification
uses its stable producer ID. Full native lifetime, death interaction, enqueue
routing, training countdown source, and final spawn placement need to be carried
through together before claiming an exact general production port. The original
visibility fix is narrower than that. This follow-up changes audit tooling and
records evidence, not production behavior.

### Reproduce and verify

```sh
make dc-info-conv
python3 tools/dc_production_audit.py > /private/tmp/dc-production-audit.json
r2 -q -e scr.color=false -e bin.cache=true -c 'pD 140 @ 0x438c95' -c 'pD 1263 @ 0x413620' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 'pD 130 @ 0x4413db' -c 'pD 300 @ 0x423dd0' -c q data/DCOLONY/DC.EXE
r2 -q -e scr.color=false -e bin.cache=true -c 'pxw 96 @ 0x419c00' -c 'pxw 60 @ 0x419c60' -c q data/DCOLONY/DC.EXE
```

The audit reads native GAMESTAT, the actual ANIM.DAT list and FIN metadata via
`dc_info_conv`; it does not load extracted stats into gameplay. Its JSON includes
all 106 types, including null-family results, native executable fingerprint,
selected label ranges, delays and complete first/last commands. Selection was
cross-checked against the earlier generated GAMESTAT header and direct
`dc_info_conv --label` inspection for human and alien releases/construction.

Verification: the audit reports 48 types with a selected family (50 directional
sequences including repeated prefixes), and all 106 prefixes/exit variants
match the separately extracted `gamestat.h`. `make`, `make tags`, the Dark Colony
headless smoke check, `test_barracks_production`, and `test_building_damage` pass.

## Retail dropship unload timing correction (2026-09-10)

This section supersedes the earlier `DROP FIN sequences and ordinary objects`
claim that the unload cycle is 47 engine tics. That claim followed the wrong
multiplier in an earlier timing interpretation.

**Confirmed executable evidence:** the inspected `data/DCOLONY/DC.EXE` is the
same retail fingerprint recorded at the top of this report: SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5`, 566272
bytes, PE32 i386, image base `0x00400000`, timestamp August 11, 1997
20:53:20. In `fcn.004230ac`, the FIN loader:

- tests the frame delay word at `0x00423544`;
- writes 15 for a zero delay at `0x0042354b`;
- computes the converted count at `0x00423563`–`0x0042358f` using a 15x
  multiplier (`5x`, then `20x - 5x`), i.e. `((raw + 3) * 15) / 100`.

`DROP.FIN` frames 0–9, the `DROPTWO` label, all have raw delay 0. Therefore
each unload frame is 2 retail animation ticks, and the complete ten-frame
unload is 20 ticks. The retail animation ticker calls `fcn.00423dd0` from
`fcn.00418394` once per animation update, consuming that converted frame count.
At open-rts's 30 Hz simulation rate, the corresponding unload duration is
20/30 seconds, approximately 0.67 seconds. This matches the requested
near-continuous retail drop better than the previous 47-tic (1.57 second)
state chain.

**Implementation consequence:** `S_DROP_UNLOAD1` through `S_DROP_UNLOAD10`
now use 2 tics each; `A_DC_Drop` remains on the zero-tic `S_DROP_RELEASE` state.
The change is in the authored `tools/dc_states.txt` row and its checked-in
`games/dark-colony/animate/DROP.inc` output. Dropship flight speed remains an
independent `ActorType`/`mobjinfo` setting; this correction does not change
movement speed or reposition orders.

**Disproven/superseded:** the previous statement that the focused instruction
sequence computes `((raw + 3) * 19) / 100`, and the resulting 47-tic `DROPTWO`
cycle, are not supported by the retail bytes. `DROPMOVE0` remains on the
existing authored movement-frame timing in this change; its movement duration
is controlled independently by the dropship flying order and speed.

Reproduce the native conversion and focused state check with:

```sh
build/dc_info_conv --label DROPTWO data/DCOLONY/ANIMATE/DROP.FIN
build/bin/tests/dark-colony/test_drop_fin_states
```

## Replace macro state catalogs with per-FIN raw includes (2026-09-10)

**Implementation-only refactor:** supersedes the exporter commands and macro
consumer descriptions in earlier sections; their native behavior findings are
unchanged. `tools/dc_states.py` now emits `animate/<FIN>.inc` raw designated
state initializers, ordinary enum entries, a raw blood-label lookup and a raw
building-range table. The old blood/building macro catalogs and exporters are
removed. `tools/dc_states.txt` explicitly owns authored frames, tics, actions,
next states and groups; `FAMILY_RULES` explicitly assigns native damage-family
actions, including `A_DC_BuildingStand` for SCRCH/BURN, and terminal/timing policy.

**Confirmed local reference:** Doom's `multigen.txt` is the actual input opened
by `multigen.c:194–195`. Action names are authored per state, interned by the
parser, and emitted as declarations/function references, not generated function
bodies. Doom's `p_enemy.c` supplies A_Look/A_Chase/A_PosAttack; see REFERENCES.md.

**Verification:** before editing, expanded every row and enum in the old tables
into an independent snapshot. After exporting, all **2,719** numeric IDs and
all six state fields compare exactly, including NULL actions, complete FIN
references, Reaper `{4,3,3,4,1,3,3,1}` movement, static Exploiter WORK, production
completion and every damage/death chain. Blood retains 208 exact labels.
The comparison also distinguishes input sets: blood retains supported files
from the ANIMATE directory; human building families use ANIM.DAT membership.
Searching all supported FINs for building labels instead of respecting that
load set finds duplicate BRRKPODDIE0 labels and is not equivalent.
No native behavior, palette, frame geometry, timer or action assignment changes.

Regenerate with `make dark-colony-states`; validate generated output with
`python3 tools/dc_states.py --check`. Native metadata/pixel/timing coverage remains
in `test_drop_fin_states`, `test_building_damage`, `test_barracks_production`,
`test_dark_colony_sprite_layout`, and `tests/tools/test_dc_info_conv.py`.

## Indexed sprites and DirectDraw (2026-09-10)

**Confirmed native evidence:** the local `data/DCOLONY/DC.EXE` still hashes to
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
The PE/toolchain fingerprint and Watcom calling-convention caveats above apply.
The broad r2ghidra export led to the functions below; conclusions were checked
against disassembly, including indirect-call arguments and byte loads/stores.

- `0x42bc80` calls the DirectDrawCreate import thunk at `0x46a1cc`, passing
  the object destination `0x4745ec`. `0x42bcd1–0x42bce5` passes **640, 480, 8**
  to vtable slot `+0x54` (`IDirectDraw::SetDisplayMode`). Failure text at
  `0x470900` is “Setting to 8 bit mode Failure”; assertions name `ddex4.c`.
- `0x42bdc4` creates the primary flip chain: descriptor size `0x6c`, flags
  `0x21` (CAPS | BACKBUFFERCOUNT), caps `0x4218` (PRIMARYSURFACE | FLIP |
  COMPLEX | VIDEOMEMORY), backbuffer count 1. `0x42be1f` calls CreateSurface
  (`+0x18`), writing the primary at `0x4745f0`. `0x42be49` requests its
  BACKBUFFER (`caps=4`) through GetAttachedSurface (`+0x30`), storing
  `0x4745f4`. The inspected sprite path below is a CPU byte blitter.
- `0x42be77–0x42beb7` creates a separate 640×480 offscreen surface at
  `0x4745f8`, with caps `0x840` (OFFSCREENPLAIN | SYSTEMMEMORY), set at
  `0x42be61` / `0x42be81`. `0x42bed2–0x42bee1` loads the initial palette from
  `intrface/load.bmp` using `0x44bee4`, storing the palette at `0x4745fc`.
  That helper reads BMP/resource palette records and calls CreatePalette
  (`+0x14`) with flag 4 (8BIT) at `0x44c1af–0x44c1b5`. This confirms an
  initial **BMP** palette path, not the still-unknown SPR six-bit RGB expansion.
- `0x42bf02` and `0x42bf1c` attach that same palette to the primary and
  offscreen surfaces with SetPalette (`+0x7c`). The loop at
  `0x42bf33–0x42bfdb` also loads 32 `cursor/cursor%d.bmp` surfaces and attaches
  the shared palette. Thus some UI images really are DirectDraw surfaces;
  this does not imply one surface per gameplay SPR cell or per team.
- Presentation routine `0x42b54c` uses BltFast (`+0x1c`) to copy the offscreen
  surface onto the attached backbuffer, optionally blits the color-keyed
  cursor (`0x42b6c6`), then calls primary Flip (`+0x2c`) at `0x42b6d5`.
- Normal FIN body dispatch `0x45c060` calls `0x45b990` / `0x45aec3` for
  compressed spans. At `0x45ba03–0x45ba08` and `0x45af36–0x45af3b`, EAX
  receives the aligned RMP base from `0x4841f0` and AH receives the queued
  palette/lighting byte from `0x4841e4`. `0x45ba24–0x45ba3c` reads a signed
  RLE control byte, skips negative runs, and dispatches nonnegative literal
  runs through `0x45a49b`. Its first entries are `0x459e71`, `0x459e69`,
  `0x459e61`. For example `0x459e71–0x459e75` does:

  ```text
  AL = source_byte[ESI]
  AL = lookup_byte[EAX]
  destination_byte[EDI] = AL
  ```

  Together with the earlier instruction-verified queue formula
  `(light << 3) + (team & 7)`, this establishes indexed source and destination
  with team/lighting applied during the blit:
  `dst = RMP[((light * 8 + team) << 8) | src]` for this normal lookup path.
  Transparent RLE spans preserve the destination. Native projected shadows
  use a different destination-index lookup, documented above.

**Disproven for the inspected path:** rendering each team requires storing an
RGB copy of every sprite, or switching a separate RGB display palette per
unit. Different teams coexist through index translation into the shared
palette. DirectDraw supplies surface/palette/presentation operations; these
sprite spans are written by the game's own CPU routines.

**Unknown / scope:** this is not a complete inventory of every video mode,
surface, palette update or special blitter. Decompiler output also exposes
other display/conversion routines (for example `0x406fb0`); their purpose and
mode selection were not established, so the 8-bit finding applies to the
verified gameplay setup above, not every possible executable path. Full
native terrain masks, blend modes and SPR channel expansion remain the
previously documented unknowns. No retail runtime capture was made here.

### Engine change and verification

**Supersedes the lazy-cache implementation in “Sprite texture memory
regression”:** indexed cells now remain indexed even after drawing.
`R_DrawSprite(renderer, sprite, cell, palette, src, dst, flip, color, blend)`
selects a source-index translation at draw time; it retains no per-cell or
per-team expanded image. The palette argument is a translation ID, with -1
or an unknown ID selecting source colors. `source_palette` remains separate
from the world palette used by blend/shadow tables. The SDL renderer owns
one streaming upload buffer, growing only to the maximum requested width
and height and released at renderer teardown. Crop, scaling, reflection,
modulation and draw order remain handled by SDL. The per-cell translation
cache, scan for team pixels, and now-unused RGBA slicing helper were removed.
World, HUD, font, selection-marker and direct FIN reference draws use the
same drawing API. Other games' existing RGBA source textures remain supported.

This storage requirement follows the user's request and Doom's
`reference/DOOM/r_draw.c::R_DrawTranslatedColumn` translation-at-draw model;
it replaces the previous GZDoom translation-specific texture cache. It does
not claim to implement DirectDraw or the native indexed framebuffer. SDL
still receives expanded ARGB pixels for the current draw. Updating the single
buffer serializes pending uses; GPU throughput has not been benchmarked.
Shadow/blend destination readback and temporary composite textures remain,
as do terrain textures. Those are separate from retained sprite/team copies.

Temporary `OPEN_RTS_DEBUG_SPRITE_BUFFER` logging on HUMAN01 reported upload
extents 13×36, 77×257, 116×257, then **127×257 (130,556 bytes)**. One upload
texture remains live; SDL may also own staging storage. This is the observed
scene's pixel payload, not the process footprint or a universal upper bound.
Diagnostics were removed before commit.

Compared against parent `233d95e`, the full 461-entry SPR/FIN catalog is
byte-identical: 447 loadable entries, the same 14 rejected entries documented
above. It hashes indices, all eight translated pixel outputs, source/world
palette data, geometry, layers, directions and timing. Manifest-output SHA-256:
`22966ed476bd8a86bf63287a25a9af0a989d74fc9f5d2ed60a276f661788a3dd`.
The HUMAN01 BMP is also byte-identical and visually inspected; SHA-256:
`d791c1fdb6914dcaf0cffc77a9f48250d4f276b081e9c38965fa8504c081910c`.
Focused tests cover two palettes queued before readback, live palette changes,
clipped/scaled/reflected crops, invalid spans/IDs, no textures after drawing,
FIN layers, shadows, height, blood, dropships, Trooper death and Reaper timing.
All four game builds and headless checks pass; tags were regenerated.

Reproduce the native and engine checks:

```sh
r2 -q -e scr.color=0 -e bin.cache=true -A \
  -c 'pdf @ 0x42bccc' -c 'pdf @ 0x42bdc4' -c 'pdf @ 0x44bee4' \
  -c 'pdf @ 0x42b54c' -c 'pdf @ 0x45c060' -c 'pdf @ 0x45b990' \
  -c 'pdf @ 0x45aec3' -c 'pxw 16 @ 0x45a49b' \
  -c 'pd 10 @ 0x459e61' -c q data/DCOLONY/DC.EXE
make -j8 all build/bin/tests/dark-colony/test_sprite_loading
rg --files data/DCOLONY | rg '\.(SPR|FIN)$' | sort > /private/tmp/dc-sprites.txt
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading /private/tmp/dc-sprites.txt
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/dc-indexed.bmp
```

### September 10: remaining memory after indexed sprite conversion

**Confirmed engine measurements**, HUMAN01 at the default 640x480, macOS arm64,
with Homebrew sdl2-compat 2.32.70 backed by SDL 3.4.12. The starting revision is
`33f932d`. These are open-rts measurements, not the retail executable's working
set. The retail evidence remains the preceding “Indexed sprites and DirectDraw”
section, for DC.EXE SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
No new executable routines were decoded during this memory investigation.

The retained images already were one byte per pixel, with eight small index
translation tables, rather than eight RGBA copies. Three independent costs
remained:

- Startup scanned every SPR in CURSOR, ENCYCLO, and INTRFACE, although gameplay
  only consumes MAINBUT and the CLIENT selection marker there. The font and GIF
  background have their own owners. The scan retained 32.36 MiB of encyclopedia
  pixels and 21.94 MiB of interface pixels, including unused EARTHG (5,398,614
  bytes) and GLOBES (4,599,665 bytes). This is an engine loading error; it does
  not establish the original executable's screen-specific loading order.
- The same gameplay image was loaded under its state-table stem, SPR path, and
  FIN dependency path. Our loader already pairs SPRITES/X.SPR and ANIMATE/X.FIN
  in either direction, so these keys must share one owner. UI paths stay
  distinct: ENCYCLO/BARR.SPR is not the gameplay BARR image. Alias entries now
  borrow the canonical sheet; freeing the cache only frees owners.
- Every logical frame reserved 32 direction records even for nondirectional
  raw cells and FIN frames. Frame definitions now allocate their actual
  authored rotation count (including genuine 16/32-direction definitions),
  retaining the same frame/layer ownership and renderer-independent source
  images. This avoids expanding Doom's fixed eight rotations into 32 unused
  slots for every frame.

Temporary loader counters showed 455 SPR loads, 90.52 MiB of indexed pixels,
20.35 MiB of frame/direction storage, and 2.78 MiB of layers before cleanup.
After sharing resource names and loading gameplay UI only: 198 SPR loads,
19.44 MiB of indexed pixels, 0.90 MiB of frame/direction storage, and 1.37 MiB
of layers. Counts include the separately owned default sprite and font. The
catalog itself is unchanged; unused screens' images remain loadable.

**Confirmed SDL allocation cause:** after asset cleanup the headless process
was around 63 MB resident after loading, but jumped above 230 MB on the first
terrain draw while live malloc bytes stayed around 39 MB. SDL's software
`SW_CreateTexture` enables surface RLE for `SDL_TEXTUREACCESS_STATIC`.
`SW_RenderCopyEx` locks an RLE source before accessing its pixels and unlocks
it afterward. Flipped atlas tiles therefore cause repeated decode/re-encode
of the entire 768x2976 ARGB atlas, leaving large freed allocations in the
allocator. Choosing `SDL_TEXTUREACCESS_STREAMING` for software textures keeps
those surfaces uncompressed and removes the spike. Accelerated renderers retain
static textures. This is SDL surface RLE, separate from native SPR compression.

Paired `/usr/bin/time -l` measurements, identical HUMAN01 screenshot:

| Measurement (bytes) | Starting revision | Final storage/software blits |
| --- | ---: | ---: |
| Maximum resident set | 337,002,496 | 66,912,256 |
| Peak physical footprint | 448,007,104 | 57,804,224 |

A normal Cocoa window with software rendering measured **70.2 MiB physical
footprint, 72.7 MiB peak** after three seconds; its pre-draw RSS included shared
macOS libraries and was about 130 MB. Do not equate RSS, physical footprint,
retained asset bytes, and virtual address space. A comparison using accelerated
scene rendering with the same cleaned assets measured 236.9 MiB footprint,
including about 166 MiB of IOAccelerator graphics allocations. Dark Colony now
selects software scene rendering by default.

**Disproven workaround:** `SDL_HINT_FRAMEBUFFER_ACCELERATION=0` did not affect
the headless RLE spike, and prevents SDL 3's Cocoa software renderer from
creating a window framebuffer. It is not retained. SDL may use a GPU texture
to present the completed software framebuffer on macOS; sprite/palette drawing
still runs on the CPU and needs no application OpenGL or palette shader. The
backbuffer and terrain atlas still use ARGB for SDL presentation/compositing.
This change does not claim a fully indexed destination surface or an exact
DirectDraw implementation. Retail's shared 8-bit destination and native RMP
lighting/blend dispatch remain the fidelity reference.

Reproduction (run the same commands at each revision):

```sh
make -j8 all
/usr/bin/time -l env SDL_VIDEODRIVER=dummy build/bin/dark-colony \
  --screenshot /private/tmp/dc-memory.bmp
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_definitions
env SDL_VIDEODRIVER=dummy build/bin/test_dark_colony_sprite_layout
find data/DCOLONY -type f \( -iname '*.SPR' -o -iname '*.FIN' \) | sort \
  > /private/tmp/dc-sprites.txt
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading \
  /private/tmp/dc-sprites.txt > /private/tmp/dc-catalog.txt
```

All 461 catalog entries match (447 loads, the same 14 rejects), including
source/team pixels and frame/layer metadata. Catalog SHA-256:
`22966ed476bd8a86bf63287a25a9af0a989d74fc9f5d2ed60a276f661788a3dd`.
HUMAN01 BMP SHA-256 before/after:
`d791c1fdb6914dcaf0cffc77a9f48250d4f276b081e9c38965fa8504c081910c`.
Temporary allocation/stage logging was removed after measurement.

**Correction to the earlier Exploiter presentation contract:** the wider suite's
`test_game_model_headless` expected both WORK states to use frame 103, but
starting revision `33f932d` already has WORK1=102 in `animate/EXPL.inc`. The user
confirmed the current animation is correct and requested updating the test.
The intended cycle is now explicitly WORK1=102 for two tics (native FIN frame
52, deployed body plus GLIT), WORK2=103 for four tics (native FIN frame 53,
body only). The test now checks this cycle; the animation table is unchanged.
This supersedes the earlier body-only presentation requirement. It is user
confirmation of intended presentation, not new executable evidence for retail
harvesting dispatch, which remains unknown.

Final verification after correcting that test: all 28 Dark Colony model tests,
SPR/FIN layout, and headless smoke checks for Dark Colony, Dark Reign,
7th Legion, and KKND pass. All four game binaries build without new warnings;
`make tags` and `git diff --check` complete successfully.

### September 10: indexed terrain and palette-driven water

**Confirmed asset/storage finding:** the ARGB terrain atlas was unnecessary.
BTS already stores 32x32 indexed tiles: count at byte 4, the 256-entry RGB palette
at byte 8, then records starting at byte 776, each a four-byte key followed by
1024 pixel indices. Retain those native pixel bytes once per tile, with a
separate palette. Neither BTS loading nor SPR loading now needs a renderer.

The previous engine loader (`86ef7e8`) added synthetic keys and full pixel
copies for every water phase. DESERT had 1382 native tiles expanded to 2222
atlas entries, occupying 768x2976x4 = 9,142,272 bytes (8.72 MiB). The indexed
replacement retains 1382x1024 = 1,415,168 bytes (1.35 MiB), plus its palette,
key lookup and one water-selection byte per tile. No synthetic tiles or water
pixel copies remain. `R_DrawIndexed` is shared by sprites and terrain and
expands only the requested source rectangle into the existing reusable SDL
upload surface. The final framebuffer/upload format remains ARGB; terrain
source storage does not. No OpenGL or palette shader is required.

**Existing engine behavior preserved, not verified retail behavior:** the old
water heuristic accepts palette indices 201–211 with R<80, G>36, B>36 and
G+B>2R. A tile animates when at least 96 pixels qualify and they comprise at
least one quarter of its nontransparent pixels. The cycling subset is the
qualifying entries within 201–207, ascending, advancing every 180 ms. At draw
time, destination palette entry i selects cycle[(i+phase)%count]; static tiles
keep the base palette. The old wave ranking selected the top seven out of at
most seven candidates, so removing that ranking changes no selection.
ATLANTIS selects five cycling colors; the other three BTS files select seven.

These thresholds, palette ranges and 180-ms period were already in the engine.
This task does not establish them from DC.EXE, nor does it establish whether
retail cycles globally or restricts cycling to specific tiles. Preserve that
unknown; a future native palette investigation should replace the heuristic
from evidence rather than tune it visually. The confirmed retail 8-bit
DirectDraw surface/shared-palette evidence remains in the preceding section.

Asset fingerprints and native tile counts:

| BTS | Tiles | SHA-256 |
| --- | ---: | --- |
| ATLANTIS | 1313 | `61a2aed6ac8d36fcb6fe07f14dfe96ead9bedf5320d6f951cbedd7d381dc9420` |
| DESERT | 1382 | `3243b51139cb3cf71de9336ee282ba1502c90e2b12520ac1c349965c7f97fc6d` |
| HTRAIN | 749 | `af18c80699bc610997fb7cc83c16599fca5677ae867b2fa0728067f492751495` |
| JUNGLE | 1320 | `ab72a4cb1358de2e5ea4ad190fe634d43046d1320311e93e6ad3e15035d8b02e` |

`test_terrain_loading` checks all 4764 native tiles across seven 180-ms phases
and all four map flips against fingerprints captured from the old ARGB path.
It additionally checks byte-identical native indexed storage, absence of
terrain textures/synthetic animations, live palette edits, transparency,
cropping, scaling and clipping. Its single-row map uses camera Y=-32 because
`L_ScreenYF` maps world Y=0 to map height; an initial baseline harness without
that offset drew outside the output and was discarded, not used as a golden.
The retained camera value is test geometry, not a gameplay offset correction.

```sh
make -j8 all build/bin/tests/dark-colony/test_terrain_loading
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_terrain_loading
env SDL_VIDEODRIVER=dummy make test-dark-colony test-layout
env SDL_VIDEODRIVER=dummy build/bin/dark-colony \
  --screenshot /private/tmp/dc-indexed-terrain.bmp
```

HUMAN01's BMP remains byte-identical to `86ef7e8` (SHA-256
`d791c1fdb6914dcaf0cffc77a9f48250d4f276b081e9c38965fa8504c081910c`).
Temporary per-BTS allocation/palette diagnostics were removed after verification.
The old auxiliary multi-game loader catalog also needed to read only allocated
sprite directions after the preceding memory change; missing slots still hash
as zero, preserving its previous fingerprint format.

Final verification: all 29 Dark Colony tests and sprite layout pass; all four
binaries build and pass dummy-video smoke checks. The full SPR/FIN catalog
remains identical to the preceding revision. The same headless HUMAN01 run
measures 48,988,160 bytes maximum RSS and 39,847,232 bytes peak physical
footprint with `/usr/bin/time -l`; these are headless process measurements,
not a new windowed-game measurement. Tags were regenerated and the diff checked.

## Confirmed fog, map flags and day/night clock (2026-09-10)

This trace supersedes the opening report's unverified fog model and the
2026-09-03 audit's remaining unknowns where explicitly identified below.
Evidence: retail `data/DCOLONY/DC.EXE`, SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
566272 bytes, PE32 image base `0x400000`. The local r2ghidra decompilation
was used to find candidates; fresh radare2 instruction listings established
addresses, register arguments, flags and integer operations. In particular,
Watcom register arguments must not be inferred from decompiler signatures.

### Map storage and visibility ownership

**Confirmed:** `0x44e720` reads 32-bit width and height, two 16-bit tile IDs
per cell, then a separate 16-bit flag plane. Runtime terrain packs
`background | (foreground << 11) | (flags << 22)` into one dword. Rows at
map `+4` are top-down; `+0x404` is a reversed row-pointer view of the same
storage. The visibility/occupancy rows are at `+0x804`; additional occupancy
planes are at `+0xc04` and `+0x1004`. These are distinct from terrain.

`0x44ea30` forces terrain bit 29 on when bit 31 is absent: in the on-disk
flag word, non-obstacles (bit 9 clear) always get sight-pass bit 7. Bit 8
means near-only current visibility. Bit 9 remains the movement obstacle.
The existing tile-flip interpretation (bits 5 and 6) is unchanged.

The engine now retains the full normalized flag word instead of discarding
all but movement and flips. Level cleanup owns this plane and the visibility
plane. It still decodes tile IDs into the common renderer arrays; it does not
copy DC's pointer aliases or impose its 11-bit tile-ID packing on other games.
As with Doom's level geometry and REJECT storage in
`reference/DOOM/p_setup.c`, visibility belongs to the active
level, independent of renderer resources. A tile grid is necessary here:
DC's authored traversal consumes cell flags, whereas Doom's REJECT bits
represent sector pairs.

**Confirmed engine bug:** bottom-up terrain rectangles were drawn using
`L_ScreenYF(height,y) = height-y`, the transform for continuous world points.
The first terrain row therefore started one cell below its proper rectangle.
Tile, overlay and blocked-cell drawing now use `L_ScreenY = height-1-y`,
already used by the minimap and tile sort keys. World points retain the
continuous transform. The two-row framebuffer test verifies the alignment;
the BTS catalog still produces identical pixels for every tile/flip/water
phase when its camera targets the corrected cell rectangle.

**Correction:** the `kev: maskbuffer` allocation at `0x432c53` is only
`0x7000` bytes, stored at `0x4e1844`; the references examined do not make it
the simulation visibility plane. `0x44ecd0` separately allocates the
viewport-sized light buffer at viewport `+0x1c`. Neither allocation's label
establishes a three-value simulation encoding.

### Sight traversal and bit layout

**Confirmed:** explored terrain uses bit 31, `0x80000000`. Current sight for
team `t` uses `0x40000000 >> t`, for teams 0 through 7. The local allied mask
is stored at game `+0x19c0` with team stride `0xe30`. The clear pass at
`0x441a20` removes current team visibility while preserving exploration and
native occupancy/memory metadata. The engine stores current team bits plus
persistent local exploration; SCN alliances initialize the team masks.

`0x446158` visits active objects (native stride `0xdc`), requires team < 8,
and skips object `+0xcb` values 1 and 2. It selects one of sixteen specialized
traversals according to flying sight, aircraft detection, local alliance and
whether clipping is necessary. `0x4447a0` and `0x4458d0` establish the ground
rules; `0x441d54` establishes flying sight:

- Reveal the current cell before pruning. If flag bit 7 is absent, stop its
  descendants for ground observers. Flying sight ignores this pruning.
- At tree depth >= 2, flag bit 8 suppresses current team visibility, but an
  allied observer still marks the terrain explored. This rule also applies
  to flying sight. It does not itself stop descendants.
- Clipped traversals stop an out-of-bounds branch.

The root pointer table starts at `0x483fbc`. Indices 1 through 10 point to
`0x477128`, `0x477364`, `0x477860`, `0x4780cc`, `0x478eb8`, `0x47a224`,
`0x47bbc0`, `0x47dd9c`, `0x480918`, and `0x483f94`. Nodes are 44 bytes:
signed X/Y offsets, `(child_count-1)*4`, and eight child pointers. The stack
visits children in reverse pointer order; `0x47608c` supplies depth increments.
Open-terrain counts are 5, 13, 29, 49, 81, 113, 149, 197, 253, 317. Each is
an integer Euclidean circle, but its parent branches determine occlusion.
A generic ray or distance test would not reproduce that pruning.

The new C extractor verifies that all ten trees are restrictions of the
largest tree with identical cells, parent coordinates and depths. It also
checks that squared distance strictly increases along every parent edge.
The engine therefore needs just 317 offsets/depths/subtree ends; skipping a
subtree preserves native occlusion without ten duplicate tables. Table index
0 is not one of these roots. Values at indices 11/12 are 1/15, not valid
pointers, despite the traversal's broader radius bound; the engine accepts
only the ten verified radii.

### Soft square brightness, not a DOTT overlay

**Confirmed:** `0x44ee68` samples brightness 16 for currently visible terrain,
10 for explored terrain and 0 for undiscovered terrain. The bypass flag
skips visibility masking. It replicates boundary samples, averages four
adjacent cells with `sum >> 2` at each tile corner, and interpolates across
each 32×32 tile. First interpolate left/right edges vertically with integer
division by 31, then interpolate horizontally with another division by 31.
Do not replace this with one floating-point bilinear interpolation: intermediate
truncation changes the stepped contour.

`0x44ecd0` constructs the 17×17×32 lookup table at `0x50f62c`:
`(((31-position)*a + position*b)/31) << 3`. Rendering ORs the selector into
the bottom three bits. Thus there are seventeen brightness levels, with
square/rectangular patches formed by quantized interpolation, not a fixed
2×2 alpha mask inside each terrain tile. The user's screenshots are visually
consistent with these patches; the algorithm and constants come from the
instructions, not tuning against a screenshot.

`0x44a7b0` generates the RMP light bank using intensity 0..31 and selector
0..7: channels are scaled by intensity/16 and clamped, then mapped back into
the palette. The selector also affects color remapping, including team slots
138..143. The engine implements the same 0/10/16 samples and integer light
field as its default, then multiplies the composed RGB world by light/16.
It does **not** yet reproduce every RMP nearest-palette choice or the native
per-layer light selector. UI is drawn afterward; the fog texture is clipped
to the world viewport and owned by the renderer. Terrain sampling is stable
under camera scrolling and independent of unit sprite dimensions.

**Disproven:** DOTT.SPR is not the source of this fog gradient. Its GAMESTAT
field with value 7 is not a seven-cell radius. The native radius is from
OBS_DAY/OBS_NIGHT; DOTT has 8/8. The previously inferred 0/1/2 cell encoding
and per-unit radial-sprite compositor are superseded by the bitfield and
light-buffer consumers above.

### Day/night timing, vision and HUD

**Confirmed:** GAMESTAT loader `0x4385f8`, specifically the destination
addresses near `0x4387bb–0x4387c5`, places OBS_DAY at type `+0x14`
(`0x4ec894`) and OBS_NIGHT at `+0x10` (`0x4ec890`), stride `0x118`.
Consequently game `+0x540` is a **night weight**, 0 by day and 256 at night.
`0x446240–0x44625e` computes
`(night_weight*night + (256-night_weight)*day) >> 8`.
Troopers have 7/4 sight and Greys 4/7; these are separately authored C stats,
not a runtime dependency on extracted GAMESTAT tables. Field 12, copied to
`0x4ec8e0` (type `+0x60`), selects the flying-sight traversal. Field 10 is a
movement category, despite the older generated enum naming it FLY. Scout,
Ortu and buildings bypass ground pruning; ordinary infantry does not.

SCN load `0x41a61c` writes starting phase to `+0x53c`, phase length to
`+0x534`, elapsed phase time to `+0x530`, and transition length to `+0x538`;
initial night weight is `phase << 8`. These are header values 2..5 in our
parser. HUMAN01 supplies day, 6750, 1500, 75. The ticker at
`0x418aba–0x418b46` increments elapsed time, flips phase and resets elapsed
to zero when elapsed **exceeds** phase length. During the transition:
night weight is `(elapsed<<8)/transition` entering night, or its complement
to 256 entering day. `0x418b54` refreshes sight every sixteen native tics;
`0x4189b8` supplies startup visibility. A full native tick is 66 ms at the
default speed (`0x41a728`, `0x41cb5a–0x41cbc5`, documented above). The engine
uses cumulative 66 ms boundaries on its 30 Hz thinker clock, avoiding a
cycle that runs twice as fast. This environment cadence is the shared
engine default; maps without a cycle duration stay in daylight.

HUD initialization `0x437630` loads `sprites/cloc`, divides its frame count
in half and divides phase duration by that half. `0x4376a8` chooses a frame
from the first half by day and the second half by night, clamping the exact
phase endpoint to its last frame. `0x4377e3–0x437806` subtracts the SPR
cell displacement from the (608,450) draw origin; the blitter reapplies it.
`0x437824` prints the total elapsed clock divided by phase length and by two.
The engine uses this native dial from its separate UI image cache and removes
CLOC from gameplay sprite IDs. Its day counter now uses the world clock,
not a parallel HUD timer. `SPRITES/CLOC.SPR` contains 36 28×28 cells with
(66,67) displacement; SHA-256
`99453d9544a3afbd64af760e9e1baf437127f30f7c9c4e848f70fbe1157fcb65`.
GAMESTAT.TXT SHA-256:
`ed13afe21ffea368a5892b49de40ef063014c0a9376c5d5bb5abf1396cb27629`.

**Unknown / not inferred:** a separate night terrain tint. Examined direct
phase/weight consumers cover sight, clock UI, saved state and script gates.
The world queue initializes global light `0x474660` to 16. The call immediately
following the transition, `0x440ab4`, only increments the pathfinding stamp
`0x475974` by two; it is not a palette update. No unsupported night color or
darkening constant has been introduced.

### Remaining fidelity boundaries

The common renderer, picking, minimap, model snapshots and target acquisition
now use current sight; exploration survives losing sight and observer death.
Games whose authored sight is still absent use an explicit engine default of
seven cells for selectable actors. This is engine policy, not evidence about
Dark Reign, 7th Legion or KKND. Dark Reign now initializes `team` from `owner`
so enemy units cannot reveal the player's map by retaining team zero.

Native `0x450078` also remembers object/type metadata in visibility-cell bits
10..17; drawing at `0x4368c9–0x436a2b` uses remembered representations outside
current sight. That object-memory rendering is not yet ported. Other
unported details include detection updates at object `+0xca` (type `+0x6c`),
status-10 sight shrinking by `(radius*(150-object[+0x46]))/150`, and special
observer eligibility outside our live actors. Native super-unit types
69..76 have 10/8 or 8/10 sight, but the current engine maps them to the normal
Trooper/Grey actor types; they still inherit those normal stats. These limits
must not be mistaken for confirmed native equivalence.

### Reproduction and verification

```sh
make build/dc_sight_gen
build/dc_sight_gen data/DCOLONY/DC.EXE > /private/tmp/dc-sight.h
cmp play/p_sight_data.h /private/tmp/dc-sight.h
r2 -q -e bin.cache=true -e scr.color=false \
  -c 'af @ 0x44ecd0; pdf @ 0x44ecd0; af @ 0x44ee68; pdf @ 0x44ee68' \
  -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -e scr.color=false \
  -c 'af @ 0x4458d0; pdf @ 0x4458d0; af @ 0x446158; pdf @ 0x446158' \
  -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -e scr.color=false \
  -c 'af @ 0x418818; pdf @ 0x418818; af @ 0x437630; pdf @ 0x437630' \
  -c q data/DCOLONY/DC.EXE
build/dc_info_conv --cell 0 data/DCOLONY/SPRITES/CLOC.SPR
env SDL_VIDEODRIVER=dummy make test-dark-colony test-dark-reign test-7legion test-kknd
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/dc-fog.bmp
```

`test_fog` checks all circle cells, branch occlusion, flying and near-only
rules, map edges, alliances, retained exploration, day/night sight reversal,
phase transitions, native clock cadence, exact interpolation samples,
software-rendered brightness, camera movement, HUD exclusion, bottom-up row
alignment, and real HUMAN01 flag/header decoding. The BTS render catalog
checks unchanged source pixels and metadata. The separate cross-game DC
command fixture still fails to earn its required build funds on the unchanged
revision as well; the Dark Reign command test now rejects unseen targets.
