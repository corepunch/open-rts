# Dark Reign module boundaries

Documented September 30, 2026 against `047477a`. This describes current C
ownership and integration, not the complete native object layout. Retail
evidence and unresolved behavior are indexed in [DR_DISASSEMBLY.md](DR_DISASSEMBLY.md).

## Level and mission ownership

The global active `level_t` owns `dr_mission_t` through `mission` and
`destroy_mission`. `w_map.c:load_mission()` allocates it; `P_FreeLevel()`
releases it. The driver/model has no parallel mission owner.

The mission stores scenario technology level, ordered product IDs and required
tech levels, and a flexible bay array indexed by engine mobj type. Unavailable
bays initialize to `(-1,-1)`. The loader resolves retail `SetType` IDs through
`mobjinfo[].doomednum` and reads each building's `SetBay` into that array.
The product list currently has a fixed 64-entry limit; this is distinct from
the dynamically allocated combined faction HUD icon catalog. Full native
definition capacity and campaign definition swaps are not certified.

## Native data and simulation

| Source | Responsibility |
|---|---|
| [w_map.c](../games/dark-reign/w_map.c) | SCN/MAP loading, definition resolution, tileset detection, decorations, native collision masks, extractors, team credits and level-owned mission |
| [g_game.c](../games/dark-reign/g_game.c) | Game identity, authored `mobjtype_t` balance/traits, AI profiles, loader hooks and interface integration |
| [dr_types.h](../games/dark-reign/dr_types.h) | Native numeric actor IDs, `dr_mission_t`, AI profile data and game declarations |
| [p_prod.c](../games/dark-reign/p_prod.c) | Product costs, prerequisites, makers, native training times, actor lookup and production-goal interface |
| [p_harvest.c](../games/dark-reign/p_harvest.c) | Water/Taelon receiver compatibility and mission-bay coordinate conversion |
| [play/p_mobj.c](../play/p_mobj.c) | Shared allocation, state actions, movement, attack/support targeting, resource transfer, cargo and borrowed-target cleanup |
| [play/p_tick.c](../play/p_tick.c) | Shared deterministic simulation lifecycle |
| [play/p_waypoint.c](../play/p_waypoint.c) | Shared one-pass, loop and backtrack execution in the ordinary mobj thinker |
| [game/g_cmd.c](../game/g_cmd.c) | Stable-ID group commands, atomic routes, ownership validation and deterministic checksums |
| [play/p_ai.c](../play/p_ai.c) | Shared AI integration; not a complete port of retail AIP scheduling |

World objects are individually allocated `mobj_t` instances linked through
global `thinkercap`. Visual effects share that lifecycle. State actions take
only `mobj_t *`; there is no per-action context stack. Renderer/UI snapshots
borrow object pointers; they do not own simulation storage. The per-game
[mobj_data.h](../games/dark-reign/mobj_data.h) currently adds no private fields.

**Confirmed engine behavior:** `P_ApplyActorTypeDefaults()` sets the actual
runtime actor traits and stats. `info.c` alone does not establish fresh-spawn
behavior. Flight, human, harvesting and manual-targeting flags are therefore
audited in both the authored table and generated type table.

**Remaining native boundary:** original DK object storage, full pathfinder,
passenger/attachment ownership, mission FSMs and special-ability lifecycle
have not been fully reproduced. `G_MissionTicker()` is currently empty; loading
SCN start data is not equivalent to executing the entire retail campaign.

## Native assets and generated states

| Source | Responsibility |
|---|---|
| [w_spr.c](../games/dark-reign/w_spr.c) | Native SPR/FTG loading, palette/translation handling, common source-image conversion and building layer composition |
| [w_til.c](../games/dark-reign/w_til.c) | Native terrain tile loading |
| [tools/dr_info_gen.c](../tools/dr_info_gen.c) | C generator for sprite names, state IDs, animation chains and type-entry points |
| [info.c](../games/dark-reign/info.c) / [info.h](../games/dark-reign/info.h) | Generated Doom-style `sprnames[]`, `states[]`, `mobjinfo[]`, enums and game info |

Native retail assets under `data/REIGN/dark` are the runtime source. OpenDR's
pinned sequence metadata supports authored animation mapping; it does not
override retail docking, palette, collision or executable evidence.

Game loaders convert native assets into engine-owned common source images and
frame/rotation definitions. Renderer-owned hardware resources have their own
lifetime. UI/menu sprites remain separate from world gameplay sprite IDs.
No per-state rendering override or opaque native callback is required in the
renderer to reproduce the audited paths.

Unit balance is authored as C literals, not loaded from native balance text at
spawn time. Map loading still reads definitions for native type/visual lookup,
scenario technology, bays and footprints. The two uses must not be confused.
See [DR_INFO_GEN.md](DR_INFO_GEN.md) for the existing tool workflow.

## HUD and menu

| Source | Responsibility |
|---|---|
| [hud/sb_bar.c](../games/dark-reign/hud/sb_bar.c) | The HUD table: native chrome, fonts, MFD pages, product grid, radar and tooltips; MENU opens the shell's options screen |
| [hud/m_menu.c](../hud/m_menu.c) | Engine item input, map targets, minimap centring and drawing |
| [hud/hu_bar.c](../hud/hu_bar.c) | Shared route book: editing, saved routes and atomic Go dispatch |
| [driver/w_image.c](../driver/w_image.c) | Engine BMP/PCX decoding |
| [menu/w_shell.c](../games/dark-reign/menu/w_shell.c) | SHELL.RLI/RLD archive, LZSS, TLF images and strip fonts |
| [menu/m_menu.c](../games/dark-reign/menu/m_menu.c) | Outer shell, campaign mission map and briefing, credits, options screen; `G_InitMenus`/`G_ControlPanel` |
| [menu/m_multi.c](../games/dark-reign/menu/m_multi.c) | Multiplayer connection, LAN browser, manual IP and the game-setup (Chat/Instant Action) panel |
| [menu/m_skirmish.c](../games/dark-reign/menu/m_skirmish.c) | Game-setup handoff from the menu to the next level's loader |

`G_InitHUD` returns one `menu_t` table and the engine drives it; `drhud`
holds the page, the product page and the route book. Native layout, glyph
interpretation and translation stay in the game's `hud/` directory. Route
editing and execution belong to the engine. `mobj_t` owns one `waypoints_t`;
there is no per-game route ticker or parallel object store. UI drafts and
saved paths are copied into a single TC_PATH command when Go is pressed.

Both factions share one dynamically allocated menu-icon catalog. The selected
FG or Imperium rig chooses structure products; other selections use the unit
list, with loaded technology and producer availability checks. This shared
catalog does not make COMMS/ORDERS/SPECIAL, upgrade or decoy controls
functional. The [HUD disassembly report](DR_HUD_DISASSEMBLY.md) identifies the
native routines and each remaining gap.

## Verification

`047477a` passed the build, generated-table comparison, 58 Dark Reign/Dark
Colony/7th Legion test executables, both model-command tests and four games'
headless smoke checks. These establish the implemented boundaries; they do not
certify the complete native architecture or all abilities.

```sh
make
env SDL_VIDEODRIVER=dummy make test-dark-reign
env SDL_VIDEODRIVER=dummy build/bin/dark-reign --check
```

The build discovers source files with sorted `find` output. Any future C
module must be included through its directory, without an individual Makefile
source list.
