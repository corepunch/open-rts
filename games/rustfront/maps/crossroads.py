#!/usr/bin/env python3
"""Writes crossroads.txt, a four-player map, mirrored both ways.

    python3 games/rustfront/maps/crossroads.py

72 x 56 cells. A cross of canyons a level down splits the plain into four quarters, one base
in each corner. Rivers run down the arms of the cross and meet at a gravel plaza in the
middle, ringed with the richest ore. Each quarter has a ramp down into each arm beside it,
by a ford across the river, and a stone mesa a level up with ore of its own. Good for two
against two (neighbours share an arm) or everyone for themselves. The helpers and checks are
in mp.py.
"""
import random

import mp

W, H = 72, 56


def main():
    rnd = random.Random(41)
    m = mp.Map(W, H)
    # Only the north-west quarter is drawn; symmetrize mirrors it onto the other three.
    m.blob(12, 20, 5, 3, ",")
    m.blob(24, 14, 4, 3, ",")
    m.rect(0, 24, 35, 27, ":", 0)                # the west arm (north half)
    m.rect(31, 0, 35, 27, ":", 0)                # the north arm (west half)
    m.rect(0, 27, 30, 27, "~")                   # their rivers
    m.rect(35, 0, 35, 22, "~")
    m.rect(31, 23, 35, 27, "#")                  # the plaza
    m.rect(32, 24, 35, 25, "*")                  # its ore
    m.rect(12, 24, 13, 27, "#")                  # fords, and ramps down to them
    m.ramp(12, 23, 13, 23)
    m.rect(31, 10, 35, 11, "#")
    m.ramp(30, 10, 30, 11)
    m.ramp(22, 23, 23, 23)                       # a second way down to the west arm
    m.rect(20, 3, 27, 9, "^", 2)                 # the quarter's mesa
    m.ramp(23, 9, 24, 9, "^")
    m.blob(23, 5, 1, 1, "*")
    m.blob(15, 14, 3, 2, "*")                    # home ore
    for _ in range(12):
        x, y = rnd.randrange(1, 30), rnd.randrange(1, 23)
        if m.g[y][x] in ".," and not (2 <= x <= 19 and 2 <= y <= 17) and not (19 <= x <= 28 and 2 <= y <= 10):
            m.g[y][x] = rnd.choice("RRx")
    m.symmetrize("mirror_xy")
    m.settle()
    starts = [(7, 7, "directorate"), (W - 10, 7, "clans"), (W - 10, H - 10, "concord"), (7, H - 10, "clans")]
    for x, y, _ in starts:
        m.pad(x, y)
    m.write("crossroads", "Crossroads", "Four players, mirrored both ways: a cross of canyons.", starts, "mirror_xy")


if __name__ == "__main__":
    main()
