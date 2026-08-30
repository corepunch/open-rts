# Plan 009 — Sprite Direction & Animation Unification

Tracking the work needed to give every plugin a single, consistent path
from simulation facing → sprite frame, with no per-game branches in
shared code.

---

## Status: foundation merged, two steps remain

### Done (merged in this branch)

| What | Files |
|------|-------|
| `facing_t` (uint16_t, N=0 CW compass) | `common/facing.h` |
| `facing_scheme_t` — per-format layout as plain data | `common/facing.h` |
| `facing_from_vector(dx,dy)` — canonical vector→angle | `common/facing.h` |
| `facing_to_index(f, scheme)` — generic N-way bucketing | `common/facing.c` |
| Named scheme constants: `compass16`, `dc8`, `dc16`, `dr16` | `common/facing.c` |
| Refactored `engine_units.c` per-mode functions to use the above | `common/engine_units.c` |
| KKnD map loader sets `direction_mode = RTS_DIRECTION_DARK_REIGN_8` | `games/KKND/kknd_loader.c` |

**What the KKnD fix resolves:** KKnD's MOBD sprites store 16 facings as
direction codes 0–15 (north=0, CW). Without an explicit `direction_mode`
the engine fell through to `compass16`, which returns only even codes
0,2,4…14.  `sequence_facing_index` does a nearest-neighbour search in
`direction_codes[]`, so odd-numbered facings (1,3,5…15) were never
selected — the infantry was missing its 8 diagonal facings.

---

## Step 1 — Lift `Unit.facing_code` to canonical `facing_t`

**Current state:** `Unit.facing_code` and `VisualEffect.facing_code` hold
a plugin-native integer — already converted from the movement vector using
`direction_code_from_vector`.  Render-side code (`sprite_frame_for_unit`,
`sequence_facing_index`, `direction_slot_for_view`) re-interprets this
integer as an index into the plugin's own `direction_codes[]` arrays.

**Goal:** store `facing_t` (canonical compass angle) in simulation state;
convert to a plugin-native index at render time only.

### Why it matters

- Simulation code (`engine_units.c`, `server/`) currently calls
  `direction_code_from_vector` and writes a plugin-native code; if two
  plugins with different `count` are ever active in the same session the
  simulation layer would need a branch.
- `facing_t` values can be safely serialised, replayed, and lerped for
  smooth visual rotation — a native code cannot.

### Migration sketch

```c
// server/game_model.h
typedef struct Unit {
    // Before: int facing_code;
    facing_t facing;  /* canonical compass, N=0 CW */
    ...
} Unit;
```

```c
// common/engine_units.c — wherever facing_code is written today:
unit->facing = facing_from_vector(dx, dy);   /* replaces direction_code_from_vector */

// client/engine_view.c — wherever direction_code is read for rendering:
// Dark Colony (state-driven):
int code = facing_to_index(unit->facing, &dc16_facing_scheme);
// then pass 'code' to state_facing_slot() as before

// KKnD / Dark Reign (sequence-driven):
int slot = facing_to_index(unit->facing, &dr16_facing_scheme);
// index directly into sequence->facing_frames[slot]
```

### Callsites to update

Search for `facing_code` in:
- `server/game_model.h` — field declaration
- `server/game_model.c` — spawn, copy, network serialisation
- `common/engine_units.c` — all writes (movement, spawning, `set_unit_state`)
- `client/engine_view.c` — `sprite_frame_for_unit`, `direction_slot_for_view`,
  `render_unit_state_overlay`
- `tests/` — any assertions that compare facing integers directly

`direction_code_from_vector` and `direction_vector_from_code` can be
removed once no callsite remains; `DirectionMode` on `GameInfo`/`GameMap`
can be removed once `facing_scheme_t` is the only per-plugin configuration.

---

## Step 2 — `resolve_frame` as a plugin-supplied function pointer

**Current state:** `unit_frame_for_view` in `client/engine_view.c` branches
on the game type to pick either the Doom-style `states[]` path (Dark Colony)
or the `SpriteSequence` path (KKnD, Dark Reign).

**Goal:** replace the branch with a `resolve_frame` function pointer on a
`Renderable` trait struct, so `engine_view.c` calls one path for every
plugin.

```c
/* common/actor.h — add to the RenderableTraits or a new RendererHooks struct */
typedef struct {
    /* Given a unit and its current canonical facing, return the sprite frame
     * to draw this tick.  Plugin-supplied; shared code never interprets the
     * internal layout. */
    const SpriteFrame *(*resolve_frame)(const Unit *u, facing_t facing, int tick);
} RenderableHooks;
```

Dark Colony's implementation walks `states[]`/`mobjinfo[]` using
`facing_to_index(facing, &dc16_facing_scheme)`.  KKnD's implementation
walks `SpriteSequence` using `facing_to_index(facing, &dr16_facing_scheme)`.
Shared render code calls `hooks->resolve_frame(unit, unit->facing, tick)`.

This step depends on Step 1 (canonical `facing_t` in `Unit`).

---

## Step 3 — Confirm Dark Reign's facing scheme

**Current state:** Dark Reign uses `RTS_DIRECTION_DARK_REIGN_8` which maps
to `dr16_facing_scheme` (north=0, CW, 16-way).  The name is a historical
misnomer — it handles 16 facings, not 8.

**To do:**
1. Cross-check against the OpenDR reference sprites at
   `data/REIGN/dark/scenario/FIXED/M01F/` — spawn a unit, rotate it in
   16 increments, compare the displayed facing against the expected compass
   direction.
2. Confirm that actor sprites (as opposed to effects/projectiles) follow
   the same convention — or document any exceptions.
3. If confirmed, rename `RTS_DIRECTION_DARK_REIGN_8` →
   `RTS_DIRECTION_DR_16` (or just remove it once Step 2 replaces the
   `DirectionMode` dispatch entirely).

---

## Step 4 — Animation timing shared across plugins

Dark Colony's `State` table already provides `{sprite, frame, tics, action,
nextstate}` per animation state.  KKnD and Dark Reign use `SpriteSequence`
with a flat frame list and no per-state timing.

A shared `anim_state_t` (sequence id + frame index + ticks + next) could
unify the two, letting `engine_units.c` own one animation tick loop instead
of each plugin reimplementing it.  This is lower priority than Steps 1–3;
document here for future reference.

```c
/* Sketch — common/anim.h */
typedef struct {
    int anim_id;       /* named sequence: stand=0, walk=1, attack=2, die=3 */
    int frame_in_seq;
    int ticks_total;
    int next_state;    /* -1 = stop, self-index = loop */
} anim_state_t;
```

---

## Networking note

The project uses an AoE-style command/simulation split (deterministic
lockstep, per `REFERENCES.md`).  `facing_t` is safe to send over the wire
as-is — it is a uint16 derived purely from the unit's movement command,
and any two clients running the same tick sequence with the same seed will
agree on it.  No additional networking work is needed for Steps 1–3.
