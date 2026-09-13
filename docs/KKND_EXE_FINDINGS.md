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
