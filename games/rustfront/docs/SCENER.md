# Making Rustfront unit art with Scener

Rustfront's buildings and vehicles are modelled as Scener XML and rendered as
sprites. Scener is a small OpenGL scene renderer from the Orion UI repository
(`~/Developer/mapview/ui/apps/scener`). This guide covers how the kit is laid
out, what conventions it follows, and how to add a unit.

Scener's own docs are the reference for every tag and flag:

- `apps/scener/CLI.md` lists the commands (`--check`, `--query`, `--render`,
  `--prefab-info`, `--transparent`, `--supersample`).
- `apps/scener/skills/populate-simplegl-scenes/references/scene-format.md`
  describes every element, attribute and modifier.

The sprite conventions follow nile-archive, which already renders its city
buildings with Scener (`docs/BUILDINGS.md`, `docs/BUILDING_ART.md`,
`tools/scener_buildings.py`).

## Install and run

`scener` must be on `PATH`. It is deployed to `~/.local/bin/scener` with:

```sh
cd ~/Developer/mapview/ui
make build/bin/scener
python3 apps/scener/deploy.py --prefix "$HOME/.local"
```

Render every preview into `art/previews/`:

```sh
games/rustfront/art/render.sh
```

Render only some cameras:

```sh
games/rustfront/art/render.sh BattleTank Refinery
```

The script runs `xmllint` on every file, then `scener --check`, then
`--render`. Rendering needs a GL context. On macOS it runs headless from a
terminal and takes about 3 seconds for the whole kit.

## Layout

```
art/layout.py           writes units.blks: palette, lights, one camera per unit, every instance
art/units.blks          GENERATED scene; edit layout.py, never this file
art/utilities.xml       copy of Scener's utilities (soft-box, slab, beam, tube, column)
art/kit.xml             Rustfront's detail kit: bunker, railing, ladder, vent, warn-light,
                        floodlight, sandbags
art/prefabs/buildings/  one .blk per building (turret/ holds the gun turret's base and head)
art/prefabs/vehicles/   one .blk per vehicle (tank/ and apc/ hold hull and turret layers)
art/prefabs/infantry/   Scener CAT characters: rifleman, rocket soldier
art/render.sh           check + render the previews
art/pack8.py            converts sprites/ to 256-colour assets/ for the engine
art/previews/           committed one-angle previews (camera name = file name) and
                        sprites_contact.png
art/sprites.py          renders the game sprites into ../sprites/ (see Game sprites)
art/contact.py          composites those sprites as the game would, as a check
art/.build/             sprites.py's scratch scenes and renders (ignored by git)
sprites/                packed sprite sheets and sprites.json
```

Each unit is one prefab, and the scene only places them. A prefab takes its
materials from the scene, so the whole kit shares one palette. Prefabs resolve
relative to the scene's directory (`source="vehicles/tank"` loads
`prefabs/vehicles/tank.blk`). Each prefab pulls in both libraries with
`<include src="../../utilities.xml"/>` and `<include src="../../kit.xml"/>`.

## Conventions

| Thing | Rule |
|---|---|
| Units | Centimetres, degrees, Y up (Scener's default; the kit does not use `up="z"`) |
| Grid | One cell is 200 cm. Buildings fill whole cells: turret 1×1, power/barracks 2×2, refinery 3×2, HQ/factory 3×3 |
| Origin | Footprint centre, on the ground (y = 0). Buildings stand on a 20 cm pad, so their geometry starts at y = 20 |
| Facing | Buildings face **+Z** (entrance, garage door). Vehicles face **+X** (gun, nose); the scene turns them `rot="0 -90 0"` so they face the viewer's lower left |
| Leading comment | States footprint, origin and front. The root carries the same as data: `<prefab up="y" front="+z">` |
| Team colour | Parts marked `tint="1"` (bands, canopies, turret tops, flags) take the instance's `color`. Everything else keeps its material |
| Palette | The materials in `layout.py`'s `PALETTE`: `sand`, `pad`, `concrete`, `concrete_dark`, `gunmetal`, `roofing`, `corrugated`, `steel`, `steel_dark`, `grating`, `rust`, `armor`, `armor_dark`, `team`, `track`, `rubber`, `sandbag`, `glass`, `ore`, `hazard`, and the emitters `glow`, `lamp`, `warn`, `flame`. Add a material there instead of inline colours |
| Shape style | See [Art direction](#art-direction) |

## Art direction

The reference is the late-90s pre-rendered RTS (Dark Reign, the original
Command & Conquer). Take the general feel from them; never copy a design.

- **Angular and heavy.** Concrete walls lean in: use `<bunker>`, a box with a
  taper. Vehicle hulls are faceted side profiles with sides leaning in. Turrets
  are top-view wedges, swept up and tapered (`<shape … rot="90 0 0">` with a
  `<taper axis="z">`). Keep bevels small (1–2 cm). Soft, rounded boxes read as
  toys.
- **Gritty palette.** Dusty sand, battered concrete with noise and a dark base
  gradient, gunmetal roofs with panel or corrugation lines, desert-tan armour.
  The only saturated colours are team blue, hazard stripes, warning lights and
  ore.
- **Mechanical density.** Every building gets the kit's small parts: railings
  on catwalks, ladders on tall things, vents on roofs, warning lights on
  corners and doors, floodlights on the pad, pipes between buildings, and hazard
  frames on doors and aprons. Vehicles get headlights, antennas, hatches,
  stowage, smoke dischargers and exhausts.
- **Team colour as paint.** Use parapets, roof panels, ribs, stripes and flags,
  all `tint="1"`. Never colour a whole mass with the team colour.
- **Readable at sprite size.** Each unit has one dominant silhouette (towers,
  a long gun, a hopper of ore). Check it in the `Lineup` render, not only in
  close-up.

## Camera and scale

All sprites share one view, so the art is a registered set:

- **Projection:** orthographic, the classic C&C / Dark Reign view. The camera
  looks due north (yaw 0, from +Z toward −Z) at elevation asin(2/3) ≈ 41.8°.
  A building shows its +Z front and its roof. The open-rts engine only draws
  axis-aligned rectangular tile grids (no isometric diamonds), and with this
  view a 200 cm cell is exactly a 48×32 px rectangle at game scale, so the art
  registers with the map. The constants are `TILE_W`, `TILE_H`, `ELEV` and
  `YAW` in `layout.py`.
- **Scale:** previews are 1.5 cm per pixel, for review. Game sprites are
  `SPRITE_CM_PER_PX` = 200/48 ≈ 4.17 cm per pixel: the tank is about 60 px
  long, and a 3×3 building is 144 px wide. Each camera sets `size="W H"` and
  `ortho="scale × H"`, so the scale is the same whatever the frame size.
- **Preview frames:** 3×3 buildings use 640×608, 3×2 use 640×544, 2×2 use
  448×448, the turret 256×256, and vehicles 384×320. `sprites.py` scales the
  same frames to game pixels.
- **Placement:** units stand 1100 cm apart along the camera's right vector
  (buildings on one row, vehicles on a second row 1700 cm nearer the camera),
  so no camera frames a neighbour. The `Lineup` camera shows them all.

`layout.py` computes every camera from these tables, so a new view angle, scale
or unit means editing its constants and running it again; `render.sh` does that
first.

## Lighting

- **Key sun:** `dir="0.85 -1.25 -0.55"`, warm, intensity 1.25, high from the
  west-south-west. Roofs are bright, fronts (+Z) get a little light, and hard
  shadows fall to the screen's right.
- **Shade is dark and keeps its colour.** The fill is a weak *warm* shadowless
  sun from the opposite side (intensity 0.14), and the ambient is a low warm
  0.17 0.15 0.13, which multiplies each material's own hue. A cool blue fill
  or strong sky reflection washes shaded faces out to grey-blue; don't use one.
  `skyReflection` stays at 0.6. Scener's skill says to keep ambient at or below
  about 0.35: higher ambient flattens the cast shadows.
- All of this lives in `layout.py` (`SCENE_ATTRS`, `KEY_SUN`, `LIGHTS`), and
  `sprites.py` uses the same constants.
- **Emitters:** lamps, glow strips and flames are shapes with
  `unlit="1" castShadow="0"`. `unlit` is a shape attribute. On a `<material>`
  the checker reports it as ignored.

## Primitive notes (learned the hard way)

- **`<soft-box>`** (from utilities) rounds the outline in **XY** and extrudes
  along Z. `radius` therefore rounds the *side profile*, which gives vehicle
  track pods their rounded ends.
- **`<shape closed="1" v="x y …">`** with an `<extrude>` gives a polygon in XY
  extruded along local Z, centred. Vehicle hulls are a side profile (X = length,
  Y = height) extruded across the width.
- **`<arch>`** is **centred on its `pos`**, like a box, not based at it. A hut
  of height 150 on the pad needs `pos` y = 20 + 75.
- **Cylinders, cones, prisms and capsules** run along local Y. Use
  `rot="90 0 0"` for an axle along Z (wheels) and `rot="0 0 -90"` for a barrel
  along +X.
- **Rotation order** is X, then Y, then Z. A positive Y rotation turns +X
  toward −Z.
- **`<array count translation>`** repeats a mesh in its own local frame,
  *before* the shape's `rot`. Use it for wheels, ribs and stripes.
- **Lathe profile ids are global** across the scene and every prefab, and the
  first definition wins. Prefix them (`rf_cooling_tower`).
- **Stripes** come from a gizmo, not from vertex colour. `hazard` is a material
  with `<gradient type="line" start="0 0 0" end="7 7 0" repeat="1" edge="0.5">`,
  which draws crisp diagonal bands on anything that uses it. `corrugated` uses
  the same gizmo along local Z with `mirror="1"` for roof ribs, so give it to
  shapes extruded along Z. Panel joints (`pad`, `roofing`) and track links
  (`track`) are `<map type="bricks">`.
- **A `<taper>` on an extruded profile** with `axis="y" effect="z"` and
  `center="0 BASE 0"` leans a hull's sides in from its base line.
- **Defines** take `${param}` only. Use a `<let>` or `{expr}` inside the body for
  arithmetic, as `kit.xml` does for array counts.
- **Faceted lumps** (ore, rocks): use a `<sphere>` with low `rings` and
  `slices`, scaled flat. The `<noise>` modifier can roughen it further.

## Adding a unit

1. Copy the closest prefab in `prefabs/buildings/` or `prefabs/vehicles/`.
   Keep the leading comment, the `<prefab up front>` root and the includes.
2. Block out the silhouette first: pad, main masses, roof or turret. Details
   come after. Mark team-colour parts `tint="1"`.
3. Add a row to `BUILDINGS` or `VEHICLES` in `layout.py`: prefab, camera name,
   frame size and look height (about half the unit's height).
4. Run `art/render.sh NewCamera` and look at the PNG. Then look at `Lineup`
   to judge the unit next to the others.

For close-ups, copy `units.blks` into a scratch folder beside symlinks to
`prefabs/`, `kit.xml` and `utilities.xml`. Multiply each camera's `size` and
keep its `ortho`: the same framing then renders at 2–3× the resolution.

When a unit misbehaves, query its geometry:

```sh
cd games/rustfront/art
scener --prefab-info prefabs/vehicles/tank.blk   # every node with world bounds
scener --query units.blks --camera BattleTank    # whole scene as JSON
scener --check units.blks --json                 # every issue with file and line
```

## Validation

`scener --check` must report **0 errors**. It also warns about the following:

- **Coplanar faces:** two faces on one plane will z-fight. Fix any that are
  visible. The kit has about forty that are hidden: parts whose bottoms rest
  on the same deck or pad, paint panels laid on a face, and parts of one
  material that meet, such as the factory's hazard door frame.
- **Floating:** a part that does not touch its support.
- **Penetration over 1 cm:** usually a mis-centred primitive. The barracks
  arch sank 55 cm into the ground before its `pos` was corrected.

Treat `--check` as a gate, not a reviewer: always look at the render.

## Game sprites

```sh
python3 games/rustfront/art/sprites.py          # every unit, about 6 s
python3 games/rustfront/art/sprites.py tank hq  # just these
python3 games/rustfront/art/contact.py          # composite check -> previews/sprites_contact.png
```

`sprites.py` writes three scenes into `art/.build/` and renders each with
`--supersample 4`, at game scale (`SPRITE_CM_PER_PX`). All of them use the
preview's view and lights.

| Pass | Scene | Gives |
|---|---|---|
| colour | the unit with team colour black, `--transparent` | `<unit>_<layer>.png` |
| team | the same with team colour white | `<unit>_<layer>_team.png` = white − black, a greyscale mask |
| shadow | the assembled unit inside `<group renderable="0">` (hidden, still casting) over a white catcher, key sun only, no ambient | `<unit>_shadow.png`, black with alpha = darkness × 0.6 |

The game draws a pixel as `base + team_colour × mask / 255` per sRGB channel.
That is an approximation (lighting is linear), but it reads right; see the red
and blue rows of the contact sheet.

**Facings.** Vehicles and turret heads have 16 facings, and buildings have
one. Facing *i* is a turn of *i* × 360/16 degrees about +Y, counter-clockwise
seen from above, starting from the prefab's front (+X for vehicles and heads).
Each facing is a camera with a `<transform target rot>` override on a single
instance, so the light stays fixed while the unit turns.

**Layers.** The tank, the APC and the gun turret have a separately rotating top.
`prefabs/vehicles/tank.blk` assembles `tank/hull` with `tank/turret` at its
mount. The sprites render each part alone: the hull about its ground origin,
the turret about its axis at mount height. For every hull facing,
`sprites.json` gives `mount_px`, the pixel offset of the turret axis from the
hull pivot. Draw the turret frame with its pivot at `hull pivot + mount_px[hull
facing]`. The shadow is of the assembled unit with the turret in line with the
hull.

**Sheets.** Each layer's frames are cropped to their common bounds and laid out
in a grid (`columns`), so every frame of a sheet has one size and one `pivot`.
The pivot is the anchor's screen position in the cropped frame. Each camera
puts it on a pixel corner, at `(W/2, H/2 + k)`.

```json
"tank": {"facings": 16,
  "layers": {"hull":   {"file": "tank_hull.png", "team": "tank_hull_team.png",
                        "frame": [190, 154], "columns": 4, "count": 16, "pivot": [..]},
             "turret": {"file": "tank_turret.png", ..., "mount_px": [[dx, dy], ... 16]}},
  "shadow": {"file": "tank_shadow.png", "frame": [..], "pivot": [..], ...}}
```

Draw in this order: every unit's shadow (under all units), then each layer, in
the order listed.

## Infantry

Soldiers are Scener characters, not hard-surface models. `prefabs/infantry/rifleman.blk`
and `rocket.blk` are copies of Scener's CAT biped preset
(`apps/scener/prefabs/characters/presets/biped.blk`). Each is costumed with desert
fatigues, a team-colour vest and shoulder pads (`tint="1"`), a helmet, a pack and
boots. The weapon is a rigid bone named `weapon` grown from the ribcage, so the
mirrored arms don't each get one. The hands hold it through limb IK. The offsets
come from `scener --list-joints`, measured from the rest wrists, and each prefab's
pose comments record them. A character prefab is Z up and faces −Y, so the sprite
scene stands it up with `rot="-90 90 0"` (Y up, facing +X).

| Animation | Source | Frames |
|---|---|---|
| idle | the `Ready` pose | 1 |
| walk | `<gait mode="spot">` with `Ready` masked onto the arms and weapon; one measured stride of 1.10 s from t = 1.20 | 6 |
| fire | the `Fire` clip: `Aim`, then `Recoil` (stepped) | 2 |
| die | the `Die` clip: `Aim` → `Hit` → `Falling` → `Dead`, falling flat on the back | 6 |

Infantry have 8 facings. `sprites.py` renders each animation frame with
`--time`, at every facing, through the colour, team and shadow passes. A sheet
holds every animation, and `sprites.json` / `rf_anims` give each one's `start`
and `frames`. The frame index is `start + facing × frames + frame`. To add a
soldier, copy a prefab, change its costume, weapon and hand offsets, and add a
row to `INFANTRY` in `sprites.py`.

Pose rules learned here:

- **Limb IK offsets are from the standing rest pose,** in the body frame. Once
  the pelvis drops (in a fall), they point at thin air. Pose falling limbs with
  joint `aim`s instead, which follow the parent.
- **Measure the gait.** Sample foot heights with `--list-joints` over time, and
  take frames from one steady stride, after the starting steps.

Scener can also play Ecstatica II's decoded actions (`apps/scener/imports/ecstatica`)
and CMU or Mixamo motion capture through `<layer mocap>`. The shipped soldiers use
only Scener's own gait and authored clips. Ecstatica's actors and actions come from
the commercial game and are fine for reference and prototyping, but not for art
this game ships. CMU captures are free to use.

## 8-bit data for the engine

The engine draws everything into one 8-bit indexed framebuffer, so the game
ships 256-colour art:

```sh
python3 games/rustfront/art/pack8.py    # sprites/ -> assets/*.bmp + sprites.inc, under a second
```

All BMPs share one palette (`art/previews/palette.png` shows it):

| Indices | Use |
|---|---|
| 0 | transparent |
| 1 | shadow: shadow sheets are only 0 and 1, and the engine darkens what lies under 1 through a lookup table |
| 2–7 | fixed emitter colours (warning red, glow cyan, lamp, flame, white, black). Median cut loses small lights, so these stay exact |
| 8–15 | reserved for the HUD |
| 16–79 | four team ramps of 16 shades (blue, red, green, yellow). Art is drawn with the blue ramp; a team palette map moves 16–31 onto another ramp |
| 80–255 | 176 colours, median cut over every non-team sprite pixel and the terrain |

A pixel is team-coloured where the team mask is at least 48, and its ramp
shade is the mask level. A pixel under half coverage becomes transparent,
because indexed art has no partial alpha. `assets/terrain.bmp` is a 48×32 tile
atlas in the same palette: 8 sand tiles, 4 ore-field tiles and 4 gravel tiles.
Its noise wraps at the tile edge, so the tiles join without seams.
`sprites.inc` is the C table the game compiles in: per sheet, the frame size,
columns, count, pivot and turret mounts in whole pixels.

Not done yet:

- **Animation** (firing, harvesting, construction) would be more `--frames` or
  transform cameras in the same way.
- **The turret shadow** follows the hull. Exact turret shadows would need a
  shadow per (hull, turret) pair, or a separate turret shadow layer.
