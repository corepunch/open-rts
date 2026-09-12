# Loader simplification verification — 2026-09-09

Baseline: `46f826a` (before this cleanup). The model is the Dark Colony sprite
cleanup starting at `992ad87`: decode into final storage, borrow checked file
records, and remove parallel arrays and owners. This is an implementation
comparison, not a new claim that the current rendering matches retail games.

## Changes

- Dark Colony terrain now reads validated MAP/MTG bytes into the level's tile,
  overlay, collision, and transform arrays. OVH converts directly into final
  cell colors only when used. Unconsumed MTG sidecars, O16 words, decoded
  MapCell copies, overview copies, and the MapNative wrapper are gone.
- The active Dark Colony level owns one parsed SCN. Initial things spawn directly
  into the thinker list, in the same city-then-dynamic order. The temporary
  800-object pool, hidden-object array, active-index list, duplicate SCN parse,
  and derived UnitConfig array are gone. The documented `DcObject` binary
  layout remains available for native evidence; it is not simulation storage.
- Dark Reign borrows FTG directory records and SPR sections. Each sprite frame
  decodes into one reusable RGBA buffer; no atlas, indexed atlas, copied section
  table, or parallel geometry arrays. Map setup shares one SCN read across
  terrain selection, decoration, resource, and team parsing.
- 7th Legion keeps validated span bounds in final sprite cells. It reuses one
  frame canvas, trims trailing null frames, and frees compressed input after
  expansion. The full frame-info table, atlas, and parallel geometry arrays
  are gone. Mission starts use a point, and unused map-number parsing is gone.
- KKnD retains only frame offsets while collecting animation order, then
  decodes each MOBD directly into its final cell texture. MAPD colors go
  straight into the terrain atlas; overlay visibility is accumulated while
  decoding, with no expanded per-layer images or later pixel scan.
- `W_ReadFile` reserves one trailing NUL byte outside `blob.size`, allowing text
  consumers to parse their owned read buffer without allocating/copying it.

## Catalog comparison

`tests/loader_catalog.c` fingerprints decoded pixels and placement/animation
metadata. Its map mode includes terrain, overlays, transforms, collision,
resources, decorations, team settings, camera, and initial mobjs in spawn order.
KKnD map comparisons additionally include every terrain-atlas pixel. Sprite
comparisons use a fixed test palette to isolate index decoding from palette
selection; screenshots exercise the normal game palettes.

| Catalog | Inputs | Accepted | Same rejection | Differences |
| --- | ---: | ---: | ---: | ---: |
| Dark Colony maps | 100 | 100 | 0 | 0 |
| Dark Reign maps | 82 | 82 | 0 | 0 |
| Dark Reign SPR records | 1,392 | 1,388 | 4 | 0 |
| 7th Legion maps | 1 | 1 | 0 | 0 |
| 7th Legion non-tile BIM files | 240 | 235 | 5 | 0 |
| KKnD LVL map candidates | 47 | 44 | 3 | 0 |
| KKnD MOBD members 0–99 | 100 | 57 | 43 | 0 |

Reproduce from a checkout with retail data present:

```sh
git worktree add --detach /private/tmp/open-rts-loader-baseline 46f826a
python3 tools/test_loaders.py --source-tree /private/tmp/open-rts-loader-baseline --output /private/tmp/loaders-before
python3 tools/test_loaders.py --output /private/tmp/loaders-after
python3 - <<'PY'
from pathlib import Path
before = Path('/private/tmp/loaders-before')
after = Path('/private/tmp/loaders-after')
for path in sorted(before.glob('*.txt')):
    assert path.read_bytes() == (after / path.name).read_bytes(), path.name
    print('identical:', path.name)
PY
make test-loaders
```

The same current harness compiles against either source tree. Each run emits
input manifests, output fingerprints, and diagnostic logs. It does not copy
retail data or expose private format decoders through new runtime APIs.

Small fixtures check MAP row order/flags, MTG rows, SPR/BIM pixels, MOBD mirroring
and displacement, truncated headers/payloads, invalid runs/spans, and cleanup
when texture creation fails. A VCLZ output shorter than its offset word is now
rejected before reading it. KKnD transparent runs beyond the canvas are now
rejected, like overflowing literal runs. These are malformed-input corrections;
no retail catalog acceptance or decoded output changed.

## Screenshots and suite

All four default headless screenshots are byte-identical to the baseline and
were visually inspected. SHA-256:

| Game | BMP SHA-256 |
| --- | --- |
| Dark Colony | `1463cd0db6d86a4609fd9d8e304184d71058a4cd00f69276492a7c32bad49577` |
| Dark Reign | `79692f5bdfa61f8e87979abccec0912d9f1fbdcf67649129e77e60f337f78be8` |
| 7th Legion | `dc09cceb94e6bbc4c77f4c8b3daac175c8e478872ca879f63c03d25bb2369b5c` |
| KKnD | `eef806697795c7ded8638526df2494f838dabbe4587b7b5b30d835d007124b53` |

`make`, all four dummy-video `--check` runs, loader fixtures, and sprite-layout
checks pass. The full `make test` retains the same three failures observed on
`46f826a`: accepting an available Dark Colony build command, finding the player
Exploiter in combat/harvest, and finding HUMAN01's initial Trooper force. No
additional failing test was introduced.

## Reference and limits

`reference/DOOM/p_setup.c`, `P_LoadVertexes` and `P_LoadThings`, reads native
records into final level data and calls `P_SpawnMapThing` for each thing. That is
the ownership/lifecycle model used here. Existing GZDoom image/geometry/resource
provenance remains in `REFERENCES.md`; its documented checkout was unavailable
in this workspace, so no new GZDoom source conclusions were drawn.

The comparison preserves existing game-specific conventions and fallbacks. It
does not verify them anew against executables. See the game findings documents
for preserved unknowns and the distinction between loader equivalence and
retail behavior. In particular, no missing animation directions, guessed
hotspots, sprite-name aliases, terrain offsets, or balance changes were added.

## Current C runner (2026-09-13)

The Python commands above record the original comparison procedure. The current
checkout builds the fixture/catalog binaries directly with Make, so they inherit
all engine libraries (including libpng). `tools/test_loaders/main.c` replaces the
old Python runner for sorted map/SPR manifests and catalog execution:

```sh
make build/test_loaders
build/test_loaders --fixtures
build/test_loaders --output /private/tmp/loaders-current
```

`--source-tree DIR` targets checkouts that have the new `loader-catalogs` Make
rules; `--no-build` uses their existing catalog binaries. Catalog records and
manifest naming remain unchanged. All new repository tools are C.
