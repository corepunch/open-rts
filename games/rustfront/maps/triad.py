#!/usr/bin/env python3
"""Writes the three-way maps: one per faction the player can take, the other two run by the
computer. Blue (owner 0) is the player; Red (1) and Green (2) are the computer.

    python3 games/rustfront/maps/triad.py

    triad.txt          the player leads the Directorate against the Clans and the Concord
    triad-clans.txt    the player leads the Scrap Clans
    triad-concord.txt  the player leads the Lumen Concord

64 x 44 cells: bases in the top-left, top-right and bottom-middle, an ore field beside each,
two richer ones between them and gravel ridges. The map format is w_rustfront.c's.
"""
from pathlib import Path

W, H = 64, 44
HERE = Path(__file__).resolve().parent

# Each faction's names for the roles and its starting force (map unit names, w_rustfront.c).
FACTIONS = {
    "directorate": dict(hq="hq", power="power", refinery="refinery", barracks="barracks", factory="factory",
                        turret="turret", harvester="harvester",
                        army=["tank", "tank", "apc", "rifleman", "rifleman", "rifleman", "rocket", "rocket"]),
    "clans": dict(hq="cl_hall", power="cl_generator", refinery="cl_smelter", barracks="cl_camp", factory="cl_yard",
                  turret="cl_tower", harvester="cl_hauler",
                  army=["cl_crawler", "cl_truck", "cl_trike", "cl_trike", "cl_raider", "cl_raider", "cl_raider",
                        "cl_raider", "cl_torcher", "cl_torcher"]),
    "concord": dict(hq="lc_spire", power="lc_fusion", refinery="lc_extractor", barracks="lc_academy",
                    factory="lc_foundry", turret="lc_tower", harvester="lc_harvester",
                    army=["lc_glaive", "lc_skimmer", "lc_warden", "lc_warden", "lc_warden", "lc_lancer"]),
}

# Base corners (top-left cell of the 13 x 12 base block), the way its army faces (0..15
# counter-clockwise from east) and where its ore field is.
SEATS = [
    dict(origin=(2, 2), facing=14, ore=(15, 8)),     # top-left: faces down-right
    dict(origin=(49, 2), facing=10, ore=(44, 8)),    # top-right: faces down-left
    dict(origin=(25, 30), facing=4, ore=(20, 36)),   # bottom-middle: faces up
]


def blob(grid, cx, cy, rx, ry, ch):
    for y in range(cy - ry, cy + ry + 1):
        for x in range(cx - rx, cx + rx + 1):
            if 0 <= x < W and 0 <= y < H and ((x - cx) / (rx + 0.5)) ** 2 + ((y - cy) / (ry + 0.5)) ** 2 <= 1:
                grid[y][x] = ch


def base(seat, faction, owner, full):
    """Unit lines for one side: a base laid out like the skirmish map's, inside a 13 x 12 block."""
    f = FACTIONS[faction]
    ox, oy = seat["origin"]
    lines = ["unit %s %d %d %d" % (f["hq"], owner, ox, oy + 1), "unit %s %d %d %d" % (f["power"], owner, ox + 4, oy + 1),
             "unit %s %d %d %d" % (f["refinery"], owner, ox + 7, oy + 1),
             "unit %s %d %d %d" % (f["barracks"], owner, ox, oy + 5)]
    if full:
        lines += ["unit %s %d %d %d" % (f["factory"], owner, ox + 3, oy + 5),
                  "unit %s %d %d %d" % (f["turret"], owner, ox + 8, oy + 5),
                  "unit %s %d %d %d" % (f["turret"], owner, ox + 11, oy + 2)]
    else:
        lines.append("unit %s %d %d %d" % (f["turret"], owner, ox + 8, oy + 5))
    lines.append("unit %s %d %d %d %d" % (f["harvester"], owner, ox + 8, oy + 4, seat["facing"]))
    army = f["army"] if full else f["army"][: len(f["army"]) // 2]
    for i, name in enumerate(army):
        lines.append("unit %s %d %d %d %d" % (name, owner, ox + 1 + (i % 6) * 2, oy + 9 + i // 6, seat["facing"]))
    return lines


def write(name, order):
    grid = [["."] * W for _ in range(H)]
    for seat in SEATS:
        blob(grid, *seat["ore"], 3, 2, "*")
    blob(grid, 32, 20, 4, 3, "*")                       # the rich middle
    blob(grid, 32, 6, 3, 2, "*")
    for x in range(22, 28):
        grid[16 + (x - 22) // 2][x] = "#"
    for x in range(37, 43):
        grid[18 - (x - 37) // 2][x] = "#"
    for x in range(28, 37):
        grid[28][x] = "#"
    lines = ["# Rustfront three-way map, written by maps/triad.py: '.' sand, '*' ore, '#' gravel.",
             "# Blue (owner 0, the player) is the %s; Red and Green are the computer." % order[0]]
    lines += ["".join(row) for row in grid]
    for owner, faction in enumerate(order):
        lines.append("# %s: %s" % (("Blue", "Red", "Green")[owner], faction))
        lines += base(SEATS[owner], faction, owner, full=owner == 0)
    lines += ["credits 1 6000", "credits 2 6000", "camera %d %d" % (SEATS[0]["origin"][0] + 6, SEATS[0]["origin"][1] + 7)]
    (HERE / name).write_text("\n".join(lines) + "\n")


def main():
    write("triad.txt", ["directorate", "clans", "concord"])
    write("triad-clans.txt", ["clans", "concord", "directorate"])
    write("triad-concord.txt", ["concord", "directorate", "clans"])
    print("wrote triad.txt, triad-clans.txt, triad-concord.txt into %s" % HERE)


if __name__ == "__main__":
    main()
