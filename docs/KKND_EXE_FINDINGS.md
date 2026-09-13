# KKnD loader evidence

## Representation cleanup (2026-09-09)

**Confirmed by asset-loader comparison to `46f826a`.** All 47 installed LVL map
candidates and MOBD members 0–99 match, including rejected candidates. There
are 44 successful maps and 57 successful MOBD members. Every map comparison
includes the terrain-atlas pixels as well as map metadata and initial mobjs.
Default screenshots are byte-identical. This task did not inspect KKND.EXE and
makes no new executable-address or retail-behavior claim. See
[loader verification](LOADER_REFACTOR_VERIFICATION.md) for commands and scope.

Representative SHA-256 fingerprints:

- `data/KKND/LEVELS/640/SPRITES.LVL`:
  `3e7dbe10624c706afd963e18f54f780052e6ee0b415fc8008add6e37de669ae6`.
- `data/KKND/LEVELS/640/SURV_01.LVL`:
  `66243cfff0f49e22684be6b7074a4bdf71d370f54e22a96d025b0fdcb7af85f7`.

The existing MAPD decoder reads the layer count, layer-offset table, palette
count and palette, then LRCS dimensions and tile offsets. It still skips the
four-byte Gen1 tile prefix and treats index zero as transparent only on upper
layers. Atlas frame numbering, including the gap before the first upper layer,
is unchanged. Palette conversion now writes the final terrain atlas directly;
upper-layer visibility is accumulated from those same pixels while decoding.
There is no need to retain expanded per-layer pixel images or scan them again.

MOBD animation ordering still follows the existing timing/offset traversal.
Only frame offsets are retained until cell allocation; each image then decodes
directly into its own texture. TRPS bit 0 still mirrors the decoded image.
Displacement remains `(width/2,height/2) - authored offset`. Raw and RLE images
retain index-zero transparency. Oversized transparent RLE runs now reject like
oversized literal runs; a fixture covers this malformed-input correction.

**Correction (2026-09-13):** preserving this formula did not establish correct
placement. It is OpenKrush's center-relative displacement, not our renderer's
ground point. See the anchor investigation below.

`MUTE_07.LVL`, `SPRITES.LVL`, and `SUPSPR.LVL` remain rejected as map candidates.
The manifest deliberately includes non-map containers and unsupported MOBD
members; these failures are not interpreted as corrupt retail assets. Animation
selection limits, member-name aliases, and the retail meaning of unsupported
members remain **unverified here** and were not changed.

## Native SURV_01 mission starts (2026-09-13)

**Confirmed from the shipped `SURV_01.LVL` asset; KKND.EXE was not inspected.**
The first CPLC member has a 20-byte header followed by four linked-list heads.
Each object record stores native pixel coordinates at offsets `+5` and `+9`,
linked-list pointers at `+16..+28`, a name pointer at `+52`, and the native
team at `+56`. The declared CPLC size covers the structured records but not the
string table; strings are bounded by the following MAPD asset.

Traversing all four lists with node cycle detection finds 33 mobile unit
records in `SURV_01.LVL`:

- Survivor team 1: 10 `UNIT_SURV_INFANTRY`, 2 `UNIT_SURV_BIKE`, and 1
  `UNIT_SURV_PICKUP`.
- Mutant team 2: 17 `UNIT_MUTE_BERSERKER` and 3 `UNIT_MUTE_WOLF`.

No starting building record was found in the CPLC unit names. The loader maps
these records to the common actor types and divides native pixel coordinates by
32 to obtain world cells. The initial camera is the centroid of the player
formation. This camera rule is **inferred** from the native player positions
and the expected first view; a distinct retail camera record has not yet been
identified. Starting resources remain the existing 5000-per-side gameplay
value; resource-node and BOXD decoding are still **unknown**.

The old two-base setup was synthetic: it placed drill rigs, tankers, buildings,
units, and vents at map-quarter positions. It is removed. A focused check is:

```sh
env SDL_VIDEODRIVER=dummy build/bin/kknd --check
```

## Runtime sprite catalog and native animation channels (2026-09-12)

**Confirmed from assets**, using the SPRITES.LVL fingerprint above. No executable
was examined. `R_InitSprites` previously returned success without loading any
actor sheets, so the interactive renderer reused its fallback rifleman sheet.
It now loads each actor's catalog member once and binds the common sprite table;
subsequent calls retain existing textures when production introduces new types.

The native pointer table before the first image record has seven channels of
sixteen 32-bit animation offsets (448 bytes). Zero slots retain their position.
Compacting nonzero offsets and grouping the result into blocks of sixteen is
**disproven**: it combines different channels on sparse actors. Limiting the
result to four groups also loses authored frames. Complete sixteen-direction
sets now share logical temporal frames; sparse channels retain each sequence
individually. Image pixels, TRPS flip flags, and authored displacement formulas
are unchanged. No missing poses are synthesized.

Two reproducing cases (offsets relative to the MOBD segment):

- Survivor mobile Derrick, member 65: member at 4132641, pointer table at
  4133117, first image record at 4133565. Channel 4 contains sixteen one-frame
  idle sequences, channel 5 one simple frame, channel 6 sixteen two-frame move
  sequences. Logical movement starts at 2, corresponding to flat frame 17.
- Dire Wolf, member 19: member at 1661004, pointer table at 1662396, first image
  at 1662844. Sparse channel 3 contains separate 16- and 24-frame sequences;
  channel 4 has sixteen idle poses, channel 5 sixteen five-frame attacks, and
  channel 6 sixteen seven-frame moves. Logical idle/attack/move starts are
  40/41/46 (flat indices 40/56/136). Treating sparse channel 3 as one rotational
  sequence with maximum length 24 loses sixteen frames; that intermediate
  hypothesis was rejected. The resulting definition has 53 logical frames.

Pinned OpenKrush sequence definitions corroborate those movement/attack starts
and the following building idle selections. Native channel 4 begins at the
listed logical frame; the preceding simple frames include construction art.
Selecting frame zero displayed flags or incomplete buildings.

| Building | Member | Idle frame |
|---|---:|---:|
| Survivor drill rig | 75 | 7 |
| Survivor power station | 74 | 5 |
| Survivor outpost | 52 | 170 |
| Survivor machine shop | 37 | 5 |
| Survivor repair bay | 56 | 20 |
| Survivor research lab | 57 | 5 |
| Evolved drill rig | 50 | 5 |
| Evolved power station | 49 | 5 |
| Evolved clan hall | 13 | 132 |
| Evolved blacksmith | 8 | 5 |
| Evolved beast enclosure | 3 | 5 |
| Evolved menagerie | 42 | 1 |
| Evolved alchemy hall | 0 | 5 |

**Remaining limitations:** tower bases and turrets are distinct native
sequences. OpenKrush's base idle flat indices are guard tower 16, missile
battery 26, cannon tower 64, machinegun nest 16, grapeshot 32, rotary cannon 80;
their normalized indices are 1, 11, 4, 1, 2, 5 respectively. Changing the
single current state to those bases alone would discard the turret, so tower
composition remains unresolved. Air members 82/83 do not use channel 4 for
idle. OpenKrush contains asset-specific missing-frame patches and presentation
offsets; none were adopted. Retail timing and animation dispatch remain
**unknown**; sequence names are corroboration from that pinned port, not a new
executable trace.

Reproduce with `make kknd-info` and
`env SDL_VIDEODRIVER=dummy make test-kknd`. The runtime sprite regression loads
all 57 catalog sheets, checks distinct textures and cache retention, verifies
Derrick/Wolf frame counts and corrected idle selections, and checks every
generated state's frame bounds. `build/bin/kknd --screenshot /private/tmp/kknd.bmp`
under the same dummy video driver verifies initial building presentation.

## OpenKrush interface and production/research port (2026-09-13)

**Confirmed from the pinned OpenKrush source and PNGs**, with provenance in
`REFERENCES.md`. No original executable was examined in this task; consequently
there are no new retail function addresses or executable behavior claims.
The user explicitly requested OpenKrush's additional gameplay rules.

- `SidebarWidget` and `SidebarButtonWidget` use 48-pixel cells at the right
  edge, with a separate product column. Infantry, vehicles, buildings, towers,
  and walls use the faction's own six-frame `actors/<faction>/sidebar.png`.
  The buttons, palette, icons and frame assets are separate UI images, not
  entries in gameplay `sprnames[]`.
- The embedded palette in individual PNGs is **not the rendered palette**.
  `core/rules/palettes.yaml` selects `core/rules/palette.png`. Keeping palette
  indices and replacing the palette reproduces the authored colors;
  decoding directly into embedded-palette RGBA was a disproven approach.
  Transparent index is 0. The upstream player-color range is 8–13; custom
  building images currently use the base palette, without team translation.
- Infantry uses the new Barracks / Warrior Hall, not Outpost / Clan Hall.
  Outpost / Clan Hall produce structures. Evolved vehicle production is split
  between Blacksmith and Beast Enclosure. The old Drill Rig construction
  queue and Clan Hall infantry queue were incompatible with these rules.
- `games/kknd/products.inc` contains the 55 supported actor products as C
  literals: faction, category, cost, producer, normal-speed build duration,
  tech level, build limit, and label. Research Lab / Alchemy Hall have limit 1,
  including queued buildings. Normal OpenKrush speed is 25 tics per second,
  so authored production ticks convert to milliseconds by multiplying by 40.
  The importer reads only these explicit fields and a checked registry of
  existing actor identities; it is not a general YAML inheritance interpreter.
- `Researchable.NextTechLevel` chooses the next authored level that unlocks
  something for that specific producer. The default mode unlocks all products
  at that level. Rebuilding a destroyed producer starts at level 0. Labs can
  research themselves through level 5. Research is an ordinary state-entry
  action on the lab mobj, with the target's stable object ID, not an array index.
- `Researches.StartResearch` computes cost `250 + 500 * nextLevel * rate / 100`
  and time `400 + 300 * nextLevel * rate / 100`. Lab levels 0–5 have rates
  `{100,90,80,70,60,50}`. Only the per-level term is discounted. At our 30 Hz,
  an integer accumulator executes exactly 25 research steps per second.
  Incremental debits preserve the reference's rounding, including its initial
  zero-cost step. Insufficient cash pauses progress. Repeating the order
  cancels research without refund; a busy lab cannot accept another target.
  Destroyed/captured targets cancel; normal production can run concurrently.
- `ProvidesResearchableRadarInfo` enables the radar/friendly dots at level 1
  and visible enemy dots at level 2. These levels are included in the base's
  next-research calculation, even where no new product uses that level.
  Research fields participate in the network consistency checksum.
- Barracks sheet: 847×192, `FrameSize=121,96`, `FrameAmount=13`, idle frame 3,
  authored sequence offset (-5,0). Warrior Hall: 396×220, `FrameSize=130,110`,
  no FrameAmount; OpenRA's complete-cell rule yields 6 frames, idle frame 3.
  The remaining six columns of the Warrior Hall image are not an extra frame.
  The first loader attempt incorrectly required FrameAmount and rejected this
  valid sheet; the loader now follows OpenRA's metadata default.

Asset SHA-256:

- Barracks: `bea6942782ab61fdf16c9c65d563ddbcfc5a8c5bf9d843c1b4801e817fcca5ff`
- Warrior Hall: `643715dfcaec7a829e56ac2638b3876f2deb52007339701eda65f76691cf0cec`
- Shared palette: `a75ec48dca69a3f5bab08a3064b8c9c370daef861ea2124a197585c97fffc7d3`

Reproduce the committed economy and icons, then verify:

```sh
make build/kknd_rules_import
build/kknd_rules_import reference/OpenKrush games/kknd/products.inc games/kknd/ui
make kknd-info
env SDL_VIDEODRIVER=dummy make test-kknd test-info-gen
```

`test_research` checks exact debits, timing, per-producer unlocks, lab upgrades,
limits, radar, checksum changes, cancellation, destruction and ownership.
`test_research_ui` checks button-to-world targeting and both faction palettes.
`test_runtime_sprites` checks every runtime state frame, including both new
buildings. `test_production` checks AI progression through the tech gates.

**Port boundaries:** this adds OpenKrush production/research rules to the
existing simulation; it is not a complete OpenKrush engine port. Wall placement,
bombing, deconstruction selling, technician repair, auto-research, infinite
queues, full combat balance, mobile-building deployment and construction-stage
presentation remain unported. Corresponding unsupported sidebar actions are
visibly disabled. The new structures use their authored idle images; research
progress is shown for the selected structure. The existing scenario startup,
combat, harvesting and building placement remain engine behavior. The minimap
currently shows known decorations and unit markers, not a terrain thumbnail.

## Uncommanded human tanker movement (2026-09-13)

**Confirmed engine bug**, not retail behavior. Temporary
`OPEN_RTS_DEBUG_TRUCK` logging recorded human owner 0, tanker id 2 at
(10.50,10.00), receiving an AI harvest assignment to vent 5 at (44.00,34.50).
The logged distance-squared was about 210 rather than the actual 1722.5.
`vent->attachment` is already an `fvec2_t`; applying `fixed_to_float` to it
moved every candidate toward the origin during nearest-deposit selection.
Additionally, the AI ran for human owners, so it issued the movement without
player input. This disproves a sprite-facing or southeast movement-vector bug.

The fix skips human players, restricts orders to the AI's own owner, and uses
`fvec2_distance_squared(vent->attachment, position)`. The shared AI fix landed
in concurrent networking commit `89cfc04`; this task adds the specific KKnD
regression. `test_truck_orders` checks 1,800 untouched simulation ticks, a
subsequent explicit movement order, and a synthetic AI tanker whose nearest
vent is far from the origin. Diagnostic logging is removed from the code.

The committed companion `openkrush/{barracks,warriorhall}.yaml` files are the
unchanged upstream sequence definitions. The PNG loader reads their authored
idle Offset, which is shared by all frame sequences in these two sheets;
there is no filename-specific placement adjustment. Frame-count and offset
checks are part of `test_runtime_sprites`.

## Sprite anchors and tanker rotation (2026-09-13)

**Confirmed from native assets and OpenKrush source**, revision
`76c634d05984e48e1e474460c46607aee0bc78a1`. SPRITES.LVL SHA-256 remains
`3e7dbe10624c706afd963e18f54f780052e6ee0b415fc8008add6e37de669ae6`.
No KKND.EXE instructions were examined; this establishes agreement with the
requested OpenKrush reference, not an independently traced retail draw routine.

Evidence chain:

- `MobdFrame.cs` reads X/Y offsets at frame-record bytes +0/+4 and the TRPS
  pointer at +12. `MobdImage.cs` mirrors each decoded row for Gen1 TRPS bit 0.
- `MobdLoader.cs` converts those offsets to a center-relative sprite offset:
  `(width/2 - OffsetX, height/2 - OffsetY)`. The authored anchor describes the
  resulting mirrored image; the loader does not reflect the offset again.
- Our renderer subtracts `spritecell_t.ground_point` from the object position,
  and reflects its X component as `width - ground.x` when flipping pixels.
  Feeding the OpenKrush displacement into that field is **disproven**. It moves
  infantry below/right of their selection markers and makes vehicles swing
  around the object position as dimensions and flip flags change.
- The MOBD loader now stores the native offset directly for unflipped frames,
  or `(width - OffsetX, OffsetY)` for flipped frames. The shared renderer then
  produces `top_left = position - native_offset` for either case. This keeps
  the existing renderer contract and decoded pixels; no per-unit adjustment,
  guessed center, or shared-renderer change is needed. This also follows Doom's
  use of authored sprite offsets (`reference/DOOM/r_things.c`, `R_ProjectSprite`).

Survivor oil tanker (member 73) idle records are at MOBD-segment offsets
`4405007 + 28*n`, for native clockwise facing slots n=0..15:

| Slot | Image size | Native anchor | Flip |
| --- | --- | --- | --- |
| 0 | 29x55 | 14,26 | no |
| 1 | 37x55 | 21,26 | yes |
| 2 | 51x50 | 27,23 | yes |
| 3 | 63x45 | 31,20 | yes |
| 4 | 67x36 | 34,18 | yes |
| 5 | 62x45 | 31,22 | yes |
| 6 | 51x55 | 27,26 | yes |
| 7 | 37x57 | 22,26 | yes |
| 8 | 28x56 | 13,26 | no |
| 9 | 37x57 | 15,26 | no |
| 10 | 51x55 | 24,26 | no |
| 11 | 62x45 | 31,22 | no |
| 12 | 67x36 | 33,18 | no |
| 13 | 63x45 | 32,20 | no |
| 14 | 51x50 | 24,23 | no |
| 15 | 37x55 | 16,26 | no |

Temporary `OPEN_RTS_DEBUG_KKND_ANCHOR` logging showed slot 0 drawn at `(0,-1)`
instead of `(-14,-26)` relative to its object, and slot 4 at `(-68,0)` instead
of `(-34,-18)`. All 9,142 decoded frame instances across catalog members 0–99
now produce the negative authored offset. Before/after comparisons retain the
same 78 accepted members, pixel hashes, image sizes, native offsets, flip flags,
lump counts, and logical-frame counts. Ground-point metadata is intentionally
corrected. Diagnostic logging was removed after verification.

**Confirmed companion PNG bug:** the Barracks and Warrior Hall loader left
ground points at zero and used the sequence offset as displacement. These
centered sheets require `ground_point = frame_size/2 - sequence_offset`:
Barracks `(65,48)` for 121x96 with offset (-5,0), Warrior Hall `(65,55)` for
130x110 with offset (0,0). The loader now encodes the entire placement in that
anchor; no separate displacement remains. Sequence metadata and pixels are
unchanged.

`test_runtime_sprites` compares actual world rendering against direct placement
using the native table above for all 16 tanker facings, plus both PNG buildings.
It failed on tanker slot 0 before the fix. Loader fixtures check reflected and
unreflected anchors (including an anchor outside the image), untouched pixels,
malformed spans, and cleanup. Reproduce with:

```sh
make
env SDL_VIDEODRIVER=dummy make test-kknd test-loader-kknd
env SDL_VIDEODRIVER=dummy build/bin/kknd --screenshot /private/tmp/kknd-anchors.bmp
```

Verification passed: full build, KKND suite and loader fixtures, and headless
checks for KKND, Dark Colony, and Dark Reign. The corrected KKND screenshot was
visually inspected; Dark Colony and Dark Reign screenshots are byte-identical
to their before-fix baselines. Tags were regenerated.

## Selection health bars and native UI search (2026-09-13)

**Confirmed engine behavior:** the removed circle was drawn by the shared
renderer, explicitly selected by KKND's `SELECTION_STYLE_CIRCLE` configuration.
It was not a native KKND sprite. The user's supplied original-game screenshot
shows framed health bars above selected units, without ground circles.

**Confirmed OpenKrush implementation:** at the pinned revision above,
`Mechanics/Ui/Traits/AdvancedSelectionDecorations.cs` selects the status-bar
overlay and emits no additional selection outline. Its companion
`Mechanics/Ui/Graphics/StatusBar.cs` draws rectangles, not an image asset:
outer RGB (206,206,206), inset (16,16,16), green fill rows (0,255,0) and
(0,181,0). The empty lower fill is (49,49,49); the empty upper row is gray.
The health-only small variant is six pixels high, with a two-pixel inset;
the big variant is seven high. `mods/openkrush/rules/core.yaml` specifies
widths 16 for infantry, 32 for vehicles/towers, and 64 for buildings.

**Native asset search, bounded result:** a temporary C diagnostic walked each
animation's frame pointers and decoded its images using `decode_mobd_image`.
The following offsets are relative to the DATA segment (file offset +8), in
the same SHA-256 SPRITES.LVL documented above:

| Member | Name | Member start | Pointer table | First frame | Frame references |
| --- | --- | --- | --- | --- | --- |
| 10 | Buttons | 559712 | 561932 | 562672 | 185 |
| 17 | Cursors | 1548487 | 1549371 | 1549547 | 155 |
| 22 | Extras | 1741072 | 1743256 | 1743704 | 392 |
| 30 | Gui | 2377068 | 2377776 | 2377980 | 59 |

Buttons, Cursors, and Gui contact sheets were visually inspected. No matching
framed health-bar image was found there. Buttons frame references 167–170 are
32x8 colored strips, but lack the screenshot's light-gray enclosing frame;
reference 169 was also inspected enlarged. Cursors includes 82x18 segmented
meters, which likewise do not match. Gui contains interface panels and menu
labels. OpenKrush's Extras sequence definitions identify weapon effects,
explosions, craters, and death sequences. These observations do **not** prove
that no other native resource or executable drawing path exists; the original
executable's health-bar implementation remains **unknown**.

The gameplay loader rejects members 10, 17, and 30 because their pointer-table
spans (740, 176, and 204 bytes) are not multiples of a sixteen-facing channel.
That restriction must not be treated as absence of images. The diagnostic
enumerated the animation frame lists directly, without synthesizing gameplay
channels or registering UI images in `sprnames`. No runtime loader change was
needed for the procedural bar. To reproduce the header evidence on this
little-endian host, the first Buttons animation has timing 268435456, frame
offset 562672, and terminator 0:

```sh
od -An -tu4 -j 559720 -N 12 data/KKND/LEVELS/640/SPRITES.LVL
od -An -tu4 -j 562680 -N 28 data/KKND/LEVELS/640/SPRITES.LVL
```

**Implementation consequence:** KKND now overrides the engine overlay with a
procedural health bar using the confirmed OpenKrush border/fill geometry and
category widths. Selection alone enables it, including full health; dead and
unselected objects have no selection bar. The fill remains green and shrinks
with HP. Bars use the current sprite's screen bounds, as requested for this
engine's presentation; OpenRA's separate mouse bounds, YAML placement offsets,
damage-state colors, and extra oil/research/veterancy rows are not ported or
claimed as reproduced retail behavior. Existing product categories supply the
widths without authoring another per-unit mapping.

The engine default is now a green sprite-bounds rectangle plus a health bar;
7th Legion uses that default. Games can replace both through `draw_overlays`,
whose context now includes the sprite bounds. Dark Colony's native sprite
overlay and Dark Reign's configured brackets remain their game overrides.
Full/reduced-health previews and a selected KKND scene were inspected headlessly.

Verification: full build and generated-file consistency checks, all four game
test suites, all four headless smoke checks, and regenerated tags. Three
existing Dark Colony synthetic rendering fixtures omitted their FIN coordinate
mode; that omission was corrected so they exercise the intended renderer path.

## Opening combat: layered fire, retaliation and deaths (2026-09-13)

**Confirmed engine omissions:** a temporary `OPEN_RTS_DEBUG_COMBAT` trace of
SURV_01 recorded Berserker id 15, type 9, HP 80, damage 10, target id 0,
range 2.50, and death state 0. Riflemen stop within their range of 4, outside
the Berserker's range of 2.5. Damage did not record the shooter; generic AI
base defense requires a base, and this native opening force has none. Every
KKND death entry was S_NULL. The old combat test stopped at the first HP
reduction, so it missed both retaliation and presentation failures.

The fix is shared engine behavior, as requested: `P_DamageMobj` credits the
shooter (the originator for missile impacts), makes an idle combatant acquire
that enemy, and preserves an existing live hostile target. Shared thinker
movement pursues visible targets into the unit's data-defined range and
updates its path when a target changes cells. `A_Look` uses the same range,
visibility and target validation as moving combatants. Attack states stop
movement while their animation plays. There is no KKND AI callback, base
requirement, mission-specific trigger, or new balance value. This follows
Doom's `P_DamageMobj` source/target wake-up rule in `reference/DOOM/p_inter.c`;
flow-field movement and visibility remain this engine's existing RTS rules.

**Confirmed native assets and pinned reference sequences:** SPRITES.LVL has
SHA-256 `3e7dbe10624c706afd963e18f54f780052e6ee0b415fc8008add6e37de669ae6`.
A temporary C inspector followed the native animation lists, seven-channel
pointer tables, frame records, and point lists. Offsets below are relative
to the DATA segment (add 8 for file offsets). OpenKrush revision
`76c634d05984e48e1e474460c46607aee0bc78a1` supplies sequence names/timing;
links are in REFERENCES.md. No DOS executable instructions were traced.
`file` identifies the installed KKND.EXE as DOS/LE with embedded DOS4GW;
its SHA-256 is `92cc8440992620e91de596f266b6dd23e58650efd85ccdb1521ffab766393e21`.

| Sprite member | Member offset | Channel table | First frame record |
| --- | ---: | ---: | ---: |
| Extras (22) | 1741072 | 1743256 | 1743704 |
| Rifleman (34) | 2733128 | 2734216 | 2734664 |
| Pickup (54) | 3699753 | 3700393 | 3700841 |
| Monster Truck (47) | 3352575 | 3353215 | 3353663 |
| Dire Wolf (19) | 1661004 | 1662396 | 1662844 |

Extras channels 0 and 1 each contain sixteen two-frame sequences: flat starts
0 and 32, logical starts 0 and 2. Channel 4 contains sixteen eight-frame
sequences, flat start 64/logical start 4. Unreferenced simple animations
follow at flat 192/logical 12. Therefore:

| Presentation | Simple animation offset | Flat start | Logical start | Frames |
| --- | ---: | ---: | ---: | ---: |
| Medium vehicle explosion | 1741216 | 224 | 44 | 13 |
| Survivor infantry death | 1741640 | 308 | 128 | 15 |
| Evolved infantry death | 1741708 | 323 | 143 | 15 |

OpenKrush's `die` for Dire Wolf uses its own logical frames 2–14 (13 frames).
Death boundaries use its 120 ms per frame, rounded cumulatively to engine
30 Hz tics: 54 tics for infantry, 47 for the Wolf and vehicle explosion.
The complete sequences run on the dying mobj and end at S_NULL; there is no
separate effect/corpse lifetime. All infantry share their faction's death
sequence; the opening bikes and Pickup use the medium explosion. Other
vehicle-specific deaths are not newly inferred from this investigation.

**Layer composition, explicitly requested engine presentation:** preserve
every native frame, then append two complete firing poses to each supported
gun sprite. These share the first shooting body image with the two Extras
muzzle frames, using ordinary `spritelayer_t` layers and existing renderer
ownership. Native frame counts remain 11 (Rifleman), 4 (Bike), 53 (Wolf), and
4 (Pickup); runtime definitions add two composed frames. The state table
splits the old four-tic first attack pose into two two-tic states. Only the
first calls A_Attack, so damage and cooldown do not double. Later body attack
frames stay unchanged. Rifleman/Saboteur/Sniper/Shotgunner/Vandal/Crazy Harry
use the infantry flash; Bike/Wolf/Pickup use the vehicle flash. Berserker
throws a projectile and has no gun-muzzle layer in the reference. Porting its
projectile flight is outside these presentation changes.

Frame byte +24 points to optional 16-byte records `{id, x, y, z}` terminated
by id -1. Point 0 supplies the weapon or turret location. Signed x/y are
converted by arithmetic shift 8, exactly as `MobdPoint.cs`; absent points
mean no additional displacement, as in `OffsetsArmament.cs`. Sprite flip
flags do not reflect these already-authored points again. With native body
anchor B, point P, and flash anchor F, the flash's offset relative to our
body canvas is `B + P - F`. For a turret, P is the sum of body point 0 and
turret point 0. This is an anchor conversion, not a tuned visual offset.

For example, Rifleman north shooting frame 2736120 has point list 2740760:
point 0 is (3,-21), after point 2. Pickup north idle frame 3700841 has point
list 3702221, with point 0 (0,3). The actor's own channel-2 turret frames
3701737 onward have no point lists; using them produced a malformed north
pose and put flashes at the mount. That attempted composition is
**disproven as the intended turret selection** and is not retained.

**Confirmed decompiled reference lookup, corroborated by OpenKrush:** pinned
OpenKKnD revision `3702e29992d0abf5e3b0648a57858fd578afc991`,
`src/_unsorted_data.cpp`, gives `turret_4x4Pickup` the member
`MOBD_MUTE_MONSTER_TRUCK` (0x2f = 47), and the Pickup UnitStat references that
attachment. The companion handler is labeled `UNIT_AttachHandler_Turret`
at 0x4479d0 in this Extreme-version reconstruction. These addresses are
**not** mapped to the installed DOS executable. OpenKrush's Pickup sequence
independently selects MonsterTruck.mobd and comments on the broken native
Pickup north frame. Reusing member 47 is thus a documented table lookup,
not a guessed sprite-name alias. Applying that Extreme lookup to the DOS
assets is **corroborated/inferred**, not a new DOS instruction trace.

In the installed member 47, channel 1 is sixteen single-frame turret poses,
logical frame 0. Its north frame 3354559 points to 3356231 with point 0
(0,-14); east is (13,-5), south (0,5), west (-13,-5). Pickup bodies retain
their own point lists. Idle/movement and both fire poses compose the selected
turret, sharing the cached member-47 images; native Pickup channel 2 is still
preserved individually. The current presentation keeps body and turret
facing together. Independent turret aiming and full retail firing dispatch
remain unported, as do exact DOS timing-word semantics.

Verification:

```sh
make kknd-info
make
env SDL_VIDEODRIVER=dummy make test
# Optional eight-facing contact sheet of both flash phases and native deaths:
env SDL_VIDEODRIVER=dummy OPEN_RTS_TEST_COMBAT_BMP=/private/tmp/kknd-combat.bmp \
  build/bin/tests/kknd/test_combat_rendering
env SDL_VIDEODRIVER=dummy build/bin/kknd --check
env SDL_VIDEODRIVER=dummy build/bin/kknd --screenshot /private/tmp/kknd-opening.bmp
```

`test_combat` drives one native opening Rifleman into the first encounter,
requires enemy advance and return fire, then requires a visible death state.
It separately checks every frame and exact total duration for all five opening
unit types. `test_combat_rendering` compares actual world rendering with
direct native-anchor placement for both muzzle phases in all sixteen facings;
point arrays preserve the inspected values, and checks require nonempty pixels.
The muzzle and death contact sheet was visually inspected. The existing anchor
test's camera was corrected to place its test objects on screen; previously
its pixel comparisons could compare two empty offscreen renders. Shared
retaliation tests run against all four game data sets, without a mission or
base, and cover pursuit, return fire, moving targets, allies and removal.
Temporary diagnostic logging was removed.

Final checks passed: full build, `make test` (including all four game suites,
shared AI tests, generated-file consistency, layout, command and loader tests),
and all four headless smoke checks. The raw MOBD catalog was also compared
against a catalog binary built from the pre-change `w_spr.c`: all 100 member
candidates, including the same 78 accepted members, have identical hashes for
pixels, native frame definitions, flips and anchors. The intentional runtime
changes are the appended layered firing poses and Pickup turret composition.
The native parser refactoring does not change raw asset decoding. Tags were
regenerated, and the default mission screenshot was visually inspected.
The network regression also passed all seven modes, including packet loss,
setup mismatch, desync detection, peer quit and the model path. Its UDP bind
requires running outside the filesystem/network sandbox; the sandboxed attempt
failed at bind before the network scenarios could run.
