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
art/prefabs/buildings/  one .blk per building
art/prefabs/vehicles/   one .blk per vehicle
art/render.sh           check + render
art/previews/           committed one-angle previews (camera name = file name)
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

- **Projection:** orthographic, yaw 45°, elevation 40° (`ELEV` in `layout.py`). The camera looks from
  +X+Z, so a building shows its +Z front on the left face and its +X side on
  the right face.
- **Scale:** 1.5 cm per pixel. Each camera sets `size="W H"` and
  `ortho="1.5 × H"`, so the scale is the same whatever the frame size.
- **Frames:** 3×3 buildings use 640×608, 3×2 use 640×544, 2×2 use 448×448, the
  turret 256×256, and vehicles 384×320.
- **Placement:** units stand 1100 cm apart along the camera's right vector
  (buildings on one row, vehicles on a second row 1700 cm nearer the camera),
  so no camera frames a neighbour. The `Lineup` camera shows them all.

`layout.py` computes every camera from these tables, so a new view angle, scale
or unit means editing its constants and running it again; `render.sh` does that
first.

## Lighting

- **Key sun:** `dir="0.99 -1.25 -0.42"`, warm, intensity 1.2. Light comes from
  the screen's upper left, so fronts (+Z) are lit and hard shadows fall to the
  right.
- **Fill sun:** a weak, cool, shadowless sun from the opposite side, so the +X
  faces are dark but not black.
- **Ambient:** 0.24, with `skyReflection="2"` for a sheen on metal. Scener's
  skill says to keep ambient at or below about 0.35: higher ambient flattens
  the cast shadows.
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
