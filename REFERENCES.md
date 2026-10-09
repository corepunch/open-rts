# Reverse Engineering References

### Warcraft II single-player campaign entry (2026-10-07)

Blizzard's [Battle.net Edition manual, printed page 6](https://downloads.war2.ru/war2/Info%20%26%20Media%20content/Documents/War2BNE_Manual_EN.pdf)
was read for the Single Player → New Campaign → race selection → briefing
→ first mission sequence. This is a Blizzard-authored document hosted by a
mirror, not a verified pixel/layout reference. The similarly titled
[manual transcript](https://oldgamesdownload.com/manual/warcraft-ii-tides-of-darkness-dos-mac-windows-manual-english/)
contains Battle.net Edition material; its title alone does not prove DOS
edition behavior. Wargus `scripts/menus/campaign.lua::RunCampaignSubmenu`
is a reimplementation's mission selector, not evidence that retail exposes
all campaign missions on entry. The new user screenshot and native findings
are recorded in [WAR2_EXE_FINDINGS.md](docs/WAR2_EXE_FINDINGS.md#single-player-campaign-entry-correction-2026-10-07).

### Warcraft II scenario picker reference (2026-10-07)

User-provided `/Users/igor/Desktop/Screenshot 2026-10-07 at 12.00.30.jpg`
shows the English Battle.net Edition scenario modal over setup. Its
fingerprint, measured layout, DOS/Battle.net differences and outstanding
native scene-import requirement are recorded in
[WAR2_EXE_FINDINGS.md](docs/WAR2_EXE_FINDINGS.md#scenario-picker-controls-and-screenshot-comparison-2026-10-07).
The existing pinned [Wargus wartool catalog](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/wartool.h)
supplies GFU widget indices, menu FONT names, and skirmish PUD indices
220–247, cross-checked against local retail records. Its source was not
copied. Local Doom `m_menu.c` supplies the menu input/lifecycle comparison.
No new web source or generated UI artwork was used.

Keep these links handy when touching loaders, tile animation, map objects, or
plugin-specific behavior.

## Doom

- id Software: https://github.com/id-Software/DOOM/tree/master/linuxdoom-1.10
  - Local source: `reference/DOOM/`.
  - Menu/startup: `d_main.c::D_ProcessEvents` gives `M_Responder` first refusal;
    `D_Display` draws `M_Drawer` last. `D_DoomMain` chooses `G_InitNew` for
    `autostart || netgame`, otherwise `D_StartTitle`. `m_menu.c` owns its own
    lifecycle and `menuactive`; `p_tick.c::P_Ticker` pauses the single-player
    world while a menu is active. DC uses that lifecycle separation with its
    own native screen scripts and assets, not Doom's menu graphics. See
    [the DC menu findings](docs/DC_EXE_FINDINGS.md#main-menu-screens-and-campaign-start-2026-09-14).
  - Networking: `d_net.c` (`NetUpdate`, `GetPackets`, `ExpandTics`,
    `TryRunTics`, `D_ArbitrateNetStart`, `D_QuitNetGame`), `d_net.h`
    (`doomcom_t`, `doomdata_t`, `BACKUPTICS`), `i_net.c` (UDP transport),
    `d_ticcmd.h` and `g_game.c` (`G_BuildTiccmd` and delayed consistency).
    The verified source behavior and necessary RTS adaptations are documented
    in [docs/NETWORK.md](docs/NETWORK.md). No original-game executable or
    native multiplayer protocol was reverse-engineered for this implementation.
  - SHA-256 of inspected networking sources:
    - `d_net.c`: `8f28965cb410bc918c7741c4e93e683292e8c9e18cbcc87e242f564863bb5f87`
    - `d_net.h`: `54d77e10fe98e7ad640c711e7146f1440e4bd3f73e46d27905f9c19b8117ac3d`
    - `d_ticcmd.h`: `2f70caa7c187365850e467254d8b0a31c07ea304d59339c68fde63d05274d146`
    - `i_net.c`: `bc7637299d67665e8294e86b7089df57ccffcf067f6b0017eaab5b78d6a52fca`
    - `g_game.c`: `bc8e2e0d76a70f8120174946641a82c46f0116a624f51c011b292e2ffcb78a29`
    - `i_system.c`: `19bd049d4c421a8893f6b262569400c2b068a7cee03f18dbd78999ee27a8b3a0`
    - `m_menu.c`: `62d58889f7dcbdef4a52efd081beb33fed85d7ed46c35e6d7d634652d0ef3e50`
  - Network error recovery (2026-10-01): the same local Doom sources confirm
    that version mismatch, synchronization abort, kill packets and consistency
    failure are fatal through `I_Error`; `i_system.c` prints, shuts down and
    exits. Its `m_menu.c::M_StartMessage` supplies the menu-message pattern.
    Returning to an offline main menu is the user's requested open-rts behavior,
    not a claim of Doom or native RTS fidelity. See [docs/NETWORK.md](docs/NETWORK.md).
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
    The former `R_GetSpriteTexture` cache followed this approach. The user's
    indexed-sprite storage requirement supersedes translation-specific GPU
    caching: `R_DrawSprite` now expands into one reusable renderer upload
    surface, retaining no Dark Colony cell/team textures. Source images and
    frame definitions remain independent of renderer resources.
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

- Retail fog and environment clock: local `data/DCOLONY/DC.EXE`, SHA-256
  `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`.
  Fresh radare2 disassembly of `0x44e720` (map planes), `0x446158` and
  `0x4458d0` (sight), `0x44ecd0` / `0x44ee68` (quantized light field),
  `0x418818` (day/night ticker), and `0x437630` / `0x4376a8` (CLOC HUD).
  The screenshot references supplied by the user on 2026-09-10 show soft,
  square brightness patches; constants and interpolation are established
  independently from executable instructions. See
  [the verified findings](docs/DC_EXE_FINDINGS.md#confirmed-fog-map-flags-and-daynight-clock-2026-09-10),
  including the C extractor, asset hashes, corrections and reproduction commands.

- DirectDraw API definitions used to identify the retail COM calls:
  - [Microsoft SDK ddraw.h](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/ddraw.h):
    original interface method order and numeric surface/palette flags.
    In particular 0x4000 is VIDEOMEMORY, 0x800 SYSTEMMEMORY, and 0x40
    OFFSCREENPLAIN; do not confuse 0x4000 with 3DDEVICE (0x2000).
  - [IDirectDraw::SetDisplayMode](https://learn.microsoft.com/en-us/previous-versions/ms785064(v=vs.85)):
    width, height and bits-per-pixel arguments. DC.EXE `0x42bce5` supplies
    640, 480 and 8; surrounding assertions name `ddex4.c`.
  - [DirectDraw interfaces](https://learn.microsoft.com/en-us/windows/win32/api/_directdraw/)
    and [surface palettes](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-setpalette).
    The retail binary uses the original interfaces; later interface docs
    explain operations, not the retail vtable offsets. See the instruction
    evidence in `docs/DC_EXE_FINDINGS.md`, “Indexed sprites and DirectDraw”.
  - [SDL2 streaming texture locking](https://wiki.libsdl.org/SDL2/SDL_LockTexture):
    the portable upload buffer uses write-only locks, respecting returned
    pitch and writing every pixel of the locked rectangle. It is not a
    DirectDraw implementation or an indexed destination framebuffer.

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
- `GAMESTAT.TXT` speed is stored raw at type `+0x0c` and is 8.8 map units per
  66 ms world tick (one step per tick, `0x4117fc`/`0x411a30`): cells/sec =
  `speed * (1000/66) / 256`. Trooper 25 is ~1.48 cells/sec. The earlier
  `speed / 32` conversion was disproven (it ran units ~1.9x too slow).
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

Barrager's standing definition uses all sixteen directions. For a missing
STAND direction completed from SHUF, a matching singleton MOVE takes precedence:
the same body can have different native placement in those two labels. BARR's
SHUF poses differ by 10–11 vertical pixels, and several mirrored BARR, REAP and
ATRIL poses differ horizontally. This is an engine turning presentation rule,
not a claim about retail SHUF dispatch. See “Barrager stationary pose placement
(2026-09-12)” in `docs/DC_EXE_FINDINGS.md` for fingerprints, commands and the
reconfirmed loader/queue/blitter addresses. Raw FIN placement remains unchanged.

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
  - Info-table audit pinned revision: `98079a904746440433795fe7f21c4b35eb6b3959`.
  - Primary Dark Reign reference for map import, `.TIL` frame layout, generated
    transition masks, resource handling, and OpenRA-style plugin structure.

- drExplorer:
  https://github.com/btigi/drExplorer
  - Useful reference for Dark Reign FTG archives.

Local game-data files that have already been useful:

- `data/REIGN/dkreign.exe` for executable AI loader and parameter strings.
  - Runtime unit capability audit (2026-09-30): SHA-256
    `3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`.
    Unit parser `0x00445c90` owns movement `+0xf0` and capability bits
    `+0x53c`; weapon parser `0x00483e70` owns human/nonhuman target classifiers
    `+0xa1/+0xa2`. Detailed evidence, corrections to catalog coverage, tests
    and unported abilities are preserved in `docs/DR_EXE_FINDINGS.md`.
    `docs/DR_DISASSEMBLY.md` indexes the matching unit, HUD, transport,
    architecture, generation and development-status Markdown reports.
    Address correction: the human-bit OR is `0x00446935`; `0x00446b3c`
    belongs to CanSpy, with bit `0x10` and arguments at `+0x560/+0x564/+0x568`.
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

September 13 selection-bar search: the native Buttons/Cursors/Gui groups were
inspected, and the procedural OpenKrush implementation is documented in
[selection health-bar findings](docs/KKND_EXE_FINDINGS.md#selection-health-bars-and-native-ui-search-2026-09-13).

- [AdvancedSelectionDecorations.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Mechanics/Ui/Traits/AdvancedSelectionDecorations.cs)
- [StatusBar.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Mechanics/Ui/Graphics/StatusBar.cs)
- [Core category and selection rules](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush/rules/core.yaml)

September 13 anchor verification uses the same pinned OpenKrush revision below.
`MobdFrame` reads the native anchor; `MobdImage` flips pixels independently;
`MobdLoader` converts the anchor to a center-relative drawing offset. See
[sprite anchor evidence](docs/KKND_EXE_FINDINGS.md#sprite-anchors-and-tanker-rotation-2026-09-13).

- [Assets/FileFormats/MobdFrame.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/FileFormats/MobdFrame.cs)
- [Assets/FileFormats/MobdImage.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/FileFormats/MobdImage.cs)
- [Survivor oil tanker sequences](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/vehicles/oiltanker/sequences.yaml)

September 12 sprite-channel verification uses OpenKrush revision
`76c634d05984e48e1e474460c46607aee0bc78a1` and the fingerprinted local SPRITES.LVL.
Detailed offsets, rejected interpretations and limitations are recorded in
[KKnD loader evidence](docs/KKND_EXE_FINDINGS.md#runtime-sprite-catalog-and-native-animation-channels-2026-09-12).
Pinned source paths:

- [Assets/FileFormats/Mobd.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/FileFormats/Mobd.cs)
- [Assets/FileFormats/MobdAnimation.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/FileFormats/MobdAnimation.cs)
- [Assets/SpriteLoaders/MobdLoader.cs](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/SpriteLoaders/MobdLoader.cs)
- [survivors/vehicles/derrick/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/vehicles/derrick/sequences.yaml)
- [evolved/vehicles/direwolf/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/vehicles/direwolf/sequences.yaml)
- [survivors/buildings/drillrig/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/drillrig/sequences.yaml)
- [survivors/buildings/powerstation/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/powerstation/sequences.yaml)
- [survivors/buildings/outpost/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/outpost/sequences.yaml)
- [survivors/buildings/machineshop/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/machineshop/sequences.yaml)
- [survivors/buildings/repairbay/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/repairbay/sequences.yaml)
- [survivors/buildings/researchlab/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/buildings/researchlab/sequences.yaml)
- [evolved/buildings/drillrig/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/drillrig/sequences.yaml)
- [evolved/buildings/powerstation/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/powerstation/sequences.yaml)
- [evolved/buildings/clanhall/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/clanhall/sequences.yaml)
- [evolved/buildings/blacksmith/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/blacksmith/sequences.yaml)
- [evolved/buildings/beastenclosure/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/beastenclosure/sequences.yaml)
- [evolved/buildings/menagerie/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/menagerie/sequences.yaml)
- [evolved/buildings/alchemyhall/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/buildings/alchemyhall/sequences.yaml)
- [survivors/towers/guardtower/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/towers/guardtower/sequences.yaml)
- [survivors/towers/missilebattery/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/towers/missilebattery/sequences.yaml)
- [survivors/towers/cannontower/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/towers/cannontower/sequences.yaml)
- [evolved/towers/machinegunnest/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/towers/machinegunnest/sequences.yaml)
- [evolved/towers/grapeshotcannon/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/towers/grapeshotcannon/sequences.yaml)
- [evolved/towers/rotarycannon/sequences.yaml](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/towers/rotarycannon/sequences.yaml)


- OpenKrush (the successor to the archived OpenRA KKnD mod):
  https://github.com/IceReaper/OpenKrush
  - Info-table audit pinned revision: `76c634d05984e48e1e474460c46607aee0bc78a1`.
  - `Assets/FileFormats/Lvl.cs` documents the `DATA` container's typed file
    lists and archive-global asset offsets.
  - `Assets/FileFormats/Mapd.cs` documents embedded palettes, `SCRL` layers,
    32×32 tile pointer grids, and the transparent upper layer.
  - `Assets/FileFormats/Mobd*.cs` documents animation tables, frame anchors,
    `SPRT` render records, and the Gen1 per-scanline sprite decompressor.
- Archived OpenRA KKnD mod:
  https://github.com/Dzierzan/KKnD
  - Info-table audit pinned revision: `d094389c01f1a985115e7a865b4fbc36b706c006`.
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
- The first CPLC member contains four linked lists of mission records. In
  `SURV_01.LVL`, the unit records contain native pixel positions and resolve to
  10 Survivor infantry, 2 Survivor bikes, 1 Survivor pickup, 17 mutant
  berserkers, and 3 mutant wolves. There are no starting building records in
  the unit-name records. The loader divides these positions by 32 and centers
  the initial camera on the player formation; the camera-centroid choice is an
  inferred presentation rule, not a confirmed executable camera field.
- `BOXD` (collision/passability), resource-node records, and non-unit CPLC
  records are still unimplemented. Starting resources remain the temporary
  5000-per-side gameplay value. The previous engine fallback setup of two bases,
  tankers, buildings, units, and vents is no longer used for `SURV_01.LVL`.

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

Dark Colony's gameplay states are authored directly in
`games/dark-colony/info.c`, one row per run of frames; the earlier
`tools/dc_states.txt`/`dc_states.py` pipeline and its `animate/*.inc` output
are gone. See `docs/DC_INFO_CONV.md`. Quake II's `mmove_t`
(`firstframe`, `lastframe`, per-frame callbacks, `endfunc`) is the model for
the run; no Quake II source is vendored.

## SDL software rendering memory (September 10, 2026)

- Installed macOS dependency: Homebrew `sdl2-compat` 2.32.70 with SDL 3.4.12
  (`pkg-config --modversion sdl2 sdl3`; `otool -L build/bin/dark-colony`).
- [sdl2-compat 2.32.70](https://github.com/libsdl-org/sdl2-compat/blob/release-2.32.70/src/sdl2_compat.c):
  `SDL_CreateRenderer` chooses SDL3's software backend for the software flag;
  `SDL_RenderCopy`/`SDL_RenderCopyEx` forward to SDL3 texture draws.
- [SDL 3.4.12 software renderer](https://github.com/libsdl-org/SDL/blob/release-3.4.12/src/render/software/SDL_render_sw.c):
  `SW_CreateTexture` enables RLE for static surfaces; `SW_RenderCopyEx` locks
  the entire source before accessing pixels, then unlocks it. Uncompressed
  streaming surfaces avoid repeated whole-atlas RLE conversion when flipping
  small terrain rectangles. This is distinct from original-game SPR RLE.
- [SDL 3.4.12 video](https://github.com/libsdl-org/SDL/blob/release-3.4.12/src/video/SDL_video.c):
  `ShouldAttemptTextureFramebuffer`, `SDL_CreateWindowFramebuffer`, and
  `SDL_CreateWindowTexture` explain single-framebuffer presentation through an
  accelerated backend. Disabling that path is unsupported by this Cocoa driver;
  it is independent of CPU sprite/palette blits. Do not infer the installed
  implementation from classic SDL2 sources simply because the app uses SDL2 APIs.
- Measurements, rejected hypotheses, pixel comparisons, and remaining indexed
  framebuffer limitations are recorded in `docs/DC_EXE_FINDINGS.md`,
  “September 10: remaining memory after indexed sprite conversion”.

## Dark Colony commander badges and selection (2026-09-10)

- Primary evidence: local retail `data/DCOLONY/DC.EXE` (SHA-256
  `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`),
  `INTRFACE/CLIENT.SPR`, `ANIMATE/{TRSC,GRAY}.FIN`,
  `GAMESTAT/GAMESTAT.TXT`, and `SCENARIO/HUMAN/HUMAN01.SCN`.
- Visual provenance: two user-provided HUMAN01 retail screenshots in the
  September 10 conversation, showing the commander unselected and selected.
  No external image URL was supplied.
- Native overlay loop: `0x4333b4`; type presentation flags at record `+0xf4`;
  persistent human badges 35–38 and alien badges 47–50. Native standing origin:
  `0x438972–0x4389f8`, FIN slot 6 via `0x423ccc`, STAND union via `0x4239f0`.
- Detailed formulas, hashes, corrections and reproduction commands:
  [DC executable findings](docs/DC_EXE_FINDINGS.md#persistent-commander-rank-and-selection-composition-2026-09-10).

## OpenRA UI and KKnD production references (2026-09-13)

The requested reference checkouts are in the existing ignored `reference/`
directory (case-insensitive `Reference` on this workspace filesystem):

- [OpenDR](https://github.com/drogoganor/OpenDR), `reference/OpenDR`, pinned to
  `98079a904746440433795fe7f21c4b35eb6b3959`.
  `mods/dr/chrome/ingame-player.yaml`, `mods/dr/chrome.yaml`,
  `mods/dr/sequences/{structures,infantry,vehicles}.yaml`, and
  `mods/dr/rules/structures-building.yaml` supply sidebar rectangles, glyphs,
  original SPR menu-image names, and retained base prerequisites after upgrades.
  Delivery investigation (2026-09-26): `mods/dr/rules/structures.yaml` defines
  the Water Launch Pad's `DockHost` offset/facing, while the power refinery is
  commented out; `mods/dr/rules/vehicles.yaml` configures shared harvester and
  docking traits. `OpenRA.Mods.Dr/Traits/Buildings/DrRefinery.cs` credits water
  for accepted resources. These are reference behavior, not proof of retail
  docking. `mod.config` pins OpenRA to `playtest-20260222` (engine not present
  locally). Retail `BUILD.TXT` instead supplies `SetBay`; `dkreign.exe`
  `0x004a056c..0x004a059c` stores its arguments in the low two nibbles of the
  building-type field at `+0x22c`. See the delivery-point investigation in
  `docs/DR_EXE_FINDINGS.md` for executable fingerprint and diagnostics. The
  subsequent “Retail transporter docking correction” traces route creation
  (`0x0049bbb0`), exact bay lookup (`0x00422550`), transfer/facing/one-shot
  handoff (`0x0049c010`), and animation advancement (`0x004a96f0`) in the same
  executable. Local native `dark/deftxt/{BUILD,UNITS,OVLEFF}.TXT` and
  `ucfrgst0.spr`/`uchfrst0.spr` supply bay coordinates, collision masks,
  capacities/batches, facing and section timing. The user's September 26
  screenshot is of the defective open-rts result, not retail. No upstream
  DockOffset or DockAngle was substituted for the native rules.
- [OpenKrush](https://github.com/IceReaper/OpenKrush), `reference/OpenKrush`,
  pinned to `76c634d05984e48e1e474460c46607aee0bc78a1`.
  `OpenRA.Mods.OpenKrush/Widgets/Ingame/` supplies the 48-pixel sidebar and
  category/action layout. `mods/openkrush_gen1/actors/` supplies the two faction
  skins, product icons, rule values, and custom Barracks/Warrior Hall images.
  `OpenRA.Mods.OpenKrush/Mechanics/Researching/` supplies research behavior;
  `mods/openkrush_gen1/core/rules/palette.png` supplies the indexed palette.
- [OpenRA PNG sheet loader](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Common/SpriteLoaders/PngSheetLoader.cs),
  `RegionsFromSlices`, inspected 2026-09-13: `FrameSize` slices the sheet in row
  order; omitted `FrameAmount` defaults to the number of complete cells.
  The former Barracks/Warrior Hall sheets used this subset of PNG metadata.
  PNG loading and bundled OpenDR/OpenKrush images were removed from the engine;
  these links preserve historical provenance, not current asset dependencies.

To reproduce the checkouts:

```sh
git clone https://github.com/drogoganor/OpenDR.git reference/OpenDR
git -C reference/OpenDR checkout 98079a904746440433795fe7f21c4b35eb6b3959
git clone https://github.com/IceReaper/OpenKrush.git reference/OpenKrush
git -C reference/OpenKrush checkout 76c634d05984e48e1e474460c46607aee0bc78a1
```

Copied assets live in `games/dark-reign/ui`, `games/kknd/ui`, and
`games/kknd/openkrush`. Each UI directory includes the upstream GPL COPYING;
KKnD icons retain upstream per-actor credits as `<product id>-CREDITS.md`.
The custom Barracks and Warrior Hall retain their separate authorship notes.
Dark Reign menu SPRs are loaded from the user's game data and are not copied.
Detailed findings and limitations are in `docs/DR_EXE_FINDINGS.md` and
`docs/KKND_EXE_FINDINGS.md`; these OpenRA-derived rules are not claims about
retail executable behavior.

### KKnD opening combat and turret composition (2026-09-13)

Native asset offsets, fingerprints, verified/inferred boundaries and focused
checks are recorded in [KKND_EXE_FINDINGS.md](docs/KKND_EXE_FINDINGS.md),
“Opening combat: layered fire, retaliation and deaths”. The installed DOS/LE
executable was fingerprinted but its instructions were not traced here.

OpenKrush, existing local checkout `reference/OpenKrush`, revision
`76c634d05984e48e1e474460c46607aee0bc78a1`:

- [Native muzzle sequences](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/core/sequences/muzzle.yaml)
- [Survivor infantry deaths](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/infantry/sequences.yaml)
- [Evolved infantry deaths](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/evolved/infantry/sequences.yaml)
- [Explosion frames and timing](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/core/weapons/explosions/sequences.yaml)
- [Death explosion selection](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/core/weapons/explosions/weapons.yaml)
- [Pickup turret source](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors/survivors/vehicles/4x4pickup/sequences.yaml)
- [MOBD point records](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Assets/FileFormats/MobdPoint.cs)
- [Weapon point selection and turret composition](https://github.com/IceReaper/OpenKrush/blob/76c634d05984e48e1e474460c46607aee0bc78a1/OpenRA.Mods.OpenKrush/Mechanics/DataFromAssets/Traits/OffsetsArmament.cs)

[OpenKKnD](https://github.com/wdigger/OpenKKND), local checkout
`reference/OpenKKND`, pinned revision `3702e29992d0abf5e3b0648a57858fd578afc991`,
is an Extreme-version reconstruction based on executable decompilation. Its
[attachment and unit tables](https://github.com/wdigger/OpenKKND/blob/3702e29992d0abf5e3b0648a57858fd578afc991/src/_unsorted_data.cpp)
confirm `turret_4x4Pickup -> MOBD_MUTE_MONSTER_TRUCK` and the Pickup's attachment
reference. [kknd.h](https://github.com/wdigger/OpenKKND/blob/3702e29992d0abf5e3b0648a57858fd578afc991/src/kknd.h)
identifies member 0x2f. This corroborates OpenKrush's explicit selection; its
Windows function addresses must not be presented as addresses in our DOS game.
No upstream code or game binaries were copied into tracked source files.

### Dark Reign Mission 01 / HUD evidence (2026-09-13)

The user supplied a retail Mission 01 screenshot (three orange Freedom Guard
buildings, 4,000 credits) and an open-rts screenshot (three rigs, 12,000 credits)
in the September 13 conversation. No external URL was supplied. These are visual
comparison references; layout, font, icon translations and mission selection
were checked against local `data/REIGN/dkreign.exe`,
`dark/scenario/FIXED/M01F/M01F.SCN`, `dark/deftxt/{UNITS,BUILD}.TXT`,
`dark/graphics/INTFACE/IGI/` and the PALS RGB555 lookup. Detailed addresses,
executable fingerprint, corrections and remaining unknowns are in
`docs/DR_EXE_FINDINGS.md`, “Mission 01 and retail HUD correction”.

- D2PLAY01 multiplayer movement and SCGM hover: local DC.EXE SHA-256
  `008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
  fresh radare2 disassembly of `0x43f07c` (PTH loader), `0x411ec4` and
  `0x43f730` (neighbor/path expansion), `0x41a11f` (air flag),
  `0x4238c8` (animation bounds), plus existing `0x4385f8` type-load evidence.
  Native D2PLAY01 SCN/MAP/PTH and SCGM/SARG FIN inputs establish authored
  positions, blocked starting cells, idle bobbing and the high Sarge marker.
  See [the findings and fingerprints](docs/DC_EXE_FINDINGS.md#d2play01-starting-units-blocked-spawns-and-osprey-idle-2026-09-13).
  Doom `reference/DOOM/p_map.c` (`P_CheckPosition`, `P_TryMove`) and
  `reference/DOOM/p_inter.c` (`P_KillMobj`, clearing flight flags) informed
  the shared movement/cleanup ownership. Complete retail blocked-start
  recovery and projection of aircraft object word +0x02 remain unverified.

### Dark Colony Single Player War reference (2026-09-27)

User-supplied local recording:
`/Users/igor/Desktop/Screen Recording 2026-09-27 at 13.27.04.mov`, 30.687 seconds,
SHA-256 `6ed67deddd0af7a6d69e44b66bd2049a9011badf1552ce61f30fb92a74ce2152`.
No public URL was supplied. It shows the native eight-player setup and a match.
The file was sampled with a temporary C program using macOS AVFoundation;
no media or generated screenshots were bundled as game assets.

The controlling evidence is local `data/DCOLONY/DC.EXE` (SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`) and
`INTRFACE/MULTIE`, `TCPWAIT.{DAT,GIF,RMP}`, native MFONTO/KNOBE SPR/FIN files,
SCENARIO/MPLAYER SCNs and TRO scripts. Fresh radare2/r2ghidra reads traced
`0x40fb20`, `0x401210`, `0x40f840`, `0x420c8c`, `0x424350`, `0x4223c0`,
`0x44b5e4`, `0x427790`, `0x424638`, `0x41a61c` and `0x4385f8`.
[Detailed findings](docs/DC_EXE_FINDINGS.md#single-player-war-and-native-menu-blitting-2026-09-27)
preserve fingerprints, data fields, corrected hypotheses, implementation
boundaries and reproduction commands. In particular, native menu blitting
supersedes the older world-FIN placement assumption; unported storage/artifact,
vent-script and AI+ effects remain explicitly identified.

### Dark Colony artillery, tower rockets and mines (2026-09-27)

Local retail `DC.EXE` SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`:
weapon loader `0x4381ac`, fire `0x412174`, projectile spawn/tick
`0x43dc74`/`0x43e92c`, damage/blast `0x43de94`/`0x43e150`, curve tables
`0x4758b0`/`0x4758f4`. Cross-checked with native GAMESTAT, WEAPSTAT, BOOMSTAT,
MBULLET, BDF, ENGI encyclopedia and FIN files using radare2 and the C
`dc_info_conv` inspector. No new external source was used.
[Detailed findings and remaining gaps](docs/DC_EXE_FINDINGS.md#artillery-tower-rockets-and-deployed-mines-2026-09-27).
Doom's local `reference/DOOM/p_mobj.c` supplies the missile ownership/state
lifecycle reference; `p_map.c` supplies the explosion damage traversal pattern.

### Dark Colony pathfinding and multiple-unit movement (2026-09-29)

Local `data/DCOLONY/DC.EXE`, 566,272 bytes, SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`;
native scenario `.PTH` files supply the 256-by-256 next-family table and
bottom-up family plane. Radare2/r2ghidra discovery plus exact instruction/data
reads established `0x43f07c` (load), `0x43f730`/`0x44085c` (bucket search),
`0x440dc4` (main family route), `0x4409d8` (local occupied search), `0x440ac0`
(aircraft), `0x4759a8` (9-by-9 direction costs), `0x41bdd0` (shared selected-unit
destination), `0x414418`/`0x414680` (congestion), and `0x4117fc` (release origin
occupancy). No external source was added. Local `reference/DOOM/p_map.c`
provided the validate-then-commit movement ownership reference.
[Detailed evidence, behavioral vectors and unported ticker behavior](docs/DC_EXE_FINDINGS.md#native-path-search-and-group-movement-2026-09-29).

### Dark Colony bombs, aircraft and artillery audit (2026-09-30)

Local retail DC.EXE SHA-256
`008052f5bc7fadfbf3809187256b000dd0115aaef1ab4fd0a9c26dfe93661f5a`,
with GAMESTAT/WEAPSTAT/BOOMSTAT/MBULLET text, aircraft/artillery FINs and retail
encyclopedia text. Radare2/r2ghidra discovery and fresh instruction reads establish
burst counter `0x4125f5..0x41261d`, timer slot write `0x41164c`, flight spawn/tick
`0x43dc74`/`0x43e92c`, zero-damage target rejection `0x432856`, impact search
`0x431e00`, damage `0x43de94`, ground blast `0x43e150`, and native types 49/50
healing dispatch `0x413cc7..0x413cdf` to `0x412f74`. C `dc_info_conv` verifies global
SPIKE/EGG labels and absent SPAKBULLET. No external source was added.
[Comparison](docs/DC_UNIT_BEHAVIORS.md) and
[detailed findings, fingerprints and reproducers](docs/DC_EXE_FINDINGS.md#bombs-flying-units-and-artillery-comparison-2026-09-30)
preserve baseline failures, newly confirmed rules and unresolved behavior.

Follow-up implementation used the same retail binary and additional focused
reads: healing/recharge `0x412f74`/`0x41840c`, detector sight `0x441d54` and mask
reset `0x446158`, FIN attachments `0x423d00`, heading/trig
`0x43d930`/`0x43da94` with tables `0x4746ac`/`0x4756ae`, scatter loader
`0x43813f`, SCN upgrade load `0x41ad91`, row mapping `0x419bc0`, and research
command `0x41b698`. Native DEPEND/MAINE, SMAY, SCGM, BARR and ATRIL inputs are
fingerprinted in the [implementation findings](docs/DC_EXE_FINDINGS.md#bomber-support-and-artillery-implementation-2026-09-30).
No external source was added. Local Doom `p_mobj.c` remains the spawning,
missile-origin reference and state/thinker lifetime model; FINs and upgrade data
remain level-owned. The report distinguishes the implemented arithmetic and
selection rules from unverified retail UI, crash, retraction and action timing.

### Dark Colony sidebar audit (2026-09-30)

The retail executable and native MAINE/DEPEND/PALETTE.RMP/WEAPSTAT assets are
the sources for the [sidebar findings](docs/DC_EXE_FINDINGS.md#sidebar-purchases-tabs-and-research-audit-2026-09-30).
The executable was read from the main checkout at
`/Users/igor/Developer/open-rts/data/DCOLONY/DC.EXE`, with the same fingerprint
as previous unit investigations. `r2` instruction checks supplement the cached
`reverse/dc-exe-r2ghidra/dc_exe.c` decompilation. The report preserves native
addresses, negative brightness semantics, purchase reservations, all research
rows, and the unfinished special/secondary/options dispatch evidence. No web
source or executable-derived runtime balance configuration was introduced.

### Dark Reign PATHS evidence (2026-09-30)

Primary sources are the local retail `data/REIGN/dkreign.exe` (SHA-256
`3e089777cea09b0fa7cb772c72c871677594515f3508baa04fc13d4dad84a965`),
`dark/graphics/INTFACE/IGI/{MFDBAC1,BASADV,SBTNS,TRAILMDE}.BMP`, FONT12W/T.PCX,
retail HELP.TXT and PathT3 tutorial text. No external image or generated asset
replaces retail files. Focused r2 reads verify PATHS initializer `0x467d60`,
callbacks `0x460c40..0x4616a0`, traversal `0x475c10`, node allocation
`0x4755b0`, selector table `0x5beab0` and radar routes `0x48f990`.
[DR_EXE_FINDINGS.md](docs/DR_EXE_FINDINGS.md#paths-controls-and-shared-engine-routes-2026-09-30)
records instructions, provenance, disproven page assignments and unresolved
contracts. OpenDR is a secondary local reference; its path support does not
certify the retail HUD. Completed DC HUD commit `05f753d` was inspected and
integrated before moving its real route storage/execution into common engine
code. Doom's existing thinker/command lifecycle remains the execution owner.

### Dark Colony options/allies screenshots (2026-10-01)

User-provided local screenshots `Screenshot 2026-10-01 at 19.42.00.jpg` and
`Screenshot 2026-10-01 at 19.42.06.jpg`, supplied from `/Users/igor/Desktop/`,
show the options column and allies/player rows respectively. Native MAINE
and the existing retail dispatch findings identify their controls; a headless
native-asset render and sidebar input tests distinguish implemented actions
from visible placeholders. No external URL was supplied or web source used.
[Verification and remaining gaps](docs/DC_EXE_FINDINGS.md#options-and-allies-screenshot-verification-2026-10-01).

The follow-up implementation traced the cached r2/r2ghidra disassembly against
the same DC.EXE fingerprint, checked 137,889 complete instruction byte strings
against PE section bytes with zero mismatches, and implemented the native
LOPTE/LOBJE/LSGE/LOADGE dialogs and MAINE group 153. No external web sources
were used. Doom's local `reference/DOOM/p_saveg.c` (P_ArchiveThinkers and
P_UnArchiveThinkers) supplies the thinker archive/restore ownership model;
RTS object references are restored by stable IDs rather than discarded.
[Native menu actions and engine saves](docs/DC_EXE_FINDINGS.md#native-sidebar-dialogs-and-diplomacy-2026-10-01).

### Dark Colony multiplayer globe and TCP entry

The user's retail reference is `/Users/igor/Desktop/Screenshot 2026-10-02 at
11.45.32.jpg`; the open-rts session-name prompt is shown in the companion
`Screenshot 2026-10-02 at 11.46.36.jpg`. Native inputs are
`INTRFACE/{NETOPTE,NET.GIF,BLEW.SPR}`, `ANIMATE/{NET,NETD}.FIN`, and the
fingerprinted retail DC.EXE. Existing r2/r2ghidra discovery output in
`reverse/dc-exe-r2ghidra/{skirmish-entry.txt,all-instructions.txt,dc_exe.c}`
locates the separate BLEW PIC window at `0x40578b–0x4057a6`, decorative
startup at `0x4057eb–0x405828`, millisecond PIC playback at
`0x426497–0x426608`, and direct TCP host/lobby entry at `0x405921–0x40592b`
and `0x405600–0x405626`. The reference checkout used for this investigation
is `/Users/igor/Developer/open-rts`; its DC.EXE remains there rather than in
this worktree's runtime data. See [the complete findings, input hashes and
regression commands](docs/DC_EXE_FINDINGS.md#multiplayer-globe-decorative-animation-and-tcp-host-entry-2026-10-02).

### Shared navigation corner repair (2026-10-02)

The user-provided local `Screen Recording 2026-10-02 at 11.51.02.mov` and native
`data/DCOLONY/SCENARIO/HUMAN/HUMAN01.MAP` informed the reproduction. The exact
mission in the recording remains unknown. Doom's existing local
`reference/DOOM/p_enemy.c:P_Move` and `reference/DOOM/p_map.c:P_TryMove` were
consulted for collision validation before committing an object's position.
No new external source or executable decompilation was used.
[Evidence and regression vectors](docs/DC_EXE_FINDINGS.md#corner-jitter-in-shared-navigation-2026-10-02).

### Dark Colony night terrain selector and day/night damage (2026-10-02)

Local retail `data/DCOLONY/DC.EXE` (fingerprint above), read through the
existing r2/r2ghidra `reverse/dc-exe-r2ghidra/all-instructions.txt`:
`0x40a7b3`, `0x432f54`, `0x44ee68` (night light selector) and `0x43ecbd`,
`0x43de94`, `0x4387d7` (race/phase direct-hit penalty), with the native
`*.RMP` banks. No external source was used.
[Findings, hashes and commands](docs/DC_EXE_FINDINGS.md#night-terrain-selector-and-daynight-direct-hit-damage-2026-10-02).

### Dark Colony larger sight and fog performance (2026-10-03)

Retail executable fingerprint above; fresh C extraction from `0x483fbc`,
existing r2/r2ghidra listings at `0x446240..0x44625e` (day/night radius),
`0x418b54` (sight cadence), and `0x418c55..0x418c8f` (independent base income).
The executable is available in the primary checkout at
`/Users/igor/Developer/open-rts/data/DCOLONY/DC.EXE`. The larger radius and
30 Hz refresh are requested engine behavior.
[Evidence, corrected doubling defect, native parent rule and benchmarks](docs/DC_EXE_FINDINGS.md#sight-radius-audit-and-fast-fog-refresh-2026-10-03).

The user-provided [Warcraft 2000: Nuclear Epidemic source](https://github.com/agend/warcraft-2000-nuclear-epidemic),
local `/Users/igor/Developer/warcraft-2000-nuclear-epidemic`, commit
`018cf4b7c7c502ebe51505ea3dd5588e8ae32e48`: `fog.cpp` (`LoadFog`,
`ShowSuperFluentFog32_160`, `ShowSuperFog`, `ProcessFog`) and `Nation.cpp`
(`OneObject::MakePreProcess`). Used as a performance comparison for palette
lookups, uniform-tile fast paths and direct framebuffer writes. Its scalar
vision diffusion and spot stamping are not Dark Colony visibility rules.

The later user revision restores retail sight distances and sets fog refresh
to 10 Hz; the 2x distance and 30 Hz policy in the preceding entries is
superseded. The C extractor now emits only the verified 317-node native tree.
[Current policy and checks](docs/DC_EXE_FINDINGS.md#current-fog-policy-retail-distances-and-10-hz-refresh-2026-10-03).

### Warcraft II forest removal (2026-10-07)

The pinned Wargus `scripts/tilesets/wargus/{summer,winter,wasteland,swamp}.lua`
definitions identify the forest mixed groups and narrow-tree specials;
see [winter.lua](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/tilesets/wargus/winter.lua).
The update behavior lives in Wargus's Stratagus dependency, now checked out
at `reference/stratagus`, commit `3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc`:
[map.cpp](https://github.com/Wargus/stratagus/blob/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src/map/map.cpp)
(`ClearWoodTile`, `FixNeighbors`, `FixTile`),
[tileset.cpp](https://github.com/Wargus/stratagus/blob/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src/map/tileset.cpp)
(`getTileBySurrounding`), and
[script_tileset.cpp](https://github.com/Wargus/stratagus/blob/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src/map/script_tileset.cpp)
(corner-mask table construction). GPL-2; used to establish behavior and
tile-format values, with an independent C implementation in open-rts.

The pinned [Warcraft 2000 Nature.cpp](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/blob/4d12ad3e62ba03c59b2dbec2a989f58d744018ee/Nature.cpp)
(`TakeResource`, `CreateSurface`) separately confirms updates to all surfaces
sharing a depleted resource. Its resource-corner map is different and was
not adopted. Detailed findings and remaining retail unknowns are in
`docs/WAR2_EXE_FINDINGS.md`, “Forest borders after harvesting”.

### Warcraft II vehicle movement (2026-10-07)

Compared the pinned [Wargus human animation scripts](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/anim.lua)
and [orc animation scripts](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/orc/anim.lua)
for siege, ships, submarines and scout aircraft. Movement frame operands
establish separate 0/1 siege and flying-machine cycles and single-pose ships
and zeppelins. Script source was not copied; C state ranges use those values.

Compared [Warcraft 2000 Nation.cpp](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/blob/4d12ad3e62ba03c59b2dbec2a989f58d744018ee/Nation.cpp)
(`LoadAnimation`, `LoadCurAnm`, land movement) and
[Water.cpp](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/blob/4d12ad3e62ba03c59b2dbec2a989f58d744018ee/Water.cpp)
(water movement's `AnmGoKind` selection). Behavioral reference only; its
animation format is different. Native GRP verification, the disproven
row-count heuristic, and remaining timing/combat limitations are recorded
in `docs/WAR2_EXE_FINDINGS.md`, “Vehicle movement frames”.

### Warcraft II PUD and MAINDAT (2026-10-03)

Retail `data/WAR2/DATA/MAINDAT.WAR` (10,403,193 bytes, SHA-256
`791bae4480d564f017122a82c9481dabd952424151f2b5d20245793e654ad3bb`) and the
eight loose PUDs under `data/WAR2/`. `WAR2.EXE` (878,119 bytes, SHA-256
`a2b4b2118ec6355371b58134be8c7331d1facc5989e7188a1d1bb68fd1f26671`, 22 May
1997) was not disassembled.

[Wargus](https://github.com/Wargus/wargus) checkout `reference/wargus`, commit
`cde1a0718a0058cc651ecd56ff8149fc39f624e9`. GPL-2. Used as a format and stats
reference (`pud.cpp`, `wartool.cpp`, `scripts/stratagus.lua`
`DefinePlayerColorIndex(208, 4)`, `scripts/*/units.lua`). Source was not
copied.

[war2tools](https://github.com/war2/war2tools) `libwar2/sprites.c` (MIT) was
read for GRP entry numbers. Its single player-RGB table matches winter blue,
not the forest palette, and is not used for remaps. See
`docs/WAR2_DATA.md`.

[Warcraft 2000: Nuclear Epidemic](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic)
checkout `reference/warcraft2000`, commit
`4d12ad3e62ba03c59b2dbec2a989f58d744018ee`. This is a later pin than the fog
comparison checkout `018cf4b7` recorded above. `Build.cpp` does not load PUD
or `MAINDAT.WAR`; that map format was not adopted. License unclear; behavioral
reference only.

### Warcraft II gathering and native HUD (2026-10-04)

The same WAR2.EXE and MAINDAT fingerprints above apply. WAR2.EXE was
identified as DOS4GW/LE, but was not disassembled or run. Native MAINDAT
carrier GRPs 122–125 have 65 records each; UI GFU 187, FONT 282/283 and
existing human/orc chrome were decoded directly. Local
`reference/DOOM/p_tick.c` was consulted for ordinary thinker ownership.

The pinned Wargus checkout above supplies `pud.cpp` (`UNIT.Data * 2500`),
`wartool.h` (carrier GRP entry numbers; trailing 25 is a repair-frame
combination, not the GRP count), `scripts/{human,orc}/units.lua` and
`anim.lua` (worker waits/poses and production improvements),
`scripts/tilesets/*.lua` (removed-tree megatile 126), `scripts/fonts.lua`
(native ink ramps), `scripts/ui.lua` and `scripts/human/ui_pandora.lua`
(HUD coordinates). Used as references, not copied implementations. Delay
and full HUD fidelity remain unverified against retail executable behavior.

The pinned Warcraft 2000 checkout above was also compared directly:
`Nature.cpp` (`TakeResource`, `FindNearestBase`, `TakeResLink`, `ShowRMap`),
`Build.cpp` (`CreateBuilding`), `Interface.cpp` (`GSSetup800`) and
`mapa.cpp` (`CreateMiniMap`, `DrawMiniMarker`). Its typed gathering/return
phases and owned depot checks informed the comparison. Its 160-pixel HUD,
48×40 icons, resource multipliers/taxation and map format were not adopted.
Retargeting and placement-payment pitfalls are recorded in the findings.

[Stratagus minimap source](https://raw.githubusercontent.com/Wargus/stratagus/master/src/map/minimap.cpp)
and [font source](https://raw.githubusercontent.com/Wargus/stratagus/master/src/video/font.cpp)
were read as primary reference code on 2026-10-04 (unversioned master,
not the pinned local Wargus tree). `GetTileGraphicPixel` supplies native
sampling coordinates and precision; `DrawUnitOn` supplies footprint and
player-color conventions. This does not establish WAR2.EXE behavior.
[Stratagus panel content source](https://raw.githubusercontent.com/Wargus/stratagus/master/src/ui/contenttype.cpp)
was also read from master on 2026-10-04: `CContentTypeText::Draw` and
formatted text use the width before `~|` as an alignment anchor. The HUD
now uses that rule with Wargus's stat/production/level coordinates.

Blizzard's primary published guides, accessed 2026-10-04:

- [Peasant/Peon](https://classic.battle.net/war2/units/peasant.shtml):
  worker stats, 51 chops, private competing-worker progress.
- [Resources](https://classic.battle.net/war2/basic/): 100 gold per trip,
  hall upgrades increase income.
- [Town management](https://classic.battle.net/war2/gs/town.shtml) and
  [offense](https://classic.battle.net/war2/gs/offense.shtml): replacement
  depots and loss of income improvements when upgraded halls are destroyed.
- [Footman](https://classic.battle.net/war2/units/footman.shtml),
  [Catapult/Ballista](https://classic.battle.net/war2/units/catapult.shtml),
  [Destroyer](https://classic.battle.net/war2/units/destroyer.shtml),
  [Battleship](https://classic.battle.net/war2/units/battleship.shtml),
  [Submarine](https://classic.battle.net/war2/units/submarine.shtml),
  [Mage](https://classic.battle.net/war2/units/mage.shtml): displayed
  damage ranges/armor; the guide takes precedence over reference mismatches.
- [Combat mechanics](https://classic.battle.net/war2/basic/combat.shtml):
  basic/piercing distinction. Combat itself remains unimplemented here.

The Blizzard Battle.net Edition manual mirror
([PDF](https://downloads.war2.ru/war2/Info%20%26%20Media%20content/Documents/War2BNE_Manual_EN.pdf))
was read as supplemental context. Attempts to inspect the
[Blizzard-hosted screenshot](https://bnetcmsus-a.akamaihd.net/cms/page_media/16/16CBPD0W69JA1706136010943.jpg)
and external DOS screenshot galleries did not yield viewable pixels in
this session. No retail screenshot dimensions/colors were inferred from
search-result descriptions. The original Blizzard-authored DOS
[manual transcript](https://oldgamesdownload.com/manual/warcraft-ii-tides-of-darkness-dos-mac-windows-manual-english/)
(mirror, pages 6–7) identifies owned minimap units/buildings as green and
the resource fields as gold, lumber, oil and food. The previously drawn
score zero has no basis in that description and was removed. Exact pixel
alignment and minimap sampling remain unverified against retail. The report records the native
evidence, corrected hypotheses, review findings and reproducible tests:
[WAR2_EXE_FINDINGS.md](docs/WAR2_EXE_FINDINGS.md).

### Warcraft II complete base unit catalog (2026-10-04)

The same local pins and retail fingerprints above apply; WAR2.EXE was not
disassembled. Wargus `pud.cpp::UnitScriptNames` and
`scripts/{human,orc}/units.lua`, `scripts/units.lua` were read for all 105
native slots (100 defined, five reserved) and base numerical/capability
definitions. `scripts/spells.lua::DefineVariables` supplies global Level
and Mana defaults; Ogre Mage's Level is a variable table with Value 2.
Authored C literals in `games/warcraft-2/info.c` own these values; the C
regression independently audits the pinned tables. No Lua or reference
balance configuration is loaded at runtime.

Blizzard's [Mage](https://classic.battle.net/war2/units/mage.shtml) and
[Death Knight](https://classic.battle.net/war2/units/deathknight.shtml)
guides give maximum mana 255 and initial mana 85, overriding Wargus's 84.
The [Catapult/Ballista guide](https://classic.battle.net/war2/units/catapult.shtml)
gives armor 0, overriding Wargus Ballista armor 5. These primary pages were
accessed 2026-10-04. The shared initial-mana override for other casters is
an inference; reference timing is not certified retail executable behavior.

Warcraft 2000's `MapDiscr.h::GeneralObject` and `Visuals` were compared for
grouped cost/time, mana, resource and damage capabilities. Their numbers
and storage design were not adopted. The findings preserve the previous
unsupported Daemon land classification, numeric indestructibility flags,
Critter exceptions, missing Deathwing graphic, and remaining unknowns:
[complete catalog audit](docs/WAR2_EXE_FINDINGS.md#complete-base-unit-catalog-audit-2026-10-04).

### Warcraft II button rims and footprint selection (2026-10-04)

The same MAINDAT/WAR2.EXE fingerprints and pinned Wargus checkout above
apply. REZDAT.WAR SHA-256 is
`d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`.
Native GFUs 354/355 and REZDAT GFUs 0/1 were decoded and inspected; no
WAR2.EXE disassembly or retail runtime was used.

User-supplied 640×480 screenshots, received on 2026-10-04, are local
references rather than downloaded images. Distribution/version and source
URLs are unknown. Original files and SHA-256:

- `/Users/igor/Desktop/2603_4a56c8cd0a4d8.jpg` (human group selection):
  `0b9e17abe5fc3769372f45aba7eb46cebcc13e4f8355c18513c67590ed9f15bd`.
- `/Users/igor/Desktop/2603_4a56c8ccf27af.jpg` (orc ship selection):
  `d019e26ca95653539e1511a6e7a97e5a040dd0881d2b0120db723674d21e861c`.
- `/Users/igor/Desktop/2603_4a56c8cc93c15.jpg` (human barracks selection):
  `90aba5346e893d120c719bd5b8a364d24191143be98ed65735a64d85c6b7f059`.

The images establish bright icon rims outside the native 46×38 picture,
framed info/progress panels, colored Menu buttons, and sprite occlusion
of selected rectangles. Exact uncompressed RGB/hover dispatch is unknown.
The user's explicit selection rule uses tile footprints even where
reference selection BoxSize differs.

Stratagus master
[unit_draw.cpp](https://raw.githubusercontent.com/Wargus/stratagus/master/src/unit/unit_draw.cpp)
(`CUnit::Draw` draws selection before the body) and
[unittype.cpp](https://raw.githubusercontent.com/Wargus/stratagus/master/src/unit/unittype.cpp)
(`DrawUnitType`, canvas-to-tile centering formula) were read on 2026-10-04.
These are unversioned references, not copied implementation or retail
executable evidence. DOOM `reference/DOOM/r_things.c` was consulted for
sorted sprite drawing. Detailed native offsets, corrected hypotheses and
tests are in the
[HUD/selection correction](docs/WAR2_EXE_FINDINGS.md#button-decorations-and-footprint-selection-correction-2026-10-04).

### Warcraft II serialized dialog resources (2026-10-07)

The pinned Wargus checkout above was consulted at
[`wartool.h`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/wartool.h)
(`Todo[]` and widget rectangles),
[`wartool.cpp`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/wartool.cpp)
(WAR and image/font conversion), and
[`scripts/guichan.lua`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/guichan.lua)
(`RunSinglePlayerTypeMenu`, explicitly authored button positions).
These are format and Wargus-behavior references, not retail scene-layout
authority; no GPL implementation was copied. The extractor's REZDAT catalog
omits the native dialog resources at indices 33–90.

The user supplied an archive/asset summary citing current
[`wartool.cpp`](https://raw.githubusercontent.com/Wargus/wargus/master/wartool.cpp)
and [ModdingWiki's Warcraft II page](https://moddingwiki.shikadi.net/wiki/Warcraft_II),
both read on 2026-10-07. Its explicitly inferred absence of layout resources
is contradicted by the local REZDAT bytes. A menu's lack of documented
editability does not establish absence of serialized rectangles and controls.
The user's other MPQ/man-page links were not used to establish DOS layout.

REZDAT SHA-256 `d0fe7edd4f89f60c64bca786a944387578635d8483bc2ff165d06306bb432246`
and STRDAT SHA-256 `5ba75d38613852be7137c5ec4035578977eb75ce5adc37d219875ba308ce2c26`
identify the inspected Russian DOS data. Native records are independently
decoded in C; exact offsets, complete 58-dialog catalog, confirmed fields,
inferred kind/flag meanings, corrected font-bound hypotheses and remaining
retail unknowns are documented in
[`docs/WAR2_EXE_FINDINGS.md`](docs/WAR2_EXE_FINDINGS.md#native-dialog-resources-and-button-text-bounds-2026-10-07).
The local Doom `m_menu.c` remains the lifecycle/table reference; the game
feeds the shared engine menu table rather than introducing a separate
responder or drawer. Retail Warcraft II was not run in this investigation.

### Warcraft II static DOS UI analysis (2026-10-07)

The disposable local toolchain uses matching
[radare2 6.2.2](https://github.com/radareorg/radare2/tree/ad27058877024389292fddf12e1db6e13824ba34)
and [r2ghidra 6.2.2](https://github.com/radareorg/r2ghidra/tree/1b5cba403c4c8751db8434f6790d5e0f132038f4),
built and installed only under `/private/tmp/war2-analysis-tools/prefix`.
The plugin's native decompiler requires no Java installation. Its packaged
ghidra-native dependency is
[`483ae94bcbc661a77667e52f1eff75928cb6aa2e`](https://github.com/radareorg/ghidra-native/tree/483ae94bcbc661a77667e52f1eff75928cb6aa2e),
with the pinned r2ghidra patches. Tool source was not copied into open-rts.
The pinned radare2 `libr/bin/p/bin_le.c` and LE loader were inspected for
inner-MZ identification and in-memory fixup handling.

Open Watcom's unversioned
[`exe16m.h`](https://raw.githubusercontent.com/open-watcom/open-watcom-v2/master/bld/watcom/h/exe16m.h)
and [`exeflat.h`](https://raw.githubusercontent.com/open-watcom/open-watcom-v2/master/bld/watcom/h/exeflat.h)
were read on 2026-10-07 to verify DOS/16M BW header links and LE fields.
The [Ghidra LX/LE loader](https://github.com/yetmorecode/ghidra-lx-loader)
was evaluated as an alternative; it was not installed or used. radare2 /
r2ghidra remains the project's primary analysis path. Current release pages
were checked; matching 6.2.2 versions were deliberately selected rather than
mixing radare2 6.2.4 with the 6.2.2 plugin.

Static WAR2.EXE UI traces establish resource 6007 for Single Player, leading
to MUDDAT entry 7 and SNDDAT string resource 2047. The pinned Wargus
`wartool.h` archive declarations independently identify type 6000 as
MUDDAT and 2000 as SNDDAT; its movie extraction list omits dialog entries
7 and 13. Local MUDDAT SHA-256:
`e009678457408b593c5705e505b50dc2be518a7097999f2a8da98dcb6811f473`;
SNDDAT SHA-256:
`a1015e38f45ac58578f2979164c603912c7ef501750e46e0c0389e4aa3dbddba`.
See [the executable findings](docs/WAR2_EXE_FINDINGS.md#static-dos-executable-analysis-toolchain-2026-10-07)
for the unchanged inner-image hash, header chain, address mapping, concrete
UI routine addresses, decompiler limitations and reproduction commands.
No retail execution, CD key or runtime trace was used.

### Warcraft II native sound bank (2026-10-07)

The pinned [Wargus](https://github.com/Wargus/wargus/tree/cde1a0718a0058cc651ecd56ff8149fc39f624e9)
checkout supplies `wartool.h` archive indices, `wartool.cpp::ConvertWav`,
`scripts/sound.lua` sound groups/remaps, and `scripts/{human,orc}/{units,anim}.lua`
event and animation assignments. GPL-2 source is reference only; the C
loader and event tables are independently implemented from format facts.
The pinned [Warcraft 2000](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/tree/4d12ad3e62ba03c59b2dbec2a989f58d744018ee)
`GameSound.cpp` is a behavioral comparison for grouped WAVs, viewport
panning and visibility filtering; no source or game-specific constants
were copied. Local `reference/DOOM/s_sound.c` supplies the sound/channel
ownership comparison; the existing Dark Colony sound implementation
supplies the actual shared mixer and event path.

Native `data/WAR2/DATA/SFXDAT.SUD` SHA-256:
`05645c6efb4f38acbff955b20bfd06a6da42942434f37f771f7c792f1e62fb95`
(6,809,845 bytes; WAR type 5000, 293 entries). MAINDAT entry 432 supplies
the UI click; its existing fingerprint above applies. See
`docs/WAR2_EXE_FINDINGS.md`, “Native sound playback”, for the mappings,
verification commands, disproven SNDDAT assumption and retail unknowns.
### Warcraft II campaign startup and results (2026-10-07)

The pinned [Wargus tree](https://github.com/Wargus/wargus/tree/cde1a0718a0058cc651ecd56ff8149fc39f624e9)
supplies campaign entry numbers and result image/palette pairs in `wartool.h`,
PUD UNIT parsing in `pud.cpp`, campaign construction/rescue objectives in
`campaigns/{human,orc}/level01*_c.sms`, `level02*_c.sms` and `level03*_c.sms`,
and the acknowledgement/result distinction in `scripts/stratagus.lua`
(`ActionVictory`, `SinglePlayerTriggers`) and `scripts/menus/results.lua`.
`scripts/wc2.lua::CreateUnit` distinguishes its custom peasant-start additions
from native campaign placement; those additions are not reproduced.

The pinned [Warcraft 2000 tree](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/tree/4d12ad3e62ba03c59b2dbec2a989f58d744018ee)
supplies the comparison in `mapa.cpp::PostLoadExtendedMap`, `ShowWinner`,
and `Nation.cpp::WinnerControl`. Its native MPF format and elimination
logic do not establish Warcraft II PUD or campaign objective semantics.
References are behavioral/format evidence only; no GPL source is copied.

Native map manifests, the corrected premature-victory hypothesis, dialog
3057/3058/3059 rectangles, MAINDAT 359–366 artwork/palettes, and static
WAR2.EXE result selection at VA 0x48318 are preserved in
[`docs/WAR2_EXE_FINDINGS.md`](docs/WAR2_EXE_FINDINGS.md#campaign-startup-native-units-and-result-transition-2026-10-07).
The user-provided 13:29 screenshot is open-rts output, not retail evidence;
its hash is recorded there. No retail execution or CD-key workaround was used.

### 7th Legion sprite direction audit (2026-10-07)

- [Quick converter pack, by Mikhail Beschetnov / Terminus](https://www.extractor.ru/files/cfdacdbdcacba1c5c80c105bf89453f3/),
  [source archive](https://www.extractor.ru/download/quick_pack.zip):
  inspected `Quick7Legionbim/src/Quick7Legionbim.dpr` (archive timestamp
  2002-02-18, version advertised as 0.9). Archive SHA-256:
  `2bcfe7fcc6ea8773191274659919b9c37b6f6df8e3610785b2372a1bc2f69fa2`.
  Corroborates sequential BIM offset-table extraction and sparse scanline
  spans; supplies no direction order or animation grouping. Its sparse first
  word handling differs from our checked data-offset interpretation. Source
  was read, not executed or copied into the engine.
- [btigi/iiEveOfPeace](https://github.com/btigi/iiEveOfPeace/tree/bd5237f36216939af864b6a7a3cd2d83b84a1655):
  **disproven sprite-reference lead**. Although its description names 7th
  Legion, this revision contains Relic SGA/Chunky readers and a Dawn of War
  example, with no BIM decoder or direction mapping.
- Retail `data/7LEGION/legion.exe`, SHA-256
  `a312f7b50a940e5a0ec737cf8923c1d03f552f9c9111c62c72546bbb46f6c154`,
  supplied the actual per-sequence indexing evidence. The 8/16/32 direction
  distinctions, offsets, strides, multipart Rockmech drawing, seven-asset
  loader comparison, unknown compass conversion and reproduction commands
  are preserved in `docs/7LEGION_EXE_FINDINGS.md`, “Sprite direction mapping
  audit”. This audit disproves the current universal eight-block heuristic;
  it does not yet implement its replacement.

### 7th Legion terrain coordinate audit (2026-10-07)

Retail `data/7LEGION/legion.exe` (SHA-256
`a312f7b50a940e5a0ec737cf8923c1d03f552f9c9111c62c72546bbb46f6c154`)
and installed `DATA/MAPT.000`, `MAPL.000`, `MAPOVL.000`, `GFX/TILES2.BIM`
are the primary evidence. Decoder `0x438f50`, ground renderer `0x4967e0`
and movement check `0x43ccc5` establish column-major `x*128+y` storage,
MAPT `(stored ^ (30000-i))-y`, MAPL `stored^x`, and basic ground value 1.
LLVM `objdump` was the available disassembler. See
`docs/7LEGION_EXE_FINDINGS.md`, “Terrain coordinate audit”, for the load/draw
chain, landmark bytes, hashes, corrected hypothesis and reproduction commands.
The user's `Screenshot 2026-10-07 at 09.33.45.jpg` is evidence of the broken
open-rts output, not a retail screenshot or authority for native behavior.
The corrected engine screenshot and temporary C coastline render were visual
verification only; no new external reference or generated asset was used.

The follow-up native-storage revision retains those column-major arrays at
runtime through `L_Index`/`L_Cell`, removing the initial load-time transpose.
The same retail renderer evidence rules out a framebuffer transpose: tile
pixels are already upright. `reference/DOOM/p_maputl.c` was consulted for
direct block-grid addressing. See the “Preserve native storage” subsection
in `docs/7LEGION_EXE_FINDINGS.md` for the implementation, save compatibility,
pixel-equivalence check and remaining sprite-mapping limitation.

### Warcraft II gameplay feature audit (2026-10-07)

Local pinned source comparisons for this audit:

- [Wargus spells](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/spells.lua),
  [missiles](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/missiles.lua),
  human/orc `units.lua`, `anim.lua`, `upgrade.lua`, `buttons.lua`, `icons.lua`,
  `ui.lua`, `sound.lua`, `stratagus.lua`, `wartool.h`, and `pud.cpp`: gameplay
  parameters, dependencies, native effect/status/sound indices, resource
  reserves, transport restrictions and HUD decoration positions.
- [Pinned Stratagus source](https://github.com/Wargus/stratagus/tree/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src):
  `action/action_repair.cpp`, `action_train.cpp`, `action_research.cpp`,
  `action_upgradeto.cpp`, `action_built.cpp`, `actions.cpp`; `unit/unit.cpp`;
  `missile/missile.cpp`, `missile_pointtopointbounce.cpp`,
  `missile_pointotpointwithhit.cpp`, `missile_deathcoil.cpp`,
  `missile_flameshield.cpp`, `missile_whirlwind.cpp`, `missile_stay.cpp`;
  `spell/spell_adjustvital.cpp`, `spell_spawnmissile.cpp`, `spell_summon.cpp`,
  `spell_demolish.cpp`, and `animation/animation_wait.cpp`: damage, burn
  thresholds, repair payment/progress, delayed impacts, spell lifetimes,
  corpse/area selection, supply-at-release and time-cost conversion.
- Native MAINDAT entries 323–351 were decoded directly. Existing MAINDAT,
  SFXDAT and WAR2.EXE fingerprints above apply. No retail process, new
  executable trace, PNG game asset, or runtime Lua dependency was used.

See [the audit and remaining gaps](docs/WAR2_EXE_FINDINGS.md#in-game-feature-audit-against-pinned-wargus-2026-10-07)
for confirmed source facts, native frame-count corrections, implementation
consequences, tests and unimplemented areas. Reference behavior does not
establish exact DOS retail behavior.

### Warcraft II naval/coast/oil correction (2026-10-08)

Behavioral references, read locally at the pinned revisions (no source copied):

- [Wargus PUD SQM specification](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/doc/pud-specs.txt),
  section 14, coast `0x0002/0x0082` versus water `0x0040`.
- [Human units](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/units.lua)
  and the paired `scripts/orc/units.lua`: oil costs/capacity, refinery bonus,
  shore building and minimum oil-deposit distance rules.
- [Human animations](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/anim.lua),
  paired orc animations, and `scripts/anim.lua`: naval attack/sinking rows,
  waits and platform `ResourceActive` poses.
- [Wartool native entry mapping](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/wartool.h)
  and faction `constructions.lua`: loaded tankers and naval construction sheets.
- [Stratagus movement masks](https://github.com/Wargus/stratagus/blob/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src/unit/unittype.cpp)
  and `src/unit/build.cpp`: transport coast access and shore footprints.
- Local `reference/DOOM/p_mobj.c`: state transitions, spawn without entry
  actions and deferred thinker removal; these remain the runtime lifecycle.

[Findings and reproduction](docs/WAR2_EXE_FINDINGS.md#naval-launch-coast-and-oil-corrections-2026-10-08)
record native fingerprints, rejected assumptions, exact entries, tests and
remaining gaps. No retail executable was run.

### Warcraft II original hero verification (2026-10-08)

Read locally at the pinned Wargus revision; used as behavioral/format
references, with no reference implementation copied:

- [PUD unit appendix](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/doc/pud-specs.txt):
  original hero slots 0x31 through 0x35.
- [Human hero definitions](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/units.lua)
  and [orc hero definitions](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/orc/units.lua):
  Lothar, Uther, Cho'gall, Gul'dan and Zul'jin stats, abilities and art families.
- [Human animations](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/anim.lua)
  and [orc animations](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/orc/anim.lua):
  attack waits, projectile strike rows and death-knight death sequence.
- [Orc buttons](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/orc/buttons.lua),
  paired human buttons and `scripts/spells.lua`: Cho'gall's research-free
  spell variants and ordinary Uther/Gul'dan research gates.

[Hero findings and reproduction](docs/WAR2_EXE_FINDINGS.md#five-original-tides-of-darkness-heroes-2026-10-08)
record native fingerprints, frame counts, corrected assumptions, regression
coverage and the distinction between Wargus rules and unverified DOS cadence.

## Building placement previews (2026-10-08)

Local reference sources examined for the shared placement feature:

- [Stratagus DrawBuildingCursor](https://github.com/Wargus/stratagus/blob/3d87c93f7fd8c0b62ee1be5df0a6d9efc72ca6cc/src/video/cursor.cpp):
  native still-frame, top-left grid snap, viewport clip and per-cell green/red
  overlay with opacity 95. Wargus checkout:
  `cde1a0718a0058cc651ecd56ff8149fc39f624e9`.
- [Warcraft 2000 mapa.cpp](https://github.com/ForNeVeR/warcraft-2000-nuclear-epidemic/blob/4d12ad3e62ba03c59b2dbec2a989f58d744018ee/mapa.cpp):
  BuildMode, CheckBuilding and RedBar/WhiteBar behavior; only behavioral
  reference, no map-format or implementation copied.
- [OpenKrush native-game actor rules](https://github.com/IceReaper/OpenKrush/tree/76c634d05984e48e1e474460c46607aee0bc78a1/mods/openkrush_gen1/actors):
  survivor/evolved foundation dimensions and masks; tower default from
  `mods/openkrush/rules/core.yaml`.
- User-provided original-game screenshot, local
  `/Users/igor/Desktop/Screenshot 2026-10-08 at 07.59.53.jpg`: green tinted
  native building image while selecting a location. No unit-blocking rule can
  be proven from this still image alone.

Retail 7th Legion descriptor/image-loader addresses, file fingerprint,
corrected BUILD.BIM hypothesis, supported catalog and remaining unknowns are
recorded in `docs/7LEGION_EXE_FINDINGS.md`. Reproduce descriptor inspection
with the C tool `make 7legion-units`. Dark Reign's native OVLEFF.TXT evidence
is recorded in `docs/DR_EXE_FINDINGS.md`; KKnD reference scope in
`docs/KKND_EXE_FINDINGS.md`; Warcraft presentation scope in
`docs/WAR2_EXE_FINDINGS.md`.

## StarCraft / Stargus (2026-10-08)

- [Stargus](https://github.com/Wargus/stargus), locally `reference/stargus`,
  commit `2a4d54604949e4772f6638a8412c8ebd62444044` (GPL-2.0). Format and
  behavior reference only: `mpqlist.txt`, `src/kaitai/*_dat.ksy`,
  `src/libgrp/GRPImage`, `src/Font.cpp`, `src/UIConsole.cpp`,
  `src/PortraitsConverter.cpp`, `scripts/guichan.lua`, `scripts/icons.lua`.
  Stargus converts assets to PNG/MNG and uses hand-authored Lua menus; this
  plugin independently decodes the original files into engine-owned indexed
  images and shared menu tables. No PNG/MNG dependency is used.
- [PyMS](https://github.com/poiuyqwert/PyMS), locally `reference/PyMS`,
  commit `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4` (MIT). Read-only format
  reference: `PyMS/FileFormats/DialogBIN.py`, `FileFormats/FNT.py`,
  `PyBIN/WidgetNode.py`. No Python program is used by our build or importer.
  `DialogBIN.py`'s `DIALOG_ASSET_SCROLL_*` / `DIALOG_ASSET_COMBOBOX_*`
  indices and `WidgetNode.py::update_dialog` also corroborate the native
  `glue/palnl/dlg.grp` scrollbar and combobox artwork used by Create Game.
  Its two-pixel track spacing and five-pixel arrow inset are editor-preview
  rules, not traced retail executable constants; see `docs/SC_EXE_FINDINGS.md`.
- [Starcraft Palettes](https://github.com/andreas-volz/stargus/wiki/Starcraft-Palettes),
  Stargus author's notes: native team ramps, command-icon palettes, and
  full-health wireframe mappings. Only the documented full-health case is
  implemented; random damaged-section coloring remains unknown.
- [StormLib](https://github.com/ladislav-zezula/StormLib), locally
  `reference/StormLib`, tag v9.24, commit
  `3846f0b8e2c47320c6b499492496f3e3f2e76821` (MIT), matching Stargus's
  `subprojects/StormLib.wrap`. Linked only into the C MPQ import tool, through
  its C API, with zlib/bzip2. The game itself reads unpacked native files.
- [libsmacker](https://github.com/JonnyH/libsmacker), locally
  `reference/libsmacker`, commit `ae8d4c9ec07b24d43ccff184d6e512bae793dfd1`
  (LGPL-2.1; retained `COPYING`). Its C decoder is linked into StarCraft for
  original indexed SMK menu animations and portraits. Source and rebuildable
  objects are retained locally; preserve applicable library licensing when
  distributing binaries.
- [CMake 3.31.6 macOS universal package](https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-macos-universal.tar.gz),
  downloaded under `reference/packages/` because system CMake was absent.
  SHA-256 `330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d`.
  The unpacked package builds StormLib. Existing SDL2 is supplied by
  `pkg-config sdl2`; no new runtime asset-conversion packages are needed.

Retail files remain local and ignored by Git. Their provenance, fingerprints,
format findings, and verification are in `docs/SC_EXE_FINDINGS.md`.

StarCraft documentation entry points:

- [Classic format reference](docs/SC_FORMATS.md): complete examined binary
  layouts, DAT column offsets, lookup formulas, parser limits and audit gaps.
- [Investigation journal](docs/SC_EXE_FINDINGS.md): fingerprints, evidence,
  corrected hypotheses and reproduction commands.

The format reference's IScript audit compares `src/kaitai/iscript_bin.ksy`
from the pinned Stargus checkout with `PyMS/FileFormats/IScriptBIN.py` from
the pinned PyMS checkout. These agree on the documented operand lengths;
our current visual compiler is intentionally reported separately from those
reference definitions. Classic/expanded unit-column offsets are computed
from `tools/sc_catalog/main.c`; only the classic retail dataset was exercised.

The CHK map loader additionally uses the same pinned Stargus checkout's
`src/Chk.cpp`, `src/tileset/TilesetHub.cpp`, `src/tileset/MegaTile.cpp` and
`src/kaitai/tileset_{cv5,vx4,vr4,vf4}.ksy` as format references. The pinned
PyMS `PyMS/FileFormats/CHK/Sections/CHKSection{ERA,UNIT,THG2,OWNR}.py`
corroborates native fields, ERA's `ice` basename and the THG2 sprite/unit
flag. No reference C++ or Python code is linked or run by the game loader.
Map fingerprints, confirmed formulas and explicit limitations are recorded
in [native CHK findings](docs/SC_EXE_FINDINGS.md#native-chk-terrain-and-placements-2026-10-08).

StarCraft gameplay/UI integration (2026-10-08) additionally consulted the pinned
[Stargus source](https://github.com/Wargus/stargus/tree/2a4d54604949e4772f6638a8412c8ebd62444044):
`src/kaitai/weapons_dat.ksy`, `units_dat.ksy`, `UnitsConverter.cpp`, `Chk.cpp`,
`doc/iscript.txt`, `scripts/icons.lua`, Terran/Protoss/Zerg worker and production
unit scripts, neutral resource scripts, and `scripts/stratagus.lua` minimap fog
configuration. [PyMS](https://github.com/poiuyqwert/PyMS/tree/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4)
`FileFormats/DialogBIN.py`, `PyBIN/WidgetNode.py`, and `FileFormats/IScriptBIN.py`
were read as format references for response bounds, text offsets and animation
header indices. No Python tool was added or executed. See the dated shared
gameplay section in `docs/SC_EXE_FINDINGS.md` for byte offsets, hashes, confirmed
behavior, rejected hypotheses and remaining unknowns.

### Warcraft II runtime identifier audit (2026-10-08)

The existing pinned Wargus
[`human/units.lua`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/human/units.lua),
[`orc/units.lua`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/orc/units.lua)
and [`missiles.lua`](https://github.com/Wargus/wargus/blob/cde1a0718a0058cc651ecd56ff8149fc39f624e9/scripts/missiles.lua)
were inspected locally to establish projectile-name provenance and the distinct,
currently unsupported critter-explosion definition. These names are reference
metadata, not native asset IDs or a runtime Lua dependency. The C audit retains
the name mapping while simulation uses enums. See the runtime identifier audit
in `docs/WAR2_EXE_FINDINGS.md`; no new executable analysis or asset generation
was performed.
