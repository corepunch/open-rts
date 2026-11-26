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
        t = dict(name=words[1], char=words[2], variants=1, seed=0, edge=0.0, jag=0.5, grain=6,
                 rim=1.0, flags=set())
        for w in words[3:]:
            if "=" in w:
                k, v = w.split("=", 1)
                t[k] = float(v) if k in ("edge", "jag", "rim") else int(v) if k in (
                    "layer", "variants", "seed", "grain") else v
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

SAND = (156, 130, 96)
ORE = [(250, 190, 60), (232, 150, 34), (176, 98, 20), (255, 232, 140)]


def ground(base, seed, lo=0.86, hi=1.02, speckle=0.03, octaves=None):
    """A base colour shaded by wrapping noise, with a sprinkle of darker and lighter grains."""
    n = periodic_noise(TILE_W, TILE_H, seed, **({"octaves": octaves} if octaves else {}))
    rnd = random.Random(seed * 7 + 1)
    img = Image.new("RGB", (TILE_W, TILE_H))
    px = img.load()
    for y in range(TILE_H):
        for x in range(TILE_W):
            v = lo + (hi - lo) * n[y][x]
            if rnd.random() < speckle:
                v *= rnd.choice((0.82, 1.1))
            px[x, y] = shade(base, v)
    return img, n, rnd


def sand(seed):
    img, n, rnd = ground(SAND, seed)
    px = img.load()
    # faint wind ripples: light crests that wander with the noise
    phase = rnd.random() * math.tau
    for y in range(TILE_H):
        for x in range(TILE_W):
            r = math.sin((y * 2 + n[y][x] * 9) * math.tau / 8 + phase + 2 * math.sin(x * math.tau / TILE_W))
            if r > 0.86:
                px[x, y] = shade(px[x, y], 1.06)
    return img


def gravel(seed):
    img, _, rnd = ground((128, 108, 82), seed, 0.84, 1.04, 0.05)
    px = img.load()
    for _ in range(46):      # pebbles: a lit top-left, a dark foot
        x, y = rnd.randrange(TILE_W), rnd.randrange(TILE_H)
        c = shade(rnd.choice([(150, 134, 110), (118, 102, 84), (162, 150, 130), (104, 90, 74)]), 1.0)
        r = rnd.choice((1, 1, 1, 2))
        for dy in range(-r + 1, r):
            for dx in range(-r, r + 1):
                put(px, x + dx, y + dy, c)
        put(px, x - r + 1, y - r + 1, shade(c, 1.22))
        for dx in range(-r, r + 1):
            put(px, x + dx, y + r, shade(c, 0.6))
    return img


def grass(seed):
    """Dry steppe grass: olive tufts over earth, with bare patches."""
    earth, blade = (116, 104, 66), (94, 126, 46)
    n = periodic_noise(TILE_W, TILE_H, seed)
    m = periodic_noise(TILE_W, TILE_H, seed + 50, ((6, 1.0), (12, 0.6)))
    rnd = random.Random(seed * 7 + 1)
    img = Image.new("RGB", (TILE_W, TILE_H))
    px = img.load()
    for y in range(TILE_H):
        for x in range(TILE_W):
            t = max(0.0, min(1.0, (m[y][x] - 0.36) * 3.0))      # 0 bare earth .. 1 grass cover
            px[x, y] = shade(mix(earth, blade, t), 0.82 + 0.2 * n[y][x])
    for _ in range(150):     # blades: short strokes, lit tips, darker roots
        x, y = rnd.randrange(TILE_W), rnd.randrange(TILE_H)
        cover = m[y][x]
        if cover < 0.34 and rnd.random() < 0.8:
            continue
        c = rnd.choice([(118, 142, 56), (92, 118, 42), (138, 146, 70), (78, 100, 38)])
        lean = rnd.choice((-1, 0, 0, 1))
        h = rnd.choice((2, 3, 3, 4))
        for i in range(h):
            put(px, x + (lean if i >= h - 1 else 0), y - i, shade(c, 0.8 + 0.3 * i / h))
        put(px, x, y + 1, shade(px[x % TILE_W, (y + 1) % TILE_H], 0.78))
    return img


def stone(seed):
    """Flat bedrock: grey-tan with broad mottling and a few cracks."""
    img, n, rnd = ground((134, 126, 114), seed, 0.80, 1.04, 0.04, ((3, 1.0), (6, 0.6), (12, 0.3), (24, 0.15)))
    px = img.load()
    for _ in range(rnd.choice((1, 2, 2, 3))):   # cracks: a wandering walk, dark with a lit lip
        x, y = rnd.randrange(TILE_W), rnd.randrange(TILE_H)
        angle = rnd.uniform(0, math.tau)
        fx, fy = float(x), float(y)
        for _ in range(rnd.randrange(8, 20)):
            put(px, round(fx), round(fy), shade(px[round(fx) % TILE_W, round(fy) % TILE_H], 0.6))
            put(px, round(fx), round(fy) + 1, shade(px[round(fx) % TILE_W, (round(fy) + 1) % TILE_H], 1.1))
            angle += rnd.uniform(-0.7, 0.7)
            fx, fy = fx + math.cos(angle), fy + 0.66 * math.sin(angle)
    return img


def water(seed):
    """Murky deep water: dark teal with long light ripples."""
    deep, light = (40, 74, 88), (92, 136, 140)
    n = periodic_noise(TILE_W, TILE_H, seed, ((3, 1.0), (6, 0.5), (12, 0.25)))
    rnd = random.Random(seed * 7 + 1)
    img = Image.new("RGB", (TILE_W, TILE_H))
    px = img.load()
    for y in range(TILE_H):
        for x in range(TILE_W):
            px[x, y] = shade(deep, 0.85 + 0.3 * n[y][x])
    for _ in range(9):
        x, y = rnd.randrange(TILE_W), rnd.randrange(TILE_H)
        for i in range(rnd.randrange(4, 9)):
            put(px, x + i, y, mix(px[(x + i) % TILE_W, y % TILE_H], light, 0.55 if 0 < i < 5 else 0.3))
    return img


def silt(seed):
    """Damp silt of a canyon floor: darker than the sand above, with wet hollows and grit."""
    img, n, rnd = ground((122, 100, 74), seed, 0.84, 1.02, 0.05, ((3, 1.0), (6, 0.6), (12, 0.3)))
    px = img.load()
    wet = periodic_noise(TILE_W, TILE_H, seed + 40, ((4, 1.0), (8, 0.5)))
    for y in range(TILE_H):
        for x in range(TILE_W):
            if wet[y][x] > 0.62:
                px[x, y] = shade(px[x, y], 0.86 if wet[y][x] < 0.7 else 0.78)
    for _ in range(18):      # grit
        x, y = rnd.randrange(TILE_W), rnd.randrange(TILE_H)
        put(px, x, y, shade(px[x, y], rnd.choice((0.7, 1.18))))
    return img


def ore(seed):
    """Sand with faceted ore crystals: a lit top and a dark foot."""
    img = sand(seed)
    px = img.load()
    rnd = random.Random(seed * 13 + 5)
    for _ in range(7):
        cx, cy = rnd.randrange(4, TILE_W - 4), rnd.randrange(4, TILE_H - 4)
        r = rnd.choice((2, 3, 3, 4))
        for dy in range(-r, r + 1):
            for dx in range(-r - 1, r + 2):
                if abs(dx) * 0.8 + abs(dy) <= r:
                    c = ORE[0] if dy < 0 else ORE[1]
                    if dy == r or abs(dx) * 0.8 + abs(dy) > r - 0.6:
                        c = ORE[2]
                    put(px, cx + dx, cy + dy, c)
        put(px, cx, cy - r + 1, ORE[3])
    return img


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
    textures = {t["name"]: [RECIPES[t["texture"]](t["seed"] + v) for v in range(t["variants"])]
                for t in terrains if "texture" in t}
    for t in terrains:
        if "texture" in t and t["texture"] not in RECIPES:
            sys.exit("terrain.py: %s uses unknown recipe %s" % (t["name"], t["texture"]))
    textures.update(model_tiles(terrains, textures, render="--no-models" not in argv))

    tiles, meta = [], {"tile": [TILE_W, TILE_H], "columns": ATLAS_COLS, "default": default, "terrains": []}
    for t in terrains:
        rec = {k: t[k] for k in ("name", "char", "layer")}
        rec["base"], rec["variants"] = len(tiles), t["variants"]
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
