# open-rts — Codex / agent instructions

## Build

```sh
make
```

## Smoke tests (no display required)

Use `SDL_VIDEODRIVER=dummy` for all non-interactive checks and screenshots:

```sh
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check --game dark-colony
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-smoke.bmp
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-dark-colony-ui.bmp --game dark-colony
```

## Software renderer workaround

If the map renders the same tile everywhere (Metal/GPU driver bug on some machines),
force software rendering with `--software`:

```sh
build/bin/open-rts --software
build/bin/open-rts --software --game dark-colony
```

## Data layout

```
data/REIGN/dark    — Dark Reign game files
data/DCOLONY       — Dark Colony game files
```

## Dark Colony direction

Treat Dark Colony as the first game to reproduce faithfully, not as a plugin
architecture exercise. The current `plugins/DarkColony/` location is only a
practical code organization boundary; if plugin purity conflicts with matching
DC.EXE behavior, matching DC.EXE wins. Once one game works well, the codebase can
be refactored around the real multi-game needs discovered from that implementation.

Prefer DC-shaped runtime data and procedures over generic engine abstractions
while reproducing Dark Colony. For example, keep object data layout-compatible
with DC.EXE (`DC_MAX_OBJECTS == 800`, `DC_OBJECT_SIZE == 0xdc`) and fill unknown
fields by offset until their meaning is known. Decompiled routines may be ported
near-literally when that preserves startup flow, object storage, rendering, or
animation behavior.
