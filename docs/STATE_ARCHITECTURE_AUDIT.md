# Actor lifecycle comparison: Hexen, Strife, and Dark Colony

Audit date: 2026-09-07. Scope: actor spawning, state entry/ticking/removal,
Dark Colony reinforcement ships, and adjacent effect/production lifetimes.
This is not a claim of full game or network equivalence.

**Superseded dropship implementation (2026-09-09):** ships now occupy the ordinary
object array, with inline game-specific cargo and native FIN frame states.
`A_DC_Fly` and `A_DC_Drop` replace the private pool, phase clocks, and part effects.
See [the current FIN/object audit](DC_EXE_FINDINGS.md#drop-fin-sequences-and-ordinary-objects-2026-09-09).
The historical comparison below is not the current dropship ownership contract.

## Source evidence

The local reference paths and SHA-256 fingerprints are recorded in
`REFERENCES.md`. Relevant functions:

| Contract | Hexen `P_MOBJ.C` | Strife `strife/p_mobj.c` | open-rts |
| --- | --- | --- | --- |
| `S_NULL` removes an actor | `P_SetMobjState`, line 87 | `P_SetMobjState`, line 67 | `P_SetMobjState` marks `remove`; it is not a temporary state |
| Actions run upon entry | `P_SetMobjState`, line 99 | `P_SetMobjState`, line 80 | Same; branch in the entered action |
| Zero-tic chaining | `P_MobjThinker`, line 1148 | `P_SetMobjState`, line 84 | Ticker chaining follows Hexen; setter enters one state |
| `-1` tics persist | `P_MobjThinker`, line 1144 | `P_MobjThinker`, line 677 | `P_TickMobjState` preserves negative tics |
| Spawn does not run actions | `P_SpawnMobj`, line 1190 | `P_SpawnMobj`, line 768 | Corrected to initialize state and visuals directly |
| Movement precedes state advancement | `P_MobjThinker` | `P_MobjThinker` | Ship tick moves, ticks the common state machine, then updates presentation |

**Correction to the attached review:** Hexen's setter does not itself loop
through zero-tic states. Strife's setter does loop, so the two references do
not agree on this detail. open-rts now follows Hexen: `P_SetMobjState` enters
one state and invokes its action, while `P_TickMobjState` owns the zero-tic
loop. A nested action redirect is honored by deriving each next state from the
currently entered state, never from a stale caller-side state.

## Implemented corrections

- The ship chain is APPROACH -> UNLOAD -> REPOSITION -> UNLOAD. REPOSITION's
  entry action releases the pending passenger, selects the next location, or
  redirects to DEPART when the manifest is exhausted. Only DEPART ends at
  `S_NULL`. No code repairs state transitions after the common ticker runs.
- Removal immediately clears ship parts and returns. Previously cleanup fell
  through into part synchronization, which could recreate the removed visuals.
- The setter refuses removed actors just as the ticker does; returning success
  while running an action on a permanently removed actor was inconsistent.
- Ship data lives in a local `Dropship` containing an `mobj_t` first member.
  Common state/movement functions accept that member, while ship actions
  recover the owning struct. These actions must only be used by this actor
  allocation. There are no new opaque callbacks or casts in the engine.
  Hexen's typed special thinkers similarly embed a common thinker first;
  its `special1`/`special2` scalar scratch fields are not a suitable untyped
  home for a manifest and 24 visual slots.
- The actor's fixed-point position and movement goal are the only position
  and goal storage. Removed the parallel center/start/target copies. Planar
  movement preserves altitude, and FIN parts copy the whole position.
- Millisecond/tic conversions use `RTS_TICRATE`, also used by `FIXED_DT`.
  Previously FIN elapsed time divided remaining tics by the *50-tic flight
  duration*, although the simulation advances at 30 Hz. This is a clock
  conversion fix, not a new retail animation delay.
- Test builds include their generated header dependencies. Without those,
  changing `mobj_t` rebuilt only directly edited test sources, leaving other
  objects with incompatible layouts and misleading runtime results. The default
  make goal is explicitly `all`, so included dependency targets cannot select
  only the first game on subsequent builds.
- The state generator emits the same live phase links as the checked-in table;
  the layout test no longer asserts the broken `S_NULL` links.
- The shared state setter/ticker now matches Hexen's division of
  responsibility: setter entry is single-state, and the thinker consumes
  zero-tic chains in the same tick.

## Remaining differences and limits

- Visual-only `effect_t` objects have no gameplay entry actions and retain a
  separate presentation lifetime. They are not a second gameplay thinker.
  Adding gameplay behavior to them would require a real actor lifetime.
- Production release still uses a millisecond countdown derived from a state
  chain. Harvesting and attack cooldowns also retain millisecond gates. These
  remain architectural differences, not evidence that Hexen used such gates.
- Positions/momenta use fixed-point storage, but movement/pathing includes float
  math, and combat uses `rand()`. This audit does not establish deterministic
  lock-step simulation. The RTS's 30 Hz rate is not Hexen's original ticrate.
- Ship flight duration/formation and absolute altitude remain existing policy;
  this audit does not establish their retail correctness. In particular, the
  attached 70-pixel suggestion is not executable-derived evidence.
- The model snapshot exposes planar positions, so the integration test checks
  deliveries and visual removal, not absolute screen-space flight height.

## Reproduction and verification

The focused Human01 integration test fails against the original actor/ship
implementation with header dependencies enabled. Temporary diagnostics showed:

```
dropship 279 -> 280 tics=30 removed=0 released=0
dropship 280 -> 0 tics=0 removed=1 released=0
seen=1 delivered=0 departed=0
```

The repaired implementation completes delivery and removes the ship visuals.
Diagnostics used for this investigation were removed. Focused commands:

```
make
make tags
make build/bin/tests/dark-colony/test_dropship build/bin/tests/dark-colony/test_mobj_states
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_dropship
SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_mobj_states
SDL_VIDEODRIVER=dummy make test-layout
SDL_VIDEODRIVER=dummy build/bin/dark-colony --check
```

The shared-state test covers spawn without actions, zero-tic chaining and
nested redirects, action invocation counts, persistent states, terminal
removal, and refusal to re-enter removed actors. The layout test also verifies
Reaper's preserved `{4,3,3,4,1,3,3,1}` movement timing.

Build and all four game smoke checks passed. The Dark Colony headless BMP was
converted to PNG and visually inspected: terrain, HUD, and the initial ship
render successfully. This startup capture does not validate retail altitude.

The full suite has three independently reproduced baseline failures:
`test_game_model_headless` expects full asset paths in the now-short sprite
catalog; `test_combat_and_harvest` cannot find the expected player Exploiter;
`test_model_commands_dark-colony` cannot accept an available build command.
The latter two were also run from a clean archive of the unchanged revision,
with the same local data. They are not treated as passing checks or repaired by
weakening their assertions. The focused lifecycle and layout checks pass.


## Shared object allocation (2026-09-09)

The local Doom source allocates and clears an object with
`Z_Malloc(sizeof(*mobj), PU_LEVEL, NULL)` inside `P_SpawnMobj`
(`reference/DOOM/p_mobj.c:480–532`), then registers its thinker.
`P_RemoveMobj` unlinks world references and calls `P_RemoveThinker`
(`p_mobj.c:546–572`). The latter marks the thinker; `P_RunThinkers` later
unlinks it and calls `Z_Free` (`reference/DOOM/p_tick.c:80–113`). Death alone
is not removal: a corpse with a persistent state still owns its storage.

open-rts keeps its compact array rather than adopting Doom's heap/list storage.
`P_AllocMobj` is the shared runtime allocation entry point for dropships,
reinforcements, and both production paths. It checks capacity and clears the
next unused slot. `P_Ticker` compacts objects marked `remove` after state
actions finish, making their slots available in the unused tail. Searching for
`hp <= 0` or overwriting pending removals inside the allocator would invalidate
corpse state or pointers still held by running actions. Full capacity therefore
remains a normal allocation failure until compaction runs.

`tests/dark-colony/test_mobj_allocation.c` fills the array and verifies that
persistent corpses and unfinished death animations survive, pending removals
are deferred, completed deaths release capacity, and reused storage is zeroed.
The cargo-ownership and Human01 delivery tests also pass with shared allocation.
Run these tests with `SDL_VIDEODRIVER=dummy`.
