# open-rts — Codex / agent instructions

## Build

```sh
make
```

## Running games

Use `--game <id>` to select a plugin.  Omit `--game` to default to Dark Reign.

```sh
# Dark Reign (default)
build/bin/open-rts --game dark-reign data/REIGN/dark scenario/FIXED/M01F/M01F.SCN ucfcnst0.spr

# Dark Colony
build/bin/open-rts --game dark-colony data/DCOLONY SCENARIO/HUMAN/HUMAN01.MAP SPRITES/TROOPER1.SPR

# 7th Legion
build/bin/open-rts --game 7legion data/7LEGION
```

Makefile convenience targets fill in the default paths:

```sh
make dark-reign
make dark-colony
make 7legion
```

## Smoke tests (no display required)

Always set `SDL_VIDEODRIVER=dummy` so tests run headless.
`--check` exits 0 on success; `--screenshot` writes a BMP then exits.
Both flags enable the software renderer automatically.

```sh
# Dark Reign
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-dark-reign.bmp

# Dark Colony
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check --game dark-colony
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-dark-colony.bmp --game dark-colony

# 7th Legion
env SDL_VIDEODRIVER=dummy build/bin/open-rts --check --game 7legion
env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot /private/tmp/open-rts-7legion.bmp --game 7legion
```

## Software renderer workaround

If the map renders the same tile everywhere (Metal/GPU driver bug on some machines),
force software rendering with `--software`:

```sh
build/bin/open-rts --software
build/bin/open-rts --software --game dark-colony
build/bin/open-rts --software --game 7legion
```

## Data layout

```
data/REIGN/dark    — Dark Reign game files
data/DCOLONY       — Dark Colony game files
data/7LEGION       — 7th Legion game files (GFX/TILES*.BIM, GFX/*.COL, SFX/)
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

## Dark Colony reverse engineering

Use the original Dark Colony binaries and data files as the authority. The goal
is to reproduce what DC does, not to invent engine-side compatibility shims.
When a behavior is unclear, inspect `data/DCOLONY/DC.EXE`, the relevant MAP/SCN
files, and the SPR/FIN assets together.

Generate project-local r2ghidra output into an ignored folder:

```sh
mkdir -p reverse/dc-exe-r2ghidra
r2 -q -e bin.cache=true -A -c "afl" -c q data/DCOLONY/DC.EXE > reverse/dc-exe-r2ghidra/functions.txt
r2 -q -e bin.cache=true -A -c "pdg @@F" -c q data/DCOLONY/DC.EXE > reverse/dc-exe-r2ghidra/dc_exe.c
```

`reverse/dc-exe-r2ghidra/` is ignored by git. Regenerate it whenever needed, but
do not commit generated decompiler dumps. r2 may print missing DLL SDB warnings
for imports such as `dplayx`; those warnings are expected and do not usually
block decompilation.

Useful r2/r2ghidra commands:

```sh
# strings and references
r2 -q -e bin.cache=true -A -c "iz~draw" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "axt @ 0x00400000" -c q data/DCOLONY/DC.EXE

# function lists and focused code
r2 -q -e bin.cache=true -A -c "afl" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdg @ 0x0043a144" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pD 2200 @ 0x0043a144" -c q data/DCOLONY/DC.EXE
```

Search the generated C with `rg` first, then go back to r2 for exact addresses,
xrefs, disassembly, and decompiler context. Keep notes of stable findings in
`REFERENCES.md` when they explain file formats, object layout, startup flow, or
rendering behavior.

Search the generated C with `rg` first, then go back to r2 for exact addresses,
xrefs, disassembly, and decompiler context. Keep notes of stable findings in
`REFERENCES.md` when they explain file formats, object layout, startup flow, or
rendering behavior.

## Reverse Engineering Workflow

To avoid getting overwhelmed by the binary's size, follow this broad-to-narrow approach:

1. **Broad Phase (Discovery):**
   - Generate the full `dc_exe.c` dump using the commands above.
   - Use `rg` (ripgrep) to search for keywords (e.g., "draw", "load", "sprite", "health", "money") to identify high-value functions.
   - Use the function list (`functions.txt`) to get a sense of the overall structure.

2. **Narrow Phase (Analysis & Implementation):**
   - Once a target function is identified, isolate it in `r2` to analyze its exact logic and call graph.
   - Refactor the function into clean C++ in the codebase, maintaining a 1:1 behavioral match.
   - Iterate: Identify the functions called by your current target and analyze those next.

3. **Incremental Progress:**
   - Work in small, logical steps (e.g., "Analyze coordinate clipping" $\rightarrow$ "Analyze unit movement").
   - Immediately document every discovered offset, constant, or data structure layout in `REFERENCES.md`.

Reverse-engineering rules for Dark Colony:

- Prefer DC-shaped data structures and procedures over generic abstractions.
- Derive positions, animation handoffs, frame choices, remaps, intensities, and
  sort/order behavior from MAP/SCN/SPR/FIN data or DC.EXE tables.
- Do not add hacks keyed by sprite name, building name, mission name, or visual
  coincidence. If something needs a special case, find the DC data field or code
  path that causes it.
- Treat Dark Colony world coordinates as bottom-up, matching the original game.
  Remove old top-left/y-flip bridge logic rather than compensating around it.
- Preserve SCN/MAP load pass order when it matters. DC loads some scenario data
  in separate passes; if a value looks flipped or delayed, check the loader flow
  before adding conversions.
- Use FIN command offsets directly for sprite layout. For example, building
  placement, Barracks Trooper release handoff, overlays, and multi-part sprites
  should come from FIN/state offsets converted through the game cell size.
- Keep the pentagon/base/map layer stable and reposition sprites as DC does;
  do not move terrain to make sprites appear correct.
- For animation playback, follow DC state chains and frame timing. For Barracks
  production, the Trooper visual release comes from `HUBU.FIN`/`TRSCBUILD0`, and
  the spawned unit handoff is the delta between the final release frame offset
  and the standing Trooper state offset.
- For selection and health overlays, use the actual DC sprites/frames and health
  thresholds found in the binary/assets instead of drawing replacement shapes.
- For resources and production, use SCN team money, product costs/prerequisites,
  producer queues, release animations, and unit spacing behavior from DC logic.
