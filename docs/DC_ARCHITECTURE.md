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

- `w_fin.c`: FIN parsing, label/command queries, and decoded-storage destruction.
- `w_sprite_paths.c`: SPR/FIN path conversion and dependency-name resolution.
- `w_spr.c`: one SPR file buffer, a direct cell-by-cell pass into final indexed
  lumps, and per-cell texture creation. Raw pixels are copied once into their
  final storage; compressed skip/literal runs decode there directly. There are
  no decoded cell objects, temporary atlases, or parallel metadata arrays.
  A single per-cell RGBA scratch buffer supplies texture uploads; frames without
  team-color pixels use their base texture for every team. FIN frame/layer
  definitions and palette/render-table application remain here.
- `w_sprite_cache.c`: asset enumeration and recursive dependency caching. A
  successfully loaded sheet enters the cache before dependencies are followed,
  preserving cycle termination. Failed dependencies retain the existing error
  behavior; this is not a transactional cache loader.
- `w_drop.c`: conversion of FIN labels into dropship animation data, independent
  of SDL sprite-cache construction.
- `w_sprite_private.h`: loader-only FIN types and cross-file helpers. The SPR
  buffer is freed on return. `DC_LoadSpriteWithAnimation` transfers FIN
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

## Direct SPR loader verification

`test_sprite_loading` exercises raw and compressed cells, transparent skips,
team-color textures, empty cells, truncation, oversized chunks, invalid runs,
and cleanup after partial loading or texture creation failure. It also accepts
a manifest path to fingerprint a local SPR catalog through SDL software rendering:

```sh
rg --files data/DCOLONY | rg '\.SPR$' | sort > /private/tmp/dc-sprites.txt
make build/bin/tests/dark-colony/test_sprite_loading
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading
env SDL_VIDEODRIVER=dummy build/bin/tests/dark-colony/test_sprite_loading \
    /private/tmp/dc-sprites.txt > /private/tmp/dc-sprite-fingerprints.txt
```

Run the same probe with the old loader (`e7073a6`) to compare revisions on the
same machine and data. The fingerprint includes indices, default and all eight
remapped SDL pixel buffers (using the base texture when no translation exists),
palettes, bounds, displacements, ground points, and FIN frame/rotation/layer
metadata. This is a same-build comparison, not a portable on-disk hash format.
The September 9, 2026 comparison had 282 identical successful files and two
identical rejections out of 284 files. The manifest-order fingerprint output
SHA-256 was `21274462e4934e11060b85fcb554ac3239d97f5a17c978498d26b5c6445dcd5e`
for both loaders. The dummy-video HUMAN01 screenshots were byte-identical.

An optional ASan/UBSan build of the focused loader test stalled with macOS
service errors and was terminated without a test result. No sanitizer pass is
claimed; the ordinary fixture, catalog, and screenshot checks completed.
