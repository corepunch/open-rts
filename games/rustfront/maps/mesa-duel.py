#!/usr/bin/env python3
"""Writes mesa-duel.txt, a two-player map, the same turned half a circle about its middle.

    python3 games/rustfront/maps/mesa-duel.py

56 x 56 cells. The bases face each other across a stone mesa a level up in the middle, with
the richest ore on top and a ramp up each of its four faces. The other two corners hold
silt basins a level down around a pond, with ore and two ramps each: the safer expansions.
The helpers and checks are in mp.py.
"""
import random

import mp

W, H = 56, 56


def main():
    rnd = random.Random(23)
    m = mp.Map(W, H)
    # Only the north half is drawn; symmetrize turns it onto the south half.
    m.blob(20, 6, 6, 3, ",")                     # meadows
    m.blob(4, 22, 3, 5, ",")
    m.rect(20, 20, 35, 27, "^", 2)               # the mesa (its north half)
    m.ramp(24, 20, 25, 20, "^")                  # up its north face ...
    m.ramp(35, 24, 35, 25, "^")                  # ... and its east face (the west one by symmetry)
    m.blob(27, 25, 3, 2, "*")                    # rich ore on top
    m.rect(40, 0, 55, 14, ":", 0)                # the north-east basin
    m.blob(50, 4, 3, 2, "~")                     # its pond
    m.blob(44, 7, 2, 2, "*")
    m.ramp(39, 6, 39, 7)                         # ramps down its west and south walls
    m.ramp(46, 15, 47, 15)
    m.blob(14, 15, 3, 2, "*")                    # home ore
    for _ in range(16):
        x, y = rnd.randrange(1, W - 1), rnd.randrange(1, 27)
        if m.g[y][x] in ".,:" and not (1 <= x <= 18 and 1 <= y <= 18) and not (19 <= x <= 36 and 19 <= y <= 27) \
                and not (38 <= x <= 48 and 4 <= y <= 16):
            m.g[y][x] = rnd.choice("RRx")
    m.symmetrize("rot180")
    m.settle()
    starts = [(6, 6, "directorate"), (W - 9, H - 9, "concord")]
    for x, y, _ in starts:
        m.pad(x, y)
    m.write("mesa-duel", "Mesa Duel", "Two players, turned about the middle: a mesa of ore.", starts, "rot180")


if __name__ == "__main__":
    main()
