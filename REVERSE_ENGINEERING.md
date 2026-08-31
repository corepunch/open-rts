# Reverse Engineering Games

Use this workflow when reproducing behavior or metadata from the original game
executables. It currently applies to Dark Colony and 7th Legion, and should be
adapted to other games as their executable-derived behavior is investigated.

## Source of truth

The original executable and game data are authoritative. Cross-check executable
logic against the relevant map, scenario, sprite, animation, palette, and other
native assets. Do not invent a format rule, visual approximation, or engine-side
compatibility shim when the original inputs can establish the behavior.

Preserve native values, data layout, and load-pass order at each game boundary.
Record stable offsets, constants, object layouts, calling conventions, and file
format findings in `REFERENCES.md`. Keep generated analysis under the ignored
`reverse/` directory and never commit decompiler dumps.

## Preserve findings

Reverse-engineering output is useful only if the next investigation can find
and reproduce it. Before ending an investigation, promote useful conclusions
out of chat logs, temporary command output, local screenshots, and ignored
decompiler dumps:

1. Record detailed executable behavior in a game-specific report under
  `docs/`, including the executable fingerprint and exact addresses or data
  offsets.
2. Record external tools, source repositories, articles, and screenshot URLs
  in `REFERENCES.md` with enough provenance to locate them again.
3. State whether each conclusion is confirmed by instructions/data, inferred
  from multiple observations, disproven, or still unknown.
4. Include the relevant formula or layout, its consequence for open-rts, and a
  focused command or test that reproduces the evidence.
5. Preserve corrected hypotheses when they warn against a plausible but wrong
  implementation. Mark them as superseded rather than deleting the lesson.

Do this even when no code is changed or the implementation is deferred.

## Fingerprint the toolchain

Do this once per executable before interpreting substantial decompiler output.
Inspect the PE and Rich headers, linker metadata, imports, and a few representative
functions:

```sh
rabin2 -I data/DCOLONY/DC.EXE
rabin2 -H data/DCOLONY/DC.EXE
r2 -q -c "iI" -c "iH" -c "ii" -c q data/DCOLONY/DC.EXE
r2 -q -e bin.cache=true -A -c "pdf @ <function>" -c q data/DCOLONY/DC.EXE
```

Note the likely compiler and version, but distinguish evidence from inference.
Check several functions for argument passing, register use, callee cleanup,
prologue and epilogue shapes, frame-pointer use, stack alignment, and runtime
helper calls. Compiler idioms affect how r2ghidra reconstructs parameters and
locals. For example, a register-heavy convention may be a Watcom convention,
not game logic.

## Broad-to-narrow workflow

Keep r2/r2ghidra as the primary analysis path.

1. **Generate broad discovery output.** Create a function list and a full C-like
   dump in a game-specific ignored directory. For Dark Colony:

   ```sh
   mkdir -p reverse/dc-exe-r2ghidra
   r2 -q -e bin.cache=true -A -c "afl" -c q data/DCOLONY/DC.EXE \
     > reverse/dc-exe-r2ghidra/functions.txt
   r2 -q -e bin.cache=true -A -c "pdg @@F" -c q data/DCOLONY/DC.EXE \
     > reverse/dc-exe-r2ghidra/dc_exe.c
   ```

   Missing DLL SDB warnings for imports such as `dplayx` are expected and do
   not usually block decompilation.

2. **Search the generated output.** Use `rg` for likely strings, constants,
   offsets, asset names, and behavior terms. Use `functions.txt` to identify
   candidate routines without trying to understand the entire binary.

3. **Narrow to the controlling routine.** Return to `r2` for exact addresses,
   references, complete disassembly, and local control flow:

   ```sh
   # strings and references
   r2 -q -e bin.cache=true -A -c "iz~draw" -c q data/DCOLONY/DC.EXE
   r2 -q -e bin.cache=true -A -c "axt @ <address>" -c q data/DCOLONY/DC.EXE

   # focused decompilation and disassembly
   r2 -q -e bin.cache=true -A -c "pdg @ <function>" -c q data/DCOLONY/DC.EXE
   r2 -q -e bin.cache=true -A -c "pdf @ <function>" -c q data/DCOLONY/DC.EXE
   ```

   Prefer `pdf` for a complete routine and `pd` or `pD` for a small instruction
   window. Follow callers and callees only far enough to establish buffer sizes,
   read order, endianness, transforms, metadata fields, and how values reach
   rendering or gameplay.

4. **Port the smallest complete behavior.** Write clear C that preserves the
   observed semantics. A near-literal port is appropriate when it protects
   startup flow, native storage, rendering order, animation behavior, or a
   subtle formula. Do not assume r2ghidra's inferred types or signatures are
   correct; reconcile them with the fingerprint and call sites.

5. **Iterate locally.** Analyze one behavior at a time. Document newly stable
   findings immediately, then move to the next controlling function.

For 7th Legion map work, trace references to the `MAPT` and `MAPOVL` buffers.
If a buffer is unnamed, begin at a filename or load-routine string reference and
identify the destination in the surrounding disassembly.

## Verify behavior

Prefer behavioral equivalence to instruction equivalence.

For routines with numeric side effects, derive a small set of representative
input/output pairs from constants, comparisons, branch boundaries, tables, and
rounding behavior in the original disassembly. Include ordinary values and edge
cases on both sides of important branches. Add focused tests asserting that the
ported C produces the same damage, movement delta, frame index, coordinate,
threshold, or other result for those inputs.

Also verify at the native data-shape level. Check decoded values and runtime
layout against the original assets and executable contracts. For Dark Colony,
this includes constraints such as `DC_MAX_OBJECTS == 800` and
`DC_OBJECT_SIZE == 0xdc`.

After implementation:

1. Run the narrow behavioral tests.
2. Rebuild with `make`.
3. Run the relevant headless `--check` command.
4. Generate a headless screenshot with `SDL_VIDEODRIVER=dummy` when the change
   affects loading or rendering, then inspect it visually.

Repeat analysis, implementation, and behavioral verification until the observed
behavior and placement match the original.

## Structural comparison

Use `radiff2` or objdiff only as secondary structural checks when a comparable,
isolated rebuilt routine exists. Function boundaries, call-graph shape, branch
count, and omitted paths can expose a misunderstanding even when compiler,
architecture, or surrounding program structure differs.

Do not treat instruction differences as failures by default and do not optimize
the normal workflow for byte-identical code generation. Reserve matching
decompilation and byte-level objdiff work for isolated, high-value routines such
as a core movement tick, pathing primitive, or repeatedly troublesome formula.
For those routines, first lock down the calling convention, types, constants,
and behavioral tests so code-generation differences are not mistaken for game
semantics.

## Dark Colony rules

- Inspect `data/DCOLONY/DC.EXE` together with the relevant MAP/SCN, SPR, and FIN
  assets whenever behavior is unclear.
- Prefer DC-shaped data structures and procedures over generic abstractions.
- Derive positions, animation handoffs, frame choices, remaps, intensities, and
  sort order from MAP/SCN/SPR/FIN data or DC.EXE tables.
- Do not add hacks keyed by sprite name, building name, mission name, or visual
  coincidence. Find the DC data field or code path that causes the behavior.
- Treat Dark Colony world coordinates as bottom-up. Remove old top-left or
  y-flip bridge logic instead of compensating around it.
- Preserve SCN/MAP load-pass order. If a value appears flipped or delayed,
  inspect loader flow before adding conversions.
- Use FIN command offsets directly for building placement, Barracks Trooper
  release handoff, overlays, and multi-part sprite layout, converting through
  the game cell size where required.
- Keep the pentagon, base, and map layers stable and reposition sprites as the
  original does; do not move terrain to make sprites appear correct.
- Follow original state chains and frame timing. For Barracks production, the
  Trooper release comes from `HUBU.FIN`/`TRSCBUILD0`; the spawned-unit handoff
  is the delta between the final release-frame offset and standing-state offset.
- Use original sprites, frames, and executable-derived thresholds for selection
  and health overlays instead of replacement shapes.
- Use SCN team money, product costs and prerequisites, producer queues, release
  animations, and unit-spacing behavior for resources and production.
