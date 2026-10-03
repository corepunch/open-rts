# open-rts — Claude Code instructions

## Build

```sh
make
```

## Smoke tests (no display required)

Always use `SDL_VIDEODRIVER=dummy` for non-interactive checks and screenshots so
they work in headless/CI environments:

```sh
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check
env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /private/tmp/open-rts-smoke.bmp
env SDL_VIDEODRIVER=dummy build/bin/kknd --check
env SDL_VIDEODRIVER=dummy build/bin/warcraft2 --check
```

## Renderer notes

- The world, HUD, and menus draw into one 8-bit indexed framebuffer. Present
  uploads that buffer to a single streaming texture and scales it to the window.
- `--software` selects only the SDL present backend (`SDL_RENDERER_SOFTWARE`).
  It is not required for the map to show distinct tiles.
- `--check` and `--screenshot` use the software present backend automatically.
  `--screenshot` writes an 8-bit indexed BMP.

## Data layout

```
data/REIGN/dark    — Dark Reign game files
data/DCOLONY       — Dark Colony game files
data/WAR2          — Warcraft II game files (MAINDAT.WAR, loose PUDs)
```

## Per-game binaries

Each game has its own binary in `build/bin/`:
- `build/bin/dark-colony`  — Dark Colony
- `build/bin/dark-reign`   — Dark Reign
- `build/bin/7legion`      — 7th Legion
- `build/bin/kknd`         — KKnD
- `build/bin/warcraft2`   — Warcraft II (`data/WAR2/ALAMO.PUD`)

No `--game` flag needed. No dynamic plugin loading.
