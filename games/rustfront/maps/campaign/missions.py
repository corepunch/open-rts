#!/usr/bin/env python3
"""Writes Rustfront's campaign: five missions for each faction, fifteen maps in this directory.

    python3 games/rustfront/maps/campaign/missions.py

Each mission is an ordinary Rustfront map (cell rows, `level` rows, `unit` lines; see
w_rustfront.c and docs/TERRAIN.md) with the mission keys of g_campaign.c: title, briefing,
objectives, tech limits, triggers, spawns and messages. docs/CAMPAIGN.md describes the format
and tells the story. Blue (owner 0) is always the player; Red (1) and Green (2) the enemy.

The script checks what it writes: every thing stands inside the map on open, flat ground
away from any cliff, no two footprints overlap, and every tag an objective or trigger names
is placed. tests/rustfront/test_campaign*.c load the maps and play them through.
"""
import math
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent

FOOTPRINT = {}
for names, size in ((("hq", "cl_hall", "lc_spire", "factory", "cl_yard", "lc_foundry"), (3, 3)),
                    (("power", "cl_generator", "lc_fusion", "barracks", "cl_camp", "lc_academy"), (2, 2)),
                    (("refinery", "cl_smelter", "lc_extractor"), (3, 2)),
                    (("turret", "cl_tower", "lc_tower"), (1, 1))):
    for n in names:
        FOOTPRINT[n] = size

# Each faction's names for the roles (map unit names, w_rustfront.c).
ROLES = {
    "directorate": dict(hq="hq", power="power", refinery="refinery", barracks="barracks", factory="factory",
                        turret="turret", harvester="harvester", infantry="rifleman", heavy_infantry="rocket",
                        light="scout", tank="tank", support="apc", artillery="artillery", top="mastodon"),
    "clans": dict(hq="cl_hall", power="cl_generator", refinery="cl_smelter", barracks="cl_camp", factory="cl_yard",
                  turret="cl_tower", harvester="cl_hauler", infantry="cl_raider", heavy_infantry="cl_torcher",
                  light="cl_trike", tank="cl_crawler", support="cl_truck", artillery="cl_rig", top="cl_crawler"),
    "concord": dict(hq="lc_spire", power="lc_fusion", refinery="lc_extractor", barracks="lc_academy",
                    factory="lc_foundry", turret="lc_tower", harvester="lc_harvester", infantry="lc_warden",
                    heavy_infantry="lc_lancer", light="lc_skimmer", tank="lc_glaive", support="lc_skimmer",
                    artillery="lc_prism", top="lc_glaive"),
}
BLOCKING = set("~Rx")
# Facings, counter-clockwise from east.
EAST, NORTH, WEST, SOUTH = 0, 4, 8, 12


class Mission:
    def __init__(self, name, w, h, ground=".", level=1, seed=1):
        self.name, self.w, self.h = name, w, h
        self.g = [[ground] * w for _ in range(h)]
        self.lv = [[str(level)] * w for _ in range(h)]
        self.rnd = random.Random(seed)
        self.units = []      # (name, owner, x, y, facing, tag)
        self.keys = []       # mission lines, in order
        self.spawns = []     # (trigger, name, owner, x, y, facing, tag)
        self.tags_needed = set()
        self.camera = None
        self.credits = {}
        self.clutter = []

    # ── ground ──
    def blob(self, cx, cy, rx, ry, ch, level=None):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                if 0 <= x < self.w and 0 <= y < self.h and \
                        ((x - cx) / (rx + 0.5)) ** 2 + ((y - cy) / (ry + 0.5)) ** 2 <= 1:
                    self.g[y][x] = ch
                    if level is not None:
                        self.lv[y][x] = str(level)

    def rect(self, x0, y0, w, h, ch=None, level=None):
        for y in range(max(0, y0), min(self.h, y0 + h)):
            for x in range(max(0, x0), min(self.w, x0 + w)):
                if ch is not None:
                    self.g[y][x] = ch
                if level is not None:
                    self.lv[y][x] = str(level)

    def plateau(self, x0, y0, w, h, level, ground="^", ramps=()):
        """A raised block with cut corners; ramps are (side, offset, width) on its edge rows."""
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                if (x in (x0, x0 + w - 1)) and (y in (y0, y0 + h - 1)):
                    continue
                self.g[y][x], self.lv[y][x] = ground, str(level)
        for side, off, width in ramps:
            for i in range(width):
                if side == "S":
                    x, y = x0 + off + i, y0 + h - 1
                elif side == "N":
                    x, y = x0 + off + i, y0
                elif side == "W":
                    x, y = x0, y0 + off + i
                else:
                    x, y = x0 + w - 1, y0 + off + i
                self.lv[y][x] = "/"
                self.g[y][x] = "#"

    def canyon(self, cx, fords, wiggle=3.0, phase=0.0, low=0, width=(-3, 6), water=(-1, 2)):
        """A river running north to south at the bottom of a canyon one level down, with gravel
        fords that ramp down both walls (as maps/badlands.py)."""
        bend = lambda y: cx + wiggle * math.sin(y / 5.5 + phase) + 1.0 * math.sin(y / 2.7 + phase)

        def river_x(y):
            x = bend(y)
            for fy in fords:
                t = max(0.0, min(1.0, 1 - (abs(y - fy - 0.5) - 2.5) / 4))
                x = x * (1 - t) + bend(fy) * t
            return round(x)
        lo, hi = width
        for y in range(self.h):
            x = river_x(y)
            for dx in range(lo, hi):
                self.g[y][x + dx], self.lv[y][x + dx] = ":", str(low)
            for dx in range(water[0], water[1] + (y % 9 < 4)):
                self.g[y][x + dx] = "~"
        for fy in fords:
            x = river_x(fy)
            for y in (fy, fy + 1, fy + 2):
                for dx in range(lo, hi):
                    self.g[y][x + dx] = "#"
                self.lv[y][x + lo - 1] = self.lv[y][x + hi] = "/"
                self.g[y][x + lo - 1] = self.g[y][x + hi] = "#"
        return river_x

    def scatter(self, ch, count, box, onto=".,#"):
        """Blocking clutter (wrecks, rocks), placed when the map is written, clear of things."""
        self.clutter.append((ch, count, box, onto))

    def place_clutter(self):
        keep = set()
        for name, owner, x, y, *_ in self.units + [s[1:] for s in self.spawns]:
            fw, fh = FOOTPRINT.get(name, (1, 1))
            keep |= {(x + dx, y + dy) for dy in range(-2, fh + 2) for dx in range(-2, fw + 2)}
        for ch, count, box, onto in self.clutter:
            self._scatter(ch, count, box, onto, keep)

    def _scatter(self, ch, count, box, onto, keep):
        x0, y0, x1, y1 = box
        for _ in range(count):
            x, y = self.rnd.randrange(x0, x1), self.rnd.randrange(y0, y1)
            if (x, y) in keep:
                continue
            flat = all(0 <= nx < self.w and 0 <= ny < self.h and self.lv[ny][nx] == self.lv[y][x]
                       for ny in range(y - 1, y + 2) for nx in range(x - 1, x + 2))
            if flat and self.g[y][x] in onto:
                self.g[y][x] = ch

    def settle(self):
        """A raised cell with no corner up would sink (w_terrain.c): sink it here."""
        W, H, lv, g = self.w, self.h, self.lv, self.g

        def corner(cx, cy):
            vals = [int(lv[y][x]) for y in (cy - 1, cy) for x in (cx - 1, cx)
                    if 0 <= x < W and 0 <= y < H and lv[y][x] != "/"]
            return min(vals) if vals else 0
        changed = True
        while changed:
            changed = False
            for y in range(H):
                for x in range(W):
                    if lv[y][x] == "/":
                        continue
                    top = max(corner(x, y), corner(x + 1, y), corner(x, y + 1), corner(x + 1, y + 1))
                    if top < int(lv[y][x]):
                        lv[y][x], changed = str(top), True
                        if g[y][x] == "^":
                            g[y][x] = "."

    # ── things ──
    def unit(self, name, owner, x, y, facing=0, tag=None):
        self.units.append((name, owner, x, y, facing, tag))

    def squad(self, names, owner, x, y, facing=0, per_row=6, step=2):
        for i, n in enumerate(names):
            self.unit(n, owner, x + (i % per_row) * step, y + (i // per_row), facing)

    def base(self, faction, owner, ox, oy, parts=("hq", "power", "refinery", "barracks", "factory", "turret",
                                                   "turret2"), facing=EAST, tags=None, pad=True):
        """A base in a 13 x 11 block whose top-left is (ox, oy), laid out as maps/triad.py's."""
        r, tags = ROLES[faction], tags or {}
        at = dict(hq=(ox, oy), power=(ox + 4, oy), refinery=(ox + 7, oy), barracks=(ox, oy + 4),
                  factory=(ox + 3, oy + 4), turret=(ox + 8, oy + 4), turret2=(ox + 11, oy + 1),
                  turret3=(ox + 11, oy + 6), harvester=(ox + 8, oy + 3))
        if pad:
            self.rect(ox - 1, oy - 1, 15, 10, "=")
        for p in parts:
            role = "turret" if p.startswith("turret") else p
            self.unit(r[role], owner, *at[p], facing if role == "harvester" else 0, tags.get(p))
        if "refinery" in parts and "harvester" not in parts:
            self.unit(r["harvester"], owner, *at["harvester"], facing)

    def key(self, line):
        self.keys.append(line)

    def trigger(self, name, spec):
        self.key("trigger %s %s" % (name, spec))

    def spawn(self, trig, names, owner, x, y, facing=0, per_row=4, tag=None):
        """Units in rows from (x, y), growing into the map from an edge."""
        sx = -2 if x > self.w * 0.6 else 2
        sy = -1 if y > self.h * 0.6 else 1
        for i, n in enumerate(names if isinstance(names, list) else [names]):
            ux, uy = x + (i % per_row) * sx, y + (i // per_row) * sy
            self.spawns.append((trig, n, owner, ux, uy, facing, tag if i == 0 else None))
            self.key("spawn %s %s %d %d %d %d%s" % (trig, n, owner, ux, uy, facing,
                                                    " @" + tag if tag and i == 0 else ""))

    def message(self, trig, text):
        self.key("message %s %s" % (trig, text))

    def objective(self, spec, text):
        kind = spec.split()[0]
        if kind in ("destroy", "protect"):
            self.tags_needed.add(spec.split()[1])
        self.key("objective %s | %s" % (spec, text))

    # ── checks and output ──
    def check(self):
        # Things placed together must not overlap: the map's own, and each trigger's.
        occupied = {}
        for group, name, owner, x, y, facing, tag in [("map",) + u for u in self.units] + list(self.spawns):
            fw, fh = FOOTPRINT.get(name, (1, 1))
            cells = [(x + dx, y + dy) for dy in range(fh) for dx in range(fw)]
            for cx, cy in cells:
                where = "%s: %s of owner %d at %d,%d" % (self.name, name, owner, x, y)
                assert 0 < cx < self.w - 1 and 0 < cy < self.h - 1, where + " leaves the map"
                assert self.g[cy][cx] not in BLOCKING, where + " stands on blocking ground"
                lvl = self.lv[cy][cx]
                assert lvl != "/", where + " stands on a ramp"
                for ny in range(cy - 1, cy + 2):
                    for nx in range(cx - 1, cx + 2):
                        assert self.lv[ny][nx] == lvl, where + " stands at a cliff"
                assert (group, cx, cy) not in occupied, where + " overlaps " + occupied[(group, cx, cy)]
                occupied[(group, cx, cy)] = name
        placed = {t for *_, t in self.units if t} | {t for *_, t in self.spawns if t}
        assert self.tags_needed <= placed, "%s: untagged %s" % (self.name, self.tags_needed - placed)

    def write(self):
        self.settle()
        self.place_clutter()
        self.check()
        lines = ["# Rustfront campaign mission, written by maps/campaign/missions.py (docs/CAMPAIGN.md)."]
        lines += [k for k in self.keys if k.split()[0] in ("mission", "title", "briefing", "objective", "tech")]
        lines += ["".join(row) for row in self.g]
        lines += ["level " + "".join(row) for row in self.lv]
        for name, owner, x, y, facing, tag in self.units:
            lines.append("unit %s %d %d %d %d%s" % (name, owner, x, y, facing, " @" + tag if tag else ""))
        for owner, n in sorted(self.credits.items()):
            lines.append("credits %d %d" % (owner, n))
        if self.camera:
            lines.append("camera %d %d" % self.camera)
        lines += [k for k in self.keys if k.split()[0] in ("trigger", "spawn", "message")]
        (HERE / (self.name + ".txt")).write_text("\n".join(lines) + "\n")


def header(m, faction, n, title, briefing):
    m.key("mission %s %d" % (faction, n))
    m.key("title " + title)
    for para in briefing:
        m.key("briefing " + para)


def tech(m, faction, roles):
    r = ROLES[faction]
    m.key("tech " + " ".join(r[x] if x in r else x for x in roles))


# ═══ The Directorate: "The Iron Mandate" ═══════════════════════════════════════════════════

def directorate_1():
    m = Mission("directorate-1", 48, 32, seed=11)
    header(m, "directorate", 1, "Foothold", [
        "Commander, the Directorate has come back to the Rustfront. Forty years after the collapse the "
        "old foundry belt is a desert of slag and wreckage, but the slag has grown ore, and ore is power.",
        "You have a Command Center and a rifle squad. Raise a Power Plant, then an Ore Refinery: it comes "
        "with a harvester that mines the ore field east of the base on its own. Train riflemen at a Barracks.",
        "Bank 4000 credits to prove the claim pays. Scavengers work these flats; keep your squad close."])
    tech(m, "directorate", ["power", "refinery", "barracks", "infantry"])
    m.objective("build refinery", "Build an Ore Refinery")
    m.objective("build barracks", "Build a Barracks")
    m.objective("credits 4000", "Bank 4000 credits")
    m.blob(18, 12, 4, 3, "*")
    m.blob(36, 24, 3, 2, "*")
    m.blob(8, 26, 6, 3, ",")
    m.blob(40, 6, 5, 3, ",")
    m.scatter("x", 10, (24, 2, 46, 30))
    m.scatter("R", 6, (2, 18, 20, 30))
    m.rect(3, 4, 13, 4, "=")
    m.unit("hq", 0, 4, 5)
    m.squad(["rifleman"] * 4 + ["scout"], 0, 4, 10, EAST)
    m.credits[0] = 3000
    m.camera = (9, 9)
    m.trigger("start", "time 3")
    m.message("start", "Select the Command Center and build a Power Plant from the sidebar.")
    m.trigger("powered", "built power")
    m.message("powered", "Power is up. Now build an Ore Refinery near the ore field.")
    m.trigger("refined", "built refinery")
    m.message("refined", "The harvester will mine on its own. Add a Barracks to train riflemen.")
    m.trigger("scavengers", "time 150")
    m.message("scavengers", "Scavengers on the eastern flats! Keep riflemen by the refinery.")
    m.spawn("scavengers", ["cl_raider"] * 3, 1, 44, 12, WEST)
    m.trigger("more", "time 260")
    m.spawn("more", ["cl_raider"] * 3 + ["cl_torcher"], 1, 44, 20, WEST)
    m.write()


def directorate_2():
    m = Mission("directorate-2", 48, 32, seed=12)
    header(m, "directorate", 2, "Relay Station", [
        "The Mandate's long-range relay on Ridge Nine is our only link to the coast. The Scrap Clans "
        "want its copper and will throw everything they have at it.",
        "Hold the relay's Power Plant for five minutes until the convoy arrives. Gun Turrets and Rocket "
        "Soldiers are cleared for this operation. The clans come from the north and the east.",
        "If the relay falls, the mission fails."])
    tech(m, "directorate", ["power", "refinery", "barracks", "turret", "infantry", "heavy_infantry"])
    m.objective("protect relay", "Keep the relay standing")
    m.objective("survive 300", "Hold out for five minutes")
    m.blob(10, 24, 4, 2, "*")
    m.blob(30, 26, 6, 3, ",")
    m.blob(40, 8, 4, 3, "^")
    m.scatter("R", 8, (34, 3, 46, 14), onto="^.")
    m.scatter("x", 8, (14, 2, 46, 30))
    m.base("directorate", 0, 3, 13, parts=("hq", "power", "refinery", "barracks", "turret"))
    m.rect(26, 12, 8, 7, "=")
    m.unit("power", 0, 28, 14, tag="relay")
    m.unit("turret", 0, 27, 13)
    m.unit("turret", 0, 31, 13)
    m.unit("turret", 0, 31, 16)
    m.squad(["rifleman"] * 4 + ["rocket"] * 3, 0, 24, 19, NORTH, per_row=4)
    m.squad(["rifleman"] * 3, 0, 5, 22, EAST)
    m.credits[0] = 4000
    m.camera = (22, 15)
    for i, (t, when, names, x, y) in enumerate((
            ("w1", 40, ["cl_raider"] * 4, 26, 1),
            ("w2", 90, ["cl_raider"] * 3 + ["cl_torcher"] * 2, 44, 14),
            ("w3", 140, ["cl_raider"] * 4 + ["cl_trike"] * 2, 30, 1),
            ("w4", 195, ["cl_truck", "cl_trike", "cl_trike"] + ["cl_raider"] * 3, 44, 18),
            ("w5", 250, ["cl_crawler"] + ["cl_raider"] * 4 + ["cl_torcher"] * 2, 22, 1))):
        m.trigger(t, "time %d" % when)
        m.spawn(t, names, 1, x, y, SOUTH if y < 5 else WEST)
    m.message("w1", "Clan raiders coming down from the ridge!")
    m.message("w3", "Trikes! Rocket Soldiers stop vehicles best.")
    m.message("w5", "Their war party is here. Hold the relay!")
    m.trigger("convoy", "time 290")
    m.message("convoy", "The convoy is in sight. Hang on, Commander.")
    m.spawn("convoy", ["tank", "tank", "apc"], 0, 4, 28, EAST)
    m.write()


def directorate_3():
    m = Mission("directorate-3", 64, 44, seed=13)
    header(m, "directorate", 3, "The Ford", [
        "The Glass River cuts the Rustfront in two, deep in its canyon. Only two fords cross it, and the "
        "Clans have dug in at the southern one with a War Camp and spike towers.",
        "Your War Factory is cleared to build Scout Buggies, Battle Tanks and APCs. Take the southern "
        "ford and hold it for a minute so the engineers can mark it, then burn out the War Camp on the "
        "far bank.",
        "Ramps lead down the canyon walls at each ford; the walls themselves cannot be climbed."])
    tech(m, "directorate", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                            "light", "tank", "support", "harvester"])
    river_x = m.canyon(31, (12, 30), phase=0.6)
    fx = river_x(30)
    m.objective("hold %d 30 9 3 60" % (fx - 3), "Hold the southern ford for one minute")
    m.objective("destroy camp", "Destroy the clans' War Camp")
    m.blob(12, 34, 6, 4, ",")
    m.blob(52, 8, 6, 4, ",")
    m.blob(16, 20, 3, 2, "*")
    m.blob(50, 26, 3, 2, "*")
    m.blob(fx + 1, 21, 1, 1, "*")
    m.scatter("x", 14, (3, 3, 60, 40))
    m.scatter("R", 6, (40, 34, 62, 42))
    m.base("directorate", 0, 3, 4)
    m.squad(["tank"] * 3 + ["apc"] + ["rifleman"] * 4 + ["rocket"] * 2, 0, 4, 16, EAST, per_row=5)
    m.credits[0] = 5000
    m.camera = (10, 12)
    # the clan bridgehead on the east bank
    m.rect(44, 28, 12, 9, "=")
    m.unit("cl_camp", 1, 46, 30, tag="camp")
    m.unit("cl_generator", 1, 50, 30)
    m.unit("cl_tower", 1, fx + 8, 27)
    m.unit("cl_tower", 1, fx + 8, 33)
    m.unit("cl_tower", 1, 54, 33)
    m.squad(["cl_raider"] * 5 + ["cl_torcher"] * 2, 1, 44, 34, WEST, per_row=4)
    m.squad(["cl_truck", "cl_trike"], 1, 52, 26, WEST)
    m.credits[1] = 0
    m.trigger("ford", "area %d 28 11 7" % (fx - 4))
    m.message("ford", "You are at the ford. Clear it and hold it.")
    m.trigger("raid", "time 200 150 3")
    m.spawn("raid", ["cl_raider"] * 4 + ["cl_trike"], 1, 58, 12, WEST)
    m.trigger("taken", "destroyed camp")
    m.message("taken", "The War Camp is burning. The ford is ours.")
    m.write()


def directorate_4():
    m = Mission("directorate-4", 64, 44, seed=14)
    header(m, "directorate", 4, "Glasswater", [
        "Beyond the river the Lumen Concord has raised a Spire on the Glasswater mesa. Its light "
        "drinks the ore from the ground for leagues around; our harvesters are coming back empty.",
        "The mesa has one ramp, on its south face, and the Concord guard it with beam towers. "
        "Artillery outranges a beam tower. Build up, break the guard and bring down the Spire.",
        "The Concord do not attack often, but when they come they come hard."])
    tech(m, "directorate", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                            "light", "tank", "support", "artillery", "harvester"])
    m.objective("destroy spire", "Destroy the Concord Spire")
    m.plateau(34, 3, 26, 17, 2, ground="^", ramps=(("S", 11, 3),))
    m.blob(12, 30, 7, 5, ",")
    m.blob(18, 20, 4, 2, "*")
    m.blob(26, 40, 4, 1, "*")
    m.blob(30, 30, 3, 2, "*")
    m.blob(52, 32, 3, 2, "*")
    m.scatter("x", 10, (2, 22, 60, 42))
    m.scatter("R", 5, (36, 5, 58, 9), onto="^")
    m.base("directorate", 0, 3, 28)
    m.squad(["tank"] * 4 + ["artillery"] * 2 + ["rocket"] * 4 + ["rifleman"] * 4, 0, 18, 26, NORTH, per_row=7)
    m.credits[0] = 6000
    m.camera = (12, 30)
    m.base("concord", 1, 40, 6, parts=("hq", "power", "refinery", "barracks", "turret", "turret2", "turret3"),
           tags={"hq": "spire"}, pad=False)
    m.blob(54, 15, 2, 1, "*")
    m.unit("lc_tower", 1, 44, 16)
    m.unit("lc_tower", 1, 49, 16)
    m.squad(["lc_glaive", "lc_lancer", "lc_lancer", "lc_warden", "lc_warden"], 1, 40, 14, SOUTH, per_row=5)
    m.credits[1] = 3000
    m.trigger("ramp", "area 42 18 9 5")
    m.message("ramp", "That is the ramp. Bring the artillery up behind the tanks.")
    m.trigger("down", "destroyed spire")
    m.message("down", "The Spire is down. The ore will come back.")
    m.write()


def directorate_5():
    m = Mission("directorate-5", 64, 44, seed=15)
    header(m, "directorate", 5, "Iron Dawn", [
        "The Clans and the Concord have made common cause against us, the scavengers' numbers behind "
        "the Concord's light. Together they hold the north. Separately they will fall.",
        "The Mastodon is cleared for the field. You have every factory the Directorate owns on the "
        "Rustfront. Destroy both armies, every structure and every unit, and the Rustfront is ours.",
        "This is the Iron Dawn, Commander. There is no second wave behind you."])
    m.objective("destroy_all", "Destroy every enemy unit and structure")
    m.canyon(32, (8, 33), wiggle=2.0, phase=1.3, low=0)
    m.blob(8, 38, 6, 3, ",")
    m.blob(19, 30, 4, 2, "*")
    m.blob(8, 24, 3, 1, "*")
    m.blob(46, 30, 3, 2, "*")
    m.blob(16, 14, 3, 2, "*")
    m.blob(50, 14, 3, 2, "*")
    m.scatter("x", 14, (2, 2, 62, 42))
    m.base("directorate", 0, 3, 31, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["mastodon", "tank", "tank", "tank", "artillery", "apc"] + ["rocket"] * 4 + ["rifleman"] * 4,
            0, 14, 26, NORTH, per_row=6)
    m.credits[0] = 8000
    m.camera = (12, 33)
    m.base("clans", 1, 3, 3, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["cl_crawler", "cl_truck", "cl_truck", "cl_trike"] + ["cl_raider"] * 6, 1, 4, 13, SOUTH, per_row=5)
    m.credits[1] = 3500
    m.base("concord", 2, 47, 3, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["lc_glaive", "lc_glaive", "lc_skimmer", "lc_lancer", "lc_warden", "lc_warden"], 2, 47, 13, SOUTH)
    m.credits[2] = 3500
    m.trigger("start", "time 4")
    m.message("start", "The Clans hold the north-west, the Concord the north-east. Choose your first target.")
    m.trigger("raid", "time 240 180 3")
    m.message("raid", "Clan raiders are crossing the southern ford!")
    m.spawn("raid", ["cl_raider"] * 4 + ["cl_trike", "cl_truck"], 1, 52, 40, WEST)
    m.write()


# ═══ The Scrap Clans: "Rust and Fury" ══════════════════════════════════════════════════════

def clans_1():
    m = Mission("clans-1", 48, 32, seed=21)
    header(m, "clans", 1, "Scrap Rights", [
        "Warboss, the Hook Clan has staked these wrecks since the burning years. Now the Directorate's "
        "surveyors are back, painting numbers on our scrap. Time to show them whose ground this is.",
        "Put up a Generator, then a Smelter: it comes with a Scrap Hauler that will strip the ore field "
        "for you. A War Camp turns out Raiders for next to nothing.",
        "Pile up 3000 credits and the other clans will follow your banner."])
    tech(m, "clans", ["power", "refinery", "barracks", "infantry"])
    m.objective("build cl_smelter", "Build a Smelter")
    m.objective("build cl_camp", "Build a War Camp")
    m.objective("credits 3000", "Hoard 3000 credits")
    m.blob(19, 20, 4, 3, "*")
    m.blob(36, 10, 3, 2, "*")
    m.blob(30, 26, 8, 3, "#")
    m.scatter("x", 18, (2, 2, 46, 30))
    m.rect(3, 4, 13, 4, "=")
    m.unit("cl_hall", 0, 4, 5)
    m.squad(["cl_raider"] * 6, 0, 4, 10, EAST)
    m.credits[0] = 2500
    m.camera = (9, 9)
    m.trigger("start", "time 3")
    m.message("start", "Select the Clan Hall and build a Generator from the sidebar.")
    m.trigger("powered", "built cl_generator")
    m.message("powered", "Good. Now a Smelter, close to the ore.")
    m.trigger("smelting", "built cl_smelter")
    m.message("smelting", "The hauler knows its trade. Build a War Camp for more raiders.")
    m.trigger("surveyors", "time 140")
    m.message("surveyors", "Directorate surveyors! Run them off.")
    m.spawn("surveyors", ["scout", "rifleman", "rifleman"], 1, 44, 8, WEST)
    m.trigger("patrol", "time 240")
    m.spawn("patrol", ["rifleman"] * 4 + ["rocket"], 1, 44, 24, WEST)
    m.write()


def clans_2():
    m = Mission("clans-2", 48, 32, seed=22)
    header(m, "clans", 2, "Smash and Grab", [
        "The Directorate keeps an ore depot at Rook's Crossing, a Refinery behind two turrets and a few "
        "bored riflemen. Burn it and the whole valley sees the Mandate bleed.",
        "No base this time, just your war band: raiders, torchers and gun trucks. Torchers melt "
        "infantry and walls up close. Your own war truck carries the Warboss; if it is lost, the band "
        "scatters.",
        "Scouts say more of our people wait in the gully halfway along."])
    m.key("tech")
    m.objective("destroy depot", "Destroy the Directorate ore depot")
    m.objective("protect warboss", "Keep the Warboss's gun truck alive")
    m.blob(38, 12, 4, 2, "*")
    m.blob(22, 24, 6, 3, "#")
    m.blob(10, 8, 6, 3, ",")
    m.scatter("x", 14, (2, 2, 46, 30))
    m.scatter("R", 8, (16, 2, 32, 12))
    m.squad(["cl_raider"] * 6 + ["cl_torcher"] * 3, 0, 3, 18, EAST, per_row=3)
    m.squad(["cl_truck", "cl_truck", "cl_truck", "cl_crawler"], 0, 3, 22, EAST)
    m.unit("cl_truck", 0, 5, 24, EAST, tag="warboss")
    m.credits[0] = 0
    m.camera = (8, 20)
    m.rect(34, 13, 13, 9, "=")
    m.unit("refinery", 1, 38, 15, tag="depot")
    m.unit("power", 1, 42, 15)
    m.unit("turret", 1, 36, 15)
    m.unit("turret", 1, 37, 19)
    m.squad(["rifleman"] * 4 + ["rocket"] * 2, 1, 39, 18, WEST, per_row=3)
    m.credits[1] = 0
    m.trigger("gully", "area 18 20 8 8")
    m.message("gully", "The gully crew joins your band!")
    m.spawn("gully", ["cl_raider"] * 4 + ["cl_torcher"] * 2, 0, 18, 22, EAST, per_row=3)
    m.trigger("alarm", "area 30 10 6 14")
    m.message("alarm", "They have seen us. Hit the refinery!")
    m.trigger("guard", "destroyed depot")
    m.message("guard", "The depot is burning! Now get the Warboss out.")
    m.write()


def clans_3():
    m = Mission("clans-3", 48, 32, seed=23)
    header(m, "clans", 3, "Junkyard Siege", [
        "The Mandate wants revenge for Rook's Crossing. Their columns are rolling on the Grand "
        "Junkyard, the clans' oldest meeting ground.",
        "Six minutes, Warboss. Hold the Junkyard for six minutes while the clans gather, and the "
        "Directorate will learn that scrap bites back. The Scrap Yard can build Trikes, Gun Trucks "
        "and Scrap Crawlers; Spike Towers chew up infantry.",
        "Their tanks come from the east, their infantry from the south."])
    tech(m, "clans", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                      "light", "support", "tank", "harvester"])
    m.objective("protect hall", "Keep the Clan Hall standing")
    m.objective("survive 360", "Hold the Junkyard for six minutes")
    m.blob(12, 24, 4, 2, "*")
    m.blob(30, 24, 3, 2, "*")
    m.scatter("x", 30, (2, 2, 46, 30))
    m.base("clans", 0, 4, 5, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2",
                                    "turret3"), tags={"hq": "hall"})
    m.squad(["cl_truck", "cl_truck", "cl_trike", "cl_trike"] + ["cl_raider"] * 6 + ["cl_torcher"] * 2,
            0, 20, 9, EAST, per_row=4)
    m.credits[0] = 4500
    m.camera = (14, 12)
    waves = (("w1", 45, ["rifleman"] * 5, 24, 30),
             ("w2", 100, ["scout", "scout", "tank"], 46, 10),
             ("w3", 160, ["rifleman"] * 4 + ["rocket"] * 3, 14, 30),
             ("w4", 220, ["tank", "tank", "apc"] + ["rifleman"] * 3, 46, 14),
             ("w5", 290, ["tank", "tank", "artillery"] + ["rocket"] * 3, 46, 8),
             ("w6", 330, ["rifleman"] * 6 + ["scout", "scout"], 22, 30))
    for t, when, names, x, y in waves:
        m.trigger(t, "time %d" % when)
        m.spawn(t, names, 1, x if x < 40 else 42, y if y < 28 else 28, WEST if x >= 40 else NORTH)
    m.message("w1", "Directorate riflemen from the south!")
    m.message("w2", "Their armour comes from the east.")
    m.message("w5", "Artillery! Get the trucks onto it before it shells the Hall.")
    m.message("w6", "One more push, Warboss!")
    m.write()


def clans_4():
    m = Mission("clans-4", 64, 44, seed=24)
    header(m, "clans", 4, "Snuff the Light", [
        "While we fought the Mandate the Lumen Concord crept onto the Fallow Mesa, and their light "
        "eats the ore the clans live on. Shamans say it burns the dreams out of a man too.",
        "Their Fusion Core powers the light. Destroy it, and the beam tower that guards its approach, "
        "and the Concord will wither on their rock. Rocket Rigs are ready in the Scrap Yard: they "
        "outrange the towers.",
        "The mesa's only ramp is on its west face."])
    tech(m, "clans", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                      "light", "support", "tank", "artillery", "harvester"])
    m.objective("destroy core", "Destroy the Concord Fusion Core")
    m.objective("destroy gate", "Destroy the beam tower at the ramp")
    m.plateau(36, 8, 25, 26, 2, ground="^", ramps=(("W", 11, 3),))
    m.blob(12, 10, 6, 3, "#")
    m.blob(18, 24, 3, 2, "*")
    m.blob(26, 36, 4, 2, "*")
    m.blob(52, 39, 3, 2, "*")
    m.scatter("x", 24, (2, 2, 34, 42))
    m.scatter("R", 6, (40, 10, 58, 14), onto="^")
    m.base("clans", 0, 3, 28)
    m.unit("cl_tower", 0, 21, 24)
    m.unit("cl_tower", 0, 17, 21)
    m.squad(["cl_crawler", "cl_crawler", "cl_rig", "cl_truck", "cl_truck"] + ["cl_raider"] * 6 + ["cl_torcher"] * 2,
            0, 18, 30, NORTH, per_row=5)
    m.credits[0] = 6000
    m.camera = (12, 30)
    m.base("concord", 1, 44, 12, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"),
           tags={"power": "core"}, pad=False)
    m.blob(52, 27, 3, 2, "*")
    m.unit("lc_tower", 1, 39, 18, tag="gate")
    m.unit("lc_tower", 1, 39, 24)
    m.squad(["lc_glaive", "lc_glaive", "lc_lancer", "lc_lancer", "lc_warden", "lc_warden"], 1, 42, 26, WEST)
    m.credits[1] = 2000
    m.trigger("gate", "destroyed gate")
    m.message("gate", "The gate tower is down! Up the ramp, all of you!")
    m.trigger("core", "destroyed core")
    m.message("core", "The light is out. Hear that? The ore sings again.")
    m.trigger("hunters", "time 180 200 2")
    m.message("hunters", "Concord skimmers are hunting our haulers!")
    m.spawn("hunters", ["lc_skimmer", "lc_skimmer"], 1, 30, 2, SOUTH)
    m.write()


def clans_5():
    m = Mission("clans-5", 64, 44, seed=25)
    header(m, "clans", 5, "Scrapfall", [
        "Every clan on the Rustfront rides behind your banner now, Warboss. One thing stands between "
        "us and the whole of the old foundry belt: the Directorate's Iron Hill, their fortress on the "
        "high ground across the canyon.",
        "Throw everything at it. Raze every structure, wreck every tank, and the Mandate's flag comes "
        "down forever. The hill has ramps on its south and west faces.",
        "Scrapfall, Warboss. Make it loud."])
    m.objective("destroy_all", "Destroy every Directorate unit and structure")
    m.canyon(22, (10, 34), wiggle=2.5, phase=0.2)
    m.plateau(34, 2, 28, 22, 2, ground="^", ramps=(("S", 12, 3), ("W", 14, 3)))
    m.blob(10, 38, 6, 3, "#")
    m.blob(10, 24, 3, 2, "*")
    m.blob(40, 32, 3, 2, "*")
    m.blob(56, 34, 3, 2, "*")
    m.scatter("x", 24, (2, 2, 62, 42))
    m.base("clans", 0, 3, 31, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["cl_crawler", "cl_crawler", "cl_crawler", "cl_rig", "cl_rig", "cl_truck", "cl_truck", "cl_truck"] +
            ["cl_raider"] * 8 + ["cl_torcher"] * 4, 0, 4, 20, EAST, per_row=6)
    m.credits[0] = 8000
    m.camera = (10, 30)
    m.base("directorate", 1, 40, 5, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2",
                                           "turret3"), pad=True)
    m.blob(57, 19, 2, 1, "*")
    m.unit("turret", 1, 47, 20)
    m.unit("turret", 1, 37, 16)
    m.squad(["tank", "tank", "tank", "artillery", "apc"] + ["rifleman"] * 4 + ["rocket"] * 3, 1, 40, 16, SOUTH)
    m.credits[1] = 6000
    m.trigger("start", "time 4")
    m.message("start", "The Iron Hill is north-east, over the canyon. Cross at either ford.")
    m.trigger("hill", "area 44 19 8 6")
    m.message("hill", "We are on the hill! Burn it all!")
    m.trigger("raid", "time 200 200 2")
    m.message("raid", "Directorate tanks are raiding our haulers!")
    m.spawn("raid", ["tank", "tank", "scout"], 1, 4, 2, SOUTH)
    m.write()


# ═══ The Lumen Concord: "The Lumen Path" ═══════════════════════════════════════════════════

def concord_1():
    m = Mission("concord-1", 48, 32, seed=31)
    header(m, "concord", 1, "First Light", [
        "Lightkeeper, the Concord has woken a Spire on the edge of the Rustfront. The ore here is "
        "young and bright; the light will teach it to flow.",
        "Raise a Fusion Core to feed the Spire, then an Extractor: it comes with a Hover Harvester. "
        "An Academy trains Wardens, who keep the peace with their beams.",
        "Gather 3000 credits of ore-light. Scavengers roam the wrecks to the east."])
    tech(m, "concord", ["power", "refinery", "barracks", "infantry"])
    m.objective("build lc_extractor", "Raise an Extractor")
    m.objective("build lc_academy", "Raise an Academy")
    m.objective("credits 3000", "Gather 3000 credits")
    m.blob(18, 18, 4, 3, "*")
    m.blob(8, 26, 7, 3, ",")
    m.blob(32, 8, 6, 3, ",")
    m.blob(40, 22, 4, 3, "^")
    m.scatter("x", 12, (26, 2, 46, 30))
    m.rect(3, 4, 13, 4, "=")
    m.unit("lc_spire", 0, 4, 5)
    m.squad(["lc_warden"] * 3, 0, 4, 10, EAST)
    m.credits[0] = 2800
    m.camera = (9, 9)
    m.trigger("start", "time 3")
    m.message("start", "Select the Spire and raise a Fusion Core from the sidebar.")
    m.trigger("powered", "built lc_fusion")
    m.message("powered", "The Spire glows. Now an Extractor beside the ore.")
    m.trigger("flowing", "built lc_extractor")
    m.message("flowing", "The ore flows. Raise an Academy to train Wardens.")
    m.trigger("scavengers", "time 160")
    m.message("scavengers", "Scavengers from the east. Wardens, to the Extractor.")
    m.spawn("scavengers", ["cl_raider"] * 4, 1, 44, 16, WEST)
    m.trigger("more", "time 270")
    m.spawn("more", ["cl_raider"] * 3 + ["cl_trike"], 1, 44, 10, WEST)
    m.write()


def concord_2():
    m = Mission("concord-2", 48, 32, seed=32)
    header(m, "concord", 2, "Shard Recovery", [
        "A shard of the First Lens has surfaced in the Saltpan, ringed by standing stones. The Clans "
        "are already there, prying at it with crowbars.",
        "Take a Glaive and its escort, clear the ring, and hold it for ninety heartbeats while the "
        "Keepers lift the shard. Reinforcements will join you when you reach the ring.",
        "The Clans will come back for it from the north and the east."])
    m.key("tech")
    m.objective("hold 28 13 6 6 90", "Hold the stone ring for 90 seconds")
    m.blob(31, 16, 5, 4, "^")
    for x, y in ((27, 12), (34, 12), (27, 19), (34, 19), (30, 11), (31, 20)):
        m.g[y][x] = "R"
    m.blob(31, 16, 1, 1, "*")
    m.blob(10, 24, 6, 3, ",")
    m.scatter("x", 12, (2, 2, 46, 30))
    m.squad(["lc_glaive", "lc_skimmer", "lc_skimmer"], 0, 3, 22, EAST)
    m.squad(["lc_warden"] * 4 + ["lc_lancer"] * 2, 0, 3, 24, EAST, per_row=3)
    m.credits[0] = 0
    m.camera = (10, 22)
    m.squad(["cl_raider"] * 4 + ["cl_torcher"], 1, 28, 14, WEST, per_row=3, step=2)
    m.unit("cl_tower", 1, 36, 17)
    m.credits[1] = 0
    m.trigger("ring", "area 24 10 14 12")
    m.message("ring", "Keepers have joined you. Clear the ring and stand fast.")
    m.spawn("ring", ["lc_glaive", "lc_lancer", "lc_lancer"], 0, 14, 26, EAST)
    m.trigger("n1", "time 70 60 3")
    m.spawn("n1", ["cl_raider"] * 4 + ["cl_torcher"], 1, 30, 1, SOUTH)
    m.trigger("e1", "time 110 70 2")
    m.message("e1", "Clan trucks from the east!")
    m.spawn("e1", ["cl_truck", "cl_trike", "cl_trike"], 1, 44, 16, WEST)
    m.write()


def concord_3():
    m = Mission("concord-3", 48, 32, seed=33)
    header(m, "concord", 3, "Vigil", [
        "The shard rests in the Beacon, a Beam Tower on the Ashfield, and it is singing the ore awake "
        "across the whole valley. The Directorate have heard it too.",
        "Keep the Beacon lit for six minutes, until the Lens is tuned. Your Foundry can make Skimmers "
        "and Glaives; Beam Towers outrange anything the Mandate fields but artillery.",
        "Their columns come from the north and the east, and they will come for the Beacon."])
    tech(m, "concord", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                        "light", "tank", "harvester"])
    m.objective("protect beacon", "Keep the Beacon lit")
    m.objective("survive 360", "Hold the vigil for six minutes")
    m.blob(12, 26, 4, 2, "*")
    m.blob(34, 26, 3, 2, "*")
    m.blob(36, 6, 6, 3, ",")
    m.scatter("x", 14, (2, 2, 46, 30))
    m.base("concord", 0, 3, 14, parts=("hq", "power", "refinery", "barracks", "factory", "turret"))
    m.rect(24, 10, 7, 6, "=")
    m.unit("lc_tower", 0, 26, 12, tag="beacon")
    m.unit("lc_tower", 0, 29, 11)
    m.unit("lc_tower", 0, 29, 14)
    m.squad(["lc_glaive", "lc_glaive", "lc_skimmer"] + ["lc_lancer"] * 3 + ["lc_warden"] * 3, 0, 20, 17, NORTH,
            per_row=3)
    m.credits[0] = 5000
    m.camera = (22, 14)
    waves = (("w1", 50, ["rifleman"] * 5 + ["rocket"], 26, 1),
             ("w2", 110, ["scout", "scout", "tank", "tank"], 44, 12),
             ("w3", 170, ["rifleman"] * 4 + ["rocket"] * 3 + ["apc"], 20, 1),
             ("w4", 235, ["tank", "tank", "artillery", "artillery"], 44, 8),
             ("w5", 300, ["tank", "tank", "tank", "apc"] + ["rocket"] * 3, 30, 1))
    for t, when, names, x, y in waves:
        m.trigger(t, "time %d" % when)
        m.spawn(t, names, 1, x, y, SOUTH if y < 5 else WEST)
    m.message("w1", "Mandate infantry from the north.")
    m.message("w4", "Artillery from the east: send the Skimmers.")
    m.message("w5", "Their last column. Hold, Lightkeeper.")
    m.write()


def concord_4():
    m = Mission("concord-4", 64, 44, seed=34)
    header(m, "concord", 4, "Breaking the Mandate", [
        "The Directorate cannot hear the Lens; they hear only engines. Their War Factory at Cinder Bluff "
        "builds the columns that hunted us at the Beacon, and their Command Center steers them.",
        "Destroy both, Lightkeeper. The Prism is ready in the Foundry: its beam splashes and reaches "
        "further than any turret. The bluff rises in a cliff, with ramps on its west and south faces.",
        "Strike before they rebuild."])
    tech(m, "concord", ["power", "refinery", "barracks", "factory", "turret", "infantry", "heavy_infantry",
                        "light", "tank", "artillery", "harvester"])
    m.objective("destroy factory", "Destroy the War Factory")
    m.objective("destroy command", "Destroy the Command Center")
    m.plateau(32, 4, 29, 22, 2, ground="^", ramps=(("W", 12, 3), ("S", 8, 3)))
    m.blob(12, 12, 6, 4, ",")
    m.blob(18, 34, 3, 2, "*")
    m.blob(26, 18, 3, 2, "*")
    m.blob(48, 34, 4, 2, "*")
    m.scatter("x", 18, (2, 2, 30, 42))
    m.scatter("R", 6, (36, 6, 58, 9), onto="^")
    m.base("concord", 0, 3, 30)
    m.squad(["lc_glaive"] * 3 + ["lc_prism", "lc_skimmer", "lc_skimmer"] + ["lc_lancer"] * 3 + ["lc_warden"] * 3,
            0, 18, 26, NORTH, per_row=6)
    m.credits[0] = 6000
    m.camera = (12, 32)
    m.base("directorate", 1, 42, 8, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2",
                                           "turret3"), tags={"hq": "command", "factory": "factory"})
    m.blob(57, 22, 2, 1, "*")
    m.unit("turret", 1, 35, 17)
    m.unit("turret", 1, 42, 22)
    m.squad(["tank", "tank", "apc", "artillery"] + ["rifleman"] * 4 + ["rocket"] * 2, 1, 42, 19, WEST, per_row=5)
    m.credits[1] = 4000
    m.trigger("factory", "destroyed factory")
    m.message("factory", "The War Factory is silent.")
    m.trigger("command", "destroyed command")
    m.message("command", "Their Command Center falls. The Mandate is blind.")
    m.trigger("column", "time 220 220 2")
    m.message("column", "A Mandate column is rolling on our Extractor!")
    m.spawn("column", ["tank", "tank", "rocket", "rocket"], 1, 26, 42, NORTH)
    m.write()


def concord_5():
    m = Mission("concord-5", 64, 44, seed=35)
    header(m, "concord", 5, "Clear Sky", [
        "The Lens is whole. When it opens, its light will reach every ore field on the Rustfront, and "
        "nothing that burns ore for war will ever stand here again.",
        "But the Clans' great camp lies to the north-east and the Directorate's last fortress to the "
        "south-east, and they will not let it open. Clear the sky, Lightkeeper: destroy every unit "
        "and every structure of both.",
        "When the last engine is still, the Lens will sing."])
    m.objective("destroy_all", "Destroy every Clan and Directorate unit and structure")
    m.objective("protect lens", "Keep the Lens tower lit")
    m.canyon(30, (9, 31), wiggle=2.0, phase=2.0)
    m.blob(8, 22, 6, 4, ",")
    m.blob(16, 10, 3, 2, "*")
    m.blob(16, 34, 3, 2, "*")
    m.blob(48, 22, 4, 2, "*")
    m.scatter("x", 18, (2, 2, 62, 42))
    m.base("concord", 0, 3, 16, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.unit("lc_tower", 0, 8, 27, tag="lens")
    m.squad(["lc_glaive"] * 4 + ["lc_prism"] * 2 + ["lc_skimmer"] * 2 + ["lc_lancer"] * 4 + ["lc_warden"] * 2,
            0, 17, 13, EAST, per_row=3)
    m.credits[0] = 8000
    m.camera = (10, 20)
    m.base("clans", 1, 47, 3, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["cl_crawler", "cl_truck", "cl_truck"] + ["cl_raider"] * 6 + ["cl_torcher"] * 2, 1, 46, 13, WEST,
            per_row=4)
    m.credits[1] = 3500
    m.base("directorate", 2, 47, 31, parts=("hq", "power", "refinery", "barracks", "factory", "turret", "turret2"))
    m.squad(["tank", "tank", "apc", "artillery"] + ["rifleman"] * 3 + ["rocket"] * 2, 2, 40, 28, WEST, per_row=5)
    m.credits[2] = 3500
    m.trigger("start", "time 4")
    m.message("start", "The Clans are north-east, the Directorate south-east. Guard the Lens tower.")
    m.trigger("raid", "time 260 240 2")
    m.message("raid", "Raiders are going for the Lens!")
    m.spawn("raid", ["cl_raider"] * 5 + ["cl_trike", "cl_trike"], 1, 2, 40, NORTH)
    m.write()


MISSIONS = [directorate_1, directorate_2, directorate_3, directorate_4, directorate_5,
            clans_1, clans_2, clans_3, clans_4, clans_5,
            concord_1, concord_2, concord_3, concord_4, concord_5]


def main():
    failed = 0
    for make in MISSIONS:
        try:
            make()
        except AssertionError as e:
            print("error:", e)
            failed += 1
    if failed:
        raise SystemExit("%d missions failed their checks" % failed)
    print("wrote %d missions into %s" % (len(MISSIONS), HERE))


if __name__ == "__main__":
    main()
