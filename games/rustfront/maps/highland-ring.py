#!/usr/bin/env python3
"""Writes highland-ring.txt, a four-player map, the same turned a quarter at a time.

    python3 games/rustfront/maps/highland-ring.py

64 x 64 cells. Each base stands on a highland a level up in its corner, with two ramps down
to a ring of plain that joins all four. Inside the ring a basin a level below the plain
holds a lake with ore on its four shores, reached by a ramp on each side. Ore lies on every
highland and on the ring between neighbours. The helpers and checks are in mp.py.
"""
import random

import mp

N = 64


def main():
    rnd = random.Random(53)
    m = mp.Map(N, N)
    # Only the north-west quarter is drawn; symmetrize turns it onto the other three.
    m.rect(0, 0, 18, 18, ",", 2)                 # the highland
    m.blob(4, 14, 3, 2, "^")
    m.ramp(18, 8, 18, 9, ",")                    # ramps down its east and south faces
    m.ramp(8, 18, 9, 18, ",")
    m.blob(13, 13, 2, 2, "*")                    # home ore
    m.rect(22, 22, 31, 31, ":", 0)               # the basin ...
    m.rect(29, 29, 31, 31, "~")                  # ... its lake
    m.blob(25, 26, 2, 1, "*")                    # ... and ore on its shore
    m.ramp(27, 21, 28, 21)                       # a ramp down its north side
    m.blob(26, 7, 2, 2, "*")                     # ore on the ring between neighbours
    m.blob(26, 14, 2, 1, "^")
    for _ in range(14):
        x, y = rnd.randrange(19, 31), rnd.randrange(1, 31)
        if m.g[y][x] in ".,^" and not (20 <= x <= 31 and 20 <= y <= 31) and not (23 <= x <= 29 and 4 <= y <= 10):
            m.g[y][x] = rnd.choice("RRx")
    m.symmetrize("rot90")
    m.settle()
    starts = [(6, 6, "directorate"), (N - 9, 6, "clans"), (N - 9, N - 9, "concord"), (6, N - 9, "directorate")]
    for x, y, _ in starts:
        m.pad(x, y)
    m.write("highland-ring", "Highland Ring", "Four players, turned a quarter at a time: highlands round a basin.",
            starts, "rot90")


if __name__ == "__main__":
    main()
