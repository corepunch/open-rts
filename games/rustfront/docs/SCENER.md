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
art/units.blks          the scene: materials, lights, ground, one camera per unit, every instance
art/utilities.xml       copy of Scener's utilities (soft-box, slab, beam, tube, column)
art/prefabs/buildings/  one .blk per building
art/prefabs/vehicles/   one .blk per vehicle
art/render.sh           check + render
art/previews/           committed one-angle previews (camera name = file name)
```

Each unit is one prefab, and the scene only places them. A prefab takes its
materials from the scene, so the whole kit shares one palette. Prefabs resolve
relative to the scene's directory (`source="vehicles/tank"` loads
`prefabs/vehicles/tank.blk`). Each prefab pulls in the utilities with
`<include src="../../utilities.xml"/>`.

## Conventions

| Thing | Rule |
|---|---|
| Units | Centimetres, degrees, Y up (Scener's default; the kit does not use `up="z"`) |
| Grid | One cell is 200 cm. Buildings fill whole cells: turret 1×1, power/barracks 2×2, refinery 3×2, HQ/factory 3×3 |
| Origin | Footprint centre, on the ground (y = 0). Buildings stand on a 20 cm pad, so their geometry starts at y = 20 |
| Facing | Buildings face **+Z** (entrance, garage door). Vehicles face **+X** (gun, nose); the scene turns them `rot="0 -90 0"` so they face the viewer's lower left |
| Leading comment | States footprint, origin and front. The root carries the same as data: `<prefab up="y" front="+z">` |
| Team colour | Parts marked `tint="1"` (bands, canopies, turret tops, flags) take the instance's `color`. Everything else keeps its material |
| Palette | Use the materials in `units.blks`: `pad`, `concrete`, `steel`, `armor`, `team`, `hazard`, `glass`, `rubber`, `track`, `ore`, and the emitters `glow`, `lamp`, `flame`. Add a material there instead of inline colours |
| Shape style | A few big, soft shapes. A unit should read by its silhouette and two or three colours at sprite size, with no part smaller than about a tenth of a cell |

## Camera and scale

All sprites share one view, so the art is a registered set:

- **Projection:** orthographic, yaw 45°, elevation 35°. The camera looks from
  +X+Z, so a building shows its +Z front on the left face and its +X side on
  the right face.
- **Scale:** 1.5 cm per pixel. Each camera sets `size="W H"` and
  `ortho="1.5 × H"`, so the scale is the same whatever the frame size.
- **Frames:** 3×3 buildings use 640×576, 3×2 use 640×512, 2×2 use 448×448, the
  turret 256×256, and vehicles 320×256.
- **Placement:** units stand 1000 cm apart along the camera's right vector
  (buildings on one row, vehicles on a second row 1600 cm nearer the camera),
  so no camera frames a neighbour. The `Lineup` camera shows them all.

The camera positions in `units.blks` are `look + 4000 × (sin45·cos35, sin35,
cos45·cos35)`. If the view changes, recompute them all; the numbers are
written out, not expressions.

## Lighting

- **Key sun:** `dir="0.99 -1.4 -0.42"`, about 50° down. Light comes from the
  screen's upper left, so fronts (+Z) are lit and shadows fall to the right.
- **Fill sun:** a weak, shadowless sun from the opposite side, so the +X faces
  are not black.
- **Ambient:** 0.30. Scener's skill says to keep ambient at or below about
  0.35: higher ambient flattens the cast shadows.
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
  with `<gradient type="line" start="0 0 0" end="9 9 0" repeat="1" edge="0.5">`,
  which draws crisp diagonal bands on anything that uses it.
- **Faceted lumps** (ore, rocks): use a `<sphere>` with low `rings` and
  `slices`, scaled flat. The `<noise>` modifier can roughen it further.

## Adding a unit

1. Copy the closest prefab in `prefabs/buildings/` or `prefabs/vehicles/`.
   Keep the leading comment, the `<prefab up front>` root and the include.
2. Block out the silhouette first: pad, main masses, roof or turret. Details
   come after. Mark team-colour parts `tint="1"`.
3. In `units.blks`, add the `<prefab>` instance on the next free slot of its row,
   and add an ortho camera whose `look` is the unit's position at about half
   its height. Use the same 1.5 cm/px rule for `ortho` and `size`.
4. Run `art/render.sh NewCamera` and look at the PNG. Then look at `Lineup`
   to judge the unit next to the others.

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
  visible. The kit still has a few that are hidden, for example parts whose
  bottoms rest on the same pad top, and parts of one material that meet, such
  as the factory's hazard door frame.
- **Floating:** a part that does not touch its support.
- **Penetration over 1 cm:** usually a mis-centred primitive. The barracks
  arch sank 55 cm into the ground before its `pos` was corrected.

Treat `--check` as a gate, not a reviewer: always look at the render.

## Toward game sprites (not done yet)

The previews are single-angle and are drawn on a sand ground. For game sprites,
follow nile-archive's pipeline:

- **Sprites:** render each unit alone with `--transparent` and
  `--supersample 3`, with the ground hidden or removed. Keep the per-camera
  `size` and `ortho`, so the pivot stays on a whole pixel.
- **Shadows:** a second pass draws the same groups with `renderable="0"` (they
  still cast shadows) onto a white catcher, lit by the key sun only. The engine
  draws that shadow layer under the sprite.
- **Facings:** vehicles need 8 or 16 headings. Add one camera per heading, or
  one camera with `<transform target="tank" rot="0 θ 0"/>` overrides. A
  generator script should write these instead of hand-editing XML.
- **Turrets:** the tank, APC and gun turret turrets should become their own
  prefabs, so the turret can turn apart from the hull.
