#!/usr/bin/env python3
"""Writes three-crowns.txt, a three-player map, mirrored west to east.

    python3 games/rustfront/maps/three-crowns.py

71 x 64 cells. Two bases in the north corners and one in the south middle, as far from each
other as from the third (about 52 cells as the crow flies). A stone plateau a level up in the
middle holds the richest ore, with one ramp facing each base. Between every two bases lies
an expansion, and a ravine a level down cuts across each northern flank. The helpers and
checks are in mp.py.
"""
import random

import mp

W, H = 71, 64
AXIS = W // 2   # column 35 is its own mirror


def main():
    rnd = random.Random(31)
    m = mp.Map(W, H)
    # Only the west half (and the axis) is drawn; symmetrize mirrors it east.
    m.blob(10, 30, 6, 6, ",")
    m.blob(30, 54, 5, 4, ",")
    m.rect(26, 24, AXIS, 37, "^", 2)             # the plateau
    m.ramp(26, 28, 26, 29, "^")                  # west ramp (east by mirror) ...
    m.ramp(34, 37, AXIS, 37, "^")                # ... and the south one, three cells wide
    m.blob(AXIS, 30, 3, 3, "*")                  # its rich ore
    m.rect(16, 16, 27, 19, ":", 0)               # the ravines, with a ramp down each end
    m.rect(19, 17, 24, 18, "~")
    m.ramp(15, 16, 15, 17)
    m.ramp(28, 18, 28, 19)
    m.blob(AXIS, 7, 3, 2, "*")                   # north expansion, between the north bases
    m.blob(14, 46, 3, 2, "*")                    # south-west expansion
    m.blob(15, 13, 3, 2, "*")                    # home ore, north-west base
    m.blob(AXIS, 46, 3, 2, "*")                  # home ore, south base
    for _ in range(18):
        x, y = rnd.randrange(1, AXIS - 1), rnd.randrange(1, H - 1)
        if m.g[y][x] in ".," and not (2 <= x <= 20 and 2 <= y <= 20) and not (24 <= x <= 47 and 22 <= y <= 39) \
                and not (26 <= x <= 47 and 40 <= y <= 63):
            m.g[y][x] = rnd.choice("RRx")
    m.symmetrize("mirror_x")
    m.settle()
    starts = [(8, 8, "directorate"), (W - 11, 8, "clans"), (AXIS - 1, 52, "concord")]
    for x, y, _ in starts:
        m.pad(x, y)
    m.write("three-crowns", "Three Crowns", "Three players, mirrored: a plateau with a ramp for each.", starts,
            "mirror_x")


if __name__ == "__main__":
    main()
