#!/usr/bin/env python3
"""Composites the packed sprites the way the game will, as a check of pivots, turret mounts,
shadows and team colour: art/previews/sprites_contact.png.

    python3 games/rustfront/art/contact.py

Each row is one unit on sand: every facing (buildings once) drawn as shadow, then each layer
with its pivot on the unit's ground point; a turret goes at hull pivot + mount_px[facing].
Turrets aim at one fixed map direction while the hulls turn, so a turret that drifts off its
ring means a wrong mount. Alternate rows use a blue and a red team.
"""
import json
from pathlib import Path

from PIL import Image, ImageChops

ART = Path(__file__).resolve().parent
SPRITES = ART.parent / "sprites"
TEAMS = [(52, 108, 224), (214, 52, 40)]
SAND = (150, 124, 88)
CELL = 200        # px between units in a row


def frame(sheet, meta, i):
    w, h = meta["frame"]
    c = meta["columns"]
    return sheet.crop(((i % c) * w, (i // c) * h, (i % c + 1) * w, (i // c + 1) * h))


def tinted(base, mask, colour):
    """base + colour * mask (sRGB, per channel), keeping the base alpha."""
    rgb = base.convert("RGB")
    add = [ImageChops.multiply(mask, Image.new("L", mask.size, c)) for c in colour]
    out = Image.merge("RGB", [ImageChops.add(ch, a) for ch, a in zip(rgb.split(), add)])
    out.putalpha(base.getchannel("A"))
    return out


def infantry_rows(u, team):
    """Idle at every facing, then each animation's frames at two facings (toward the viewer's
    lower right, 7, and away, 3), on the same ground point as the game draws them."""
    lm, sm = u["layers"]["body"], u["shadow"]
    sheet = Image.open(SPRITES / lm["file"]).convert("RGBA")
    tsheet = Image.open(SPRITES / lm["team"]).convert("L")
    shadow = Image.open(SPRITES / sm["file"]).convert("RGBA")
    cell = lm["frame"][0] + 8
    h = max(lm["frame"][1], sm["frame"][1]) + 12
    out = []
    def draw(row, j, index):
        gx, gy = 10 + j * cell + cell // 2, int(h * 0.7)
        row.alpha_composite(frame(shadow, sm, index), (round(gx - sm["pivot"][0]), round(gy - sm["pivot"][1])))
        img = tinted(frame(sheet, lm, index), frame(tsheet, lm, index), team)
        row.alpha_composite(img, (round(gx - lm["pivot"][0]), round(gy - lm["pivot"][1])))
    for name, a in u["anims"].items():
        n = a["facings"]
        picks = [(d, f) for d in range(n) for f in range(a["frames"])] if a["frames"] == 1 else \
                [(d, f) for d in (7 % n, 3 % n) for f in range(a["frames"])]
        row = Image.new("RGBA", (20 + cell * len(picks), h), SAND + (255,))
        for j, (d, f) in enumerate(picks):
            draw(row, j, a["start"] + d * a["frames"] + f)
        out.append(row)
    return out


def main():
    m = json.loads((SPRITES / "sprites.json").read_text())
    units = [k for k in m.get("_order", []) if k in m] or [k for k in m if not k.startswith("_")]
    rows = []
    for r, key in enumerate(units):
        u = m[key]
        if "anims" in u:
            rows += infantry_rows(u, TEAMS[r % 2])
            continue
        n = u["facings"]
        team = TEAMS[r % 2]
        layers = u["layers"]
        sheets = {name: (Image.open(SPRITES / lm["file"]).convert("RGBA"),
                         Image.open(SPRITES / lm["team"]).convert("L")) for name, lm in layers.items()}
        shadow = Image.open(SPRITES / u["shadow"]["file"]).convert("RGBA")
        hmax = max(max(lm["frame"][1] for lm in layers.values()), u["shadow"]["frame"][1])
        wmax = max(lm["frame"][0] for lm in layers.values())
        cell = max(CELL, wmax + 16)
        # vehicles: every facing, stepping through the animation frames as they turn;
        # buildings: every animation frame
        first = next(iter(layers.values()))
        shown = list(range(n)) if n > 1 else list(range(first.get("frames", 1)))
        row = Image.new("RGBA", (cell * len(shown) + 40, hmax + 40), SAND + (255,))
        for j, i in enumerate(shown):
            gx, gy = 20 + j * cell + cell // 2, 20 + int(hmax * 0.62)
            sm = u["shadow"]
            sf = frame(shadow, sm, i if n > 1 else 0)
            row.alpha_composite(sf, (round(gx - sm["pivot"][0]), round(gy - sm["pivot"][1])))
            for name, lm in layers.items():
                sheet, tsheet = sheets[name]
                frames = lm.get("frames", 1)
                if "mount_px" in lm:
                    # turret layer: aim at map direction 4 of 16 whatever the hull does
                    facings = lm["count"] // frames
                    fi = (4 * facings // 16) % facings * frames + j % frames
                    dx, dy = lm["mount_px"][i if n > 1 else 0]
                elif n > 1:
                    fi, dx, dy = i * frames + j % frames, 0, 0
                else:
                    fi, dx, dy = i % frames, 0, 0
                img = tinted(frame(sheet, lm, fi), frame(tsheet, lm, fi), team)
                row.alpha_composite(img, (round(gx + dx - lm["pivot"][0]), round(gy + dy - lm["pivot"][1])))
        rows.append(row)
    W = max(r.width for r in rows)
    out = Image.new("RGB", (W, sum(r.height for r in rows)), SAND)
    y = 0
    for r in rows:
        out.paste(r, (0, y), r)
        y += r.height
    path = ART / "previews" / "sprites_contact.png"
    out.save(path, optimize=True)
    print("wrote %s (%dx%d)" % (path, *out.size))


if __name__ == "__main__":
    main()
