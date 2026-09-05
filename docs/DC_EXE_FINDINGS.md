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

## Animation generator coverage

**Confirmed from `data/DCOLONY/ANIMATE/EXPL.FIN` and the focused layout test:**
`EXPLMOVE0` through `EXPLMOVE15` provide 16 directional labels. Each label
contains two temporal body frames, so the generated `S_DC_EXPL_RUN1` and
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
frames per label are confirmed. No additional Exploiter frames should be
invented without another native asset or executable trace.

## State-driven sprite rotations

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
