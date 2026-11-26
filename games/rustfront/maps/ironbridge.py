#!/usr/bin/env python3
"""Writes ironbridge.txt, a two-player map, mirrored west to east.

    python3 games/rustfront/maps/ironbridge.py

64 x 40 cells. A canyon a level below the plain runs north to south down the middle with a
river at its bottom; three gravel fords cross it, each with ramps down both walls. Ore lies
by each base, on the canyon floor by the outer fords (the first fights), and on top of a
stone mesa a level up in each base's south, reached by one ramp. Rocks and wrecks break up
the plains. The helpers and checks are in mp.py.
"""
import random

import mp

W, H = 64, 40
FORDS = ((5, 6), (19, 20), (33, 34))


def main():
    rnd = random.Random(11)
    m = mp.Map(W, H)
    m.blob(8, 32, 6, 4, ",")                     # meadows
    m.blob(18, 6, 5, 3, ",")
    m.rect(27, 0, 36, H - 1, ":", 0)             # the canyon
    m.rect(31, 0, 32, H - 1, "~")                # its river
    for y0, y1 in FORDS:
        m.rect(27, y0, 36, y1, "#")
        m.ramp(26, y0, 26, y1)
        m.ramp(37, y0, 37, y1)
    m.blob(29, 12, 1, 2, "*")                    # canyon ore by the north and south fords
    m.blob(29, 27, 1, 2, "*")
    m.rect(11, 30, 20, 38, "^", 2)               # the mesa, one ramp up its north face
    m.ramp(15, 30, 16, 30, "^")
    m.blob(16, 35, 2, 1, "*")
    m.blob(14, 13, 3, 2, "*")                    # home ore
    m.blob(4, 4, 2, 1, "~")                      # a pond
    for _ in range(14):                          # outcrops and wrecks
        x, y = rnd.randrange(2, 25), rnd.randrange(1, H - 1)
        if m.g[y][x] in ".," and not (2 <= x <= 13 and 13 <= y <= 27) and not (10 <= x <= 21 and 29 <= y <= 39):
            m.g[y][x] = rnd.choice("Rx")
    m.symmetrize("mirror_x")
    m.settle()
    starts = [(6, 18, "directorate"), (W - 9, 18, "clans")]
    for x, y, _ in starts:
        m.pad(x, y)
    m.write("ironbridge", "Ironbridge", "Two players, mirrored: a canyon with three fords.", starts, "mirror_x")


if __name__ == "__main__":
    main()
