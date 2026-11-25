#!/usr/bin/env python3
"""Draws Rustfront's effect animations procedurally: games/rustfront/sprites/fx_<name>.png.

    python3 games/rustfront/art/effects.py

Each effect is a strip of frames: a fireball of a few overlapping blobs that swells, cools
from white through yellow and orange to red, and leaves smoke that rises and thins. Smoke
pixels are written half transparent; art/pack8.py turns them into the shadow index, which the
game blends over what is behind. The frames are deterministic (seeded), so a rebuild
reproduces them. effects.json lists size, count and pivot for pack8.

    hit      a shot landing: 24 px, 5 frames
    boom     a vehicle or soldier's weapon going up: 64 px, 10 frames
    bigboom  part of a building going up: 96 px, 12 frames
"""
import json
import math
import random
from pathlib import Path

from PIL import Image

OUT = Path(__file__).resolve().parent.parent / "sprites"

# (name, size, frames, blobs, seed)
EFFECTS = [("hit", 24, 5, 3, 11), ("boom", 64, 10, 6, 23), ("bigboom", 96, 12, 9, 37)]

RAMP = [  # heat threshold, colour
    (0.80, (255, 255, 235)), (0.62, (255, 230, 120)), (0.46, (255, 176, 48)),
    (0.32, (236, 110, 24)), (0.20, (170, 52, 16)), (0.12, (92, 30, 14)),
]
SMOKE = [(60, 56, 52), (44, 41, 39), (30, 28, 27)]


def hash01(x, y, k):
    h = (x * 374761393 + y * 668265263 + k * 2147483647) & 0xffffffff
    h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0


def effect(size, frames, blobs, seed):
    rnd = random.Random(seed)
    half = size / 2
    parts = [(rnd.uniform(-0.22, 0.22) * size, rnd.uniform(-0.16, 0.2) * size,
              rnd.uniform(0.22, 0.36) * size, rnd.uniform(0, 0.25)) for _ in range(blobs)]
    parts[0] = (0.0, 0.05 * size, 0.38 * size, 0.0)
    sparks = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(0.5, 1.0)) for _ in range(blobs * 2)]
    out = []
    for f in range(frames):
        t = f / (frames - 1)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        px = img.load()
        for y in range(size):
            for x in range(size):
                fire = smoke = 0.0
                for bx, by, br, delay in parts:
                    age = (t - delay) / (1 - delay) if t >= delay else -1
                    if age < 0:
                        continue
                    grow = 1 - (1 - min(age * 2.2, 1)) ** 2          # fast swell, then holds
                    rise = age * age * 0.30 * size                    # smoke drifts up
                    dx, dy = x - half - bx, y - half - by
                    d = math.hypot(dx, dy * 1.08)
                    r = br * (0.35 + 0.65 * grow)
                    if d < r:
                        fire = max(fire, (1 - d / r) ** 0.7 * max(0.0, 1 - age * 1.55))
                    ds = math.hypot(dx, dy + rise)
                    rs = br * (0.5 + 0.7 * age)
                    if d < r or ds < rs:
                        smoke = max(smoke, (1 - min(ds / rs, 1)) * max(0.0, 1.15 - age))
                n = hash01(x, y, f) * 0.16 - 0.08
                heat = fire + n
                colour = next((c for th, c in RAMP if heat >= th), None)
                if colour:
                    px[x, y] = colour + (255,)
                elif smoke + n * 1.5 > 0.16 and t > 0.12:
                    s = smoke + n
                    px[x, y] = SMOKE[0 if s > 0.5 else 1 if s > 0.3 else 2] + (128,)
        for angle, speed in sparks:                                   # embers flung outward
            d = (0.12 + 0.42 * t * speed) * size
            sx, sy = int(half + math.cos(angle) * d), int(half + math.sin(angle) * d * 0.8 - 2)
            if 0 <= sx < size and 0 <= sy < size and t < 0.7:
                px[sx, sy] = ((255, 230, 120) if t < 0.35 else (236, 110, 24)) + (255,)
        out.append(img)
    return out


def main():
    OUT.mkdir(exist_ok=True)
    meta = {}
    for name, size, frames, blobs, seed in EFFECTS:
        imgs = effect(size, frames, blobs, seed)
        strip = Image.new("RGBA", (size * frames, size), (0, 0, 0, 0))
        for i, img in enumerate(imgs):
            strip.paste(img, (i * size, 0))
        strip.save(OUT / ("fx_%s.png" % name), optimize=True)
        meta[name] = dict(file="fx_%s.png" % name, frame=[size, size], count=frames, columns=frames,
                          pivot=[size // 2, size // 2 + size // 10])
    (OUT / "effects.json").write_text(json.dumps(meta, indent=1) + "\n")
    print("wrote %d effects into %s" % (len(EFFECTS), OUT))


if __name__ == "__main__":
    main()
