#!/usr/bin/env python3
"""Writes badlands.txt, a two-player map that shows every terrain in art/terrain.txt.

    python3 games/rustfront/maps/badlands.py

64 x 44 cells. A river winds down the middle with two gravel fords; the player's Directorate
base (Blue, west) and the Scrap Clans (Red, east, the computer) stand on concrete pads, with
grass meadows, stone plateaus with rock outcrops, scrap wrecks from an old battle, and ore
fields on both banks and on an island. Bases come from triad.py; the map format is
w_rustfront.c's.
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


def river_x(y):
    return 30 + round(3.5 * math.sin(y / 6.0) + 1.5 * math.sin(y / 2.7))


def main():
    rnd = random.Random(7)
    g = [["."] * W for _ in range(H)]
    fill = lambda cx, cy, rx, ry, ch: triad.blob(g, cx, cy, rx, ry, ch)

    fill(8, 30, 7, 6, ",")              # meadows
    fill(17, 36, 5, 4, ",")
    fill(54, 9, 8, 6, ",")
    fill(44, 4, 4, 2, ",")
    fill(22, 6, 5, 3, "^")              # stone plateaus, rocks on them
    fill(42, 36, 5, 4, "^")
    fill(6, 40, 4, 2, "^")
    for cx, cy in ((21, 5), (24, 7), (20, 8), (43, 35), (40, 37), (45, 38), (5, 40)):
        g[cy][cx] = "R"
    fill(26, 24, 2, 5, "#")             # gravel spoil along the river
    fill(37, 18, 2, 4, "#")

    for y in range(H):                  # the river
        x = river_x(y)
        for dx in range(-1, 2 + (y % 9 < 4)):
            g[y][x + dx] = "~"
    for fy in (12, 33):                 # fords: gravel across the water
        for y in (fy, fy + 1):
            x = river_x(y)
            for dx in range(-2, 4):
                g[y][x + dx] = "#"
    fill(river_x(22) + 1, 22, 1, 1, "*")   # an ore island
    for y in range(21, 24):
        for dx in (-2, -1, 3, 4):
            x = river_x(22) + dx
            if g[y][x] == ".":
                g[y][x] = "~"

    fill(*BLUE["ore"], 3, 2, "*")
    fill(*RED["ore"], 3, 2, "*")
    fill(22, 40, 3, 2, "*")
    fill(42, 4, 3, 2, "*")

    for ox, oy in (BLUE["origin"], RED["origin"]):   # concrete pads under the bases
        for y in range(oy, oy + 9):
            for x in range(ox - 1, ox + 13):
                g[y][x] = "="
    for _ in range(9):                  # wrecks of an old battle near the fords
        y = rnd.choice((rnd.randrange(9, 16), rnd.randrange(30, 37)))
        x = river_x(y) + rnd.choice((-5, -4, 5, 6))
        if g[y][x] in ".#,":
            g[y][x] = "x"

    lines = ["# Rustfront badlands, written by maps/badlands.py: every terrain of art/terrain.txt.",
             "# Blue (owner 0, the player) is the Directorate in the west; Red, the computer, the Clans in the east."]
    lines += ["".join(row) for row in g]
    lines.append("# Blue: directorate")
    lines += triad.base(BLUE, "directorate", 0, full=True)
    lines.append("# Red: clans")
    lines += triad.base(RED, "clans", 1, full=True)
    lines += ["credits 1 6000", "camera %d %d" % (BLUE["origin"][0] + 7, BLUE["origin"][1] + 7)]
    (HERE / "badlands.txt").write_text("\n".join(lines) + "\n")
    print("wrote badlands.txt into %s" % HERE)


if __name__ == "__main__":
    main()
