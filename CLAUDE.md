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
```

## Renderer notes

- Hardware renderer (`SDL_RENDERER_ACCELERATED`) is the default for interactive mode.
- On some machines the Metal/GPU backend renders the same tile everywhere due to
  incorrect source-rect handling. Pass `--software` to force `SDL_RENDERER_SOFTWARE`:
  ```sh
  build/bin/dark-colony --software
  build/bin/dark-reign --software
  ```
- `--check` and `--screenshot` already use the software renderer automatically.

## Data layout

```
data/REIGN/dark    — Dark Reign game files
data/DCOLONY       — Dark Colony game files
```

## Per-game binaries

Each game has its own binary in `build/bin/`:
- `build/bin/dark-colony`  — Dark Colony
- `build/bin/dark-reign`   — Dark Reign
- `build/bin/7legion`      — 7th Legion
- `build/bin/kknd`         — KKnD

No `--game` flag needed. No dynamic plugin loading.
