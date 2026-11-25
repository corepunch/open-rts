#!/usr/bin/env python3
"""Writes art/units.blks: the palette, the lights, every unit instance and one orthographic
sprite camera per unit. Edit the tables here, not the generated scene.

    python3 games/rustfront/art/layout.py

The view is the sprite view of the game: yaw 45 degrees (camera at +X+Z), elevation ELEV,
CM_PER_PX centimetres per pixel. Units stand SPACING apart along the camera's right vector,
buildings on one row and vehicles on a row ROW_GAP nearer the camera, so no camera frames a
neighbour. The prefabs themselves live in prefabs/ and are hand-written.
"""
import math
from pathlib import Path

ELEV = 40.0          # degrees above the horizon
YAW = 45.0           # camera bearing from +Z toward +X
CM_PER_PX = 1.5
SPACING = 1100.0
ROW_GAP = 1700.0
TEAM = "0.20 0.42 0.88"

# (prefab, camera, frame w, frame h, look height cm)
BUILDINGS = [
    ("buildings/hq",       "CommandCenter", 640, 608, 140),
    ("buildings/power",    "PowerPlant",    448, 448, 100),
    ("buildings/refinery", "Refinery",      640, 544, 110),
    ("buildings/barracks", "Barracks",      448, 448,  80),
    ("buildings/factory",  "WarFactory",    640, 608, 140),
    ("buildings/turret",   "GunTurret",     256, 256,  60),
]
VEHICLES = [
    ("vehicles/scout",     "ScoutBuggy", 384, 320, 50),
    ("vehicles/tank",      "BattleTank", 384, 320, 60),
    ("vehicles/artillery", "Artillery", 384, 320, 80),
    ("vehicles/harvester", "Harvester", 384, 320, 70),
    ("vehicles/apc",       "APC", 384, 320, 60),
]
VEHICLE_YAW = -90    # prefabs face +X; -90 turns them to +Z, toward the viewer's lower left

PALETTE = """\
  <!-- palette: dusty ground, battered concrete, gunmetal, desert-tan armour, one team colour -->
  <material id="sand"          color="0.62 0.52 0.38" shininess="3"><map type="noise" size="90" octaves="4" from="0.84 0.84 0.86" to="1.08 1.06 1.02"/></material>
  <material id="pad"           color="0.50 0.49 0.46" shininess="4"><map type="bricks" size="100 100 100" mortar="1.8" bond="0" vary="0.07" from="1 1 1" to="0.62 0.61 0.60"/></material>
  <material id="pad_dark"      color="0.33 0.33 0.33" shininess="4"/>
  <material id="concrete"      color="0.68 0.65 0.58" shininess="5" gradientFrom="0.62 0.60 0.58" gradientTo="1.04 1.03 1.02"><map type="noise" size="30" octaves="4" from="0.88 0.88 0.88" to="1.05 1.05 1.05" bump="0.4"/></material>
  <material id="concrete_dark" color="0.50 0.49 0.46" shininess="5" gradientFrom="0.70 0.70 0.70" gradientTo="1.05 1.05 1.05"><map type="noise" size="30" octaves="4" from="0.88 0.88 0.88" to="1.05 1.05 1.05"/></material>
  <material id="gunmetal"      color="0.36 0.38 0.40" shininess="40" metallic="0.35" gradientFrom="0.72 0.72 0.74" gradientTo="1.05 1.05 1.05"/>
  <material id="roofing"       color="0.30 0.31 0.32" shininess="8" metallic="0.1"><map type="bricks" size="60 60 60" mortar="1.2" bond="0" vary="0.06" from="1 1 1" to="0.6 0.6 0.6"/></material>
  <material id="corrugated"    color="1 1 1" shininess="24" metallic="0.25">
    <gradient type="line" start="0 0 0" end="0 0 12" repeat="1" mirror="1" from="0.30 0.31 0.33" to="0.48 0.49 0.51"/>
  </material>
  <material id="steel"         color="0.60 0.62 0.64" shininess="45" metallic="0.35"/>
  <material id="steel_dark"    color="0.22 0.23 0.25" shininess="35" metallic="0.3"/>
  <material id="grating"       color="0.30 0.31 0.33" shininess="20"><map type="checker" size="4" from="0.7 0.7 0.7" to="1.1 1.1 1.1"/></material>
  <material id="rust"          color="0.55 0.32 0.19" shininess="6"><map type="noise" size="14" octaves="4" from="0.72 0.70 0.68" to="1.10 1.05 0.98"/></material>
  <material id="armor"         color="0.66 0.59 0.43" shininess="16" gradientFrom="0.70 0.69 0.66" gradientTo="1.06 1.05 1.02"><map type="noise" size="25" from="0.90 0.90 0.90" to="1.04 1.04 1.04"/></material>
  <material id="armor_dark"    color="0.44 0.40 0.30" shininess="14"/>
  <material id="team"          color="{TEAM}" shininess="22"/>
  <material id="track"         color="0.27 0.26 0.24" shininess="10"><map type="bricks" size="9 60 60" mortar="2.5" bond="0" from="1.25 1.22 1.2" to="0.45 0.45 0.45"/></material>
  <material id="rubber"        color="0.12 0.12 0.13" shininess="10"/>
  <material id="sandbag"       color="0.62 0.56 0.40" shininess="3"/>
  <material id="glass"         color="0.16 0.34 0.48" shininess="90" specular="1"/>
  <material id="ore"           color="0.95 0.70 0.18" shininess="40" specular="1"/>
  <material id="glow"          color="0.45 0.95 1.00"/>
  <material id="lamp"          color="1.00 0.90 0.62"/>
  <material id="warn"          color="1.00 0.18 0.10"/>
  <material id="flame"         color="1.00 0.55 0.15"/>
  <material id="hazard"        color="1 1 1" shininess="8">
    <gradient type="line" start="0 0 0" end="7 7 0" repeat="1" edge="0.5" from="0.96 0.74 0.10" to="0.10 0.09 0.08"/>
  </material>"""


# Shade stays dark and keeps its colour: a low warm ambient (it multiplies each material's own
# hue) and a weak warm fill, never a cool blue one, which washes shaded faces out to grey-blue.
SCENE_ATTRS = 'ambient="0.17 0.15 0.13" skyReflection="0.6"'
KEY_SUN = '<sun dir="0.99 -1.25 -0.42" color="1.0 0.93 0.80" intensity="1.25" castShadows="1"/>'
LIGHTS = """  <!-- hard key from the screen's upper left, shadows fall to the right; weak warm fill -->
  """ + KEY_SUN + """
  <sun dir="-1 -0.6 0.3" color="1.0 0.82 0.62" intensity="0.14" castShadows="0"/>"""


def unit(az, el):
    a, e = math.radians(az), math.radians(el)
    return (math.sin(a) * math.cos(e), math.sin(e), math.cos(a) * math.cos(e))


def main():
    to_cam = unit(YAW, ELEV)
    a = math.radians(YAW)
    right = (math.cos(a), 0.0, -math.sin(a))
    near = (math.sin(a), 0.0, math.cos(a))
    dist = 4000.0

    def camera(name, look, w, h, comment):
        pos = [look[k] + dist * to_cam[k] for k in range(3)]
        f = lambda v: " ".join("%.1f" % c for c in v)
        return ('  <camera name="%s" comment="%s" pos="%s" look="%s" ortho="%g" size="%d %d"/>'
                % (name, comment, f(pos), f(look), h * CM_PER_PX, w, h))

    cams, insts = [], []
    for row, units, yaw, offset in (("building", BUILDINGS, 0, 0.0), ("vehicle", VEHICLES, VEHICLE_YAW, ROW_GAP)):
        mid = (len(units) - 1) / 2
        for i, (src, name, w, h, ly) in enumerate(units):
            t = (i - mid) * SPACING
            p = [right[k] * t + near[k] * offset for k in range(3)]
            rot = ' rot="0 %d 0"' % yaw if yaw else ""
            insts.append('  <prefab source="%s" name="%s" pos="%.1f 0 %.1f"%s color="%s"/>'
                         % (src, src.split("/")[1], p[0], p[2], rot, TEAM))
            cams.append(camera(name, (p[0], ly, p[2]), w, h, "Sprite preview: %s, %g cm/px" % (name, CM_PER_PX)))

    mid = [near[k] * ROW_GAP / 2 for k in range(3)]
    mid[1] = 90.0
    width = (max(len(BUILDINGS), len(VEHICLES)) - 1) * SPACING + 1000
    height = ROW_GAP * math.sin(math.radians(ELEV)) + 800
    lh = 720
    lw = int(round(lh * width / height / 8)) * 8
    cams.insert(0, '  <camera name="Lineup" comment="Every unit at once" pos="%s" look="%s" ortho="%g" size="%d %d"/>'
                % (" ".join("%.1f" % (mid[k] + dist * to_cam[k]) for k in range(3)),
                   " ".join("%.1f" % c for c in mid), height, lw, lh))

    scene = f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- GENERATED by layout.py; edit that, not this file.
     Rustfront unit kit: every building and vehicle with an orthographic sprite camera.
     View: yaw {YAW:g}, elevation {ELEV:g}, {CM_PER_PX:g} cm per pixel; a cell is 200 cm.
     Team-colour parts are tint="1": each instance's color recolours them. See ../docs/SCENER.md. -->
<scene {SCENE_ATTRS} background="0.30 0.27 0.23">
{PALETTE.replace("{TEAM}", TEAM)}

{LIGHTS}

{chr(10).join(cams)}

  <!-- shadow-catching ground; drop it (or renderable="0") for transparent sprite passes -->
  <box name="ground" size="10000 10 10000" pos="0 -5 0" material="sand"/>

{chr(10).join(insts)}
</scene>
"""
    Path(__file__).with_name("units.blks").write_text(scene)


if __name__ == "__main__":
    main()
