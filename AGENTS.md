# open-rts — Codex / agent instructions

## Build

```sh
make
```

## Code navigation with ctags

Use the generated `tags` files to jump to definitions of functions, structs,
enums, typedefs, and other C symbols. Editor integrations (vim, VS Code, etc.)
read these files automatically when placed in the project root or game dirs.

```sh
# Regenerate all tags (engine + per-game)
make tags
```

**Tags files:**
- `./tags` — engine code (driver/, render/, hud/, interface/, play/, game/)
- `games/<game>/tags` — game-specific code

After modifying source files, run `make tags` to keep symbols up to date.

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

## Code style: keep the implementation small and direct

Prefer the shortest clear implementation of the behavior we actually need.
The sprite-loader cleanups (`992ad87` through `2eadb91`) show the approach:
decode into final storage, borrow checked native records, reuse existing
helpers, and delete intermediate representations and needless indirection.

- Remove unnecessary work and state before adding abstractions. Do not build
  wrappers, callbacks, configuration knobs, or fallback paths for hypothetical
  future uses. A helper should simplify its callers or centralize a real rule.
- Keep one owner and one representation for each piece of data. Decode only
  what needs conversion; avoid copying whole tables just to rename fields.
- Use native dimensions, ranges, names, and flags directly. Do not reconstruct
  information already present in the asset or scan pixels to guess metadata.
- Build sprite frame/rotation definitions from native assets at load time. Do
  not generate duplicate asset tables into `info.c`; it owns sprite names and
  state references. Load all FIN frames without an action-name whitelist.
- Keep UI, cursor, and encyclopedia SPR images out of gameplay sprite IDs and
  `sprnames[]`. Store them separately by path in the UI image cache, including
  selection markers drawn over world units.
- No asset-name exceptions, guessed aliases, compensating offsets, or visual
  hacks. Trace the native lookup when data appears inconsistent. Distinguish
  verified native behavior from an explicitly requested engine behavior.
- Reuse whole-value operations and standard string functions. Keep temporary
  values local; retain owned storage only when its lifetime requires it.
- Delete dead calculations, unused parameters/includes, and accessors that
  merely return a public field. Consolidate cleanup under the actual owner.
- Keep checks that protect file spans, allocations, and ownership. Short code
  must still handle real failures; do not compress it into clever expressions.
- Verify simplifications against existing behavior. For asset loaders, compare
  catalog pixels and metadata; document any intentional behavior correction
  separately with native evidence. Fewer lines alone is not proof of a fix.

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

## Code style: prefer structs over loose fields

Work at the highest abstraction level the data supports. Group related scalars
into named structs and use those structs as the unit of work. Never manipulate
individual x/y or w/h components when a struct literal, assignment, or helper
function would be shorter and more expressive.

**Don't work with components; work with utility functions.** Operations on
`fvec2_t`, `fixed3_t`, `ivec2_t`, and `isize2_t` should use whole-value
helpers instead of open-coding arithmetic on their individual components.

**Canonical types** (in `driver/m_vec.h`):

| Type | Fields | Use for |
|---|---|---|
| `ivec2_t` | `int x, y` | integer 2-D point (mouse, tile coords) |
| `fvec2_t` | `float x, y` | float 2-D vector (camera position, velocity) |
| `fixed3_t` | `fixed_t x, y, z` | signed 32-bit 16.16 object position or momentum |
| `isize2_t` | `int w, h` | integer extent (window size, cell size) |

**Rules:**
- Use compound literals to initialize or assign whole structs:
  `app.win = (isize2_t){ 640, 480 };`  not two separate assignments.
- Use helper functions (`fvec2_add`, `fvec2_sub`) instead of per-component
  arithmetic where the intent is a vector operation.
- Keep simulation object positions and momentums as `fixed3_t`; convert to
  `fvec2_t` only at explicitly planar pathing, collision, UI, or render boundaries.
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
reference/GZDoom/    — primary modern sprite/texture/rendering reference
reference/DOOM95/source/ — Doom95 source reconstruction (reference only)
reference/DOOM95/dump/   — Doom95 debug info and decompilation evidence
```

Use GZDoom as the main reference for modern rendering architecture, especially
sprite images, texture ownership, palette translations, materials, and GPU
resource caching. The pinned checkout and relevant source paths are documented
in `REFERENCES.md`. Follow its separation between engine-owned source images,
sprite frame/rotation definitions, and renderer-owned hardware textures: game
loaders convert native formats into the common image representation and do not
leave format callbacks or opaque native sprite data in renderer structures.

Use the original Doom/Heretic/Hexen sources as the primary reference for game
simulation, state machines, thinkers, fixed-point math, and lock-step timing.

Key patterns to follow from that lineage:
- **Thinker/action system** — objects advance via per-tick `thinker_t` callbacks
- **Fixed-point math** — use integer fixed-point for deterministic simulation
- **Gametic / ticrate** — decouple simulation tics from render frames
- **Lock-step networking** — exchange input commands per tic, never game state

### Standing refactoring rule: move closer to Doom at every opportunity

Whenever work exposes a difference between our code and Doom, treat it as an
opportunity to adopt Doom's approach. Read the reference implementation, then
bring the relevant code closer in naming, data structures, ownership, control
flow, and lifecycle. Apply this proactively during implementation, review,
debugging, and cleanup; do not wait for the user to request each convergence.
Carry the change through its callers and tests, rather than merely renaming
an existing abstraction. The default response is: “Doom stores objects
differently? Let's store them the way Doom does.”

Our existing architecture is not a constraint to preserve: it is what we are
refactoring. Extra wrappers, pools, contexts, and parallel representations must
justify their existence against the reference. Do not defend a difference
merely because the current implementation depends on it, because this is an
RTS, or because the map is principally 2-D. First establish whether Doom's
approach can do the job. Depart from it only when verified game behavior or a
concrete requirement proves necessary, and document that reason. Preserve the
native-game fidelity and modern-rendering reference priorities stated above.

The replacement of the flat object array, effect pool, and per-action context
machinery is the model for future refactors. The excuse “Our flat object array
and effect pool still need storage that original Doom doesn't have” described
the structures to remove, not a reason to retain them. This rule applies to
every architectural difference encountered, not only object storage.

Use one global active level, Doom-named state/type tables, individually allocated
mobjs linked through the global `thinkercap`, and the shared thinker lifecycle.
Visual effects are ordinary mobjs, as Doom's puffs, blood, and missiles are;
there must be no separate effect array or effect lifetime system. Object
references must remain stable through other objects' creation/removal.
Actions take only `mobj_t *` and use shared level globals, with no context stack.
The map is principally 2-D here; that is not a reason to replace Doom's object
storage, spawning, thinker ticking, or deferred removal model.

### How the object/unit system maps to Doom

| Doom | open-rts |
|---|---|
| `state_t.action` — func ptr on the *state*, not the entity | `state_t.action` — same |
| `P_SetMobjState` chains zero-tic states immediately | `P_SetMobjState` does the same |
| `mobjinfo_t` with `spawnstate/seestate/missilestate/deathstate` | `mobjinfo_t` with identical fields |
| `mobj->tics` counts down per tick; on 0 → `nextstate` | `unit->tics` — same |
| Single `P_MobjThinker` drives all objects | `P_RunThinkers()` dispatches `P_MobjThinker` |
| Action functions fire **on state entry** (e.g. `A_Chase`, `A_PosAttack`) | `state_t.action` fires on entry in `P_SetMobjState` |
| Doubly-linked `thinker_t` ring (polymorphic: doors, lights, mobjs) | Global `thinkercap` ring with individually allocated objects and deferred removal |

The attack cooldown (`attack_cooldown_left_ms`) is the one remaining non-tic timer. It serves as
the rate-of-fire gate between attack cycles. In pure Doom style this would be encoded as a
recovery-state chain with specific tics. Until that refactor is done, `attack_cooldown_left_ms`
is decremented in ms and is set from `ActorType.attack_cooldown_ms`.

Attack *animation* locking is done via the state group: a unit in a `group == 3` state (attack
group) will not interrupt its animation to walk or start a new attack. No separate
`attack_anim_left_ms` ms-timer is needed or used in the state-machine path.

### Current state/action contract

`state_t` is intentionally compact: sprite, frame, tics, action, next state,
and a gameplay `group`. FIN layers own rendering flags, remap, and intensity;
FIN `remap` is a native drawing mode, not a team-color ID. Choose palette
translations from the object's team, independently of animation commands;
do not reintroduce state rendering overrides or generic `misc` fields.
Visible Dark Colony states must select complete FIN frames. The generic blood
effect remains a legacy exception until native per-type BLOODA–G dispatch is ported
(see `docs/DC_EXE_FINDINGS.md`, “Native BLOOD dispatch and shared effect sprites”);
`test_drop_fin_states` checks this across the entire state table.
Directional frame arrays, overlay state
fields, and `state_userdata` are not part of the runtime contract. Basic
sprite/frame selection is the current presentation target; do not reintroduce
parallel directional or overlay metadata without verified game evidence.

World state actions use the Doom/Hexen-style `void action(mobj_t *actor)`
signature and shared level globals. Do not introduce a context struct, getter,
stack, or per-action save/restore. Spawning initializes an unlinked object's
state without invoking its action, as in Doom.

Hexen's psprite actions are a separate family: weapon actions receive
`player_t *` and `pspdef_t *` because they operate on the player's view weapon,
not a world mobj. Do not use that signature for ordinary actor or dropship
states.

### Level-owned mission state

The active `level_t` owns game-specific mission state through its opaque
`mission` pointer and `destroy_mission` callback. The game implementation
attaches mission state during `G_DoLoadLevel()`. `G_MissionTicker(level_t *map,
...)` resolves the mission from the active level, and `P_FreeLevel()` releases
it. Drivers and `RtsGameModel` must not keep a parallel `void *mission` owner or
pass mission objects separately through the simulation.

Dark Colony dropships are ordinary allocated `mobj_t` objects in `thinkercap`. Their FIN states advance through `P_Ticker` and their `A_` handlers move
and release cargo. There is no separate dropship pool, ticker, or effect bridge.
Per-game `mobj_data.h` supplies typed inline fields through `MOBJ_GAME_FIELDS`;
Dark Colony owns delivery payloads there. Objects remain at stable addresses
until deferred thinker removal frees them. Temporary UI/render pointer lists
and exported snapshots are borrowed views, never simulation storage.

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
- **Sprite directions**: load every FIN frame and use each sequence's authored
  8/16 facings. Do not merge actions, discard singleton directions, or branch
  on unit names in the sprite loader. State/action code chooses animations;
  the loader only builds frame/rotation definitions from the assets.
- **Exploiter walk cycle**: EXPL.FIN's even MOVE ranges contain two temporal
  frames and its odd MOVE ranges contain one. RUN1/RUN2 does not imply fourteen
  missing temporal frames. STAND and SHUF are separate native label sets;
  their original selection path remains unknown (see `docs/DC_EXE_FINDINGS.md`).
- **Reaper animation after regeneration**: after regenerating `info.c` or `info.h`, always restore
  and verify the native Reaper movement timing `{4, 3, 3, 4, 1, 3, 3, 1}` for `S_REAP_RUN1`
  through `S_REAP_RUN8`; run `build/bin/test_dark_colony_sprite_layout` before finishing.
- **Exploiter deploy orientation**: when ordered to harvest, the unit first rotates to face
  south-east (code 6), then plays the DEPLOY1-20 animation. Code already does this; preserve it.
- **Exploiter work (harvesting) animation**: the WORK1-15 data (frames 25-33) was incorrectly
  interpreted as a probe-arm animation. WORK states show only the static deployed body
  (SPR cell 34 through FIN frame 53, the final `EDPLYSTAND14` frame). The original
  game appears to use a pulsating light during harvesting, but its native
  implementation is still unknown; preserve that unknown and
  investigate it in DC.EXE before adding a spawned visual effect.

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

## Reverse engineering

Follow [`REVERSE_ENGINEERING.md`](REVERSE_ENGINEERING.md) when deriving behavior
or metadata from original game executables. It defines the shared Dark Colony
and 7th Legion workflow, verification requirements, and game-specific fidelity
rules.

### No unverified magic-number fixes

Never add a numeric offset, scale, delay, threshold, or other magic constant as
a visual or behavioral compensation unless evidence confirms that value is
present in the retail game. Derive values from native assets or runtime data
when possible; otherwise trace and document the executable instruction, data
offset, or observed retail behavior that justifies the constant. If the value
is still unknown, preserve the unknown and investigate it instead of tuning a
number until the output looks right.

### Preserve every original-game finding

Do not leave reverse-engineering knowledge only in chat, terminal output,
ignored `reverse/` files, or local screenshots. Before finishing any task that
examines an original executable, native asset, runtime trace, or original-game
screenshot, document every useful result, including disproven hypotheses and
remaining unknowns.

- Put detailed, game-specific executable findings in
  `docs/<GAME>_EXE_FINDINGS.md` (for example, `docs/DC_EXE_FINDINGS.md`).
- Put external source URLs and provenance in `REFERENCES.md`; update
  `REVERSE_ENGINEERING.md` when the reusable workflow changes.
- Record the executable fingerprint, function addresses or data offsets,
  observed layout or formula, evidence chain, implementation consequence, and
  a command or focused test that can reproduce the result.
- Distinguish **confirmed**, **inferred**, **disproven**, and **unknown** facts.
  Never silently turn a visual guess or decompiler type guess into a rule.
- Keep superseded conclusions with a short correction when they explain why a
  tempting workaround is wrong. This prevents later work from repeating the
  same investigation.

## Debugging

Always add temporary diagnostic logging (`printf` / `fprintf(stderr, ...)`) when
investigating a bug or unexpected behavior.  Print the values of key variables
at the point of interest so the problem can be diagnosed from terminal output
alone — do not rely on the user running a debugger or staring at screenshots.

Gate behind a compile-time define (`#define DEBUG_FOO` at the top of the
file) or env-var check (`getenv("OPEN_RTS_DEBUG_...")`), and remove the
logging before committing the fix.

## Git workflow

- Commit all completed working-tree changes after verification so progress stays
  easy to inspect and bisect.
- Write a clear imperative subject and a detailed commit body describing the
  behavior changed, the reason for the change, and the verification performed.
- Do not push commits unless the user explicitly asks for a push.
