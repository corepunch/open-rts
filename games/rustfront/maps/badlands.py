#!/usr/bin/env python3
"""Writes badlands.txt, a two-player map that shows every terrain in art/terrain.txt, its cliffs and ramps.

    python3 games/rustfront/maps/badlands.py

64 x 44 cells on two heights and a mesa. A river winds down the middle at the bottom of a
canyon, level 0 under the plain's level 1; ramps lead down its walls to two gravel fords. The
player's Directorate base (Blue, west) and the Scrap Clans (Red, east, the computer) stand on
concrete pads, with grass meadows, a stone mesa (level 2, one ramp up its south face) and
stone flats with rock outcrops, scrap wrecks from an old battle, and ore fields on both
plains and on an island in the canyon. Bases come from triad.py; the map format, cell rows
then level rows, is w_rustfront.c's.
"""
import math
import random
from pathlib import Path

import triad

W, H = 64, 44
HERE = Path(__file__).resolve().parent
triad.W, triad.H = W, H

BLUE = dict(origin=(3, 4), facing=0, ore=(14, 18))
RED = dict(origin=(48, 28), facing=8, ore=(50, 22))


FORDS = (12, 33)


def river_x(y):
    """The river's west bank; the canyon runs straight past each ford so ramps can meet it,
    and eases back into its bends."""
    bend = lambda y: 31 + 5 * math.sin(y / 5.5) + 1.5 * math.sin(y / 2.7)
    x = bend(y)
    for fy in FORDS:
        w = max(0.0, min(1.0, 1 - (abs(y - fy - 0.5) - 2.5) / 4))
        x = x * (1 - w) + bend(fy) * w
    return round(x)


def settle(g, lv):
    """A grid corner stands at the lowest level of its cells (w_terrain.c), so a raised cell
    with no corner up, a spur one cell wide, would sink: sink it here, onto the canyon's silt."""
    def corner(cx, cy):
        return min(int(lv[y][x]) for y in (cy - 1, cy) for x in (cx - 1, cx)
                   if 0 <= x < W and 0 <= y < H and lv[y][x] != "/")
    changed = True
    while changed:
        changed = False
        for y in range(H):
            for x in range(W):
                if lv[y][x] == "/":
                    continue
                top = max(corner(x, y), corner(x + 1, y), corner(x, y + 1), corner(x + 1, y + 1))
                if top < int(lv[y][x]):
                    lv[y][x], g[y][x], changed = str(top), ":" if top == 0 else g[y][x], True


def main():
    rnd = random.Random(7)
    g = [["."] * W for _ in range(H)]
    lv = [["1"] * W for _ in range(H)]
    fill = lambda cx, cy, rx, ry, ch: triad.blob(g, cx, cy, rx, ry, ch)

    fill(8, 30, 7, 6, ",")              # meadows
    fill(15, 37, 5, 4, ",")
    fill(54, 9, 8, 6, ",")
    fill(47, 4, 4, 2, ",")
    for y in range(3, 11):              # the mesa: stone, a level up, rocks on top
        for x in range(16, 28):
            if (x in (16, 27)) and y in (3, 10):
                continue
            g[y][x], lv[y][x] = "^", "2"
    for x in (21, 22):                  # its ramp, down the south face
        lv[10][x] = "/"
    for cx, cy in ((18, 5), (25, 4), (19, 8), (45, 35), (42, 37), (47, 38), (5, 40)):
        g[cy][cx] = "R"
    fill(44, 36, 5, 4, "^")             # stone flats
    fill(6, 40, 4, 2, "^")
    for cx, cy in ((45, 35), (42, 37), (47, 38), (5, 40)):
        g[cy][cx] = "R"

    for y in range(H):                  # the canyon: silt along a river, a level down
        x = river_x(y)
        for dx in range(-3, 6):
            g[y][x + dx], lv[y][x + dx] = ":", "0"
        for dx in range(-1, 2 + (y % 9 < 4)):
            g[y][x + dx] = "~"
    for fy in FORDS:                    # fords: gravel across the water, ramps down both walls
        x = river_x(fy)
        for y in (fy, fy + 1):
            for dx in range(-3, 6):
                g[y][x + dx] = "#"
            lv[y][x - 4] = lv[y][x + 6] = "/"
            g[y][x - 4] = g[y][x + 6] = "#"
    settle(g, lv)
    ix = river_x(22)                    # an ore island
    for y in range(20, 25):
        for dx in range(-1, 4):
            g[y][ix + dx] = "*" if 21 <= y <= 23 and 0 <= dx <= 2 else "~"

    fill(*BLUE["ore"], 3, 2, "*")
    fill(*RED["ore"], 3, 2, "*")
    fill(12, 41, 3, 2, "*")
    fill(48, 3, 3, 2, "*")

    for ox, oy in (BLUE["origin"], RED["origin"]):   # concrete pads under the bases
        for y in range(oy, oy + 9):
            for x in range(ox - 1, ox + 13):
                g[y][x] = "="
    for _ in range(12):                 # wrecks of an old battle near the fords
        fy = rnd.choice(FORDS)
        y = fy + rnd.randrange(-4, 6)
        x = river_x(y) + rnd.choice((-8, -7, -6, 8, 9, 10, 0, 4))
        flat = all(lv[ny][nx] == lv[y][x] for ny in range(y - 1, y + 2) for nx in range(x - 1, x + 2))
        if flat and g[y][x] in ".#,":
            g[y][x] = "x"

    lines = ["# Rustfront badlands, written by maps/badlands.py: every terrain of art/terrain.txt.",
             "# Blue (owner 0, the player) is the Directorate in the west; Red, the computer, the Clans in the east."]
    lines += ["".join(row) for row in g]
    lines.append("# Levels: the canyon is 0, the plains 1, the mesa 2; '/' is a ramp.")
    lines += ["level " + "".join(row) for row in lv]
    lines.append("# Blue: directorate")
    lines += triad.base(BLUE, "directorate", 0, full=True)
    lines.append("# Red: clans")
    lines += triad.base(RED, "clans", 1, full=True)
    lines += ["credits 1 6000", "camera %d %d" % (BLUE["origin"][0] + 7, BLUE["origin"][1] + 7)]
    (HERE / "badlands.txt").write_text("\n".join(lines) + "\n")
    print("wrote badlands.txt into %s" % HERE)


if __name__ == "__main__":
    main()
