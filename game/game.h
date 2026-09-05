#ifndef __GAME__
#define __GAME__

/*
 * Doom-style game interface.  Every game binary defines these externs in its
 * own games/{GameDir}/plugin.c (or game.c).  The engine calls them by name —
 * no vtable, no dlopen, no registry.
 *
 * Include this header from both the engine side (engine calls) and the game
 * side (game implements).
 */

#include "engine.h"
#include "ui_definition.h"
#include "g_game.h"

/* ── game identity ─────────────────────────────────────────────────────── */
extern const char *const g_game_id;
extern const char *const g_game_name;
extern const char *const g_game_default_root;
extern const char *const g_game_default_map;
extern const char *const g_game_default_sprite;
extern const int g_cell_w;
extern const int g_cell_h;
extern const uint16_t g_debug_enemy_type;
extern const gameinfo_t *const gameinfo;
extern const actortype_t *const mobjinfo;
extern const int num_mobjinfo;
extern const uidefinition_t *const gameui;   /* NULL if unused */

/* ── game functions ────────────────────────────────────────────────────── */

/* Convert native game metadata into the engine's canonical runtime format. */
void     G_InitGame(void);

/* Load the map at path into *out.  Returns true on success. */
bool     G_DoLoadLevel(const char *path, level_t *out);

/* Load tile/sprite assets into tileset and unit_sprite. */
bool     W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                      const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite);

/* Populate mobjs[] with initial units from the map file.
   Returns the number of mobjs spawned, or 0 on none/error. */
int      P_LoadThings(const char *path, mobj_t *mobjs, int max);

/* Load per-unit sprites into cache after units are known.
   Returns true on success (partial loads are allowed). */
bool     R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                       const mobj_t *mobjs, int count, spritecache_t *cache);

/* Load the game UI font into *font.  Returns false if the game has no font. */
bool     HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font);

/* Advance mission state by dt seconds. */
void     G_MissionTicker(level_t *map, mobj_t *mobjs, int *count,
                         effect_t *effects, int max_effects, hudtext_t *hud, float dt);

/* Return mission state: 0=active, 1=won, 2=lost, 3=ally_lost. */
int      G_MissionState(const level_t *map);

/* ── custom interactive UI / sidebar hooks ─────────────────────────────── */

/* Initialize game-specific interactive UI/sidebar. Returns an opaque pointer, or NULL if none. */
void    *G_InitCustomUI(app_t *app, const char *data_root);

/* Handle input events for custom UI. Returns true if handled. */
bool     G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                             mobj_t *units, int unit_count, const SDL_Event *event);

/* Advance custom UI state by one tick. */
void     G_CustomUITicker(void *ui);

/* Draw custom UI overlay/sidebar. */
void     G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                          const mobj_t *units, int unit_count,
                          const spritecache_t *sprites, const hudtext_t *hud);

/* Advance game-specific production queues in interactive mode. Returns true if a unit was spawned. */
bool     G_UpdateProduction(void *ui, level_t *map, mobj_t *units, int *unit_count,
                            effect_t *effects, int max_effects, float dt);

/* Shutdown and free custom UI. */
void     G_ShutdownCustomUI(void *ui);

/* Return the effective world viewport width in screen pixels. */
int      G_WorldViewportWidth(const app_t *app);

/* ── headless model hooks ───────────────────────────────────────────────── */

/* Query static products supported by the game. Returns count. */
int      G_ModelGetProducts(const RtsGameModel *model, int owner,
                            StaticProductDefinition *out, int max_products);

/* Query alien production products. Returns count. */
int      G_ModelAlienProducts(StaticProductDefinition *out, int max_products);

/* Lookup a static product definition by its UI ID. */
const StaticProductDefinition *G_ModelProductByUIId(const RtsGameModel *model, int ui_id);

/* Lookup a static product definition by class and product type. */
const StaticProductDefinition *G_ModelProductByClassType(const RtsGameModel *model,
                                                         int product_class,
                                                         int product_type);

/* Check if prerequisites are satisfied for an owner to build a product. */
bool     G_ModelProductAvailable(const RtsGameModel *model, int owner,
                                 const StaticProductDefinition *product);
bool     G_ModelProductAvailableForUnits(const mobj_t *units, int unit_count,
                                          const StaticProductDefinition *product);

/* Find the producer mobj index for a product. Returns -1 if no eligible producer. */
int      G_ModelFindProducerIndex(const RtsGameModel *model, int owner,
                                  const StaticProductDefinition *product);

/* Lookup actor type ID corresponding to a product definition. */
uint16_t G_ModelActorIdForProduct(const StaticProductDefinition *product);

/* Initial frame and state for spawned building products. */
int      G_ModelBuildingFrameForProduct(const StaticProductDefinition *product);
int      G_ModelBuildingStateForProduct(const gameinfo_t *game_info,
                                        const StaticProductDefinition *product);

/* Product training / build time in milliseconds. */
int      G_ModelProductTrainingTimeMs(const StaticProductDefinition *product);

/* Game-specific release animation and placement hooks (e.g. Dark Colony barracks release). */
bool     G_ModelStartProductionRelease(RtsGameModel *model, mobj_t *producer,
                                       const StaticProductDefinition *product,
                                       uint16_t actor_id);
bool     G_ModelSpecialReleaseSpawnPoint(const RtsGameModel *model, const mobj_t *producer,
                                         const StaticProductDefinition *product,
                                         const mobj_t *new_unit,
                                         float *out_gx, float *out_gy);

/* Check if a player/owner currently possesses an alive unit of actor_id. */
bool     G_ModelHasActorType(const RtsGameModel *model, int owner, uint16_t actor_id);

/* Emit declarative UI script from model and snapshot state. */
void     G_ModelBuildUIScript(const RtsGameModel *model,
                              const RtsRenderSnapshot *snapshot,
                              char *dst, size_t dst_size);

/* Periodic AI production thinker. */
void     G_ModelAIProduction(RtsGameModel *model, int elapsed_ms);

#endif
