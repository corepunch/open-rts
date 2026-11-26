#!/usr/bin/env python3
"""Rustfront's audio, synthesised from nothing but this script.

Pure Python 3 (the standard library's `wave`, `math`, `random`, `zlib`; no numpy). Every
sound is built from oscillators, filtered noise, envelopes and a few effects, seeded from its
own name, so a run writes the same files every time:

    python3 games/rustfront/art/sfx.py              # everything
    python3 games/rustfront/art/sfx.py w_rifle mus_menu   # just these (names without .wav)
    python3 games/rustfront/art/sfx.py --list       # the names

Effects go to assets/sfx/ at 22050 Hz, music to assets/music/ at 16000 Hz; both 16-bit mono.
docs/AUDIO.md describes each family and how the game plays them (s_rustfront.c).
"""
import math
import os
import random
import struct
import sys
import wave
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SFX_DIR = os.path.join(ROOT, "assets", "sfx")
MUSIC_DIR = os.path.join(ROOT, "assets", "music")

SR = 22050       # effects
MSR = 16000      # music
TAU = 2.0 * math.pi


# ── building blocks ──────────────────────────────────────────────────────────
# A sound is a list of floats in -1..1 at some rate `sr`. Every helper takes the rate it
# works at, so the same instruments serve effects and music.

def rng_for(name):
    return random.Random(zlib.crc32(name.encode()))


def silence(sec, sr=SR):
    return [0.0] * max(1, int(sec * sr))


def noise(sec, rng, sr=SR):
    u = rng.uniform
    return [u(-1.0, 1.0) for _ in range(int(sec * sr))]


def env_exp(n, tau, sr=SR, attack=0.0):
    """exp(-t/tau), with an optional linear attack (seconds)."""
    a = max(1, int(attack * sr))
    k = math.exp(-1.0 / (tau * sr))
    out = []
    g = 1.0
    for i in range(n):
        out.append(g * (i / a if i < a else 1.0))
        g *= k
    return out


def env_adsr(n, a, d, s, r, sr=SR):
    """Attack, decay, sustain level, release (seconds); the release ends the n samples."""
    a, d, r = int(a * sr), int(d * sr), int(r * sr)
    hold = max(0, n - a - d - r)
    out = []
    for i in range(n):
        if i < a:
            v = i / max(1, a)
        elif i < a + d:
            v = 1.0 - (1.0 - s) * (i - a) / max(1, d)
        elif i < a + d + hold:
            v = s
        else:
            v = s * max(0.0, 1.0 - (i - a - d - hold) / max(1, r))
        out.append(v)
    return out


def mul(x, e):
    return [a * b for a, b in zip(x, e)]


def gain(x, g):
    return [a * g for a in x]


def add(dst, src, at=0, g=1.0, wrap=False):
    """Mixes src into dst from sample `at`; with wrap, what runs past the end comes in at the
    start (seamless loops). Without wrap, dst grows to fit."""
    n = len(dst)
    if wrap:
        for i, v in enumerate(src):
            dst[(at + i) % n] += v * g
        return dst
    need = at + len(src)
    if need > n:
        dst.extend([0.0] * (need - n))
    for i, v in enumerate(src):
        dst[at + i] += v * g
    return dst


def concat(*parts):
    out = []
    for p in parts:
        out.extend(p)
    return out


def osc(freq, sec, shape="sine", sr=SR, phase=0.0, width=0.5):
    """freq is a number or a function of time in seconds. Shapes: sine, saw, square, tri, pulse."""
    n = int(sec * sr)
    out = []
    ph = phase
    f = freq if callable(freq) else None
    inc = 0.0 if f else freq / sr
    sin = math.sin
    for i in range(n):
        if f:
            inc = f(i / sr) / sr
        if shape == "sine":
            v = sin(TAU * ph)
        elif shape == "saw":
            v = 2.0 * ph - 1.0
        elif shape == "square":
            v = 1.0 if ph < 0.5 else -1.0
        elif shape == "pulse":
            v = 1.0 if ph < width else -1.0
        else:  # tri
            v = 4.0 * ph - 1.0 if ph < 0.5 else 3.0 - 4.0 * ph
        out.append(v)
        ph += inc
        ph -= math.floor(ph)
    return out


def sweep(f0, f1, sec, curve="exp"):
    """A pitch or cutoff that moves from f0 to f1 over sec, exponentially or linearly."""
    if curve == "exp":
        r = math.log(f1 / f0)
        return lambda t: f0 * math.exp(r * min(1.0, t / sec))
    return lambda t: f0 + (f1 - f0) * min(1.0, t / sec)


def lowpass1(x, fc, sr=SR):
    a = 1.0 - math.exp(-TAU * fc / sr)
    y = 0.0
    out = []
    for v in x:
        y += a * (v - y)
        out.append(y)
    return out


def highpass1(x, fc, sr=SR):
    a = math.exp(-TAU * fc / sr)
    out = []
    px = py = 0.0
    for v in x:
        py = a * (py + v - px)
        px = v
        out.append(py)
    return out


def svf(x, fc, q=0.707, mode="low", sr=SR, block=16):
    """Zavalishin's trapezoidal state-variable filter: stable at any cutoff. fc is a number
    or a function of time; it is re-read every `block` samples."""
    f = fc if callable(fc) else None
    k = 1.0 / q
    ic1 = ic2 = 0.0
    out = []
    a1 = a2 = a3 = 0.0
    nyq = sr * 0.49
    for i, v0 in enumerate(x):
        if i % block == 0:
            c = f(i / sr) if f else fc
            c = 10.0 if c < 10.0 else nyq if c > nyq else c
            g = math.tan(math.pi * c / sr)
            a1 = 1.0 / (1.0 + g * (g + k))
            a2 = g * a1
            a3 = g * a2
        v3 = v0 - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        if mode == "low":
            out.append(v2)
        elif mode == "band":
            out.append(v1)
        elif mode == "high":
            out.append(v0 - k * v1 - v2)
        else:  # notch
            out.append(v0 - k * v1)
    return out


def drive(x, amount):
    t = math.tanh
    norm = 1.0 / t(amount)
    return [t(v * amount) * norm for v in x]


def crush(x, bits, hold=1):
    """Bit depth and sample-and-hold decimation: the Clans' grit."""
    steps = 2 ** (bits - 1)
    out = []
    held = 0.0
    for i, v in enumerate(x):
        if i % hold == 0:
            held = round(v * steps) / steps
        out.append(held)
    return out


def echo(x, taps, sr=SR):
    """taps: (delay seconds, gain); the dry signal plus each delayed copy."""
    tail = int(max(d for d, _ in taps) * sr) + 1
    out = list(x) + [0.0] * tail
    for d, g in taps:
        o = int(d * sr)
        for i, v in enumerate(x):
            out[i + o] += v * g
    return out


def reverb(x, sr, size=1.0, mix=0.3, damp=0.4, loop=False):
    """A small Schroeder reverb: four damped combs, two allpasses. With loop the signal runs
    through twice and the second pass is kept, so a loop's tail rings into its own start."""
    src = list(x) + list(x) if loop else list(x) + [0.0] * int(1.5 * sr * size)
    scale = sr / 44100.0 * size
    combs = [int(d * scale) for d in (1557, 1617, 1491, 1422)]
    alls = [int(d * sr / 44100.0) for d in (556, 225)]
    wet = [0.0] * len(src)
    for d in combs:
        buf = [0.0] * d
        idx = 0
        lp = 0.0
        fb = 0.84
        for i, v in enumerate(src):
            y = buf[idx]
            lp = y * (1.0 - damp) + lp * damp
            buf[idx] = v + lp * fb
            idx = idx + 1 if idx + 1 < d else 0
            wet[i] += y
    for d in alls:
        buf = [0.0] * d
        idx = 0
        out = []
        for v in wet:
            b = buf[idx]
            y = -v + b
            buf[idx] = v + b * 0.5
            idx = idx + 1 if idx + 1 < d else 0
            out.append(y)
        wet = out
    res = [a * (1.0 - mix) + b * mix * 0.25 for a, b in zip(src, wet)]
    return res[len(x):] if loop else res


def fade(x, fin=0.002, fout=0.01, sr=SR):
    n = len(x)
    a, b = int(fin * sr), int(fout * sr)
    out = list(x)
    for i in range(min(a, n)):
        out[i] *= i / a
    for i in range(min(b, n)):
        out[n - 1 - i] *= i / b
    return out


def normalize(x, peak):
    m = max(1e-9, max(abs(v) for v in x))
    return [v * peak / m for v in x]


def trim(x, floor=0.0015):
    """Drops the near-silent tail."""
    end = len(x)
    while end > 1 and abs(x[end - 1]) < floor:
        end -= 1
    return x[:end + 64] if end + 64 <= len(x) else x


def write_wav(path, x, sr):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = bytearray()
    pack = struct.Struct("<h").pack
    for v in x:
        s = int(round(v * 32767.0))
        data += pack(32767 if s > 32767 else -32768 if s < -32768 else s)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(data))


def midi(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


NOTE = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6,
        "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}


def n2m(name):
    """'C4' -> 60, 'F#2' -> 42."""
    pitch, octave = name[:-1], int(name[-1])
    return 12 * (octave + 1) + NOTE[pitch]


# ── percussion and blasts ────────────────────────────────────────────────────

def thump(f0, f1, sec, tau, sr=SR):
    """A pitched body: a sine falling from f0 to f1."""
    n = int(sec * sr)
    return mul(osc(sweep(f0, f1, sec * 0.6), sec, "sine", sr), env_exp(n, tau, sr))


def burst(rng, sec, tau, fc, mode="low", q=0.7, sr=SR, attack=0.0):
    """Shaped noise through a filter whose cutoff may move."""
    x = noise(sec, rng, sr)
    x = svf(x, fc, q, mode, sr)
    return mul(x, env_exp(len(x), tau, sr, attack))


def crackle(rng, sec, density, tau=0.004, sr=SR, fc=3000.0):
    """Random clicks: fire, debris, static."""
    n = int(sec * sr)
    out = [0.0] * n
    clicks = int(density * sec)
    for _ in range(clicks):
        at = rng.randrange(n)
        amp = rng.uniform(0.2, 1.0) * (1.0 - at / n) ** 0.5
        length = int(tau * sr * rng.uniform(0.5, 2.0))
        for i in range(min(length, n - at)):
            out[at + i] += amp * rng.uniform(-1, 1) * math.exp(-i / max(1, tau * sr))
    return svf(out, fc, 0.8, "band", sr)


def metal(rng, base, sec, tau, partials=(1.0, 2.76, 5.40, 8.93), sr=SR):
    """An inharmonic struck plate or girder."""
    n = int(sec * sr)
    out = [0.0] * n
    for i, p in enumerate(partials):
        f = base * p * rng.uniform(0.98, 1.02)
        if f > sr * 0.45:
            continue
        add(out, mul(osc(f, sec, "sine", sr, rng.random()), env_exp(n, tau / (1 + i * 0.6), sr)),
            0, 1.0 / (1 + i))
    return out


def explosion(rng, sec, size, sr=SR):
    """size 0..1: a crack, a falling body and a cloud of noise that darkens as it dies."""
    out = silence(sec, sr)
    add(out, burst(rng, 0.03, 0.006, 6000.0, "high", sr=sr), 0, 0.6)
    add(out, thump(110 - 50 * size, 30, sec, 0.12 + 0.35 * size, sr), 0, 1.0)
    cloud = burst(rng, sec, 0.15 + 0.5 * size, sweep(3500 - 1500 * size, 160, sec * 0.8), "low", 0.6, sr)
    add(out, cloud, 0, 1.3)
    add(out, crackle(rng, sec * 0.8, 30 + 60 * size, sr=sr, fc=1800), int(0.05 * sr), 0.5 * size)
    return drive(out, 1.8)


# ── weapons ──────────────────────────────────────────────────────────────────

def shot_crack(rng, body=1.0, bright=1.0):
    """One rifle round: a supersonic crack, a short thump and a slap off the ground."""
    out = silence(0.35)
    add(out, burst(rng, 0.012, 0.002, 7000.0 * bright, "high"), 0, 0.9)
    add(out, burst(rng, 0.15, 0.025, 1600.0 * bright, "band", 1.2), 0, 1.0)
    add(out, thump(180, 70, 0.12, 0.03), 0, 0.7 * body)
    add(out, burst(rng, 0.35, 0.09, 900.0, "low"), int(0.01 * SR), 0.45)
    return out


def w_rifle(rng):
    return drive(shot_crack(rng), 1.5)


def w_mg(rng):
    out = silence(0.5)
    for i in range(5):
        add(out, shot_crack(rng, body=0.8, bright=1.15), int(i * 0.072 * SR), 0.85 + 0.15 * rng.random())
    return drive(out, 2.0)


def w_cannon(rng):
    out = silence(1.0)
    add(out, burst(rng, 0.02, 0.004, 5000.0, "high"), 0, 0.8)
    add(out, thump(140, 38, 0.8, 0.16), 0, 1.4)
    add(out, burst(rng, 1.0, 0.22, sweep(2600, 200, 0.6), "low"), 0, 1.0)
    add(out, metal(rng, 310, 0.4, 0.05), 0, 0.12)
    return drive(out, 2.2)


def w_artillery(rng):
    out = silence(1.8)
    add(out, burst(rng, 0.03, 0.006, 4000.0, "high"), 0, 0.7)
    add(out, thump(90, 28, 1.4, 0.35), 0, 1.6)
    add(out, burst(rng, 1.8, 0.5, sweep(1800, 120, 1.2), "low"), 0, 1.2)
    add(out, burst(rng, 1.6, 0.6, 140.0, "low"), int(0.08 * SR), 1.0)   # the rumble rolls on
    return drive(out, 2.0)


def w_rocket(rng):
    sec = 0.9
    n = int(sec * SR)
    out = silence(sec)
    add(out, thump(260, 90, 0.1, 0.03), 0, 0.6)
    hiss = svf(noise(sec, rng), sweep(500, 2600, 0.5), 1.4, "band")
    shape = [min(1.0, i / (0.04 * SR)) * math.exp(-i / (0.32 * SR)) for i in range(n)]
    add(out, mul(hiss, shape), 0, 1.3)
    roar = svf(noise(sec, rng), 400.0, 0.7, "low")
    add(out, mul(roar, env_exp(n, 0.25)), 0, 1.0)
    return drive(out, 1.6)


def w_flame(rng):
    sec = 0.75
    n = int(sec * SR)
    roar = svf(noise(sec, rng), sweep(1800, 700, sec), 0.6, "low")
    flick = osc(lambda t: 13 + 6 * math.sin(TAU * 1.7 * t), sec)
    shape = env_adsr(n, 0.05, 0.1, 0.8, 0.3)
    out = [r * s * (0.7 + 0.3 * f) for r, s, f in zip(roar, shape, flick)]
    add(out, burst(rng, 0.25, 0.05, 2500.0, "band", 0.8), 0, 0.4)       # the igniter's pop
    add(out, crackle(rng, sec, 60, sr=SR, fc=2600.0), 0, 0.6)
    return drive(out, 1.4)


def w_beam(rng):
    """The Concord's beam: a bright FM chirp falling through the octaves, with a glassy ring."""
    sec = 0.45
    n = int(sec * SR)
    f = sweep(3200, 700, 0.3)
    mod = osc(lambda t: f(t) * 1.5, sec)
    out = []
    ph = 0.0
    env = env_exp(n, 0.12, attack=0.004)
    for i in range(n):
        t = i / SR
        ph += f(t) / SR
        out.append(math.sin(TAU * ph + 2.5 * math.exp(-t / 0.08) * mod[i]) * env[i])
    add(out, mul(osc(2100, sec, "sine"), env_exp(n, 0.2)), 0, 0.25)
    add(out, mul(svf(noise(sec, rng), 4500, 2.0, "band"), env_exp(n, 0.03)), 0, 0.3)
    return echo(out, [(0.045, 0.3), (0.09, 0.15)])


def w_beam_heavy(rng):
    """The Prism and the beam tower: a rising charge, then a thick detuned discharge."""
    charge, fire = 0.28, 0.8
    out = silence(charge + fire)
    nc = int(charge * SR)
    up = osc(sweep(300, 2400, charge), charge, "sine")
    add(out, mul(up, [i / nc for i in range(nc)]), 0, 0.5)
    nf = int(fire * SR)
    body = [0.0] * nf
    for d in (0.995, 1.0, 1.007):
        add(body, osc(sweep(880 * d, 220 * d, 0.5), fire, "saw"), 0, 0.33)
    body = svf(body, sweep(5000, 600, fire), 1.5, "low")
    add(body, mul(svf(noise(fire, rng), 2500, 1.0, "band"), env_exp(nf, 0.05)), 0, 0.6)
    add(body, mul(osc(1320, fire), env_exp(nf, 0.3)), 0, 0.3)
    add(out, mul(body, env_exp(nf, 0.22, attack=0.003)), nc, 1.0)
    return echo(drive(out, 1.5), [(0.06, 0.3), (0.13, 0.15)])


# ── impacts and explosions ───────────────────────────────────────────────────

def hit_bullet(rng):
    out = silence(0.3)
    add(out, burst(rng, 0.05, 0.008, 3500.0, "band", 1.5), 0, 1.0)
    add(out, metal(rng, rng.uniform(1100, 1500), 0.2, 0.03), 0, 0.25)
    if rng.random() < 0.6:          # a ricochet's whine
        n = int(0.22 * SR)
        add(out, mul(osc(sweep(rng.uniform(2600, 3200), 1500, 0.22), 0.22), env_exp(n, 0.07)),
            int(0.01 * SR), 0.25)
    return out


def hit_shell(rng):
    return explosion(rng, 0.7, 0.25)


def boom_small(rng):
    return explosion(rng, 0.9, 0.45)


def boom_large(rng):
    out = explosion(rng, 1.6, 0.85)
    add(out, metal(rng, 180, 0.8, 0.2), int(0.05 * SR), 0.15)   # the wreck's panels ring
    return out


def collapse(rng):
    """A structure coming down: blasts, groaning girders, falling rubble."""
    sec = 3.0
    out = explosion(rng, 1.8, 1.0)
    out.extend([0.0] * int((sec - 1.8) * SR))
    groan = svf(noise(1.6, rng), sweep(420, 160, 1.6), 9.0, "band")
    add(out, mul(groan, env_adsr(len(groan), 0.3, 0.4, 0.7, 0.8)), int(0.35 * SR), 0.9)
    rubble = crackle(rng, 2.4, 140, tau=0.008, fc=900.0)
    add(out, mul(rubble, env_adsr(len(rubble), 0.2, 0.5, 0.6, 1.4)), int(0.4 * SR), 1.4)
    add(out, explosion(rng, 1.2, 0.6), int(0.55 * SR), 0.6)
    add(out, burst(rng, 2.2, 0.9, 120.0, "low"), int(0.5 * SR), 1.2)   # dust settling
    for i in range(4):
        add(out, metal(rng, rng.uniform(140, 260), 0.6, 0.12), int((0.7 + 0.35 * i) * SR), 0.15)
    return drive(out, 1.5)


def zap(rng):
    """A bolt landing: electric crackle and a falling ping."""
    sec = 0.4
    n = int(sec * SR)
    out = mul(crackle(rng, sec, 400, tau=0.002, fc=4000.0), env_exp(n, 0.1))
    add(out, mul(osc(sweep(2400, 900, 0.2), sec), env_exp(n, 0.08)), 0, 0.5)
    add(out, burst(rng, 0.2, 0.03, 1500.0, "low"), 0, 0.5)
    return out


def zap_heavy(rng):
    sec = 0.9
    out = explosion(rng, sec, 0.35)
    add(out, gain(zap(rng), 1.0), 0)
    n = int(sec * SR)
    add(out, mul(osc(sweep(1600, 200, 0.6), sec, "tri"), env_exp(n, 0.2)), 0, 0.4)
    return echo(out, [(0.07, 0.25)])


def flame_hit(rng):
    sec = 0.6
    n = int(sec * SR)
    out = mul(svf(noise(sec, rng), 3200, 0.7, "high"), env_adsr(n, 0.02, 0.1, 0.5, 0.4))
    add(out, crackle(rng, sec, 90, fc=2200.0), 0, 0.8)
    add(out, mul(svf(noise(sec, rng), 500, 0.7, "low"), env_exp(n, 0.15)), 0, 0.8)
    return out


def die_soldier(rng):
    """A short cry and the body hitting the ground."""
    f0 = rng.uniform(150, 190)
    sec = 0.32
    cry = formant_voice(rng, [("a", 1.0)], sec, lambda t: f0 * (1.15 - 0.6 * t / sec), breath=0.25)
    out = mul(cry, env_adsr(len(cry), 0.01, 0.08, 0.6, 0.18))
    out = svf(out, 2600, 0.7, "low")
    add(out, thump(120, 50, 0.25, 0.05), int(0.36 * SR), 0.7)
    add(out, burst(rng, 0.2, 0.04, 600.0, "low"), int(0.36 * SR), 0.6)
    return out


# ── construction ─────────────────────────────────────────────────────────────

def build_place(rng):
    """A foundation dropped on the ground: a heavy clunk and ringing steel."""
    out = silence(0.9)
    add(out, thump(120, 45, 0.5, 0.09), 0, 1.2)
    add(out, burst(rng, 0.4, 0.06, 700.0, "low"), 0, 0.8)
    add(out, metal(rng, 220, 0.9, 0.25), 0, 0.5)
    add(out, metal(rng, 333, 0.6, 0.12), int(0.11 * SR), 0.25)
    return drive(out, 1.4)


def build_scaffold(rng):
    """Work on a site: hammer strikes, a ratchet, a pneumatic hiss."""
    sec = 1.6
    out = silence(sec)
    t = 0.0
    while t < sec - 0.25:
        add(out, metal(rng, rng.uniform(600, 900), 0.25, 0.04), int(t * SR), rng.uniform(0.5, 0.9))
        add(out, burst(rng, 0.05, 0.01, 2500.0, "band", 1.0), int(t * SR), 0.4)
        t += rng.choice((0.18, 0.22, 0.36))
    for i in range(6):
        add(out, burst(rng, 0.02, 0.004, 3800.0, "band", 3.0), int((0.9 + i * 0.045) * SR), 0.35)
    hiss = svf(noise(0.4, rng), 5000, 0.7, "high")
    add(out, mul(hiss, env_adsr(len(hiss), 0.02, 0.1, 0.6, 0.25)), int(1.15 * SR), 0.25)
    return out


def build_done(rng):
    """A structure locking into place: hydraulics, a clunk, a rising chime."""
    out = silence(1.3)
    hiss = svf(noise(0.5, rng), sweep(1200, 5000, 0.5), 1.0, "band")
    add(out, mul(hiss, env_adsr(len(hiss), 0.05, 0.1, 0.7, 0.3)), 0, 0.5)
    add(out, thump(150, 60, 0.4, 0.08), int(0.45 * SR), 1.0)
    add(out, metal(rng, 260, 0.6, 0.15), int(0.45 * SR), 0.4)
    for i, m in enumerate((72, 76, 79)):
        add(out, bell(midi(m), 0.5, SR, index=1.5), int((0.55 + 0.08 * i) * SR), 0.25)
    return out


# ── interface and announcements ──────────────────────────────────────────────

def bell(freq, sec, sr, index=2.0, ratio=3.5, tau=None):
    """FM bell: a sine carrier modulated at an inharmonic ratio, the index dying away."""
    n = int(sec * sr)
    tau = tau or sec * 0.35
    out = []
    pc = pm = 0.0
    ic, im = freq / sr, freq * ratio / sr
    sin, exp = math.sin, math.exp
    for i in range(n):
        e = exp(-i / (tau * sr))
        out.append(sin(TAU * pc + index * e * sin(TAU * pm)) * e)
        pc += ic
        pm += im
    return out


def tone(freq, sec, shape="sine", sr=SR, a=0.005, r=0.03):
    n = int(sec * sr)
    return mul(osc(freq, sec, shape, sr), env_adsr(n, a, 0.0, 1.0, r, sr))


def ui_click(rng):
    out = silence(0.06)
    add(out, mul(osc(2200, 0.03, "square"), env_exp(int(0.03 * SR), 0.004)), 0, 0.4)
    add(out, burst(rng, 0.02, 0.003, 5000.0, "band", 1.5), 0, 0.5)
    return svf(out, 6000, 0.7)


def data_chirp(rng, sec=0.18):
    """Digital chatter under an announcement."""
    out = []
    while len(out) < sec * SR:
        out.extend(tone(rng.choice((1400, 1800, 2100, 2600)), 0.018, "square", a=0.001, r=0.004))
    return svf(out, 3500, 0.7)


def ui_unit_ready(rng):
    out = silence(0.8)
    add(out, data_chirp(rng, 0.12), 0, 0.12)
    add(out, bell(midi(72), 0.5, SR, 1.2, 2.0), int(0.12 * SR), 0.6)
    add(out, bell(midi(79), 0.6, SR, 1.2, 2.0), int(0.28 * SR), 0.6)
    return out


def ui_structure_ready(rng):
    out = silence(1.1)
    add(out, thump(130, 60, 0.3, 0.06), 0, 0.5)
    for i, m in enumerate((67, 72, 76, 79)):
        add(out, bell(midi(m), 0.6, SR, 1.0, 2.0), int((0.1 + 0.11 * i) * SR), 0.5)
    return out


def ui_low_power(rng):
    """Power failing: a falling hum, twice, and a dying buzz."""
    out = silence(1.4)
    for k in range(2):
        n = int(0.5 * SR)
        drop = svf(osc(sweep(700, 160, 0.5), 0.5, "saw"), 1800, 0.8)
        add(out, mul(drop, env_adsr(n, 0.01, 0.1, 0.8, 0.15)), int(k * 0.55 * SR), 0.5)
    buzz = mul(osc(lambda t: 50 + 5 * math.sin(TAU * 6 * t), 0.3, "square"), env_exp(int(0.3 * SR), 0.1))
    add(out, svf(buzz, 900, 0.7), int(1.05 * SR), 0.35)
    return out


def ui_no_funds(rng):
    out = silence(0.6)
    for k in range(2):
        add(out, svf(tone(185, 0.16, "square", a=0.003, r=0.03), 1400, 0.8), int(k * 0.22 * SR), 0.45)
        add(out, svf(tone(139, 0.16, "square", a=0.003, r=0.03), 1400, 0.8), int(k * 0.22 * SR), 0.3)
    return out


def ui_under_attack(rng):
    """A klaxon: two tones alternating, urgent but short."""
    out = silence(1.5)
    for k in range(6):
        f = 660 if k % 2 == 0 else 880
        add(out, svf(tone(f, 0.2, "square", a=0.004, r=0.03), 2400, 0.9), int(k * 0.21 * SR), 0.35)
    add(out, data_chirp(rng, 0.2), int(1.27 * SR), 0.1)
    return out


# ── unit voices ──────────────────────────────────────────────────────────────
# No words: syllables of formant-filtered buzz (Directorate, Clans) or FM chimes (Concord),
# with the intonation of a reply. A selection asks (rising), a move confirms (falling), an
# attack barks (fast and accented).

VOWELS = {
    "a": (730, 1090, 2440), "e": (530, 1840, 2480), "i": (300, 2200, 2900),
    "o": (570, 840, 2410), "u": (320, 870, 2240), "@": (500, 1500, 2500),
}


def formant_voice(rng, syllables, sec, pitch, breath=0.1, sr=SR):
    """syllables: (vowel, weight) spans across sec; pitch: f0 as a function of time.
    A sawtooth glottal source through three formant band-passes that glide between vowels."""
    n = int(sec * sr)
    total = sum(w for _, w in syllables)
    marks = []
    t = 0.0
    for v, w in syllables:
        marks.append((t, VOWELS[v]))
        t += w / total * sec
    marks.append((sec, VOWELS[syllables[-1][0]]))

    def formant(k):
        def f(time):
            for (t0, v0), (t1, v1) in zip(marks, marks[1:]):
                if time <= t1:
                    u = 0.0 if t1 <= t0 else (time - t0) / (t1 - t0)
                    u = u * u * (3 - 2 * u)
                    return v0[k] + (v1[k] - v0[k]) * u
            return marks[-1][1][k]
        return f

    jitter = [1.0 + 0.02 * rng.uniform(-1, 1) for _ in range(64)]
    src = osc(lambda tt: pitch(tt) * jitter[int(tt * 40) % 64], sec, "saw", sr)
    src = [s + breath * rng.uniform(-1, 1) for s in src]
    out = [0.0] * n
    for k, (q, g) in enumerate(((5.0, 1.0), (7.0, 0.6), (9.0, 0.35))):
        add(out, svf(src, formant(k), q, "band", sr), 0, g)
    return out


def syllable_train(rng, count, sec, base, contour, accent=0.0, vowels="aeiou@"):
    """Separate syllables with gaps and consonant onsets, under one pitch contour."""
    out = []
    gap = 0.035
    each = (sec - gap * (count - 1)) / count
    for s in range(count):
        t0 = s / max(1, count - 1) if count > 1 else 0.0
        p0 = base * contour(t0)
        p1 = base * contour(min(1.0, t0 + 1.0 / count))
        v = [(rng.choice(vowels), 1.0), (rng.choice(vowels), 0.7)]
        syl = formant_voice(rng, v, each, lambda t, a=p0, b=p1: a + (b - a) * t / each)
        env = env_adsr(len(syl), 0.012, 0.04, 0.75 + accent * 0.25 * (s == 0), 0.05)
        syl = mul(syl, env)
        if rng.random() < 0.7:      # a consonant: a burst of shaped noise at the onset
            c = burst(rng, 0.03, 0.008, rng.choice((2500.0, 4000.0, 6000.0)), "band", 1.5)
            add(syl, c, 0, 0.5 + accent * 0.3)
        out.extend(syl)
        if s < count - 1:
            out.extend([0.0] * int(gap * SR))
    return out


CONTOURS = {
    "select": (2, 0.42, lambda t: 1.0 + 0.25 * t, 0.0),      # a question
    "move": (3, 0.55, lambda t: 1.15 - 0.3 * t, 0.2),       # yes, going
    "attack": (3, 0.46, lambda t: 1.25 + 0.1 * math.sin(math.pi * t), 1.0),  # barked
}


def squelch(rng, sec=0.06):
    n = int(sec * SR)
    return mul(svf(noise(sec, rng), 3000, 0.6, "band"), [1.0 - i / n for i in range(n)])


def directorate_voice(rng, kind):
    """Military radio: a clipped voice in a telephone band, static, squelch at both ends and
    a confirmation pip on orders."""
    count, sec, contour, accent = CONTOURS[kind]
    base = rng.uniform(105, 130)
    v = syllable_train(rng, count, sec, base, contour, accent, "aeo@u")
    v = highpass1(svf(v, 2800, 0.8), 380)
    v = drive(normalize(v, 1.0), 3.0)
    v = [s + 0.05 * rng.uniform(-1, 1) for s in v]
    out = concat(gain(squelch(rng, 0.05), 0.5), silence(0.02), v, silence(0.02), gain(squelch(rng, 0.09), 0.6))
    if kind != "select":
        add(out, tone(1250, 0.06, "sine", a=0.002, r=0.01), len(out) - int(0.03 * SR), 0.35)
    else:
        out = concat(tone(1500, 0.04, "sine", a=0.002, r=0.01), silence(0.03), gain(out, 1.0))
    return out


def clans_voice(rng, kind):
    """Gritty: a low growling voice, ring-modulated, overdriven and crushed to a few bits,
    like a salvaged loudhailer."""
    count, sec, contour, accent = CONTOURS[kind]
    base = rng.uniform(78, 96)
    v = syllable_train(rng, count, sec * 0.95, base, contour, accent + 0.3, "ao@u")
    growl = osc(31 + rng.uniform(-3, 3), len(v) / SR, "sine")
    v = [s * (0.65 + 0.35 * g) for s, g in zip(v, growl)]
    v = drive(normalize(v, 1.0), 6.0)
    v = crush(v, 6, 2)
    v = svf(v, 3200, 0.9)
    # Clank of the loudhailer key.
    out = concat(gain(metal(rng, 900, 0.04, 0.01), 0.4), v, silence(0.03))
    add(out, crackle(rng, len(out) / SR, 30, fc=1800.0), 0, 0.25)
    return out


CONCORD_SCALE = [74, 76, 78, 81, 83, 86, 88, 90, 93]   # D major pentatonic plus the lydian G#


def concord_voice(rng, kind):
    """Crystalline: chimes in a bright pentatonic, gliding into each other, a whispered
    formant shimmer behind them and a glassy echo."""
    count, sec, contour, accent = CONTOURS[kind]
    notes = {"select": 2, "move": 3, "attack": 4}[kind]
    step = sec / notes
    out = silence(sec + 0.4)
    idx = rng.randrange(2, 5)
    for k in range(notes):
        if kind == "select":
            idx += rng.choice((1, 2))
        elif kind == "move":
            idx -= rng.choice((1, 2))
        else:
            idx += rng.choice((-1, 2))
        idx = max(0, min(len(CONCORD_SCALE) - 1, idx))
        f = midi(CONCORD_SCALE[idx])
        dur = step * (1.6 if k == notes - 1 else 1.1)
        index = 3.0 if kind == "attack" else 1.4
        chime = bell(f, dur, SR, index, ratio=2.0 if kind != "attack" else 3.01, tau=dur * 0.5)
        glide = osc(sweep(f * 0.94, f, 0.03), dur, "sine")
        add(chime, mul(glide, env_exp(len(glide), dur * 0.3)), 0, 0.4)
        add(out, chime, int(k * step * SR), 0.6 if k < notes - 1 else 0.75)
    whisper = formant_voice(rng, [("i", 1.0), ("e", 1.0)], sec, lambda t: 220.0, breath=0.9)
    add(out, mul(svf(whisper, 3000, 1.0, "high"), env_adsr(len(whisper), 0.05, 0.1, 0.6, 0.1)), 0, 0.08)
    return echo(out, [(0.07, 0.35), (0.15, 0.2), (0.23, 0.1)])


VOICES = {"dir": directorate_voice, "cl": clans_voice, "lc": concord_voice}


# ── ambience ─────────────────────────────────────────────────────────────────

def smooth_lfo(rng, sec, rate, sr):
    """A random curve through a point every 1/rate seconds, wrapping at sec (loopable)."""
    points = max(2, int(sec * rate))
    vals = [rng.random() for _ in range(points)]
    n = int(sec * sr)
    out = []
    for i in range(n):
        x = i / n * points
        k = int(x)
        u = x - k
        u = u * u * (3 - 2 * u)
        out.append(vals[k % points] * (1 - u) + vals[(k + 1) % points] * u)
    return out


def loop_noise_filtered(rng, sec, fc, q, mode, sr):
    """Filtered noise rendered twice the length and cross-faded into a seamless loop."""
    n = int(sec * sr)
    x = svf(noise(sec * 2, rng, sr), fc, q, mode, sr)
    # Keep the settled second half; fade its last fifth into what led up to its start, so
    # its final sample runs on into its first.
    body = x[n:2 * n]
    k = n // 5
    for j in range(n - k, n):
        u = (j - (n - k)) / k
        body[j] = body[j] * (1 - u) + x[j] * u
    return body


def amb_wind(rng):
    sr = 11025
    sec = 14.0
    low = loop_noise_filtered(rng, sec, 380.0, 0.7, "low", sr)
    whistle = loop_noise_filtered(rng, sec, 1100.0, 6.0, "band", sr)
    gust = smooth_lfo(rng, sec, 0.5, sr)
    flutter = smooth_lfo(rng, sec, 3.0, sr)
    out = [l * (0.35 + 0.65 * g) + w * 0.25 * g * g * (0.5 + 0.5 * f)
           for l, w, g, f in zip(low, whistle, gust, flutter)]
    return out, sr


def amb_base(rng):
    """A base's machinery: a mains hum with slow beating and a pump's steady thud. Every
    component completes whole cycles in the loop."""
    sr = SR
    sec = 6.0
    n = int(sec * sr)
    out = [0.0] * n
    for f, g in ((50.0, 0.5), (100.0, 0.35), (150.5, 0.2), (200.0, 0.1), (301.0 / 1.0, 0.06)):
        add(out, osc(f, sec, "sine", sr), 0, g)
    rumble = loop_noise_filtered(rng, sec, 160.0, 0.7, "low", sr)
    add(out, rumble, 0, 0.8)
    for k in range(9):          # 1.5 thuds a second
        add(out, thump(70, 45, 0.3, 0.07, sr), int(k * sec / 9 * sr), 0.35, wrap=True)
        add(out, svf(burst(rng, 0.15, 0.03, 1200.0, "band", 1.0, sr), 1500, 0.7, sr=sr),
            int((k * sec / 9 + 0.33) * sr), 0.05, wrap=True)
    return out, sr


# ── music: a tiny tracker ────────────────────────────────────────────────────
# A track is a buffer of the loop's length. Each instrument renders one note; `place` puts it
# at a step and wraps what rings past the end into the start, so the loop is seamless.

def kick(rng, sr=MSR, punch=1.0):
    n = int(0.4 * sr)
    out = mul(osc(sweep(160, 42, 0.09), 0.4, "sine", sr), env_exp(n, 0.16, sr))
    add(out, burst(rng, 0.01, 0.002, 3000.0, "band", 1.0, sr), 0, 0.3 * punch)
    return drive(out, 1.2 + punch)


def snare(rng, sr=MSR, tail=0.12):
    out = burst(rng, 0.3, tail, 2600.0, "band", 0.6, sr)
    add(out, mul(osc(190, 0.1, "tri", sr), env_exp(int(0.1 * sr), 0.03, sr)), 0, 0.6)
    add(out, burst(rng, 0.15, 0.03, 5500.0, "high", sr=sr), 0, 0.4)
    return out


def hat(rng, sr=MSR, tau=0.025):
    return burst(rng, tau * 5, tau, 6500.0, "high", 0.7, sr)


def tom(rng, freq, sr=MSR):
    n = int(0.5 * sr)
    out = mul(osc(sweep(freq * 1.6, freq, 0.06), 0.5, "sine", sr), env_exp(n, 0.14, sr))
    add(out, burst(rng, 0.05, 0.01, 1200.0, "band", 1.0, sr), 0, 0.3)
    return drive(out, 1.5)


def clank(rng, sr=MSR):
    return metal(rng, rng.uniform(380, 520), 0.35, 0.06, sr=sr)


def bass(freq, dur, sr=MSR, grit=0.0, cutoff=900.0):
    n = int(dur * sr)
    x = osc(freq, dur, "saw", sr)
    add(x, osc(freq * 0.5, dur, "square", sr), 0, 0.4)
    x = svf(x, lambda t: 140 + cutoff * math.exp(-t / 0.12), 1.4, "low", sr)
    if grit:
        x = drive(x, 1 + 6 * grit)
    return mul(x, env_adsr(n, 0.005, 0.08, 0.7, 0.04, sr))


def brass(freq, dur, sr=MSR, bright=1.0):
    n = int(dur * sr)
    x = [0.0] * n
    for d in (0.997, 1.003):
        add(x, osc(lambda t, f=freq * d: f * (1 + 0.004 * math.sin(TAU * 5.5 * t) * min(1, t / 0.3)),
                   dur, "saw", sr), 0, 0.5)
    x = svf(x, lambda t: 400 + 2200 * bright * min(1.0, t / 0.07) * (0.6 + 0.4 * math.exp(-t / 0.3)), 0.9,
            "low", sr)
    return mul(x, env_adsr(n, 0.03, 0.15, 0.75, 0.08, sr))


def pad(freqs, dur, sr=MSR, cutoff=1300.0, attack=0.6):
    n = int(dur * sr)
    x = [0.0] * n
    for f in freqs:
        for d in (0.993, 1.0, 1.006):
            add(x, osc(f * d, dur, "saw", sr, phase=(f * d) % 1.0), 0, 0.12)
    x = svf(x, cutoff, 0.7, "low", sr, block=64)
    return mul(x, env_adsr(n, attack, 0.3, 0.8, min(0.8, dur * 0.3), sr))


def pluck(rng, freq, dur, sr=MSR, damp=0.5):
    """Karplus-Strong string."""
    period = max(2, int(sr / freq))
    buf = [rng.uniform(-1, 1) for _ in range(period)]
    n = int(dur * sr)
    out = []
    idx = 0
    for _ in range(n):
        a = buf[idx]
        b = buf[(idx + 1) % period]
        buf[idx] = (a * (1 - damp) + b * damp) * 0.996
        out.append(a)
        idx = (idx + 1) % period
    return mul(out, env_adsr(n, 0.002, 0.0, 1.0, 0.03, sr))


def power_chord(rng, root, dur, sr=MSR, mute=False):
    """Root, fifth and octave plucked and overdriven: the Clans' guitar."""
    n = int(dur * sr)
    x = [0.0] * n
    for k, semis in enumerate((0, 7, 12)):
        add(x, pluck(rng, midi(root + semis), dur, sr, 0.5 if not mute else 0.8), int(k * 0.004 * sr), 0.4)
    x = drive(x, 7.0)
    x = svf(x, 2400 if not mute else 1100, 0.9, "low", sr)
    return mul(x, env_exp(n, 0.08 if mute else dur * 0.8, sr))


def lead(freq, dur, sr=MSR, shape="pulse"):
    n = int(dur * sr)
    x = osc(lambda t: freq * (1 + 0.006 * math.sin(TAU * 5 * t) * min(1, t / 0.25)), dur, shape, sr, width=0.3)
    x = svf(x, 2600, 0.8, "low", sr)
    return mul(x, env_adsr(n, 0.01, 0.1, 0.7, 0.06, sr))


class Track:
    def __init__(self, bpm, bars, sr=MSR, beats=4):
        self.sr = sr
        self.step = 60.0 / bpm / 4          # a 16th
        self.steps = bars * beats * 4
        self.buf = [0.0] * int(round(self.steps * self.step * sr))

    def at(self, step):
        return int(round(step * self.step * self.sr))

    def place(self, step, sound, g=1.0):
        add(self.buf, sound, self.at(step), g, wrap=True)

    def pattern(self, start, text, make, g=1.0, repeat=1):
        """text: one char per 16th; 'x' is an accent, 'o' a normal hit, '.' a rest."""
        steps = len(text)
        for r in range(repeat):
            for i, c in enumerate(text):
                if c in "xo":
                    self.place(start + r * steps + i, make(), g * (1.0 if c == "x" else 0.6))

    def dur(self, steps):
        return steps * self.step


def master(buf, sr, room=0.0, size=1.0, peak=0.85):
    if room:
        buf = reverb(buf, sr, size=size, mix=room, loop=True)
    buf = highpass1(buf + buf, 30.0, sr)[len(buf):]   # twice round, so the filter is settled at the seam
    buf = normalize(buf, 1.0)
    buf = drive(buf, 1.3)
    return normalize(buf, peak)


def chord_tones(root, quality):
    third = 3 if quality == "m" else 4
    return [root, root + third, root + 7]


def mus_menu(rng):
    """The front end: D minor, slow. A cold pad, a falling arpeggio of bells, a distant
    war drum, and a theme on a muted horn in the second half."""
    t = Track(84, 16)
    prog = [(50, "m"), (46, ""), (53, ""), (45, "")] * 4         # Dm Bb F A
    for bar, (root, q) in enumerate(prog):
        s = bar * 16
        tones = chord_tones(root + 12, q)
        t.place(s, pad([midi(n) for n in tones], t.dur(16) + 0.4, cutoff=900.0, attack=0.8), 0.9)
        t.place(s, bass(midi(root - 12), t.dur(12), cutoff=300.0), 0.6)
        for k, n in enumerate((tones[2] + 12, tones[1] + 12, tones[0] + 12, tones[1] + 12) * 2):
            t.place(s + k * 2, bell(midi(n), 1.2, MSR, 1.0, 2.0), 0.16 if k % 2 else 0.22)
        if bar >= 4:
            t.pattern(s, "x.......o.....o.", lambda: tom(rng, 70), 0.55)
        if bar >= 8:
            t.pattern(s, "........x.......", lambda: snare(rng, tail=0.2), 0.25)
    theme = [(74, 6), (72, 2), (69, 8), (70, 6), (69, 2), (65, 8),
             (65, 4), (67, 4), (69, 8), (73, 8), (76, 8),
             (74, 6), (72, 2), (69, 8), (70, 4), (72, 4), (74, 8),
             (77, 6), (76, 2), (74, 8), (73, 8), (74, 16)]
    pos = 8 * 16
    for note, length in theme:
        if pos >= t.steps:
            break
        t.place(pos, brass(midi(note), t.dur(length) * 0.95, bright=0.5), 0.35)
        pos += length
    return master(t.buf, MSR, room=0.45, size=1.3), MSR


def mus_directorate(rng):
    """The Directorate: C minor, a march. Snare rolls, a driving bass ostinato, square-wave
    brass, a call-and-answer theme."""
    t = Track(112, 16)
    prog = [(48, "m"), (48, "m"), (44, ""), (46, ""), (48, "m"), (51, ""), (43, ""), (43, "")] * 2
    for bar, (root, q) in enumerate(prog):
        s = bar * 16
        tones = chord_tones(root, q)
        t.pattern(s, "x...o...x...o...", lambda: kick(rng), 0.9)
        t.pattern(s, "....x..o....x.oo", lambda: snare(rng), 0.6)
        t.pattern(s, "o.o.o.o.o.o.o.oo", lambda: hat(rng), 0.25)
        for k, off in enumerate((0, 0, 7, 0, 12, 0, 7, 10)):
            t.place(s + k * 2, bass(midi(root - 12 + off), t.dur(2) * 0.9, grit=0.2), 0.7)
        if bar % 4 == 3:
            t.pattern(s + 8, "oooooooo", lambda: snare(rng, tail=0.05), 0.35)   # a roll into the next phrase
        t.place(s, pad([midi(n + 12) for n in tones], t.dur(16), cutoff=1100.0, attack=0.3), 0.45)
    theme = [(67, 3), (67, 1), (72, 4), (70, 2), (68, 2), (67, 4),
             (65, 3), (67, 1), (68, 4), (67, 4), (63, 4),
             (65, 3), (65, 1), (68, 4), (67, 2), (65, 2), (63, 4),
             (62, 3), (63, 1), (65, 4), (67, 8)]
    for half in (0, 8):
        pos = half * 16
        for note, length in theme:
            t.place(pos, brass(midi(note + (0 if half == 0 else 12)), t.dur(length) * 0.9), 0.5 if half else 0.4)
            pos += length
        pos = half * 16 + 64
        for note, length in theme[:11]:
            t.place(pos, lead(midi(note), t.dur(length) * 0.85, MSR, "square"), 0.12)
            pos += length
    return master(t.buf, MSR, room=0.18, size=0.9), MSR


def mus_clans(rng):
    """The Scrap Clans: E phrygian, fast and heavy. Distorted power chords, pounding toms,
    scrap-metal percussion and a snarling lead."""
    t = Track(138, 16)
    riff = [(40, 0, 3, False), (40, 3, 1, True), (40, 4, 2, True), (41, 6, 2, False),
            (40, 8, 3, False), (40, 11, 1, True), (43, 12, 2, False), (41, 14, 2, False)]
    riff_b = [(45, 0, 4, False), (45, 4, 2, True), (43, 6, 2, False),
              (41, 8, 4, False), (41, 12, 2, True), (40, 14, 2, False)]
    for bar in range(16):
        s = bar * 16
        section_b = 8 <= bar < 12
        for root, off, length, mute in (riff_b if section_b else riff):
            t.place(s + off, power_chord(rng, root, t.dur(length) * 0.95, mute=mute), 0.5)
            t.place(s + off, bass(midi(root - 12), t.dur(length) * 0.9, grit=0.6, cutoff=600.0), 0.5)
        t.pattern(s, "x..ox.o.x..ox.o.", lambda: kick(rng, punch=1.5), 0.9)
        t.pattern(s, "....x.......x...", lambda: snare(rng, tail=0.1), 0.7)
        t.pattern(s, "..o...o...o...oo", lambda: clank(rng), 0.25)
        if bar % 4 == 3:
            t.pattern(s + 8, "x.o.x.o.", lambda: tom(rng, 95), 0.5)
    snarl = [(64, 2), (65, 2), (67, 4), (65, 2), (64, 2), (62, 4),
             (64, 2), (67, 2), (70, 4), (69, 2), (67, 2), (65, 4)]
    for half in (4, 12):
        pos = half * 16
        for rep in range(2):
            for note, length in snarl:
                x = drive(lead(midi(note), t.dur(length) * 0.9, MSR, "saw"), 3.0)
                t.place(pos, crush(x, 7, 2), 0.18)
                pos += length
    return master(t.buf, MSR, room=0.1, size=0.7, peak=0.88), MSR


def mus_concord(rng):
    """The Lumen Concord: A lydian, slow and luminous. Interlocking bell arpeggios, a wide
    airy pad, a soft pulse and a long reverb."""
    t = Track(92, 16)
    prog = [(57, ""), (59, ""), (57, ""), (64, ""),
            (54, "m"), (59, ""), (52, ""), (57, "")] * 2               # A B A E F#m B E A
    for bar, (root, q) in enumerate(prog):
        s = bar * 16
        tones = chord_tones(root, q)
        t.place(s, pad([midi(n) for n in tones] + [midi(tones[0] + 12)], t.dur(16) + 0.6,
                       cutoff=2200.0, attack=1.0), 0.55)
        arp = [tones[0] + 12, tones[1] + 12, tones[2] + 12, tones[0] + 24]
        for k in range(8):
            n = arp[(k * 3 + bar) % 4]
            t.place(s + k * 2, bell(midi(n), 1.4, MSR, 1.6, 3.0), 0.18 if k % 2 else 0.26)
        if bar >= 2:
            for k in range(4):
                t.place(s + 1 + k * 4, bell(midi(arp[(k + 2) % 4] + 12), 0.8, MSR, 0.8, 2.0), 0.08)
        t.pattern(s, "x.......x.......", lambda: kick(rng, punch=0.2), 0.45 if bar >= 4 else 0.0)
        t.pattern(s, "..o...o...o...o.", lambda: hat(rng, tau=0.012), 0.12 if bar >= 4 else 0.0)
        t.place(s, bass(midi(root - 24), t.dur(14), cutoff=200.0), 0.45)
    melody = [(81, 8), (83, 4), (85, 4), (88, 12), (87, 4), (85, 8), (83, 8),
              (81, 8), (78, 8), (80, 8), (76, 8)]
    pos = 8 * 16
    for note, length in melody:
        x = bell(midi(note), t.dur(length) + 0.5, MSR, 0.7, 1.0, tau=t.dur(length) * 0.6)
        t.place(pos, x, 0.3)
        pos += length
    return master(t.buf, MSR, room=0.5, size=1.6, peak=0.8), MSR


# ── the catalogue ────────────────────────────────────────────────────────────
# name -> (function, peak). Variants share a function and differ by their name's seed.

def voice(fac, kind):
    return lambda rng: VOICES[fac](rng, kind)


SFX = {
    "w_rifle1": (w_rifle, 0.8), "w_rifle2": (w_rifle, 0.8),
    "w_mg": (w_mg, 0.8),
    "w_cannon1": (w_cannon, 0.9), "w_cannon2": (w_cannon, 0.9),
    "w_artillery": (w_artillery, 0.95),
    "w_rocket": (w_rocket, 0.85),
    "w_flame": (w_flame, 0.8),
    "w_beam1": (w_beam, 0.75), "w_beam2": (w_beam, 0.75),
    "w_beam_heavy": (w_beam_heavy, 0.85),
    "hit_bullet1": (hit_bullet, 0.5), "hit_bullet2": (hit_bullet, 0.5), "hit_bullet3": (hit_bullet, 0.5),
    "hit_shell": (hit_shell, 0.8),
    "boom_small": (boom_small, 0.9),
    "boom_large": (boom_large, 0.95),
    "collapse": (collapse, 0.95),
    "zap": (zap, 0.7),
    "zap_heavy": (zap_heavy, 0.85),
    "flame_hit": (flame_hit, 0.6),
    "die_soldier1": (die_soldier, 0.55), "die_soldier2": (die_soldier, 0.55),
    "build_place": (build_place, 0.85),
    "build_scaffold1": (build_scaffold, 0.6), "build_scaffold2": (build_scaffold, 0.6),
    "build_done": (build_done, 0.8),
    "ui_click": (ui_click, 0.6),
    "ui_unit_ready": (ui_unit_ready, 0.7),
    "ui_structure_ready": (ui_structure_ready, 0.75),
    "ui_low_power": (ui_low_power, 0.75),
    "ui_no_funds": (ui_no_funds, 0.7),
    "ui_under_attack": (ui_under_attack, 0.75),
}
for _fac in ("dir", "cl", "lc"):
    for _kind in ("select", "move", "attack"):
        for _v in (1, 2):
            SFX["%s_%s%d" % (_fac, _kind, _v)] = (voice(_fac, _kind), 0.75)

LOOPS = {"amb_wind": (amb_wind, 0.6), "amb_base": (amb_base, 0.6)}
MUSIC = {"mus_menu": mus_menu, "mus_directorate": mus_directorate, "mus_clans": mus_clans,
         "mus_concord": mus_concord}


def build(name):
    rng = rng_for(name)
    if name in SFX:
        fn, peak = SFX[name]
        x = fade(trim(normalize(highpass1(fn(rng), 25.0), peak)))
        write_wav(os.path.join(SFX_DIR, name + ".wav"), x, SR)
        return len(x) / SR
    if name in LOOPS:
        fn, peak = LOOPS[name]
        x, sr = fn(rng)
        write_wav(os.path.join(SFX_DIR, name + ".wav"), normalize(x, peak), sr)
        return len(x) / sr
    if name in MUSIC:
        x, sr = MUSIC[name](rng)
        write_wav(os.path.join(MUSIC_DIR, name + ".wav"), x, sr)
        return len(x) / sr
    raise SystemExit("sfx.py: no sound named %r (try --list)" % name)


def main(argv):
    names = list(SFX) + list(LOOPS) + list(MUSIC)
    if argv and argv[0] == "--list":
        print("\n".join(names))
        return
    for name in argv or names:
        sec = build(name)
        print("%-22s %6.2f s" % (name, sec))


if __name__ == "__main__":
    main(sys.argv[1:])
