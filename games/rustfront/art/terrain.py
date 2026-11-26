#!/usr/bin/env python3
"""Makes Rustfront's terrain tiles from art/terrain.txt: procedural textures, Scener-modelled
tiles, and the transition overlays that blend one terrain into the next.

    python3 games/rustfront/art/terrain.py            # everything
    python3 games/rustfront/art/terrain.py --no-models  # reuse the last model renders

Output (games/rustfront/sprites/):
    terrain.png    RGBA atlas of TILE_W x TILE_H tiles, ATLAS_COLS across. Ground tiles are
                   opaque; transition tiles are transparent where they do not cover.
    terrain.json   every terrain's tiles in the atlas, read by pack8.py, which writes the
                   game's assets/terrain.pcx and assets/terrain.txt.

Tiles. A cell is 200 cm square, which the game view shows as 48 x 32 pixels. Textures are
drawn in that space and wrap at the tile edge, so a field of one terrain has no seams. A
model tile is the prefab rendered at game scale (layout.py's view, sprites.py's camera), a
transparent colour pass composited over its ground's texture with a shadow pass.

Transitions (the Dark Reign scheme). Terrains stack by layer. A cell draws its own tile,
then, lowest layer first, every higher terrain among its eight neighbours spills over its
edges: one edge tile for the sides that neighbour touches (N 1, E 2, S 4, W 8) and one corner
tile for the diagonals it touches alone (NE 1, SE 2, SW 4, NW 8), so 15 edge and 15 corner
tiles per spilling terrain. A spill covers the pixels nearer the side (or the corner) than
edge * (1 + jag * noise); the noise wraps with the tile, and an edge tile and a corner tile
reach the same depth where they meet, so the spill runs on unbroken across cells. The spill
shows the upper terrain's first texture tile, so it joins that terrain's own cells.
"""
import json
import math
import random
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops

ART = Path(__file__).resolve().parent
sys.path.insert(0, str(ART))
import layout as L  # noqa: E402
import relief as RL  # noqa: E402
import sprites as S  # noqa: E402

CONFIG = ART / "terrain.txt"
OUT = ART.parent / "sprites"
WORK = ART / ".build" / "terrain"
TILE_W, TILE_H = L.TILE_W, L.TILE_H
ATLAS_COLS = 16
EDGE_BITS = {"n": 1, "e": 2, "s": 4, "w": 8}
CORNER_BITS = {"ne": 1, "se": 2, "sw": 4, "nw": 8}
RIM_WIDTH = 0.05          # cells: how deep the rim shading reaches into a spill
RELIEF = None             # the config's relief line: cliffs and ramps (relief.py)


# ── config ─────────────────────────────────────────────────────────────────

def parse_config(path=CONFIG):
    """[terrain dicts in file order], default name."""
    terrains, default = [], None
    global RELIEF
    for n, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.strip()       # comments are whole lines: '#' is also a map letter
        if not line or line.startswith("#"):
            continue
        words = line.split()
        if words[0] == "default" and len(words) == 2:
            default = words[1]
            continue
        if words[0] == "relief":
            RELIEF = {k: float(v) if "." in v else int(v) for k, v in (w.split("=", 1) for w in words[1:])}
            continue
        if words[0] != "terrain" or len(words) < 4 or len(words[2]) != 1:
            sys.exit("%s:%d: expected 'terrain NAME CHAR key=value...'" % (path, n))
        t = dict(name=words[1], char=words[2], variants=1, period=1, seed=0, edge=0.0, jag=0.5, grain=6,
                 rim=1.0, flags=set())
        for w in words[3:]:
            if "=" in w:
                k, v = w.split("=", 1)
                t[k] = float(v) if k in ("edge", "jag", "rim") else int(v) if k in (
                    "layer", "variants", "period", "seed", "grain") else v
            else:
                t["flags"].add(w)
        if "layer" not in t or ("texture" in t) == ("model" in t):
            sys.exit("%s:%d: %s needs a layer and one of texture= or model=" % (path, n, t["name"]))
        terrains.append(t)
    names = {t["name"] for t in terrains}
    chars = [t["char"] for t in terrains]
    if len(set(chars)) != len(chars):
        sys.exit("%s: two terrains share a map letter" % path)
    for t in terrains:
        if "ground" in t and t["ground"] not in names:
            sys.exit("%s: %s stands on unknown ground %s" % (path, t["name"], t["ground"]))
    if default not in names:
        sys.exit("%s: 'default NAME' must name a terrain" % path)
    return terrains, default


# ── noise ──────────────────────────────────────────────────────────────────

def periodic_noise(w, h, seed, octaves=((4, 1.0), (8, 0.5), (16, 0.25))):
    """Value noise in 0..1 that wraps at the tile edge, so tiles join without seams. Octaves
    are (cells across, amplitude); the cells down keep the features square in the game view."""
    rnd = random.Random(seed)
    field = [[0.0] * w for _ in range(h)]
    total = sum(a for _, a in octaves)
    for cells, amp in octaves:
        cx, cy = cells, max(1, round(cells * h / w))
        grid = [[rnd.random() for _ in range(cx)] for _ in range(cy)]
        for y in range(h):
            fy = y * cy / h
            y0, ty = int(fy), fy - int(fy)
            sy = ty * ty * (3 - 2 * ty)
            for x in range(w):
                fx = x * cx / w
                x0, tx = int(fx), fx - int(fx)
                sx = tx * tx * (3 - 2 * tx)
                a = grid[y0 % cy][x0 % cx]
                b = grid[y0 % cy][(x0 + 1) % cx]
                c = grid[(y0 + 1) % cy][x0 % cx]
                d = grid[(y0 + 1) % cy][(x0 + 1) % cx]
                field[y][x] += amp * ((a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy)
    return [[v / total for v in row] for row in field]


def shade(rgb, k):
    return tuple(max(0, min(255, round(c * k))) for c in rgb)


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def put(px, x, y, c):
    px[x % TILE_W, y % TILE_H] = c


# ── texture recipes: recipe(seed) -> RGB tile ──────────────────────────────

# Each recipe paints a SHEET of period x period cells that wraps at its edges, and the game
# gives each cell its slice of the sheet (x mod period, y mod period), so a field of one terrain
# shows broad patches, dunes and strata instead of one tile stamped over and over. Colours come
# from ramps with hue shifts, as a painter would mix them: shadows lean red or cool, lights
# warm, so the ground reads rich in the shared palette's few colours.

def putw(px, w, h, x, y, c):
    """put() on a sheet: wraps at the sheet's edges."""
    px[x % w, y % h] = c


def ramp(stops, t):
    """A colour from (position, rgb) stops at t in 0..1."""
    t = max(0.0, min(1.0, t))
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            return mix(ca, cb, (t - a) / (b - a) if b > a else 0.0)
    return stops[-1][1]


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def sheet_noise(period, seed, octaves):
    """Wrapping noise over a sheet of period x period cells; octaves are (features across one
    cell, amplitude), and fractional ones (broad patches across the sheet) are allowed."""
    w, h = TILE_W * period, TILE_H * period
    return periodic_noise(w, h, seed, tuple((max(1, round(c * period)), a) for c, a in octaves))


def stretch(v, k):
    return max(0.0, min(1.0, 0.5 + (v - 0.5) * k))


def slices(img, period):
    """The sheet cut into its cells, row by row: the game's variant (y % p) * p + x % p."""
    return [img.crop((x * TILE_W, y * TILE_H, (x + 1) * TILE_W, (y + 1) * TILE_H))
            for y in range(period) for x in range(period)]


def new_sheet(period):
    img = Image.new("RGB", (TILE_W * period, TILE_H * period))
    return img, img.load(), TILE_W * period, TILE_H * period


SAND_RAMP = [(0.0, (128, 98, 74)), (0.30, (166, 132, 96)), (0.55, (192, 160, 118)),
             (0.80, (212, 184, 140)), (1.0, (230, 208, 166))]
ORE_SOIL = (132, 70, 44)
ORE = [(70, 36, 18), (172, 104, 28), (238, 182, 58), (255, 246, 190)]   # edge, shaded face, lit face, glint


def sand_sheet(seed, period):
    """Warm desert sand: broad pale and dusky patches, wind ripples whose crests catch the
    light and whose lee falls into shade, and a sprinkle of grains."""
    img, px, w, h = new_sheet(period)
    big = sheet_noise(period, seed, ((0.25, 1.0), (0.5, 0.8), (1, 0.5), (2, 0.3), (4, 0.15)))
    warp = sheet_noise(period, seed + 3, ((0.75, 1.0), (1.5, 0.5)))
    fine = sheet_noise(period, seed + 5, ((8, 1.0), (16, 0.5)))
    rnd = random.Random(seed)
    kx, ky = 2 * period, 5 * period            # ripples: whole waves across the sheet, so it wraps
    for y in range(h):
        for x in range(w):
            v = stretch(big[y][x], 1.25)
            r = math.sin(math.tau * (kx * x / w + ky * y / h) + 7 * warp[y][x])
            ripple = 0.06 * smooth(0.55, 0.95, r) - 0.04 * smooth(0.2, -0.6, r)
            t = 0.3 + 0.45 * v + ripple * (0.6 + 0.8 * v) + 0.08 * (fine[y][x] - 0.5)
            if rnd.random() < 0.035:
                t += rnd.choice((-0.16, 0.12))
            px[x, y] = ramp(SAND_RAMP, t)
    return img


def sand(seed, period):
    return slices(sand_sheet(seed, period), period)


def ore(seed, period, base=None):
    """Ore on the sand it lies in: the ground stained rust-red round clusters of amber crystals,
    each a faceted prism with a lit face, a shaded face, a dark outline and a glint. Painted over
    the sand's own sheet, so a mined-out cell (which turns to sand) matches its neighbours."""
    img = base.copy() if base is not None else sand_sheet(seed, period)
    px, w, h = img.load(), img.size[0], img.size[1]
    stain = sheet_noise(period, seed + 11, ((0.75, 1.0), (1.5, 0.6), (3, 0.3)))
    for y in range(h):
        for x in range(w):
            k = 0.1 + 0.35 * smooth(0.4, 0.8, stain[y][x])
            px[x, y] = mix(px[x, y], ORE_SOIL, k)
    rnd = random.Random(seed * 13 + 5)
    clusters = []
    for cy in range(period):
        for cx in range(period):
            for _ in range(rnd.choice((2, 2, 3))):
                clusters.append((cx * TILE_W + rnd.randrange(5, TILE_W - 5), cy * TILE_H + rnd.randrange(9, TILE_H - 3)))
    clusters.sort(key=lambda c: c[1])          # back to front
    for x0, y0 in clusters:
        # rust-dark ground round the cluster's foot
        for dy in range(-3, 4):
            for dx in range(-7, 8):
                if (dx / 7.5) ** 2 + (dy / 3.5) ** 2 <= 1:
                    putw(px, w, h, x0 + dx + 1, y0 + dy, mix(px[(x0 + dx + 1) % w, (y0 + dy) % h], (74, 34, 20), 0.5))
        shards = sorted(((rnd.uniform(-0.6, 0.6), rnd.uniform(7, 13), rnd.uniform(2.4, 3.6), rnd.randint(-4, 4))
                         for _ in range(rnd.choice((3, 3, 4)))), key=lambda s: -s[1])
        for angle, length, width, off in shards:
            ax, ay = math.sin(angle), -math.cos(angle)           # along the shard, tip up
            for yy in range(-13, 2):
                for xx in range(-9, 10):
                    rx, ry = xx - off + 0.5, yy + 0.5
                    along = rx * ax + ry * ay
                    across = rx * -ay + ry * ax
                    if along < 0 or along > length:
                        continue
                    t = along / length
                    # a hexagonal prism seen side on: straight flanks, then a pointed cap
                    half = width if t < 0.62 else width * (1 - t) / 0.38
                    if abs(across) > half + 0.3:
                        continue
                    lit = across < -0.2                           # the face toward the sun
                    c = ORE[2] if lit else ORE[1]
                    if t >= 0.62 and lit:
                        c = mix(ORE[2], ORE[3], 0.55)             # the cap's lit facet
                    if abs(across) > half - 0.7 or t < 0.08:
                        c = ORE[0]                                # the dark edge that makes it a crystal
                    if lit and abs(across + half * 0.45) < 0.6 and 0.2 < t < 0.6:
                        c = ORE[3]                                # the glint down the lit face
                    putw(px, w, h, x0 + xx, y0 + yy, c)
    return slices(img, period)


def silt(seed, period):
    """Damp silt of a canyon floor: cooler and darker than the sand above, with wet hollows that
    hold a sheen of sky at their far edge, and grit."""
    img, px, w, h = new_sheet(period)
    big = sheet_noise(period, seed, ((0.34, 1.0), (0.67, 0.7), (1.5, 0.4), (3, 0.2), (6, 0.1)))
    wet = sheet_noise(period, seed + 40, ((1, 1.0), (2, 0.7), (4, 0.5), (8, 0.3)))
    grit = sheet_noise(period, seed + 41, ((6, 1.0), (12, 0.6)))
    rnd = random.Random(seed)
    stops = [(0.0, (70, 62, 52)), (0.4, (104, 92, 72)), (0.75, (132, 116, 90)), (1.0, (152, 136, 106))]
    for y in range(h):
        for x in range(w):
            t = 0.3 + 0.5 * stretch(big[y][x], 1.2) + 0.1 * (grit[y][x] - 0.5)
            wv = wet[y][x]
            c = ramp(stops, t)
            if wv > 0.68:
                c = mix(c, (74, 72, 66), 0.2 + 0.25 * smooth(0.68, 0.78, wv))
                if wet[(y - 1) % h][x] <= 0.68:  # the far lip of a damp hollow catches the sky
                    c = mix(c, (150, 150, 140), 0.35)
            if rnd.random() < 0.04:
                c = shade(c, rnd.choice((0.72, 1.16)))
            px[x, y] = c
    return slices(img, period)


def gravel(seed, period):
    """Gravel: grey-taupe grit packed with pebbles of many stones, each lit at its top left
    and grounded by a dark foot."""
    img, px, w, h = new_sheet(period)
    big = sheet_noise(period, seed, ((1, 1.0), (2, 0.5), (8, 0.3)))
    stops = [(0.0, (96, 84, 70)), (0.5, (138, 122, 100)), (1.0, (172, 156, 130))]
    for y in range(h):
        for x in range(w):
            px[x, y] = ramp(stops, 0.2 + 0.65 * stretch(big[y][x], 1.4))
    rnd = random.Random(seed * 7 + 1)
    stones = [(176, 168, 156), (156, 116, 86), (196, 184, 158), (112, 104, 98), (150, 140, 136), (170, 142, 104)]
    for _ in range(18 * period * period):
        x, y = rnd.randrange(w), rnd.randrange(h)
        c = rnd.choice(stones)
        rx, ry = rnd.choice((1, 1, 2, 2, 3)), rnd.choice((1, 1, 2))
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if (dx / (rx + 0.5)) ** 2 + (dy / (ry + 0.5)) ** 2 <= 1:
                    k = 1.18 if dx + dy < -1 else (0.8 if dx + dy > 1 else 1.0)
                    putw(px, w, h, x + dx, y + dy, shade(c, k))
        for dx in range(-rx, rx + 1):
            putw(px, w, h, x + dx + 1, y + ry + 1, shade(px[(x + dx + 1) % w, (y + ry + 1) % h], 0.62))
    return slices(img, period)


def grass(seed, period):
    """Dry steppe: sage and olive tufts with sun-bleached gold patches over red-brown earth."""
    img, px, w, h = new_sheet(period)
    cover = sheet_noise(period, seed + 50, ((1.33, 1.0), (2.67, 0.7), (5.33, 0.5), (8, 0.3)))
    dry = sheet_noise(period, seed + 60, ((0.67, 1.0), (1.33, 0.7), (2.67, 0.4)))
    n = sheet_noise(period, seed, ((4, 1.0), (8, 0.5), (16, 0.3)))
    earth = [(0.0, (112, 88, 60)), (1.0, (160, 132, 94))]
    green = [(0.0, (64, 82, 38)), (0.45, (100, 122, 52)), (0.8, (138, 152, 70)), (1.0, (174, 176, 96))]
    gold = [(0.0, (124, 106, 54)), (0.5, (170, 150, 80)), (1.0, (206, 188, 116))]
    for y in range(h):
        for x in range(w):
            c = stretch(cover[y][x], 1.8)
            t = 0.3 + 0.55 * n[y][x]
            g = mix(ramp(green, t), ramp(gold, t), 0.65 * smooth(0.5, 0.75, dry[y][x]))
            px[x, y] = mix(ramp(earth, t), g, 0.55 + 0.45 * smooth(0.1, 0.4, c))   # earth only peeks through
    rnd = random.Random(seed * 7 + 1)
    for _ in range(170 * period * period):     # blades: short strokes, lit tips, darker roots
        x, y = rnd.randrange(w), rnd.randrange(h)
        if cover[y][x] < 0.3 and rnd.random() < 0.85:
            continue
        base = px[x, y]
        lean = rnd.choice((-1, 0, 0, 1))
        tall = rnd.choice((2, 3, 3, 4))
        for i in range(tall):
            putw(px, w, h, x + (lean if i >= tall - 1 else 0), y - i, shade(base, 0.78 + 0.5 * i / tall))
        putw(px, w, h, x + 1, y + 1, shade(px[(x + 1) % w, (y + 1) % h], 0.74))
    return slices(img, period)


def stone(seed, period):
    """Flat bedrock: a cool grey slab with violet shadows and warm lights, faint strata, cracks
    with a lit lip, and spots of orange and sage lichen."""
    img, px, w, h = new_sheet(period)
    big = sheet_noise(period, seed, ((0.5, 1.0), (1, 0.8), (2, 0.4), (4, 0.25), (8, 0.12)))
    warp = sheet_noise(period, seed + 7, ((1, 1.0), (2, 0.5)))
    stops = [(0.0, (82, 76, 82)), (0.35, (118, 110, 108)), (0.7, (154, 144, 130)), (1.0, (186, 174, 152))]
    for y in range(h):
        for x in range(w):
            band = 0.05 * math.sin(math.tau * (3 * period * y / h) + 5 * warp[y][x])
            px[x, y] = ramp(stops, 0.18 + 0.72 * stretch(big[y][x], 1.5) + band)
    rnd = random.Random(seed * 7 + 1)
    for _ in range(period * period + 2):       # cracks: a wandering walk
        fx, fy = float(rnd.randrange(w)), float(rnd.randrange(h))
        angle = rnd.uniform(0, math.tau)
        for _ in range(rnd.randrange(10, 26)):
            x, y = round(fx), round(fy)
            putw(px, w, h, x, y, shade(px[x % w, y % h], 0.74))
            putw(px, w, h, x, y + 1, shade(px[x % w, (y + 1) % h], 1.08))
            angle += rnd.uniform(-0.6, 0.6)
            fx, fy = fx + math.cos(angle), fy + 0.66 * math.sin(angle)
    for _ in range(7 * period * period):       # lichen
        x, y = rnd.randrange(w), rnd.randrange(h)
        c = rnd.choice(((176, 118, 58), (124, 134, 90), (150, 150, 110)))
        for dx, dy in ((0, 0), (1, 0), (0, 1), (-1, 0), (1, 1)):
            if rnd.random() < 0.8:
                putw(px, w, h, x + dx, y + dy, mix(px[(x + dx) % w, (y + dy) % h], c, 0.7))
    return slices(img, period)


def water(seed, period):
    """Deep water: blue-teal depths, swells that lighten toward their crests, and glints."""
    img, px, w, h = new_sheet(period)
    big = sheet_noise(period, seed, ((0.5, 1.0), (1, 0.7), (2, 0.4)))
    warp = sheet_noise(period, seed + 9, ((1, 1.0), (2, 0.5)))
    stops = [(0.0, (16, 38, 58)), (0.45, (28, 70, 92)), (0.8, (46, 104, 120)), (1.0, (72, 136, 146))]
    for y in range(h):
        for x in range(w):
            swell = math.sin(math.tau * (period * x / w + 4 * period * y / h) + 6 * warp[y][x])
            px[x, y] = ramp(stops, 0.15 + 0.6 * stretch(big[y][x], 1.4) + 0.1 * smooth(0.5, 1.0, swell))
    rnd = random.Random(seed * 7 + 1)
    for _ in range(5 * period * period):       # crests and glints
        x, y = rnd.randrange(w), rnd.randrange(h)
        for i in range(rnd.randrange(3, 8)):
            putw(px, w, h, x + i, y, mix(px[(x + i) % w, y % h], (156, 200, 204), 0.6 if 0 < i < 4 else 0.3))
        if rnd.random() < 0.12:
            putw(px, w, h, x + 2, y - 1, (220, 236, 232))
    return slices(img, period)


RECIPES = {"silt": silt, "sand": sand, "gravel": gravel, "grass": grass, "stone": stone, "water": water, "ore": ore}


# ── model tiles (Scener) ───────────────────────────────────────────────────

def model_tiles(terrains, textures, render=True):
    """{terrain name: [RGB tile per variant]} for every model terrain."""
    models = [t for t in terrains if "model" in t]
    if not models:
        return {}
    shots, subjects, keys = [], [], []
    slot = 0
    if render:
        if WORK.exists():
            shutil.rmtree(WORK)
        (WORK / "prefabs").mkdir(parents=True)
        for name in ("kit.xml", "utilities.xml"):
            (WORK / name).symlink_to(ART / name)
    for t in models:
        src = (ART / "prefabs" / (t["model"] + ".blk")).read_text()
        for v in range(t["variants"]):
            name = "%s_%d" % (t["name"], v)
            if '<let name="v" value="0"/>' not in src:
                sys.exit("terrain.py: %s.blk has no <let name=\"v\" value=\"0\"/> knob" % t["model"])
            if render:
                text = src.replace('<let name="v" value="0"/>', '<let name="v" value="%d"/>' % v)
                (WORK / "prefabs" / (name + ".blk")).write_text(text)
            anchor = (slot * S.SPACING, 0.0, 0.0)
            slot += 1
            subjects.append((name, name, anchor))
            shots.append(dict(camera=name, xml=S.camera(name, anchor, (TILE_W, TILE_H), 0)))
            keys.append((t, v, name))
    colour, shadow = WORK / "colour", WORK / "shadow"
    if render:
        S.write_scene(WORK / "tiles.blks", subjects, shots, team=L.TEAM)
        S.write_scene(WORK / "tiles_shadow.blks", subjects, shots, shadow=True)
        S.render(WORK / "tiles.blks", colour)
        S.render(WORK / "tiles_shadow.blks", shadow)
    out = {}
    for t, v, name in keys:
        rgba = Image.open(colour / (name + ".png")).convert("RGBA")
        if "ground" in t:
            base_tiles = textures[t["ground"]]
            tile = base_tiles[v % len(base_tiles)].convert("RGBA")
            sh = S.shadow_alpha(Image.open(shadow / (name + ".png")))
            tile = Image.alpha_composite(tile, sh)
            alpha = rgba.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
            tile.paste(rgba.convert("RGB"), mask=alpha)
        else:
            tile = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 255))
            tile = Image.alpha_composite(tile, rgba)
        out.setdefault(t["name"], []).append(tile.convert("RGB"))
    return out


# ── transitions ────────────────────────────────────────────────────────────

def coverage(t, edges, corners, field):
    """Per pixel: None (uncovered) or how far inside the spill it lies, in cells."""
    out = [[None] * TILE_W for _ in range(TILE_H)]
    for y in range(TILE_H):
        v = (y + 0.5) / TILE_H
        for x in range(TILE_W):
            u = (x + 0.5) / TILE_W
            ds = []
            if edges & 1: ds.append(v)
            if edges & 2: ds.append(1 - u)
            if edges & 4: ds.append(1 - v)
            if edges & 8: ds.append(u)
            if corners & 1: ds.append(math.hypot(1 - u, v))
            if corners & 2: ds.append(math.hypot(1 - u, 1 - v))
            if corners & 4: ds.append(math.hypot(u, 1 - v))
            if corners & 8: ds.append(math.hypot(u, v))
            if not ds:
                continue
            reach = t["edge"] * (1 + t["jag"] * (2 * field[y][x] - 1))
            inside = reach - min(ds)
            if inside > 0:
                out[y][x] = inside
    return out


def transition_tile(t, source, edges, corners, field):
    cov = coverage(t, edges, corners, field)
    img = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 0))
    px, src = img.load(), source.load()
    for y in range(TILE_H):
        for x in range(TILE_W):
            inside = cov[y][x]
            if inside is None:
                continue
            c = src[x, y]
            if t["rim"] != 1.0 and inside < RIM_WIDTH:
                c = shade(c, t["rim"])
            px[x, y] = c + (255,)
    return img


def spill_field(t):
    """The noise that breaks up a terrain's spill: wraps with the tile like the textures."""
    g = t["grain"]
    return periodic_noise(TILE_W, TILE_H, t["seed"] + 77, ((g, 1.0), (g * 2, 0.5), (g * 4, 0.25)))


# ── relief ─────────────────────────────────────────────────────────────────

def add_relief(terrains, textures, tiles, meta, by_name):
    """Cliff and ramp pieces (relief.py) and, for every ground a plateau can be made of, its
    texture through each shape's top mask. Empty pieces are -1."""
    made = RL.make(RELIEF)
    V = made["variants"]
    rel = {"height": made["height"], "variants": V, "cliffs": [], "ramps": []}
    faces = []

    def place(pieces):
        out = []
        for p in pieces:
            if p is None:
                out.append(-1)
            else:
                out.append(len(tiles))
                faces.append(len(tiles))
                tiles.append(p)
        return out

    shapes = []
    for m in RL.CLIFF_SHAPES:
        for v in range(V):
            pieces, top = made["cliffs"][(m, v)]
            rel["cliffs"].append([m, v] + place(pieces))
            shapes.append(top)
    for d in range(4):
        for sides in range(4):
            pieces, top = made["ramps"][(d, sides)]
            rel["ramps"].append([d, sides] + place(pieces))
            shapes.append(top)
    for t in terrains:
        if "texture" not in t or "blocked" in t["flags"]:
            continue
        by_name[t["name"]]["tops"] = len(tiles)
        tiles += [RL.top_tile(textures[t["name"]][0], mask) for mask in shapes]
    rel["faces"] = faces
    meta["relief"] = rel


# ── main ───────────────────────────────────────────────────────────────────

def main(argv):
    terrains, default = parse_config()
    for t in terrains:
        if "texture" in t and t["texture"] not in RECIPES:
            sys.exit("terrain.py: %s uses unknown recipe %s" % (t["name"], t["texture"]))
    # A textured terrain is one sheet of period x period cells, its variants the sheet's cells.
    # Ore is painted over the default ground's sheet (same period), so mined-out cells match.
    textures, sheets = {}, {}
    for t in sorted((t for t in terrains if "texture" in t), key=lambda t: t["texture"] == "ore"):
        p = t["period"]
        t["variants"] = p * p
        if t["texture"] == "sand":
            sheets[t["name"]] = sand_sheet(t["seed"], p)
            textures[t["name"]] = slices(sheets[t["name"]], p)
        elif t["texture"] == "ore":
            base = sheets.get(default)
            if base is not None and base.size != (TILE_W * p, TILE_H * p):
                sys.exit("terrain.py: ore's period must be the default ground's")
            textures[t["name"]] = ore(t["seed"], p, base)
        else:
            textures[t["name"]] = RECIPES[t["texture"]](t["seed"], p)
    textures.update(model_tiles(terrains, textures, render="--no-models" not in argv))

    tiles, meta = [], {"tile": [TILE_W, TILE_H], "columns": ATLAS_COLS, "default": default, "terrains": []}
    for t in terrains:
        rec = {k: t[k] for k in ("name", "char", "layer")}
        rec["base"], rec["variants"] = len(tiles), t["variants"]
        if "texture" in t and t["period"] > 1:
            rec["period"] = t["period"]
        rec["flags"] = sorted(t["flags"])
        if "ground" in t:
            rec["blend"] = t["ground"]
        tiles += [im.convert("RGBA") for im in textures[t["name"]]]
        meta["terrains"].append(rec)
    by_name = {r["name"]: r for r in meta["terrains"]}
    for t in terrains:
        if "texture" not in t or t["edge"] <= 0:
            continue
        field, source = spill_field(t), textures[t["name"]][0]
        rec = by_name[t["name"]]
        rec["edges"] = len(tiles)
        tiles += [transition_tile(t, source, m, 0, field) for m in range(1, 16)]
        rec["corners"] = len(tiles)
        tiles += [transition_tile(t, source, 0, m, field) for m in range(1, 16)]

    if RELIEF is not None:
        add_relief(terrains, textures, tiles, meta, by_name)

    rows = (len(tiles) + ATLAS_COLS - 1) // ATLAS_COLS
    atlas = Image.new("RGBA", (ATLAS_COLS * TILE_W, rows * TILE_H), (0, 0, 0, 0))
    for i, im in enumerate(tiles):
        atlas.paste(im, ((i % ATLAS_COLS) * TILE_W, (i // ATLAS_COLS) * TILE_H))
    meta["count"] = len(tiles)
    OUT.mkdir(exist_ok=True)
    atlas.save(OUT / "terrain.png")
    (OUT / "terrain.json").write_text(json.dumps(meta, indent=1) + "\n")
    print("wrote %d terrain tiles (%d terrains) into %s" % (len(tiles), len(terrains), OUT / "terrain.png"))


if __name__ == "__main__":
    main(sys.argv[1:])
