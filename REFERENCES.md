# Reverse Engineering References

Keep these links handy when touching loaders, tile animation, map objects, or
plugin-specific behavior.

## Doom

- id Software: https://github.com/id-Software/DOOM/tree/master/linuxdoom-1.10
  - Local source: `reference/DOOM/`.
  - `r_defs.h`: `spriteframe_t` owns `rotate`, `lump[8]`, and `flip[8]`;
    `spritedef_t` owns only frame count and frame pointer.
  - `info.c`: states refer to a numeric sprite ID and frame; `sprnames[]`
    supplies the names used during initialization.
  - `r_things.c`: `R_InitSpriteDefs` scans matching lumps and derives frame and
    rotation from their suffixes, without classifying walk/fire/death actions.
    `R_InstallSpriteLump` builds the runtime definitions; `R_ProjectSprite`
    indexes `sprites[thing->sprite].spriteframes[frame]` directly.
  - SHA-256 of inspected local `r_defs.h`:
    `d8856503bea02282f5f338f3533885e87c6c430ce4f11511d6a04b5fc040db31`.
  - SHA-256 of inspected local `r_things.c`:
    `b3ff03ba213782ed6488a9e0fb0afa0a0ee1f5cfbff8ce498c7133e93e01e0e3`.

## GZDoom

- ZDoom/GZDoom:
  https://github.com/ZDoom/gzdoom
  - Local checkout: `reference/GZDoom/` at commit
    `c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7`.
  - `src/r_data/sprites.h` stores a texture ID for each sprite frame and view
    rotation. Sprite definitions choose images; they do not own a fixed atlas.
  - `FGameTexture` keeps the source image, dimensions, offsets, and optional
    material layers separate from renderer resources. Its sprite positioning
    data supplies the equivalent of our sprite-cell bounds and ground point, separate from lumps.
  - `FHardwareTextureContainer` lazily owns the default GPU texture and cached
    translation-specific GPU textures for one source image. The translated
    cache is keyed by translation rather than represented as fixed slots on
    every sprite.
    Source: [hw_texcontainer.h](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/textures/hw_texcontainer.h),
    `GetTexID`, `GetHardwareTexture`, `AddHardwareTexture`, and `Clean`.
    Dark Colony's indexed images now use the same demand-driven texture and
    translation ownership through `R_GetSpriteTexture`; loading a cell does
    not allocate a hardware texture.
  - This supports converting each game format into engine-owned indexed sprite
    images at load time, then creating renderer textures from those images. It
    does not support keeping game-loader state or format callbacks on the
    generic sprite type.

## Doom95

- 7dog123 / Win95Doom-recreation:
  https://github.com/7dog123/Win95Doom-recreation
  - Local checkout: `reference/DOOM95/source/` at commit
    `b4b190a6797afc03483dbe38a05e8f30d940fe8c`.
  - A reconstruction of the official Windows 95 port built from the released
    Linux Doom source and archaeology of the shipping `DOOM95.EXE`, not an
    original Microsoft/id Software source release.
  - Contains the Doom engine plus reconstructed Windows-specific files such as
    `doom95.cpp`, `i_w95gdk.cpp`, `i_win32.cpp`, DirectDraw, DirectInput,
    DirectSound, MIDI, launcher, registry configuration, and XBAND code.
  - The repository has no declared license. Treat it as a behavioral and
    reverse-engineering reference only; do not copy code from it into open-rts.

- 7dog123 / doom95-dump:
  https://github.com/7dog123/doom95-dump
  - Local checkout: `reference/DOOM95/dump/` at commit
    `e4b268b42adf0161e30b319a83810b66bbda3896`.
  - Companion evidence containing the `DOOM95.EXE` CodeView/debug-information
    dump, Snowman decompilations, DLL material, and Resource Hacker output used
    by the reconstruction.
  - This repository also has no declared license. Use it only for behavioral
    comparison and independently document any conclusions derived from it.

## Dark Colony

- MobyGames Dark Colony screenshots:
  https://www.mobygames.com/game/2737/dark-colony/screenshots/
  - Native 640x480 gameplay captures used to compare unit, building, terrain,
    and HUD scale. Screenshot `395046` includes a mobile Exploiter near human
    structures and a beacon.

- My Abandonware Dark Colony gallery:
  https://www.myabandonware.com/game/dark-colony-49w
  - Additional native 640x480 captures used to compare the human landing
    structure and its narrow tower/pod column.

- Dark Colony Wiki, Exploiter:
  https://darkcolony.fandom.com/wiki/Exploiter
  - The "Active Exploiter" gameplay crop was used to compare the deployed
    body, mast, and vent relationship. Treat the image as visual corroboration,
    not a substitute for executable coordinate evidence.

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
    This confirms viewer behavior, not yet the retail executable's conversion.
    Direct source: https://github.com/smdimos/jxspr/blob/master/SPR.java
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
- Vent and beacon presentation now uses ordinary mobjs with complete FIN
  states; see [the vent/beacon findings](docs/DC_EXE_FINDINGS.md#vent-and-beacon-mobj-states-and-vent-origin-2026-09-09).
  `VENTSTAND0` carries PUFF, VENT2, GLIT and SMSP together. The static crater
  belongs to the map. The existing resource `attachment` at `(x+0.5,y-0.5)`
  supplies the visual/harvesting origin; raw SCN `cell` remains the script key.
  Using its cell center instead places the animation one row above the crater.
- Superseded vent interpretation: the former `VENT2` overlay calculation used
  SPR disY and negated FIN Y to obtain `(-9,25)`. That is not the current shared
  FIN rendering contract documented below; do not restore it as a plume fix.
  `VENT.SPR` has four raw crater cells, but these are not the VENTSTAND0 frames.
- `BEACSTAND2` alternates the base FIN frame with a complete base-plus-light
  frame. Its original palette, offsets and layer-5 light remain asset-owned;
  there is no independent blink flag or glow effect.
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
- `S_EXPL_STND` — idle, `tics=-1` (infinite hold), `misc1=1`
- `S_EXPL_RUN1/RUN2` — walking 2-frame loop, `misc1=2`

**Deploy sequence (body frame 14 fixed, overlay extends turret arm):**
- `S_EXPL_DEPLOY1..DEPLOY10` — 10 states, `tics=3` each, `misc1=5`
- Overlay frames progress 15→16→17→…→23 for E/SE/S/SW-facing directions.
  N/NW/W/SW-facing directions use frame 15 throughout the deploy sequence
  (the turret arm is visually in its base/folded position for those angles).
- All deploy states use `misc1=5`, which blocks `update_unit_harvest` from
  re-triggering the deploy while it is already playing.

**Work/harvest loop (body frame 14 fixed, overlay pulses):**
- `S_EXPL_WORK1..WORK15` — 15-state loop, `WORK1 tics=3`, `WORK2-15 tics=5`
- `WORK1` and `WORK15` use overlay frame 24 (turret-top rest position, `Y=-27`);
  `WORK2..WORK14` pulse through frames 25→26→27→28→29→30→32→33→…→25 at
  `Y=-31..-33`. The brief 5-tick dwell at frame 24/`Y=-27` at the end of each
  cycle creates a subtle visual "settle" before the next pulse.
- `WORK15` loops back to `WORK1` (`nextstate = S_EXPL_WORK1`). All work
  states have `misc1=5`.
- The harvest guard in `engine_units.c` triggers `set_unit_state(DEPLOY1)` only
  when `state->misc1 != 5`, so the work loop runs uninterrupted.

**Death sequence:**
- `S_EXPL_DIE1..DIE6, CORPSE` — `misc1=4`, triggers `A_DC_Fall` at DIE1.
- `A_DC_Fall` strips `T_HARVESTER` and `T_MOBILE` from the
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
those indices and selects them from the object's team. The earlier claim that
FIN `remap` selects a palette is disproven by DC.EXE's separate queue fields:
team/lighting at +0x14, FIN drawing mode at +0x15. See the September 9 correction
in `docs/DC_EXE_FINDINGS.md` for instruction addresses and asset examples.

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
              bytes 14..15 remap   native drawing mode; 2 selects alternate clipping
              bytes 16..17 intens  color intensity; 16 is normal
              bytes 18..19 layer   1 for main unit body, 3/5 for effects/overlays
              bytes 20..21 flags   bit 0 is horizontal flip in current samples
```

Important: label `start..end` values are **not raw `.SPR` frame ids**. For
example, `GRAYFIREA0` starts at command index `78`, and command `78` points to
raw `GRAY.SPR` frame `80`. Likewise `GRAYMOVE2 0x47..0x4d` is seven command
records, not raw frames `71..77`; its body commands point at raw frames
`23,31,39,47,55,63,71`.

**Correction (2026-09-09):** the `aux_count` records above are the 164-byte FIN
frames, and named ranges index those frames rather than flattened commands.
Their part counts locate variable-length command groups. The first word `29`
is not a frame count; its precise format/timing role remains unverified. See
`docs/DC_EXE_FINDINGS.md`, “Native record views and sprite geometry,” for native
file fingerprints, span offsets, the corrected loader, and regression results.

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

**Superseded implementation (2026-09-09):** the preceding separate muzzle-state
and additive-yellow description records the former raw-SPR workaround, not
retail evidence. Complete FIN attack frames now own their BLAZ commands.
`S_*_MUZZLE`, `mobjinfo.muzzleflash`, and the artificial additive/yellow flags
have been removed. Selector 3 is retained from FIN without inventing an SDL
blend mode or tint; its exact native blending remains unverified here. See
`docs/DC_EXE_FINDINGS.md`, “FIN layer flags and Doom misc fields”.

Doom's `P_SetPsprite` uses `misc1`/`misc2` for player-weapon screen coordinates:
when `misc1` is nonzero it assigns `sx = misc1 << FRACBITS` and
`sy = misc2 << FRACBITS`. These are not sprite blend flags or world-actor
animation groups. Local source provenance is the Doom checkout listed above;
reproduction is `rg -n 'misc1|misc2' reference/DOOM/{info.h,p_pspr.c}`.
SHA-256: `info.h`
`3e68ee6cc13323e69cbcf208e9a0af7b13867bead49e66d765abefa5d914b0dd`;
`p_pspr.c`
`b47100f9c2c2c4f7913831c170bff55161af2d5d01f5467dbbc6be081c4f1af1`.

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

Barracks production reconstruction also uses local `HUBU.FIN/TRSCBUILD0`,
`TRSC.FIN/TRSCSTAND8`, and the historical work in `e80cf56` / `51219fd`.
See [building mobjs and Barracks production](docs/DC_EXE_FINDINGS.md#register-building-mobjs-and-restore-barracks-production-2026-09-09)
for corrected timing, handoff coordinates, preserved unknowns and the playable-flow test.

City placement was rechecked directly in the local `data/DCOLONY/DC.EXE`
using r2 on 2026-09-09: slot table `0x475b64`, constructor `0x4412d4`, and
render-origin subtraction `0x436662..0x436687`. The earlier findings survive
in commit `db463f4` (2026-08-31). See [city initialization and placement](docs/DC_EXE_FINDINGS.md#restore-city-fin-initialization-and-native-slot-positions-2026-09-09)
for the fingerprint, formulas, asset ranges, corrected hypotheses and tests.

See `docs/DC_EXE_FINDINGS.md` for the consolidated executable fingerprint,
rendering call graph, native data layouts, animation timing, direction lookup,
open-rts consequences, and unresolved questions.

`data/DCOLONY/DC.EXE` (566 KB) and `data/DCOLONY/DC16.EXE` (637 KB) are the
original Dark Colony DOS MZ executables. Neither contains game data beyond the
engine itself; all unit, sprite, and animation data lives in the external files.

#### DirectDraw and software sprite rendering

`DC.EXE` imports only `DirectDrawCreate`; all other DirectDraw operations are
indirect COM vtable calls. The graphics setup rooted at `0x0042bbf0` stores the
`IDirectDraw` pointer at `0x004745ec`. Surface setup at `0x0042bdc4` creates the
primary surface (`0x004745f0`), its attached back buffer (`0x004745f4`), and an
additional surface (`0x004745f8`). It also creates 32 auxiliary surfaces stored
from `0x004c1120` and applies the game palette at `0x004745fc`.

Confirmed DirectDraw v1 method slots used by these routines are:

- `IDirectDraw +0x18`: `CreateSurface`
- `IDirectDraw +0x50`: `SetCooperativeLevel`
- `IDirectDraw +0x54`: `SetDisplayMode(640, 480, 8)`
- `IDirectDrawSurface +0x1c`: `Blt`
- `IDirectDrawSurface +0x2c`: `Flip`
- `IDirectDrawSurface +0x30`: `GetAttachedSurface`
- `IDirectDrawSurface +0x44`: `GetDC`
- `IDirectDrawSurface +0x64`: `Lock`
- `IDirectDrawSurface +0x68`: `ReleaseDC`
- `IDirectDrawSurface +0x74`: `SetColorKey`
- `IDirectDrawSurface +0x7c`: `SetPalette`
- `IDirectDrawSurface +0x80`: `Unlock`

Normal SPR rendering is CPU rasterization into locked surface memory, not one
DirectDraw `Blt` call per sprite. The SPR-cell dispatcher at `0x0044b5e4`
validates the frame index, obtains the 24-byte runtime descriptor, adds both
authored displacement words to the input draw point, clips, then dispatches to
`0x0044b2f0` for raw pixels or `0x0044b45c` for chunked/RLE pixels:

```text
draw_x = input_x + cell.disX
draw_y = input_y + cell.disY
```

The decisive instructions are `mov ax, [ecx+4]; add edx, eax` for `disX` and
`mov ax, [ecx+6]; add ebx, eax` for `disY`. Code using Dark Colony SPR frames
must not discard `disY`; the original dispatcher consumes it before clipping.

The dispatcher is installed as the sprite object's draw callback at object
offset `+0x5c` by the constructor at `0x004297e4`; this is why ordinary static
call xrefs do not expose its callers. Gameplay visual builders at `0x00436290`
and `0x00436a44` enqueue 28-byte render records through `0x00432dec`, up to the
native 800-record limit. Presentation routine `0x0042b54c` blits the additional
surface (`0x004745f8`) to the back buffer (`0x004745f4`), optionally composites
one of the 32 auxiliary surfaces, then flips the primary surface
(`0x004745f0`).

#### Native SPR and FIN runtime data

The SPR loader at `0x0044b048` reads the native header as flags, cell count,
payload size, 256 RGB triplets, then one 8-byte descriptor per cell:

```text
u16 width, u16 height, u16 disX, u16 disY
```

Each descriptor becomes a 24-byte runtime cell containing those four words,
decoded/device data pointers, a used flag, and decoded or chunk byte count.
SPR flags `& 0x180` select the chunked/RLE path.

The FIN loader at `0x004230ac` resolves sprite dependencies from `SPRITES/`,
loads labels and timeline metadata, and expands each 22-byte draw command. A
native command contains an 8-byte dependency name followed by seven signed
16-bit values: SPR cell, x, y, remap, intensity, layer, and flags. During load,
the original converts command coordinates to its fixed render units:

```text
runtime_x = FIN.x * 8
runtime_y = FIN.y * -8
```

The render queue converts these fixed values back while preserving the authored
integer FIN offsets. Exact names for FIN layer values `0/1/3/5` and flag value
`1` remain unresolved; preserve them as native fields until their consumers are
identified.

#### Exploiter and Reaper animation ranges

**Correction (2026-09-09):** DC.EXE's STAND setup at `0x0043895f` calls
`0x00423a50` with the literal action name. Missing directions fall back within
that action; it does not merge SHUF into STAND. The full native stationary/turning selection
path is still unknown. The earlier open-rts presentation rule merged STAND/SHUF
and filtered singleton MOVE directions. It is superseded by generic FIN loading:
keep actions separate and use all authored directions, including sixteen when
present. Animation selection belongs in the state/action code.
See `docs/DC_EXE_FINDINGS.md`, "Resolve FIN actions by their own names," for
instruction addresses, file fingerprints, fallback-table evidence, and the
remaining angle-quantizer limitations. The executable/asset provenance is the
local retail data already listed above; no new external source was used.

Direction and cycle length are data-driven by FIN label ranges rather than
hardcoded per unit in the renderer. `EXPL.FIN` contains directional poses for
all 16 direction codes: even `EXPLSTAND*` labels and odd `EXPLSHUF*` labels.
Its principal moving cycles are eight two-frame ranges (`EXPLMOVE0`, `14`,
`12`, `10`, `8`, `6`, `4`, and `2`); odd move labels elsewhere in the file are
single-frame intermediate poses. The unresolved gameplay-side question is when
DC.EXE selects an odd shuffle pose versus quantizing movement to an even range.

`REAP.FIN` contains eight principal movement ranges of eight timeline frames
each: `REAPMOVE0` is `16..23`, then directions `14`, `12`, `10`, `8`, `2`, `4`,
and `6` occupy `24..79`. Fire ranges are five frames each. Reaper death ranges
are not uniform: observed inclusive ranges contain 10, 11, 14, 15, 26, or 27
timeline frames. Any six-frame Reaper playback in open-rts is therefore a state
generation/playback limitation, not a limit in `REAP.SPR`, `REAP.FIN`, or the
original generic FIN renderer.

**Timing correction (2026-09-09):** the 19-multiplier interpretation in this
historical section is disproven. Rechecking `0x42356b..0x42358f` gives
`((raw_ticks + 3) * 15) / 100`, with zero first replaced by 15. Retail default
world ticks are 66 ms (`0x41a728`, consumed at `0x41cb5a..0x41cbc5`). See
`docs/DC_EXE_FINDINGS.md`, “Native damage channel implementation”, for the
register-by-register evidence, startup advance, implementation and remaining
non-damage timing audit. The explicitly required Reaper movement cadence is
preserved as an authored engine behavior, not claimed as this formula's output.

FIN frame word `+2` is a raw delay. The loader at `0x00423544` replaces zero
with `15`, then `0x00423563..0x0042358f` converts it to runtime ticks with:

```text
runtime_ticks = ((raw_ticks + 3) * 19) / 100
```

`REAPMOVE0` stores `20,13,13,20,6,13,13,6`, producing runtime delays
`4,3,3,4,1,3,3,1`. The old generator assigned all eight states three tics,
which erased this cadence. `EXPLMOVE*` stores zero and therefore normalizes to
three runtime tics. `tools/dc_info_gen.c` now preserves the native converted
timing for the Reaper movement sequence.

**Source files embedded as assert strings** (incomplete list — useful for
orienting reverse-engineering efforts):
`animate.c`, `juicel.c`, `mobiles.c`, `objects.c`, `vobj.c`, `engmain.c`,
`sprite.c`, `sprites.c`, `collide.c`, `path.c`, `ai.c`, `trigger.c`.

**FIN files are called "juice files" internally.** The engine loads them from
`animate/%s` (i.e. `animate/reap.fin`) and refers to animation label ranges as
"angles". Assert strings: `"Whoa, One of my animation angles is NULL."`,
`"Whoa Batman, I don't have any record of juice file %s"`.

**Animation name construction.** Function `0x00423a50` first concatenates the
unit stem and state keyword with `%s%s` at `0x0046fccc`, then probes 16 labels
with `%s%d` at `0x0046fcd4`. Its suffix expression is `(12 - index) & 15`. It
then expands those 16 animation pointers into a 32-entry heading table using
the fallback-order table at `0x00474500`, choosing a nearby available angle
when a label is absent. This confirms that numbered odd-angle labels are
first-class native inputs rather than unused names.

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
and `0x00423a50` confirm the engine queries these labels by angle and fills
missing angle slots by nearest fallback.

Open-rts currently generates Exploiter stand/run/deploy states with only the
eight even direction codes. Its 16-direction turn logic can stop on odd codes,
but `state_facing_slot()` then nearest-matches those codes to even art. Thus the
native `EXPLSHUF1/3/.../15` and later `EXPLMOVE1/3/.../15` poses are not emitted
or displayed. Which odd set DC.EXE selects while turning versus moving still
requires tracing the callers of `0x00423a50`; the renderer itself supports the
angles once the selected animation table contains them.

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

Configuration rule: raw Dark Colony text files are extraction inputs only. The
runtime must never open `GAMESTAT/*.TXT`, `INTRFACE/MAINE`, or another original
configuration file. `tools/dc_gamestat_gen` exports those values into the
checked-in C arrays in `games/dark-colony/gamestat.h`, while gameplay-facing
tables remain explicit C data, in the same spirit as Doom's `mobjinfo[]`.

`python3` is available on the development host, but no Python dependency is
needed for this export: the deterministic C generator is built by
`make dark-colony-gamestat`.

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

- `data/REIGN/dkreign.exe` for executable AI loader and parameter strings.
- `data/REIGN/dark/aip/*.AIP`, `*.FSM`, and `aip/AIPDEF.H` for the shipped
  strategy profiles, construction-account modes, force matching, and
  conditional AI switching.
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
- OpenDR `DrSprLoader.cs` exposes Dark Reign RSPR frames with a zero offset and
  the full `Szx`/`Szy` canvas as both frame size and sprite size. The RSPR
  canvas is therefore centered on the actor origin; its opaque-pixel bounds
  must not be interpreted as a bottom-center ground point. The loader also
  parses per-frame hotspot records, but those are attachment metadata rather
  than the body or selection origin.
- Dark Reign buildings are layered entities rather than single sprites.
  OpenDR `sequences/structures.yaml` composes the completed building from
  frame 1 of a tileset-archive terrain underlay, frame 1 of the same basename
  in the shared/base archive, frame 1 of the second `SetBuildingImages` sprite,
  and a separate shadow SPR. Archive identity and palette are therefore part
  of a sprite reference; a cache keyed only by basename will load the underlay
  as the body and omit most of the building.
- `AddBuildingAt` coordinates attach to the top-left of the full authored RSPR
  canvas, not the top-left or center of a separately inferred collision
  footprint.  Decoration rendering therefore uses an explicit plugin-authored
  sprite pivot of `(0, 0)` for Dark Reign buildings.  Deriving the draw origin
  from the guessed footprint shifted bridges and structures north-west (for
  example, 24 pixels on both axes for the 144x120 civilian bridge).
- The apparent faint terrain/"sand" in incomplete buildings was not a reason
  to threshold low alpha. OpenDR's TIL masking retains the authored gradient
  as `min(255, mask * 4)`; the visible error came from rendering the
  terrain-palette underlay as the whole entity instead of compositing its base
  and top layers.
- A fixed mission's `.SCN` is paired with the same-basename `.MAP`; sibling
  `TACTICS.MM` is scenario/tactics data and is not the six-byte terrain grid.
- Dark Reign scenario and map coordinates are top-down. `SetStartLocation`
  values are world pixels (24 pixels per map cell), and imported SPR rotation
  zero points north after the file loader's quarter-turn normalization.

## KKnD

- OpenKrush (the successor to the archived OpenRA KKnD mod):
  https://github.com/IceReaper/OpenKrush
  - `Assets/FileFormats/Lvl.cs` documents the `DATA` container's typed file
    lists and archive-global asset offsets.
  - `Assets/FileFormats/Mapd.cs` documents embedded palettes, `SCRL` layers,
    32×32 tile pointer grids, and the transparent upper layer.
  - `Assets/FileFormats/Mobd*.cs` documents animation tables, frame anchors,
    `SPRT` render records, and the Gen1 per-scanline sprite decompressor.
- Archived OpenRA KKnD mod:
  https://github.com/Dzierzan/KKnD
  - Provides the original named index for `SPRITES.LVL` members. In particular,
    `34.mobd` is `Infantry.mobd`.
- OpenRA multi-layer map discussion:
  https://github.com/OpenRA/OpenRA/issues/13364
  - Confirms that KKnD terrain uses a second map layer for art that actors can
    travel behind or underneath.

Local format findings from `data/KKND`:

- `LEVELS/640/SURV_01.LVL` is the first Survivor mission. Its first `MAPD`
  member has two 50×40 `SCRL` layers, using 32×32 pixels per cell and a
  256-color embedded palette.
- The second `MAPD` member is a 16×1 graphic and is not the world map.
- `LEVELS/640/SPRITES.LVL` has 86 MOBD slots, 81 populated. Infantry expands
  to 176 referenced frames: 16 single-frame standing directions, 16×4 attack
  frames, and 16×6 walking frames. Repeated frame pointers are intentional and
  preserve the original direction table.
- `CPLC` (mission objects/scripts) and `BOXD` (collision/passability) are still
  unimplemented. The current vertical slice renders the original two-layer
  mission art and original MOBD infantry, then uses the engine's fallback units
  to make sprite loading and movement visible.

## Hexen and Strife actor lifecycle audit (2026-09-07)

- Local Hexen source: `reference/Hexen Source/` (actual directory name here).
  `P_MOBJ.C` SHA-256:
  `57e7658304368c17e4b93fff7d28501f645b4894b0bc25d719638784aa81f342`.
  `H2DEF.H` SHA-256:
  `ee2ff69dc1e8b581eb9a99f078927f9a706e689e9bd3128a5e6e947ef32c50da`.
  `P_SetMobjState`, `P_MobjThinker`, and `P_SpawnMobj` establish terminal
  removal, action entry, thinker-side zero-tic chaining, and no-action spawn.
  `P_SPEC.H` embeds `thinker_t` first in specialized thinker structures.
- User-supplied Strife: Veteran Edition source:
  `reference/strife-ve-master/strife-ve-src/src/strife/p_mobj.c` (game logic is
  in the `strife/` subdirectory). SHA-256:
  `58803893a19581099556dd3765e6ea33196a74baae1993b074a1e94de44be7eb`.
  This is a modern source port, not a retail executable disassembly. Its
  `P_SetMobjState` is annotated "[STRIFE] Verified unmodified" and chains
  zero-tic states in the setter. `P_SpawnMobj` initializes without actions.
  No checkout commit or download URL was established for these local trees;
  use the file hashes to reproduce this comparison.
- Detailed contracts, corrections to the supplied review, implementation
  consequences, and remaining differences: `docs/STATE_ARCHITECTURE_AUDIT.md`.

## Doom rendering globals

- id Software Linux Doom `r_main.c`:
  https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/r_main.c
  (consulted 2026-09-09). File-scope `viewx`, `viewy`, `viewz`, `viewangle`,
  `viewplayer`, and column/span function pointers are rendering globals.
  This supports using one active rendering context in open-rts; the SDL
  `r_renderer` pointer is our implementation choice, not an original Doom type.
  Video initialization registers it and video shutdown clears it.

## Quake II temporary formatted strings

- id Software Quake II `game/q_shared.c`, `va`:
  https://github.com/id-Software/Quake-2/blob/master/game/q_shared.c
  (consulted 2026-09-09). The original formats into a static buffer and returns
  its address for immediate use. `M_va` adopts that call-site convenience with
  bounded `vsnprintf`, eight rotating buffers per thread, and NULL on overflow.
  Its documented lifetime permits seven subsequent calls; retained strings
  must be copied before reuse. This is a utility design reference, not evidence
  about Dark Colony file formats or retail rendering behavior.

### FIN exporter text format reference (2026-09-09)

Reference inspected: local `reference/doom-utilities-master/`
`multigen.txt` and `multigen.c` (John Carmack's DOOM STATESCR, version 1.0).
This checkout has no Git metadata; upstream URL/revision is unverified.
The text uses `state sprite frame tics action nextstate` rows and semicolon
comments. Dark Colony's FIN exporter follows that column structure with numeric
native frame indices and raw durations; it is not directly compatible with the
original parser, which interprets frame letters. Details and asset exceptions
are recorded in `docs/DC_EXE_FINDINGS.md`, “FIN-only multigen-style exporter”.

### Per-game object schemas and designated fields (2026-09-09)

Confirmed in local `reference/doom-utilities-master/multigen.c`:

- Lines 213–236 read `$ DEFAULT` into `baseinfo`, recording field names and values.
- Lines 310–319 generate `mobjinfo_t` and its array declaration in `info.h`.
  Fields beginning with `str_` become `char *`; the others become `int`.
- Lines 374–395 emit each object's values, substituting defaults for missing
  fields and printing the field name as a comment.
- `reference/DOOM/info.h:1304–1333` contains Doom's object schema and array
  declaration. Its header identifies the tables as multigen output.

This explains the type's placement: the schema and object table are outputs of
the same game data description. open-rts now keeps `mobjinfo_t` in each game's
`info.h`, selected at build time; shared actor declarations borrow the type by
forward declaration. The existing schemas retain their field layouts.
Dark Colony uses one `.field = value,` per line, omits zero fields (including
S_NULL, whose enum value is zero), and keeps MT names as object comments.
C's implicit zero initialization replaces explicit zero values; it does not
apply multigen's potentially nonzero `$ DEFAULT` values. The all-zero MT_NULL
entry retains `{0}` semantics for C11. An independent comparison checked all
384 fields across 16 entries; all four game builds, the DC layout test, state
and thinker-action tests, and the headless DC smoke check pass.

### Loader storage cleanup (2026-09-09)

- Doom's `P_LoadVertexes` and `P_LoadThings` in the local
  `reference/DOOM/p_setup.c` were read for final level allocation and direct
  spawning from native records. Upstream provenance:
  https://github.com/id-Software/DOOM/blob/master/linuxdoom-1.10/p_setup.c
- No new external format reader or executable decompilation was used. Native
  asset fingerprints, unchanged unknowns, comparison counts, and reproduction
  commands are recorded in `docs/LOADER_REFACTOR_VERIFICATION.md` and the four
  game-specific `*_EXE_FINDINGS.md` documents. The GZDoom source/ownership
  provenance above is unchanged; its documented local checkout was absent.


### Dark Colony native sprite shadows (September 9, 2026)

- Primary evidence: local retail `data/DCOLONY/DC.EXE`, SHA-256
  `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
  FIN dispatch `0x44f95c`; normal/mirrored shadow setup `0x45c7b0` /
  `0x45cc04`; span projection `0x45ba5a` / `0x463bc0`; terrain RMP load
  `0x44a7b0`; destination mapping at `0x45681a` / `0x460e72`.
- The shipped terrain `.RMP` row at `0x4800` supplies shadow colors. The source
  silhouette uses 128/256 horizontal shear and 40/256 row-repetition carry,
  with FIN layer 1 selecting shadow+body and layer 2 shadow only.
- See `docs/DC_EXE_FINDINGS.md`, “September 9: projected sprite shadows”, for
  formulas, asset hashes, exact instructions, tests and unresolved native
  clipping/framebuffer differences. Local decompiler/disassembly material
  stays under ignored `reverse/dc-exe-r2ghidra/`.
- User-provided visual/context link:
  https://www.gog.com/dreamlist/game/dark-colony (accessed September 9, 2026).
  The fetched page is a Dreamlist shell, not executable or rendering evidence;
  no projection or palette constants were inferred from it.


### Dark Colony city eligibility (September 9, 2026)

- Primary evidence: local retail `data/DCOLONY/DC.EXE`, SHA-256
  `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
  and shipped `SCENARIO/HUMAN/HUMAN02.SCN` / `HUMAN03.SCN`.
- Scenario loader `0x41a61c`, especially `0x41abb7..0x41ad52`, distinguishes
  the AI pair from the city pair and disables city slots for zero city X.
  Constructor `0x4412d4` rejects disabled slots. Fresh radare2 disassembly
  verifies the cached local r2ghidra decompilation; no external source was used.
- See `docs/DC_EXE_FINDINGS.md`, “September 9: phantom cities at AI locations”,
  for exact fields, branch addresses, disproven fallback, native tower gate,
  scenario records and regression commands.


### Human01 startup test correction (September 9, 2026)

- Primary data: shipped `data/DCOLONY/SCENARIO/HUMAN/HUMAN01.SCN`,
  `HUMAN01.TRO` (opening reinforcement at line 49), and `HUMAN02.SCN`.
  Exact hashes, object counts and the misleading test history are preserved
  in `docs/DC_EXE_FINDINGS.md`, “September 9: resolve the stale Human01
  headless assertions”. No new external source or executable analysis was used.
- Human02 visibility expectations use the already verified retail spawn
  routine `0x419d44`: negative health selects defaults, not hidden status.

### Human city damage animation sources (2026-09-09)

Retail `data/DCOLONY/ANIM.DAT` lines 8–9 load BURN.FIN/BURN2.FIN, providing
human city SCRCH/BURN/DIE sequences alongside HUBU.FIN. BURN3.FIN is not in
the native load index and its duplicate one-frame BRRKPODDIE0 must not override
BURN2's 35-frame sequence. DC.EXE `0x413620` chooses HP-dependent looping
animations; `0x4385f8` binds the label categories; `0x415618` starts death
playback. Exact thresholds, branch directions, asset hashes, ranges, timing
and reproducer commands are preserved in
[Human city damage, fire and explosions](docs/DC_EXE_FINDINGS.md#human-city-damage-fire-and-explosions-2026-09-09).
These are local retail asset/disassembly findings, with no external screenshot
or secondary-source inference. `tools/dc_building_states.py` exports only state
references from this indexed data; FIN continues to own all drawing commands.

Human02 petrovent guard audit (2026-09-09): local retail HUMAN02.SCN/TRO
place ten Greys around (53,27) and assign two-point waypoint orders.
DC.EXE parser `0x43ae2c`, dispatcher `0x43a144`, waypoint assignment
`0x43a094`, command table `0x4742ac` and action initializer `0x415260`
confirm native per-object waypoint storage and dispatch. Fingerprints,
offsets, runtime diagnostics, current AI/script defects and remaining
engagement-rule unknowns are recorded in
[the Human02 guard audit](docs/DC_EXE_FINDINGS.md#human02-petrovent-guards-and-premature-attack-audit-2026-09-09).
No external sources were used; the retail encounter behavior was reported
by the user and distinguished from the disassembly evidence.

The follow-up implementation traces action table `0x474304` entry 9 to
`0x415364`, confirming repeating waypoint routes. Local weapon-range
acquisition is traced through `0x432a30` / `0x4323bc`; the separate weighted
sight calculation is at `0x446240..0x44625e`. See the same Human02 audit
for exact instructions, the removed unconditional-pursuit policy, regression
coverage, and remaining native AI/visibility limitations.

### Dark Colony global build-menu configuration

Retail `data/DCOLONY/GAMESTAT/DEPEND.TXT` provides prerequisite row IDs and
costs; `data/DCOLONY/INTRFACE/MAINE` provides fixed control positions and icon
frames. The user supplied the initial-menu screenshot in the 2026-09-09 request
(no external URL). File hashes, the full human dependency graph, screenshot
identification and implementation limits are recorded in
[the global build-menu audit](docs/DC_EXE_FINDINGS.md#global-build-menu-and-native-dependency-configuration-2026-09-09).

### Dark Colony production and construction channels

Retail `data/DCOLONY/DC.EXE`, SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
checked against `GAMESTAT/GAMESTAT.TXT` and every FIN in `ANIM.DAT`.
The [all-building production audit](docs/DC_EXE_FINDINGS.md#production-channel-audit-across-all-city-buildings-2026-09-09)
records type +0x98 BUILDSTAND/BUILD lookup (`0x438c95`), independent object
channels +0x14/+0x1c/+0x24 (`0x418567`, `0x43645b`), production startup
(`0x4139ef`) versus construction (`0x44144b`), queue/exit tables
(`0x419c60`, `0x419c00`), and remaining implementation gaps.
`python3 tools/dc_production_audit.py` reproduces all 106 type lookups,
selected native ranges and first/last commands without changing gameplay data.

### Multigen action assignment and per-FIN includes (2026-09-10)

The actual input is present locally at
`reference/doom-utilities-master/multigen.txt`; `multigen.c:194–195` opens that
literal filename. Input lines 405–415 assign `A_Look` and `A_Chase` explicitly.
`ParseState` interns the action token (`multigen.c:110–120`); output code
at lines 349–365 emits declarations and the function name in each raw state row.
The tool does not generate action bodies or infer behavior from sprite names.
Doom implements these actions in `reference/DOOM/p_enemy.c` (`A_Look:604`,
`A_Chase:672`, `A_PosAttack:802`); weapon actions live in `p_pspr.c`.
The utilities checkout has no verified upstream revision or URL, as recorded
above. This is a local source comparison, not new retail DC executable evidence.

Dark Colony now uses `tools/dc_states.txt` for authored sequence/action policy
and `tools/dc_states.py` for native family policy and raw `animate/*.inc`
output. See `docs/DC_INFO_CONV.md` for syntax and regeneration. Existing numeric
state IDs and all row values are preserved; the C preprocessor no longer
expands blood/building state or label macros.
