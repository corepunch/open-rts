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


def layer(name, prefab, frame, centre, mount=None, facings=None, frames=None):
    """frame is authored in preview pixels (1.5 cm); it is converted to game pixels, even.
    frames lists the layer's animation frames as knob settings: each dict overrides the
    prefab's <let name value> knobs (move, fire, spin, blink...) for one frame."""
    fw, fh = (int(math.ceil(v * 1.5 / SCALE / 2)) * 2 for v in frame)
    return dict(name=name, prefab=prefab, frame=(fw, fh), centre=centre, mount=mount, facings=facings,
                frames=frames or [{}])


def variant(prefab, knobs, index):
    """A copy of the prefab in the build folder with its knobs set; returns its source name."""
    if not knobs:
        return prefab
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


# key: shadow (assembled prefab), facings of the unit, layers
UNITS = {
    "hq":        dict(shadow="buildings/hq",       facings=1,  layers=[layer("body", "buildings/hq", (640, 608), 140,
                     frames=[{"spin": k, "blink": int(k % 4 < 2)} for k in range(8)])]),
    "power":     dict(shadow="buildings/power",    facings=1,  layers=[layer("body", "buildings/power", (448, 448), 100,
                     frames=[{"pulse": 0, "blink": 1}, {"pulse": 1, "blink": 1}, {"pulse": 0, "blink": 0}, {"pulse": 1, "blink": 0}])]),
    "refinery":  dict(shadow="buildings/refinery", facings=1,  layers=[layer("body", "buildings/refinery", (640, 544), 110,
                     frames=[{"flick": 0, "blink": 1}, {"flick": 1, "blink": 1}, {"flick": 0, "blink": 0}, {"flick": 1, "blink": 0}])]),
    "barracks":  dict(shadow="buildings/barracks", facings=1,  layers=[layer("body", "buildings/barracks", (448, 448), 80, frames=BLINK)]),
    "factory":   dict(shadow="buildings/factory",  facings=1,  layers=[layer("body", "buildings/factory", (640, 608), 140, frames=BLINK)]),
    "turret":    dict(shadow="buildings/turret",   facings=1,  layers=[
                     # two base frames: the firing run steps the body's frame, and the head follows it
                     layer("base", "buildings/turret/base", (256, 256), 40, frames=[{}, {}]),
                     layer("head", "buildings/turret/head", (288, 240), 0, mount=(0, 98, 0), facings=16, frames=FIRE)]),
    "scout":     dict(shadow="vehicles/scout",     facings=16, layers=[layer("body", "vehicles/scout", (320, 320), 50, frames=MOVE_FIRE)]),
    "tank":      dict(shadow="vehicles/tank",      facings=16, layers=[
                     layer("hull", "vehicles/tank/hull", (384, 352), 45, frames=MOVE),
                     layer("turret", "vehicles/tank/turret", (512, 512), 0, mount=(-12, 102, 0), frames=FIRE)]),
    "artillery": dict(shadow="vehicles/artillery", facings=16, layers=[layer("body", "vehicles/artillery", (640, 576), 70, frames=MOVE_FIRE)]),
    "harvester": dict(shadow="vehicles/harvester", facings=16, layers=[layer("body", "vehicles/harvester", (448, 384), 70, frames=MOVE)]),
    "apc":       dict(shadow="vehicles/apc",       facings=16, layers=[
                     layer("hull", "vehicles/apc/hull", (384, 352), 50, frames=MOVE),
                     layer("turret", "vehicles/apc/turret", (288, 256), 0, mount=(22, 112, 0), frames=FIRE)]),
}

# Infantry: one animated layer, 8 facings. Each animation is the instance's children (a gait,
# layers playing the soldier's own poses and clips) sampled at fixed --time values. The
# soldier prefab is Z up facing -Y; rot="-90 90 0" stands it in this Y-up world facing +X.
WALK_CYCLE, WALK_FROM, WALK_FRAMES = 1.10, 1.20, 6      # a steady spot-gait stride, measured
INFANTRY_ROT = "-90 90 0"
ANIMS = [
    ("idle", ' pose="Ready"', "", [0.0]),
    ("walk", "", '<gait mode="spot" start="0"/><layer pose="Ready" mask="left_collarbone right_collarbone weapon"/>',
     [round(WALK_FROM + i * WALK_CYCLE / WALK_FRAMES, 4) for i in range(WALK_FRAMES)]),
    ("fire", "", '<layer clip="Fire"/><option name="flash" enabled="1"/>', [0.05]),
    ("fire2", "", '<layer clip="Fire"/>', [0.15]),
    ("die", "", '<layer clip="Die"/>', [0.1, 0.22, 0.34, 0.46, 0.6, 0.8]),
]
INFANTRY = {
    "rifleman": dict(prefab="infantry/rifleman", facings=8, frame=(112, 96), centre=60, anims=ANIMS),
    "rocket":   dict(prefab="infantry/rocket", facings=8, frame=(120, 96), centre=60, anims=ANIMS),
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
                subjects.append((inst, variant(ly["prefab"], knobs, f), anchor))
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
        body = '  <box name="catcher" size="200000 10 200000" pos="0 -5 0" material="catcher"/>\n'
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
            meta = pack_layer("%s_%s" % (key, ly["name"]), black, white, [s["pivot"] for s in ls])
            meta.update(frames=len(ly["frames"]), facings=len(ls) // len(ly["frames"]))
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
