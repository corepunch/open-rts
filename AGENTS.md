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

## Build: never list individual source files in the Makefile

Always use `find` to collect sources for a build target — never enumerate
individual `.c` files. Each engine subsystem directory and each game directory
is a self-contained unit; adding or removing a file should require no Makefile
edit.

```makefile
# correct
ENGINE_SOURCES := $(sort $(shell find driver render hud interface play -name '*.c'))
DC_GAME_SOURCES := $(sort $(shell find games/dark-colony -name '*.c'))

# wrong — add a new file and the build silently ignores it
ENGINE_SOURCES := driver/d_main.c render/r_draw.c ...
```

Use `$(sort ...)` to keep the list deterministic across platforms.

## Code style: header include guards

Use classic C `#ifndef` guards in all header files — never `#pragma once`.
The guard symbol is `__FILENAME__` in uppercase with the extension replaced by
the uppercase suffix, e.g. `w_spr.h` → `__W_SPR__`, `dc_types.h` → `__DC_TYPES__`.

```c
/* correct */
#ifndef __W_SPR__
#define __W_SPR__
/* ... */
#endif

/* wrong */
#pragma once
```

## Code style: designated initializers for enum-indexed arrays

Always use C99 designated initializers (`[ENUMNAME] = value`) when initializing
arrays indexed by an enum. This applies to `sprnames[]`, `mobjinfo[]`, and any
similar table where positional order could silently diverge from the enum.

```c
/* correct */
const char *const sprnames[NUMSPRITES] = {
    [SPR_DC_CURSOR_CURS] = "CURSOR/CURS.SPR",
    [SPR_DC_GRAY]        = "SPRITES/GRAY.SPR",
};

/* wrong — positional, silent mismatch if enum changes */
const char *const sprnames[NUMSPRITES] = {
    "CURSOR/CURS.SPR",
    "SPRITES/GRAY.SPR",
};
```

## Code style: prefer structs over loose fields

Work at the highest abstraction level the data supports. Group related scalars
into named structs and use those structs as the unit of work. Never manipulate
individual x/y or w/h components when a struct literal, assignment, or helper
function would be shorter and more expressive.

**Canonical types** (in `driver/m_vec.h`):

| Type | Fields | Use for |
|---|---|---|
| `ivec2_t` | `int x, y` | integer 2-D point (mouse, tile coords) |
| `fvec2_t` | `float x, y` | float 2-D vector (camera position, velocity) |
| `isize2_t` | `int w, h` | integer extent (window size, cell size) |

**Rules:**
- Use compound literals to initialize or assign whole structs:
  `app.win = (isize2_t){ 640, 480 };`  not two separate assignments.
- Use helper functions (`fvec2_add`, `fvec2_sub`) instead of per-component
  arithmetic where the intent is a vector operation.
- When adding a new 2-D quantity, add it as `ivec2_t` / `fvec2_t` / `isize2_t`
  from the start, not as a pair of scalars.
- Same principle applies to any new struct in the codebase: group logically
  related fields, then write helpers that operate on the whole group.

## Game and Network Architecture

We base our game loop, object thinker model, and network architecture on the
Doom/Heretic/Hexen engine family (id Software / Raven Software, 1993–1996).
Reference source code lives in `reference/` (git-ignored):

```
reference/DOOM/      — Doom / Doom II source (id Software)
reference/Heretic/   — Heretic source (Raven Software)
reference/Hexen/     — Hexen source (Raven Software)
```

Key patterns to follow from that lineage:
- **Thinker/action system** — objects advance via per-tick `thinker_t` callbacks
- **Fixed-point math** — use integer fixed-point for deterministic simulation
- **Gametic / ticrate** — decouple simulation tics from render frames
- **Lock-step networking** — exchange input commands per tic, never game state

### How the object/unit system maps to Doom

| Doom | open-rts |
|---|---|
| `state_t.action` — func ptr on the *state*, not the entity | `State.action StateAction` — same |
| `P_SetMobjState` chains zero-tic states immediately | `set_unit_state` does the same |
| `mobjinfo_t` with `spawnstate/seestate/missilestate/deathstate` | `MobjInfo` with identical fields |
| `mobj->tics` counts down per tick; on 0 → `nextstate` | `unit->tics` — same |
| Single `P_MobjThinker` drives all objects | `update_units()` is the single loop |
| Action functions fire **on state entry** (e.g. `A_Chase`, `A_PosAttack`) | `StateAction` fires on entry in `set_unit_state` |
| Doubly-linked `thinker_t` ring (polymorphic: doors, lights, mobjs) | Flat `Unit` array + swap-compaction (no polymorphic thinkers needed) |

The attack cooldown (`attack_cooldown_left_ms`) is the one remaining non-tic timer. It serves as
the rate-of-fire gate between attack cycles. In pure Doom style this would be encoded as a
recovery-state chain with specific tics. Until that refactor is done, `attack_cooldown_left_ms`
is decremented in ms and is set from `ActorType.attack_cooldown_ms`.

Attack *animation* locking is done via the state group: a unit in a `misc1 == 3` state (attack
group) will not interrupt its animation to walk or start a new attack. No separate
`attack_anim_left_ms` ms-timer is needed or used in the state-machine path.

### Unit balance configuration

Unit stats (speed, HP, damage, range, cooldown) are **hardcoded in C** inside each plugin, not
loaded from game data files at runtime. This mirrors Doom's `mobjinfo[]` table in `info.c`.

Each plugin has two config layers:
- `ActorType[]` — gameplay stats: speed, HP, attack damage/range/cooldown, effect names.
  Lives in `games/<Game>/plugin.c`.
- `MobjInfo[]` + `State[]` — state-machine entry points and animation.
  Lives in `games/<Game>/info.c` (generated by `tools/*_info_gen`).

Do **not** wire `dc_gamestat_units[]` (or any equivalent binary-extracted table) directly into
unit spawning. Extract those values once, author them as C literals in the plugin's `ActorType`
table, and commit. A separate extraction tool can help, but the authoritative source is the C
array, not the binary.

### Dark Colony unit notes

- **Exploiter speed**: 3.5 grid-units/s. Heavy harvester — should be slower than infantry (Trooper 5.0).
- **Exploiter walk cycle**: the original game has 16 animation frames per facing. The generated
  `info.c` only has 2 RUN states (RUN1/RUN2). The missing 14 frames need to be extracted and added
  by regenerating `info.c` via `tools/dc_info_gen` against the actual EXPL.SPR frame table.
- **Exploiter deploy orientation**: when ordered to harvest, the unit first rotates to face
  south-east (code 6), then plays the DEPLOY1-20 animation. Code already does this; preserve it.
- **Exploiter work (harvesting) animation**: the WORK1-15 overlay loop (frames 25-33) was the
  probe arm cycling open/closed — incorrect. WORK states now show only the static deployed body
  (frame 34, DC_NO_OVERLAY). The original game plays a pulsating light effect during harvesting;
  the correct overlay frames for that light are TBD — identify via DC.EXE analysis and add as
  a looping overlay or spawned visual effect on the `harvest_state_id` entry.

## Dark Colony direction

Treat Dark Colony as the first game to reproduce faithfully, not as a plugin
architecture exercise. The current `games/dark-colony/` location is only a
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

## When actual game behavior or metadata is required

When a loader or renderer needs to match the original executable, use the
original binary as the authority before inventing a format rule or engine-side
fallback. The focused disassembly loop is:

1. Open the actual game executable with `radare2` (`r2`) and run its analysis
   pass (`-A`), for example:

   ```sh
   r2 -q -e bin.cache=true -A data/7LEGION/legion.exe
   ```

2. Locate the relevant data or buffer, then use `axt @ <address>` to find code
   references to it. For 7th Legion map work, trace the references to the
   `MAPT` and `MAPOVL` buffers. If the buffer is not named, start from a file
   name/string reference or the load routine and identify the buffer address in
   the surrounding disassembly.

3. Use `pdf @ <function>` on each relevant routine. Follow the caller/callee
   path far enough to establish the real buffer size, read order, endianness,
   transforms, metadata fields, and how decoded values reach rendering or
   gameplay. `pd` is useful for a small instruction window; `pdf` is preferred
   for the complete routine and local control flow.

4. Implement the smallest faithful change in the game loader/plugin. Preserve
   the original pass order and native values at the game boundary; do not
   replace an unknown executable-derived field with a visual guess or a
   sprite-name special case. Record stable offsets, constants, and format
   findings in `REFERENCES.md`.

5. Rebuild with `make`, regenerate a headless capture using the relevant
   `SDL_VIDEODRIVER=dummy ... --screenshot` command, and visually inspect the
   result. Repeat the disassembly → implementation → rebuild → capture → visual
   inspection loop for each iteration until the behavior and placement match
   the original. Keep generated disassembly dumps in ignored `reverse/`
   directories; never commit them.

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

## Git workflow

- Commit all completed working-tree changes after verification so progress stays
  easy to inspect and bisect.
- Write a clear imperative subject and a detailed commit body describing the
  behavior changed, the reason for the change, and the verification performed.
- Do not push commits unless the user explicitly asks for a push.
