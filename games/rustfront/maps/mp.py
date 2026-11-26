"""Shared helpers for Rustfront's multiplayer maps (ironbridge.py, mesa-duel.py, three-crowns.py,
crossroads.py, highland-ring.py).

A multiplayer map has no bases: its "start N X Y [FACTION]" lines say where each side's
headquarters goes (X, Y its top-left cell), and the game spawns the faction a player picks
there (w_starts.c). These helpers draw terrain and relief, make the map symmetric, and check
it before writing: every start stands on flat, open ground with room for its force, every
start can drive to every other, and every start has the same ore at the same distance.

The relief check repeats w_terrain.c's rules: a ramp cell ('/') stands at the level of its
highest plain neighbour; a grid corner is at the lowest level of its four cells; a cell whose
corners differ is a cliff (it blocks), or a ramp if it is marked '/' and slopes straight
down one side.
"""
from collections import deque
from pathlib import Path

HERE = Path(__file__).resolve().parent
BLOCKING = set("~Rx")
RAMP_SHAPES = {9, 3, 6, 12}  # high side N, E, S, W (corner bits NE 1, SE 2, SW 4, NW 8)


class Map:
    def __init__(self, w, h, ground=".", level=1):
        self.w, self.h = w, h
        self.g = [[ground] * w for _ in range(h)]
        self.lv = [[str(level)] * w for _ in range(h)]

    def inside(self, x, y):
        return 0 <= x < self.w and 0 <= y < self.h

    def rect(self, x0, y0, x1, y1, ch=None, level=None):
        """Cells x0..x1, y0..y1 inclusive: terrain `ch` and/or height `level`."""
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                if ch is not None:
                    self.g[y][x] = ch
                if level is not None:
                    self.lv[y][x] = str(level)

    def blob(self, cx, cy, rx, ry, ch=None, level=None):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if self.inside(x, y) and ((x - cx) / (rx + 0.5)) ** 2 + ((y - cy) / (ry + 0.5)) ** 2 <= 1:
                    if ch is not None:
                        self.g[y][x] = ch
                    if level is not None:
                        self.lv[y][x] = str(level)

    def ramp(self, x0, y0, x1, y1, ch="#"):
        """A ramp: the high ground's edge cells x0..x1, y0..y1 slope down to the low side."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.lv[y][x] = "/"
                if ch:
                    self.g[y][x] = ch

    def symmetrize(self, kind):
        """Copies the canonical part of the map onto the rest:
        mirror_x  the west half onto the east       rot180  the north half, turned about the middle
        mirror_xy the north-west quarter onto all   rot90   the north-west quarter, turned a quarter
                                                            each time (square maps)"""
        w, h = self.w, self.h
        g, lv = self.g, self.lv
        def put(x, y, sx, sy):
            g[y][x], lv[y][x] = g[sy][sx], lv[sy][sx]
        for y in range(h):
            for x in range(w):
                if kind == "mirror_x" and x >= (w + 1) // 2:
                    put(x, y, w - 1 - x, y)
                elif kind == "rot180" and (y > (h - 1) / 2 or (y == (h - 1) / 2 and x > (w - 1) / 2)):
                    put(x, y, w - 1 - x, h - 1 - y)
                elif kind == "mirror_xy" and (x >= (w + 1) // 2 or y >= (h + 1) // 2):
                    put(x, y, x if x < (w + 1) // 2 else w - 1 - x, y if y < (h + 1) // 2 else h - 1 - y)
        if kind == "rot90":
            assert w == h, "rot90 needs a square map"
            n = w
            half = (n + 1) // 2
            for y in range(half):
                for x in range(half):
                    for rx, ry in ((n - 1 - y, x), (n - 1 - x, n - 1 - y), (y, n - 1 - x)):
                        put(rx, ry, x, y)

    def settle(self):
        """A raised cell with no grid corner up would sink (w_terrain.c): sink it here."""
        def corner(cx, cy):
            return min(int(self.lv[y][x]) for y in (cy - 1, cy) for x in (cx - 1, cx)
                       if self.inside(x, y) and self.lv[y][x] != "/")
        changed = True
        while changed:
            changed = False
            for y in range(self.h):
                for x in range(self.w):
                    if self.lv[y][x] == "/":
                        continue
                    top = max(corner(x, y), corner(x + 1, y), corner(x, y + 1), corner(x + 1, y + 1))
                    if top < int(self.lv[y][x]):
                        self.lv[y][x], changed = str(top), True

    # ── w_terrain.c's relief, to check the map ─────────────────────────────

    def relief(self):
        """Per cell: (level, kind) with kind 'flat', 'cliff' or 'ramp'."""
        w, h, lv = self.w, self.h, self.lv
        level = [[0 if lv[y][x] == "/" else int(lv[y][x]) for x in range(w)] for y in range(h)]
        for y in range(h):
            for x in range(w):
                if lv[y][x] == "/":
                    level[y][x] = max([level[ny][nx] for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y))
                                       if self.inside(nx, ny) and lv[ny][nx] != "/"] or [0])
        def corner(cx, cy):
            return min(level[y][x] for y in (cy - 1, cy) for x in (cx - 1, cx) if self.inside(x, y))
        out = [[None] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                c = [corner(x + 1, y), corner(x + 1, y + 1), corner(x, y + 1), corner(x, y)]  # NE SE SW NW
                lo, hi = min(c), max(c)
                if lo == hi:
                    out[y][x] = (lo, "flat")
                    continue
                shape = sum(1 << k for k in range(4) if c[k] > lo)
                out[y][x] = (hi, "ramp" if lv[y][x] == "/" and shape in RAMP_SHAPES else "cliff")
        return out

    def open_cells(self):
        r = self.relief()
        return [[self.g[y][x] not in BLOCKING and r[y][x][1] != "cliff" for x in range(self.w)] for y in range(self.h)]

    def distances(self, start, passable):
        dist = [[-1] * self.w for _ in range(self.h)]
        q = deque([start])
        dist[start[1]][start[0]] = 0
        while q:
            x, y = q.popleft()
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if self.inside(nx, ny) and passable[ny][nx] and dist[ny][nx] < 0:
                    dist[ny][nx] = dist[y][x] + 1
                    q.append((nx, ny))
        return dist

    def check(self, starts, ore_radius=16, tolerance=2):
        """Asserts that every start is fair and playable; returns a short report."""
        r = self.relief()
        passable = self.open_cells()
        report = []
        for i, (sx, sy, _) in enumerate(starts):
            lvl = r[sy][sx][0]
            for y in range(sy - 4, sy + 7):          # the HQ, its force and room around
                for x in range(sx - 4, sx + 7):
                    assert self.inside(x, y), "start %d too near the edge" % i
                    assert passable[y][x] and r[y][x] == (lvl, "flat"), \
                        "start %d: cell %d,%d is not flat open ground at level %d" % (i, x, y, lvl)
        dist = [self.distances((sx + 1, sy + 1), passable) for sx, sy, _ in starts]
        for i in range(len(starts)):
            for j, (sx, sy, _) in enumerate(starts):
                assert dist[i][sy + 1][sx + 1] >= 0, "start %d cannot reach start %d" % (i, j)
        ore = [(x, y) for y in range(self.h) for x in range(self.w) if self.g[y][x] == "*"]
        near = []
        for i, d in enumerate(dist):
            home = [v for v in (self.ore_step(d, x, y) for x, y in ore) if 0 <= v <= ore_radius]
            near.append((len(home), min(home) if home else -1))
        counts = {n for n, _ in near}
        closest = [c for _, c in near]
        assert max(counts) - min(counts) <= tolerance, "unequal home ore: %s" % near
        assert max(closest) - min(closest) <= tolerance, "unequal distance to ore: %s" % near
        pair = [dist[i][sy + 1][sx + 1] for i in range(len(starts)) for j, (sx, sy, _) in enumerate(starts) if j > i]
        report.append("%d starts, %d ore cells, home ore %s, drive between starts %s" %
                      (len(starts), len(ore), near, pair))
        return "; ".join(report)

    def ore_step(self, d, x, y):
        """Steps from a start to stand beside ore cell x, y (-1: unreachable)."""
        best = -1
        for nx, ny in ((x, y), (x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if self.inside(nx, ny) and d[ny][nx] >= 0 and (best < 0 or d[ny][nx] < best):
                best = d[ny][nx]
        return best

    def pad(self, sx, sy, ch="="):
        """Concrete under a start's headquarters and its door."""
        self.rect(sx - 1, sy - 1, sx + 3, sy + 3, ch)

    def write(self, name, title, about, starts, symmetry=None):
        """Checks and writes maps/<name>.txt. `starts` is [(x, y, default_faction)]."""
        report = self.check(starts)
        lines = ["# Rustfront multiplayer map, written by maps/%s.py. %s" % (name, about),
                 "# Starts are neutral: the game puts the faction each player picks there (w_starts.c).",
                 "title " + title]
        lines += ["".join(row) for row in self.g]
        lines.append("# Levels; '/' is a ramp.")
        lines += ["level " + "".join(row) for row in self.lv]
        for i, (x, y, faction) in enumerate(starts):
            lines.append("start %d %d %d %s" % (i, x, y, faction))
        (HERE / (name + ".txt")).write_text("\n".join(lines) + "\n")
        print("wrote %s.txt (%dx%d%s): %s" % (name, self.w, self.h, ", " + symmetry if symmetry else "", report))
