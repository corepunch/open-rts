#!/usr/bin/env python3
"""Draws Rustfront's effect animations procedurally: games/rustfront/sprites/fx_<name>.png.

    python3 games/rustfront/art/effects.py

Each effect is a strip of frames: a fireball of a few overlapping blobs that swells, cools
from white through yellow and orange to red, and leaves smoke that rises and thins. Smoke
pixels are written half transparent; art/pack8.py turns them into the shadow index, which the
game blends over what is behind. The frames are deterministic (seeded), so a rebuild
reproduces them. effects.json lists size, count and pivot for pack8.

    shell    a tank or artillery round in flight: a bright tracer, 16 facings
    rocket   a rocket with its motor burning, 16 facings
    smoke    a puff a rocket leaves behind: 12 px, 5 frames
    plume    a damaged unit's smoke, rising and spreading: 32 x 52 px, 8 frames
    fire     a burning structure's flames, flickering then dying: 16 x 24 px, 6 frames
    bolt     a Concord beam bolt in flight: a cyan streak with a white core, 16 facings
    flame    a Torcher's gout of flame in flight, 16 facings
    zap      a bolt landing: a cyan flash and sparks, 24 px, 6 frames
    hit      a bullet landing: 24 px, 5 frames
    blast    a shell or rocket landing: 40 px, 7 frames
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
EFFECTS = [("hit", 24, 5, 3, 11), ("blast", 40, 7, 4, 17), ("boom", 64, 10, 6, 23), ("bigboom", 96, 12, 9, 37)]

# Projectiles: one picture per facing, 16 facings counter-clockwise from east, as the units'.
# The view squashes depth by 2/3 (layout.TILE_H / TILE_W), so a shot flying north looks shorter.
FACINGS = 16
SQUASH = 2 / 3

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


def line(px, size, x0, y0, x1, y1, colour, width=1):
    steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(steps + 1):
        x, y = x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps
        for dy in range(width):
            for dx in range(width):
                xi, yi = int(round(x)) + dx - width // 2, int(round(y)) + dy - width // 2
                if 0 <= xi < size and 0 <= yi < size:
                    px[xi, yi] = colour


def projectile(kind, size):
    """16 facings of a shell (a tracer, hot at the nose) or a rocket (body, nose, flame)."""
    out = []
    c = size / 2 - 0.5
    for f in range(FACINGS):
        a = 2 * math.pi * f / FACINGS
        ux, uy = math.cos(a), -math.sin(a) * SQUASH
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        px = img.load()
        at = lambda d: (c + ux * d, c + uy * d)
        if kind == "bolt":
            line(px, size, *at(-11), *at(-5), (40, 150, 200, 255), 2)
            line(px, size, *at(-6), *at(5), (90, 230, 255, 255), 3)
            line(px, size, *at(-4), *at(6), (235, 255, 255, 255), 1)
        elif kind == "flame":
            for d, r, col in ((-7, 2.0, (170, 52, 16)), (-4, 3.0, (236, 110, 24)), (-1, 3.6, (255, 176, 48)),
                            (2, 3.2, (255, 230, 120)), (4, 2.2, (255, 255, 235))):
                cx, cy = at(d)
                for yy in range(size):
                    for xx in range(size):
                        if math.hypot(xx - cx, (yy - cy) / SQUASH ** 0.5) <= r:
                            px[xx, yy] = col + (255,)
        elif kind == "shell":
            line(px, size, *at(-6), *at(-2), (236, 110, 24, 255))
            line(px, size, *at(-2), *at(2), (255, 230, 120, 255))
            line(px, size, *at(2), *at(4), (255, 255, 235, 255), 2)
        else:
            line(px, size, *at(-9), *at(-6), (236, 110, 24, 255))          # motor flame
            line(px, size, *at(-6), *at(-4), (255, 230, 120, 255), 2)
            line(px, size, *at(-4), *at(3), (54, 58, 60, 255), 2)           # body
            line(px, size, *at(-1), *at(1), (150, 154, 152, 255), 2)        # band
            line(px, size, *at(3), *at(5), (200, 60, 40, 255), 2)           # warhead
        out.append(img)
    return out


def zap(size, frames):
    """A bolt landing: a white-cyan flash that shrinks to a ring, sparks flying out."""
    out = []
    rnd = random.Random(91)
    sparks = [(rnd.uniform(0, 2 * math.pi), rnd.uniform(0.5, 1.0)) for _ in range(10)]
    for f in range(frames):
        t = f / (frames - 1)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        px = img.load()
        c = size / 2 - 0.5
        r = size * (0.18 + 0.28 * t)
        for y in range(size):
            for x in range(size):
                d = math.hypot(x - c, (y - c) * 1.3)
                if t < 0.4 and d < r * (1 - t):
                    px[x, y] = (235, 255, 255, 255) if d < r * 0.5 else (90, 230, 255, 255)
                elif abs(d - r) < 1.2 and hash01(x, y, f) > t * 0.9:
                    px[x, y] = (40, 150, 200, 255) if t > 0.5 else (90, 230, 255, 255)
        for a, s in sparks:
            d = (0.1 + 0.4 * t * s) * size
            sx, sy = int(c + math.cos(a) * d), int(c + math.sin(a) * d * 0.7)
            if 0 <= sx < size and 0 <= sy < size and t < 0.8:
                px[sx, sy] = (235, 255, 255, 255)
        out.append(img)
    return out


def smoke_puff(size, frames):
    """A rocket's smoke: a grey ball that grows, lightens and breaks up (dithered away)."""
    out = []
    for f in range(frames):
        t = f / (frames - 1)
        img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        px = img.load()
        r = size * (0.22 + 0.24 * t)
        grey = int(110 + 70 * t)
        for y in range(size):
            for x in range(size):
                d = math.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5 + t * 2)
                if d < r and hash01(x, y, f) > t * 0.85 - 0.1 + 0.3 * d / r:
                    px[x, y] = (grey, grey - 4, grey - 10, 255)
        out.append(img)
    return out


def plume(w, h, f, frames):
    """Dark smoke rising from the pivot (bottom centre): a column of puffs that grows, drifts
    right with the wind, greys and thins."""
    t = f / (frames - 1)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for p in range(4):
        age = t + p * 0.25
        if age > 1.15:
            continue
        cx = w / 2 + age * 5 + math.sin(p * 2.1) * 1.5
        cy = h - 5 - age * (h - 12)
        r = 4 + age * 9
        for y in range(h):
            for x in range(w):
                d = math.hypot(x - cx, (y - cy) * 1.1)
                # opaque grey, thinned by dithering toward the edge and as it ages: it reads
                # at game scale where half-alpha dots vanish
                if d < r and hash01(x, y, f * 7 + p) > 0.02 + 0.55 * (d / r) + 0.5 * max(0, age - 0.6):
                    g = 58 + int(50 * age) + int(14 * (1 - d / r))
                    px[x, y] = (g, g - 4, g - 9, 255)
    return img


def fire(w, h, f, frames):
    """Flames licking up from the pivot: tongues that flicker frame to frame, the last frames
    dying down. Opaque, in the fireball colours."""
    rnd = random.Random(77 + f)
    life = 1.0 if f < frames - 2 else (0.6 if f == frames - 2 else 0.3)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    tongues = [(rnd.uniform(w * 0.25, w * 0.75), rnd.uniform(0.55, 1.0) * (h - 6) * life, rnd.uniform(2.2, 3.8))
               for _ in range(4)]
    for y in range(h):
        for x in range(w):
            heat = 0.0
            for tx, th, tw in tongues:
                up = (h - 4 - y) / max(th, 1)
                if 0 <= up <= 1:
                    width = tw * (1 - up) ** 0.6
                    d = abs(x - tx - math.sin(up * 3 + f) * 1.2)
                    if d < width:
                        heat = max(heat, (1 - d / width) * (1 - up * 0.7))
            heat += hash01(x, y, f + 50) * 0.18 - 0.09
            colour = next((c for th, c in RAMP if heat >= th + 0.06), None)
            if colour:
                px[x, y] = colour + (255,)
    return img


def main():
    OUT.mkdir(exist_ok=True)
    meta = {}
    for kind, size in (("shell", 16), ("rocket", 24), ("bolt", 24), ("flame", 20)):
        imgs = projectile(kind, size)
        strip = Image.new("RGBA", (size * FACINGS, size), (0, 0, 0, 0))
        for i, img in enumerate(imgs):
            strip.paste(img, (i * size, 0))
        strip.save(OUT / ("fx_%s.png" % kind), optimize=True)
        meta[kind] = dict(file="fx_%s.png" % kind, frame=[size, size], count=FACINGS, columns=FACINGS,
                          pivot=[size // 2, size // 2], frames=1)
    imgs = zap(24, 6)
    strip = Image.new("RGBA", (24 * 6, 24), (0, 0, 0, 0))
    for i, img in enumerate(imgs):
        strip.paste(img, (i * 24, 0))
    strip.save(OUT / "fx_zap.png", optimize=True)
    meta["zap"] = dict(file="fx_zap.png", frame=[24, 24], count=6, columns=6, pivot=[12, 14])
    imgs = smoke_puff(12, 5)
    strip = Image.new("RGBA", (12 * 5, 12), (0, 0, 0, 0))
    for i, img in enumerate(imgs):
        strip.paste(img, (i * 12, 0))
    strip.save(OUT / "fx_smoke.png", optimize=True)
    meta["smoke"] = dict(file="fx_smoke.png", frame=[12, 12], count=5, columns=5, pivot=[6, 6])
    for name, (w, h, frames, fn) in {"plume": (32, 52, 8, plume), "fire": (16, 24, 6, fire)}.items():
        imgs = [fn(w, h, f, frames) for f in range(frames)]
        strip = Image.new("RGBA", (w * frames, h), (0, 0, 0, 0))
        for i, img in enumerate(imgs):
            strip.paste(img, (i * w, 0))
        strip.save(OUT / ("fx_%s.png" % name), optimize=True)
        meta[name] = dict(file="fx_%s.png" % name, frame=[w, h], count=frames, columns=frames, pivot=[w // 2, h - 4])
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
