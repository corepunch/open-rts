#!/usr/bin/env python3
"""The campaign's world map of the Rustfront, in the manner of Dune 2000's: assets/worldmap.pcx.

    python3 games/rustfront/art/worldmap.py

A painted relief map of the old foundry belt, 640 x 480 like the front end: the Directorate's
ruined foundries in the west, the Glass River's canyon down the middle, the Scrap Clans' junk
and dunes in the south, the Lumen Concord's salt and glass in the east. The map fills the left
of the screen; the briefing panel covers the right. It is cut into territories (a Voronoi of
their seats, its borders warped by noise so they wander like real ones), each written as its
index into assets/worldmap_mask.pcx, and assets/worldmap.txt names them, says who holds each
at the start, and where each campaign's missions are fought (m_worldmap.c reads it).

The game tints each territory with its holder's colour as the campaign goes, so the palette
(192 colours) is cut from the map AND its three tinted copies; 48 more are the interface skin's
(art/ui.py) and 16 the menus' inks, as on the title screen (art/title.py).
"""
import math
import os
import random
import sys

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
sys.path.insert(0, HERE)

W, H = 640, 480
MAP_W = 386            # the map's part of the screen; the briefing panel covers the rest
OWNERS = ["none", "directorate", "clans", "concord"]
# Holders' colours on the map: the Directorate's steel blue, the Clans' rust, the Concord's light.
TINT = {1: (70, 120, 225), 2: (215, 72, 38), 3: (70, 205, 215)}
TINT_AMOUNT = 0.36

# id: (name, x, y, holder at the start)
TERRITORIES = [
    ("Iron Hill", 40, 76, 1),
    ("Foundry Flats", 50, 186, 0),
    ("Ridge Nine", 126, 96, 1),
    ("Cinder Bluff", 42, 304, 1),
    ("Rook's Crossing", 124, 214, 1),
    ("Northern Scarp", 206, 56, 0),
    ("The Ford", 196, 156, 2),
    ("The Crucible", 202, 258, 0),
    ("Grand Junkyard", 134, 338, 2),
    ("Hook Wastes", 78, 424, 0),
    ("Gutter Dunes", 200, 418, 2),
    ("Glasswater", 282, 96, 3),
    ("Ashfield", 276, 222, 0),
    ("Saltpan", 288, 360, 0),
    ("Fallow Mesa", 352, 160, 3),
    ("Lumen Reach", 352, 286, 0),
    ("The Lens", 350, 420, 3),
]
# Each campaign's five missions, in order: the territory each is fought over (docs/CAMPAIGN.md).
MISSIONS = {
    "directorate": ["Foundry Flats", "Ridge Nine", "The Ford", "Glasswater", "The Crucible"],
    "clans": ["Hook Wastes", "Rook's Crossing", "Grand Junkyard", "Fallow Mesa", "Iron Hill"],
    "concord": ["Lumen Reach", "Saltpan", "Ashfield", "Cinder Bluff", "The Lens"],
}
INKS = [(12, 18, 22), (24, 36, 45), (40, 56, 66), (220, 230, 220), (255, 196, 96), (150, 160, 150),
        (90, 40, 30), (200, 80, 50), (110, 200, 120), (16, 12, 10), (255, 255, 255), (0, 0, 0),
        (60, 70, 74), (120, 130, 128), (232, 150, 70), (28, 24, 22)]


def noise(w, h, seed, octaves):
    """Smooth value noise, sum of octaves (cells across, amplitude), in 0..1."""
    rnd = random.Random(seed)
    out = [[0.0] * w for _ in range(h)]
    total = sum(a for _, a in octaves)
    for cells, amp in octaves:
        cy = max(1, round(cells * h / w))
        grid = [[rnd.random() for _ in range(cells + 1)] for _ in range(cy + 1)]
        for y in range(h):
            fy = y * cy / h
            y0, ty = int(fy), fy - int(fy)
            ty = ty * ty * (3 - 2 * ty)
            for x in range(w):
                fx = x * cells / w
                x0, tx = int(fx), fx - int(fx)
                tx = tx * tx * (3 - 2 * tx)
                a, b = grid[y0][x0], grid[y0][x0 + 1]
                c, d = grid[y0 + 1][x0], grid[y0 + 1][x0 + 1]
                out[y][x] += amp * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty)
    return [[v / total for v in row] for row in out]


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def ramp(stops, t):
    t = max(0.0, min(1.0, t))
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            return mix(ca, cb, (t - a) / (b - a))
    return stops[-1][1]


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def river_x(y):
    """The Glass River's course, north to south down the middle."""
    return 200 + 14 * math.sin(y / 46.0) + 8 * math.sin(y / 17.0 + 1.3)


def paint():
    height = noise(W, H, 7, ((4, 1.0), (8, 0.55), (16, 0.3), (32, 0.15), (64, 0.08)))
    region = noise(W, H, 11, ((3, 1.0), (6, 0.5)))
    grain = noise(W, H, 13, ((48, 1.0), (96, 0.6)))
    rnd = random.Random(5)
    img = Image.new("RGB", (W, H))
    px = img.load()
    sand = [(0.0, (92, 62, 40)), (0.35, (150, 108, 70)), (0.6, (190, 146, 98)), (0.85, (214, 178, 126)),
            (1.0, (232, 206, 156))]
    salt = [(0.0, (128, 116, 100)), (0.5, (196, 184, 160)), (1.0, (236, 230, 210))]
    slag = [(0.0, (52, 40, 34)), (0.5, (92, 70, 54)), (1.0, (130, 100, 76))]
    hs = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            # the canyon cut into the relief
            d = abs(x - river_x(y))
            h = height[y][x] - 0.35 * (1 - smooth(6, 34, d))
            # terraces: mesas and benches with steep steps between them
            k = h * 7
            h = (math.floor(k) + smooth(0.0, 0.25, k - math.floor(k))) / 7
            hs[y][x] = h
    for y in range(H):
        for x in range(W):
            h = hs[y][x]
            # hillshade from the north-west
            gx = hs[y][min(W - 1, x + 1)] - hs[y][max(0, x - 1)]
            gy = hs[min(H - 1, y + 1)][x] - hs[max(0, y - 1)][x]
            light = 1.0 - 16.0 * (gx + gy)
            east = smooth(250, 330, x + 30 * (region[y][x] - 0.5))
            west = 1 - smooth(110, 170, x + 30 * (region[y][x] - 0.5))
            south = smooth(320, 400, y + 40 * (region[y][x] - 0.5)) * (1 - east)
            c = ramp(sand, 0.15 + 0.85 * h)
            c = mix(c, ramp(salt, h), 0.55 * east)          # the Concord's salt and glass
            if east > 0.5 and grain[y][x] > 0.78:
                c = mix(c, (150, 225, 230), 0.35)           # glass glinting in the salt
            c = mix(c, ramp(slag, h), 0.65 * west)          # the foundry belt's slag
            c = mix(c, mix(c, (70, 54, 40), 0.5), 0.5 * south * (grain[y][x] > 0.55))  # junk fields
            d = abs(x - river_x(y))
            if d < 4.5:                                     # the river
                c = mix((40, 96, 112), (88, 150, 158), smooth(4.5, 0, d) * 0.6 + 0.2 * grain[y][x])
            elif d < 7:
                c = mix(c, (60, 48, 38), 0.6)               # its dark banks
            c = tuple(max(0, min(255, round(v * (0.78 + 0.32 * light)))) for v in c)
            px[x, y] = c
    # the foundry belt: ruined blocks of many sizes along a few old roads in the west
    roads = [(rnd.randrange(10, 150), rnd.uniform(-0.25, 0.25)) for _ in range(4)]
    for ry, slope in roads:
        for x in range(0, 175):
            y = int(ry + slope * x + 26 * math.sin(x / 40.0 + ry))
            for yy in range(y * 3 % H, H, 160):
                if 0 <= yy < H:
                    px[x, yy] = mix(px[x, yy], (34, 30, 28), 0.5 * (1 - smooth(120, 175, x)))
    for _ in range(420):
        x, y = rnd.randrange(0, 160), rnd.randrange(0, H)
        if rnd.random() > 1 - smooth(60, 165, x):
            continue
        bw, bh = rnd.choice((3, 4, 5, 7, 9)), rnd.choice((2, 3, 4, 6))
        for dy in range(bh):
            for dx in range(bw):
                if x + dx < W and y + dy < H:
                    edge = dy == 0 or dx == 0
                    px[x + dx, y + dy] = mix(px[x + dx, y + dy], (150, 146, 140) if edge else (38, 34, 34), 0.35 if edge else 0.5)
    # ore: amber glints where the story finds it
    for name in ("The Ford", "Saltpan", "Grand Junkyard", "Foundry Flats", "Ashfield", "Fallow Mesa"):
        _, tx, ty, _ = next(t for t in TERRITORIES if t[0] == name)
        for _ in range(90):
            a, r = rnd.uniform(0, math.tau), abs(rnd.gauss(0, 13))
            x, y = int(tx + 12 + r * math.cos(a)), int(ty + 10 + r * 0.7 * math.sin(a))
            if 0 <= x < W - 1 and 0 <= y < H:
                px[x, y] = (238, 176, 60)
                px[x + 1, y] = (176, 104, 32)
    # the Lens: a crater with a cyan glint in the south-east
    _, lx, ly, _ = next(t for t in TERRITORIES if t[0] == "The Lens")
    for y in range(ly - 22, ly + 23):
        for x in range(lx - 30, lx + 31):
            r = math.hypot((x - lx) / 30, (y - ly) / 22)
            if r < 1 and 0 <= x < W and 0 <= y < H:
                px[x, y] = mix(px[x, y], (60, 56, 60) if r > 0.7 else (110, 120, 124), 0.55)
                if r < 0.18:
                    px[x, y] = mix(px[x, y], (120, 240, 245), 0.8)
    # a cartographer's grid and frame
    for y in range(H):
        for x in range(W):
            if (x % 64 == 0 or y % 64 == 0):
                px[x, y] = mix(px[x, y], (30, 24, 18), 0.18)
    img = img.filter(ImageFilter.UnsharpMask(radius=1, percent=40, threshold=2))
    # vignette
    px = img.load()
    for y in range(H):
        for x in range(W):
            dx, dy = (x - MAP_W / 2) / (MAP_W * 0.75), (y - H / 2) / (H * 0.7)
            px[x, y] = tuple(round(v * max(0.55, 1.05 - 0.45 * (dx * dx + dy * dy))) for v in px[x, y])
    return img


def territories_mask():
    warp_x = noise(W, H, 21, ((6, 1.0), (12, 0.5), (24, 0.25)))
    warp_y = noise(W, H, 22, ((6, 1.0), (12, 0.5), (24, 0.25)))
    mask = Image.new("L", (W, H), 255)
    m = mask.load()
    for y in range(H):
        for x in range(MAP_W):
            wx, wy = x + 26 * (warp_x[y][x] - 0.5), y + 26 * (warp_y[y][x] - 0.5)
            best, bd = 255, 1e18
            for i, (_, tx, ty, _) in enumerate(TERRITORIES):
                d = (wx - tx) ** 2 + ((wy - ty) * 1.1) ** 2
                if d < bd:
                    best, bd = i, d
            m[x, y] = best
    return mask


def main():
    base = paint()
    mask = territories_mask()
    # the palette: the map with every holder's tint, the skin's metal, the inks
    pool = [base]
    for owner, colour in TINT.items():
        pool.append(Image.blend(base, Image.new("RGB", base.size, colour), TINT_AMOUNT))
    strip = Image.new("RGB", (W, H * len(pool)))
    for i, im in enumerate(pool):
        strip.paste(im, (0, i * H))
    pal = strip.quantize(colors=192, method=Image.MEDIANCUT).getpalette()[:192 * 3]
    skin = os.path.join(GAME, "sprites", "ui.png")
    if os.path.exists(skin):
        ui = Image.open(skin).convert("RGBA")
        opaque = [c[:3] for c in ui.getdata() if c[3] >= 128]
        side = int(len(opaque) ** 0.5) + 1
        pool_img = Image.new("RGB", (side, side))
        pool_img.putdata(opaque + [opaque[-1]] * (side * side - len(opaque)))
        pal += pool_img.quantize(colors=48, method=Image.MEDIANCUT).getpalette()[:48 * 3]
    else:
        pal += [0] * (48 * 3)
    for c in INKS:
        pal.extend(c)
    pal = pal[:768]
    pimg = Image.new("P", (1, 1))
    pimg.putpalette(pal)
    out = base.quantize(palette=pimg, dither=Image.Dither.NONE)
    os.makedirs(os.path.join(GAME, "assets"), exist_ok=True)
    out.save(os.path.join(GAME, "assets", "worldmap.pcx"))
    mpal = Image.new("P", (W, H))
    mpal.putdata(list(mask.getdata()))
    mpal.putpalette([v for i in range(256) for v in ((i * 37) % 256, (i * 91) % 256, (i * 53) % 256)])
    mpal.save(os.path.join(GAME, "assets", "worldmap_mask.pcx"))
    lines = ["# GENERATED by art/worldmap.py: the campaign map's territories and missions.",
             "# territory ID X Y HOLDER NAME   (X, Y: the territory's seat on assets/worldmap.pcx;",
             "#                                 HOLDER at the start: none directorate clans concord)",
             "# mission FACTION N TERRITORY    (mission N of FACTION's campaign is fought there)",
             "# tint HOLDER R G B AMOUNT%"]
    for i, (name, x, y, owner) in enumerate(TERRITORIES):
        lines.append("territory %d %d %d %s %s" % (i, x, y, OWNERS[owner], name))
    names = [t[0] for t in TERRITORIES]
    for faction, ts in MISSIONS.items():
        for n, t in enumerate(ts, 1):
            lines.append("mission %s %d %d" % (faction, n, names.index(t)))
    for owner, colour in TINT.items():
        lines.append("tint %s %d %d %d %d" % (OWNERS[owner], *colour, round(TINT_AMOUNT * 100)))
    open(os.path.join(GAME, "assets", "worldmap.txt"), "w").write("\n".join(lines) + "\n")
    # a preview for the docs
    prev = base.copy()
    edge = mask.filter(ImageFilter.FIND_EDGES).point(lambda v: 255 if v else 0)
    prev.paste((24, 18, 14), mask=edge)
    prev.save(os.path.join(HERE, "previews", "worldmap.png"))
    print("wrote assets/worldmap.pcx, worldmap_mask.pcx, worldmap.txt (%d territories)" % len(TERRITORIES))


if __name__ == "__main__":
    main()
