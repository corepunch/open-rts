# Dark Colony module boundaries

The active `level_t` owns the `Mission` aggregate through `mission` and
`destroy_mission`. The driver does not own or pass a second mission object.

## Simulation

- `p_spec.c`: mission allocation, destruction, and ticker orchestration. Each
  active tick runs AI, then script commands, then dropships, preserving the
  previous order. Mission completion freezes subsequent mission ticks.
- `p_script.c` / `p_script.h`: private script state, MSG/TRO parsing, city-slot
  conditions, command evaluation, and mission outcome. Script commands submit
  copied payload requests through `DC_StartDropship`; they cannot access ship
  internals.
- `p_ai.c` / `p_ai.h`: attack policy, target selection, and harvester assignments.
  Timers and the retained wave target belong to an `AiState` per mission.
- `p_drop.c` / `p_drop.h`: private bounded storage for eight dropship mobjs,
  payload queues, phase transitions, movement, and effect-part lifetimes. The
  level-owned mission owns this subsystem. State actions resolve it through the
  active level; no mission pointer is stored on a mobj. Approach, reposition,
  and departure share one flight-entry helper.
- `p_reinforce.c` / `p_reinforce.h`: native script type mapping and reinforcement
  spawning, shared by direct script commands and dropship unloading.
- `p_prod.c`, `p_inter.c`, and `sb_bar.c`: production, interaction, and custom UI.

This refactor retains existing state/action signatures, timings, animation
conversion formulas, AI policy, and script interpretation. It does not claim
that those behaviors are all verified retail behavior. In particular, the
current engine still passes `statecontext_t *` to world actions; migrating the
shared action ABI to the documented Hexen-style contract is separate work.

## Asset loading

- `w_juice.c`: SPR palette/cell decoding and decoded-storage destruction.
- `w_fin.c`: FIN parsing, label/command queries, and decoded-storage destruction.
- `w_sprite_paths.c`: SPR/FIN path conversion and dependency-name resolution.
- `w_spr.c`: conversion to engine-owned source images and sprite frame/layer
  definitions, plus palette/render-table application.
- `w_sprite_cache.c`: asset enumeration and recursive dependency caching. A
  successfully loaded sheet enters the cache before dependencies are followed,
  preserving cycle termination. Failed dependencies retain the existing error
  behavior; this is not a transactional cache loader.
- `w_drop.c`: conversion of FIN labels into dropship animation data, independent
  of SDL sprite-cache construction.
- `w_sprite_private.h`: loader-only decoded types and cross-file helpers. Native
  SPR/FIN allocations are temporary. `DC_LoadSpriteWithAnimation` transfers FIN
  ownership to its caller when requested; the cache frees it after following
  dependencies. Renderer structures retain engine images and definitions only.

The build discovers all sources automatically; no per-file Makefile list is
needed. Generated state tables and their Reaper timings are unchanged.

## Refactor verification

`make` and the dummy-video Dark Colony `--check` pass. The dropship lifecycle,
AI, flow-field, render-helper, sprite-definition, and sprite-layout checks pass.
`test_mission_ownership` additionally checks payload copying, independent level
lifetimes/ticking, bounded ship capacity, and reuse after departure without
retail assets.

`env SDL_VIDEODRIVER=dummy make test-dark-colony test-layout` still reports three
existing failures, also reproduced from the pre-refactor HEAD in an isolated
source tree: combat muzzle-flash visibility, headless gameplay sprite catalog,
and unlinked actor spawn-action initialization. These failures are not fixed by
moving subsystem boundaries.
