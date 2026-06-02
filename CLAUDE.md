# open-rts — Claude Code instructions

## Build

```sh
make
```

## Smoke tests (no display required)

Always use `SDL_VIDEODRIVER=dummy` for non-interactive checks and screenshots so
they work in headless/CI environments:

```sh
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check --game dark-colony
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-smoke.bmp
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-dark-colony-ui.bmp --game dark-colony
```

## Renderer notes

- Hardware renderer (`SDL_RENDERER_ACCELERATED`) is the default for interactive mode.
- On some machines the Metal/GPU backend renders the same tile everywhere due to
  incorrect source-rect handling. Pass `--software` to force `SDL_RENDERER_SOFTWARE`:
  ```sh
  build/bin/open-rts --software
  build/bin/open-rts --software --game dark-colony
  ```
- `--check` and `--screenshot` already use the software renderer automatically.

## Data layout

```
data/REIGN/dark    — Dark Reign game files
data/DCOLONY       — Dark Colony game files
```

## Plugins

Plugins are shared libraries in `build/libs/`. Each plugin exports a
`rts_plugin_entry` symbol. The main binary loads them at runtime via `--game`.
