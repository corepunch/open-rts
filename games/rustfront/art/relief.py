"""Cliffs and ramps for Rustfront's terrain: the tiles that join one height level to the next.
terrain.py imports this module and adds what it makes to the terrain atlas.

The map gives every cell a level. A corner of the grid takes the lowest level of the cells
around it, so a cell whose corners are not all at one level stands on the edge of a plateau:
it is a cliff, or a ramp where the map says so. Its shape is the set of its high corners,
NE 1, SE 2, SW 4, NW 8 (the corner tiles' bits), so 14 cliff shapes and 4 straight ramps.

Each shape is a heightfield over its cell, drawn the way KKnD draws its cliffs: the plateau
stays where a flat cell of it would be drawn and the low ground stays where its flat cells
are, and the wall between them hangs HEIGHT pixels down the screen from the plateau's lip,
over the low ground in front of it. A wall that faces south shows its whole face, one that
faces east or west its flank, one that faces north only its lip. The field is cast into the
game view column by column, nearest ground first, like a voxel landscape, so a near rock
hides a far one. Every noise wraps with the cell and the wall's profile along a cell side
depends only on the two corners of that side, so walls run on unbroken from cell to cell.

What a shape makes:
    pieces   the wall cut into cell-sized tiles: piece k is drawn over the cell k rows south
             of the cliff's own cell (k = 0, 1, 2), transparent where no rock shows.
    top      a mask of the pixels where the plateau's own ground shows in the cell: the game
             draws the cell's terrain through it over the low ground's tile (tops_for).
A ramp is a straight shape whose wall is a slope of packed track instead; where a cliff runs
on beside it, a rock flank rises from the slope (its side bits, 1 and 2).
"""
import math
import random

from PIL import Image

TILE_W, TILE_H = 48, 32
PIECES = 3
SAMPLES = 256              # ground samples down a cell's column (8 per screen row)
CM_PER_DEPTH_PX = 200.0 / 48 / math.cos(math.asin(2 / 3))   # a pixel of drop, in cm
CLIFF_SHAPES = list(range(1, 15))
RAMP_SHAPES = [9, 3, 6, 12]         # high side N, E, S, W
TOP, FLOOR, ROCK, TRACK = 1, 2, 3, 4
LIGHT = (-0.45, 0.72, 0.52)          # from the west, high, a little toward the viewer

ROCK_RGB = (150, 114, 78)
TRACK_RGB = (128, 108, 80)


class Noise:
    """Value noise that wraps once per cell in both directions; octaves are (cells across,
    cells down, amplitude). Sampled at (u, v) in cells."""

    def __init__(self, seed, octaves):
        rnd = random.Random(seed)
        self.octaves = [(cu, cv, a, [[rnd.random() for _ in range(cu)] for _ in range(cv)])
                        for cu, cv, a in octaves]
        self.total = sum(a for _, _, a, _ in self.octaves)

    def __call__(self, u, v):
        s = 0.0
        for cu, cv, a, g in self.octaves:
            fx, fy = u * cu, v * cv
            x0, y0 = math.floor(fx), math.floor(fy)
            tx, ty = fx - x0, fy - y0
            tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
            r0, r1 = g[y0 % cv], g[(y0 + 1) % cv]
            x0, x1 = x0 % cu, (x0 + 1) % cu
            s += a * ((r0[x0] * (1 - tx) + r0[x1] * tx) * (1 - ty) + (r1[x0] * (1 - tx) + r1[x1] * tx) * ty)
        return s / self.total


def smooth(t):
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return t * t * (3 - 2 * t)


def bilinear(mask, u, v):
    """1 at the shape's high corners, 0 at its low ones."""
    ne, se, sw, nw = (float(bool(mask & b)) for b in (1, 2, 4, 8))
    return (nw * (1 - u) + ne * u) * (1 - v) + (sw * (1 - u) + se * u) * v


class Relief:
    def __init__(self, conf):
        self.height = conf.get("height", 40)
        self.variants = conf.get("variants", 3)
        self.seed = conf.get("seed", 900)
        self.width = conf.get("wall", 0.24)      # the wall's footprint, in cells
        self.jag = conf.get("jag", 0.30)         # how far its lip wanders
        self.grid = [(i + 0.5) / TILE_W for i in range(-1, TILE_W + 1)]
        self.rows = [1 - j / (SAMPLES - 1) for j in range(SAMPLES)]   # near (south) first
        self.fields = [self.noise_fields(v) for v in range(self.variants)]

    def noise_fields(self, variant):
        """Every noise a shape needs, sampled once on the cast grid. The contour noise is the
        same for every variant at a cell's sides (so variants join) and its own inside."""
        base = Noise(self.seed, ((3, 3, 1.0), (6, 6, 0.5), (12, 12, 0.25)))
        own = Noise(self.seed + 31 * (variant + 1), ((2, 2, 1.0), (4, 4, 0.6), (9, 9, 0.3)))
        rough = Noise(self.seed + 7 + 13 * variant, ((7, 7, 1.0), (14, 14, 0.6), (28, 28, 0.3)))
        streak = Noise(self.seed + 3 + 17 * variant, ((24, 4, 1.0), (48, 8, 0.5)))
        crack = Noise(self.seed + 5 + 19 * variant, ((10, 10, 1.0), (20, 20, 0.5)))
        scree = Noise(self.seed + 9 + 23 * variant, ((9, 9, 1.0), (18, 18, 0.45)))
        out = {k: [] for k in ("contour", "rough", "streak", "crack", "scree")}
        for u in self.grid:
            cols = {k: [] for k in out}
            for v in self.rows:
                window = math.sin(math.pi * min(max(u, 0), 1)) * math.sin(math.pi * v)
                cols["contour"].append(base(u, v) + 0.8 * window * (own(u, v) - 0.5))
                cols["rough"].append(rough(u, v))
                cols["streak"].append(streak(u, v))
                cols["crack"].append(crack(u, v))
                cols["scree"].append(scree(u, v))
            for k in out:
                out[k].append(cols[k])
        return out

    # ── heightfields: depth below the plateau, in screen pixels ──────────────

    def cliff_depth(self, mask, u, v, nz):
        """(depth, t): t runs 0 at the lip to 1 at the foot, <0 on the plateau, >1 below."""
        f = bilinear(mask, u, v) + self.jag * (nz["contour"] - 0.5)
        t = (0.5 + self.width / 2 - f) / self.width
        d = smooth(t)
        if 0 < t < 1:   # knobbly rock, rough mid-face, smooth where it meets the ground
            d += 0.10 * (nz["rough"] - 0.5) * math.sin(math.pi * t)
        elif -0.4 < t <= 0:   # a crumbling rim of rock along the lip, seen even from behind
            d -= (1 + t / 0.4) * 0.9 * max(0.0, nz["rough"] - 0.42)
        elif t >= 1 and t < 1.8 and nz["scree"] > 0.60:   # boulders fallen at its foot
            d -= (nz["scree"] - 0.60) * 0.9 * (1.8 - t)
        return d * self.height, t

    def ramp_depth(self, mask, sides, u, v, nz):
        """A track sloping from the high side to the low one, with rock flanks on the sides
        in `sides` (bit 1 the west or north side, bit 2 the east or south)."""
        along, across = {9: (v, u), 3: (1 - u, v), 6: (1 - v, u), 12: (u, v)}[mask]
        r = smooth((along - 0.10) / 0.80)
        d = r * self.height
        cd, ct = self.cliff_depth(mask, u, v, nz)
        flank = 0.0
        if sides & 1:
            flank = max(flank, 1 - smooth(across / 0.16))
        if sides & 2:
            flank = max(flank, 1 - smooth((1 - across) / 0.16))
        return d * (1 - flank) + cd * flank, ct, flank

    # ── casting ────────────────────────────────────────────────────────────────

    def cast(self, variant, mask, ramp_sides=None):
        """[(rgb or None) per pixel] for PIECES cells down, and the top mask of the cell."""
        nzf = self.fields[variant]
        ncol, nrow = len(self.grid), SAMPLES
        depth = [[0.0] * nrow for _ in range(ncol)]
        kind = [[FLOOR] * nrow for _ in range(ncol)]
        tval = [[0.0] * nrow for _ in range(ncol)]
        for i, u in enumerate(self.grid):
            for j, v in enumerate(self.rows):
                nz = {k: nzf[k][i][j] for k in nzf}
                if ramp_sides is None:
                    d, t = self.cliff_depth(mask, u, v, nz)
                    flank = 1.0
                else:
                    d, t, flank = self.ramp_depth(mask, ramp_sides, u, v, nz)
                depth[i][j], tval[i][j] = d, t
                kind[i][j] = flank
        h = self.height
        W, Hh = TILE_W, TILE_H * PIECES
        img = [[None] * W for _ in range(Hh)]
        top = [[False] * W for _ in range(TILE_H)]
        lx, ly, lz = LIGHT
        ln = math.sqrt(lx * lx + ly * ly + lz * lz)
        lx, ly, lz = lx / ln, ly / ln, lz / ln
        dv = 1.0 / (SAMPLES - 1)
        for px in range(W):
            i = px + 1
            ymin = None
            for j, v in enumerate(self.rows):
                d = depth[i][j]
                sy = v * TILE_H + d
                row = int(math.floor(sy))
                if ymin is None:
                    ymin = min(Hh, row + 1)    # the nearest sample covers its own row only
                if row >= ymin:
                    continue
                # gradient, px of depth per cell
                gu = (depth[i + 1][j] - depth[i - 1][j]) * TILE_W / 2
                jn, jp = max(j - 1, 0), min(j + 1, SAMPLES - 1)
                gv = (depth[i][jn] - depth[i][jp]) / ((jp - jn) * dv)   # rows run south to north
                slope = math.hypot(gu, gv)
                t = tval[i][j]
                if d < 0.6 and slope < 25:
                    what = TOP
                elif d > h - 0.6 and slope < 25:
                    what = FLOOR
                elif ramp_sides is not None and slope < 95 and kind[i][j] < 0.5:
                    what = TRACK
                else:
                    what = ROCK
                colour = None
                if what in (ROCK, TRACK):
                    k = CM_PER_DEPTH_PX / 200.0
                    nx, ny, nzv = gu * k, 1.0, gv * k        # z grows toward the viewer (south)
                    nl = math.sqrt(nx * nx + ny * ny + nzv * nzv)
                    lam = max(0.0, (nx * lx + ny * ly + nzv * lz) / nl)
                    colour = self.shade(what, lam, d, t, {k2: nzf[k2][i][j] for k2 in nzf}, u=self.grid[i], v=v)
                for r in range(max(row, 0), ymin):
                    if r < Hh:
                        img[r][px] = colour
                        if r < TILE_H:
                            top[r][px] = what == TOP
                ymin = max(min(ymin, row), 0)
        return img, top

    def shade(self, what, lam, d, t, nz, u, v):
        h = self.height
        if what == TRACK:
            k = 0.80 + 0.30 * lam
            k *= 0.92 + 0.16 * nz["rough"]
            if 0.47 < (nz["streak"] * 3) % 1.0 < 0.53:     # ruts down the slope
                k *= 0.82
            return tuple(max(0, min(255, round(c * k))) for c in TRACK_RGB)
        k = 0.42 + 0.78 * lam
        k *= 0.80 + 0.40 * nz["streak"]                      # streaks down the face
        k *= 1.0 + 0.07 * math.sin(d * 0.55 + 5 * nz["rough"])  # strata
        if nz["crack"] > 0.66:                               # crevices
            k *= 0.55
        if 0 <= t < 0.10:                                    # a sunlit lip
            k *= 1.15
        if d > h * 0.8:                                      # dark at its foot
            k *= 1 - 0.35 * (d - h * 0.8) / (h * 0.2)
        base = ROCK_RGB if nz["scree"] < 0.75 else (136, 106, 76)
        return tuple(max(0, min(255, round(c * k))) for c in base)


def to_tiles(img, top):
    """(piece images or None when empty, top mask image) from a cast."""
    pieces = []
    for k in range(PIECES):
        tile = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 0))
        px = tile.load()
        any_px = False
        for y in range(TILE_H):
            for x in range(TILE_W):
                c = img[k * TILE_H + y][x]
                if c is not None:
                    px[x, y] = c + (255,)
                    any_px = True
        pieces.append(tile if any_px else None)
    mask = Image.new("L", (TILE_W, TILE_H), 0)
    mp = mask.load()
    for y in range(TILE_H):
        for x in range(TILE_W):
            if top[y][x]:
                mp[x, y] = 255
    return pieces, mask


def make(conf):
    """{"cliffs": {(shape, variant): (pieces, top)}, "ramps": {(dir, sides): (pieces, top)}}"""
    r = Relief(conf)
    out = {"cliffs": {}, "ramps": {}, "variants": r.variants, "height": r.height}
    for v in range(r.variants):
        for m in CLIFF_SHAPES:
            out["cliffs"][(m, v)] = to_tiles(*r.cast(v, m))
    for d, m in enumerate(RAMP_SHAPES):
        for sides in range(4):
            out["ramps"][(d, sides)] = to_tiles(*r.cast(0, m, sides))
    return out


def top_tile(texture, mask):
    """The terrain's texture through a top mask: transparent where the plateau does not show."""
    tile = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 0))
    tile.paste(texture.convert("RGBA"), mask=mask)
    return tile
