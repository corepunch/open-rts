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

- dreamerman / SPR2BMP:
  http://www.dreamerman.cba.pl/
  - Original Dark Colony file-format reverse engineering lead credited by both
    DSPR and XSPR descendants. The host may return HTTP 421, so use mirrors,
    bundled readmes, and downstream source when the site is unavailable.
  - Wrote `SPR2BMP` and the early guide used by later viewers.

- Dmytro Malikov / DSPR:
  http://malikov.us/dspr/
  - Dmytro's Sprite Viewer, credited by the Kotlin DSPR port. Use it as a
    historical source for the Dreamerman-derived viewer lineage.

- ACOM / XSPR:
  http://xinth.net/xspr/
  - 2017 ES6 browser viewer for Dark Colony files. Known supported formats from
    downstream ports: `.SPR`, `.BTS`, `.FIN`, and `.MAP`.
  - The Java/JavaFX `jxspr` port below is the practical source-code reference
    for the XSPR parser behavior.

- smdimos/jxspr:
  https://github.com/smdimos/jxspr
  - Java/JavaFX port of ACOM's XSPR viewer. Tiny repository with direct parser
    source: `SPR.java`, `BTS.java`, `MAP.java`, `PixelCanvas.java`.
  - `SPR.java` confirms `.SPR` header byte `0` value `129` marks compressed
    RLE, frame descriptors start at byte `776`, and the descriptor words are
    `width`, `height`, `disX`, `disY`.
  - `SPR.java` and `BTS.java` scale palette channels as `stored * 4 + 3`, not
    plain `stored * 4`.
  - `SPR.java` confirms indices `138..143` are the six team-color slots and
    remap by `id += (team - 7) * 6`, with team `7` as Aerogen/cyan default.
  - `BTS.java` confirms magenta transparency is palette RGB `(255, 3, 255)`.
  - `MAP.java` confirms tile flip bits in the map flag word: bit `5` flips the
    main tile, bit `6` flips the overlay tile.

- darekbx/DSPR:
  https://github.com/darekbx/DSPR
  - Kotlin/Android port of Dmytro's DSPR. The README credits Dmytro and
    dreamerman, and says the module renders `.SPR`, `.BTS`, `.MAP`, and `.FIN`.
  - `SPR.kt` independently confirms the same `.SPR` layout, `*4+3` palette
    scaling, RLE decode, team-color slots `138..143`, and optional displacement
    drawing.
  - `FIN.kt` names the high-level `.FIN` layout: header words include a count
    of 164-byte unknown/weird blocks, animation labels are 20 bytes each, and
    animation frame commands are 22 bytes each. Our extractor uses this exact
    `refs + labels + aux_count * 164` layout for the command table.
  - `MAP.kt` exposes extra collision/debug interpretations for map flag bits,
    but rendering-relevant bits `5` and `6` match `jxspr`.

- darekbx/alien-colony:
  https://github.com/darekbx/alien-colony
  - LibGDX game using Dark Colony graphics. Useful as a practical rendering
    reference if future work needs a full game-loop example rather than a file
    viewer.

- endotermic/Dark-Colony:
  https://github.com/endotermic/Dark-Colony
  - Open-source Dark Colony reference lead. Use it when validating `.MAP`,
    `.BTS`, `.SPR`, object placement, palette cycling, and faction/unit logic.
  - Contains original Classic/Council Wars game-data snapshots and a Ghidra
    workspace. `REAP.SPR` and `REAP.FIN` match our local data byte-for-byte.
  - The bundled "Dark Colony - Map editor" readme describes Petra-7 placement
    as two separate steps: first place `lar(vent)` blocks on the terrain, then
    use the Lar Attributes menu to attach vent info. That means `.SCN` rows like
    `x y 40 rate amount` are model/resource metadata and should not be treated
    as unconditional visual sprite placements.
  - The map editor and Classic data snapshots both include identical
    `SCENARIO/VENT.JUS` and `SCENARIO/ALL.JUS` files. These use the same
    descriptor shape as `.SPR` (`flags`, `frame_count`, palette, then
    `width/height/disX/disY` records), but are editor block/stamp palettes.
    `VENT.JUS` frame 0 is the yellow glow and frame 7 is the brown crater stamp.
  - `DecompiledWithGhidra/` is a Ghidra project with two imported programs:
    `dc16.exe` and `ENGEXP16.EXE`. It does not contain exported decompiler C
    files, but its project state preserves useful renamed functions, navigation
    history, and the binary string tables. Open it in Ghidra for deeper work;
    export decompiler text from there before relying on exact control flow.
  - The Ghidra project history says the prior reverser focused on CD checks and
    interface/startup routines. Named functions currently visible in project
    state include `FUN_CheckCd_1`, `FUN_InterfaceIntro_0`,
    `FUN_InterfaceNewNetworkGame_0`, `FUN_RunProgram_0`,
    `FUN_RunProgram_1`, and `FUN_CreateWindowRunProgram`. Useful addresses:
    `dc16.exe` has `FUN_CheckCd_1` referenced 16 times; its current browser
    focus is around `00405c40`. `ENGEXP16.EXE` has run/create-window helpers at
    `00405264`, `0042e6c8`, and `0042e6e8`.
  - Classic and Council Wars binaries expose many original source-module names
    in strings: `scenario.c`, `animate.c`, `button.c`, `widget.c`, `gadget.c`,
    `sprites.c`, `depend.c`, `trigger.c`, `objects.c`, `mobiles.c`, `city.c`,
    `tile.c`, `mapit.c`, `lighting.c`, `pervasve.c`, and AI modules such as
    `krusty_general.c`, `krusty_attack.c`, `krusty_defend.c`,
    `krusty_scout.c`, and `krusty_army.c`. Use these names when naming our
    loader/model files and when searching the decompile.
  - Original engine assertions use object positions as 8.8 fixed point:
    `gs->map->load[vent->z_pos>>8][vent->x_pos>>8]&(1<<ALIVE_MINE)` and
    `a->action.setmoney.x == gs->all_objects[troop].x_pos>>8` /
    `a->action.setmoney.z == gs->all_objects[troop].z_pos>>8`. This confirms
    map-cell coordinates are produced by shifting fixed-point object x/z
    positions by 8; internal vertical map coordinate is named `z`, not `y`.
  - The original UI parser is file/layout driven. Strings name widget and
    interface keywords including `size`, `pictures`, `background`, `palette`,
    `text`, `font_offset`, `bright_pushed`, `bright_highlight`, `remap`,
    `intens`, `read_only`, `immediate`, `read_write`, `images`, `pushb`,
    `checkb`, `picture`, `in_text`, `count`, `scount`, `group`, `list`,
    `scroll`, `colour`, `font`, `animation`, `textmsg`, `gadget`, `label`,
    `banim`, `mask`, `unmask`, `anim_loop`, `anim_oneoff`, and
    `anim_stopped`. Original interface assets include `intrface/main`,
    `intrface/client`, `intrface/insee`, `intrface/lobj`, `intrface/lopt`,
    `intrface/loadg`, `intrface/wingame.dat`, and multiplayer screens such as
    `intrface/multi`, `intrface/multiwin.dat`, `intrface/net.dat`,
    `intrface/server.dat`, and `intrface/tcpwait.dat`.
  - Original trigger/script keywords visible in the binary include `newrate`,
    `newtype`, `newrate2`, `setmoney`, `waypoint`, `setarray`, `setlifes`,
    `exomoney`, `vision`, `nopickup`, `funkytower`, `ally`, `dfiddle`, `bail`,
    `artifact`, `abduct`, `reinforce`, `noundeploy`, `found`, `norm`, and
    `trip`. Treat `.TRO` support as a first-class gameplay scripting task, not
    as ad hoc mission special cases.
  - Original animation/state strings visible in the binary include `MOVE`,
    `STAND`, `DIEA`, `DIEB`, `DIEC`, `DEPLOY`, `FUNK`, `BUILDSTAND`, `BUILD`,
    `SCRCH`, and `BURN`. Weapon/effect tokens include `BULLET`, `EXPLODE`,
    `EXPL`, `FIRE`, `FIREA`, `FIREB`, and `FIREC`.
  - Binary strings confirm the original data filenames and entry points we keep
    touching: `anim.dat`, `animate/%s`, `sprites/%s`, `sprites/cloc`,
    `gamestat/gamestat.txt`, `gamestat/weapstat.txt`,
    `gamestat/boomstat.txt`, `gamestat/mbullet.txt`, `gamestat/depend.txt`,
    `gamestat/unitid.txt`, `scenario/mplayer/%s`, `sound/slist.dat`,
    `sound/sound2.dat`, `fade.dat`, and `primes.dat`.
  - CD-check strings differ between Classic and Council Wars:
    `hbnfufl.a01` for Classic and `hbnfufl.a02` for Council Wars. This is only
    useful when validating executable variants; it should not affect data
    loading.

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
  - `OpenRA.Mods.DarkColony/SpriteLoaders/BTSLoader.cs` confirms `.BTS` tiles
    are `u32 id + 32x32 indexed pixels` after an 8-byte header and 768-byte
    palette. `SPRLoader.cs` confirms `.SPR` descriptor offsets are first-class
    frame placement data, not padding.
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
- `GAMESTAT.TXT` speed values are pixel-scale movement rates on 32px Dark
  Colony cells. Convert to model cells/sec by dividing by 32, not by a small
  gameplay fudge factor.
- `data/DCOLONY/SCENARIO/*.MAP`, `*.SCN`, and `*.BTS` for maps, starting
  objects, tilesets, and water palette bands.
- Dark Colony `.SCN` object rows shaped `x y 40 rate amount` are Petra-7 vents,
  not ordinary starting units. The original mission text explicitly calls them
  Petra-7 vents and training orders say to move/deploy the Exploiter or Brozaar
  over **active** vents to extract P-7. The fourth field is the vent extraction
  rate; `0` means dormant/inactive. The fifth field is remaining P-7 amount.
- Human02 shows why rendering every type-40 row as `VENT.SPR` is wrong: all four
  type-40 rows have matching 3x3 terrain crater blocks in `.MAP` at
  `x, height - 1 - y` (`4815` is the desert crater center), while the direct
  row coordinate can be ordinary terrain. The visual vent block comes from map
  terrain/stamp data; the row supplies resource state/rate/amount.
- Dark Colony `.TRO` scripts control vent eruptions. Commands such as
  `newrate 25 38 29` and `newrate2 81 77 15` set the extraction rate at an
  existing vent coordinate; `setmoney x y amount` sets the remaining P-7. This
  matches training text like "Second Vent Has Become Active" and "Watch For
  Erupting Vents."
- The original binary's `trigger.c` strings list the wider `.TRO` command set:
  `newrate`, `newtype`, `newrate2`, `setmoney`, `waypoint`, `setarray`,
  `setlifes`, `exomoney`, `vision`, `nopickup`, `funkytower`, `ally`,
  `dfiddle`, `bail`, `artifact`, `abduct`, `reinforce`, `noundeploy`, `found`,
  `norm`, and `trip`.
- Dark Colony `.MAP` files have a flags plane after the terrain tile planes.
  For ground units, flag bit `9` (`0x0200`) marks impassable terrain. `.PTH`
  files contain path/family data and are not the terrain passability mask.
- `data/DCOLONY/SPRITES/*.SPR` for unit sprites.
- Dark Colony `.SPR` descriptor `disX/disY` values look like canonical
  screen-space placement coordinates for each frame. Unit rendering should treat
  the model coordinate as the ground/feet point and place the decoded frame from
  `.SPR` descriptor metadata plus the active `.FIN` frame-part draw offset.
  Do not infer the feet/ground point from opaque pixels: sprite pixels include
  shadows, outlines, weapons, effects, and frame-specific protrusions that drift
  independently of the unit's simulation anchor.
- When a Dark Colony frame is rendered with the FIN/SPR flip flag, mirror the
  visible hit-test bounds across the decoded SPR canvas. Raw FIN state sprites
  are positioned by FIN top-left coordinates plus SPR descriptor displacement,
  not by inferred ground anchors.
- Selection circle radius comes from `MobjInfo.radius`/`GAMESTAT.TXT`, converted
  from Dark Colony pixel units to model cells by dividing by 32. Rendering can
  clamp to a sprite-width minimum for readability, but pathing and interaction
  should keep using the model radius.
- `VENT.SPR` is a state bundle, not a four-frame animation: large active, large
  inactive, small active, small inactive. `VENT2.SPR` is a two-frame small
  yellow glow and can be used as the active/blinking overlay when the local data
  set does not include `SCENARIO/VENT.JUS`.
- `BEAC.SPR` has a base beacon frame and a separate glow frame; preserve the
  sprite palette for the glow and render it as an overlay rather than tinting the
  base sprite.
- `data/DCOLONY/ANIMATE/*.FIN` for sprite animation labels and frame ranges.
- `data/DCOLONY/ANIM.DAT` — newline-delimited index of all `.fin` filenames
  (lowercase) used by the original engine at startup; useful for bulk-loading.

### Dark Colony SPR/FIN Sprite Placement

Each `.SPR` frame descriptor is raw data: `width`, `height`, `disX`, `disY`.
Our Dark Colony loader keeps those values unmutated: frame pixels are decoded at
the start of their atlas cell, the source rectangle is the raw `width × height`,
and `disX/disY` are stored as frame metadata. It does not bake displacement into
the bitmap, subtract a minimum displacement, or synthesize a ground anchor.

Each `.FIN` frame command is also raw data. For `RTS_STATE_COORDS_FIN_TOP_LEFT`,
the renderer places a state sprite from `grid_to_screen(unit) + FIN x/y` plus
the horizontal SPR displacement only when the FIN command is unflipped. Flipped
FIN commands already carry the mirrored X placement. The renderer does not add
SPR `disY`; FIN Y is the frame's bottom edge in draw space, so runtime draws the
source rectangle at `FIN.y - height`. Do not re-center, infer feet from opaque
bounds, or apply building-specific canvas bottom tweaks.

Example: `EXPL.SPR` frame `0` is `46×49` with `dis=(137,104)`, while
`EXPL.FIN` label `EXPLSTAND0` draws frame `0` at `(-159,19)`. Runtime draws the
raw `46×49` source rectangle at `origin + (-22,-30)`, derived from
`FIN.x + disX` and `FIN.y - height`. Flipped label `EXPLSTAND6` draws frame `6`
at `origin + (-30,-32)` with no extra `disX` or `disY`.

### Dark Colony Exploiter Animation States

The Exploiter (`EXPL.SPR`) uses a Doom-style state machine with these phases:

**Mobile (body frame varies by direction, no overlay):**
- `S_DC_EXPL_STND` — idle, `tics=-1` (infinite hold), `misc1=1`
- `S_DC_EXPL_RUN1/RUN2` — walking 2-frame loop, `misc1=2`

**Deploy sequence (body frame 14 fixed, overlay extends turret arm):**
- `S_DC_EXPL_DEPLOY1..DEPLOY10` — 10 states, `tics=3` each, `misc1=5`
- Overlay frames progress 15→16→17→…→23 for E/SE/S/SW-facing directions.
  N/NW/W/SW-facing directions use frame 15 throughout the deploy sequence
  (the turret arm is visually in its base/folded position for those angles).
- All deploy states use `misc1=5`, which blocks `update_unit_harvest` from
  re-triggering the deploy while it is already playing.

**Work/harvest loop (body frame 14 fixed, overlay pulses):**
- `S_DC_EXPL_WORK1..WORK15` — 15-state loop, `WORK1 tics=3`, `WORK2-15 tics=5`
- `WORK1` and `WORK15` use overlay frame 24 (turret-top rest position, `Y=-27`);
  `WORK2..WORK14` pulse through frames 25→26→27→28→29→30→32→33→…→25 at
  `Y=-31..-33`. The brief 5-tick dwell at frame 24/`Y=-27` at the end of each
  cycle creates a subtle visual "settle" before the next pulse.
- `WORK15` loops back to `WORK1` (`nextstate = S_DC_EXPL_WORK1`). All work
  states have `misc1=5`.
- The harvest guard in `engine_units.c` triggers `set_unit_state(DEPLOY1)` only
  when `state->misc1 != 5`, so the work loop runs uninterrupted.

**Death sequence:**
- `S_DC_EXPL_DIE1..DIE6, CORPSE` — `misc1=4`, triggers `A_DC_Fall` at DIE1.
- `A_DC_Fall` strips `RTS_TRAIT_HARVESTER` and `RTS_TRAIT_MOBILE` from the
  unit's traits and sets `death_started=true`.

**`A_DC_Fall` side-effects:**
Clears selection, path, attack/harvest targets and cooldowns; strips
`SELECTABLE`, `MOBILE`, `ATTACK`, and `HARVESTER` traits. Must only fire from
the death sequence — not from the WORK loop.

### Dark Colony Harvesting Interaction Radius

`unit_harvest_interaction_radius_cells()` returns `0.05f` cells. The Exploiter
must be within 0.05 cells of the vent center (`vent->gx + 0.5,
vent->gy + 0.5`) for harvesting to tick. `separate_units` can push a 0.5-cell
radius Exploiter outside this window; when pushed out, the harvest timer stops
but the animation state (misc1=5) is not reset, so the WORK loop continues
playing without actually extracting resources.

### Dark Colony SPR Binary Format

```
offset  size  field
0x00    u16LE flags          bit 7 = per-frame RLE compression
0x02    u16LE frame_count
0x04    4     padding / unknown
0x08    768   palette        256 × RGB, 6-bit channels scaled as stored*4+3
                              (index 0 = transparent for sprites)
0x308   frame_count×8  frame descriptors:
              u16LE width, u16LE height, u16LE dis_x, u16LE dis_y
after descriptors: pixel data
  if compressed: per-frame [u32LE chunk_size][RLE data]
    RLE: signed byte cmd; cmd<0 → skip (-cmd) pixels; cmd≥0 → copy (cmd+1) pixels
  if uncompressed: raw indexed pixels, row-major, width×height per frame
```

Palette indices `138..143` are the six team-color slots. XSPR/DSPR remap those
with `id += (team - 7) * 6`; team `7` is the default Aerogen/cyan palette. Our
renderer builds remapped atlas textures for Dark Colony sprites that contain
those indices and selects them when FIN remap is non-zero.

### Dark Colony FIN Binary Format

`.FIN` files map human-readable animation label names to ranges of FIN draw
commands. Those commands then point at raw frames inside one of the companion
`.SPR` files. `ANIM.DAT` lists all `.fin` stems; the matching unit `.SPR` is in
`data/DCOLONY/SPRITES/`.

```
offset  size  field
0x00    u16LE magic          always 0x001d (29); treat as version/format ID
0x02    u16LE aux_count      DSPR names these 164-byte "weird blocks"
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
              bytes 14..15 remap   palette remap selector
              bytes 16..17 intens  color intensity; 16 is normal
              bytes 18..19 layer   1 for main unit body, 3/5 for effects/overlays
              bytes 20..21 flags   bit 0 is horizontal flip in current samples
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

The direction code set and ordering **do not differ by sprite**:

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
points at the unit's muzzle flash state. Layer-5 unit-sprite commands are separate
same-sprite weapon overlays; for Trooper fire states they are attached to the
state overlay fields so the visible barrel flash appears with the BLAZ light.

`EXPL.FIN` uses the same command table layering for the Exploiter's Petra-7
vent attach animation. `EXPLDEPLOY14` alternates `expl` layer-1 body frame `14`
with `expl` layer-0 top/turret frames `15..24`; `EDPLYSTAND14` uses body frame
`14` plus top/turret frame `25`. These layer-0 same-sprite commands are not
replacement body frames. The generator pairs them with the nearest body command
for the same state and stores the offset as `overlay.x - body.x`,
`overlay.y - body.y`. Those stored offsets are FIN command-space deltas, not
final top-left screen deltas: the renderer must also account for the body frame
and overlay frame `.SPR` descriptor/ground points before drawing the overlay.
Layer-5 `hit*` commands in the opposite-facing attach labels are effect sprites,
not the Exploiter's main body or top piece. `GAMESTAT/GAMESTAT.TXT` has a
separate "Human mining tower" entry named `EDPLY` at type 47, so the deployed
miner should resolve to `EDPLYSTAND*`, not the mobile `EXPL` loop. Do not use
`SLUGFUNK*` as the normal human mining loop: those labels carry flipped
same-sprite `expl` commands and represent a different mirrored/side path.
`EDPLYSTAND14` is the deployed tower body/top pair; `EDPLYSTAND2` is the other
deployed view, body frame `34` with a layer-5 `hitd` effect. The upright mining
pulse is not the `FUNK` labels: `EXPLFUNK*` and `SLUGFUNK*` carry smoke and/or
flipped `expl` commands, matching the bad mirrored result seen in-game. The
non-flipped pulse keeps body frame `14` fixed and derives the layer-0 tower top
from original animation labels rather than a hand-authored frame table. The
path is: `GAMESTAT/GAMESTAT.TXT` names the deployed "Human mining tower" object
`EDPLY`; `DC16.EXE` exposes state keyword roots including `STAND`, `DIE*`, and
`BLOOD%c`; `EXPL.FIN` then provides the actual command ranges. The generated
loop appends top frames from `EDPLYSTAND14`, `EXPLDIE0`, and every
`EDPLYBLOOD?0` label in FIN table order, skipping only consecutive duplicate
frames. This resolves to `25,26,27,28,29,30,32,33,32,30,28,27,26,25,24`;
`EDPLYBLOODA0` supplies the peak frame `33`. Despite the names, `EXPLDIE0` is
only the rising half of the deployed pulse; looping it alone snaps from frame
`32` back to `25` instead of playing the full pulse return.

The `dc16.exe` strings around `0x82434..0x8250c` (`Frame parts`,
`BManimation`, `%s%s`, `%s%d`) and the `juicel.c` / bad-juice-file assertions
confirm the original path as a frame-part renderer: animation labels are looked
up by string, then each part supplies a sprite cell, draw x/y, layer, and flags.

Dark Colony `.TRO` script `c>` conditions are mission counter values, not
30 Hz render ticks. Human02 has an enemy reinforcement at `c>300` near the
Petra-7 vent (`reinforce 3 69 52 ...`), and treating `c` as 33 ms spawns that
wave around 10 seconds after load. Those Grays are exactly four cells above the
mining vent and kill the Exploiter, which looks like the deployed mining
animation cancels. Use a one-second counter scale until the original counter
rate is identified more precisely.
The cell metadata includes `xoffset`/`yoffset`, so multi-part sprites must place
each part with its own cell descriptor rather than inheriting the body part's
top-left rectangle.

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

### DC.EXE / DC16.EXE Findings

`data/DCOLONY/DC.EXE` (566 KB) and `data/DCOLONY/DC16.EXE` (637 KB) are the
original Dark Colony DOS MZ executables. Neither contains game data beyond the
engine itself; all unit, sprite, and animation data lives in the external files.

**Source files embedded as assert strings** (incomplete list — useful for
orienting reverse-engineering efforts):
`animate.c`, `juicel.c`, `mobiles.c`, `objects.c`, `vobj.c`, `engmain.c`,
`sprite.c`, `sprites.c`, `collide.c`, `path.c`, `ai.c`, `trigger.c`.

**FIN files are called "juice files" internally.** The engine loads them from
`animate/%s` (i.e. `animate/reap.fin`) and refers to animation label ranges as
"angles". Assert strings: `"Whoa, One of my animation angles is NULL."`,
`"Whoa Batman, I don't have any record of juice file %s"`.

**Animation name construction.** The engine builds animation label names by
concatenating a unit stem, a state keyword, and an integer angle suffix — format
string `%s%s%d` is present at `0x7d6d8` immediately before `%s%d` at `0x7d6e8`,
both adjacent to `BManimation` and `ANIM_NAME_LEN`. This confirms the engine
looks up labels like `REAPMOVE0`, `REAPMOVE1` … `REAPMOVE15` directly by
number, using the unit's current heading converted to an angle index.

**State keywords found in the EXE** (`0x7f270`, near the unit-definition
loader): `MOVE`, `STAND`, `DIEA`, `DIEB`, `DIEC`, `DEPLOY`, `FUNK`,
`BUILDSTAND`, `BUILD`, `SCRCH`, `BURN`, `BLOOD%c`. The `BLOOD%c` entry uses a
`%c` suffix, not a number, suggesting blood effects have a letter variant code.

**16-direction walk animation: what the FIN data tells us.** Reaper's apparent
"missing" walk frames are not hidden in `REAP.SPR` and are not hardcoded in the
EXE. They are encoded in `REAP.FIN` as repeated frame-part commands with the
FIN flip flag set and a different draw x offset. Example: `REAPMOVE0` contains
eight body commands:

```
9, 17, 25, 33, 9 flipped, 17 flipped, 25 flipped, 33 flipped
```

The flipped half uses x offsets around `-24` while the unflipped half uses
offsets around `-157`. So the original animation system is not "just raw SPR
frame index"; it is `SPR cell + FIN flags + FIN draw offsets`. Several other
16-direction units use the same idea (`PSYC` has five frames plus five flipped
frames per even direction; `BARR` mixes short repeated cycles with flip flags).
`SARG` is a counterexample with eight distinct body frame commands per even
direction and no flip flags in the walk rows.

Even-numbered angle labels (`REAPMOVE0`, `REAPMOVE2` … `REAPMOVE14`) hold the
animated walk cycles. Odd-numbered angle labels (`REAPMOVE1`, `REAPMOVE3` …
`REAPMOVE15`) are still single intermediate-angle body poses. The EXE strings
confirm the engine builds animation names with `%s%d` / `%s%s%d` and asserts
when any animation angle is NULL, so these labels are expected to exist and are
queried by angle number.

Renderer implication: flipped FIN frame parts must honor the FIN command offset
as well as the flip flag. Mirroring the already-normalized SPR canvas around its
center is not equivalent to the original renderer unless the FIN x/y draw
offsets are also applied or converted into the engine's unit anchor space.

By contrast, 8-direction units (TRSC, GRAY, SCYT, XENO) only have even-numbered
MOVE labels in their FIN files; the engine presumably only queries even angles
for those units.

**Gamestat fields.** `data/DCOLONY/GAMESTAT/GAMESTAT.TXT` column layout
(0-indexed, whitespace-delimited):

```
col  0  name
col  1  team (0=human, 1=alien)
col  2  TurnSpeed
col  3  ObsDay
col  4  ObsNight
col  5..7  WeaponID×3
col  8..9  xsiz / ysiz (sprite cell size)
col 10  fly (0=ground, 1–5=vehicle/air types)
col 11  Health
col 12  unknown
col 13  unknown (31 for most armed units)
col 21  unknown (correlates with unit class/mobility type)
col 22  unknown (32=mech, 96=cyborg, 216=vehicle, 128=flier)
col 31  signature (unit-type lookup index into unitid.txt)
```

`data/DCOLONY/GAMESTAT/UNITID.TXT` maps `(team, weapon_class, unit_type)`
triples to weapon/armour lookup indices; it is not a unit–FIN-stem mapping.

### Dark Colony Construction / Production UI

The original right sidebar data for human construction is split between
`data/DCOLONY/INTRFACE/MAINE` and `data/DCOLONY/GAMESTAT/DEPEND.TXT`.

`MAINE` defines button placement, icon frame, and hover label/cost. Human
building buttons are the right column of the command panel:

| UI id | Label | Icon frame | Product type |
|-------|-------|------------|--------------|
| 206 | Exo-Ctr 2000 | 129 | building 16 `EXCOPOD` |
| 80 | Barracks 1000 | 20 | building 17 `BRRKPOD` |
| 81 | Sci-Pod 2000 | 21 | building 20 `SCNCPOD` |
| 82 | Robo-Ftr 2000 | 22 | building 18 `ROBOPOD` |
| 83 | Rsch-Bay 3000 | 23 | building 22 `RSCHPOD` |
| 85 | Sci-Pod + 2000 | 26 | building 21 `SCNCPOD2` |
| 86 | Robo-Ftr+ 2000 | 30 | building 19 `ROBOPOD2` |

Human unit production buttons are mostly the left column:

| UI id | Label | Icon frame | Product type |
|-------|-------|------------|--------------|
| 87 | Exploiter 1500 | 8 | unit 6 `EXPL` |
| 89 | Trooper 350 | 6 | unit 0/69-72 `TRSC` |
| 90 | Sentinel 450 | 5 | unit 1 tower builder / mine-deploy path |
| 92 | Osprey IV 600 | 9 | unit 5 `SCGM` |
| 91 | Reaper 600 | 11 | unit 2 `REAP` |
| 88 | Firestorm 900 | 10 | unit 1 tower builder path |
| 93 | Barrager 1000 | 7 | unit 3 `BARR` |
| 94 | S.A.R.G.E 1500 | 12 | unit 4 `SARG` |
| 135 | Medi-craft 900 | 29 | unit 49 `BEON` |

`DEPEND.TXT` links each UI id to cost, product class, product type, faction,
and prerequisite rows. Row examples:

- `0 2000 206 0 0 0 0 -1` = Exo Center build entry.
- `1 1000 80 0 1 0 0 0 -1` = Barracks depends on Exo Center.
- `7 1500 87 1 6 0 -1` = Exploiter unit depends on Exo Center.
- `9 350 89 1 0 1 -1` = Trooper unit depends on Barracks.

Current engine gaps before this can be made interactive:

1. Add Dark Colony building actor types for rows 16-22 and load starting
   buildings from `.SCN` as selectable/renderable non-mobile units.
2. Add a product definition table from `DEPEND.TXT` and `MAINE` button frames.
3. Add right-sidebar modes for normal commands, building products, and unit
   products.
4. Implement click-to-place building ghosts, resource spend/refund, map
   footprint blocking, and completed building insertion.
5. Implement unit production from selected production buildings, including a
   queue timer and spawn rally point.

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
- A fixed mission's `.SCN` is paired with the same-basename `.MAP`; sibling
  `TACTICS.MM` is scenario/tactics data and is not the six-byte terrain grid.
- Dark Reign scenario and map coordinates are top-down. `SetStartLocation`
  values are world pixels (24 pixels per map cell), and imported SPR rotation
  zero points north after the file loader's quarter-turn normalization.
