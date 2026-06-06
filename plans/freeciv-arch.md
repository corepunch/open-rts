# Freeciv-inspired architecture improvements

Stolen ideas from https://github.com/freeciv/freeciv after comparing implementations.
Ordered by effort vs. payoff. Each phase is independent and shippable on its own.

---

## Phase 1 — Terrain movement costs (half a day)

**What freeciv does:** `common/aicore/path_finding.h` — `struct pf_parameter` with a
`tile_behavior()` callback and per-tile move cost. `pft_fill_unit_parameter()` populates
costs from unit type automatically.

**What we do now:** `engine_path.c` — binary walkable/blocked, every passable tile costs 1.

**Plan:**

1. Add `movement_cost` (uint8, default 1) to `Tileset` or a new `TerrainType` table keyed
   by tileset name. Plugins set this in `load_assets`.
2. In `engine_path.c` `a_star()`, replace the hardcoded `g + 1` step cost with
   `g + movement_cost_for_tile(map, nx, ny)`.
3. Add a `movement_cost` override field to `RtsActorType` (e.g., wheeled units ignore
   forest penalty).
4. Dark Reign: map SNOW/WATER tile ranges to cost values based on the `.TIL` mask flags
   already decoded in `dr_loader.c`.

**Files touched:** `engine_path.c`, `engine.h` (TerrainType or Tileset fields),
`plugins/DarkReign/dr_loader.c`, `plugins/DarkColony/dc_loader.c`.

---

## Phase 2 — Order queue (half a day)

**What freeciv does:** `common/unit.h` — `struct unit_order orders[MAX_LEN_ROUTE]` with
`enum unit_orders` (MOVE, ACTIVITY, PERFORM_ACTION) and a target. Decouples player
intent from the state-machine tick.

**What we do now:** units have one goal at a time; right-click sets `move_goal_gx/gy`.
Chaining (move → attack → harvest) requires external orchestration.

**Plan:**

1. Add to `Unit` in `engine.h`:
   ```c
   #define MAX_UNIT_ORDERS 8
   typedef struct { uint8_t type; int16_t target_unit; float gx, gy; } UnitOrder;
   UnitOrder orders[MAX_UNIT_ORDERS];
   int order_count, order_index;
   ```
2. Order types: `ORDER_MOVE`, `ORDER_ATTACK`, `ORDER_HARVEST`, `ORDER_STOP`.
3. In `engine_units.c` `update_units()`, when a unit finishes its current goal (arrived,
   kill confirmed, harvest done), pop the next order instead of going idle.
4. Right-click with Shift appends an order; without Shift replaces the queue (matches
   standard RTS UX).
5. Plugin mission scripts use order queues to script scripted unit behaviour.

**Files touched:** `engine.h`, `engine_units.c`, `src/main.c` (input → order dispatch).

---

## Phase 3 — Combat modifiers + light RNG (half a day)

**What freeciv does:** `common/combat.h` — `win_chance(as, ahp, afp, ds, dhp, dfp)`,
`get_modified_firepower()` applies terrain and unit-class flags, veterancy on `struct unit`.

**What we do now:** `engine_units.c:~940` — `target->hp -= attacker->attack_damage`, flat
and deterministic.

**Plan:**

1. Add to `RtsActorType`: `float attack_bonus` and `float defense_bonus` (both default 1.0).
2. Replace flat damage:
   ```c
   float dmg = attacker->attack_damage
               * attacker_type->attack_bonus
               * (1.0f - defender_type->defense_bonus)
               * (0.85f + rng_float() * 0.30f);   // ±15% variance
   target->hp -= (int)fmaxf(1.0f, dmg);
   ```
3. Add `uint8_t veteran` to `Unit` (0–2). Increment on kill. Apply `1.0 + veteran * 0.1`
   multiplier to attack and defense.
4. Terrain defense bonus: look up `movement_cost` from Phase 1; cost > 1 gives a small
   defense multiplier (e.g., forest tile → +20% defense).
5. Feed `attack_bonus`/`defense_bonus` from `GAMESTAT.TXT`/`WEAPSTAT.TXT` for Dark Colony
   in `dc_loader.c`.

**Files touched:** `engine.h`, `engine_units.c`, `plugin.h` (RtsActorType fields),
`plugins/DarkColony/dc_loader.c`, `plugins/DarkReign/plugin.c`.

---

## Phase 4 — Fog of war (one week)

**What freeciv does:** `common/vision.h` — `struct vision` with `radius_sq` per unit,
three layers (normal/stealth/subsurface), per-player `known` tile bitset, atomic
fog/unfog on unit movement. Client renders tiles as unseen/remembered/current.

**What we do now:** nothing — all units and tiles always visible.

**Plan:**

1. Add to `GameMap` in `engine.h`:
   ```c
   uint8_t *tile_visible;    // 1 bit per tile, current visibility
   uint8_t *tile_explored;   // 1 bit per tile, ever seen
   ```
2. Add `int sight_range` to `RtsActorType` (tiles, not pixels). Default 5.
3. In `engine_units.c`, after every unit position update call
   `update_fog(map, unit)`: clear old visible circle, set new visible circle,
   mark newly visible tiles as explored.
4. In `render_map()` (`engine_view.c`): skip rendering tiles where
   `!tile_visible` and `!tile_explored`. Draw explored-but-not-currently-visible
   tiles at ~40% brightness (darken by modulating SDL texture color mod before
   the blit).
5. In `render_world_objects()`: skip enemy units on non-visible tiles.
   Own units always render.
6. Minimap (Dark Colony): dim unexplored cells; show explored cells at half
   brightness.
7. Plugin hook `on_tile_revealed(map, x, y)` for mission scripting.

**Files touched:** `engine.h`, `engine_units.c`, `engine_view.c`, `engine_core.c`
(map alloc/free), `plugins/DarkColony/plugin.c` (minimap, mission hooks).

---

## Not worth stealing from freeciv

- **Sparse tile structs** (`struct tile*` pointer graph) — designed for freeciv's
  non-rectangular world map and network sync. Our flat `uint16_t tile_ids[]` array
  is faster for a fixed-size RTS grid and should stay.
- **Turn-based action validation** (`unit_attack_unit_at_tile_result()`) — freeciv's
  legal-move checker is turn-based and city-aware. Our real-time combat loop has
  different constraints.
- **Ruleset loader** (Lua/spec files) — heavyweight. Our plugin `.so` system already
  provides per-game data injection.
