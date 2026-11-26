#!/usr/bin/env python3
"""Writes the duel maps: one against one on point-symmetric ground, so neither corner has the
better start. Blue (owner 0, the player) is top left, Red (1, the computer) bottom right.

    python3 games/rustfront/maps/duel.py

    duel.txt           Directorate against Directorate
    duel-clans.txt     the player leads the Scrap Clans against the Lumen Concord
    duel-concord.txt   the player leads the Lumen Concord against the Directorate

56 x 36 cells: each base has an ore field beside it and a second one a short drive away;
a rich field sits in the middle between two gravel ridges, with rock outcrops to steer round.
The balance harness (tests/rustfront/test_balance.c) plays every faction pairing here. The map
format is w_rustfront.c's.
"""
from pathlib import Path

W, H = 56, 36
HERE = Path(__file__).resolve().parent

ROLES = {
    "directorate": dict(hq="hq", power="power", refinery="refinery", barracks="barracks", turret="turret",
                        harvester="harvester", army=["tank", "rifleman", "rifleman", "rocket"]),
    "clans": dict(hq="cl_hall", power="cl_generator", refinery="cl_smelter", barracks="cl_camp", turret="cl_tower",
                  harvester="cl_hauler", army=["cl_truck", "cl_raider", "cl_raider", "cl_torcher"]),
    "concord": dict(hq="lc_spire", power="lc_fusion", refinery="lc_extractor", barracks="lc_academy",
                    turret="lc_tower", harvester="lc_harvester", army=["lc_glaive", "lc_warden", "lc_lancer"]),
}

# Blue's base: (role, x, y, footprint w, h); Red's is the point reflection.
BASE = [("hq", 3, 3, 3, 3), ("power", 7, 3, 2, 2), ("refinery", 10, 3, 3, 2), ("barracks", 3, 8, 2, 2),
        ("turret", 12, 9, 1, 1)]
HARVESTER = (11, 6)
ARMY = (4, 12)
# A refinery docks its harvester on its south side, so Red's cannot be the plain reflection
# (its dock would face the map's bottom edge): it stands three rows up, as near its own field.
RED_REFINERY = (42, 28)
RED_HARVESTER = (45, 31)


def mirror_cell(x, y, w=1, h=1):
    return W - x - w, H - y - h


def blob(grid, cx, cy, rx, ry, ch):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            if 0 <= x < W and 0 <= y < H and ((x - cx) / (rx + 0.5)) ** 2 + ((y - cy) / (ry + 0.5)) ** 2 <= 1:
                grid[y][x] = ch


def ground():
    grid = [["."] * W for _ in range(H)]
    half = []
    blob(grid, 17, 5, 3, 2, "*")          # home field
    blob(grid, 7, 20, 3, 2, "*")          # second field, down the flank
    blob(grid, 28, 18, 4, 3, "*")         # the rich middle (its own mirror)
    blob(grid, 12, 16, 2, 1, ",")
    blob(grid, 33, 6, 3, 2, ",")
    for x in range(20, 26):
        grid[11 + (x - 20) // 3][x] = "#"  # a gravel ridge north-west of the middle
    blob(grid, 24, 27, 1, 1, "R")          # rocks to drive round
    blob(grid, 40, 11, 1, 0, "R")
    # Make it point-symmetric: the lower-right half copies the upper-left.
    for y in range(H):
        for x in range(W):
            mx, my = mirror_cell(x, y)
            if (y, x) < (my, mx) and grid[y][x] != ".":
                grid[my][mx] = grid[y][x]
            elif (y, x) < (my, mx) and grid[my][mx] != ".":
                grid[y][x] = grid[my][mx]
    return grid


def side(faction, owner):
    f = ROLES[faction]
    lines = []
    for role, x, y, w, h in BASE:
        if owner:
            x, y = RED_REFINERY if role == "refinery" else mirror_cell(x, y, w, h)
        lines.append("unit %s %d %d %d" % (f[role], owner, x, y))
    hx, hy = HARVESTER if not owner else RED_HARVESTER
    facing = 14 if not owner else 6
    lines.append("unit %s %d %d %d %d" % (f["harvester"], owner, hx, hy, facing))
    for i, name in enumerate(f["army"]):
        ax, ay = ARMY[0] + 2 * i, ARMY[1]
        if owner:
            ax, ay = mirror_cell(ax, ay)
        lines.append("unit %s %d %d %d %d" % (name, owner, ax, ay, facing))
    return lines


def write(name, blue, red):
    lines = ["# Rustfront duel map, written by maps/duel.py: '.' sand, '*' ore, '#' gravel, ',' grass, 'R' rocks.",
             "# Point-symmetric: Blue (owner 0, the player) is the %s top left, Red the %s bottom right." % (blue, red)]
    lines += ["".join(row) for row in ground()]
    lines.append("# Blue: %s" % blue)
    lines += side(blue, 0)
    lines.append("# Red: %s" % red)
    lines += side(red, 1)
    lines += ["camera 9 8"]
    (HERE / name).write_text("\n".join(lines) + "\n")


def main():
    write("duel.txt", "directorate", "directorate")
    write("duel-clans.txt", "clans", "concord")
    write("duel-concord.txt", "concord", "directorate")
    print("wrote duel.txt, duel-clans.txt, duel-concord.txt into %s" % HERE)


if __name__ == "__main__":
    main()
