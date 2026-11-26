#!/usr/bin/env python3
"""Renders Rustfront's game sprites with Scener and packs them into sheets with a manifest.

    python3 games/rustfront/art/sprites.py            # every unit
    python3 games/rustfront/art/sprites.py tank hq    # just these

Output (games/rustfront/sprites/):
    <unit>_<layer>.png        colour frames, team parts rendered black
    <unit>_<layer>_team.png   team mask: how much team colour each pixel takes (greyscale)
    <unit>_shadow.png         ground shadow, black with alpha
    sprites.json              frame size, sheet columns, pivots, facings, turret mounts

Three passes share one sprite view: layout.py's yaw and elevation at SPRITE_CM_PER_PX, so a
200 cm cell is a 48 x 32 pixel rectangle that matches the engine's tile grid:
  colour  - transparent background, team parts black; supersampled 3x3
  team    - the same with team parts white; mask = white - black. In the game a pixel is
            base + team_colour * mask (in sRGB, an approximation that reads right)
  shadow  - the assembled unit hidden (renderable="0", it still casts) over a white catcher
            lit by the key sun alone; alpha = (1 - catcher brightness) * SHADOW_OPACITY

Layers: a unit is one layer, or a hull and a turret that turn independently (tank, APC,
gun turret). A turret layer is rendered about its own axis; sprites.json gives, for every
hull facing, the pixel offset of the turret's mount from the hull's pivot, so the game
draws the turret frame with its pivot at hull pivot + mount offset.

Facing i of N is a turn of i * 360/N degrees about +Y, counter-clockwise seen from above,
starting from the prefab's front (+X for vehicles and turret heads). A layer may have
animation frames: each is the prefab rendered with its <let> knobs set (see layer()), and a
sheet's frame index is facing * frames + frame. Pivots are the
screen position of the layer's anchor (a unit's ground origin, a turret's axis at mount
height) in its cropped frame, on a pixel corner.
"""
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

ART = Path(__file__).resolve().parent
sys.path.insert(0, str(ART))
import layout as L  # noqa: E402

OUT = ART.parent / "sprites"
WORK = ART / ".build"
SUPERSAMPLE = 4
SCALE = L.SPRITE_CM_PER_PX   # cm per sprite pixel: one 200 cm cell is TILE_W x TILE_H
SHADOW_OPACITY = 0.6     # darkest shadow alpha: the ground still gets ambient light
SPACING = 2400.0          # cm between subjects along the camera's right vector
SCENER = os.environ.get("SCENER", "scener")


def layer(name, prefab, frame, centre, mount=None, facings=None, frames=None, structure=None,
          damaged=True, build_sheet=False):
    """frame is authored in preview pixels (1.5 cm); it is converted to game pixels, even.
    frames lists the layer's animation frames as knob settings: each dict overrides the
    prefab's <let name value> knobs (move, fire, spin, blink...) for one frame.
    A structure (foot cells, height cm) also gets scorched copies of its frames (damaged) and
    the BUILD_STAGES construction frames: after them on its own sheet, or with build_sheet on a
    separate <unit>_build sheet (the gun turret, so its head does not show over a foundation)."""
    fw, fh = (int(math.ceil(v * 1.5 / SCALE / 2)) * 2 for v in frame)
    frames = frames or [{}]
    anim = len(frames)
    if structure and not build_sheet:
        frames = frames + [{"build": b} for b in BUILD_STAGES]
    return dict(name=name, prefab=prefab, frame=(fw, fh), centre=centre, mount=mount, facings=facings,
                frames=frames, structure=structure, anim=anim, damaged=bool(structure) and damaged,
                build_sheet=build_sheet)


# Construction: a structure rises through these heights (fractions of its own) inside a
# scaffold, on a fresh foundation slab.
BUILD_STAGES = [0.06, 0.35, 0.65, 0.9]


def construction(prefab, stage, foot, height, index):
    """A wrapper prefab: the structure squashed to `stage` of its height on a foundation slab,
    inside scaffolding a little taller than the work. foot is cells, height cm."""
    w, d = foot[0] * 200 - 16, foot[1] * 200 - 16
    top = height * stage + 50
    parts = ['  <box size="%g 10 %g" pos="0 5 0" material="concrete_dark"/>' % (w, d),
             '  <group scale="1 %g 1"><prefab source="%s"/></group>' % (stage, prefab)]
    step_x, step_z = w / max(1, round(w / 110)), d / max(1, round(d / 110))
    xs = [-w / 2 + i * step_x for i in range(round(w / step_x) + 1)]
    zs = [-d / 2 + i * step_z for i in range(round(d / step_z) + 1)]
    edge = [(x, z) for x in xs for z in (zs[0], zs[-1])] + [(x, z) for x in (xs[0], xs[-1]) for z in zs[1:-1]]
    for x, z in edge:
        parts.append('  <cylinder radius="2.5" height="%g" sides="6" pos="%g %g %g" material="steel"/>' % (top, x, top / 2 + 10, z))
    for y in range(60, int(top) + 1, 70):
        mat = "hazard" if y + 70 > top else "steel_dark"
        for z in (zs[0], zs[-1]):
            parts.append('  <box size="%g 3 4" pos="0 %g %g" material="%s"/>' % (w, y, z, mat))
        for x in (xs[0], xs[-1]):
            parts.append('  <box size="4 3 %g" pos="%g %g 0" material="%s"/>' % (d, x, y, mat))
    if stage > 0.5:   # a jib crane over the work
        parts.append('  <box size="12 %g 12" pos="%g %g %g" material="hazard"/>' % (top + 60, xs[-1] + 14, (top + 60) / 2 + 10, zs[0] + 14))
        parts.append('  <box size="%g 8 8" pos="%g %g %g" rot="0 35 0" material="hazard"/>' % (w * 0.7, xs[-1] - w * 0.25, top + 66, zs[0] + w * 0.2))
    name = "%s@build%d" % (prefab, index)
    (WORK / "prefabs" / (name + ".blk")).write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n<!-- GENERATED by sprites.py: %s under construction -->\n<prefab>\n%s\n</prefab>\n'
        % (prefab, "\n".join(parts)))
    return name


def variant(prefab, knobs, index, structure=None):
    """A copy of the prefab in the build folder with its knobs set; returns its source name.
    A {"build": stage} frame is a construction wrapper instead (structure = (foot, height))."""
    if not knobs:
        return prefab
    if "build" in knobs:
        return construction(prefab, knobs["build"], structure[0], structure[1], index)
    text = (WORK / "prefabs" / (prefab + ".blk")).read_text()
    for name, value in knobs.items():
        text, hits = re.subn(r'(<let name="%s" value=")[^"]*(")' % re.escape(name), r"\g<1>%s\g<2>" % value, text)
        if hits != 1:
            sys.exit("sprites.py: %s.blk has no single <let name=\"%s\"> knob" % (prefab, name))
    name = "%s@%d" % (prefab, index)
    (WORK / "prefabs" / (name + ".blk")).write_text(text)
    return name


# Frame lists. Hulls: standing, then the running gear a step on. Guns: idle, the shot, run-out.
MOVE = [{}, {"move": 1}]
FIRE = [{}, {"fire": 1}, {"fire": 2}]
MOVE_FIRE = [{}, {"move": 1}, {"fire": 1}]
BLINK = [{"blink": 1}, {"blink": 0}]
PULSE = [{"pulse": 0}, {"pulse": 1}]
BLINK_FLICK = [{"blink": 1, "flick": 0}, {"blink": 0, "flick": 1}]
SPIN_PULSE = [{"spin": k / 8, "pulse": int(k % 4 >= 2)} for k in range(8)]


# key: shadow (assembled prefab), facings of the unit, layers
UNITS = {
    "hq":        dict(shadow="buildings/hq",       facings=1,  layers=[layer("body", "buildings/hq", (640, 608), 140, structure=((3, 3), 290),
                     frames=[{"spin": k, "blink": int(k % 4 < 2)} for k in range(8)])]),
    "power":     dict(shadow="buildings/power",    facings=1,  layers=[layer("body", "buildings/power", (448, 448), 100, structure=((2, 2), 220),
                     frames=[{"pulse": 0, "blink": 1}, {"pulse": 1, "blink": 1}, {"pulse": 0, "blink": 0}, {"pulse": 1, "blink": 0}])]),
    "refinery":  dict(shadow="buildings/refinery", facings=1,  layers=[layer("body", "buildings/refinery", (640, 544), 110, structure=((3, 2), 300),
                     frames=[{"flick": 0, "blink": 1}, {"flick": 1, "blink": 1}, {"flick": 0, "blink": 0}, {"flick": 1, "blink": 0}])]),
    "barracks":  dict(shadow="buildings/barracks", facings=1,  layers=[layer("body", "buildings/barracks", (448, 448), 80, structure=((2, 2), 220), frames=BLINK)]),
    "factory":   dict(shadow="buildings/factory",  facings=1,  layers=[layer("body", "buildings/factory", (640, 608), 140, structure=((3, 3), 300), frames=BLINK)]),
    "turret":    dict(shadow="buildings/turret",   facings=1,  layers=[
                     # two base frames: the firing run steps the body's frame, and the head follows it
                     layer("base", "buildings/turret/base", (256, 256), 40, frames=[{}, {}],
                           structure=((1, 1), 130), damaged=False, build_sheet=True),
                     layer("build", "buildings/turret", (256, 256), 40, structure=((1, 1), 130),
                           frames=[{"build": b} for b in BUILD_STAGES], damaged=False, build_sheet=True),
                     layer("head", "buildings/turret/head", (288, 240), 0, mount=(0, 98, 0), facings=16, frames=FIRE)]),
    "uplink":    dict(shadow="buildings/uplink",   facings=1,  layers=[layer("body", "buildings/uplink", (448, 640), 150, structure=((2, 2), 290),
                     frames=[{"spin": k, "blink": int(k % 4 < 2)} for k in range(8)])]),
    "scout":     dict(shadow="vehicles/scout",     facings=16, layers=[layer("body", "vehicles/scout", (320, 320), 50, frames=MOVE_FIRE)]),
    "tank":      dict(shadow="vehicles/tank",      facings=16, layers=[
                     layer("hull", "vehicles/tank/hull", (384, 352), 45, frames=MOVE),
                     layer("turret", "vehicles/tank/turret", (512, 512), 0, mount=(-12, 102, 0), frames=FIRE)]),
    "artillery": dict(shadow="vehicles/artillery", facings=16, layers=[layer("body", "vehicles/artillery", (640, 576), 70, frames=MOVE_FIRE)]),
    "harvester": dict(shadow="vehicles/harvester", facings=16, layers=[layer("body", "vehicles/harvester", (448, 384), 70, frames=MOVE)]),
    "mastodon":  dict(shadow="vehicles/mastodon",  facings=16, layers=[
                     layer("hull", "vehicles/mastodon/hull", (512, 480), 50, frames=MOVE),
                     layer("turret", "vehicles/mastodon/turret", (672, 640), 0, mount=(-14, 112, 0), frames=FIRE)]),
    "hammer":    dict(shadow="vehicles/hammer",    facings=16, layers=[layer("body", "vehicles/hammer", (704, 640), 90, frames=MOVE_FIRE)]),
    # Scrap Clans: warning lamps blink while barrels, furnaces and welders flicker; the turbine turns
    "cl_hall":      dict(shadow="clans/hall", facings=1, layers=[layer("body", "clans/hall", (640, 704), 170,
                         structure=((3, 3), 400), frames=BLINK_FLICK)]),
    "cl_generator": dict(shadow="clans/generator", facings=1, layers=[layer("body", "clans/generator", (448, 640), 150,
                         structure=((2, 2), 370), frames=[{"blink": 1, "fan": 0}, {"blink": 0, "fan": 1}])]),
    "cl_smelter":   dict(shadow="clans/smelter", facings=1, layers=[layer("body", "clans/smelter", (640, 704), 170,
                         structure=((3, 2), 425), frames=BLINK_FLICK)]),
    "cl_camp":      dict(shadow="clans/camp", facings=1, layers=[layer("body", "clans/camp", (448, 512), 110,
                         structure=((2, 2), 250), frames=BLINK_FLICK)]),
    "cl_yard":      dict(shadow="clans/yard", facings=1, layers=[layer("body", "clans/yard", (640, 704), 160,
                         structure=((3, 3), 390), frames=BLINK_FLICK)]),
    "cl_tower":     dict(shadow="clans/spike_tower", facings=1, layers=[
                        layer("base", "clans/spike_tower/base", (256, 288), 50, frames=[{"blink": 1}, {"blink": 0}],
                              structure=((1, 1), 210), damaged=False, build_sheet=True),
                        layer("build", "clans/spike_tower", (256, 320), 50, structure=((1, 1), 210),
                              frames=[{"build": b} for b in BUILD_STAGES], damaged=False, build_sheet=True),
                        layer("head", "clans/spike_tower/head", (320, 256), 0, mount=(0, 110, 0), facings=16, frames=FIRE)]),
    "cl_shop":      dict(shadow="clans/chop_shop", facings=1, layers=[layer("body", "clans/chop_shop", (448, 576), 130,
                         structure=((2, 2), 260), frames=BLINK_FLICK)]),
    "cl_trike":     dict(shadow="clans/trike", facings=16, layers=[layer("body", "clans/trike", (320, 288), 45, frames=MOVE_FIRE)]),
    "cl_truck":     dict(shadow="clans/gun_truck", facings=16, layers=[
                        layer("hull", "clans/gun_truck/hull", (480, 416), 55, frames=MOVE),
                        layer("turret", "clans/gun_truck/turret", (320, 288), 0, mount=(-62, 112, 0), frames=FIRE)]),
    "cl_crawler":   dict(shadow="clans/crawler", facings=16, layers=[
                        layer("hull", "clans/crawler/hull", (448, 416), 50, frames=MOVE),
                        layer("turret", "clans/crawler/turret", (512, 512), 0, mount=(-14, 104, 0), frames=FIRE)]),
    "cl_rig":       dict(shadow="clans/rocket_rig", facings=16, layers=[layer("body", "clans/rocket_rig", (512, 480), 70, frames=MOVE_FIRE)]),
    "cl_hauler":    dict(shadow="clans/hauler", facings=16, layers=[layer("body", "clans/hauler", (544, 480), 70, frames=MOVE)]),
    "cl_wrecker":   dict(shadow="clans/wrecker", facings=16, layers=[layer("body", "clans/wrecker", (448, 416), 60, frames=MOVE_FIRE)]),
    # Lumen Concord: a ring turns 15 degrees a frame (threefold, so eight frames loop) as the glow pulses
    "lc_spire":     dict(shadow="concord/spire", facings=1, layers=[layer("body", "concord/spire", (640, 768), 200,
                         structure=((3, 3), 470), frames=SPIN_PULSE)]),
    "lc_fusion":    dict(shadow="concord/fusion", facings=1, layers=[layer("body", "concord/fusion", (448, 576), 140,
                         structure=((2, 2), 320), frames=SPIN_PULSE)]),
    "lc_extractor": dict(shadow="concord/extractor", facings=1, layers=[layer("body", "concord/extractor", (640, 576), 110,
                         structure=((3, 2), 245), frames=PULSE)]),
    "lc_academy":   dict(shadow="concord/academy", facings=1, layers=[layer("body", "concord/academy", (448, 576), 130,
                         structure=((2, 2), 300), frames=SPIN_PULSE)]),
    "lc_foundry":   dict(shadow="concord/foundry", facings=1, layers=[layer("body", "concord/foundry", (640, 640), 150,
                         structure=((3, 3), 330), frames=SPIN_PULSE)]),
    "lc_tower":     dict(shadow="concord/beam_tower", facings=1, layers=[
                        layer("base", "concord/beam_tower/base", (256, 320), 60, frames=[{"pulse": 0}, {"pulse": 1}],
                              structure=((1, 1), 210), damaged=False, build_sheet=True),
                        layer("build", "concord/beam_tower", (256, 320), 60, structure=((1, 1), 210),
                              frames=[{"build": b} for b in BUILD_STAGES], damaged=False, build_sheet=True),
                        layer("head", "concord/beam_tower/head", (320, 256), 0, mount=(0, 140, 0), facings=16, frames=FIRE)]),
    "lc_sanctum":   dict(shadow="concord/sanctum", facings=1, layers=[layer("body", "concord/sanctum", (448, 640), 150,
                         structure=((2, 2), 290), frames=SPIN_PULSE)]),
    "lc_skimmer":   dict(shadow="concord/skimmer", facings=16, layers=[layer("body", "concord/skimmer", (320, 320), 55, frames=MOVE_FIRE)]),
    "lc_glaive":    dict(shadow="concord/glaive", facings=16, layers=[
                        layer("hull", "concord/glaive/hull", (416, 384), 50, frames=MOVE),
                        layer("turret", "concord/glaive/turret", (512, 512), 0, mount=(-14, 80, 0), frames=FIRE)]),
    "lc_prism":     dict(shadow="concord/prism", facings=16, layers=[layer("body", "concord/prism", (640, 576), 90, frames=MOVE_FIRE)]),
    "lc_harvester": dict(shadow="concord/harvester", facings=16, layers=[layer("body", "concord/harvester", (448, 416), 75, frames=MOVE)]),
    "lc_aegis":     dict(shadow="concord/aegis", facings=16, layers=[layer("body", "concord/aegis", (480, 448), 70, frames=MOVE_FIRE)]),
    "apc":       dict(shadow="vehicles/apc",       facings=16, layers=[
                     layer("hull", "vehicles/apc/hull", (384, 352), 50, frames=MOVE),
                     layer("turret", "vehicles/apc/turret", (288, 256), 0, mount=(22, 112, 0), frames=FIRE)]),
}

# Infantry: one animated layer, 8 facings. Each animation is the instance's children (a gait,
# layers playing the soldier's own poses and clips) sampled at fixed --time values. The
# soldier prefab is Z up facing -Y; rot="-90 90 0" stands it in this Y-up world facing +X.
RUN_CYCLE, RUN_FROM, RUN_FRAMES = 0.5, 1.5, 6        # stride / speed; sampled after the ramp
INFANTRY_ROT = "-90 90 0"
RUN = ('<gait mode="spot" start="0" stride="170" speed="340" lift="20" swing="0.5" bounce="5" crouch="5"'
       ' hipTwist="12" spineTwist="8" footRoll="30"/>'
       '<layer pose="Run" mask="spine left_collarbone right_collarbone weapon head"/>'
       '<layer clip="RunBob" mode="additive"/>')
ANIMS = [
    ("idle", "", '<layer clip="Idle"/>', [0.0, 0.6, 1.2, 1.8]),
    ("walk", "", RUN, [round(RUN_FROM + i * RUN_CYCLE / RUN_FRAMES, 4) for i in range(RUN_FRAMES)]),
    ("fire", "", '<layer clip="Fire"/><option name="flash" enabled="1"/>', [0.05]),
    ("fire2", "", '<layer clip="Fire"/>', [0.15]),
    ("die", "", '<layer clip="Die"/>', [0.08, 0.2, 0.34, 0.48, 0.62, 0.76, 0.95]),
]
INFANTRY = {
    "rifleman": dict(prefab="infantry/rifleman", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
    "rocket":   dict(prefab="infantry/rocket", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
    "cl_raider": dict(prefab="clans/raider", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
    "cl_torcher": dict(prefab="clans/torcher", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
    "lc_warden": dict(prefab="concord/warden", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
    "lc_lancer": dict(prefab="concord/lancer", facings=8, frame=(160, 128), centre=60, anims=ANIMS),
}
ALL_UNITS = list(UNITS) + list(INFANTRY)


def basis():
    a, e = math.radians(L.YAW), math.radians(L.ELEV)
    to_cam = (math.sin(a) * math.cos(e), math.sin(e), math.cos(a) * math.cos(e))
    right = (math.cos(a), 0.0, -math.sin(a))
    up = (-math.sin(e) * math.sin(a), math.cos(e), -math.sin(e) * math.cos(a))
    return to_cam, right, up


TO_CAM, RIGHT, UP = basis()
dot = lambda p, q: sum(x * y for x, y in zip(p, q))


def turn(p, deg):
    """Rotate p about +Y the way Scener's rot="0 deg 0" does (+X toward -Z)."""
    t = math.radians(deg)
    x, y, z = p
    return (x * math.cos(t) + z * math.sin(t), y, -x * math.sin(t) + z * math.cos(t))


def to_px(p):
    """Screen offset in pixels (x right, y down) of a world displacement p."""
    return (dot(p, RIGHT) / SCALE, -dot(p, UP) / SCALE)


def camera(name, anchor, frame, k, rot_target=None, deg=0.0):
    """An ortho camera whose image puts `anchor` at (W/2, H/2 + k) pixels."""
    look = [anchor[i] + UP[i] * k * SCALE for i in range(3)]
    pos = [look[i] + 4000 * TO_CAM[i] for i in range(3)]
    f = lambda v: " ".join("%.3f" % c for c in v)
    w, h = frame
    xml = '  <camera name="%s" pos="%s" look="%s" ortho="%g" size="%d %d"' % (
        name, f(pos), f(look), h * SCALE, w, h)
    if rot_target is None:
        return xml + "/>"
    return xml + '>\n    <transform target="%s" rot="0 %g 0"/>\n  </camera>' % (rot_target, deg)


def plan(keys):
    """Every frame to render: (pass-independent) subjects, cameras and bookkeeping."""
    subjects, shots, shadow_subjects, shadow_shots = [], [], [], []
    slot = 0
    for key in keys:
        u = UNITS[key]
        for ly in u["layers"]:
            n = ly["facings"] or u["facings"]
            base = [RIGHT[i] * slot * SPACING for i in range(3)]
            slot += 1
            mount = ly["mount"] or (0, 0, 0)
            anchor = (base[0], mount[1], base[2])
            k = round(ly["centre"] * math.cos(math.radians(L.ELEV)) / SCALE)
            for f, knobs in enumerate(ly["frames"]):
                if f:
                    base = [RIGHT[i] * slot * SPACING for i in range(3)]
                    slot += 1
                    anchor = (base[0], mount[1], base[2])
                inst = "%s_%s_f%d" % (key, ly["name"], f)
                subjects.append((inst, variant(ly["prefab"], knobs, f, ly["structure"]), anchor))
                for i in range(n):
                    cam = "%s_%02d" % (inst, i)
                    shots.append(dict(unit=key, layer=ly["name"], facing=i, frame=f, camera=cam,
                                      xml=camera(cam, anchor, ly["frame"], k, inst, i * 360.0 / n),
                                      pivot=(ly["frame"][0] / 2, ly["frame"][1] / 2 + k)))
        # shadow of the assembled unit, one per unit facing, in a frame twice the first layer's
        n = u["facings"]
        base = [RIGHT[i] * slot * SPACING for i in range(3)]
        slot += 1
        inst = "%s_shadow" % key
        shadow_subjects.append((inst, u["shadow"], tuple(base)))
        w, h = u["layers"][0]["frame"]
        frame = (w * 2, h * 2)
        for i in range(n):
            cam = "%s_%02d" % (inst, i)
            shadow_shots.append(dict(unit=key, facing=i, camera=cam,
                                     xml=camera(cam, base, frame, 0, inst, i * 360.0 / n),
                                     pivot=(frame[0] / 2, frame[1] / 2)))
    return subjects, shots, shadow_subjects, shadow_shots


def write_scene(path, subjects, shots, team=None, shadow=False):
    if shadow:
        head = ('<scene ambient="0 0 0" background="1 1 1">\n%s\n  <material id="catcher" color="1 1 1" shininess="1" specular="0"/>\n'
                % L.PALETTE.replace("{TEAM}", L.TEAM))
        lights = "  " + L.KEY_SUN
        # one catcher under every subject: slots run SPACING apart along +X from 0, one per
        # layer frame and per shadow, so the plane spans far past the last of them
        body = '  <box name="catcher" size="4000000 10 400000" pos="1990000 -5 0" material="catcher"/>\n'
        body += "".join('  <group renderable="0"><prefab source="%s" name="%s" pos="%.3f %.3f %.3f"/></group>\n'
                        % (src, inst, *anchor) for inst, src, anchor in subjects)
    else:
        head = '<scene %s background="0 0 0">\n%s\n' % (L.SCENE_ATTRS, L.PALETTE.replace("{TEAM}", team))
        lights = L.LIGHTS
        body = "".join('  <prefab source="%s" name="%s" pos="%.3f %.3f %.3f" color="%s"/>\n'
                       % (src, inst, *anchor, team) for inst, src, anchor in subjects)
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<!-- GENERATED by sprites.py -->\n' + head + lights + "\n\n"
           + "\n".join(s["xml"] for s in shots) + "\n\n" + body + "</scene>\n")
    path.write_text(xml)


def render(scene, outdir, time=None):
    if outdir.exists():
        shutil.rmtree(outdir)
    cmd = [SCENER, "--render", str(scene), "--format", "png", "--output-dir", str(outdir),
           "--supersample", str(SUPERSAMPLE)]
    if time is not None:
        cmd += ["--time", "%g" % time]
    if "shadow" not in scene.name:
        cmd.append("--transparent")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit("scener failed on %s:\n%s" % (scene, r.stderr or r.stdout))


def shadow_alpha(img):
    grey = img.convert("L")
    hist = grey.histogram()
    lit = max(range(256), key=lambda v: hist[v]) or 1      # the open catcher: its most common value
    def alpha(v):
        dark = 1 - v / lit
        return 0 if dark < 0.04 else max(0, min(255, round(255 * dark * SHADOW_OPACITY)))
    a = grey.point(alpha)
    out = Image.new("RGBA", img.size, (0, 0, 0, 0))
    out.putalpha(a)
    return out


def pack(frames, pivots):
    """Crop frames to their union bounds and lay them out in a grid; returns sheet and meta."""
    box = None
    for f in frames:
        b = f.getchannel("A").getbbox()
        if b:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    box = box or (0, 0, 1, 1)
    w, h = box[2] - box[0], box[3] - box[1]
    n = len(frames)
    cols = n if n <= 4 else int(math.ceil(math.sqrt(n)))
    rows = int(math.ceil(n / cols))
    sheet = Image.new(frames[0].mode, (cols * w, rows * h), (0, 0, 0, 0) if frames[0].mode == "RGBA" else 0)
    for i, f in enumerate(frames):
        sheet.paste(f.crop(box), ((i % cols) * w, (i // cols) * h))
    px, py = pivots[0]
    return sheet, dict(frame=[w, h], columns=cols, count=n, pivot=[px - box[0], py - box[1]]), box


def soot_field(w, h, seed, cell=11):
    """Smooth value noise in 0..1, about `cell` pixels to a feature: irregular soot patches."""
    rnd = random.Random(3000 + seed)
    gw, gh = w // cell + 2, h // cell + 2
    grid = [[rnd.random() for _ in range(gw)] for _ in range(gh)]
    fine = [[rnd.random() for _ in range(gw * 2 + 2)] for _ in range(gh * 2 + 2)]
    def sample(g, x, y, c):
        fx, fy = x / c, y / c
        x0, y0 = int(fx), int(fy)
        tx, ty = fx - x0, fy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a, b = g[y0][x0], g[y0][x0 + 1]
        c0, d = g[y0 + 1][x0], g[y0 + 1][x0 + 1]
        return (a * (1 - tx) + b * tx) * (1 - ty) + (c0 * (1 - tx) + d * tx) * ty
    return [[0.72 * sample(grid, x, y, cell) + 0.28 * sample(fine, x, y, cell / 2) for x in range(w)]
            for y in range(h)]


def scorch(img, seed, colour=None):
    """A battle-damaged copy of a structure's picture: irregular soot (darkened where a noise
    field runs high), burned-through holes at its peaks with a few embers in them. The team mask
    (colour given) takes the same soot, so team paint is burned too; embers only go on the
    colour picture. The same seed gives the same damage on picture and mask."""
    src = colour or img
    w, h = img.size
    alpha = src.getchannel("A")
    out = img.copy()
    px, apx = out.load(), alpha.load()
    field = soot_field(w, h, seed)
    rnd = random.Random(4000 + seed)
    for y in range(h):
        for x in range(w):
            if apx[x, y] < 128:
                continue
            n = field[y][x]
            if n < 0.56:
                continue
            t = min(1.0, (n - 0.56) / 0.16)
            k = 1 - 0.72 * t * t * (3 - 2 * t)
            c = px[x, y]
            if n > 0.76 and colour is None and rnd.random() < 0.025:
                px[x, y] = (255, 120 + rnd.randrange(80), 30, c[3])         # an ember
            elif isinstance(c, tuple):
                px[x, y] = (int(c[0] * k), int(c[1] * k), int(c[2] * k), c[3])
            else:
                px[x, y] = int(c * k)
    return out


def pack_layer(base, black, white, pivots):
    """Packs colour frames and their team masks (white - black) into sprites/<base>.png and
    <base>_team.png; returns the sheet's manifest entry."""
    sheet, meta, box = pack(black, pivots)
    tsheet = Image.new("L", sheet.size, 0)
    cols, (fw, fh) = meta["columns"], meta["frame"]
    for i, (b, w) in enumerate(zip(black, white)):
        d = ImageChops.subtract(w.convert("RGB"), b.convert("RGB")).convert("L")
        tsheet.paste(d.crop(box), ((i % cols) * fw, (i // cols) * fh))
    sheet.save(OUT / (base + ".png"), optimize=True)
    tsheet.save(OUT / (base + "_team.png"), optimize=True)
    meta.update(file=base + ".png", team=base + "_team.png")
    return meta


def infantry_scene(path, inst, prefab, anim, cams, team=None, shadow=False):
    _, attrs, children, _ = anim
    tag = '<prefab source="%s" name="%s" rot="%s"%s%s>%s</prefab>' % (
        prefab, inst, INFANTRY_ROT, attrs, "" if shadow else ' color="%s"' % team, children)
    if shadow:
        head = ('<scene ambient="0 0 0" background="1 1 1">\n%s\n  <material id="catcher" color="1 1 1" shininess="1" specular="0"/>\n'
                % L.PALETTE.replace("{TEAM}", L.TEAM))
        body = ('  <box name="catcher" size="20000 10 20000" pos="0 -5 0" material="catcher"/>\n'
                '  <group renderable="0">%s</group>\n' % tag)
        lights = "  " + L.KEY_SUN
    else:
        head = '<scene %s background="0 0 0">\n%s\n' % (L.SCENE_ATTRS, L.PALETTE.replace("{TEAM}", team))
        body = "  " + tag + "\n"
        lights = L.LIGHTS
    path.write_text('<?xml version="1.0" encoding="UTF-8"?>\n<!-- GENERATED by sprites.py -->\n'
                    + head + lights + "\n\n" + "\n".join(cams) + "\n\n" + body + "</scene>\n")


def render_infantry(key):
    """Every animation frame at every facing, in three passes; returns the manifest entry."""
    u = INFANTRY[key]
    n = u["facings"]
    inst = key
    k = round(u["centre"] * math.cos(math.radians(L.ELEV)) / SCALE)
    w, h = u["frame"]
    cams = [camera("%s_%02d" % (inst, i), (0, 0, 0), (w, h), k, inst, i * 360.0 / n) for i in range(n)]
    scams = [camera("%s_%02d" % (inst, i), (0, 0, 0), (w * 2, h * 2), 0, inst, i * 360.0 / n) for i in range(n)]
    black, white, shadows, anims = [], [], [], {}
    for anim in u["anims"]:
        name, _, _, times = anim
        anims[name] = dict(start=len(black), frames=len(times), facings=n)
        for p, team in (("black", "0 0 0"), ("white", "1 1 1"), ("shadow", None)):
            infantry_scene(WORK / ("%s_%s_%s.blks" % (key, name, p)), inst, u["prefab"], anim,
                           scams if p == "shadow" else cams, team=team, shadow=p == "shadow")
        # facing-major within an animation: index = start + facing * frames + frame
        grid = {p: [[None] * len(times) for _ in range(n)] for p in ("black", "white", "shadow")}
        for f, t in enumerate(times):
            for p in ("black", "white", "shadow"):
                out = WORK / ("%s_%s_%s_%d" % (key, name, p, f))
                render(WORK / ("%s_%s_%s.blks" % (key, name, p)), out, t)
                for i in range(n):
                    img = Image.open(out / ("%s_%02d.png" % (inst, i)))
                    grid[p][i][f] = shadow_alpha(img) if p == "shadow" else img.convert("RGBA")
        for i in range(n):
            black += grid["black"][i]
            white += grid["white"][i]
            shadows += grid["shadow"][i]
    pivot = (w / 2, h / 2 + k)
    entry = dict(facings=n, anims=anims, layers={"body": pack_layer("%s_body" % key, black, white, [pivot] * len(black))})
    sheet, meta, _ = pack(shadows, [(w, h)] * len(shadows))
    sheet.save(OUT / ("%s_shadow.png" % key), optimize=True)
    meta.update(file="%s_shadow.png" % key)
    entry["shadow"] = meta
    return entry


def prepare_work():
    """The build folder: a fresh copy of prefabs/ (frame variants are written into it) beside
    links to the libraries."""
    WORK.mkdir(exist_ok=True)
    work_prefabs = WORK / "prefabs"
    if work_prefabs.is_symlink():
        work_prefabs.unlink()
    elif work_prefabs.exists():
        shutil.rmtree(work_prefabs)
    shutil.copytree(ART / "prefabs", work_prefabs)
    for name in ("kit.xml", "utilities.xml"):
        link = WORK / name
        if not link.exists():
            link.symlink_to(Path("..") / name)


def main(argv):
    keys = argv or ALL_UNITS
    unknown = [k for k in keys if k not in ALL_UNITS]
    if unknown:
        sys.exit("unknown units: %s (have %s)" % (" ".join(unknown), " ".join(ALL_UNITS)))
    infantry = [k for k in keys if k in INFANTRY]
    keys = [k for k in keys if k in UNITS]
    prepare_work()
    OUT.mkdir(exist_ok=True)

    subjects, shots, shadow_subjects, shadow_shots = plan(keys)
    if not keys:
        shots = shadow_shots = []
    write_scene(WORK / "black.blks", subjects, shots, team="0 0 0")
    write_scene(WORK / "white.blks", subjects, shots, team="1 1 1")
    write_scene(WORK / "shadow.blks", shadow_subjects, shadow_shots, shadow=True)
    for p in ("black", "white", "shadow") if keys else ():
        print("rendering %s pass (%d frames)" % (p, len(shadow_shots if p == "shadow" else shots)))
        render(WORK / ("%s.blks" % p), WORK / p)

    manifest_path = OUT / "sprites.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest["_order"] = [k for k in ALL_UNITS if k in manifest or k in keys or k in infantry]
    manifest["_view"] = dict(yaw=L.YAW, elevation=round(L.ELEV, 4), cm_per_px=round(SCALE, 4), tile=[L.TILE_W, L.TILE_H],
                             facing0="+X of the prefab", facing_turn="counter-clockwise seen from above",
                             team="pixel = base + team_colour * mask / 255 (sRGB)")
    for key in keys:
        u = UNITS[key]
        entry = dict(facings=u["facings"], layers={})
        for ly in u["layers"]:
            # lump order: facing-major, frame-minor (lump = facing * frames + frame)
            ls = sorted((s for s in shots if s["unit"] == key and s["layer"] == ly["name"]),
                        key=lambda s: (s["facing"], s["frame"]))
            black = [Image.open(WORK / "black" / (s["camera"] + ".png")).convert("RGBA") for s in ls]
            white = [Image.open(WORK / "white" / (s["camera"] + ".png")).convert("RGBA") for s in ls]
            nframes = len(ly["frames"])
            if ly["damaged"]:
                # scorched copies of the animation frames, between them and the build stages
                anim = ly["anim"]
                black = black[:anim] + [scorch(b, i) for i, b in enumerate(black[:anim])] + black[anim:]
                white = white[:anim] + [scorch(w, i, black[i]) for i, w in enumerate(white[:anim])] + white[anim:]
                ls = ls[:anim] + ls[:anim] + ls[anim:]
                nframes += anim
            meta = pack_layer("%s_%s" % (key, ly["name"]), black, white, [s["pivot"] for s in ls])
            meta.update(frames=nframes, facings=len(ls) // nframes)
            if ly["structure"]:
                # Frame layout of a structure's sheet: animation, scorched (damaged), build stages.
                meta.update(anim=ly["anim"], damaged=ly["anim"] if ly["damaged"] else 0)
                if not ly["build_sheet"] and any("build" in k for k in ly["frames"]):
                    meta["build_at"] = nframes - len(BUILD_STAGES)
            if ly["mount"]:
                n = u["facings"]
                meta["mount_px"] = [[round(c, 2) for c in to_px(turn(ly["mount"], i * 360.0 / n))] for i in range(n)]
            entry["layers"][ly["name"]] = meta
        ss = [s for s in shadow_shots if s["unit"] == key]
        frames = [shadow_alpha(Image.open(WORK / "shadow" / (s["camera"] + ".png"))) for s in ss]
        sheet, meta, _ = pack(frames, [s["pivot"] for s in ss])
        sheet.save(OUT / ("%s_shadow.png" % key), optimize=True)
        meta.update(file="%s_shadow.png" % key)
        entry["shadow"] = meta
        manifest[key] = entry
        print("%-10s %s" % (key, ", ".join("%s %dx%d x%d" % (n, *m["frame"], m["count"]) for n, m in entry["layers"].items())))
    for key in infantry:
        print("rendering %s (%d animations x %d facings)" % (key, len(INFANTRY[key]["anims"]), INFANTRY[key]["facings"]))
        manifest[key] = render_infantry(key)
        m = manifest[key]["layers"]["body"]
        print("%-10s body %dx%d x%d (%s)" % (key, *m["frame"], m["count"],
              ", ".join("%s %d" % (a, v["frames"]) for a, v in manifest[key]["anims"].items())))
    manifest_path.write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")


if __name__ == "__main__":
    main(sys.argv[1:])
