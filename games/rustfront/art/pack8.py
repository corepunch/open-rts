#!/usr/bin/env python3
"""Converts Rustfront's RGBA sprite sheets (sprites.py) into the engine's 8-bit form.

    python3 games/rustfront/art/pack8.py

Reads games/rustfront/sprites/ and writes games/rustfront/assets/: indexed PCX files that all share
one 256-colour palette, a terrain tile atlas in the same palette, and sprites.inc, the C
tables (frame grids, pivots, turret mounts) the game compiles in.

Palette (every file carries it):
    0            transparent
    1            shadow: half-alpha black in the game. Shadow sheets are only 0 and 1; colour
                 sheets use it for their soft contact edge, effects for smoke.
                 A unit's shadow sheet shares its first layer's frame size and pivot,
                 because the engine draws the shadow lump at the body's top-left
    2..7         fixed emitter colours, kept exact because median cut loses small lights:
                 warning red, glow cyan, lamp, flame, white, black
    8..15        reserved (HUD), black for now
    16..79       four team ramps of 16 shades, dark to light: blue, red, green, yellow.
                 Art is drawn with the blue ramp; a team palette map moves 16..31 to its own
    80..255      176 colours, median cut over every non-team sprite pixel and the terrain

A colour pixel is a team pixel where the team mask (sprites.py) is strong; its shade is the
mask level. Pixels under half coverage become transparent: the engine has no partial alpha.
"""
import json
import math
import random
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

ART = Path(__file__).resolve().parent
SPRITES = ART.parent / "sprites"
DATA = ART.parent / "assets"     # not "data": .gitignore ignores every directory of that name

TRANSPARENT, SHADOW = 0, 1
TEAM_BASE, TEAM_SHADES = 16, 16
TEAMS = [("blue", (60, 120, 235)), ("red", (225, 50, 38)), ("green", (70, 175, 60)), ("yellow", (235, 195, 40))]
FIXED = [(255, 46, 26), (115, 242, 255), (255, 230, 158), (255, 140, 38), (255, 255, 255), (8, 8, 8)]
FIXED_BASE = 2
FREE_BASE = TEAM_BASE + TEAM_SHADES * len(TEAMS)          # 80
FREE_COUNT = 256 - FREE_BASE
TEAM_MIN_MASK = 48            # mask at or above this: the pixel is team-coloured
ALPHA_MIN = 128               # coverage at or above this: the pixel is drawn
SHADOW_MIN = 40               # shadow alpha at or above this: in shadow

TILE_W, TILE_H = 48, 32
SAND = (156, 130, 96)
ORE = [(250, 190, 60), (232, 150, 34), (176, 98, 20), (255, 232, 140)]


def team_ramp(rgb):
    """16 shades of a team colour, from near black to a light tint, as the mask reads."""
    out = []
    for i in range(TEAM_SHADES):
        t = (i + 0.5) / TEAM_SHADES
        k = 1.15 * t
        c = [min(255, round(v * min(k, 1.0) + (255 - v) * max(0.0, k - 1.0))) for v in rgb]
        out.append(tuple(c))
    return out


def periodic_noise(w, h, seed, octaves=((4, 1.0), (8, 0.5), (16, 0.25))):
    """Value noise that wraps at the tile edge, so tiles join without seams."""
    rnd = random.Random(seed)
    field = [[0.0] * w for _ in range(h)]
    for cells, amp in octaves:
        cx, cy = cells, max(1, cells * h // w)
        grid = [[rnd.random() for _ in range(cx)] for _ in range(cy)]
        for y in range(h):
            fy = y * cy / h
            y0, ty = int(fy), fy - int(fy)
            for x in range(w):
                fx = x * cx / w
                x0, tx = int(fx), fx - int(fx)
                a = grid[y0 % cy][x0 % cx]
                b = grid[y0 % cy][(x0 + 1) % cx]
                c = grid[(y0 + 1) % cy][x0 % cx]
                d = grid[(y0 + 1) % cy][(x0 + 1) % cx]
                sx, sy = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
                field[y][x] += amp * ((a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy)
    return field


def terrain_tiles():
    """RGB tiles: 0..7 sand, 8..11 ore field, 12..15 darker gravel."""
    tiles = []
    for i in range(16):
        n = periodic_noise(TILE_W, TILE_H, 100 + i)
        rnd = random.Random(900 + i)
        img = Image.new("RGB", (TILE_W, TILE_H))
        px = img.load()
        base = SAND if i < 12 else (128, 108, 82)
        for y in range(TILE_H):
            for x in range(TILE_W):
                v = 0.86 + 0.16 * (n[y][x] / 1.75)
                if rnd.random() < 0.03:
                    v *= rnd.choice((0.82, 1.1))
                px[x, y] = tuple(min(255, round(c * v)) for c in base)
        if 8 <= i < 12:          # ore: faceted crystals with a lit top and a dark foot
            for _ in range(7):
                cx, cy = rnd.randrange(4, TILE_W - 4), rnd.randrange(4, TILE_H - 4)
                r = rnd.choice((2, 3, 3, 4))
                for dy in range(-r, r + 1):
                    for dx in range(-r - 1, r + 2):
                        if abs(dx) * 0.8 + abs(dy) <= r:
                            c = ORE[0] if dy < 0 else ORE[1]
                            if dy == r or abs(dx) * 0.8 + abs(dy) > r - 0.6:
                                c = ORE[2]
                            px[(cx + dx) % TILE_W, (cy + dy) % TILE_H] = c
                px[cx % TILE_W, (cy - r + 1) % TILE_H] = ORE[3]
        tiles.append(img)
    return tiles


def split(img, meta):
    """The frames of a packed sheet, in order."""
    w, h = meta["frame"]
    c = meta["columns"]
    return [img.crop(((n % c) * w, (n // c) * h, (n % c + 1) * w, (n // c + 1) * h)) for n in range(meta["count"])]


def outline(img):
    """A soft dark contact edge: transparent pixels touching the sprite take the shadow index,
    which the game draws as half-alpha black. It lifts units off ground of their own colour."""
    solid = img.point(lambda v: 255 if v > SHADOW else 0)
    edge = ImageChops.subtract(solid.filter(ImageFilter.MaxFilter(3)), solid)
    edge = ImageChops.multiply(edge, img.point(lambda v: 255 if v == TRANSPARENT else 0))
    out = img.copy()
    out.paste(SHADOW, mask=edge)
    return out


def recanvas(a, b):
    """Pads two layers' frames (lists [name, meta, frames, pivot]) to one size and pivot."""
    (aw, ah), (bw, bh) = a[2][0].size, b[2][0].size
    (ax, ay), (bx, by) = a[3], b[3]
    left, top = max(ax, bx), max(ay, by)
    w, h = left + max(aw - ax, bw - bx), top + max(ah - ay, bh - by)
    for layer, (px, py) in ((a, (ax, ay)), (b, (bx, by))):
        padded = []
        for f in layer[2]:
            c = Image.new("L", (w, h), TRANSPARENT)
            c.paste(f, (left - px, top - py))
            padded.append(c)
        layer[2], layer[3] = padded, (left, top)


def main():
    m = json.loads((SPRITES / "sprites.json").read_text())
    order = m["_order"]
    ramps = [team_ramp(rgb) for _, rgb in TEAMS]

    # 1. Gather every non-team opaque pixel (and the terrain) to build the free colours.
    sheets = {}
    samples = []
    for key in order:
        for name, lm in m[key]["layers"].items():
            rgba = Image.open(SPRITES / lm["file"]).convert("RGBA")
            mask = Image.open(SPRITES / lm["team"]).convert("L")
            sheets[(key, name)] = (rgba, mask)
            a, t, rgb = rgba.getchannel("A").getdata(), mask.getdata(), rgba.convert("RGB").getdata()
            samples += [c for c, al, tv in zip(rgb, a, t) if al >= ALPHA_MIN and tv < TEAM_MIN_MASK]
    effects = json.loads((SPRITES / "effects.json").read_text())
    for em in effects.values():
        fx = Image.open(SPRITES / em["file"]).convert("RGBA")
        samples += [c for c, a in zip(fx.convert("RGB").getdata(), fx.getchannel("A").getdata()) if a >= 192]
    icons = Image.open(SPRITES / "icons.png").convert("RGB")
    icon_meta = json.loads((SPRITES / "icons.json").read_text())
    samples += list(icons.getdata()) * 2
    tiles = terrain_tiles()
    for t in tiles:                 # terrain covers the screen: weight it
        samples += list(t.getdata()) * 6
    side = int(math.ceil(math.sqrt(len(samples))))
    pool = Image.new("RGB", (side, side))
    pool.putdata(samples + [samples[-1]] * (side * side - len(samples)))
    free = pool.quantize(colors=FREE_COUNT, method=Image.Quantize.MEDIANCUT).getpalette()[:FREE_COUNT * 3]
    free_colors = [tuple(free[i * 3:i * 3 + 3]) for i in range(FREE_COUNT)]

    palette = [(0, 0, 0)] * 256
    palette[TRANSPARENT] = (255, 0, 255)
    palette[SHADOW] = (0, 0, 0)
    for r, ramp in enumerate(ramps):
        for i, c in enumerate(ramp):
            palette[TEAM_BASE + r * TEAM_SHADES + i] = c
    for i, c in enumerate(free_colors):
        palette[FREE_BASE + i] = c
    for i, c in enumerate(FIXED):
        palette[FIXED_BASE + i] = c
    flat = [v for c in palette for v in c]

    # Nearest-colour mapping over the fixed and free colours (never team, shadow or key).
    choices = FIXED + free_colors
    index_of = [FIXED_BASE + i for i in range(len(FIXED))] + [FREE_BASE + i for i in range(FREE_COUNT)]
    free_pal = Image.new("P", (1, 1))
    pad = 256 - len(choices)              # quantize may pick any of 256 entries: pad with copies
    free_pal.putpalette([v for c in choices + [choices[-1]] * pad for v in c])
    index_of += [index_of[-1]] * pad

    def to_free(rgb_img):
        q = rgb_img.quantize(palette=free_pal, dither=Image.Dither.NONE)
        return [index_of[i] for i in q.getdata()]

    DATA.mkdir(exist_ok=True)

    def save(indices, size, name):
        img = Image.new("P", size)
        img.putpalette(flat)
        img.putdata(indices)
        img.save(DATA / name)

    lines = ["/* GENERATED by games/rustfront/art/pack8.py from sprites/sprites.json; do not edit. */",
             "/* View: yaw %g, elevation %.4f, %.4f cm/px, tile %dx%d. */"
             % (m["_view"]["yaw"], m["_view"]["elevation"], m["_view"]["cm_per_px"], TILE_W, TILE_H), ""]
    sheet_rows, mount_rows, anim_rows = [], [], []
    for key in order:
        u = m[key]
        for aname, a in sorted(u.get("anims", {}).items(), key=lambda kv: kv[1]["start"]):
            anim_rows.append('    {"%s", "%s", %d, %d, %d},' % (key, aname, a["start"], a["frames"], a["facings"]))
        layers = []
        for name, lm in u["layers"].items():
            rgba, mask = sheets[(key, name)]
            free_idx = to_free(rgba.convert("RGB"))
            out = []
            for f, al, tv in zip(free_idx, rgba.getchannel("A").getdata(), mask.getdata()):
                if al < ALPHA_MIN:
                    out.append(TRANSPARENT)
                elif tv >= TEAM_MIN_MASK:
                    out.append(TEAM_BASE + min(TEAM_SHADES - 1, tv * TEAM_SHADES // 256))
                else:
                    out.append(f)
            img = Image.new("L", rgba.size)
            img.putdata(out)
            layers.append([name, lm, [outline(f) for f in split(img, lm)],
                           (round(lm["pivot"][0]), round(lm["pivot"][1]))])
        sm = u["shadow"]
        sh = Image.open(SPRITES / sm["file"]).convert("RGBA").getchannel("A").point(
            lambda a: SHADOW if a >= SHADOW_MIN else TRANSPARENT)
        shadow = ["shadow", sm, split(sh, sm), (round(sm["pivot"][0]), round(sm["pivot"][1]))]
        # One shadow per facing, repeated for each animation frame of the first layer, so the
        # shadow sheet has the body's lump order (the engine indexes both with one number).
        body_frames = layers[0][1].get("frames", 1)
        if body_frames > 1:
            shadow[2] = [f for f in shadow[2] for _ in range(body_frames)]
        # The engine draws the shadow lump at the body's top-left with the body's lump index:
        # give the first layer and the shadow one canvas and one pivot.
        recanvas(layers[0], shadow)
        for name, lm, frames, pivot in layers + [shadow]:
            fname = "%s_%s.pcx" % (key, name)
            cols = lm["columns"] if len(frames) == lm["count"] else lm["columns"] * body_frames
            fw, fh = frames[0].size
            sheet = Image.new("L", (cols * fw, ((len(frames) + cols - 1) // cols) * fh), TRANSPARENT)
            for n, f in enumerate(frames):
                sheet.paste(f, ((n % cols) * fw, (n // cols) * fh))
            save(list(sheet.getdata()), sheet.size, fname)
            mount = "rf_mount_%s_%s" % (key, name) if "mount_px" in lm else "NULL"
            if "mount_px" in lm:
                mount_rows.append("static const short %s[][2] = {%s};" % (
                    mount, ", ".join("{%d, %d}" % (round(dx), round(dy)) for dx, dy in lm["mount_px"])))
            nframes = body_frames if name == "shadow" else lm.get("frames", 1)
            sheet_rows.append('    {"%s", "%s", "%s", %d, %d, %d, %d, %d, %d, %d, %s},' % (
                key, name, fname, fw, fh, cols, len(frames), pivot[0], pivot[1], nframes, mount))

    # Effects (art/effects.py): opaque fire in the free colours, half-transparent smoke as shadow.
    for name, em in effects.items():
        rgba = Image.open(SPRITES / em["file"]).convert("RGBA")
        idx = [TRANSPARENT if a < 64 else SHADOW if a < 192 else f
               for f, a in zip(to_free(rgba.convert("RGB")), rgba.getchannel("A").getdata())]
        save(idx, rgba.size, "fx_%s.pcx" % name)
        sheet_rows.append('    {"fx", "%s", "fx_%s.pcx", %d, %d, %d, %d, %d, %d, %d, NULL},' % (
            name, name, em["frame"][0], em["frame"][1], em["columns"], em["count"],
            em["pivot"][0], em["pivot"][1], em["count"]))

    # Icons are flat pictures with no team mask: let them use the blue ramp as well, so the
    # sidebar can recolour them for the player's team like any sprite.
    icon_choices = choices + ramps[0]
    icon_index = index_of[:len(choices)] + [TEAM_BASE + i for i in range(TEAM_SHADES)]
    icon_pal = Image.new("P", (1, 1))
    icon_pad = 256 - len(icon_choices)
    icon_pal.putpalette([v for c in icon_choices + [icon_choices[-1]] * icon_pad for v in c])
    icon_index += [icon_index[-1]] * icon_pad
    save([icon_index[i] for i in icons.quantize(palette=icon_pal, dither=Image.Dither.NONE).getdata()],
         icons.size, "icons.pcx")

    atlas = Image.new("RGB", (TILE_W * 8, TILE_H * 2))
    for i, t in enumerate(tiles):
        atlas.paste(t, ((i % 8) * TILE_W, (i // 8) * TILE_H))
    save(to_free(atlas), atlas.size, "terrain.pcx")

    lines += ["/* The tables belong to the sprite loader; other files include this for the constants. */",
              "#ifdef RF_SPRITE_TABLES"]
    lines += mount_rows + ["", "static const rf_sheetdef_t rf_sheets[] = {"] + sheet_rows + ["};", ""]
    lines += ["/* Infantry animations: frame = start + facing * frames + frame, facings counter-clockwise from +X. */",
              "static const rf_animdef_t rf_anims[] = {"] + anim_rows + ["};", ""]
    lines.append("/* Build icons (art/icons.py): assets/icons.pcx, one %dx%d cell per name, left to right. */"
                 % tuple(icon_meta["size"]))
    lines.append("static const char *const rf_icons[] = {%s};" % ", ".join('"%s"' % k for k in icon_meta["order"]))
    lines.append("#endif")
    lines.append("#define RF_ICON_W %d" % icon_meta["size"][0])
    lines.append("#define RF_ICON_H %d" % icon_meta["size"][1])
    lines.append("#define RF_TEAM_BASE %d" % TEAM_BASE)
    lines.append("#define RF_TEAM_SHADES %d" % TEAM_SHADES)
    lines.append("#define RF_TEAM_COUNT %d" % len(TEAMS))
    lines.append("#define RF_SHADOW_INDEX %d" % SHADOW)
    lines.append("#define RF_TILE_W %d" % TILE_W)
    lines.append("#define RF_TILE_H %d" % TILE_H)
    (ART.parent / "sprites.inc").write_text("\n".join(lines) + "\n")

    preview = Image.new("P", (16 * 12, 16 * 12))
    preview.putpalette(flat)
    preview.putdata([(y // 12) * 16 + x // 12 for y in range(16 * 12) for x in range(16 * 12)])
    preview.convert("RGB").save(ART / "previews" / "palette.png")
    print("wrote %d sheets, terrain.pcx and sprites.inc into %s" % (len(sheet_rows) + 0, DATA))


if __name__ == "__main__":
    main()
