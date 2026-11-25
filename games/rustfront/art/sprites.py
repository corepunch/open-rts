#!/usr/bin/env python3
"""Renders Rustfront's game sprites with Scener and packs them into sheets with a manifest.

    python3 games/rustfront/art/sprites.py            # every unit
    python3 games/rustfront/art/sprites.py tank hq    # just these

Output (games/rustfront/sprites/):
    <unit>_<layer>.png        colour frames, team parts rendered black
    <unit>_<layer>_team.png   team mask: how much team colour each pixel takes (greyscale)
    <unit>_shadow.png         ground shadow, black with alpha
    sprites.json              frame size, sheet columns, pivots, facings, turret mounts

Three passes share one sprite view (layout.py's yaw, elevation and cm per pixel):
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
starting from the prefab's front (+X for vehicles and turret heads). Pivots are the
screen position of the layer's anchor (a unit's ground origin, a turret's axis at mount
height) in its cropped frame, on a pixel corner.
"""
import json
import math
import os
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
SUPERSAMPLE = 3
SHADOW_OPACITY = 0.6     # darkest shadow alpha: the ground still gets ambient light
SPACING = 2400.0          # cm between subjects along the camera's right vector
SCENER = os.environ.get("SCENER", "scener")


def layer(name, prefab, frame, centre, mount=None, facings=None):
    return dict(name=name, prefab=prefab, frame=frame, centre=centre, mount=mount, facings=facings)


# key: shadow (assembled prefab), facings of the unit, layers
UNITS = {
    "hq":        dict(shadow="buildings/hq",       facings=1,  layers=[layer("body", "buildings/hq", (640, 608), 140)]),
    "power":     dict(shadow="buildings/power",    facings=1,  layers=[layer("body", "buildings/power", (448, 448), 100)]),
    "refinery":  dict(shadow="buildings/refinery", facings=1,  layers=[layer("body", "buildings/refinery", (640, 544), 110)]),
    "barracks":  dict(shadow="buildings/barracks", facings=1,  layers=[layer("body", "buildings/barracks", (448, 448), 80)]),
    "factory":   dict(shadow="buildings/factory",  facings=1,  layers=[layer("body", "buildings/factory", (640, 608), 140)]),
    "turret":    dict(shadow="buildings/turret",   facings=1,  layers=[
                     layer("base", "buildings/turret/base", (256, 256), 40),
                     layer("head", "buildings/turret/head", (256, 224), 0, mount=(0, 98, 0), facings=16)]),
    "scout":     dict(shadow="vehicles/scout",     facings=16, layers=[layer("body", "vehicles/scout", (320, 320), 50)]),
    "tank":      dict(shadow="vehicles/tank",      facings=16, layers=[
                     layer("hull", "vehicles/tank/hull", (384, 352), 45),
                     layer("turret", "vehicles/tank/turret", (448, 448), 0, mount=(-12, 102, 0))]),
    "artillery": dict(shadow="vehicles/artillery", facings=16, layers=[layer("body", "vehicles/artillery", (512, 448), 70)]),
    "harvester": dict(shadow="vehicles/harvester", facings=16, layers=[layer("body", "vehicles/harvester", (448, 384), 70)]),
    "apc":       dict(shadow="vehicles/apc",       facings=16, layers=[
                     layer("hull", "vehicles/apc/hull", (384, 352), 50),
                     layer("turret", "vehicles/apc/turret", (256, 224), 0, mount=(22, 112, 0))]),
}


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
    return (dot(p, RIGHT) / L.CM_PER_PX, -dot(p, UP) / L.CM_PER_PX)


def camera(name, anchor, frame, k, rot_target=None, deg=0.0):
    """An ortho camera whose image puts `anchor` at (W/2, H/2 + k) pixels."""
    look = [anchor[i] + UP[i] * k * L.CM_PER_PX for i in range(3)]
    pos = [look[i] + 4000 * TO_CAM[i] for i in range(3)]
    f = lambda v: " ".join("%.3f" % c for c in v)
    w, h = frame
    xml = '  <camera name="%s" pos="%s" look="%s" ortho="%g" size="%d %d"' % (
        name, f(pos), f(look), h * L.CM_PER_PX, w, h)
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
            inst = "%s_%s" % (key, ly["name"])
            subjects.append((inst, ly["prefab"], anchor))
            k = round(ly["centre"] * math.cos(math.radians(L.ELEV)) / L.CM_PER_PX)
            for i in range(n):
                cam = "%s_%02d" % (inst, i)
                shots.append(dict(unit=key, layer=ly["name"], facing=i, camera=cam,
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


def render(scene, outdir):
    if outdir.exists():
        shutil.rmtree(outdir)
    cmd = [SCENER, "--render", str(scene), "--format", "png", "--output-dir", str(outdir),
           "--supersample", str(SUPERSAMPLE)]
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


def main(argv):
    keys = argv or list(UNITS)
    unknown = [k for k in keys if k not in UNITS]
    if unknown:
        sys.exit("unknown units: %s (have %s)" % (" ".join(unknown), " ".join(UNITS)))
    WORK.mkdir(exist_ok=True)
    for name in ("prefabs", "kit.xml", "utilities.xml"):
        link = WORK / name
        if not link.exists():
            link.symlink_to(Path("..") / name)
    OUT.mkdir(exist_ok=True)

    subjects, shots, shadow_subjects, shadow_shots = plan(keys)
    write_scene(WORK / "black.blks", subjects, shots, team="0 0 0")
    write_scene(WORK / "white.blks", subjects, shots, team="1 1 1")
    write_scene(WORK / "shadow.blks", shadow_subjects, shadow_shots, shadow=True)
    for p in ("black", "white", "shadow"):
        print("rendering %s pass (%d frames)" % (p, len(shadow_shots if p == "shadow" else shots)))
        render(WORK / ("%s.blks" % p), WORK / p)

    manifest_path = OUT / "sprites.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    manifest["_order"] = [k for k in UNITS if k in manifest or k in keys]
    manifest["_view"] = dict(yaw=L.YAW, elevation=L.ELEV, cm_per_px=L.CM_PER_PX,
                             facing0="+X of the prefab", facing_turn="counter-clockwise seen from above",
                             team="pixel = base + team_colour * mask / 255 (sRGB)")
    for key in keys:
        u = UNITS[key]
        entry = dict(facings=u["facings"], layers={})
        for ly in u["layers"]:
            ls = [s for s in shots if s["unit"] == key and s["layer"] == ly["name"]]
            black = [Image.open(WORK / "black" / (s["camera"] + ".png")).convert("RGBA") for s in ls]
            white = [Image.open(WORK / "white" / (s["camera"] + ".png")).convert("RGBA") for s in ls]
            masks = []
            for b, w in zip(black, white):
                d = ImageChops.subtract(w.convert("RGB"), b.convert("RGB")).convert("L")
                m = Image.new("LA", b.size)
                m.putalpha(b.getchannel("A"))
                m = Image.merge("LA", (d, b.getchannel("A")))
                masks.append(m.convert("RGBA"))
            sheet, meta, box = pack(black, [s["pivot"] for s in ls])
            tsheet = Image.new("L", sheet.size, 0)
            cols, (fw, fh) = meta["columns"], meta["frame"]
            for i, m in enumerate(masks):
                tsheet.paste(m.crop(box).getchannel("R"), ((i % cols) * fw, (i // cols) * fh))
            base = "%s_%s" % (key, ly["name"])
            sheet.save(OUT / (base + ".png"), optimize=True)
            tsheet.save(OUT / (base + "_team.png"), optimize=True)
            meta.update(file=base + ".png", team=base + "_team.png")
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
    manifest_path.write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n")


if __name__ == "__main__":
    main(sys.argv[1:])
