# One pathfinder to rule them all? An essay on unifying movement in open-rts

*2026-09-30. Research only: no engine code was changed. Every claim is tagged
**[verified]** (read in this repo, in the DR decompile, or in the OpenKKND
decompile), **[inferred]** (follows from verified facts), or **[memory]**
(recalled from general knowledge, not checked against source here).*

---

## 0. Verdict up front

1. **Yes, a shared navigation core is worth building, but not a single
   algorithm.** What the four games share is a *grid, an 8-way neighbourhood,
   per-mover passability, dynamic occupancy and a goal-rooted search*. What they
   differ on is how a route is *consumed* (cell hops vs. free steering) and how
   crowds are resolved. Put the shared part in `play/nav/`, put the differences
   in a per-game **profile** struct, and keep DC's bit-exact port as one
   registered backend/policy, not as "the" algorithm.
2. **The best single idea for this codebase: DC's search already *is* a
   partial flow field.** `DC_FindPath` runs backwards from the goal and stops
   when it reaches the start. Let it run to exhaustion and you have a flow field.
   So "A\* for one unit" and "flow field for a group" are the same goal-rooted
   Dijkstra with different stopping rules. One engine, two modes.
3. **A correction about StarCraft:** StarCraft does *not* use flow fields
   **[memory]**. SC1 uses a region/chokepoint graph computed at map load, A\*
   over regions, then local steering with physics. Flow fields were popularised
   later (Supreme Commander 2, Planetary Annihilation, the "flow field tiles"
   chapter in *Game AI Pro*) **[memory]**. Your *observation* is right, though:
   SC units move at arbitrary headings. That comes from the locomotion layer
   (256-step facing, turn rate, acceleration), not from the planner.
4. **The "ONLY_45_DEGREES" flag is really three flags** living in three layers
   (planner, executor, sprites). Section 6 explains which. It is cheap for the
   planner, moderate for the executor, and a non-issue for sprites.
5. **Do not make DR or KKnD chase retail bug-compatibility** with the evidence
   we have. For DR we have genuinely good evidence (Section 3.2); for KKnD we
   have almost none for the *planner* (Section 3.3).

---

## 1. What the engine does today

There are **two unrelated movement systems** selected by `#ifdef
RTS_GAME_DARK_COLONY`:

### 1.1 Dark Colony: a native port of `DC.EXE path.c` **[verified]**

`games/dark-colony/p_path.c` (363 lines), ported from disassembly:

| Property | Detail |
| --- | --- |
| Search direction | **Backwards**, goal to start (`0x440dc4`) |
| Queue | 256-slot circular **bucket (Dial) queue**; records are 0x18 bytes |
| Neighbours | 8, corner-cutting blocked unless an adjacent orthogonal is open |
| Cost | Not octile. A **9x9 table indexed by the direction from the current cell to the start**, so it is a directional bias: it steers the search toward the start, so it behaves like A\* without a separate heuristic |
| Regions | `PTH` file: a 256x256 **family graph** plus a per-cell family byte. The search only expands cells whose family lies on the family chain, which prunes whole areas |
| Route | At most **32 cells**; when consumed, the unit re-searches from where it stands |
| Crowds | Occupancy is per *cell*: a unit owns the cell it is stepping into. Blocked route cell leads to a bounded 256-pop local detour, or a `random()%3-1` perturbation of the destination |
| Air | No search: diagonal first, then the remaining axis |
| Executor | `DC_MoveTarget` yields the next cell centre; `tick_actor` (`play/p_mobj.c:1138`) splits any off-axis segment into a diagonal run plus an axial run. Travel is **strictly 8-directional** |

### 1.2 Everyone else: a generic Dijkstra "flow field" **[verified]**

`play/p_map.c`:

- `build_flow_field` is a full-map Dijkstra, **cost 10/14**, 8-connected with the
  same corner rule. Its open list is **an unsorted array scanned linearly for the
  minimum on every pop** (`p_map.c:394`), so it is O(N^2) on a fully open map.
  Fine for 64x64; it will hurt on 256x256 with several goals.
- Fields are cached **per goal cell** in a linked list on the level
  (`level.flow_fields`). The only `P_FreeFlowFields` call outside tests is level
  setup (`play/p_setup.c:11`), so I found **no invalidation when a building is
  placed or destroyed**. That is either a latent bug or handled by something I
  did not find; it needs checking.
- `P_FlowFieldTarget` walks downhill from the unit's cell as far as
  `line_walkable` (a circle swept along the segment) allows. That is a crude
  **string-pull**, so the unit always heads for the farthest visible cell.
- The executor steers to that point at any angle, limited by `turn_step`, and
  on a blocked step **slides along axes** (`move_unit_if_walkable`).
- Passability is one byte per cell (`level.blocked[]`). There is **no per-mover
  class**, no terrain speed, and no slope.
- Unit-vs-unit avoidance is nearly absent: reserved formation goals and docked
  harvesters only (`P_TryMove`, `yield_harvest_bay`).

So DR, KKnD and 7th Legion currently move like a generic prototype: smooth
headings, correct-ish routes, no crowd model, no terrain cost. DC moves like the
retail game.

---

## 2. The three layers every RTS movement system has

It helps to stop arguing about "pathfinding" and separate:

```text
A. PLANNING    where to go        goal-rooted search over a nav grid       -> route or field
B. LOCOMOTION  how to travel it   cell hops, or heading+turn rate+accel    -> position per tick
C. CROWD       who gets there     cell claims, or radius collision+push    -> resolved positions
```

| | A. Planning | B. Locomotion | C. Crowd |
| --- | --- | --- | --- |
| **DC** | bucket search + PTH families | cell hops, 8 headings | cell claim + detour + random nudge |
| **DR** | A\* on binary heap (Section 3.2) | continuous (unconfirmed) | blocker rules in the search itself |
| **KKnD** | unknown | pixel position, **8/16 headings** | unknown |
| **StarCraft** **[memory]** | regions + A\* | 256 headings, turn rate, accel | physics pushing + "bump" repath |
| **open-rts now (non-DC)** | O(N^2) Dijkstra flow field | continuous + string-pull | almost none |

Only layer A is truly shareable across all of them, and it is shareable because
all four games run on a tile grid with 8 neighbours.

---

## 3. What the original games actually do

### 3.1 Dark Colony (`DC.EXE`) **[verified]**

Covered in 1.1. The interesting lesson for unification: **DC's "cost table" is
an A\*-ish heuristic baked into edge costs**, and the PTH family graph is a
*precomputed hierarchical abstraction*. Both are optimisations a generic engine
can reproduce (heuristic, flood-fill regions) without copying the exact bytes.

### 3.2 Dark Reign (`dkreign.exe`) **[verified, decompile in `reverse/dr-hud/dkreign.c`]**

The binary embeds source file names `C:\WinTactics\Path.c` and
`C:\WinTactics\Pathsrch.c`. From their assertion strings and the decompiled
`ContinueSearch` (`0x428xxx`, `0x429150`, `0x42a2e0`):

| Finding | Evidence |
| --- | --- |
| **8-connected** | 8-entry (dx,dy) table at `0x5a7328`: N, NE, E, SE, S, SW, W, NW (dumped from the exe) |
| **A\*** | Cells carry `g` and `f`; a **binary heap priority queue** keyed on `f` (`PQ_Put`, `0x428b10`); string `(gmin,gmax)=..., (fmin,fmax)=...` and four global min/max trackers updated as cells are touched |
| **6-byte path cells** | `pathsrch.pathcell` is per-row arrays of 6-byte cells |
| **Lazy clearing** | `pathsrch.rowmark`: a per-*row* stamp byte, incremented per search; the row is only zeroed when the stamp wraps. Cheaper than DC's per-cell stamp |
| **Time-sliced** | `Pathsrch_ContinueSearch`: a per-team ring of pending requests (stride `0x1f50`, 1000 entries) and a work budget per call; "Search queue filled - make it bigger" |
| **Two search phases** | `search type` 0 and 1, dispatched to `0x429150` and `0x42a2e0` (expansion, then path trace); "Illegal search type" otherwise |
| **Stored as tile steps** | "Path > %d tiles aborted", "Path circularity detected", "Trace: Travelled off path", "tried to walk off the map following path" |
| **Dynamic blockers in the search** | Neighbour acceptance calls helpers that look up the mobile occupying the tile (`0x499d00`) and only treat it as passable under team/order/state conditions (same team, unit idle, moving out of the way, etc.) |
| **Terrain classes with speed and slope** | `deftxt/TRNEFF.TXT`: `DefineEffectType(Wheel 100 2)`, `Track`, `Foot 100 3`, `Hover`, `Flying`, `LeggedDroid 100 3 ;; Percent speed, max slope`, `Fixed 0 0`. Each terrain index has a speed percentage per class (25, 100, 200%), and 0 means impassable |

**What this means for design:** DR needs a **per-movement-class cost/speed
grid**, not a bool. The data is already in the game files. Cost of entering a
cell should be `base / speed_percent`, which an A\* or goal-rooted Dijkstra
handles trivially. `level.blocked[]` (one byte) cannot express it, so the nav
grid needs a *cost layer per class*.

**Not yet known:** how DR's executor consumes the tile-step list (continuous
interpolation or cell hops?), the exact heuristic (`0x4275c0` ends in an `ftol`,
so it is floating-point: likely Euclidean or octile), and the crowd rules. Those
need another disassembly pass before claiming parity.

### 3.3 KKnD (`KKND.EXE`, LE/DOS4GW) **[partly verified]**

- The binary has no path-related strings at all (no source file names), unlike DR.
- In the OpenKKND decompile (`reference/OpenKKND/src`), `entity_move` only
  stores a destination in pixel coordinates (`sprite_x_2/y_2`, snapped by
  `entity_414440_boxd`) and switches to `entity_mode_move_attack`. **The
  pathfinder itself is not in what I read**; I did not locate it.
- What *is* clear: positions are **sub-tile fixed point** (`>>8` when comparing
  distances), and headings are quantised to **8 directions for infantry**
  (`0x40D600`, multiples of 32 out of 256) **and 16 for vehicles**
  (`0x40D6F0`, multiples of 16; thresholds use `618/256 = tan 67.5°`).
- So KKnD sits between DC and StarCraft: a tile grid for obstacles, continuous
  position, and an 8/16-facing locomotion model. That suggests "smooth-ish with
  quantised headings", which a profile should be able to express
  (`heading_quantum`), but I cannot say whether it steers along the quantised
  heading or merely draws it.

### 3.4 StarCraft (reference for the "fluid" feel) **[memory]**

- Planner: walkability at 8x8 px mini-tiles; the map is pre-split into
  **regions** joined by chokepoints; a unit A\*s over the region graph, then
  walks region to region. No flow fields.
- Locomotion: 256 facings, per-unit turn rate, acceleration/braking, sprites with
  16 (mirrored to 32) facings. That is the "fluid" look.
- Crowd: units have a radius; moving units push idle ones; blocked units repath
  or wait. Workers mining are a special case (they ignore each other while
  gathering, which reads as smoothness) **[memory, low confidence]**.

The key transferable idea is **hierarchy (regions)**, and DC's PTH families are
exactly that. A generic engine can compute regions by flood-fill at level load
instead of reading a PTH, and use them to reject unreachable goals in O(1) and
to relocate unreachable goals to the nearest reachable cell (DC's
`DC_MoveUnitTo` already does that with an expanding square search).

---

## 4. Do flow fields fit?

**Yes for groups, no as the only mechanism.**

| Situation | Best tool | Why |
| --- | --- | --- |
| 1 unit, far goal | goal-rooted search with **early exit** | DC/DR do this; no need to touch the whole map |
| 12 units ordered to one point | **full field** from that goal | One search amortised over 12 units; each follows the gradient, which naturally spreads them |
| Harvester shuttling mine to refinery all game | **cached field per (class, goal, version)** | Reuse indefinitely until obstacles change |
| Flyers | none | Straight line (DC already does that) |
| Crowds in a choke | field + local steering + reservation | No planner fixes congestion on its own |

Flow fields also give "fluid" motion for free if you **sample the field
bilinearly** instead of hopping cell to cell: the gradient is smooth between cell
centres, so units curve around corners rather than zig-zag. That is the cheap
route to the StarCraft feel without a region graph.

Weak spots of flow fields, all visible in the existing code: memory (one int per
cell per goal: 64 KB for 256x256 at 16-bit, 256 KB at 32-bit, per distinct goal),
**staleness** when buildings change the grid (the present invalidation gap), and
that **a field has no notion of a moving obstacle**, so crowd handling must live
in layer C.

---

## 5. Proposed architecture

```text
play/nav/
  nav_grid.[ch]     walkable + per-class speed/slope layers, version counter, region labels
  nav_search.[ch]   goal-rooted Dijkstra/A* on a bucket queue; modes: EARLY_EXIT | FULL_FIELD
  nav_field.[ch]    (class, goal, grid_version) cache; invalidation; LRU cap
  nav_route.[ch]    route extraction, string-pulling (optional), octile smoothing
  nav_steer.[ch]    locomotion executors: CELL_HOP | FREE_STEER
  nav_crowd.[ch]    occupancy: CELL_CLAIM | RADIUS (spatial hash), yielding, detour
  nav_profile.h     per-game policy struct (below)
games/<game>/nav_profile.c   data only (+ DC retail backend hook)
```

### 5.1 The profile struct (this is where "flags per game" live)

```c
typedef struct {
    /* planning */
    uint8_t  connectivity;          /* 8 (all games); 4 available for debugging */
    bool     cut_corners;           /* false in all known games: need an open orthogonal */
    const int *step_cost;           /* 10/14 octile, or DC's 9x9 direction table */
    int      max_route_cells;       /* DC: 32, others: unlimited */
    int      search_budget;         /* pops per tick; DR time-slices */
    bool     use_regions;           /* flood-fill regions for early-out + goal relocation */
    /* locomotion */
    enum { NAV_CELL_HOP, NAV_FREE_STEER } executor;
    uint8_t  heading_quantum;       /* 8, 16, 0 = continuous (limits *movement* heading) */
    bool     only_45;               /* alias: executor==CELL_HOP && quantum==8 */
    bool     smooth_routes;         /* string-pull + bilinear field sampling */
    /* crowd */
    enum { NAV_CLAIM_CELL, NAV_RADIUS } occupancy;
    bool     replan_on_block;
    /* determinism / compatibility */
    bool     retail_exact;          /* use the game's ported backend, not the generic one */
} nav_profile_t;
```

### 5.2 How the existing code maps in

- `DC_FindPath` becomes the `retail_exact` backend for DC. It does not get
  deleted: it stays as the oracle that the generic engine is regression-tested
  against (Section 8).
- `build_flow_field` becomes `nav_search` in `FULL_FIELD` mode, replacing the
  linear-scan open list with the same 256-bucket queue DC already uses (DR uses a
  binary heap; both are fine, buckets are simpler with small integer costs).
- `P_FlowFieldTarget` + `line_walkable` becomes `nav_route` string-pulling.
- `move_unit_if_walkable`'s axis sliding becomes part of `nav_crowd`.
- The `#ifdef RTS_GAME_DARK_COLONY` blocks in `play/p_map.c` and
  `play/p_mobj.c` are replaced by profile switches, so the engine stops having
  a DC-shaped hole in it.

### 5.3 Determinism

`docs/NETWORK.md` already flags float determinism as an open concern (line ~182),
and these games are lockstep. The planner must therefore be **integer only**
(costs, heuristic, tie-breaks, iteration order). DR's float heuristic is the one
place where exactness matters and integers are easy to substitute (octile
distance in tenths). Locomotion can stay in the existing fixed-point positions
(`fixed3_t`).

---

## 6. Making the 45° question a flag

"Only 45°" is actually three separable things:

| Layer | What changes | Cost of supporting both |
| --- | --- | --- |
| **Planner** | *Nothing.* All games plan on an 8-connected grid. `ONLY_45` does **not** change which cells are chosen | zero |
| **Executor** | CELL_HOP: go cell centre to cell centre, diagonal-then-axial (what `tick_actor` does for DC at `p_mobj.c:1138`). FREE_STEER: head for the farthest visible waypoint or follow the bilinear field, turn-rate limited | one branch in `tick_actor`, lifted into `nav_steer` |
| **Sprites** | None. Sprite facing is already quantised from `core.angle` by `angle_to_direction`, independent of how it was produced | zero |

So the real cost is pulling the executor out of `tick_actor` into its own module.
Two subtleties:

1. **8-connected grid paths are octile-suboptimal** (up to about 8% longer than a
   straight line). With `FREE_STEER` you must string-pull or the units look
   obviously grid-bound: they'd walk an L even though they could cut straight.
   The engine already does a form of this, so the machinery exists.
2. **A smooth executor under a cell-claim crowd model breaks.** DC claims the
   destination cell before stepping (`0x414f8b`). Free movement has units in
   between cells, so it needs radius collision. `occupancy` and `executor` are
   not independent; the profile must reject combinations that don't make sense
   (assert at load).

Practical default suggestion: DC = `CELL_HOP + CLAIM_CELL`, DR =
`FREE_STEER + RADIUS` (pending the executor disassembly), KKnD = `FREE_STEER` with
`heading_quantum` 8/16 until the original is understood, 7th Legion = whatever
its tests already assume.

---

## 7. Is it worth it?

**Benefits**

- Fixes real defects in the non-DC games (O(N^2) search, no terrain cost, stale
  fields, no crowd logic) in one place rather than four.
- Removes the `#ifdef` in the movement core. A third game adopting the engine
  (your stated goal) inherits movement instead of reinventing it.
- Gives DR a real model of **terrain speed and slope**, which it currently lacks
  and which the game data provides.
- Network determinism gets one audit target instead of several.

**Costs and risks**

- **Abstraction tax.** `ARCHITECTURE.md` says the design is "deliberately ...
  close to the original game" and self-contained per game. A profile-driven nav
  core pulls the other way. Mitigate by keeping each game's retail behaviour
  reachable (`retail_exact`) and by treating the profile as *data*, not as a
  plugin registry.
- **DC parity is fragile.** It took bit-exact work (bucket order, LIFO ties,
  `random()%3-1`). The unified core must not silently change DC; the regression
  tests (`test_native_pathfinding`, `test_bottleneck_movement`,
  `test_flow_field_movement`) must stay green at every step.
- **Evidence gap** for DR's executor and KKnD's planner. Building a generic core
  first and "calibrating" later is fine; claiming fidelity before then is not.
- **One algorithm will not reproduce all four games' *quirks*.** Their quirks are
  mostly what players remember (DC's clumping, DR's traffic jams). If the aim is
  "feels like the original" per game, the quirks live in layers B and C, which is
  exactly where the games differ. Sharing layer A buys correctness and speed, not
  feel.

**My recommendation:** yes, but scoped as *"shared planner + shared grid +
pluggable locomotion/crowd"*. Do not attempt to unify B and C across all games
before the evidence exists.

---

## 8. Staged plan

| Phase | Work | Risk | Gate |
| --- | --- | --- | --- |
| **0** | Benchmarks for `build_flow_field` on 256x256; golden-route tests for DC | low | numbers recorded; DC tests green |
| **1** | Extract `nav_grid` (classes, version counter, flood-fill regions) and `nav_search` (bucket queue). Replace O(N^2) scan. Add invalidation on building place/destroy | low | existing shared tests green; field build >10x faster |
| **2** | Put DC behind `retail_exact` using the new interfaces; delete the `#ifdef` in `p_map.c` | medium | all DC tests bit-identical |
| **3** | DR classes from `TRNEFF.TXT` (speed %, slope), cost = base / speed; A\* single-unit mode; per-team time slicing | medium | DR harvest/transport tests; new terrain-cost tests |
| **4** | Extract executor to `nav_steer`; add bilinear field sampling and string-pulling; `only_45` becomes a profile field | medium | DC unchanged; DR units curve around corners |
| **5** | Crowd: spatial hash, radius collision, yielding, repath on block; replaces `yield_harvest_bay` hack | high | bottleneck + docking tests; new crowd tests |
| **6** | Disassemble DR executor (`0x42a2e0` and its callers) and locate KKnD's pathfinder; tune profiles | research | new entries in `docs/*_EXE_FINDINGS.md` |

Phases 0 to 2 are pure refactor plus a perf fix and carry most of the payoff. 3 to 5
are where the gameplay changes.

---

## 9. What I could not establish

- **DR executor** (continuous vs. cell hop) and its **heuristic**.
- **KKnD's pathfinder**: not in the parts of the OpenKKND decompile I read;
  `KKND.EXE` has no path strings. Needs a targeted search from
  `entity_mode_move_attack`.
- **Whether stale flow fields are actually a bug** in this repo (no
  invalidation found, not tested).
- All **StarCraft** details (from memory, not source).
- I did not build or run anything for this essay; the performance claim about the
  O(N^2) open list is from reading the loop, not from a measurement.

Suggested first step, if you want to proceed: Phase 0 + 1 (benchmark, then
`nav_search`), since they're low-risk and don't change any game's behaviour.
