#!/usr/bin/env python3
"""Rustfront's front-end backdrop: assets/title.pcx, 640 x 480 in a palette of its own.

The picture is the game itself: a Directorate base on the badlands (art/previews/title-scene.png,
a crop of the game's own --screenshot of maps/badlands.txt), darkened and graded toward rust,
with a stencilled RUSTFRONT logo drawn here from a pixel font, so nothing depends on a system
font. The menus draw their panels and text over it. The output is the same on every run; to
refresh the scene:

    env SDL_VIDEODRIVER=dummy build/bin/rustfront --map maps/badlands.txt --screenshot shot.bmp
    python3 games/rustfront/art/title.py --scene shot.bmp
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
W, H = 640, 480

# A 5 x 7 stencil face: '#' is ink. Gaps in the strokes are the stencil's bridges.
GLYPHS = {
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "U": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "S": [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    "T": ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "O": [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    "N": ["#...#", "##..#", "#.#.#", "#.#.#", "#..##", "#...#", "#...#"],
}


def logo(text, cell):
    """The logo as an RGBA image: rusted plate letters with a dark rim and a lit top edge."""
    gw, gh, gap = 5 * cell, 7 * cell, cell
    width = len(text) * (gw + gap) - gap
    mask = Image.new("L", (width + 8, gh + 8), 0)
    d = ImageDraw.Draw(mask)
    for i, ch in enumerate(text):
        ox = 4 + i * (gw + gap)
        for y, row in enumerate(GLYPHS[ch]):
            for x, c in enumerate(row):
                if c == "#":
                    # Stencil bridges: the middle row of each letter is notched at its left edge.
                    x0, y0 = ox + x * cell, 4 + y * cell
                    d.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=255)
        # Notch: a thin horizontal cut through the letter's middle, the stencil's bridge.
        d.rectangle([ox, 4 + 3 * cell + cell // 2 - 1, ox + gw, 4 + 3 * cell + cell // 2], fill=0)
    out = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    px = out.load()
    m = mask.load()
    for y in range(mask.size[1]):
        t = y / (mask.size[1] - 1)
        # Rust: hot orange at the top, oxidised brown below.
        base = (int(214 - 90 * t), int(118 - 62 * t), int(52 - 26 * t))
        for x in range(mask.size[0]):
            if not m[x, y]:
                continue
            r, g, b = base
            # Pitting: a fixed hash per pixel, so the logo is the same on every run.
            h = (x * 73856093 ^ y * 19349663) & 255
            k = 0.86 + 0.14 * (h / 255.0)
            if y > 0 and not m[x, y - 1]:
                k = 1.35  # the lit top edge
            px[x, y] = (min(255, int(r * k)), min(255, int(g * k)), min(255, int(b * k)), 255)
    rim = mask.filter(ImageFilter.MaxFilter(5))
    shadow = Image.new("RGBA", mask.size, (12, 8, 6, 0))
    shadow.putalpha(rim)
    shadow.alpha_composite(out)
    return shadow


def main():
    scene = os.path.join(HERE, "previews", "title-scene.png")
    if len(sys.argv) == 3 and sys.argv[1] == "--scene":
        # The world part of a 1280 x 960 screenshot, clear of the sidebar and of the fog below.
        Image.open(sys.argv[2]).convert("RGB").crop((0, 0, 746, 560)).save(scene)
    shot = Image.open(scene).convert("RGB").resize((W, H), Image.LANCZOS)
    # Grade: darken, pull toward a dusty rust, and vignette so the panels read.
    px = shot.load()
    cx, cy = W / 2, H / 2
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            lum = (r * 3 + g * 6 + b) / 10
            r, g, b = lum * 0.95 + 18, lum * 0.78 + 8, lum * 0.62
            dx, dy = (x - cx) / cx, (y - cy) / cy
            v = max(0.20, 0.70 - 0.36 * (dx * dx + dy * dy))
            px[x, y] = (int(r * v), int(g * v), int(b * v))
    canvas = shot.convert("RGBA")
    mark = logo("RUSTFRONT", 6)
    canvas.alpha_composite(mark, ((W - mark.size[0]) // 2, 34))
    rgb = canvas.convert("RGB")
    # One palette for the screen: the picture's colours, the skin's, and the menus' own inks kept exact.
    inks = [(12, 18, 22), (24, 36, 45), (40, 56, 66), (220, 230, 220), (255, 196, 96), (150, 160, 150),
            (90, 40, 30), (200, 80, 50), (110, 200, 120), (16, 12, 10), (255, 255, 255), (0, 0, 0),
            (60, 70, 74), (120, 130, 128), (232, 150, 70), (28, 24, 22)]
    # The pages' buttons and panels are the interface's metal (art/ui.py, drawn through this
    # palette): 48 of the colours are the skin's, so its steel stays steel over the sepia picture.
    skin = os.path.join(GAME, "sprites", "ui.png")
    picture = 240
    skin_pal = []
    if os.path.exists(skin):
        ui = Image.open(skin).convert("RGBA")
        opaque = [c[:3] for c in ui.getdata() if c[3] >= 128]
        side = int(len(opaque) ** 0.5) + 1
        pool = Image.new("RGB", (side, side))
        pool.putdata(opaque + [opaque[-1]] * (side * side - len(opaque)))
        skin_pal = pool.quantize(colors=48, method=Image.MEDIANCUT).getpalette()[:48 * 3]
        picture = 240 - 48
    q = rgb.quantize(colors=picture, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[:picture * 3] + skin_pal
    for c in inks:
        pal.extend(c)
    pal = pal[:768]
    q.putpalette(pal)
    os.makedirs(os.path.join(GAME, "assets"), exist_ok=True)
    out = os.path.join(GAME, "assets", "title.pcx")
    q.save(out)
    print("wrote", os.path.relpath(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
