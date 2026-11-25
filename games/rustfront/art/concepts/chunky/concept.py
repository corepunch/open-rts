#!/usr/bin/env python3
"""Renders the "chunky" building concept beside the buildings the game ships, for review.

    python3 games/rustfront/art/concepts/chunky/concept.py            # the whole sheet
    python3 games/rustfront/art/concepts/chunky/concept.py Factory    # one close-up, for modelling

The concept is a proposal, not game art: prefabs/buildings/ here mirrors art/prefabs/buildings/
(same footprints, origins, fronts and animation knobs), so adopting it means moving these files
over the shipped ones and adding PAINT and chunky.xml to layout.py and kit.xml.

The style: big painted hulls in the team colour with rounded corners and chamfered rims, on
pale slabs, with hazard skirts, dark trim, cyan glazing and a gold cog roundel on every roof.
The view, the lights and the vehicles are the game's (layout.py), so the two sets compare
like for like. The last row is the concept at the game's resolution with its team paint put
through pack8.py's 16-shade team ramp, as the engine would draw it.
Output: preview.png, and .build/ (scratch scenes and renders, ignored by git).
"""
import math
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ART = HERE.parent.parent
sys.path.insert(0, str(ART))
import layout as L  # noqa: E402
import pack8  # noqa: E402

WORK = HERE / ".build"
SCENER = os.environ.get("SCENER", "scener")
BLUE, RED = L.TEAM, "0.86 0.16 0.12"
CM_PER_PX = 2.0          # the sheet's rows: about twice the game's sprite resolution
SPACING = 820.0          # cm between buildings along a row

# The concept's own materials, on top of layout.py's palette. A tint="1" part takes the
# instance's colour and keeps its material's gradient and maps, so paint is the team colour
# with panel seams and a darker foot, and paint_dark the same hue at trim strength.
PAINT = """\
  <!-- chunky concept: team paint, pale slabs, dark trim, the roundel's colours -->
  <material id="slab"         color="0.80 0.79 0.74" shininess="6"><map type="bricks" size="100 100 100" mortar="2" bond="0" vary="0.05" from="1 1 1" to="0.70 0.69 0.68"/></material>
  <material id="paint"        color="{TEAM}" shininess="46" specular="1" gradientFrom="0.60 0.60 0.64" gradientTo="1.10 1.10 1.10"><map type="bricks" size="96 64 96" mortar="1.6" bond="0" from="1 1 1" to="0.55 0.55 0.6"/></material>
  <material id="paint_plain"  color="{TEAM}" shininess="46" specular="1" gradientFrom="0.66 0.66 0.70" gradientTo="1.10 1.10 1.10"/>
  <material id="paint_dark"   color="{TEAM}" shininess="30" gradientFrom="0.42 0.42 0.46" gradientTo="0.50 0.50 0.54"/>
  <material id="trim"         color="0.13 0.16 0.24" shininess="34" specular="1"/>
  <material id="trim_light"   color="0.80 0.82 0.84" shininess="40" specular="1" gradientFrom="0.70 0.70 0.74" gradientTo="1.05 1.05 1.05"/>
  <material id="window"       color="0.30 0.85 1.00" shininess="90" specular="1"/>
  <material id="emblem_gold"  color="1.00 0.78 0.10" shininess="40"/>
  <material id="emblem_white" color="0.96 0.96 0.92" shininess="30"/>
  <material id="emblem_dark"  color="0.10 0.12 0.22" shininess="30"/>
  <material id="hazard_big"   color="1 1 1" shininess="10">
    <gradient type="line" start="0 0 0" end="13 13 0" repeat="1" edge="0.5" from="0.98 0.78 0.10" to="0.10 0.09 0.08"/>
  </material>
  <material id="hazard_skirt" color="1 1 1" shininess="10">
    <gradient type="line" start="0 0 0" end="13 0 13" repeat="1" edge="0.5" from="0.98 0.78 0.10" to="0.10 0.09 0.08"/>
  </material>
  <material id="hazard_ring"  color="1 1 1" shininess="10">
    <gradient type="uv" tile="8 1" repeat="1" edge="0.5" from="0.98 0.78 0.10" to="0.10 0.09 0.08"/>
  </material>
  <material id="hazard_ring_big" color="1 1 1" shininess="10">
    <gradient type="uv" tile="14 1" repeat="1" edge="0.5" from="0.98 0.78 0.10" to="0.10 0.09 0.08"/>
  </material>"""

# (prefab, close-up camera, look height cm, close-up frame at 1.5 cm/px)
BUILDINGS = [
    ("hq",       "CommandCenter", 140, (640, 608)),
    ("power",    "PowerPlant",    100, (448, 448)),
    ("refinery", "Refinery",      110, (640, 544)),
    ("barracks", "Barracks",       80, (448, 448)),
    ("factory",  "WarFactory",    140, (640, 608)),
    ("turret",   "GunTurret",      60, (256, 256)),
]
# the game's own units for scale: (prefab, x from the first building, z, rot)
VEHICLES = [("vehicles/tank", 500, 300, "0 -45 0"), ("vehicles/harvester", 1560, 540, "0 60 0"),
            ("vehicles/scout", 2100, 330, "0 -120 0"), ("vehicles/apc", 3730, 400, "0 -20 0"),
            ("infantry/rifleman", 2330, 400, "-90 60 0"), ("infantry/rifleman", 2420, 440, "-90 100 0"),
            ("infantry/rocket", 2520, 410, "-90 130 0")]


def unit(az, el):
    a, e = math.radians(az), math.radians(el)
    return (math.sin(a) * math.cos(e), math.sin(e), math.cos(a) * math.cos(e))


TO_CAM = unit(L.YAW, L.ELEV)


def camera(name, look, w, h, cm_per_px):
    pos = [look[k] + 4000.0 * TO_CAM[k] for k in range(3)]
    f = lambda v: " ".join("%.1f" % c for c in v)
    return '  <camera name="%s" pos="%s" look="%s" ortho="%g" size="%d %d"/>' % (name, f(pos), f(look), h * cm_per_px, w, h)


def scene(folder, prefabs, team, palette):
    """Writes .build/<folder>/scene.blks: the six buildings in a row with a few of the game's
    vehicles in front, a Row camera over all of it and one close-up camera per building."""
    work = WORK / folder
    work.mkdir(parents=True, exist_ok=True)
    for name, target in (("prefabs", prefabs), ("kit.xml", ART / "kit.xml"), ("utilities.xml", ART / "utilities.xml"),
                         ("chunky.xml", HERE / "chunky.xml")):
        link = work / name
        if link.is_symlink():
            link.unlink()
        link.symlink_to(target)
    mid = (len(BUILDINGS) - 1) / 2
    xs = [(i - mid) * SPACING for i in range(len(BUILDINGS))]
    xs[-1] -= 180        # the turret needs less room
    insts, cams = [], []
    for (src, cam, ly, (w, h)), x in zip(BUILDINGS, xs):
        insts.append('  <prefab source="buildings/%s" pos="%.1f 0 0" color="%s"/>' % (src, x, team))
        cams.append(camera(cam, (x, ly, 0), w * 2, h * 2, 0.75))
    for src, x, z, rot in VEHICLES:
        insts.append('  <prefab source="%s" pos="%.1f 0 %.1f" rot="%s" color="%s"/>' % (src, x + xs[0], z, rot, team))
    width = xs[-1] - xs[0] + 760
    rw, rh = int(round(width / CM_PER_PX / 8)) * 8, 500
    cams.insert(0, camera("Row", ((xs[0] + xs[-1]) / 2 + 30, 120, 110), rw, rh, CM_PER_PX))
    (work / "scene.blks").write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED by concepts/chunky/concept.py -->
<scene {L.SCENE_ATTRS} background="0.30 0.27 0.23">
{L.PALETTE.replace("{TEAM}", team)}
{palette.replace("{TEAM}", team)}

{L.LIGHTS}

{chr(10).join(cams)}

  <box name="ground" size="12000 10 12000" pos="0 -5 0" material="sand"/>

{chr(10).join(insts)}
</scene>
""")
    return work


def render(work, cam):
    out = work / "out"
    samples = "3" if cam == "Row" else "4"       # Scener renders at most 8192 pixels across
    subprocess.run([SCENER, "--render", str(work / "scene.blks"), "--camera", cam, "--format", "png",
                    "--supersample", samples, "--output-dir", str(out)], check=True, stdout=subprocess.DEVNULL)
    return Image.open(out / (cam + ".png")).convert("RGB")


def font(size):
    for path in ("/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf"):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def as_game(prefabs, palette, team):
    """The Row at game resolution, team-coloured as sprites.py and pack8.py do it: the mask is
    a white-team render minus a black-team one, and a pixel whose mask reaches TEAM_MIN_MASK
    takes the ramp shade of its mask level. Shown at twice its size, pixels kept square."""
    black = render(scene("black", prefabs, "0 0 0", palette), "Row")
    white = render(scene("white", prefabs, "1 1 1", palette), "Row")
    size = tuple(round(v * CM_PER_PX / L.SPRITE_CM_PER_PX) for v in black.size)
    black, white = black.resize(size, Image.LANCZOS), white.resize(size, Image.LANCZOS)
    mask = ImageChops.subtract(white, black).convert("L")
    ramp = pack8.team_ramp(dict(pack8.TEAMS)[team])
    shade = [ramp[min(pack8.TEAM_SHADES - 1, v * pack8.TEAM_SHADES // 256)] for v in range(256)]
    paint = Image.merge("RGB", [mask.point([c[k] for c in shade]) for k in range(3)])
    black.paste(paint, mask=mask.point(lambda v: 255 if v >= pack8.TEAM_MIN_MASK else 0))
    return black.resize((size[0] * 2, size[1] * 2), Image.NEAREST)


def main():
    shipped = scene("shipped", ART / "prefabs", BLUE, "")
    blue = scene("blue", HERE / "prefabs", BLUE, PAINT)
    red = scene("red", HERE / "prefabs", RED, PAINT)
    check = subprocess.run([SCENER, "--check", str(blue / "scene.blks")], capture_output=True, text=True)
    print("\n".join(l for l in (check.stdout + check.stderr).splitlines() if "error" in l or l.startswith("check ")))
    if len(sys.argv) > 1:
        for cam in sys.argv[1:]:
            print(blue / "out" / (cam + ".png"))
            render(blue, cam)
        return
    rows = [("Current buildings", render(shipped, "Row")),
            ("Concept, blue team", render(blue, "Row")),
            ("Concept, red team", render(red, "Row")),
            ("Concept as the game would draw it: 48 px a cell, 16-shade team ramp (shown 2x)",
             as_game(HERE / "prefabs", PAINT, "blue"))]
    w = max(im.size[0] for _, im in rows)
    gap, label = 10, 34
    sheet = Image.new("RGB", (w, sum(im.size[1] + label + gap for _, im in rows) - gap), (24, 22, 20))
    draw = ImageDraw.Draw(sheet)
    y = 0
    for title, im in rows:
        draw.text((12, y + 6), title, fill=(236, 228, 210), font=font(20))
        sheet.paste(im, (0, y + label))
        y += im.size[1] + label + gap
    sheet.save(HERE / "preview.png")
    print(HERE / "preview.png")


if __name__ == "__main__":
    main()
